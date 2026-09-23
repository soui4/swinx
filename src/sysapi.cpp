#include <windows.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <mach-o/dyld.h>
#include <sys/event.h>
#include <crt_externs.h>
#include <sys/sysctl.h>
#include <os/log.h>
#endif
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <assert.h>
#include <iconv.h>
#include <setjmp.h>
#include <poll.h>
#include <dirent.h>

extern char **environ; // POSIX 约定的环境指针（execve 备用）
#include <map>
#include <vector>
#include <set>
#include <string>
#include <atomic>
#include <mutex>
#include <algorithm>
#include "SConnection.h"
#include "wnd.h"
#include "uimsg.h"
#include "uniconv.h"
#include "cptable.h"
#include "synhandle.h"
#include "tostring.h"
#include "debug.h"
#include "sysapi.h"
#include "platform_api.h"
#include "SwinxUtils.h"
#include "cursormgr.h"
#ifdef __ANDROID__
#include <android/log.h>
#endif //__ANDROID__
// 声明外部函数
extern void UnloadModuleResources(HMODULE hModule);

#define kLogTag "sysapi"

#ifndef WCHAR_SIZE
#define WCHAR_SIZE 4
#endif // WCHAR_SIZE

using namespace swinx;
/*
ANSI_X3.4-1968 ANSI_X3.4-1986 ASCII CP367 IBM367 ISO-IR-6 ISO646-US ISO_646.IRV:1991 US US-ASCII CSASCII
UTF-8 UTF8
UTF-8-MAC UTF8-MAC
ISO-10646-UCS-2 UCS-2 CSUNICODE
UCS-2BE UNICODE-1-1 UNICODEBIG CSUNICODE11
UCS-2LE UNICODELITTLE
ISO-10646-UCS-4 UCS-4 CSUCS4
UCS-4BE
UCS-4LE
UTF-16
UTF-16BE
UTF-16LE
UTF-32
UTF-32BE
UTF-32LE
UNICODE-1-1-UTF-7 UTF-7 CSUNICODE11UTF7
UCS-2-INTERNAL
UCS-2-SWAPPED
UCS-4-INTERNAL
UCS-4-SWAPPED
C99
JAVA
CP819 IBM819 ISO-8859-1 ISO-IR-100 ISO8859-1 ISO_8859-1 ISO_8859-1:1987 L1 LATIN1 CSISOLATIN1
ISO-8859-2 ISO-IR-101 ISO8859-2 ISO_8859-2 ISO_8859-2:1987 L2 LATIN2 CSISOLATIN2
ISO-8859-3 ISO-IR-109 ISO8859-3 ISO_8859-3 ISO_8859-3:1988 L3 LATIN3 CSISOLATIN3
ISO-8859-4 ISO-IR-110 ISO8859-4 ISO_8859-4 ISO_8859-4:1988 L4 LATIN4 CSISOLATIN4
CYRILLIC ISO-8859-5 ISO-IR-144 ISO8859-5 ISO_8859-5 ISO_8859-5:1988 CSISOLATINCYRILLIC
ARABIC ASMO-708 ECMA-114 ISO-8859-6 ISO-IR-127 ISO8859-6 ISO_8859-6 ISO_8859-6:1987 CSISOLATINARABIC
ECMA-118 ELOT_928 GREEK GREEK8 ISO-8859-7 ISO-IR-126 ISO8859-7 ISO_8859-7 ISO_8859-7:1987 ISO_8859-7:2003 CSISOLATINGREEK
HEBREW ISO-8859-8 ISO-IR-138 ISO8859-8 ISO_8859-8 ISO_8859-8:1988 CSISOLATINHEBREW
ISO-8859-9 ISO-IR-148 ISO8859-9 ISO_8859-9 ISO_8859-9:1989 L5 LATIN5 CSISOLATIN5
ISO-8859-10 ISO-IR-157 ISO8859-10 ISO_8859-10 ISO_8859-10:1992 L6 LATIN6 CSISOLATIN6
ISO-8859-11 ISO8859-11 ISO_8859-11
ISO-8859-13 ISO-IR-179 ISO8859-13 ISO_8859-13 L7 LATIN7
ISO-8859-14 ISO-CELTIC ISO-IR-199 ISO8859-14 ISO_8859-14 ISO_8859-14:1998 L8 LATIN8
ISO-8859-15 ISO-IR-203 ISO8859-15 ISO_8859-15 ISO_8859-15:1998 LATIN-9
ISO-8859-16 ISO-IR-226 ISO8859-16 ISO_8859-16 ISO_8859-16:2001 L10 LATIN10
KOI8-R CSKOI8R
KOI8-U
KOI8-RU
CP1250 MS-EE WINDOWS-1250
CP1251 MS-CYRL WINDOWS-1251
CP1252 MS-ANSI WINDOWS-1252
CP1253 MS-GREEK WINDOWS-1253
CP1254 MS-TURK WINDOWS-1254
CP1255 MS-HEBR WINDOWS-1255
CP1256 MS-ARAB WINDOWS-1256
CP1257 WINBALTRIM WINDOWS-1257
CP1258 WINDOWS-1258
850 CP850 IBM850 CSPC850MULTILINGUAL
862 CP862 IBM862 CSPC862LATINHEBREW
866 CP866 IBM866 CSIBM866
MAC MACINTOSH MACROMAN CSMACINTOSH
MACCENTRALEUROPE
MACICELAND
MACCROATIAN
MACROMANIA
MACCYRILLIC
MACUKRAINE
MACGREEK
MACTURKISH
MACHEBREW
MACARABIC
MACTHAI
HP-ROMAN8 R8 ROMAN8 CSHPROMAN8
NEXTSTEP
ARMSCII-8
GEORGIAN-ACADEMY
GEORGIAN-PS
KOI8-T
CP154 CYRILLIC-ASIAN PT154 PTCP154 CSPTCP154
MULELAO-1
CP1133 IBM-CP1133
ISO-IR-166 TIS-620 TIS620 TIS620-0 TIS620.2529-1 TIS620.2533-0 TIS620.2533-1
CP874 WINDOWS-874
VISCII VISCII1.1-1 CSVISCII
TCVN TCVN-5712 TCVN5712-1 TCVN5712-1:1993
ISO-IR-14 ISO646-JP JIS_C6220-1969-RO JP CSISO14JISC6220RO
JISX0201-1976 JIS_X0201 X0201 CSHALFWIDTHKATAKANA
ISO-IR-87 JIS0208 JIS_C6226-1983 JIS_X0208 JIS_X0208-1983 JIS_X0208-1990 X0208 CSISO87JISX0208
ISO-IR-159 JIS_X0212 JIS_X0212-1990 JIS_X0212.1990-0 X0212 CSISO159JISX02121990
CN GB_1988-80 ISO-IR-57 ISO646-CN CSISO57GB1988
CHINESE GB_2312-80 ISO-IR-58 CSISO58GB231280
CN-GB-ISOIR165 ISO-IR-165
ISO-IR-149 KOREAN KSC_5601 KS_C_5601-1987 KS_C_5601-1989 CSKSC56011987
EUC-JP EUCJP EXTENDED_UNIX_CODE_PACKED_FORMAT_FOR_JAPANESE CSEUCPKDFMTJAPANESE
MS_KANJI SHIFT-JIS SHIFT_JIS SJIS CSSHIFTJIS
CP932
ISO-2022-JP CSISO2022JP
ISO-2022-JP-1
ISO-2022-JP-2 CSISO2022JP2
CN-GB EUC-CN EUCCN GB2312 CSGB2312
GBK
CP936 MS936 WINDOWS-936
GB18030
ISO-2022-CN CSISO2022CN
ISO-2022-CN-EXT
HZ HZ-GB-2312
EUC-TW EUCTW CSEUCTW
BIG-5 BIG-FIVE BIG5 BIGFIVE CN-BIG5 CSBIG5
CP950
BIG5-HKSCS:1999
BIG5-HKSCS:2001
BIG5-HKSCS BIG5-HKSCS:2004 BIG5HKSCS
EUC-KR EUCKR CSEUCKR
CP949 UHC
CP1361 JOHAB
ISO-2022-KR CSISO2022KR
CP856
CP922
CP943
CP1046
CP1124
CP1129
CP1161 IBM-1161 IBM1161 CSIBM1161
CP1162 IBM-1162 IBM1162 CSIBM1162
CP1163 IBM-1163 IBM1163 CSIBM1163
DEC-KANJI
DEC-HANYU
437 CP437 IBM437 CSPC8CODEPAGE437
CP737
CP775 IBM775 CSPC775BALTIC
852 CP852 IBM852 CSPCP852
CP853
855 CP855 IBM855 CSIBM855
857 CP857 IBM857 CSIBM857
CP858
860 CP860 IBM860 CSIBM860
861 CP-IS CP861 IBM861 CSIBM861
863 CP863 IBM863 CSIBM863
CP864 IBM864 CSIBM864
865 CP865 IBM865 CSIBM865
869 CP-GR CP869 IBM869 CSIBM869
CP1125
EUC-JISX0213
SHIFT_JISX0213
ISO-2022-JP-3
BIG5-2003
ISO-IR-230 TDS565
ATARI ATARIST
RISCOS-LATIN1
*/
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

tid_t GetCurrentThreadId()
{
    return (tid_t)pthread_self();
}

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

#ifdef __APPLE__
// 获取当前进程的 RPATH 列表
static int get_current_rpaths(swinx_stl::list<swinx_stl::string> &rpaths)
{
    // 1. 获取当前可执行文件的 Mach-O 头
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (header == nullptr)
    {
        std::cerr << "Failed to get Mach-O header." << std::endl;
        return 0;
    }

    // 2. 遍历加载命令
    uintptr_t cmd_ptr = (uintptr_t)(header + 1); // 第一个加载命令的地址
    for (uint32_t i = 0; i < header->ncmds; i++)
    {
        const struct load_command *cmd = (const struct load_command *)cmd_ptr;

        // 检查是否为 RPATH 命令
        if (cmd->cmd == LC_RPATH)
        {
            const struct rpath_command *rpath_cmd = (const struct rpath_command *)cmd;
            // 获取 RPATH 字符串（紧跟在命令结构体后）
            const char *rpath = (const char *)(rpath_cmd + 1);
            rpaths.push_back(rpath);
        }

        cmd_ptr += cmd->cmdsize; // 移动到下一个加载命令
    }

    return rpaths.size();
}
#endif //__APPLE__

class DllLoader {
  public:
    DllLoader()
    {
        // search app dir
        char szPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, szPath, MAX_PATH);
        char *p = strrchr(szPath, '/');
        assert(p);
        p[1] = 0;
#ifdef __APPLE__
        const char rpaths[] = "@executable_path/";
        swinx_stl::list<swinx_stl::string> lstRPaths;
        get_current_rpaths(lstRPaths);
        for (auto it : lstRPaths)
        {
            swinx_stl::string path = it;
            if (path.find(rpaths) == 0)
            {
                path.replace(0, sizeof(rpaths) - 1, szPath);
            }
            path += "/";
            m_lstDirs.push_back(path);
        }
#endif //__APPLE__
        m_lstDirs.push_back(szPath);
#ifdef __OHOS__
        m_lstDirs.push_back("/data/storage/el1/bundle/libs/arm64/");
        m_lstDirs.push_back("/data/storage/el1/bundle/libs/arm64-v8a/");
#endif
    }

    BOOL SetDllDirectoryA(LPCSTR lpPathName)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (lpPathName)
        {
            if ((GetFileAttributesA(lpPathName) & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return FALSE;
            m_userDllDir = lpPathName;
            if (*m_userDllDir.rbegin() != '/')
                m_userDllDir += '/';
            return TRUE;
        }
        else
        {
            m_userDllDir = "";
            return TRUE;
        }
    }

    DWORD GetDllDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_userDllDir.length() > nBufferLength)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        memcpy(lpBuffer, m_userDllDir.c_str(), m_userDllDir.length());
        if (nBufferLength > m_userDllDir.length())
            lpBuffer[m_userDllDir.length()] = 0;
        return m_userDllDir.length();
    }

    DWORD GetDllDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return MultiByteToWideChar(CP_UTF8, 0, m_userDllDir.c_str(), -1, lpBuffer, nBufferLength);
    }

    HMODULE LoadDll(LPCSTR lpFileName, int mode)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_userDllDir.empty())
        {
            swinx_stl::string path = m_userDllDir + lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        for (auto it : m_lstDirs)
        {
            swinx_stl::string path = it + lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        {
            // search current dir
            char szPath[MAX_PATH] = { 0 };
            int len = GetCurrentDirectoryA(MAX_PATH, szPath);
            if (szPath[len - 1] != '/')
            {
                szPath[len++] = '/';
                szPath[len] = 0;
            }
            swinx_stl::string path = szPath;
            path += lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        return NULL;
    }

    static HMODULE mydlopen(LPCSTR lpFileName, int mode)
    {
        HMODULE ret = dlopen(lpFileName, mode);
        if (ret)
            return ret;
        SLOG_FMTE("LoadLibraryA: dlopen failed for %s, error: %s", lpFileName, dlerror());
        return NULL;
    }

  private:
    std::mutex m_mutex;
    swinx_stl::list<swinx_stl::string> m_lstDirs;
    swinx_stl::string m_userDllDir;
};

static DllLoader s_dllLoader;

HMODULE WINAPI LoadLibraryA(LPCSTR lpFileName)
{

    if (!lpFileName)
        return NULL;
    HMODULE ret = 0;
    do
    {
        ret = dlopen(lpFileName, RTLD_NOW);
        if (ret)
            break;
        DWORD dwAttrib = GetFileAttributesA(lpFileName);
        if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            SLOG_FMTE("LoadLibraryA: dlopen failed for %s, error: %s", lpFileName, dlerror());
            break;
        }
        ret = s_dllLoader.LoadDll(lpFileName, RTLD_NOW);
        if (ret)
            break;
        char szPath[MAX_PATH];
        const char *ext = strrchr(lpFileName, '.');
        if (!ext)
        {
// no ext, add so as the extend name
#ifdef __APPLE__
            sprintf(szPath, "%s.dylib", lpFileName);
#else
            sprintf(szPath, "%s.so", lpFileName);
#endif
        }
        else if (stricmp(ext, ".dll") == 0)
        {
            // windows dll name pattern, change to xxx.so
            strcpy(szPath, lpFileName);
#ifdef __APPLE__
            strcpy(szPath + (ext - lpFileName), ".dylib");
#else
            strcpy(szPath + (ext - lpFileName), ".so");
#endif
            ret = s_dllLoader.LoadDll(szPath, RTLD_NOW);
            if (ret)
                break;
        }
        else
        {
            strcpy(szPath, lpFileName);
        }
        ret = s_dllLoader.LoadDll(szPath, RTLD_NOW);
        if (ret)
            return ret;
        if (strchr(szPath, '/') == NULL && strncmp(szPath, "lib", 3) != 0)
        {
            // add lib prefix to szPath;
            char szTmp[MAX_PATH];
            strcpy(szTmp, szPath);
            snprintf(szPath, sizeof(szPath), "lib%s", szTmp);
        }
        ret = s_dllLoader.LoadDll(szPath, RTLD_NOW);
    } while (false);
    return ret;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR lpFileName)
{
    char szName[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, szName, MAX_PATH, NULL, NULL))
        return 0;
    return LoadLibraryA(szName);
}

FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    if (!hModule || hModule == GetModuleHandle(nullptr))
        return (FARPROC)dlsym(RTLD_DEFAULT, lpProcName);
    else
        return (FARPROC)dlsym(hModule, lpProcName);
}

BOOL WINAPI FreeLibrary(HMODULE hModule)
{
    // 卸载模块资源
    UnloadModuleResources(hModule);
    // dlclose returns 0 on success, Win32 wants TRUE
    return dlclose(hModule) == 0;
}

DWORD WINAPI GetDllDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
    return s_dllLoader.GetDllDirectoryA(nBufferLength, lpBuffer);
}

BOOL WINAPI SetDllDirectoryA(LPCSTR lpPathName)
{
    return s_dllLoader.SetDllDirectoryA(lpPathName);
}

DWORD WINAPI GetDllDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    return s_dllLoader.GetDllDirectoryW(nBufferLength, lpBuffer);
}

BOOL WINAPI SetDllDirectoryW(LPCWSTR lpPathName)
{
    if (!lpPathName)
        return SetDllDirectoryA(NULL);
    swinx_stl::string str;
    tostring(lpPathName, -1, str);
    return SetDllDirectoryA(str.c_str());
}

#define STIF_DEFAULT     0x00000000L
#define STIF_SUPPORT_HEX 0x00000001L

BOOL StrToInt64ExW(const wchar_t *str, DWORD flags, LONGLONG *ret)
{
    BOOL negative = FALSE;
    LONGLONG value = 0;

    if (!str || !ret)
        return FALSE;

    /* Skip leading space, '+', '-' */
    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;

    if (*str == '-')
    {
        negative = TRUE;
        str++;
    }
    else if (*str == '+')
        str++;

    if (flags & STIF_SUPPORT_HEX && *str == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        /* Read hex number */
        str += 2;

        if (!isxdigit(*str))
            return FALSE;

        while (isxdigit(*str))
        {
            value *= 16;
            if (*str >= '0' && *str <= '9')
                value += (*str - '0');
            else if (*str >= 'A' && *str <= 'Z')
                value += 10 + (*str - 'A');
            else
                value += 10 + (*str - 'a');
            str++;
        }

        *ret = value;
        return TRUE;
    }

    /* Read decimal number */
    if (*str < '0' || *str > '9')
        return FALSE;

    while (*str >= '0' && *str <= '9')
    {
        value *= 10;
        value += (*str - '0');
        str++;
    }

    *ret = negative ? -value : value;
    return TRUE;
}

BOOL StrToIntExW(const wchar_t *str, DWORD flags, INT *ret)
{
    LONGLONG value;
    BOOL res;
    res = StrToInt64ExW(str, flags, &value);
    if (res)
        *ret = value;
    return res;
}

BOOL StrToInt64ExA(const char *str, DWORD flags, LONGLONG *ret)
{
    std::wstring wstr;
    towstring(str, -1, wstr);
    return StrToInt64ExW(wstr.c_str(), flags, ret);
}

BOOL StrToIntExA(const char *str, DWORD flags, INT *ret)
{
    LONGLONG value;
    BOOL res;
    res = StrToInt64ExA(str, flags, &value);
    if (res)
        *ret = value;
    return res;
}

void GetLocalTime(SYSTEMTIME *pSysTime)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *now = localtime(&tv.tv_sec);
    pSysTime->wYear = now->tm_year + 1900;
    pSysTime->wMonth = now->tm_mon + 1;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = tv.tv_usec / 1000;
}

void GetSystemTime(SYSTEMTIME *pSysTime)
{
    struct timeval tvNow;
    gettimeofday(&tvNow, 0);

    struct tm *now = localtime(&tvNow.tv_sec);
    pSysTime->wYear = now->tm_year + 1900;
    pSysTime->wMonth = now->tm_mon + 1;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = tvNow.tv_usec / 1000;
}

// Current timezone bias in seconds east of UTC (e.g. +28800 for UTC+8).
// Win32's FileTimeToLocalFileTime / LocalFileTimeToFileTime apply the
// current bias as a plain arithmetic offset; DST history is not involved.
static int64_t get_local_utc_bias_seconds()
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    return lt ? (int64_t)lt->tm_gmtoff : 0;
}

BOOL LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime)
{
    if (!lpLocalFileTime || !lpFileTime)
        return FALSE;
    ULONGLONG local = ((ULONGLONG)lpLocalFileTime->dwHighDateTime << 32) | lpLocalFileTime->dwLowDateTime;
    ULONGLONG utc = local - (ULONGLONG)(get_local_utc_bias_seconds() * 10000000LL);
    lpFileTime->dwLowDateTime = (DWORD)utc;
    lpFileTime->dwHighDateTime = (DWORD)(utc >> 32);
    return TRUE;
}

BOOL FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
    if (!lpFileTime || !lpLocalFileTime)
        return FALSE;
    ULONGLONG utc = ((ULONGLONG)lpFileTime->dwHighDateTime << 32) | lpFileTime->dwLowDateTime;
    ULONGLONG local = utc + (ULONGLONG)(get_local_utc_bias_seconds() * 10000000LL);
    lpLocalFileTime->dwLowDateTime = (DWORD)local;
    lpLocalFileTime->dwHighDateTime = (DWORD)(local >> 32);
    return TRUE;
}

time_t _mkgmtime(struct tm *_Tm)
{
    return mktime(_Tm);
}

int _localtime64_s(struct tm *ptm, const __time64_t *ptime)
{
    *ptm = *localtime((time_t *)ptime);
    return 0;
}

static inline BOOL is_valid_hex(WCHAR c)
{
    if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'))))
        return FALSE;
    return TRUE;
}

static const BYTE guid_conv_table[256] = {
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 */
    0, 1,   2,   3,   4,   5,   6,   7, 8, 9, 0, 0, 0, 0, 0, 0, /* 0x30 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf                             /* 0x60 */
};

BOOL IIDFromString(LPCWSTR s, GUID *id)
{
    int i;

    if (!s || s[0] != '{')
    {
        memset(id, 0, sizeof(*id));
        if (!s)
            return TRUE;
        return FALSE;
    }

    /* In form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} */

    id->Data1 = 0;
    for (i = 1; i < 9; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data1 = (id->Data1 << 4) | guid_conv_table[s[i]];
    }
    if (s[9] != '-')
        return FALSE;

    id->Data2 = 0;
    for (i = 10; i < 14; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data2 = (id->Data2 << 4) | guid_conv_table[s[i]];
    }
    if (s[14] != '-')
        return FALSE;

    id->Data3 = 0;
    for (i = 15; i < 19; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data3 = (id->Data3 << 4) | guid_conv_table[s[i]];
    }
    if (s[19] != '-')
        return FALSE;

    for (i = 20; i < 37; i += 2)
    {
        if (i == 24)
        {
            if (s[i] != '-')
                return FALSE;
            i++;
        }
        if (!is_valid_hex(s[i]) || !is_valid_hex(s[i + 1]))
            return FALSE;
        id->Data4[(i - 20) / 2] = guid_conv_table[s[i]] << 4 | guid_conv_table[s[i + 1]];
    }

    if (s[37] == '}' && s[38] == '\0')
        return TRUE;

    return FALSE;
}

#define __COMPARE(context, p1, p2)                comp(context, p1, p2)
#define __SHORTSORT(lo, hi, width, comp, context) shortsort_s(lo, hi, width, comp, context);

#ifndef _ASSERT_EXPR
#define _ASSERT_EXPR(expr, expr_str) ((void)0)
#endif
#ifndef _CRT_WIDE
#define __CRT_WIDE(_String) L##_String
#define _CRT_WIDE(_String)  __CRT_WIDE(_String)
#endif

#define _INVALID_PARAMETER(expr) _CALL_INVALID_PARAMETER_FUNC(_invalid_parameter, expr)

#define _VALIDATE_RETURN_VOID(expr, errorcode)       \
    {                                                \
        int _Expr_val = !!(expr);                    \
        _ASSERT_EXPR((_Expr_val), _CRT_WIDE(#expr)); \
        if (!(_Expr_val))                            \
        {                                            \
            errno = errorcode;                       \
            return;                                  \
        }                                            \
    }

#define STKSIZ (8 * sizeof(void *) - 2)
#define CUTOFF 8 /* testing shows that this is good value */

static void shortsort_s(char *lo, char *hi, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *);
#define swap swap_c

static void swap(char *p, char *q, size_t width);

LONG InterlockedDecrement(LONG volatile *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

LONG InterlockedIncrement(LONG volatile *v)
{
    return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
}

LONG InterlockedCompareExchange(LONG volatile *v, LONG Exchange, LONG Comparand)
{
    // Win32 semantics: return the ORIGINAL value of *v.
    // On failure __atomic_compare_exchange_n stores the original into
    // Comparand; on success Comparand already holds it (it matched).
    __atomic_compare_exchange_n(v, &Comparand, Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comparand;
}

LONG InterlockedExchangeAdd(LONG volatile *v, LONG Increment)
{
    return __sync_add_and_fetch(v, Increment);
}

int64_t InterlockedExchangeAdd64(int64_t volatile *v, int64_t Increment)
{
    return __sync_add_and_fetch(v, Increment);
}

int64_t InterlockedDecrement64(int64_t volatile *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

int64_t InterlockedIncrement64(int64_t volatile *v)
{
    return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
}

int64_t InterlockedCompareExchange64(int64_t volatile *v, int64_t Exchange, int64_t Comparand)
{
    // Win32 semantics: return the ORIGINAL value (see 32-bit version)
    __atomic_compare_exchange_n(v, &Comparand, Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comparand;
}

void qsort_s(void *base, size_t num, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *context)
{
    char *lo, *hi;       /* ends of sub-array currently sorting */
    char *mid;           /* points to middle of subarray */
    char *loguy, *higuy; /* traveling pointers for partition step */
    size_t size;         /* size of the sub-array */
    char *lostk[STKSIZ], *histk[STKSIZ];
    int stkptr; /* stack for saving sub-array to be processed */

    /* validation section */
    _VALIDATE_RETURN_VOID(base != NULL || num == 0, EINVAL);
    _VALIDATE_RETURN_VOID(width > 0, EINVAL);
    _VALIDATE_RETURN_VOID(comp != NULL, EINVAL);

    if (num < 2)
        return; /* nothing to do */

    stkptr = 0; /* initialize stack */

    lo = (char *)base;
    hi = (char *)base + width * (num - 1); /* initialize limits */

    /* this entry point is for pseudo-recursion calling: setting
       lo and hi and jumping to here is like recursion, but stkptr is
       preserved, locals aren't, so we preserve stuff on the stack */
recurse:

    size = (hi - lo) / width + 1; /* number of el's to sort */

    /* below a certain size, it is faster to use a O(n^2) sorting method */
    if (size <= CUTOFF)
    {
        __SHORTSORT(lo, hi, width, comp, context);
    }
    else
    {
        /* First we pick a partitioning element.  The efficiency of the
           algorithm demands that we find one that is approximately the median
           of the values, but also that we select one fast.  We choose the
           median of the first, middle, and last elements, to avoid bad
           performance in the face of already sorted data, or data that is made
           up of multiple sorted runs appended together.  Testing shows that a
           median-of-three algorithm provides better performance than simply
           picking the middle element for the latter case. */

        mid = lo + (size / 2) * width; /* find middle element */

        /* Sort the first, middle, last elements into order */
        if (__COMPARE(context, lo, mid) > 0)
        {
            swap(lo, mid, width);
        }
        if (__COMPARE(context, lo, hi) > 0)
        {
            swap(lo, hi, width);
        }
        if (__COMPARE(context, mid, hi) > 0)
        {
            swap(mid, hi, width);
        }

        /* We now wish to partition the array into three pieces, one consisting
           of elements <= partition element, one of elements equal to the
           partition element, and one of elements > than it.  This is done
           below; comments indicate conditions established at every step. */

        loguy = lo;
        higuy = hi;

        /* Note that higuy decreases and loguy increases on every iteration,
           so loop must terminate. */
        for (;;)
        {
            /* lo <= loguy < hi, lo < higuy <= hi,
               A[i] <= A[mid] for lo <= i <= loguy,
               A[i] > A[mid] for higuy <= i < hi,
               A[hi] >= A[mid] */

            /* The doubled loop is to avoid calling comp(mid,mid), since some
               existing comparison funcs don't work when passed the same
               value for both pointers. */

            if (mid > loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy < mid && __COMPARE(context, loguy, mid) <= 0);
            }
            if (mid <= loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy <= hi && __COMPARE(context, loguy, mid) <= 0);
            }

            /* lo < loguy <= hi+1, A[i] <= A[mid] for lo <= i < loguy,
               either loguy > hi or A[loguy] > A[mid] */

            do
            {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) > 0);

            /* lo <= higuy < hi, A[i] > A[mid] for higuy < i < hi,
               either higuy == lo or A[higuy] <= A[mid] */

            if (higuy < loguy)
                break;

            /* if loguy > hi or higuy == lo, then we would have exited, so
               A[loguy] > A[mid], A[higuy] <= A[mid],
               loguy <= hi, higuy > lo */

            swap(loguy, higuy, width);

            /* If the partition element was moved, follow it.  Only need
               to check for mid == higuy, since before the swap,
               A[loguy] > A[mid] implies loguy != mid. */

            if (mid == higuy)
                mid = loguy;

            /* A[loguy] <= A[mid], A[higuy] > A[mid]; so condition at top
               of loop is re-established */
        }

        /*     A[i] <= A[mid] for lo <= i < loguy,
               A[i] > A[mid] for higuy < i < hi,
               A[hi] >= A[mid]
               higuy < loguy
           implying:
               higuy == loguy-1
               or higuy == hi - 1, loguy == hi + 1, A[hi] == A[mid] */

        /* Find adjacent elements equal to the partition element.  The
           doubled loop is to avoid calling comp(mid,mid), since some
           existing comparison funcs don't work when passed the same value
           for both pointers. */

        higuy += width;
        if (mid < higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) == 0);
        }
        if (mid >= higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > lo && __COMPARE(context, higuy, mid) == 0);
        }

        /* OK, now we have the following:
              higuy < loguy
              lo <= higuy <= hi
              A[i]  <= A[mid] for lo <= i <= higuy
              A[i]  == A[mid] for higuy < i < loguy
              A[i]  >  A[mid] for loguy <= i < hi
              A[hi] >= A[mid] */

        /* We've finished the partition, now we want to sort the subarrays
           [lo, higuy] and [loguy, hi].
           We do the smaller one first to minimize stack usage.
           We only sort arrays of length 2 or more.*/

        if (higuy - lo >= hi - loguy)
        {
            if (lo < higuy)
            {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy;
                ++stkptr;
            } /* save big recursion for later */

            if (loguy < hi)
            {
                lo = loguy;
                goto recurse; /* do small recursion */
            }
        }
        else
        {
            if (loguy < hi)
            {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                ++stkptr; /* save big recursion for later */
            }

            if (lo < higuy)
            {
                hi = higuy;
                goto recurse; /* do small recursion */
            }
        }
    }

    /* We have sorted the array, except for any pending sorts on the stack.
       Check if there are any, and do them. */

    --stkptr;
    if (stkptr >= 0)
    {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse; /* pop subarray from stack */
    }
    else
        return; /* all subarrays done */
}

/***
 *shortsort(hi, lo, width, comp) - insertion sort for sorting short arrays
 *shortsort_s(hi, lo, width, comp, context) - insertion sort for sorting short arrays
 *
 *Purpose:
 *       sorts the sub-array of elements between lo and hi (inclusive)
 *       side effects:  sorts in place
 *       assumes that lo < hi
 *
 *Entry:
 *       char *lo = pointer to low element to sort
 *       char *hi = pointer to high element to sort
 *       size_t width = width in bytes of each array element
 *       int (*comp)() = pointer to function returning analog of strcmp for
 *               strings, but supplied by user for comparing the array elements.
 *               it accepts 2 pointers to elements, together with a pointer to a context
 *               (if present). Returns neg if 1<2, 0 if 1=2, pos if 1>2.
 *       void *context - pointer to the context in which the function is
 *               called. This context is passed to the comparison function.
 *
 *Exit:
 *       returns void
 *
 *Exceptions:
 *
 *******************************************************************************/

static void shortsort_s(char *lo, char *hi, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *context)
{
    char *p, *max;

    /* Note: in assertions below, i and j are alway inside original bound of
       array to sort. */

    while (hi > lo)
    {
        /* A[i] <= A[j] for i <= j, j > hi */
        max = lo;
        for (p = lo + width; p <= hi; p += width)
        {
            /* A[i] <= A[max] for lo <= i < p */
            if (__COMPARE(context, p, max) > 0)
            {
                max = p;
            }
            /* A[i] <= A[max] for lo <= i <= p */
        }

        /* A[i] <= A[max] for lo <= i <= hi */

        swap(max, hi, width);

        /* A[i] <= A[hi] for i <= hi, so A[i] <= A[j] for i <= j, j >= hi */

        hi -= width;

        /* A[i] <= A[j] for i <= j, j > hi, loop top condition established */
    }
    /* A[i] <= A[j] for i <= j, j > lo, which implies A[i] <= A[j] for i < j,
       so array is sorted */
}

/***
 *swap(a, b, width) - swap two elements
 *
 *Purpose:
 *       swaps the two array elements of size width
 *
 *Entry:
 *       char *a, *b = pointer to two elements to swap
 *       size_t width = width in bytes of each array element
 *
 *Exit:
 *       returns void
 *
 *Exceptions:
 *
 *******************************************************************************/

static void swap(char *a, char *b, size_t width)
{
    char tmp;

    if (a != b)
        /* Do the swap one character at a time to avoid potential alignment
           problems. */
        while (width--)
        {
            tmp = *a;
            *a++ = *b;
            *b++ = tmp;
        }
}

#undef __COMPARE
#undef __SHORTSORT
#undef swap

void PostThreadMessageW(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{
    return PostThreadMessageA(tid, msg, wp, lp);
}
void PostThreadMessageA(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{
    SConnection *conn = SConnMgr::instance()->getConnection(tid);
    if (!conn)
        return;
    conn->postMsg(0, msg, wp, lp);
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

/* WinEvent 异步派发（实现位于 src/oleacc.cpp，所有平台变体均在编译）：
 * OUTOFCONTEXT 钩子的回调与 user32 一致，在消息泵里投递。 */
extern "C" void WINAPI SwinxDispatchPendingWinEvents(void);

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
    // 它们要么由原生窗口管理器画在窗口矩形之外（Linux 的 _MOTIF_WM_HINTS、macOS 的
    // NSWindowStyleMask），要么由上层 UI 库（SOUI 的菜单控件）负责，因此在 swinx 的
    // 窗口矩形里厚度恒为 0。显式返回 0 而不是落进 default 分支，免得每次查询都刷
    // 一遍 "unknown index"——0 在这里是"确知没有"，不是"未实现"。
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
#ifdef __APPLE__
    if (nIndex == SM_CXCURSOR || nIndex == SM_CYCURSOR)
        return ret;
#endif
    return ret * GetSystemScale() / 100;
}

#define PROC_EVENT_FMT "proc_event_E23A140E-2711-44CE-AE4E-67D55217FF7A_%u"

class ChildStatusMgr {
  public:
    ChildStatusMgr()
    {
    }

    void setPidCode(pid_t pid, int status)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_child_status[pid] = status;
    }

    BOOL getExitCode(pid_t pid, LPDWORD lpExitCode)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_child_status.find(pid);
        if (it == m_child_status.end())
            return FALSE;
        int status = it->second;

        if (WIFEXITED(status))
        {
            *lpExitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            *lpExitCode = -1;
        }
        return TRUE;
    }

  private:
    swinx_stl::map<pid_t, int> m_child_status;
    std::mutex m_mutex;
} s_child_status_mgr;

// ---- SIGCHLD self-pipe 方案 ----
// 信号处理器只允许 async-signal-safe 操作（man 7 signal-safety）。旧实现把
// setPidCode(std::mutex)、sprintf、CreateEventA（全局句柄表写锁 = sem_wait）
// 全部放在信号上下文里：SIGCHLD 打断持有上述任一锁的线程会自死锁。
// 现在 handler 只向 self-pipe 写一个唤醒字节（write 是安全的，写端
// O_NONBLOCK 保证永不阻塞），收尸与登记移到排水线程的普通上下文完成。
// 等待方（proc_event 命名事件的 FIFO select）零改动。
static int s_sigchld_pipe[2] = { -1, -1 }; // [0]=排水线程阻塞读, [1]=handler 非阻塞写

static void sigchld_handler(int signo __attribute__((unused)))
{
    // 唤醒字节允许被合并甚至丢弃（管道满时 EAGAIN）：排水线程每次醒来
    // 都会把 waitpid 清到无子进程可收，不会漏掉任何一次退出。
    char b = 1;
    ssize_t nRet = write(s_sigchld_pipe[1], &b, 1);
    (void)nRet;
}

static void *sigchld_drain_thread(void * /*unused*/)
{
    for (;;)
    {
        char b;
        ssize_t nRet;
        do
        {
            nRet = read(s_sigchld_pipe[0], &b, 1); // 阻塞等待唤醒
        } while (nRet == -1 && errno == EINTR);
        if (nRet <= 0)
            break; // 写端被关闭/管道异常：线程退出，不再收尸（仅进程收尾期可能发生）

        // 普通上下文：可安全取 std::mutex / 句柄表写锁 / 分配内存。
        // SIGCHLD 是合并型信号，循环收尸保证多个子进程退出一个不漏；
        // 剩余唤醒字节会在下一轮 read 立即返回，waitpid 空转一次无害。
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            s_child_status_mgr.setPidCode(pid, status);
            char szName[100];
            sprintf(szName, PROC_EVENT_FMT, pid);
            HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, szName);
            SetEvent(hEvent);
            CloseHandle(hEvent);
        }
    }
    return nullptr;
}

static std::mutex s_mutex_sigchild;
static bool s_sigchild_flag = false;
int install_sigchld_handler()
{
    std::unique_lock<std::mutex> lock(s_mutex_sigchild);
    if (s_sigchild_flag)
        return 0;
    s_sigchild_flag = true;

    if (pipe(s_sigchld_pipe) == -1)
    {
        s_sigchld_pipe[0] = s_sigchld_pipe[1] = -1;
        SLOG_STMW() << "sigchld self-pipe create failed, errno=" << errno;
        return -1;
    }
    // 写端非阻塞：信号处理器内绝不允许阻塞（排水线程卡死时 EAGAIN 丢弃）
    int flags = fcntl(s_sigchld_pipe[1], F_GETFL, 0);
    if (flags != -1)
        fcntl(s_sigchld_pipe[1], F_SETFL, flags | O_NONBLOCK);
    // CLOEXEC：同步管道不泄露给 fork 出的子进程
    fcntl(s_sigchld_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(s_sigchld_pipe[1], F_SETFD, FD_CLOEXEC);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    int nRet = pthread_create(&tid, &attr, sigchld_drain_thread, nullptr);
    pthread_attr_destroy(&attr);
    if (nRet != 0)
    {
        // 排水线程建不起来时收尸将无人执行（子进程会变僵尸），干脆不装
        // handler，保持与未安装时一致的行为（仅丢失退出通知）。
        close(s_sigchld_pipe[0]);
        close(s_sigchld_pipe[1]);
        s_sigchld_pipe[0] = s_sigchld_pipe[1] = -1;
        SLOG_STMW() << "sigchld drain thread create failed, errno=" << nRet;
        return -1;
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // 选项可以根据需要调整
    sigaction(SIGCHLD, &sa, NULL);
    return 1;
}

BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
    if (!lpExitCode)
        return FALSE;

    pid_t pid = GetProcessId(hProcess);
    if (!pid)
        return FALSE;
    return s_child_status_mgr.getExitCode(pid, lpExitCode);
}

#define PROC_STATUS "/proc/%d/status"
// 读取指定PID的进程状态文件，获取其有效用户ID
int WINAPI get_process_uid(int pid)
{
    char filename[64];
    FILE *file;
    char line[256];
    int uid = -1;

    // 构造状态文件路径
    snprintf(filename, sizeof(filename), PROC_STATUS, pid);

    // 打开状态文件
    file = fopen(filename, "r");
    if (!file)
    {
        perror("无法打开进程状态文件");
        return -1;
    }

    // 逐行读取文件内容，查找Uid字段
    while (fgets(line, sizeof(line), file))
    {
        if (strncmp(line, "Uid:", 4) == 0)
        {
            // 解析Uid字段，获取有效用户ID
            sscanf(line, "Uid:\t%d", &uid);
            break;
        }
    }

    fclose(file);
    return uid;
}

DWORD WINAPI GetCurrentProcessId()
{
    return (DWORD)getpid();
}

DWORD WINAPI GetProcessId(HANDLE hProcess)
{
    if (hProcess == INVALID_HANDLE_VALUE)
        return getpid();
    char szName[1001];
    if (!GetHandleName(hProcess, szName))
        return 0;
    DWORD pid;
    if (1 != sscanf(szName, PROC_EVENT_FMT, &pid))
        return 0;
    return pid;
}

HANDLE WINAPI GetCurrentProcess_Priv(void)
{
    return INVALID_HANDLE_VALUE; // return a pseudo handle
}

static void RedirectFd(HANDLE h, int fd2)
{
    if (!h)
        return;
    int fd = _open_osfhandle(h, 0);
    if (fd == -1)
        return;
    dup2(fd, fd2);
}

BOOL WINAPI CreateProcessAsUserA(HANDLE hToken, LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes __attribute__((unused)), LPSECURITY_ATTRIBUTES lpThreadAttributes __attribute__((unused)), BOOL bInheritHandles __attribute__((unused)), DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    if (!lpApplicationName)
    {
        if (!lpCommandLine)
            return FALSE;
        lpApplicationName = lpCommandLine;
        if (lpApplicationName[0] == '\"')
            lpCommandLine = strstr(lpCommandLine, "\" ");
        else
            lpCommandLine = strchr(lpCommandLine, ' ');
        if (lpCommandLine)
        {
            lpCommandLine += lpApplicationName[0] == '\"' ? 2 : 1;
            lpCommandLine[-1] = '\0';
        }
    }
    install_sigchld_handler();

    swinx_stl::list<char *> lstArg;
    lstArg.push_back((char *)lpApplicationName);
    while (lpCommandLine)
    {
        char *pArgEnd = nullptr;
        if (lpCommandLine[0] == '\"')
        {
            lstArg.push_back(lpCommandLine + 1);
            pArgEnd = strstr(lpCommandLine + 1, "\" ");
            if (pArgEnd)
            {
                pArgEnd[0] = 0;
                pArgEnd += 2;
            }
            else
            {
                pArgEnd = strrchr(lpCommandLine + 1, '\"');
                if (pArgEnd)
                {
                    // for last param
                    pArgEnd[0] = 0;
                    pArgEnd = NULL;
                }
            }
        }
        else
        {
            lstArg.push_back(lpCommandLine);
            pArgEnd = strchr(lpCommandLine, ' ');
            if (pArgEnd)
            {
                pArgEnd[0] = 0;
                pArgEnd++;
            }
        }
        lpCommandLine = pArgEnd;
    }
    if (lstArg.size() > 1000)
        return FALSE; // 子进程退出

    // ---- 一切需要取锁/分配内存的准备都在 fork 之前完成 ----
    // 多线程进程 fork 只继承调用线程，其它线程（如 SIGCHLD 排水线程）在
    // fork 瞬间持有的锁——包括 shm 中的进程共享句柄表锁、malloc 锁——会被
    // 冻结在"已加锁"状态；子进程若在 exec 前调用任何取锁的 CRT/swinx API
    // 就会永久卡死，且卡死的子进程会永久占住 shm 锁、连带阻塞父进程
    // （macOS 快速连续 spawn 时实测触发）。因此子进程路径只允许系统调用。
    swinx_stl::vector<char *> args;
    swinx_stl::string strHost;
    if ((UINT_PTR)hToken == Verb_RunAs)
    {
        // 提权宿主在父进程内探测（原实现在子进程内调 swinx API，fork 不安全）
        const char *hosts[] = { "/usr/bin/pkexec", "/usr/bin/kdesu", "/usr/bin/gksu" };
        for (int j = 0; j < (int)(sizeof(hosts) / sizeof(hosts[0])); j++)
        {
            if (access(hosts[j], F_OK) == 0)
            {
                strHost = hosts[j];
                break;
            }
        }
        if (strHost.empty())
            return FALSE;
    }
    // argv 直接取 lstArg（其首元素即 lpApplicationName，勿重复 push：
    // 重复会让 sh 把 "/bin/sh" 当作脚本文件名，退出码全错）。
    for (auto it = lstArg.begin(); it != lstArg.end(); it++)
    {
        args.push_back(*it);
    }
    // execve 语义要求 argv/envp 都是以 NULL 指针结尾的数组：内核 count()
    // 依赖 NULL 终止符，缺失会越界扫描（典型结果 EFAULT，execve 失败）。
    args.push_back(nullptr);

    // env 组成保持与原实现一致：DISPLAY/XAUTHORITY（非 RunAs）+ lpEnvironment
    swinx_stl::vector<swinx_stl::string> envStore;
    if ((UINT_PTR)hToken != Verb_RunAs)
    {
        const char *szDisplay = getenv("DISPLAY");
        const char *szAuth = getenv("XAUTHORITY");
        if (szDisplay)
            envStore.push_back(swinx_stl::string("DISPLAY=") + szDisplay);
        if (szAuth)
            envStore.push_back(swinx_stl::string("XAUTHORITY=") + szAuth);
    }
    else
    {
        // RunAs 走 pkexec："env DISPLAY=... XAUTHORITY=..." 作为参数注入
        const char *szDisplay = getenv("DISPLAY");
        const char *szAuth = getenv("XAUTHORITY");
        envStore.push_back(swinx_stl::string("DISPLAY=") + (szDisplay ? szDisplay : ""));
        envStore.push_back(swinx_stl::string("XAUTHORITY=") + (szAuth ? szAuth : ""));
    }
    if (lpEnvironment)
    {
        if (dwCreationFlags & CREATE_UNICODE_ENVIRONMENT)
        {
            LPCWSTR pszEnv = (LPCWSTR)lpEnvironment;
            while (*pszEnv)
            {
                size_t len = wcslen(pszEnv);
                swinx_stl::string strEnv;
                tostring(pszEnv, -1, strEnv);
                envStore.push_back(strEnv);
                pszEnv += len + 1;
            }
        }
        else
        {
            LPCSTR pszEnv = (LPCSTR)lpEnvironment;
            while (*pszEnv)
            {
                size_t len = strlen(pszEnv);
                envStore.push_back(swinx_stl::string(pszEnv, len));
                pszEnv += len + 1;
            }
        }
    }
    swinx_stl::vector<char *> envs;
    for (auto it = envStore.begin(); it != envStore.end() && envs.size() < 1000; it++)
    {
        envs.push_back((char *)it->c_str());
    }
    // NULL 结尾（见 args 处说明）。注意无 DISPLAY/XAUTHORITY 的会话
    // （SSH/纯终端）envStore 可能为空：空环境 = 仅一个 nullptr，合法；
    // 若不补 nullptr，envs.data() 为 nullptr，execve 直接 EFAULT。
    envs.push_back(nullptr);

    // CLOEXEC 同步管道：execve 成功时写端被自动关闭 -> 父进程读到 EOF；
    // execve 失败时子进程把 errno 写回（4 字节）后 _exit
    int fds[2] = { -1, -1 };
    if (pipe(fds) == -1)
        return FALSE;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid == -1)
    {
        // fork 失败
        close(fds[0]);
        close(fds[1]);
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        // ============================================================
        // exec 前只允许系统调用（chdir/dup2/execve/write/_exit）：
        // 任何 CRT/swinx API 都可能踩到 fork 时被冻结的锁。
        // "已启动"事件不再由子进程创建/置位（改由父进程负责）。
        // ============================================================
        if (lpCurrentDirectory && chdir(lpCurrentDirectory) != 0)
            _exit(126);
        if (lpStartupInfo)
        {
            // redirect stdin/out/err
            RedirectFd(lpStartupInfo->hStdInput, STDIN_FILENO);
            RedirectFd(lpStartupInfo->hStdOutput, STDOUT_FILENO);
            RedirectFd(lpStartupInfo->hStdError, STDERR_FILENO);
        }
        if ((UINT_PTR)hToken == Verb_RunAs)
        {
            // pkexec 的参数式环境注入："env DISPLAY=... XAUTHORITY=..." 放在最前
            size_t nIns = envStore.size() < 2 ? envStore.size() : 2;
            for (size_t k = 0; k < nIns; k++)
            {
                args.insert(args.begin() + 2 + k, (char *)envStore[k].c_str());
            }
            args.insert(args.begin() + 1, (char *)"env");
        }
        execve(args[0], args.data(), envs.data()); // 替换子进程的代码为新程序
        int err = errno;
        ssize_t nRet = write(fds[1], &err, sizeof(err)); // exec 失败：errno 回传父进程
        (void)nRet;
        _exit(127); // 不能用 exit()：会踩到可能被冻结的 atexit/malloc 锁
    }
    else
    {
        close(fds[1]); // 父进程只读

        char szName[100];
        // must match the name the PARENT creates/signals after fork
        // (PROC_EVENT_FMT) — the child no longer touches swinx objects
        sprintf(szName, PROC_EVENT_FMT, pid);
        HANDLE hProcess = CreateEventA(NULL, TRUE, FALSE, szName);

        // 等待 exec 结果：EOF = execve 已执行（CLOEXEC 自动关闭写端）；
        // 读到 4 字节 = execve 失败（errno 由子进程回传）；超时 = 子进程
        // 存活但尚未 exec（极端负载），按已启动处理
        int err = 0;
        bool bExecFailed = false;
        struct pollfd pfd = { fds[0], POLLIN, 0 };
        int nPoll;
        do
        {
            nPoll = poll(&pfd, 1, 2000);
        } while (nPoll == -1 && errno == EINTR);
        if (nPoll > 0)
        {
            ssize_t nRet = read(fds[0], &err, sizeof(err)); // EOF -> 0, errno -> 4
            if (nRet == (ssize_t)sizeof(err))
                bExecFailed = true;
        }
        close(fds[0]);

        if (bExecFailed)
        {
            CloseHandle(hProcess);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return FALSE;
        }
        if (!hProcess)
            return FALSE;
        // 子进程已 exec（或超时仍存活）："已启动"事件由父进程置位
        SetEvent(hProcess);
        if (lpProcessInformation)
        {
            lpProcessInformation->hProcess = hProcess;
            lpProcessInformation->hThread = INVALID_HANDLE_VALUE;
            lpProcessInformation->dwProcessId = pid;
            lpProcessInformation->dwThreadId = 0;
        }
        return TRUE;
    }
}

BOOL WINAPI CreateProcessAsUserW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    swinx_stl::string strApp, strCmd, strDir;
    tostring(lpApplicationName, -1, strApp);
    tostring(lpCommandLine, -1, strCmd);
    tostring(lpCurrentDirectory, -1, strDir);
    swinx_stl::string strDesktop, strTitle;
    STARTUPINFOA startupINfoA;
    STARTUPINFOA *pStartInfoA = nullptr;
    if (lpStartupInfo)
    {
        pStartInfoA = &startupINfoA;
        if (lpStartupInfo->lpDesktop)
            tostring(lpStartupInfo->lpDesktop, -1, strDesktop);
        if (lpStartupInfo->lpTitle)
            tostring(lpStartupInfo->lpTitle, -1, strTitle);
        startupINfoA.cb = sizeof(startupINfoA);
        startupINfoA.lpReserved = NULL;
        startupINfoA.lpDesktop = lpStartupInfo->lpDesktop ? (char *)strDesktop.c_str() : nullptr;
        startupINfoA.lpTitle = lpStartupInfo->lpTitle ? (char *)strTitle.c_str() : nullptr;
        startupINfoA.dwX = lpStartupInfo->dwX;
        startupINfoA.dwY = lpStartupInfo->dwY;
        startupINfoA.dwXSize = lpStartupInfo->dwXSize;
        startupINfoA.dwYSize = lpStartupInfo->dwYSize;
        startupINfoA.dwXCountChars = lpStartupInfo->dwXCountChars;
        startupINfoA.dwYCountChars = lpStartupInfo->dwYCountChars;
        startupINfoA.dwFillAttribute = lpStartupInfo->dwFillAttribute;
        startupINfoA.dwFlags = lpStartupInfo->dwFlags;
        startupINfoA.wShowWindow = lpStartupInfo->wShowWindow;
        startupINfoA.cbReserved2 = 0;
        startupINfoA.lpReserved2 = nullptr;
        startupINfoA.hStdInput = lpStartupInfo->hStdInput;
        startupINfoA.hStdOutput = lpStartupInfo->hStdOutput;
        startupINfoA.hStdError = lpStartupInfo->hStdError;
    }
    return CreateProcessAsUserA(hToken, lpApplicationName ? strApp.c_str() : nullptr, lpCommandLine ? (char *)strCmd.c_str() : nullptr, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory ? strDir.c_str() : nullptr, pStartInfoA, lpProcessInformation);
}

BOOL WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    return CreateProcessAsUserA(0, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL WINAPI CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    return CreateProcessAsUserW(0, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
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
    // todo:hjx
    return MapVirtualKey(uCode, uMapType);
}

DWORD GetTickCount()
{
    return (DWORD)GetTickCount64();
}

uint64_t GetTickCount64()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

BOOL CallMsgFilter(LPMSG lpMsg, int nCode)
{
    if (CallHook(WH_SYSMSGFILTER, nCode, 0, (LPARAM)lpMsg))
        return TRUE;
    return CallHook(WH_MSGFILTER, nCode, 0, (LPARAM)lpMsg);
}

__time64_t _mktime64(const tm *ptime)
{
    return mktime((struct tm *)ptime);
}

__time64_t _time64(__time64_t *_Time)
{
#ifdef __x86_64
    return time((time_t *)_Time);
#else
    time_t tmp = _Time ? (*_Time) : 0;
    time_t ret = time(&tmp);
    if (_Time)
        *_Time = tmp;
    return ret;
#endif //__x86_64
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

BOOL DestroyCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    BOOL bRet = conn->DestroyCursor(hCursor);
    if (!bRet)
        return FALSE;
    return CursorMgr::destroyCursor(hCursor);
}

HCURSOR SetCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetCursor(0, hCursor);
}

HCURSOR GetCursor(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetCursor();
}

static __thread sigjmp_buf jump_buffer;

static void sigsegv_handler(int sig __attribute__((unused)))
{
    siglongjmp(jump_buffer, 1);
}

BOOL IsBadReadPtr(const void *ptr, size_t size)
{
    if (ptr == NULL || size == 0)
    {
        return 1; // Invalid pointer or size
    }
    // Set up signal handler
    struct sigaction new_action, old_action;
    new_action.sa_handler = sigsegv_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1)
    {
        perror("sigaction");
        return FALSE;
    }
    BOOL bRet = FALSE;
    if (sigsetjmp(jump_buffer, 1) == 0)
    {
        volatile const char *p = (const char *)ptr;
        char dummy __attribute__((unused));
        UINT_PTR count = size;
        size_t pageSize = sysconf(_SC_PAGESIZE);
        while (count > pageSize)
        {
            dummy = *p;
            p += pageSize;
            count -= pageSize;
        }
        dummy = p[0];
        dummy = p[count - 1];
    }
    else
    {
        bRet = TRUE;
    }
    if (sigaction(SIGSEGV, &old_action, NULL) == -1)
    {
        perror("sigaction");
    }
    return bRet; // Invalid pointer
}

BOOL IsBadWritePtr(const void *ptr, size_t size)
{
    if (ptr == NULL || size == 0)
    {
        return 1; // Invalid pointer or size
    }
    struct sigaction new_action, old_action;
    new_action.sa_handler = sigsegv_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1)
    {
        perror("sigaction");
        return FALSE;
    }
    BOOL bRet = FALSE;
    if (sigsetjmp(jump_buffer, 1) == 0)
    {

        volatile char *p = (char *)ptr;
        UINT_PTR count = size;
        size_t pageSize = sysconf(_SC_PAGESIZE);

        while (count > pageSize)
        {
            *p |= 0;
            p += pageSize;
            count -= pageSize;
        }
        p[0] |= 0;
        p[count - 1] |= 0;
    }
    else
    {
        bRet = TRUE;
    }
    if (sigaction(SIGSEGV, &old_action, NULL) == -1)
    {
        perror("sigaction");
    }
    return bRet; // Invalid pointer
}

BOOL WINAPI IsBadStringPtrA(LPCSTR lpsz, UINT_PTR ucchMax)
{
    if (!lpsz || ucchMax == 0)
        return TRUE;
    for (UINT_PTR i = 0; i < ucchMax; i++)
    {
        if (lpsz[i] == 0)
        {
            ucchMax = i + 1;
            break;
        }
    }
    return IsBadReadPtr(lpsz, ucchMax);
}

BOOL WINAPI IsBadStringPtrW(LPCWSTR lpsz, UINT_PTR ucchMax)
{
    if (!lpsz || ucchMax == 0)
        return TRUE;
    for (UINT_PTR i = 0; i < ucchMax; i++)
    {
        if (lpsz[i] == 0)
        {
            ucchMax = i + 1;
            break;
        }
    }
    return IsBadReadPtr(lpsz, ucchMax * sizeof(wchar_t));
}

DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
    if (hModule)
    {
        Dl_info info;
        int result = dladdr(hModule, &info);
        if (!result)
            return 0;
        ssize_t len = strlen(info.dli_fname);
        if (!lpFilename)
            return len;
        if (nSize < len)
            return 0;
        strncpy(lpFilename, info.dli_fname, len);
        if (len < nSize)
            lpFilename[len] = 0;
        return len;
    }
    else
    {
        char path[MAX_PATH] = { 0 };
        ssize_t len;
#ifdef __APPLE__
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) != 0)
        {
            perror("readlink");
            return 0;
        }
        len = strlen(path);
#else
        len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1)
        {
            perror("readlink");
            return 0;
        }
#endif

        if (lpFilename == 0)
            return len;
        if (nSize < len)
            return 0;
        strncpy(lpFilename, path, len);
        if (len < nSize)
            lpFilename[len] = 0;
        return len;
    }
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
    char szName[MAX_PATH];
    if (GetModuleFileNameA(hModule, szName, MAX_PATH) == 0)
        return 0;
    std::wstring str;
    towstring(szName, -1, str);
    if (str.length() >= nSize)
        return 0;
    wcscpy(lpFilename, str.c_str());
    return str.length();
}

void WINAPI OutputDebugStringA(LPCSTR lpOutputString)
{
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "output", "%s", lpOutputString);
#else
    printf("%s", lpOutputString);
    fflush(stdout);
#endif //__ANDROID__
}
void WINAPI OutputDebugStringW(LPCWSTR lpOutputString)
{
    wprintf(L"%s", lpOutputString);
}

void WINAPI set_error(int e)
{
    errno = e;
}

VOID WINAPI Sleep(DWORD dwMilliseconds)
{
    // Win32 语义：至少睡满 dwMilliseconds（允许因调度过冲而更久），永不提前返回；
    // 0 表示只让出剩余时间片，INFINITE 表示永不返回。
    //
    // 老写法（把毫秒数乘 1000 直接塞进 timeval::tv_usec，并忽略 select 的返回值）有两个缺陷：
    //   1) tv_usec 的合法范围是 [0, 999999]。请求 >= 1000ms 时会构造出越界的 timeval，
    //      POSIX 未定义：musl、macOS 会直接返回 -1/EINVAL（glibc 2.32 也是），于是
    //      Sleep(1000) 变成"立即返回"，一点都没睡。必须拆成 tv_sec + tv_usec。
    //   2) select() 会被信号打断（EINTR；SA_RESTART 对它无效，见 sysobjs.cpp 里 selectfds
    //      的同类注释。本进程装了 SIGCHLD 处理器，线程挂起/唤醒也用信号），老写法忽略
    //      返回值，Sleep 就被截断成更短的睡眠。
    // 这两点都会让"靠 Sleep 定序"的多线程代码（包括单元测试）随机失败。
    if (dwMilliseconds == 0)
    {
        sched_yield(); // Win32 的 Sleep(0) 让出剩余时间片
        return;
    }
    if (dwMilliseconds == INFINITE)
    {
        for (;;)
        {
            select(0, NULL, NULL, NULL, NULL); // 与 Win32 一致：永不返回
        }
    }

    const uint64_t deadline = GetTickCount64() + dwMilliseconds;
    for (;;)
    {
        const uint64_t now = GetTickCount64();
        if (now >= deadline)
            break;

        const uint64_t remain = deadline - now;
        struct timeval tv;
        tv.tv_sec = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000; // 恒在 [0, 999000]，不再越界

        const int ret = select(0, NULL, NULL, NULL, &tv);
        if (ret < 0 && errno != EINTR)
            break; // 其它错误（EBADF/EINVAL…）无法继续等待，直接返回以免死循环
    }
}

LONG CompareFileTime(const FILETIME *ft1, const FILETIME *ft2)
{
    if (ft1->dwHighDateTime < ft2->dwHighDateTime)
        return -1;
    if (ft1->dwHighDateTime > ft2->dwHighDateTime)
        return 1;
    if (ft1->dwLowDateTime < ft2->dwLowDateTime)
        return -1;
    if (ft1->dwLowDateTime > ft2->dwLowDateTime)
        return 1;
    return 0;
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT val __attribute__((unused)), void *ptr, UINT winini __attribute__((unused)))
{
    // todo:hjx
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

void WINAPI DebugBreak(void)
{
    DbgBreakPoint();
}

VOID WINAPI DbgBreakPoint(VOID)
{
    assert(0);
}

UINT WINAPI GetDoubleClickTime(VOID)
{
    return SConnMgr::instance()->getConnection()->GetDoubleClickTime();
}

#define TICKSPERSEC 10000000

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    lpFrequency->QuadPart = TICKSPERSEC;
    return TRUE;
}

#define SECS_1601_TO_1970 ((369 * 365 + 89) * (ULONGLONG)86400)

static inline ULONGLONG ticks_from_time_t(time_t time)
{
    if (sizeof(time_t) == sizeof(int)) /* time_t may be signed */
        return ((ULONGLONG)(ULONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
    else
        return ((ULONGLONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    struct timeval now;
    gettimeofday(&now, 0);
    lpPerformanceCount->QuadPart = ticks_from_time_t(now.tv_sec) + now.tv_usec * 10;
    return TRUE;
}

//---------------------------------------------------
UINT WINAPI RegisterClipboardFormatA(_In_ LPCSTR lpszFormat)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->RegisterClipboardFormatA(lpszFormat);
}

UINT WINAPI RegisterClipboardFormatW(_In_ LPCWSTR lpszFormat)
{
    swinx_stl::string str;
    tostring(lpszFormat, -1, str);
    return RegisterClipboardFormatA(str.c_str());
}

BOOL WINAPI EmptyClipboard(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->EmptyClipboard();
}

BOOL WINAPI IsClipboardFormatAvailable(_In_ UINT format)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->IsClipboardFormatAvailable(format);
}

BOOL WINAPI OpenClipboard(_In_opt_ HWND hWndNewOwner)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->OpenClipboard(hWndNewOwner);
}

BOOL WINAPI CloseClipboard(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->CloseClipboard();
}

HWND WINAPI GetClipboardOwner(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardOwner();
}

//---------------------------------------------------
// 系统信息相关 API

VOID WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo)
{
    if (!lpSystemInfo)
        return;

    // 初始化系统信息结构
    memset(lpSystemInfo, 0, sizeof(SYSTEM_INFO));

    // 设置处理器架构
#ifdef __x86_64__
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#elif defined(__i386__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#elif defined(__arm64__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#elif defined(__arm__)
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM;
#else
    lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
#endif

    // 设置页面大小
    lpSystemInfo->dwPageSize = sysconf(_SC_PAGESIZE);

    // 设置处理器数量
#ifdef __linux__
    lpSystemInfo->dwNumberOfProcessors = sysconf(_SC_NPROCESSORS_CONF);
#elif __APPLE__
    int count;
    size_t size = sizeof(count);
    sysctlbyname("hw.ncpu", &count, &size, NULL, 0);
    lpSystemInfo->dwNumberOfProcessors = count;
#else
    lpSystemInfo->dwNumberOfProcessors = 1;
#endif

    // 设置分配粒度
    lpSystemInfo->dwAllocationGranularity = 65536; // 默认值

    // 设置处理器级别
    lpSystemInfo->wProcessorLevel = 6;

    // 设置处理器版本
    lpSystemInfo->wProcessorRevision = 0;
}

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA lpVersionInfo)
{
    if (!lpVersionInfo)
        return FALSE;

    // 验证结构大小
    if (lpVersionInfo->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA))
        return FALSE;

    // 初始化版本信息
    memset(lpVersionInfo, 0, lpVersionInfo->dwOSVersionInfoSize);
    lpVersionInfo->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

    // 设置版本号（模拟 Windows 10）
    lpVersionInfo->dwMajorVersion = 10;
    lpVersionInfo->dwMinorVersion = 0;
    lpVersionInfo->dwBuildNumber = 19041;
    lpVersionInfo->dwPlatformId = VER_PLATFORM_WIN32_NT;

    // 设置版本字符串
    strcpy(lpVersionInfo->szCSDVersion, "");

    return TRUE;
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpVersionInfo)
{
    if (!lpVersionInfo)
        return FALSE;

    // 验证结构大小
    if (lpVersionInfo->dwOSVersionInfoSize < sizeof(OSVERSIONINFOW))
        return FALSE;

    // 初始化版本信息
    memset(lpVersionInfo, 0, lpVersionInfo->dwOSVersionInfoSize);
    lpVersionInfo->dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);

    // 设置版本号（模拟 Windows 10）
    lpVersionInfo->dwMajorVersion = 10;
    lpVersionInfo->dwMinorVersion = 0;
    lpVersionInfo->dwBuildNumber = 19041;
    lpVersionInfo->dwPlatformId = VER_PLATFORM_WIN32_NT;

    // 设置版本字符串
    wcscpy(lpVersionInfo->szCSDVersion, L"");

    return TRUE;
}

BOOL WINAPI GetComputerNameA(LPSTR lpBuffer, LPDWORD nSize)
{
    if (!lpBuffer || !nSize)
        return FALSE;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        return FALSE;

    size_t len = strlen(hostname);
    if (len >= *nSize)
    {
        // buffer too small: report the required size INCLUDING the NUL
        *nSize = len + 1;
        return FALSE;
    }

    strcpy(lpBuffer, hostname);
    // Win32: on success nSize is the length WITHOUT the terminating NUL
    *nSize = len;

    return TRUE;
}

BOOL WINAPI GetComputerNameW(LPWSTR lpBuffer, LPDWORD nSize)
{
    if (!lpBuffer || !nSize)
        return FALSE;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
        return FALSE;

    std::wstring wHostname;
    towstring(hostname, -1, wHostname);

    size_t len = wHostname.length();
    if (len >= *nSize)
    {
        *nSize = len + 1;
        return FALSE;
    }

    wcscpy(lpBuffer, wHostname.c_str());
    // Win32: on success nSize is the length WITHOUT the terminating NUL
    *nSize = len;

    return TRUE;
}

BOOL WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD nSize)
{
    if (!lpBuffer || !nSize)
        return FALSE;

    const char *username = getenv("USER");
    if (!username)
        username = getenv("LOGNAME");
    if (!username)
        return FALSE;

    size_t len = strlen(username);
    if (len >= *nSize)
    {
        *nSize = len + 1;
        return FALSE;
    }

    strcpy(lpBuffer, username);
    *nSize = len + 1;

    return TRUE;
}

BOOL WINAPI GetUserNameW(LPWSTR lpBuffer, LPDWORD nSize)
{
    if (!lpBuffer || !nSize)
        return FALSE;

    const char *username = getenv("USER");
    if (!username)
        username = getenv("LOGNAME");
    if (!username)
        return FALSE;

    std::wstring wUsername;
    towstring(username, -1, wUsername);

    size_t len = wUsername.length();
    if (len >= *nSize)
    {
        *nSize = len + 1;
        return FALSE;
    }

    wcscpy(lpBuffer, wUsername.c_str());
    *nSize = len + 1;

    return TRUE;
}

//---------------------------------------------------
// 虚拟内存相关 API

// Track VirtualAlloc regions so VirtualFree(MEM_RELEASE) can release the
// whole region (Win32 requires dwSize == 0 for MEM_RELEASE).
static std::mutex s_virtualMemMutex;
static swinx_stl::map<LPVOID, SIZE_T> s_virtualAllocs;

LPVOID WINAPI VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType __attribute__((unused)), DWORD flProtect)
{
    if (dwSize == 0)
        return NULL;

    int prot = PROT_NONE;
    if (flProtect & PAGE_EXECUTE)
        prot |= PROT_EXEC;
    if (flProtect & PAGE_READONLY)
        prot |= PROT_READ;
    if (flProtect & PAGE_READWRITE)
        prot |= PROT_WRITE | PROT_READ;

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;

    void *addr = mmap(lpAddress, dwSize, prot, flags, -1, 0);
    if (addr == MAP_FAILED)
        return NULL;

    {
        std::unique_lock<std::mutex> lock(s_virtualMemMutex);
        s_virtualAllocs[addr] = dwSize;
    }
    return addr;
}

BOOL WINAPI VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    if (!lpAddress)
        return FALSE;

    if (dwFreeType & MEM_RELEASE)
    {
        // Win32: MEM_RELEASE must not be combined with MEM_DECOMMIT
        // and requires dwSize == 0.
        if ((dwFreeType & MEM_DECOMMIT) || dwSize != 0)
            return FALSE;

        SIZE_T len = 0;
        {
            std::unique_lock<std::mutex> lock(s_virtualMemMutex);
            auto it = s_virtualAllocs.find(lpAddress);
            if (it != s_virtualAllocs.end())
            {
                len = it->second;
                s_virtualAllocs.erase(it);
            }
        }
        if (len == 0)
            return FALSE;
        return munmap(lpAddress, len) == 0;
    }

    if (dwFreeType & MEM_DECOMMIT)
    {
        // anonymous mmap memory has no separate commit state;
        // decommit is approximated as a no-op (pages stay accessible)
        return TRUE;
    }

    return FALSE;
}

HANDLE
WINAPI
GetClipboardData(_In_ UINT uFormat)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardData(uFormat);
}

HANDLE
WINAPI
SetClipboardData(_In_ UINT uFormat, _In_opt_ HANDLE hMem)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetClipboardData(uFormat, hMem);
}

//------------------------------------------------------------
// MessageBeep：播放系统提示音。
//   实现按平台分别放在 src/platform/<平台>/utils.*，构建时只有当前平台目录进入源文件
//   列表，由链接器解析同一个符号 swinx_messageBeep（契约见 SwinxUtils.h）
BOOL WINAPI MessageBeep(_In_ UINT uType)
{
    return swinx_messageBeep(uType);
}

#if defined(__IOS__)
// 由 platform/ios/utils.mm 实现，调用 NSTemporaryDirectory()
DWORD swinx_iOSTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
#endif

DWORD
WINAPI
GetTempPathA(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer)
{
#if defined(__IOS__)
    // iOS：使用 NSTemporaryDirectory() 获取应用沙盒临时目录
    return swinx_iOSTempPathA(nBufferLength, lpBuffer);
#elif defined(__ANDROID__) || defined(__OHOS__)
    // Android：优先调用平台层（Java getCacheDir）提供的临时目录
    if (g_platformAPI.path.getTempPathA)
        return g_platformAPI.path.getTempPathA(nBufferLength, lpBuffer);
    return 0;
#else
    // Linux/macOS 及其它 POSIX 平台：优先 TMPDIR，回退到 /tmp
    // Win32 语义：返回值不含结尾 '\0'，且路径保证以分隔符结尾
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    DWORD nLen = (DWORD)strlen(tmp);
    if (nBufferLength < nLen + 2) // path + 分隔符 + '\0'
        return 0;
    memcpy(lpBuffer, tmp, nLen);
    if (nLen == 0 || (lpBuffer[nLen - 1] != '/' && lpBuffer[nLen - 1] != '\\'))
        lpBuffer[nLen++] = '/';
    lpBuffer[nLen] = '\0';
    return nLen;
#endif
}

DWORD
WINAPI
GetTempPathW(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPWSTR lpBuffer)
{
    // 复用 GetTempPathA 的平台相关解析逻辑，避免重复实现
    char szTmp[MAX_PATH];
    DWORD nLen = GetTempPathA(MAX_PATH, szTmp);
    if (nLen == 0)
        return 0;
    std::wstring str;
    towstring(szTmp, -1, str);
    DWORD nWide = (DWORD)str.length() + 1; // 含结尾 '\0'
    if (nBufferLength < nWide)
        return 0;
    wcscpy(lpBuffer, str.c_str());
    return nWide;
}

UINT WINAPI GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName)
{
    swinx_stl::string strPath, strPrefix;
    char szTmpFileName[MAX_PATH] = { 0 };
    tostring(lpPathName, -1, strPath);
    tostring(lpPrefixString, -1, strPrefix);
    int ret = GetTempFileNameA(strPath.c_str(), strPrefix.c_str(), uUnique, szTmpFileName);
    if (!ret)
        return 0;
    MultiByteToWideChar(CP_UTF8, 0, szTmpFileName, -1, lpTempFileName, MAX_PATH);
    return ret;
}

UINT WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT unique, LPSTR buffer)
{
    DWORD attr = GetFileAttributesA(lpPathName);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        SetLastError(ERROR_DIRECTORY);
        return 0;
    }

    strcpy(buffer, lpPathName);
    char *p = buffer + strlen(buffer);

    /* add a \, if there isn't one  */
    if ((p == buffer) || (p[-1] != '/'))
        *p++ = '/';

    if (lpPrefixString)
        for (int i = 3; (i > 0) && (*lpPrefixString); i--)
            *p++ = *lpPrefixString++;

    unique &= 0xffff;
    if (unique)
        sprintf_s(p, MAX_PATH - (p - buffer), "%x.tmp", unique);
    else
    {
        /* get a "random" unique number and try to create the file */
        HANDLE handle;
        UINT num = GetTickCount() & 0xffff;
        static UINT last;

        /* avoid using the same name twice in a short interval */
        if (last - num < 10)
            num = last + 1;
        if (!num)
            num = 1;
        unique = num;
        do
        {
            sprintf_s(p, MAX_PATH - (p - buffer), "%x.tmp", unique);
            handle = CreateFileA(buffer, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
            if (handle != INVALID_HANDLE_VALUE)
            { /* We created it */
                CloseHandle(handle);
                last = unique;
                break;
            }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_SHARING_VIOLATION)
                break; /* No need to go on */
            if (!(++unique & 0xffff))
                unique = 1;
        } while (unique != num);
    }
    return unique;
}

BOOL IsValidCodePage(UINT CodePage __attribute__((unused)))
{
    // todo:hjx
    return TRUE;
}

UINT WINAPI GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    return SConnMgr::instance()->getConnection()->GetKeyboardLayoutList(nBuff, lpList);
}

HKL ActivateKeyboardLayout(HKL hkl, UINT Flags __attribute__((unused)))
{
    return SConnMgr::instance()->getConnection()->ActivateKeyboardLayout(hkl);
}

UINT WINAPI GetACP(void)
{
    return CP_UTF8;
}

BOOL WINAPI IsDBCSLeadByte(BYTE c)
{
    return UTF8CharLength(c) > 1;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName)
{
    if (lpModuleName)
    {
        void *hMod = dlopen(lpModuleName, RTLD_LAZY);
        if (hMod)
            dlclose(hMod);
        return (HMODULE)hMod;
    }
    else
    {
#ifdef __APPLE__
        uint32_t count = _dyld_image_count();
        assert(count >= 1);
        const struct mach_header *header = _dyld_get_image_header(0);
        if (!header)
        {
            return 0;
        }
        return (HMODULE)header;
#else
        char pathexe[MAX_PATH];
        GetModuleFileNameA(NULL, pathexe, MAX_PATH);
        FILE *fp;
        char line[1024];
        void *module_addr = NULL;

        // 打开 /proc/self/maps 文件
        fp = fopen("/proc/self/maps", "r");
        if (fp == NULL)
        {
            perror("fopen");
            return 0;
        }
        // 读取文件内容，查找包含当前进程可执行文件路径的行
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, pathexe) != NULL)
            {
#if __WORDSIZE == 64
                sscanf(line, "%lx", (UINT_PTR *)&module_addr); // 从行中读取模块地址
#else
                sscanf(line, "%x", (UINT_PTR *)&module_addr);
#endif
                break;
            }
        }
        fclose(fp);
        return (HMODULE)module_addr;
#endif //__APPLE__
    }
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName)
{
    if (!lpModuleName)
        return GetModuleHandleA(NULL);
    swinx_stl::string str;
    tostring(lpModuleName, -1, str);
    return GetModuleHandleA(str.c_str());
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
    // setenv returns 0 on success, Win32 wants TRUE
    return setenv(lpName, lpValue, 1) == 0;
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue)
{
    swinx_stl::string name, value;
    tostring(lpName, -1, name);
    tostring(lpValue, -1, value);
    return SetEnvironmentVariableA(name.c_str(), value.c_str());
}

DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
    const char *value = getenv(lpName);
    if (!value)
    {
        SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    size_t len = strlen(value);
    if (len >= nSize)
        return len + 1;
    strcpy(lpBuffer, value);
    return len;
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize)
{
    swinx_stl::string name;
    tostring(lpName, -1, name);
    const char *value = getenv(name.c_str());
    if (!value)
    {
        SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    std::wstring wstr;
    towstring(value, -1, wstr);
    size_t len = wstr.length();
    if (len >= nSize)
        return len + 1;
    wcscpy(lpBuffer, wstr.c_str());
    return len;
}

//-----------------------------------------------------------
struct FdHandle : _SynHandle
{
    int fd;
    FdHandle(int _fd)
        : fd(_fd)
    {
        type = HFdHandle;
    }

    ~FdHandle()
    {
    }

    int getReadFd() override
    {
        return fd;
    }

    int getWriteFd() override
    {
        return fd;
    }

    bool init(LPCSTR pszName __attribute__((unused)), void *initData __attribute__((unused))) override
    {
        return false;
    }
    void lock() override
    {
    }
    void unlock() override
    {
    }
    void *getData() override
    {
        return nullptr;
    }
    LPCSTR getName() const override
    {
        return nullptr;
    }
};

class SOsHandleMgr {
    enum
    {
        kSpan_CheckFd = 1000, // time interval for check fd
    };

  public:
    SOsHandleMgr()
        : m_ts(0)
    {
    }

    ~SOsHandleMgr()
    {
        cleanup();
    }

    bool isValidFd(int fd)
    {
        int new_fd = dup(fd);
        if (new_fd == -1)
        {
            return false;
        }
        else
        {
            close(new_fd);
            return true;
        }
    }

    HANDLE fd2Handle(int fd)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        uint64_t now = GetTickCount64();
        if (m_ts != 0 && now - m_ts > kSpan_CheckFd)
        {
            m_ts = now;
            // time for check fd validation.
            auto it = m_fdMap.begin();
            while (it != m_fdMap.end())
            {
                auto it_bk = it++;
                if (!isValidFd(it_bk->first))
                {
                    CloseHandle(it_bk->second);
                    m_fdMap.erase(it_bk);
                }
            }
        }
        auto it = m_fdMap.find(fd);
        if (it != m_fdMap.end())
            return it->second;
        HANDLE hRet = NewSynHandle(new FdHandle(fd));
        m_fdMap.insert(swinx_stl::make_pair(fd, hRet));
        return hRet;
    }

    void cleanup()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_fdMap.begin();
        while (it != m_fdMap.end())
        {
            CloseHandle(it->second);
            it++;
        }
        m_fdMap.clear();
    }

  private:
    swinx_stl::map<int, HANDLE> m_fdMap;
    std::mutex m_mutex;
    uint64_t m_ts;
};

static SOsHandleMgr s_osHandleMgr;

HANDLE WINAPI _get_osfhandle(int fd)
{
    return s_osHandleMgr.fd2Handle(fd);
}

LPSTR WINAPI GetCommandLineA(void)
{
    static char cmdline[1024] = { 0 };
    if (cmdline[0] != 0)
        return cmdline;
#ifdef __APPLE__
    // 注意：googletest 解析参数时会从 argv 数组中左移剔除 gtest 开关，但只
    // 递减 main 收到的 argc 副本；macOS 上 *_NSGetArgc() 指向的 NXArgc 全局
    // 变量不会同步更新，导致它大于 argv 数组中实际有效的元素个数、数组尾部
    // 出现 NULL（googletest issue #1346）。因此不能只信 argc，需同时校验
    // NULL 终止符，否则传过 gtest 参数后调用本函数会 strlen(NULL) 崩溃。
    int argc = *_NSGetArgc();
    char ***argv = _NSGetArgv();
    if (!argv || !*argv)
        return cmdline;
    char *p = cmdline;
    for (int i = 0; i < argc && (*argv)[i] != nullptr; i++)
    {
        size_t arg_len = strlen((*argv)[i]);

        if (strchr((*argv)[i], ' '))
        {
            if (p + arg_len + 2 - cmdline >= sizeof(cmdline) - 1)
                break;
            *p++ = '\"';
            strcpy(p, (*argv)[i]);
            p += arg_len;
            *p++ = '\"';
        }
        else
        {
            if (p + arg_len - cmdline >= sizeof(cmdline) - 1)
                break;
            strcpy(p, (*argv)[i]);
            p += arg_len;
        }
        if (i < argc - 1)
        {
            if (p - cmdline < sizeof(cmdline) - 1)
            {
                *p++ = ' ';
            }
        }
    }
    return cmdline;
#else
    FILE *file = fopen("/proc/self/cmdline", "r");
    if (!file)
    {
        perror("Failed to open cmdline file");
        return cmdline;
    }

    // Read the entire cmdline file which contains null-separated arguments
    size_t bytes_read = fread(cmdline, 1, sizeof(cmdline) - 1, file);
    fclose(file);

    if (bytes_read > 0)
    {
        // Null-terminate the string
        cmdline[bytes_read] = '\0';

        // Replace null separators with spaces
        for (size_t i = 0; i < bytes_read; i++)
        {
            if (cmdline[i] == '\0')
                cmdline[i] = ' ';
        }
    }
    else
    {
        cmdline[0] = '\0';
    }

    return cmdline;
#endif
}

LPWSTR WINAPI GetCommandLineW(void)
{
    static wchar_t cmdline[1024] = { 0 };
    if (cmdline[0] != 0)
        return cmdline;
    char *tmp = GetCommandLineA();
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, cmdline, 1024);
    return cmdline;
}

//------------------------------------------------------------------------------
HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    if (!lpPathName)
        return INVALID_HANDLE_VALUE;
    swinx_stl::string str;
    tostring(lpPathName, -1, str);
    return FindFirstChangeNotificationA(str.c_str(), bWatchSubtree, dwNotifyFilter);
}

#ifdef __linux__
struct NotifyHandle : FdHandle
{
    swinx_stl::map<int, swinx_stl::string> mapWdPath; // watch descriptor -> path mapping
    swinx_stl::map<swinx_stl::string, int> mapPathWd; // path -> watch descriptor mapping
    BOOL bWatchSubtree;                   // 是否监控子目录
    uint32_t mask;                        // inotify mask

    NotifyHandle(int _fd)
        : FdHandle(_fd)
        , bWatchSubtree(FALSE)
        , mask(0)
    {
        type = HNotifyHandle;
    }

    ~NotifyHandle()
    {
        for (auto it : mapWdPath)
        {
            inotify_rm_watch(fd, it.first);
        }
        mapWdPath.clear();
        mapPathWd.clear();
        close(fd);
    }

    // 添加单个路径监控
    bool addWatch(const char *path)
    {
        // 检查是否已经在监控
        if (mapPathWd.find(path) != mapPathWd.end())
            return true;

        int wd = inotify_add_watch(fd, path, mask);
        if (wd == -1)
            return false;

        mapWdPath[wd] = path;
        mapPathWd[path] = wd;
        return true;
    }

    // 移除单个路径监控
    void removeWatch(int wd)
    {
        auto it = mapWdPath.find(wd);
        if (it != mapWdPath.end())
        {
            mapPathWd.erase(it->second);
            mapWdPath.erase(it);
            inotify_rm_watch(fd, wd);
        }
    }

    // 移除路径监控(通过路径)
    void removeWatchByPath(const swinx_stl::string &path)
    {
        auto it = mapPathWd.find(path);
        if (it != mapPathWd.end())
        {
            int wd = it->second;
            mapPathWd.erase(it);
            mapWdPath.erase(wd);
            inotify_rm_watch(fd, wd);
        }
    }
};

// 递归添加目录及其子目录的监控
static void add_path_watch_recursive(NotifyHandle *pHandle, const char *path)
{
    if (!pHandle->addWatch(path))
        return;

    if (!pHandle->bWatchSubtree)
        return;

    // 监视子目录
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 忽略 "." 和 ".."
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (entry->d_type == DT_DIR)
            {
                // 递归监视子目录
                add_path_watch_recursive(pHandle, full_path);
            }
        }
    }

    closedir(dir);
}

// 递归移除目录及其子目录的监控
static void remove_path_watch_recursive(NotifyHandle *pHandle, const swinx_stl::string &path)
{
    // 先移除所有子目录
    swinx_stl::vector<swinx_stl::string> toRemove;
    for (auto &it : pHandle->mapPathWd)
    {
        if (it.first.find(path) == 0) // 路径以path开头
        {
            toRemove.push_back(it.first);
        }
    }

    for (auto &p : toRemove)
    {
        pHandle->removeWatchByPath(p);
    }
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    uint32_t mask = 0;
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_FILE_NAME)
    {
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_DIR_NAME)
    {
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_SIZE)
    {
        mask |= IN_MODIFY;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
    {
        mask |= IN_MODIFY;
    }

    if (!lpPathName || mask == 0)
        return INVALID_HANDLE_VALUE;
    int fd = inotify_init();
    if (fd == -1)
    {
        return INVALID_HANDLE_VALUE;
    }
    NotifyHandle *notifyHandle = new NotifyHandle(fd);
    notifyHandle->bWatchSubtree = bWatchSubtree;
    notifyHandle->mask = mask;

    add_path_watch_recursive(notifyHandle, lpPathName);
    if (notifyHandle->mapWdPath.empty())
    {
        delete notifyHandle;
        return INVALID_HANDLE_VALUE;
    }
    return NewSynHandle(notifyHandle);
}

BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle)
{
    _SynHandle *synHandle = GetSynHandle(hChangeHandle);
    if (!synHandle || synHandle->getType() != HNotifyHandle)
        return FALSE;

    int tst = WaitForSingleObject(hChangeHandle, 0);
    if (tst != WAIT_OBJECT_0)
        return FALSE;

    NotifyHandle *notifyHandle = (NotifyHandle *)synHandle;

    // 读取并处理inotify事件
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t totalRead = 0;

    while (true)
    {
        ssize_t bytesRead = read(notifyHandle->fd, buf, sizeof(buf));
        if (bytesRead <= 0)
            break;

        totalRead += bytesRead;

        // 解析inotify事件
        const struct inotify_event *event;
        for (char *ptr = buf; ptr < buf + bytesRead; ptr += sizeof(struct inotify_event) + event->len)
        {
            event = (const struct inotify_event *)ptr;

            // 如果不监控子目录,不需要处理目录创建/删除
            if (!notifyHandle->bWatchSubtree)
                continue;

            // 获取事件对应的路径
            auto it = notifyHandle->mapWdPath.find(event->wd);
            if (it == notifyHandle->mapWdPath.end())
                continue;

            swinx_stl::string parentPath = it->second;

            // 只处理有名称的事件
            if (event->len == 0)
                continue;

            swinx_stl::string fullPath = parentPath + "/" + event->name;

            // 处理目录创建事件
            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR))
            {
                // 新建了子目录,添加监控
                add_path_watch_recursive(notifyHandle, fullPath.c_str());
            }
            // 处理目录删除事件
            else if ((event->mask & IN_DELETE) && (event->mask & IN_ISDIR))
            {
                // 删除了子目录,移除监控
                remove_path_watch_recursive(notifyHandle, fullPath);
            }
            // 处理目录移入事件
            else if ((event->mask & IN_MOVED_TO) && (event->mask & IN_ISDIR))
            {
                // 目录移入,添加监控
                add_path_watch_recursive(notifyHandle, fullPath.c_str());
            }
            // 处理目录移出事件
            else if ((event->mask & IN_MOVED_FROM) && (event->mask & IN_ISDIR))
            {
                // 目录移出,移除监控
                remove_path_watch_recursive(notifyHandle, fullPath);
            }
            // 处理监控被删除事件
            else if (event->mask & IN_IGNORED)
            {
                // 监控的目录被删除了,从映射中移除
                notifyHandle->removeWatch(event->wd);
            }
        }

        // 如果读取的数据小于缓冲区大小,说明已经读完了
        if (bytesRead < (ssize_t)sizeof(buf))
            break;
    }

    return totalRead > 0;
}

#elif defined(__APPLE__) && defined(__MACH__)
struct NotifyHandle : FdHandle
{
    swinx_stl::map<int, swinx_stl::string> mapFdPath; // fd -> path mapping
    swinx_stl::map<swinx_stl::string, int> mapPathFd; // path -> fd mapping
    swinx_stl::string rootPath;                 // 根路径
    BOOL watchSubtree;                    // 是否监控子目录
    DWORD notifyFilter;                   // 通知过滤器

    NotifyHandle(int kq)
        : FdHandle(kq)
        , watchSubtree(FALSE)
        , notifyFilter(0)
    {
        type = HNotifyHandle;
    }

    ~NotifyHandle()
    {
        // 关闭所有监控的文件描述符
        for (auto &it : mapFdPath)
        {
            close(it.first);
        }
        mapFdPath.clear();
        mapPathFd.clear();

        // 关闭kqueue
        if (fd != -1)
        {
            close(fd);
        }
    }

    // 移除路径监控
    void removeWatchByPath(const swinx_stl::string &path)
    {
        auto it = mapPathFd.find(path);
        if (it != mapPathFd.end())
        {
            int watchFd = it->second;
            mapPathFd.erase(it);
            mapFdPath.erase(watchFd);
            close(watchFd);
        }
    }
};

// 添加单个路径到kqueue监控
static bool add_path_to_kqueue(NotifyHandle *handle, const char *path)
{
    // 检查是否已经在监控
    if (handle->mapPathFd.find(path) != handle->mapPathFd.end())
        return true;

    int pathFd = open(path, O_RDONLY);
    if (pathFd == -1)
    {
        return false;
    }

    struct kevent kev;
    uint32_t fflags = 0;

    // 根据通知过滤器设置kqueue事件标志
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_FILE_NAME)
    {
        fflags |= NOTE_DELETE | NOTE_RENAME | NOTE_WRITE; // NOTE_WRITE用于检测新文件
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_DIR_NAME)
    {
        fflags |= NOTE_DELETE | NOTE_RENAME | NOTE_WRITE; // NOTE_WRITE用于检测新目录
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
    {
        fflags |= NOTE_ATTRIB;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_SIZE)
    {
        fflags |= NOTE_EXTEND | NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_LAST_WRITE)
    {
        fflags |= NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
    {
        fflags |= NOTE_WRITE; // macOS kqueue没有直接的访问时间监控
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_CREATION)
    {
        fflags |= NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_SECURITY)
    {
        fflags |= NOTE_ATTRIB;
    }

    // 如果没有指定任何过滤器，监控所有变化
    if (fflags == 0)
    {
        fflags = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME;
    }

    EV_SET(&kev, pathFd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, (void *)(LONG_PTR)pathFd);

    if (kevent(handle->fd, &kev, 1, NULL, 0, NULL) == -1)
    {
        close(pathFd);
        return false;
    }

    handle->mapFdPath[pathFd] = path;
    handle->mapPathFd[path] = pathFd;
    return true;
}

// 递归添加目录及其子目录到监控
static void add_directory_watch(NotifyHandle *handle, const char *dirPath)
{
    // 添加当前目录
    if (!add_path_to_kqueue(handle, dirPath))
    {
        return;
    }

    // 如果不需要监控子目录，直接返回
    if (!handle->watchSubtree)
    {
        return;
    }

    // 遍历子目录
    DIR *dir = opendir(dirPath);
    if (!dir)
    {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 跳过 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char fullPath[PATH_MAX];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

        struct stat statBuf;
        if (stat(fullPath, &statBuf) == 0 && S_ISDIR(statBuf.st_mode))
        {
            // 递归添加子目录
            add_directory_watch(handle, fullPath);
        }
    }

    closedir(dir);
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    if (!lpPathName)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    // 创建kqueue
    int kq = kqueue();
    if (kq == -1)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }

    // 创建通知句柄
    NotifyHandle *notifyHandle = new NotifyHandle(kq);
    notifyHandle->rootPath = lpPathName;
    notifyHandle->watchSubtree = bWatchSubtree;
    notifyHandle->notifyFilter = dwNotifyFilter;

    // 检查路径是否存在
    struct stat statBuf;
    if (stat(lpPathName, &statBuf) != 0)
    {
        delete notifyHandle;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    // 根据路径类型添加监控
    if (S_ISDIR(statBuf.st_mode))
    {
        // 目录
        add_directory_watch(notifyHandle, lpPathName);
    }
    else
    {
        // 文件
        if (!add_path_to_kqueue(notifyHandle, lpPathName))
        {
            delete notifyHandle;
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // 检查是否成功添加了任何监控
    if (notifyHandle->mapFdPath.empty())
    {
        delete notifyHandle;
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }

    return NewSynHandle(notifyHandle);
}

BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle)
{
    _SynHandle *synHandle = GetSynHandle(hChangeHandle);
    if (!synHandle || synHandle->getType() != HNotifyHandle)
        return FALSE;

    NotifyHandle *notifyHandle = (NotifyHandle *)synHandle;

    // 读取并处理kqueue事件
    struct kevent events[10];
    struct timespec timeout = { 0, 0 }; // 非阻塞读取

    int nEvents = kevent(notifyHandle->fd, NULL, 0, events, 10, &timeout);
    if (nEvents <= 0)
    {
        return FALSE; // 没有事件或出错
    }

    // 处理所有事件，清空事件队列
    for (int i = 0; i < nEvents; i++)
    {
        struct kevent &event = events[i];

        // 查找事件对应的路径
        auto it = notifyHandle->mapFdPath.find((int)event.ident);
        if (it == notifyHandle->mapFdPath.end())
            continue;

        swinx_stl::string watchedPath = it->second;

        // 如果监控子目录且检测到目录内容变化,需要检查是否有新增或删除的子目录
        if (notifyHandle->watchSubtree && (event.fflags & NOTE_WRITE))
        {
            // 检查目录是否存在
            struct stat statBuf;
            if (stat(watchedPath.c_str(), &statBuf) != 0 || !S_ISDIR(statBuf.st_mode))
                continue;

            // 扫描目录,查找新增的子目录
            DIR *dir = opendir(watchedPath.c_str());
            if (dir)
            {
                swinx_stl::set<swinx_stl::string> currentSubDirs;
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL)
                {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                        continue;

                    char fullPath[PATH_MAX];
                    snprintf(fullPath, sizeof(fullPath), "%s/%s", watchedPath.c_str(), entry->d_name);

                    if (stat(fullPath, &statBuf) == 0 && S_ISDIR(statBuf.st_mode))
                    {
                        currentSubDirs.insert(fullPath);

                        // 如果是新目录且未监控,添加监控
                        if (notifyHandle->mapPathFd.find(fullPath) == notifyHandle->mapPathFd.end())
                        {
                            add_directory_watch(notifyHandle, fullPath);
                        }
                    }
                }
                closedir(dir);
            }
        }

        // 处理目录删除或重命名事件
        if (event.fflags & (NOTE_DELETE | NOTE_RENAME))
        {
            // 目录被删除或重命名,移除监控
            if (notifyHandle->watchSubtree)
            {
                // 移除所有以此路径开头的监控
                swinx_stl::vector<swinx_stl::string> toRemove;
                for (auto &pathIt : notifyHandle->mapPathFd)
                {
                    if (pathIt.first.find(watchedPath) == 0)
                    {
                        toRemove.push_back(pathIt.first);
                    }
                }
                for (auto &path : toRemove)
                {
                    notifyHandle->removeWatchByPath(path);
                }
            }
        }
    }

    return TRUE; // 成功处理了事件
}

#else
#pragma message("unsupport os")
#endif
BOOL WINAPI FindCloseChangeNotification(HANDLE hChangeHandle)
{
    return CloseHandle(hChangeHandle);
}

struct ThreadInfo
{
    ThreadInfo()
    {
        hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
        hEventResume = CreateEventA(NULL, FALSE, FALSE, NULL);
    }
    ~ThreadInfo()
    {
        CloseHandle(hEvent);
        CloseHandle(hEventResume);
    }
    HANDLE hEvent;
    HANDLE hEventResume;
};

struct ThreadObj
    : PipeSynHandle
    , ThreadInfo
{
    pthread_t thread;
    std::atomic<DWORD> nSuspend;
    std::atomic<bool> bExited; // user routine has returned
    bool bCreateSuspended;
    ThreadObj()
        : thread(0)
        , nSuspend(0)
        , bExited(false)
        , bCreateSuspended(false)
    {
        type = HThread;
        init(NULL, NULL);
    }
    ~ThreadObj()
    {
        if (thread != 0)
        {
            pthread_join(thread, nullptr);
        }
    }
    void *getData() override
    {
        return (ThreadInfo *)this;
    }
};

struct ThreadParam
{
    ThreadParam(LPTHREAD_START_ROUTINE _lpStartAddress, LPVOID _lpParameter, DWORD _dwCreationFlags, tid_t *_lpThreadId)
        : lpStartAddress(_lpStartAddress)
        , lpParameter(_lpParameter)
        , dwCreationFlags(_dwCreationFlags)
        , lpThreadId(_lpThreadId)
    {
    }
    LPTHREAD_START_ROUTINE lpStartAddress;
    LPVOID lpParameter;
    DWORD dwCreationFlags;
    tid_t *lpThreadId;
    ThreadObj *info;
};

static __thread UINT_PTR tls_exitCode = 0;

#ifdef __linux__
// ---------------------------------------------------------------------------
// Per-thread suspension without SIGSTOP/SIGCONT.
//
// SIGSTOP is PROCESS-wide on Linux (group-stop): pthread_kill(tid, SIGSTOP)
// freezes every thread in the process, not just the target. It also stops the
// process in a way debuggers catch and present as a "crash", and it is nothing
// like Win32 SuspendThread. Instead we park the target thread inside a signal
// handler using two queued realtime signals (RT signals are never coalesced,
// which keeps the park/wake race free):
//
//   SIGPARK: handler loops in sigsuspend() while the thread's suspend count
//            is > 0. The thread is parked inside the handler (any syscall it
//            was in is interrupted and restarted on return).
//   SIGWAKE: empty handler; its only job is to interrupt the sigsuspend()
//            above so the loop can re-check the (now decremented) count.
//
// Only swinx-created threads register themselves in tls_parkObj; the handler
// returns immediately on any other thread, so stray signals can never park
// foreign threads (main thread, timer scheduler, host app threads).
// ---------------------------------------------------------------------------
static __thread ThreadObj *tls_parkObj = nullptr;

static int Swinx_SigPark()
{
    static int s_sig = SIGRTMIN + 4; // glibc reserves SIGRTMIN..+2 internally
    return s_sig;
}
static int Swinx_SigWake()
{
    static int s_sig = SIGRTMIN + 5;
    return s_sig;
}

static void Swinx_ThreadWakeHandler(int)
{
    // empty on purpose: interrupting the sigsuspend() in the park handler
    // is the whole effect
}

static void Swinx_ThreadParkHandler(int, siginfo_t *, void *)
{
    ThreadObj *self = tls_parkObj;
    if (!self)
        return; // not a swinx thread: nothing to park
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, Swinx_SigPark());
    sigdelset(&mask, Swinx_SigWake());
    while (self->nSuspend.load(std::memory_order_acquire) > 0)
        sigsuspend(&mask);
}

static void Swinx_InstallThreadParkSignals()
{
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        struct sigaction saPark;
        memset(&saPark, 0, sizeof(saPark));
        saPark.sa_sigaction = Swinx_ThreadParkHandler;
        saPark.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&saPark.sa_mask);
        sigaction(Swinx_SigPark(), &saPark, NULL);

        struct sigaction saWake;
        memset(&saWake, 0, sizeof(saWake));
        saWake.sa_handler = Swinx_ThreadWakeHandler;
        saWake.sa_flags = SA_RESTART;
        sigemptyset(&saWake.sa_mask);
        sigaction(Swinx_SigWake(), &saWake, NULL);
    });
}
#endif // __linux__

static void *Swinx_ThreadProc(void *p)
{
    ThreadParam *param = (ThreadParam *)p;
#ifdef __linux__
    tls_parkObj = param->info; // allow SuspendThread to park this thread
#endif
    if (param->lpThreadId)
        *param->lpThreadId = GetCurrentThreadId();
    SetEvent(param->info->hEvent);
    if (param->dwCreationFlags & CREATE_SUSPENDED)
    {
        WaitForSingleObject(param->info->hEventResume, INFINITE);
    }
    tls_exitCode = param->lpStartAddress(param->lpParameter);
    param->info->bExited.store(true, std::memory_order_release);
    param->info->writeSignal(); // wakeup waitings for the thread object.
    delete param;
#ifdef __linux__
    tls_parkObj = nullptr;
#endif
    return &tls_exitCode;
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes __attribute__((unused)), SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, tid_t *lpThreadId)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (dwStackSize != 0)
    {
        pthread_attr_setstacksize(&attr, dwStackSize);
    }

    ThreadParam *param = new ThreadParam(lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
    ThreadObj *trdObj = new ThreadObj();
    trdObj->bCreateSuspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
    param->info = trdObj;
    if (pthread_create(&trdObj->thread, &attr, Swinx_ThreadProc, param) != 0)
    {
        SLOG_STME() << "Failed to create thread";
        delete param;
        delete trdObj;
        return INVALID_HANDLE_VALUE;
    }
    WaitForSingleObject(trdObj->hEvent, INFINITE);
    return NewSynHandle(trdObj);
}

// Resume thread: first call resumes a thread created with CREATE_SUSPENDED
// (via the hEventResume bootstrap), later calls decrement the suspend count.
// Real suspension parks the target thread inside the SIGPARK signal handler
// (see Swinx_ThreadParkHandler); ResumeThread decrements the count and sends
// SIGWAKE to interrupt the park loop. SIGSTOP/SIGCONT are NOT used: SIGSTOP
// is process-wide on Linux (group-stop of all threads) and is caught by
// debuggers, which looks like a crash. On Apple no real per-thread suspension
// exists here, so the count is tracked but the thread keeps running.
DWORD WINAPI ResumeThread(HANDLE hThread)
{
    if (hThread == INVALID_HANDLE_VALUE)
        return (DWORD)-1;
    _SynHandle *synHandle = GetSynHandle(hThread);
    if (!synHandle || synHandle->getType() != HThread)
        return (DWORD)-1;
    ThreadObj *threadObj = (ThreadObj *)synHandle;
    if (threadObj->bCreateSuspended)
    {
        threadObj->bCreateSuspended = false;
        SetEvent(threadObj->hEventResume);
        return 0;
    }
    DWORD prev = threadObj->nSuspend.load();
    // decrement the suspend count (platform-independent bookkeeping); only
    // the wake signal delivery is Linux-specific (see comment above).
    while (prev > 0 && !threadObj->nSuspend.compare_exchange_weak(prev, prev - 1))
        ;
#ifdef __linux__
    if (prev == 1)
    {
        Swinx_InstallThreadParkSignals();
        // queued RT signal: safe even if the thread has not entered the park
        // loop yet -- the park handler re-checks the count (now 0) and skips
        // parking; if it is parked, sigsuspend() returns and the loop exits.
        pthread_kill(threadObj->thread, Swinx_SigWake());
    }
#endif
    // Win32 returns the previous suspend count (0 when the thread was running)
    return prev;
}

DWORD WINAPI SuspendThread(HANDLE hThread)
{
    if (hThread == INVALID_HANDLE_VALUE)
        return (DWORD)-1;
    _SynHandle *synHandle = GetSynHandle(hThread);
    if (!synHandle || synHandle->getType() != HThread)
        return (DWORD)-1;
    ThreadObj *threadObj = (ThreadObj *)synHandle;
    DWORD prev = threadObj->nSuspend.fetch_add(1);
#ifdef __linux__
    if (prev == 0 && !threadObj->bExited.load(std::memory_order_acquire))
    {
        Swinx_InstallThreadParkSignals();
        pthread_kill(threadObj->thread, Swinx_SigPark());
    }
#endif
    return prev;
}

BOOL WINAPI TerminateThread(HANDLE hThread __attribute__((unused)), DWORD dwExitCode __attribute__((unused)))
{
    // not support
    return 0;
}

VOID WINAPI ExitThread(DWORD dwExitCode)
{
    tls_exitCode = dwExitCode;
}

BOOL WINAPI GetModuleHandleExA(_In_ DWORD dwFlags, _In_opt_ LPCSTR lpModuleName, _Out_ HMODULE *phModule)
{
    if (!phModule)
        return FALSE;

    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
    {
        const void *addr = lpModuleName;
        Dl_info info;
        if (dladdr(addr, &info) != 0 && info.dli_fbase != NULL)
        {
            *phModule = (HMODULE)info.dli_fbase;
            return TRUE;
        }
        return FALSE;
    }

    HMODULE hMod = GetModuleHandleA(lpModuleName);
    if (hMod)
    {
        *phModule = hMod;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI GetModuleHandleExW(_In_ DWORD dwFlags, _In_opt_ LPCWSTR lpModuleName, _Out_ HMODULE *phModule)
{
    if (!phModule)
        return FALSE;

    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
    {
        return GetModuleHandleExA(dwFlags, (LPCSTR)lpModuleName, phModule);
    }

    if (!lpModuleName)
        return GetModuleHandleExA(dwFlags, NULL, phModule);

    swinx_stl::string str;
    tostring(lpModuleName, -1, str);
    return GetModuleHandleExA(dwFlags, str.c_str(), phModule);
}

//========================================================================
// Thread Local Storage (TLS)
//
// Win32 semantics: TlsAlloc hands out indices in [0, TLS_MINIMUM_AVAILABLE)
// process-wide; per-thread slot values default to NULL and are kept in
// thread_local storage. TlsFree marks the index reusable (values in other
// threads are NOT cleared, matching Win32; a reusing owner must set the
// value before reading it).
//========================================================================

static std::mutex s_tlsMutex;
static uint64_t s_tlsUsedMask = 0; // bit i set => slot i allocated

static thread_local void *s_tlsSlots[TLS_MINIMUM_AVAILABLE] = {};

DWORD WINAPI TlsAlloc(VOID)
{
    std::lock_guard<std::mutex> lock(s_tlsMutex);
    for (int i = 0; i < TLS_MINIMUM_AVAILABLE; i++)
    {
        if (!(s_tlsUsedMask & (1ULL << i)))
        {
            s_tlsUsedMask |= (1ULL << i);
            return (DWORD)i;
        }
    }
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return TLS_OUT_OF_INDEXES;
}

BOOL WINAPI TlsFree(DWORD dwTlsIndex)
{
    if (dwTlsIndex >= TLS_MINIMUM_AVAILABLE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::lock_guard<std::mutex> lock(s_tlsMutex);
    if (!(s_tlsUsedMask & (1ULL << dwTlsIndex)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    s_tlsUsedMask &= ~(1ULL << dwTlsIndex);
    return TRUE;
}

LPVOID WINAPI TlsGetValue(DWORD dwTlsIndex)
{
    // Win32: TlsGetValue does not report failure via GetLastError; an
    // invalid index simply yields NULL.
    if (dwTlsIndex >= TLS_MINIMUM_AVAILABLE)
        return NULL;
    return s_tlsSlots[dwTlsIndex];
}

BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)
{
    if (dwTlsIndex >= TLS_MINIMUM_AVAILABLE)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // Not locked on purpose: s_tlsSlots is thread-local, so only the
    // owning thread ever touches this slot.
    s_tlsSlots[dwTlsIndex] = lpTlsValue;
    return TRUE;
}