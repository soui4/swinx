#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "tostring.h"
#include "debug.h"
#define kLogTag "version"

/* VERSION.dll 的 POSIX 实现。
 *
 * 版本信息只存在于 PE 镜像的 RT_VERSION 资源里，是一棵 VS_VERSIONINFO 树。
 * 这里做两件事：把文件按 PE 镜像读一遍（MZ -> PE 头 -> 数据目录 -> 段表 ->
 * 资源目录），然后在树里按路径取节点。
 *
 * 为什么不复用 coffparser（WindResResourceParser）：它解析的是 COFF **目标文件**
 * （.o/.obj），文件开头就是 COFF 头；PE 镜像前面还有 DOS 头、PE 头、可选头，
 * 资源段位置要走数据目录。两者布局不同，硬套只会得到"找不到资源"。
 *
 * 对 ELF 等非 PE 文件，本函数如实返回"没有版本资源"（GetFileVersionInfoSize
 * 返回 0），而不是编一份版本号出来 —— POSIX 上的 .so 确实没有这种资源。
 *
 * ⚠️ 宽度：资源里的字符串恒为 UTF-16，而 swinx 的 WCHAR 在 POSIX 上是 4 字节。
 * 所以字符串值必须先解码到 WCHAR 缓冲再返回，绝不能让调用方按 WCHAR* 去读
 * 资源里的 UTF-16 字节——那会在每个字符后多吞两个字节。
 *
 * 返回的缓冲区用 thread_local 保存，有效期到本线程下一次 VerQueryValue 调用。
 * Win32 保证的"节点在 pBlock 生命周期内有效"在 4 字节 WCHAR 下无法原样满足
 * （除非把整棵树转码复制一遍），thread_local 缓冲是这个约束下最接近的语义：
 * WebKit 的用法是查完立刻拷进 String，不受影响。
 */

namespace
{

struct VersionNode
{
    const uint8_t *start; // 节点头
    uint16_t length;      // wLength，整个节点（含子节点）的字节数
    uint16_t valueLength; // wValueLength，含义随 wType 变
    uint16_t type;        // wType: 0 = 二进制, 1 = 文本
    const uint8_t *value;
    const uint8_t *children;
    const uint8_t *end;
};

inline size_t align4(size_t n)
{
    return (n + 3) & ~(size_t)3;
}

inline uint16_t readWord(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

inline uint32_t readDword(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

// 资源里的键是 UTF-16LE，且没有 BOM。按码元解出成 WCHAR（POSIX 上是 UTF-32）。
std::wstring decodeUtf16Key(const uint8_t *p, const uint8_t *end)
{
    std::wstring result;
    while (p + 1 < end)
    {
        uint16_t unit = readWord(p);
        p += 2;
        if (unit == 0)
            break;
        // 代理对：合成一个码点，与 4 字节 WCHAR 对齐
        if (unit >= 0xD800 && unit <= 0xDBFF && p + 1 < end)
        {
            uint16_t low = readWord(p);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                p += 2;
                result.push_back((wchar_t)(0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00))));
                continue;
            }
        }
        result.push_back((wchar_t)unit);
    }
    return result;
}

bool parseNode(const uint8_t *node, const uint8_t *blockEnd, VersionNode &out)
{
    if (node + 6 > blockEnd)
        return false;

    out.start = node;
    out.length = readWord(node);
    out.valueLength = readWord(node + 2);
    out.type = readWord(node + 4);
    if (out.length < 6 || node + out.length > blockEnd)
        return false;
    out.end = node + out.length;

    // 跳过 szKey（UTF-16，以 0 结尾）
    const uint8_t *key = node + 6;
    const uint8_t *p = key;
    while (p + 1 < out.end && readWord(p) != 0)
        p += 2;
    if (p + 1 >= out.end)
        return false;
    p += 2;

    // 值按 4 字节对齐
    out.value = out.start + align4((size_t)(p - out.start));
    size_t valueBytes = out.type == 1 ? (size_t)out.valueLength * 2 : (size_t)out.valueLength;
    if (out.value + valueBytes > out.end)
        valueBytes = out.end > out.value ? (size_t)(out.end - out.value) : 0;
    out.children = out.value + align4(valueBytes);
    if (out.children > out.end)
        out.children = out.end;
    return true;
}

// 子节点名与查询路径的比较规则：不分大小写（与 Win32 一致）
std::wstring lowerCase(const std::wstring &s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= L'A' && out[i] <= L'Z')
            out[i] = (wchar_t)(out[i] + 32);
    }
    return out;
}

bool equalsIgnoreCase(const std::wstring &a, const std::wstring &b)
{
    return a.size() == b.size() && lowerCase(a) == lowerCase(b);
}

bool readWholeFile(const std::string &path, std::vector<uint8_t> &data)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size <= 0)
    {
        fclose(f);
        return false;
    }
    rewind(f);

    data.resize((size_t)size);
    size_t read = fread(data.data(), 1, data.size(), f);
    fclose(f);
    if (read != data.size())
        return false;
    return true;
}

/* ---- 最小 PE 镜像读取 ---- */

struct PeImage
{
    const uint8_t *base;
    size_t size;
    uint32_t resourceRva;
    uint32_t resourceSize;
    uint32_t sectionTable;
    uint16_t sectionCount;
};

// RVA -> 文件偏移。资源目录里的所有偏移都是 RVA，而文件里是原始偏移。
bool rvaToOffset(const PeImage &pe, uint32_t rva, uint32_t &offset)
{
    for (uint16_t i = 0; i < pe.sectionCount; ++i)
    {
        const uint8_t *section = pe.base + pe.sectionTable + (size_t)i * 40;
        uint32_t virtualSize = readDword(section + 8);
        uint32_t virtualAddress = readDword(section + 12);
        uint32_t rawSize = readDword(section + 16);
        uint32_t rawOffset = readDword(section + 20);
        uint32_t span = virtualSize ? virtualSize : rawSize;
        if (rva >= virtualAddress && rva < virtualAddress + span)
        {
            uint32_t delta = rva - virtualAddress;
            if (rawOffset + delta >= pe.size)
                return false;
            offset = rawOffset + delta;
            return true;
        }
    }
    return false;
}

bool openPeImage(const std::vector<uint8_t> &image, PeImage &pe)
{
    // DOS 头：'MZ' + e_lfanew(0x3C) 指向 PE 签名
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z')
        return false;

    uint32_t peOffset = readDword(image.data() + 0x3C);
    if (peOffset + 24 > image.size() || memcmp(image.data() + peOffset, "PE\0\0", 4) != 0)
        return false;

    pe.base = image.data();
    pe.size = image.size();
    pe.sectionCount = readWord(image.data() + peOffset + 6);
    uint16_t optionalHeaderSize = readWord(image.data() + peOffset + 20);
    pe.sectionTable = peOffset + 24 + optionalHeaderSize;
    if (pe.sectionTable > image.size() || pe.sectionTable + (size_t)pe.sectionCount * 40 > image.size())
        return false;

    // 可选头里的 DataDirectory[2] 就是资源目录。PE32 从偏移 96 开始，
    // PE32+ 从 112 开始；两者的区别只在前面那几个 8 字节字段。
    const uint8_t *optionalHeader = image.data() + peOffset + 24;
    if (optionalHeaderSize < 96)
        return false;
    uint16_t magic = readWord(optionalHeader);
    size_t directories = (magic == 0x20B) ? 112 : 96;
    if (optionalHeaderSize < directories + 16)
        return false;

    pe.resourceRva = readDword(optionalHeader + directories + 2 * 8);
    pe.resourceSize = readDword(optionalHeader + directories + 2 * 8 + 4);
    return pe.resourceRva != 0 && pe.resourceSize != 0;
}

// 资源目录三级：类型 -> 名字 -> 语言。每级都是
// IMAGE_RESOURCE_DIRECTORY + 若干 IMAGE_RESOURCE_DIRECTORY_ENTRY。
struct ResourceLeaf
{
    uint32_t dataRva;
    uint32_t size;
};

// 在某一级里按 id 找条目；id 为 -1 表示"随便取第一个"。
bool findDirectoryEntry(const PeImage &pe, const uint8_t *dir, uint32_t dirOffset, int32_t id,
                        uint32_t &nextOffset, bool &isDirectory)
{
    if (dirOffset + 16 > pe.resourceSize)
        return false;

    const uint8_t *header = dir + dirOffset;
    uint16_t named = readWord(header + 12);
    uint16_t ids = readWord(header + 14);
    uint32_t total = (uint32_t)named + ids;

    const uint8_t *entries = header + 16;
    if (dirOffset + 16 + total * 8 > pe.resourceSize)
        return false;

    for (uint32_t i = 0; i < total; ++i)
    {
        uint32_t name = readDword(entries + i * 8);
        uint32_t data = readDword(entries + i * 8 + 4);
        bool namedEntry = (name & 0x80000000u) != 0;
        uint32_t entryId = name & 0xFFFFu;
        if (id >= 0 && (namedEntry || (int32_t)entryId != id))
            continue;

        isDirectory = (data & 0x80000000u) != 0;
        nextOffset = data & 0x7FFFFFFFu;
        return true;
    }
    return false;
}

bool findVersionLeaf(const PeImage &pe, ResourceLeaf &leaf)
{
    uint32_t resourceOffset = 0;
    if (!rvaToOffset(pe, pe.resourceRva, resourceOffset))
        return false;

    const uint8_t *dir = pe.base + resourceOffset;

    uint32_t typeDir = 0;
    bool isDir = false;
    if (!findDirectoryEntry(pe, dir, 0, 16 /* RT_VERSION */, typeDir, isDir) || !isDir)
        return false;

    uint32_t nameDir = 0;
    if (!findDirectoryEntry(pe, dir, typeDir, 1, nameDir, isDir) || !isDir)
    {
        // 名字不叫 1 的版本资源也存在，退一步取第一个
        if (!findDirectoryEntry(pe, dir, typeDir, -1, nameDir, isDir) || !isDir)
            return false;
    }

    uint32_t langDir = 0;
    bool langIsDir = false;
    if (!findDirectoryEntry(pe, dir, nameDir, -1, langDir, langIsDir) || langIsDir)
        return false;

    // 三级之后就是 IMAGE_RESOURCE_DATA_ENTRY { OffsetToData, Size, CodePage, Reserved }
    if (langDir + 16 > pe.resourceSize)
        return false;
    leaf.dataRva = readDword(dir + langDir);
    leaf.size = readDword(dir + langDir + 4);
    return leaf.size != 0;
}

// 抽出 RT_VERSION 里那份版本资源
bool extractVersionResource(const wchar_t *fileName, std::vector<uint8_t> &resource)
{
    if (!fileName)
        return false;

    std::string path;
    tostring(fileName, -1, path);

    std::vector<uint8_t> image;
    if (!readWholeFile(path, image))
    {
        SLOG_FMTW("cannot read %s", path.c_str());
        return false;
    }

    PeImage pe;
    if (!openPeImage(image, pe))
    {
        // ELF（POSIX 上的 .so/可执行文件）本来就没有版本资源，不是错误路径
        SLOG_FMTW("%s is not a PE image, no version resource", path.c_str());
        return false;
    }

    ResourceLeaf leaf;
    if (!findVersionLeaf(pe, leaf))
        return false;

    uint32_t offset = 0;
    if (!rvaToOffset(pe, leaf.dataRva, offset) || offset + leaf.size > image.size())
        return false;

    resource.assign(image.begin() + offset, image.begin() + offset + leaf.size);
    return true;
}

} // namespace

DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
    if (lpdwHandle)
        *lpdwHandle = 0;

    std::vector<uint8_t> resource;
    if (!extractVersionResource(lptstrFilename, resource))
    {
        SetLastError(lptstrFilename ? ERROR_RESOURCE_TYPE_NOT_FOUND : ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (DWORD)resource.size();
}

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
    if (!lptstrFilename)
    {
        if (lpdwHandle)
            *lpdwHandle = 0;
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    std::wstring wide;
    towstring(lptstrFilename, -1, wide);
    return GetFileVersionInfoSizeW(wide.c_str(), lpdwHandle);
}

BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    // dwHandle 在 Win32 里是保留的（且从 16 位时代遗留下来的"0"），必须忽略
    (void)dwHandle;
    if (!lpData || dwLen == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::vector<uint8_t> resource;
    if (!extractVersionResource(lptstrFilename, resource))
    {
        SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return FALSE;
    }

    // 调用方通常先用 GetFileVersionInfoSize 拿到的尺寸再调这里；尺寸不一致时
    // 按 Win32 的惯例截断复制，而不是报错。
    size_t toCopy = resource.size() < dwLen ? resource.size() : dwLen;
    memcpy(lpData, resource.data(), toCopy);
    return TRUE;
}

BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    if (!lptstrFilename)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::wstring wide;
    towstring(lptstrFilename, -1, wide);
    return GetFileVersionInfoW(wide.c_str(), dwHandle, dwLen, lpData);
}

BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen)
{
    if (!pBlock || !lpSubBlock || !lplpBuffer || !puLen)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    const uint8_t *block = (const uint8_t *)pBlock;
    VersionNode root;
    // 根节点的长度字段就是整份资源的长度
    if (!parseNode(block, block + readWord(block), root))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // 把 "\\StringFileInfo\\040904E4\\ProductName" 拆成各级键
    std::vector<std::wstring> path;
    {
        std::wstring current;
        for (const wchar_t *p = lpSubBlock; ; ++p)
        {
            if (*p == L'\\' || *p == 0)
            {
                if (!current.empty())
                {
                    path.push_back(current);
                    current.clear();
                }
                if (*p == 0)
                    break;
                continue;
            }
            current.push_back(*p);
        }
    }

    VersionNode node = root;
    for (size_t i = 0; i < path.size(); ++i)
    {
        VersionNode child;
        bool found = false;
        // 逐个子节点比对（子节点名的大小写不敏感）
        const uint8_t *p = node.children;
        while (p + 6 <= node.end)
        {
            VersionNode candidate;
            if (!parseNode(p, node.end, candidate))
                break;
            if (equalsIgnoreCase(decodeUtf16Key(p + 6, candidate.end), path[i]))
            {
                child = candidate;
                found = true;
                break;
            }
            if (candidate.length == 0)
                break;
            p = node.children + align4((size_t)(p + candidate.length - node.children));
        }
        if (!found)
        {
            SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
            return FALSE;
        }
        node = child;
    }

    if (node.type == 1)
    {
        // 文本值：资源里是 UTF-16，而 WCHAR 可能是 4 字节，必须解码后再交出
        static thread_local std::wstring textBuffer;
        textBuffer = decodeUtf16Key(node.value, node.end);
        *lplpBuffer = (LPVOID)textBuffer.c_str();
        // Win32 对字符串返回的是字符数（含结尾 0），不是字节数——
        // WebKit 正是按 "bufferLength - 1" 当作字符数来用的。
        *puLen = (UINT)(textBuffer.size() + 1);
        return TRUE;
    }

    // 二进制值（根节点的 VS_FIXEDFILEINFO）全是 DWORD，字节布局与 WCHAR 宽度无关，
    // 可以直接指向资源内存
    *lplpBuffer = (LPVOID)node.value;
    *puLen = (UINT)node.valueLength;
    return TRUE;
}

BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen)
{
    if (!lpSubBlock)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::wstring wide;
    towstring(lpSubBlock, -1, wide);

    void *value = NULL;
    UINT length = 0;
    if (!VerQueryValueW(pBlock, wide.c_str(), &value, &length))
        return FALSE;

    // 文本值刚从 W 变体的 thread_local 宽缓冲里来；二进制值仍指向调用方的块。
    // 判断依据是地址是否落在块内，这样 A 变体不需要重新解析一遍树。
    const unsigned char *block = (const unsigned char *)pBlock;
    const unsigned char *bytes = (const unsigned char *)value;
    size_t blockLength = pBlock ? readWord(block) : 0;
    bool fromBlock = bytes >= block && bytes < block + blockLength;
    if (fromBlock || !value)
    {
        *lplpBuffer = value;
        *puLen = length;
        return TRUE;
    }

    // 文本值必须再转成 UTF-8：A 变体的调用方按 char* 读，直接交出宽指针
    // 会让它在每个 UTF-16 单元后多读一个 0。
    static thread_local std::string utf8Buffer;
    tostring((const wchar_t *)value, -1, utf8Buffer);
    *lplpBuffer = (LPVOID)utf8Buffer.c_str();
    *puLen = (UINT)(utf8Buffer.size() + 1);
    return TRUE;
}
