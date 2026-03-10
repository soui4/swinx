#include "coffparser.h"
#include "tostring.hpp"
#include "log.h"
#define kLogTag "coffparser"

int to_unicode(const char *input, size_t input_len, int codePage, std::wstring &out);

static void strtoup(std::wstring &str)
{
    for (auto &ch : str)
    {
        ch = towupper(ch);
    }
}

static void strtoup(std::string &str)
{
    for (auto &ch : str)
    {
        ch = toupper(ch);
    }
}

// 将资源ID字符串转换为可读形式
std::wstring WindResResourceParser::ResourceIdToString(uintptr_t id) const
{
    if (IS_INTRESOURCE(id))
    { // 是资源ID
        wchar_t buf[32];
        swprintf(buf, 32, L"#%u", static_cast<uint16_t>(id));
        return std::wstring(buf);
    }
    else
    { // 可能是字符串指针
        const char *str = reinterpret_cast<const char *>(id);
        std::wstring result;
        towstring(str, -1, result);
        return result;
    }
}

// 解析Unicode字符串（COFF中的资源名是utf-16）
std::wstring WindResResourceParser::ParseUnicodeString(const uint8_t *data) const
{
    const IMAGE_RESOURCE_DIR_STRING_U *str = reinterpret_cast<const IMAGE_RESOURCE_DIR_STRING_U *>(data);

    std::wstring result;
    to_unicode(reinterpret_cast<const char *>(str->NameString), str->Length * sizeof(uint16_t), CP_UTF16_LE, result);
    return result;
}

// 查找.data段
const uint8_t *WindResResourceParser::FindDataSection() const
{
    const COFF_FILE_HEADER *coffHeader = reinterpret_cast<const COFF_FILE_HEADER *>(m_baseAddr);

    if (m_totalSize < sizeof(COFF_FILE_HEADER))
    {
        SLOG_STME() << "COFF header too small";
        return nullptr;
    }

    const COFF_SECTION_HEADER *sections = reinterpret_cast<const COFF_SECTION_HEADER *>(m_baseAddr + sizeof(COFF_FILE_HEADER));

    for (int i = 0; i < coffHeader->NumberOfSections; i++)
    {
        const COFF_SECTION_HEADER &section = sections[i];
        // 检查段名是否为.data
        if (memcmp(section.Name, ".rsrc", 5) == 0 || memcmp(section.Name, ".rdata", 6) == 0 || memcmp(section.Name, ".data", 5) == 0)
        {
            // 验证段偏移是否有效
            if (section.PointerToRawData >= m_totalSize)
            {
                SLOG_STME() << "section offset out of range";
                return nullptr;
            }
            return m_baseAddr + section.PointerToRawData;
        }
    }

    SLOG_STME() << ".rsrc/.rdata/.data section not found";
    return nullptr;
}

// 递归解析资源目录
void WindResResourceParser::ParseResourceDirectory(const uint8_t *dirBase, uint32_t dirOffset, const std::vector<std::wstring> &typePath, std::vector<ResourceInfo> &resources, int depth)
{
    // 防止无限递归（正常情况下最多3层）
    const int MAX_RESOURCE_DEPTH = 3;
    if (depth > MAX_RESOURCE_DEPTH)
    {
        SLOG_STME() << "resource directory depth exceeded limit";
        return;
    }

    const uint8_t *dirAddr = dirBase + dirOffset;
    
    // 检查指针范围
    if (dirAddr < dirBase || dirAddr >= m_baseAddr + m_totalSize)
    {
        SLOG_STME() << "resource directory offset out of range";
        return;
    }

    if (dirAddr + sizeof(IMAGE_RESOURCE_DIRECTORY) > m_baseAddr + m_totalSize)
    {
        SLOG_STME() << "resource directory header out of range";
        return;
    }

    const IMAGE_RESOURCE_DIRECTORY *dir = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY *>(dirAddr);

    // 获取条目数组
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *entries = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY_ENTRY *>(dir + 1);

    // 检查条目数组是否会越界
    int totalEntries = dir->NumberOfNamedEntries + dir->NumberOfIdEntries;
    if ((const uint8_t *)(entries + totalEntries) > m_baseAddr + m_totalSize)
    {
        SLOG_STME() << "resource directory entries out of range";
        return;
    }

    for (int i = 0; i < totalEntries; i++)
    {
        const IMAGE_RESOURCE_DIRECTORY_ENTRY &entry = entries[i];

        std::wstring currentName;
        if (entry.NameIsString)
        {
            // 名称是字符串
            currentName = ParseUnicodeString(dirBase + entry.NameOffset);
        }
        else
        {
            // 名称是ID
            wchar_t buf[32];
            swprintf(buf, 32, L"#%u", entry.Id);
            currentName = std::wstring(buf);
        }

        std::vector<std::wstring> newPath = typePath;
        newPath.push_back(currentName);

        if (entry.DataIsDirectory)
        {
            // 进入子目录
            ParseResourceDirectory(dirBase, entry.OffsetToDirectory, newPath, resources, depth + 1);
        }
        else
        {
            // 到达数据节点
            const IMAGE_RESOURCE_DATA_ENTRY *dataEntry = reinterpret_cast<const IMAGE_RESOURCE_DATA_ENTRY *>(dirBase + entry.OffsetToData);

            // 验证数据条目
            if ((const uint8_t *)(dataEntry + 1) > m_baseAddr + m_totalSize)
            {
                SLOG_STME() << "data entry out of range";
                continue;
            }

            ResourceInfo info;
            if (newPath.size() >= 1)
            {
                // 对于整数类型，使用 #id 形式
                info.typeName = newPath[0];
            }
            if (newPath.size() >= 2)
                info.nameName = newPath[1];
            if (newPath.size() >= 3)
            {
                info.languageId = static_cast<uint16_t>(std::stoul(newPath[2].c_str() + 1)); // 去掉#号
            }
            else
            {
                info.languageId = 0; // 默认语言
            }

            info.dataOffset = dataEntry->DataRVA;
            info.dataSize = dataEntry->Size;
            info.codePage = dataEntry->CodePage;
            info.dataPtr = dirBase + info.dataOffset; // RVA在COFF中就是偏移

            resources.push_back(info);

            // 使用三级结构存储
            m_resourceMap[info.typeName][info.nameName][info.languageId] = info;
        }
    }
}

BOOL WindResResourceParser::Parse()
{
    // 查找资源数据的起始位置（通常是.data段）
    const uint8_t *resourceBase = FindDataSection();
    if (!resourceBase)
    {
        SLOG_STME() << "resource data not found";
        return FALSE;
    }

    // 验证资源数据头
    if (m_totalSize < 16)
    {
        SLOG_STME() << "resource data too small";
        return FALSE;
    }

    // 资源数据通常以资源目录开始
    const IMAGE_RESOURCE_DIRECTORY *rootDir = reinterpret_cast<const IMAGE_RESOURCE_DIRECTORY *>(resourceBase);

    // 开始递归解析
    std::vector<ResourceInfo> resources;
    ParseResourceDirectory(resourceBase, 0, std::vector<std::wstring>(), resources);

    SLOG_STMI() << "parse done, found " << resources.size() << " resources";
    #ifdef _DEBUG
    DumpResources();
    #endif//_DEBUG
    m_isValid = true;
    return TRUE;
}

// FindResource 的等价实现
HRSRC WindResResourceParser::FindResourceW(HMODULE hModule, const wchar_t *lpName, const wchar_t *lpType, WORD wLanguage) const
{
    if (!m_isValid)
        return nullptr;

    // 将 char* 转换为 wchar_t* 再调用 ResourceIdToString
    std::wstring typeW, nameW;
    if (IS_INTRESOURCE(lpType))
    {
        typeW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpType));
    }
    else
    {
        typeW = lpType;
    }
    if (IS_INTRESOURCE(lpName))
    {
        nameW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpName));
    }
    else
    {
        nameW = lpName;
    }
    strtoup(typeW);
    strtoup(nameW);
    // 在三级结构中查找
    auto typeIt = m_resourceMap.find(typeW);
    if (typeIt != m_resourceMap.end())
    {
        auto nameIt = typeIt->second.find(nameW);
        if (nameIt != typeIt->second.end())
        {
            // 找到第一个匹配的语言版本
            auto &langMap = nameIt->second;
            if (langMap.empty())
                return nullptr;
            if (langMap.size() == 1 || wLanguage == 0)
            {
                return (HRSRC)(&langMap.begin()->second);
            }
            auto langIt = langMap.find(wLanguage);
            if (langIt != langMap.end())
            {
                return (HRSRC)(&langIt->second);
            }
            else
            {
                return (HRSRC)(&langMap.begin()->second);
            }
        }
    }

    return nullptr;
}

// SizeofResource 的等价实现
uint32_t WindResResourceParser::SizeofResource(HMODULE hModule, HRSRC hResInfo) const
{
    if (!hResInfo)
        return 0;
    ResourceInfo *info = (ResourceInfo *)(hResInfo);
    return info->dataSize;
}

// LoadResource 的等价实现
HGLOBAL WindResResourceParser::LoadResource(HMODULE hModule, HRSRC hResInfo)
{
    if (!hResInfo)
        return nullptr;
    // 在windres资源包中，数据已经加载，直接返回信息指针
    return (HGLOBAL)hResInfo;
}

// LockResource 的等价实现
const void *WindResResourceParser::LockResource(HGLOBAL hResData)
{
    if (!hResData)
        return nullptr;
    ResourceInfo *info = (ResourceInfo *)(hResData);
    return info->dataPtr;
}

// 枚举资源类型
BOOL WindResResourceParser::EnumResourceTypesA(HMODULE hModule, ENUMRESTYPEPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return FALSE;

    // 直接遍历第一级（类型）
    for (const auto &typeEntry : m_resourceMap)
    {
        std::string strType;
        tostring(typeEntry.first.c_str(), typeEntry.first.length(), strType);
        if (!lpEnumFunc(hModule, (char *)strType.c_str(), lParam))
        {
            return FALSE;
        }
    }
    return TRUE;
}
BOOL WindResResourceParser::EnumResourceTypesW(HMODULE hModule, ENUMRESTYPEPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return FALSE;

    // 直接遍历第一级（类型）
    for (const auto &typeEntry : m_resourceMap)
    {
        if (!lpEnumFunc(hModule, (wchar_t *)typeEntry.first.c_str(), lParam))
        {
            return FALSE;
        }
    }
    return TRUE;
}
// 枚举资源名称
BOOL WindResResourceParser::EnumResourceNamesA(HMODULE hModule, const char *lpType, ENUMRESNAMEPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return false;

    // 将 char* 转换为 std::wstring
    std::wstring typeW;
    if (IS_INTRESOURCE(lpType))
    {
        typeW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpType));
    }
    else
    {
        towstring(lpType, -1, typeW);
    }

    strtoup(typeW);

    // 在三级结构中查找类型
    auto typeIt = m_resourceMap.find(typeW);
    if (typeIt != m_resourceMap.end())
    {
        // 遍历第二级（名称）
        for (const auto &nameEntry : typeIt->second)
        {
            std::string name;
            tostring(nameEntry.first.c_str(), nameEntry.first.length(), name);
            if (!lpEnumFunc(hModule, lpType, (char *)name.c_str(), lParam))
            {
                return false;
            }
        }
    }
    return true;
}

BOOL WindResResourceParser::EnumResourceNamesW(HMODULE hModule, const wchar_t *lpType, ENUMRESNAMEPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return false;

    // 将 wchar_t* 转换为 std::wstring
    std::wstring typeW;
    if (IS_INTRESOURCE(lpType))
    {
        typeW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpType));
    }
    else
    {
        typeW = lpType;
    }
    strtoup(typeW);
    // 在三级结构中查找类型
    auto typeIt = m_resourceMap.find(typeW);
    if (typeIt != m_resourceMap.end())
    {
        // 遍历第二级（名称）
        for (const auto &nameEntry : typeIt->second)
        {
            if (!lpEnumFunc(hModule, lpType, (wchar_t *)nameEntry.first.c_str(), lParam))
            {
                return false;
            }
        }
    }
    return true;
}
BOOL WindResResourceParser::EnumResourceLanguagesA(HMODULE hModule, const char *lpType, const char *lpName, ENUMRESLANGPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return false;

    // 将 wchar_t* 转换为 std::wstring
    std::wstring typeW, nameW;
    if (IS_INTRESOURCE(lpType))
    {
        typeW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpType));
    }
    else
    {
        towstring(lpType, -1, typeW);
    }
    if (IS_INTRESOURCE(lpName))
    {
        nameW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpName));
    }
    else
    {
        towstring(lpName, -1, nameW);
    }
    strtoup(typeW);
    strtoup(nameW);

    // 在三级结构中查找类型
    auto typeIt = m_resourceMap.find(typeW);
    if (typeIt != m_resourceMap.end())
    {
        // 遍历第二级（名称）
        for (const auto &nameEntry : typeIt->second)
        {
            if (nameEntry.first == nameW)
            {
                // 遍历第三级（语言）
                for (const auto &langEntry : nameEntry.second)
                {
                    if (!lpEnumFunc(hModule, lpType, lpName, langEntry.first, lParam))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// 枚举资源名称
BOOL WindResResourceParser::EnumResourceLanguagesW(HMODULE hModule, const wchar_t *lpType, const wchar_t *lpName, ENUMRESLANGPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!m_isValid || !lpEnumFunc)
        return false;

    // 将 wchar_t* 转换为 std::wstring
    std::wstring typeW, nameW;
    if (IS_INTRESOURCE(lpType))
    {
        typeW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpType));
    }
    else
    {
        typeW = lpType;
    }
    if (IS_INTRESOURCE(lpName))
    {
        nameW = ResourceIdToString(reinterpret_cast<uintptr_t>(lpName));
    }
    else
    {
        nameW = lpName;
    }
    strtoup(typeW);
    strtoup(nameW);
    // 在三级结构中查找类型
    auto typeIt = m_resourceMap.find(typeW);
    if (typeIt != m_resourceMap.end())
    {
        // 遍历第二级（名称）
        for (const auto &nameEntry : typeIt->second)
        {
            if (nameEntry.first == nameW)
            {
                // 遍历第三级（语言）
                for (const auto &langEntry : nameEntry.second)
                {
                    if (!lpEnumFunc(hModule, lpType, lpName, langEntry.first, lParam))
                    {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
// 加载字符串资源的方法
int WindResResourceParser::LoadStringW(UINT uID, LPWSTR lpBuffer, int cchBufferMax) const
{
    if (!m_isValid || !lpBuffer || cchBufferMax <= 0)
        return 0;

    // 字符串资源的组织方式：每个字符串表包含16个字符串
    // uID = (table_index * 16) + string_index
    UINT tableIndex = uID / 16 + 1; // 表索引从1开始
    UINT stringIndex = uID % 16;

    // 构建字符串表的名称（使用数字ID）
    wchar_t tableNameBuf[16];
    swprintf(tableNameBuf, 16, L"%u", tableIndex);
    std::wstring tableName(tableNameBuf);

    // 查找字符串表资源
    // 字符串资源的类型是 "#6" (RT_STRING)
    std::wstring typeName = L"#6"; // RT_STRING

    // 在资源映射中查找
    auto typeIt = m_resourceMap.find(typeName);
    if (typeIt == m_resourceMap.end())
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    auto nameIt = typeIt->second.find(tableName);
    if (nameIt == typeIt->second.end())
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    // 找到第一个语言版本
    if (nameIt->second.empty())
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    const ResourceInfo &info = nameIt->second.begin()->second;

    // 解析字符串表
    const uint8_t *data = info.dataPtr;
    uint32_t offset = 0;

    // 验证数据指针有效性
    if (!data || data < m_baseAddr || data >= m_baseAddr + m_totalSize)
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    // 遍历字符串表中的字符串
    for (UINT i = 0; i < stringIndex; i++)
    {
        if (offset + sizeof(uint16_t) > info.dataSize)
        {
            if (cchBufferMax > 0)
                lpBuffer[0] = L'\0';
            return 0;
        }
        // 字符串长度（字数，不是字节数）
        uint16_t len = *reinterpret_cast<const uint16_t *>(data + offset);
        offset += 2 + (len * sizeof(uint16_t));
    }

    if (offset + sizeof(uint16_t) > info.dataSize)
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    // 读取目标字符串
    uint16_t len = *reinterpret_cast<const uint16_t *>(data + offset);
    offset += 2;
    
    // 检查显示字符串数据是否是有效
    if (offset + (len * sizeof(uint16_t)) > info.dataSize)
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    std::wstring stringData;
    to_unicode(reinterpret_cast<const char *>(data + offset), len * sizeof(uint16_t), CP_UTF16_LE, stringData);
    int copyLen = stringData.length();
    if (cchBufferMax > 0)
    {
        copyLen = std::min(copyLen, cchBufferMax - 1);
        wcsncpy(lpBuffer, stringData.c_str(), copyLen);
        lpBuffer[copyLen] = L'\0';
    }
    return copyLen;
}

// 打印所有资源信息（调试用）
void WindResResourceParser::DumpResources() const
{
    printf("\n=== windres资源包内容 ===\n");

    int totalResources = 0;
    for (const auto &typeEntry : m_resourceMap)
    {
        for (const auto &nameEntry : typeEntry.second)
        {
            totalResources += nameEntry.second.size();
        }
    }

    printf("总资源数: %d\n\n", totalResources);

    int index = 0;
    for (const auto &typeEntry : m_resourceMap)
    {
        const std::wstring &typeName = typeEntry.first;
        for (const auto &nameEntry : typeEntry.second)
        {
            const std::wstring &nameName = nameEntry.first;
            for (const auto &langEntry : nameEntry.second)
            {
                uint16_t languageId = langEntry.first;
                const ResourceInfo &info = langEntry.second;

                // 将 std::wstring 转换为 char* 进行打印
                std::string typeNameA, nameNameA;
                tostring(typeName.c_str(), typeName.length(), typeNameA);
                tostring(nameName.c_str(), nameName.length(), nameNameA);

                printf("[%d] 类型: %-20s 名称: %-20s 语言: %d 大小: %-8u 字节 偏移: 0x%x\n", ++index, typeNameA.c_str(), nameNameA.c_str(), languageId, info.dataSize, info.dataOffset);
            }
        }
    }
}
