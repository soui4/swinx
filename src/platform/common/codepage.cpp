/*
 * codepage.cpp -- ANSI/UTF 代码页转换（MultiByteToWideChar / WideCharToMultiByte）
 *
 * 与窗口系统无关，所有平台共用这一份。历史上它有两份：posix/sysapi.cpp 里一份，
 * platform/freertos/codepage.cpp 里一份逐字拷贝，靠注释约定保持同步 —— 结果两边
 * 的 UTF-16 兜底已经漂移（只有 freertos 那份认识 UTF-16 源串，因为裸机 newlib
 * 的 iconv 连 UTF-16 转换器都没有）。现在合并成一份，平台差异改为**运行时判定**：
 *
 *   1. 优先 iconv（Linux/macOS/iOS 桌面链行为不变）；
 *   2. iconv_open 失败或返回无效句柄 -> 用 cptable.h 的静态码表兜底。
 *      走这条路的是 Android(bionic)、OHOS(musl)，以及裸机 newlib（其 iconv 由
 *      freertos/sysapi.cpp 提供返回无效句柄的桩）；
 *   3. 内置兜底覆盖 CP936、Windows-125x，以及 UTF-16LE/BE —— 后者是 PE 资源里
 *      的类型名/名字用的编码，COFF 解析（src/coffparser.cpp）直接依赖它。
 *
 * 所以这份代码里没有一条 #ifdef 平台分支，唯一的条件编译是 WCHAR_SIZE（wchar_t
 * 是 16 位还是 32 位，由构建系统按编译器实际值给出，见 uniconv.h）。
 */
#include <windows.h>
#include "sysapi.h"

#include <assert.h>
#include <errno.h>
#include <iconv.h>
#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <vector>

#include "cptable.h"
#include "uniconv.h"
#include "SwinxUtils.h" // swinx_ansi_codepage()：GetACP 的平台答案
#include <log.h>
#define kLogTag "codepage"
#include "tostring.h"

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
    // UTF-16 也有内置实现(见 builtinToUnicode/builtinToMb 的对应分支): iconv 不可用
    // 时 PE 资源里的 UTF-16 名字必须仍能转换。
    return codePage == 936 || codePage == CP_UTF16_LE || codePage == CP_UTF16_BE ||
           builtinSingleByteTable(codePage) != NULL;
}

/* ---- 代码页的「是什么」查询 ---------------------------------------------
 * 这三个 API 原先分散在各平台目录里，而且已经分叉：posix 按 UTF-8 首字节判 lead
 * byte，freertos 恒返回 FALSE；GetACP 在裸机上还有两份不同的答案（sysapi 与
 * win32_rich 各一份，其中一个文件在某些 target 里根本不参与编译）。
 *
 * 处理方式：GetACP 的值是平台约定（「这平台的 ANSI 串是什么编码」），由
 * swinx_ansi_codepage() 这个契约回答，见 src/SwinxUtils.h；GetACP 本体与由它
 * 派生的 IsDBCSLeadByte 收在这里一份，保证两者永远同口径。
 */
UINT WINAPI GetACP(void)
{
    return (UINT)swinx_ansi_codepage();
}

BOOL WINAPI IsValidCodePage(UINT codePage)
{
    /* 恒 TRUE，且**有意**如此：Win32 里这个 API 的用途是「调用方想确认这个代码页
     * 存在吗」，而 swinx 的转换层（to_unicode/to_mb）对任何代码页都是尽力而为 ——
     * iconv 认识就用 iconv，不认识用内置码表，都不行就整体失败并 SetLastError。
     * 在这里按「我们内置了哪些表」做白名单，会把 Win32 认为有效、而我们只能部分
     * 支持的代码页（932/949/950、ISO-8859-*）判成无效，调用方于是直接放弃转换 ——
     * 比「尝试后失败」更糟。真正的门禁在转换调用的返回值上，不在这条查询上。 */
    (void)codePage;
    return TRUE;
}

BOOL WINAPI IsDBCSLeadByte(BYTE c)
{
    /* 只有多字节 ANSI 码页才有 lead byte：125x 这类单字节码页恒 FALSE（裸机就是
     * 这个分支，与合并前行为一致）。CP_UTF8 下按 UTF-8 首字节特征判断 ——
     * 0xC0..0xFF 是多字节序列的起始，0x80..0xBF 是后续字节。 */
    if (GetACP() != CP_UTF8)
        return FALSE;
    return UTF8CharLength(c) > 1;
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

    // UTF-16 源串：iconv 不可用的平台上没有 UTF-16 转换器（裸机 newlib 与
    // bionic/musl 都没有），这里自带解码。wchar_t=4 时把代理对合成一个码点；
    // wchar_t=2 时原样保留两个单元（wchar_t 本身就是 UTF-16 单元）。
    if (codePage == CP_UTF16_LE || codePage == CP_UTF16_BE)
    {
        const bool little = (codePage == CP_UTF16_LE);
        size_t i = 0;
        for (; i + 1 < input_len; i += 2)
        {
            const unsigned char lo = (unsigned char)input[i];
            const unsigned char hi = (unsigned char)input[i + 1];
            unsigned short u = little ? (unsigned short)(lo | (hi << 8)) : (unsigned short)((lo << 8) | hi);
#if WCHAR_SIZE == 4
            if (u >= 0xD800 && u <= 0xDBFF && i + 3 < input_len)
            {
                const unsigned char lo2 = (unsigned char)input[i + 2];
                const unsigned char hi2 = (unsigned char)input[i + 3];
                unsigned short v = little ? (unsigned short)(lo2 | (hi2 << 8)) : (unsigned short)((lo2 << 8) | hi2);
                if (v >= 0xDC00 && v <= 0xDFFF)
                {
                    out.push_back((wchar_t)(0x10000u + (((unsigned int)(u - 0xD800)) << 10) + (v - 0xDC00)));
                    i += 2;
                    continue;
                }
            }
#endif
            out.push_back((wchar_t)u);
        }
        unConvertedChars = (int)(input_len % 2); // 末尾落单的半个单元
        return (int)out.length();
    }

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
static const std::vector<unsigned int> &cp936ReverseMap()
{
    static const std::vector<unsigned int> *s_map = []() {
        std::vector<unsigned int> *map = new std::vector<unsigned int>();
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
    const std::vector<unsigned int> &map = cp936ReverseMap();
    std::vector<unsigned int>::const_iterator it = std::lower_bound(map.begin(), map.end(), ch << 16);
    if (it != map.end() && (*it >> 16) == ch)
        return (unsigned short)(*it & 0xFFFF);
    return 0;
}

// 用内置码表把宽字符串转成多字节串。返回值语义同 to_mb(写出的字节数); 无法映射的
// 字符按 Win32 语义替换为 '?'。不写结尾 0, 由调用方按 dstLen 处理。
static int builtinToMb(const wchar_t *input, size_t input_len, int codePage, std::string &out)
{
    out.clear();
    out.reserve(input_len * 2);

    // UTF-16 目标串：与上面的解码对称，iconv 不可用时也要能写出 CP_UTF16_LE/BE。
    // 与 Win32 一致，不写 BOM。
    if (codePage == CP_UTF16_LE || codePage == CP_UTF16_BE)
    {
        const bool little = (codePage == CP_UTF16_LE);
        auto put = [&](unsigned short u) {
            out.push_back((char)(little ? (u & 0xFF) : (u >> 8)));
            out.push_back((char)(little ? (u >> 8) : (u & 0xFF)));
        };
        for (size_t i = 0; i < input_len; ++i)
        {
            const unsigned int ch = (unsigned int)input[i];
#if WCHAR_SIZE == 4
            if (ch >= 0x10000)
            { // 补充平面 -> 代理对
                const unsigned int v = ch - 0x10000;
                put((unsigned short)(0xD800 + (v >> 10)));
                put((unsigned short)(0xDC00 + (v & 0x3FF)));
                continue;
            }
#endif
            put((unsigned short)ch);
        }
        return (int)out.length();
    }

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

static int to_mb(const wchar_t *input, size_t input_len, int codePage, std::string &out)
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

int MultiByteToWideChar(int cp, int flags, const char *src, int len, wchar_t *dst, int dstLen)
{
    assert(src);
    if (cp == CP_OEMCP)
        cp = 936;

    if (len < 0)
    {
        // 源串以 NUL 结尾时自己数长度：UTF-16 源要按双字节对齐找 0x0000，
        // 用 strlen 会在第一个高位为 0 的字节处截断（"Hi" -> 只剩 "H"）。
        if (cp == CP_UTF16_LE || cp == CP_UTF16_BE)
        {
            const unsigned char *p = (const unsigned char *)src;
            len = 0;
            while (p[len] || p[len + 1])
                len += 2;
            len += 2; // 含结尾的 0x0000
        }
        else
        {
            len = (int)strlen(src) + 1;
        }
    }
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
        std::string str;
        int ret = to_mb(src, len, cp, str);
        // to_mb 用 0 同时表示「转换失败」和「空串」；len > 0 时一定是失败，
        // len == 0 时是空串（Win32 对空串是成功的，且 len < 0 时还要算上 NUL）。
        if (ret == 0 && len > 0)
            return 0;
        // Win32 语义：dst 为 NULL（或 dstLen 为 0）是「查询所需大小」，此时返回长度
        // 而不是报缓冲区不足。少了这一条，WideCharToMultiByte(936, ..., NULL, 0, ...)
        // 恒返回 0，调用方要么分配 0 字节缓冲，要么直接放弃转换 —— 传统代码页在
        // 这条路上等于不可用（只在 UTF-7/936/125x 分支，CP_UTF8 分支一直是对的）。
        //
        // 返回值含结尾 NUL 的字节数（len < 0 时 Win32 会把它写进缓冲并计入返回
        // 值）；NUL 占几个字节取决于目标编码：UTF-16 是一个 code unit=2 字节。
        const int nulBytes = (cp == CP_UTF16_LE || cp == CP_UTF16_BE) ? 2 : 1;
        const int total = ret + (nullTerminated ? nulBytes : 0);
        if (!dst)
            return total;
        if (total > dstLen)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(dst, str.c_str(), ret);
        if (nullTerminated)
            memset(dst + ret, 0, nulBytes); // 不越过 dstLen：上面的 total 检查已保证
        return total;
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
