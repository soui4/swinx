/*
 * sysbridge.cpp -- FreeRTOS bridge for the platform-neutral subset of
 * src/sysapi.cpp / src/memory.cpp / src/sharedmem.cpp.
 *
 * sysapi.cpp (and friends) stay excluded from the FreeRTOS build because of
 * their POSIX-only halves (dlopen / fork / inotify / /proc / mmap).  The
 * pieces the core window path needs, though, are platform neutral: the
 * codepage-conversion block, message-pump forwarding to SConnection,
 * keyboard state forwarding, GetSystemMetrics, cursors, the process heap
 * and the single-process SharedMemory fallback.  They are provided here,
 * on the same principle as winobjs.cpp: keep the framework untouched, wrap
 * the missing surface in src/platform/freertos.
 *
 * KEEP IN SYNC: the conversion block between the MARKER comments is copied
 * verbatim from src/sysapi.cpp (Cp2IConvCode .. WideCharToMultiByte).
 */
#include <windows.h>
#include "sysapi.h"
#include "SConnection.h"
#include "cptable.h"
#include "uniconv.h"
#include "hook.h"
#include "cursormgr.h"
#include "uimsg.h"
#include "sharedmem.h"
#include <string>
#include <vector>
#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <iconv.h>
#include <log.h>

#define kLogTag "sysbridge"
#include "tostring.h"
#include <new>

/* implemented in src/oleacc.cpp (compiled on all platforms) */
extern "C" void WINAPI SwinxDispatchPendingWinEvents(void);

//---- MARKER: verbatim from src/sysapi.cpp -----------------------------------

using namespace swinx;

#ifdef __APPLE__
#define ICONV_UTF32LE "UTF-32LE"
#define ICONV_UTF16LE "UTF-16LE"
#define ICONV_UTF16BE "UTF-16BE"
#else
#define ICONV_UTF32LE "UTF32LE"
#define ICONV_UTF16LE "UTF16LE"
#define ICONV_UTF16BE "UTF16BE"
#endif //__APPLE__
#define ICONV_WINDOWS_936  "WINDOWS-936"
#define ICONV_WINDOWS_1250 "WINDOWS-1250"
#define ICONV_WINDOWS_1251 "WINDOWS-1251"
#define ICONV_WINDOWS_1252 "WINDOWS-1252"
#define ICONV_WINDOWS_1253 "WINDOWS-1253"
#define ICONV_WINDOWS_1254 "WINDOWS-1254"
#define ICONV_WINDOWS_1255 "WINDOWS-1255"
#define ICONV_WINDOWS_1256 "WINDOWS-1256"
#define ICONV_WINDOWS_1257 "WINDOWS-1257"
#define ICONV_WINDOWS_1258 "WINDOWS-1258"

static const char *Cp2IConvCode(int codePage)
{
    switch (codePage)
    {
    case CP_UTF16_LE:
        return ICONV_UTF16LE;
    case CP_UTF16_BE:
        return ICONV_UTF16BE;
    case 936:
        return ICONV_WINDOWS_936;
    case 1250:
        return ICONV_WINDOWS_1250;
    case 1251:
        return ICONV_WINDOWS_1251;
    case 1252:
        return ICONV_WINDOWS_1252;
    case 1253:
        return ICONV_WINDOWS_1253;
    case 1254:
        return ICONV_WINDOWS_1254;
    case 1255:
        return ICONV_WINDOWS_1255;
    case 1256:
        return ICONV_WINDOWS_1256;
    case 1257:
        return ICONV_WINDOWS_1257;
    case 1258:
        return ICONV_WINDOWS_1258;
    default:
        return NULL;
    }
}

// ============================================================================
// iconv 不可用时的内置码表兜底
// ----------------------------------------------------------------------------
// Android(bionic) 与 OHOS(musl) 的 iconv 只实现了 Unicode 系编码(UTF-8/16/32、
// US-ASCII、wchar_t), 既不认识 GBK/CP936 与 Windows-125x, 也没有别名解析能力,
// 于是 iconv_open 对传统代码页必然失败(失败时返回 -1, 个别实现返回 NULL),
// 使 MultiByteToWideChar/WideCharToMultiByte 对 CP936 等直接失效。
// 这里用 cptable.h 的静态码表兜底, 让各平台的转换结果保持一致; iconv 可用的平台
// (Linux/macOS/iOS 桌面链)仍优先走 iconv, 行为不变。
// ============================================================================

// iconv_open 成功时才返回有效句柄; 失败时不同实现返回 (iconv_t)-1 或 NULL, 都算失败
static inline bool isInvalidIconv(iconv_t cd)
{
    return cd == (iconv_t)-1 || cd == (iconv_t)0;
}

// Windows-125x 单字节页码表; 该代码页无内置码表时返回 NULL
static const unsigned short *builtinSingleByteTable(int codePage)
{
    switch (codePage)
    {
    case 1250:
        return kCP1250ToUnicode;
    case 1251:
        return kCP1251ToUnicode;
    case 1252:
        return kCP1252ToUnicode;
    case 1253:
        return kCP1253ToUnicode;
    case 1254:
        return kCP1254ToUnicode;
    case 1255:
        return kCP1255ToUnicode;
    case 1256:
        return kCP1256ToUnicode;
    case 1257:
        return kCP1257ToUnicode;
    case 1258:
        return kCP1258ToUnicode;
    default:
        return NULL;
    }
}

// 是否有内置码表可以处理该代码页(目前为 CP936 与 Windows-125x)
static inline bool hasBuiltinTable(int codePage)
{
    return codePage == 936 || builtinSingleByteTable(codePage) != NULL;
}

// 尾字节 -> 表内序号(0x40..0x7E 与 0x80..0xFE 合法, 0x7F/0xFF 非法), 非法返回 -1
static inline int cp936TrailIndex(unsigned char c)
{
    if (c >= 0x40 && c <= 0x7E)
        return c - 0x40;
    if (c >= 0x80 && c <= 0xFE)
        return c - 0x80 + 63; // 0x40..0x7E 共 63 个尾字节
    return -1;
}

// 用内置码表把多字节串转成宽字符串。返回值语义同 to_unicode(写出的字符数);
// unConvertedChars 记录未能转换的字节数(语义与 iconv 路径一致), 供
// MultiByteToWideChar 判定 ERROR_NO_UNICODE_TRANSLATION。无法转换的字节按 Win32
// 语义替换为 U+FFFD 并继续, 而不是中断整个转换。
static int builtinToUnicode(const char *input, size_t input_len, int codePage, std::wstring &out, int &unConvertedChars)
{
    size_t unconverted = 0;
    out.clear();
    out.reserve(input_len);

    if (codePage == 936)
    {
        for (size_t i = 0; i < input_len;)
        {
            unsigned char c = (unsigned char)input[i];
            if (c < 0x80)
            { // 单字节区与 ASCII 一致
                out.push_back((wchar_t)c);
                ++i;
                continue;
            }
            int trail = (i + 1 < input_len) ? cp936TrailIndex((unsigned char)input[i + 1]) : -1;
            if (c >= kCP936LeadFirst && c <= kCP936LeadLast && trail >= 0)
            {
                unsigned short u = kCP936ToUnicode[(c - kCP936LeadFirst) * kCP936TrailCount + trail];
                if (u)
                {
                    out.push_back((wchar_t)u);
                    i += 2;
                    continue;
                }
            }
            // 非法序列: 只前进一个字节, 让后一个字节仍有机会被正确解码
            out.push_back((wchar_t)0xFFFD);
            ++unconverted;
            ++i;
        }
        unConvertedChars = (int)unconverted;
        return (int)out.length();
    }

    const unsigned short *table = builtinSingleByteTable(codePage);
    if (!table)
        return 0; // 该代码页无内置码表(如 UTF-16), 由调用方按转换失败处理

    for (size_t i = 0; i < input_len; ++i)
    {
        unsigned char c = (unsigned char)input[i];
        unsigned short u = c;
        if (c >= 0x80)
        {
            u = table[c - 0x80];
            if (!u)
            { // 该页未定义此字节
                u = 0xFFFD;
                ++unconverted;
            }
        }
        out.push_back((wchar_t)u);
    }
    unConvertedChars = (int)unconverted;
    return (int)out.length();
}

// Unicode -> CP936 反查表: 元素为 (unicode << 16) | 表内序号, 排序后即可二分查找。
// 首次使用时由正向码表构建一次。故意 new 出来不释放(leak-on-purpose): 若被其它
// 静态对象的析构函数调用, 函数局部静态的析构会早于调用方, 造成 UAF。
static const swinx_stl::vector<unsigned int> &cp936ReverseMap()
{
    static const swinx_stl::vector<unsigned int> *s_map = []() {
        swinx_stl::vector<unsigned int> *map = new swinx_stl::vector<unsigned int>();
        map->reserve(kCP936TableSize);
        for (unsigned int i = 0; i < (unsigned int)kCP936TableSize; ++i)
        {
            unsigned short u = kCP936ToUnicode[i];
            if (u)
                map->push_back((((unsigned int)u) << 16) | i);
        }
        std::sort(map->begin(), map->end());
        return map;
    }();
    return *s_map;
}

// Unicode -> CP936 单字符编码; 无法映射返回 0
static unsigned short cp936Encode(unsigned int ch)
{
    if (ch < 0x80)
        return (unsigned short)ch;
    const swinx_stl::vector<unsigned int> &map = cp936ReverseMap();
    swinx_stl::vector<unsigned int>::const_iterator it = std::lower_bound(map.begin(), map.end(), ch << 16);
    if (it != map.end() && (*it >> 16) == ch)
        return (unsigned short)(*it & 0xFFFF);
    return 0;
}

// 用内置码表把宽字符串转成多字节串。返回值语义同 to_mb(写出的字节数); 无法映射的
// 字符按 Win32 语义替换为 '?'。不写结尾 0, 由调用方按 dstLen 处理。
static int builtinToMb(const wchar_t *input, size_t input_len, int codePage, swinx_stl::string &out)
{
    out.clear();
    out.reserve(input_len * 2);

    if (codePage == 936)
    {
        for (size_t i = 0; i < input_len; ++i)
        {
            unsigned int ch = (unsigned int)input[i];
            if (ch < 0x80)
            {
                out.push_back((char)ch);
                continue;
            }
            unsigned short idx = cp936Encode(ch);
            if (!idx)
            {
                out.push_back('?');
                continue;
            }
            int trail = idx % kCP936TrailCount;
            out.push_back((char)(kCP936LeadFirst + idx / kCP936TrailCount));
            out.push_back((char)(trail < 63 ? (0x40 + trail) : (0x80 + trail - 63)));
        }
        return (int)out.length();
    }

    const unsigned short *table = builtinSingleByteTable(codePage);
    if (!table)
        return 0; // 该代码页无内置码表(如 UTF-16), 由调用方按转换失败处理

    for (size_t i = 0; i < input_len; ++i)
    {
        unsigned int ch = (unsigned int)input[i];
        char mb = 0;
        if (ch < 0x80)
            mb = (char)ch;
        else
        {
            for (int b = 0; b < 128; ++b)
            { // 单字节页只有 128 项, 线性反查足够
                if (table[b] == ch)
                {
                    mb = (char)(0x80 + b);
                    break;
                }
            }
            if (!mb)
                mb = '?';
        }
        out.push_back(mb);
    }
    return (int)out.length();
}

static int to_mb(const wchar_t *input, size_t input_len, int codePage, swinx_stl::string &out)
{
    const char *toCode = Cp2IConvCode(codePage);
    if (!toCode)
        return 0;
#if WCHAR_SIZE == 4
    iconv_t cd = iconv_open(toCode, ICONV_UTF32LE);
#else
    iconv_t cd = iconv_open(toCode, ICONV_UTF16LE);
#endif
    if (isInvalidIconv(cd))
    {
        // iconv 不认识该代码页(典型: Android/OHOS 上的 CP936 与 Windows-125x),
        // 改用内置码表兜底, 否则这些代码页在移动平台上完全不可用。
        if (hasBuiltinTable(codePage))
        {
            SLOG_STMW() << "iconv_open failed, fallback to builtin table, codePage=" << codePage;
            return builtinToMb(input, input_len, codePage, out);
        }
        SLOG_STMW() << "iconv_open failed, codePage=" << codePage;
        return 0;
    }
    size_t output_len = (input_len + 1) * 4; // UTF-32 字符一般比 GBK 长
    out.resize(output_len);
    char *output = (char *)out.c_str();
    char *inbuf = (char *)input;
    char *outbuf = output;
    size_t inbytesleft = input_len * sizeof(wchar_t);
    size_t outbytesleft = output_len;

    size_t ret = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (ret != (size_t)-1)
    {
        out.resize(output_len - outbytesleft);
    }
    else
    {
        out.clear();
    }

    iconv_close(cd);
    return out.length();
}

int to_unicode(const char *input, size_t input_len, int codePage, std::wstring &out, int &unConvertedChars)
{
    const char *fromCode = Cp2IConvCode(codePage);
    if (!fromCode)
        return 0;
#if WCHAR_SIZE == 4
    iconv_t cd = iconv_open(ICONV_UTF32LE, fromCode);
#else
    iconv_t cd = iconv_open(ICONV_UTF16LE, fromCode);
#endif
    if (isInvalidIconv(cd))
    {
        // iconv 不认识该代码页(典型: Android/OHOS 的 bionic/musl 只实现了 Unicode
        // 系编码), 改用内置码表兜底, 否则 GBK 等中文编码在本平台完全无法转换。
        if (hasBuiltinTable(codePage))
        {
            SLOG_STMW() << "iconv_open failed, fallback to builtin table, codePage=" << codePage;
            return builtinToUnicode(input, input_len, codePage, out, unConvertedChars);
        }
        SLOG_STMW() << "iconv_open failed, codePage=" << codePage;
        return 0;
    }
    size_t output_len = (input_len + 1) * sizeof(wchar_t); // UTF-32 字符一般比 GBK 长
    out.resize(output_len);
    char *output = (char *)out.c_str();
    char *inbuf = (char *)input;
    char *outbuf = output;
    size_t inbytesleft = input_len;
    size_t outbytesleft = output_len;

    size_t ret = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (ret != (size_t)-1)
    {
        out.resize((output_len - outbytesleft) / sizeof(wchar_t));
    }
    else
    {
        out.clear();
    }
    unConvertedChars = inbytesleft;
    iconv_close(cd);
    return out.length();
}

FILE *_wfopen(const wchar_t *path, const wchar_t *mode)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    char *u8path = (char *)malloc(len + 1);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, u8path, len + 1, nullptr, nullptr);
    u8path[len] = 0;
    char u8mode[20] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, mode, -1, u8mode, 20, nullptr, nullptr);
    FILE *ret = fopen(u8path, u8mode);
    free(u8path);
    return ret;
}

void SetLastError(int e)
{
    errno = e;
}

int GetLastError()
{
    return errno;
}

int MulDiv(int a, int b, int c)
{
    if (c == 0)
    {
        SetLastError(ERROR_INT_DIVIDE_BY_ZERO);
        return -1;
    }
    LONGLONG ret;
    /* We want to deal with a positive divisor to simplify the logic. */
    if (c < 0)
    {
        a = -a;
        c = -c;
    }

    /* If the result is positive, we "add" to round. else, we subtract to round. */
    if ((a < 0 && b < 0) || (a >= 0 && b >= 0))
        ret = (((LONGLONG)a * b) + (c / 2)) / c;
    else
        ret = (((LONGLONG)a * b) - (c / 2)) / c;

    if (ret > 2147483647 || ret < -2147483647)
        return -1;
    return ret;
}

// GetCurrentThreadId is provided by platform/freertos/winobjs.cpp

int MultiByteToWideChar(int cp, int flags, const char *src, int len, wchar_t *dst, int dstLen)
{
    assert(src);
    if (cp == CP_OEMCP)
        cp = 936;

    if (len < 0)
        len = strlen(src) + 1;
    if (cp != CP_ACP && cp != CP_UTF8)
    {
        std::wstring str;
        int unConvertedChars = 0;
        int ret = to_unicode(src, len, cp, str, unConvertedChars); // iconv 或内置码表(见 builtinToUnicode)
        if (unConvertedChars)
        {
            if (flags & MB_ERR_INVALID_CHARS)
                return 0;
            else
                SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        }
        if (!dst)
            return str.length();
        else if (dstLen < ret)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        else
        {
            memcpy(dst, str.c_str(), ret * sizeof(wchar_t));
            if (ret < dstLen)
                dst[ret] = 0;
            return ret;
        }
    }

#if (WCHAR_SIZE == 2)
    assert(sizeof(wchar_t) == 2);
    // handle for utf16
    size_t unconvertedCharacters = 0;
    int bufRequire = UTF16Length(src, len, unconvertedCharacters);
    if (unconvertedCharacters)
    {
        if (flags & MB_ERR_INVALID_CHARS)
            return 0;
        else
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    }
    if (!dst)
    {
        return bufRequire;
    }
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF16FromUTF8(src, len, (uint16_t *)dst, dstLen);
    }
#else
    assert(sizeof(wchar_t) == 4);
    // handle for utf32
    size_t unconvertedCharacters = 0;
    int bufRequire = UTF32Length(src, len, unconvertedCharacters);
    if (unconvertedCharacters)
    {
        if (flags & MB_ERR_INVALID_CHARS)
            return 0;
        else
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    }
    if (!dst)
    {
        return bufRequire;
    }
    if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF32FromUTF8(src, len, (uint32_t *)dst, dstLen);
    }
#endif
}

int WideCharToMultiByte(int cp, int flags __attribute__((unused)), const wchar_t *src, int len, char *dst, int dstLen, LPCSTR p1 __attribute__((unused)), BOOL *p2 __attribute__((unused)))
{
    assert(src);
    // a NUL-terminated source (len == -1) must include the terminator in the
    // returned size, like Win32 does.
    bool nullTerminated = len < 0;
    if (len < 0)
        len = wcslen(src);
    if (cp == CP_OEMCP)
        cp = CP_UTF8; // todo:hjx
    if (cp != CP_ACP && cp != CP_UTF8)
    {
        swinx_stl::string str;
        int ret = to_mb(src, len, cp, str);
        if (ret == 0)
            return 0;
        if (ret > dstLen)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(dst, str.c_str(), ret);
        if (ret < dstLen)
            dst[ret] = 0;
        return ret;
    }

#if (WCHAR_SIZE == 2)
    assert(sizeof(wchar_t) == 2);
    int bufRequire = UTF16toUTF8Length((const uint16_t *)src, len);
    if (nullTerminated)
        bufRequire++;
    if (!dst)
    {
        return bufRequire;
    }
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        int ret = UTF8FromUTF16((const uint16_t *)src, len, dst, dstLen);
        return nullTerminated ? ret + 1 : ret;
    }
#else
    assert(sizeof(wchar_t) == 4);
    int bufRequire = UTF32toUTF8Length((const uint32_t *)src, len);
    if (nullTerminated)
        bufRequire++;
    if (!dst)
        return bufRequire;
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        int ret = UTF8FromUTF32((const uint32_t *)src, len, dst, dstLen);
        return nullTerminated ? ret + 1 : ret;
    }
#endif
}

//---- END MARKER -------------------------------------------------------------

/* iconv is declared by newlib but not implemented in this toolchain build:
 * return the invalid handle so isInvalidIconv() routes everything to the
 * builtin code tables (same fallback path as Android/OHOS bionic). */
extern "C" iconv_t iconv_open(const char *, const char *)
{
    return (iconv_t)0;
}

extern "C" size_t iconv(iconv_t, char **, size_t *, char **, size_t *)
{
    return (size_t)-1;
}

extern "C" int iconv_close(iconv_t)
{
    return 0;
}

/* LoadImage* lives in src/cursoricon.cpp (PE parsing + resource scan) which
 * stays excluded on FreeRTOS: stub to NULL, cursors are not rendered. */
HANDLE WINAPI LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT)
{
    return NULL;
}

HANDLE WINAPI LoadImageBuf(const void *, UINT, UINT, INT, INT, UINT)
{
    return NULL;
}

BOOL WaitMessage()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMsg();
}

DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMutliObjectAndMsg(pHandles, nCount, dwMilliseconds, fWaitAll, dwWakeMask);
}

BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    SwinxDispatchPendingWinEvents();
    SConnection *conn = SConnMgr::instance()->getConnection();
    BOOL bRet = conn->getMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    SwinxDispatchPendingWinEvents();
    if (bRet)
    {
        if (CallHook(WH_GETMESSAGE, HC_ACTION, 1, (LPARAM)lpMsg))
            return GetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }
    return bRet;
}

BOOL PeekMessage(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    SwinxDispatchPendingWinEvents();
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    BOOL bRet = conn->peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    SwinxDispatchPendingWinEvents();
    if (bRet)
    {
        if (CallHook(WH_GETMESSAGE, HC_ACTION, wRemoveMsg & PM_REMOVE, (LPARAM)pMsg))
            return PeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }
    return bRet;
}

BOOL CallMsgFilter(LPMSG lpMsg, int nCode)
{
    if (CallHook(WH_SYSMSGFILTER, nCode, 0, (LPARAM)lpMsg))
        return TRUE;
    return CallHook(WH_MSGFILTER, nCode, 0, (LPARAM)lpMsg);
}

BOOL GetKeyboardState(PBYTE lpKeyState)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyboardState(lpKeyState);
}

SHORT GetKeyState(int nVirtKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyState(nVirtKey);
}

SHORT WINAPI GetAsyncKeyState(int vKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetAsyncKeyState(vKey);
}

UINT MapVirtualKey(UINT uCode, UINT uMapType)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->MapVirtualKey(uCode, uMapType);
}

UINT MapVirtualKeyEx(UINT uCode, UINT uMapType, HKL dwhkl __attribute__((unused)))
{
    return MapVirtualKey(uCode, uMapType);
}

int GetSystemScale()
{
    int dpi = GetDpiForWindow(0);
    return dpi * 100 / 96;
}

int GetSystemMetrics(int nIndex)
{
    int ret = 0;
    SConnection *conn = SConnMgr::instance()->getConnection();
    HMONITOR hMonitor = conn->GetScreen(0);
    switch (nIndex)
    {
    case SM_CXSCREEN:
        return conn->GetScreenWidth(hMonitor);
    case SM_CYSCREEN:
        return conn->GetScreenHeight(hMonitor);
    case SM_CXBORDER:
    case SM_CYBORDER:
    case SM_CXEDGE:
    case SM_CYEDGE:
        return 1;
    case SM_CXSMICON:
        return 16;
    case SM_CYSMICON:
        return 16;
    // swinx 不自绘的非客户区尺寸：标题栏及其按钮、调整边框、对话框边框、菜单栏。
    // 它们要么由原生窗口管理器画在窗口矩形之外，要么由上层 UI 库（SOUI 的菜单
    // 控件）负责，因此在 swinx 的窗口矩形里厚度恒为 0。
    case SM_CYCAPTION:
    case SM_CXSIZE:
    case SM_CYSIZE:
    case SM_CXSMSIZE:
    case SM_CYSMSIZE:
    case SM_CXFRAME:
    case SM_CYFRAME:
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME:
    case SM_CYMENU:
        return 0;
    case SM_CXICON:
        ret = 32;
        break;
    case SM_CYICON:
        ret = 32;
        break;
    case SM_CXCURSOR:
        ret = 24;
        break;
    case SM_CYCURSOR:
        ret = 24;
        break;
    case SM_CYHSCROLL:
    case SM_CXVSCROLL:
        ret = 16;
        break;
    case SM_CYVTHUMB:
    case SM_CXHTHUMB:
        ret = 16;
        break;
    case SM_CXDRAG:
    case SM_CYDRAG:
        ret = 4;
        break;
    case SM_CXMINTRACK:
    case SM_CYMINTRACK:
        ret = 6;
        break;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:
        ret = 1;
        break;
    default:
        printf("unknown index for GetSystemMetrics, index=%d\n", nIndex);
        break;
    }
    return ret * GetSystemScale() / 100;
}

HCURSOR LoadCursorA(HINSTANCE hInstance __attribute__((unused)), LPCSTR lpCursorName)
{
    return CursorMgr::loadCursor(lpCursorName);
}

HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName)
{
    if (IS_INTRESOURCE(lpCursorName))
        return LoadCursorA(hInstance, (LPCSTR)lpCursorName);
    swinx_stl::string str;
    tostring(lpCursorName, -1, str);
    return CursorMgr::loadCursor(str.c_str());
}

HCURSOR SetCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetCursor(0, hCursor);
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT val __attribute__((unused)), void *ptr, UINT winini __attribute__((unused)))
{
    switch (action)
    {
    case SPI_GETWHEELSCROLLLINES:
    {
        unsigned int *pv = (unsigned int *)ptr;
        *pv = 3;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT val, void *ptr, UINT winini)
{
    return SystemParametersInfoA(action, val, ptr, winini);
}

/* bare metal: any non-null address is readable, no SIGSEGV probing */
BOOL IsBadReadPtr(const void *ptr, size_t)
{
    return ptr == NULL ? TRUE : FALSE;
}

//=============================================================================
// Process heap (malloc-backed replacement for src/memory.cpp's POSIX heap)
//=============================================================================

struct HeapBlockFr
{
    void *ptr;
    size_t len;
};

struct HeapInfoFr
{
    std::recursive_mutex mutex;
    swinx_stl::vector<HeapBlockFr> blocks;
};

HANDLE GetProcessHeap()
{
    // leak-on-purpose: the default heap lives for the process lifetime
    static HeapInfoFr *s_heap = new HeapInfoFr();
    return (HANDLE)s_heap;
}

LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info)
        return nullptr;
    void *ptr = malloc(dwBytes);
    if (!ptr)
        return nullptr;
    if (dwFlags & HEAP_ZERO_MEMORY)
        memset(ptr, 0, dwBytes);
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    HeapBlockFr block = {ptr, dwBytes};
    info->blocks.push_back(block);
    return ptr;
}

BOOL HeapFree(HANDLE hHeap, DWORD dwFlags __attribute__((unused)), LPVOID lpMem)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            info->blocks.erase(it);
            free(lpMem);
            return TRUE;
        }
    }
    return FALSE;
}

LPVOID HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, size_t dwBytes)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    size_t oldLen = 0;
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            oldLen = it->len;
            break;
        }
    }
    void *ptr = malloc(dwBytes);
    if (!ptr)
        return nullptr;
    memcpy(ptr, lpMem, oldLen < dwBytes ? oldLen : dwBytes);
    if ((dwFlags & HEAP_ZERO_MEMORY) && dwBytes > oldLen)
        memset((char *)ptr + oldLen, 0, dwBytes - oldLen);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            it->ptr = ptr;
            it->len = dwBytes;
            break;
        }
    }
    free(lpMem);
    return ptr;
}

//=============================================================================
// SharedMemory -- single-process heap fallback (mirrors the mobile heap
// fallback in src/sharedmem.cpp; bare metal has no shm_open / temp files)
//=============================================================================

namespace swinx
{

    SharedMemory::~SharedMemory()
    {
        if (m_pBuf == (LPBYTE)-1)
            return;
        if (m_bHeap)
        {
            // the refcount header word sits in front of the buffer
            delete[](m_pBuf - sizeof(uint32_t));
        }
        delete m_rwlock;
        m_pBuf = (LPBYTE)-1;
    }

    SharedMemory::InitStat SharedMemory::init(const char *name, uint32_t size)
    {
        assert(m_rwlock == nullptr);
        TSemRwLock<kSharedNumber> *rwlock = new TSemRwLock<kSharedNumber>();
        if (!rwlock->init(name))
        {
            delete rwlock;
            return Failed;
        }
        m_rwlock = rwlock;

        const uint32_t memSize = size + sizeof(uint32_t);
        LPBYTE ptr = new (std::nothrow) uint8_t[memSize];
        if (ptr == nullptr)
        {
            delete rwlock;
            m_rwlock = nullptr;
            return Failed;
        }
        memset(ptr, 0, memSize);
        m_bHeap = true;

        nRef = 1;
        m_pBuf = ptr + sizeof(uint32_t);
        shmid = -1;
        m_dwSize = size;
        m_name = name ? name : "";
        m_bDetached = false;
        SLOG_FMTD("open share memory (freertos, heap fallback), name=%s, size=%u\n", m_name.c_str(), size);
        return Created;
    }

    void SharedMemory::detach()
    {
        m_bDetached = true;
    }

} // namespace swinx
