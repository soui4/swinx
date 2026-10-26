/* MLang（IMultiLanguage / IMLangFontLink2）的 POSIX 实现。
 *
 * 只做两件事，但都做真：
 *   1. 码页判定。字符属于哪些码页由 Unicode 区块决定，字体覆盖哪些码页由
 *      fontconfig 的 charset 决定。二者用的是同一套 MP_* 位（见 mlang.h），
 *      所以 WebKit 里 `GetCharCodePages(ch) & fontCodePages` 的判交是真判断，
 *      而不是恒定返回 0 或全 1。
 *   2. 缺字回退。MapFont 用 FcFontSort 找一个既覆盖该字符、又不同于当前
 *      字体的家族，按 DC 现有字体的字号/字重 CreateFontIndirect 出来。
 *
 * 与 usp10 的实现边界一致：这里不做整形、不做真正的码页编码表。涉及具体
 * 编码转换的只有 IMultiLanguage::ConvertString*，仅支持 UTF-8 与 Latin-1，
 * 其余码页返回 E_FAIL —— 明确的"不支持"，而不是猜一个可能错的转换。
 *
 * IMultiLanguage2 在此之上补了转换的 fallback / 自动检测与码页信息：检测是
 * BOM + UTF-8 合法性 + 首选码页的真判断；ConvertStringInIStream 是
 * MIME(RFC1522/2047) 语义，本文件没有 MIME 解码器，因此明确 E_NOTIMPL。
 */

#include <windows.h>
#include <mlang.h>
#include <objidl.h> /* IMultiLanguage2 的流式方法要真的调用 IStream */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <mutex>

#include <fontconfig/fontconfig.h>

#include "debug.h"
#define kLogTag "mlang"

namespace
{

/* ------------------------------------------------------------------ 码页表 */

struct CpEntry
{
    UINT cp;
    DWORD bit;
};

/* 同一族的码页共用一个位（Windows 的 "family code page" 概念）。
 * 表里没有的码页一律返回 0 —— 空集会让调用方走"非本码页"分支，
 * 比猜一个位更安全。 */
const CpEntry kCodePages[] = {
    { 1252, MP_LATIN1 },   { 28591, MP_LATIN1 }, { 20127, MP_LATIN1 }, { 437, MP_LATIN1 },
    { 850, MP_LATIN1 },    { 65001, MP_LATIN1 }, { 10000, MP_LATIN1 }, { 1251, MP_CYRILLIC },
    { 20866, MP_CYRILLIC }, { 21866, MP_CYRILLIC }, { 855, MP_CYRILLIC }, { 866, MP_CYRILLIC },
    { 1250, MP_CENTRALEU }, { 852, MP_CENTRALEU }, { 895, MP_CENTRALEU }, { 1253, MP_GREEK },
    { 737, MP_GREEK },     { 869, MP_GREEK },     { 1254, MP_TURKISH }, { 1255, MP_HEBREW },
    { 862, MP_HEBREW },    { 1256, MP_ARABIC },   { 864, MP_ARABIC },   { 720, MP_ARABIC },
    { 1257, MP_BALTIC },   { 775, MP_BALTIC },    { 1258, MP_VIETNAM }, { 874, MP_THAI },
    { 932, MP_JAPANESE },  { 51932, MP_JAPANESE }, { 50220, MP_JAPANESE }, { 50221, MP_JAPANESE },
    { 50222, MP_JAPANESE }, { 20932, MP_JAPANESE }, { 936, MP_SCHINESE }, { 54936, MP_SCHINESE },
    { 20936, MP_SCHINESE }, { 949, MP_KOREAN },   { 51949, MP_KOREAN }, { 50225, MP_KOREAN },
    { 950, MP_TCHINESE },  { 51950, MP_TCHINESE }, { 50227, MP_TCHINESE }, { 1361, MP_JOHAB },
    { 10003, MP_JOHAB },   { 42, MP_SYMBOL },
};

/* 每个族的代表码页（CodePagesToCodePage 用）。 */
struct BitEntry
{
    DWORD bit;
    UINT cp;
};

const BitEntry kRepCodePages[] = {
    { MP_LATIN1, 1252 },   { MP_CYRILLIC, 1251 }, { MP_CENTRALEU, 1250 }, { MP_GREEK, 1253 },
    { MP_TURKISH, 1254 },  { MP_HEBREW, 1255 },   { MP_ARABIC, 1256 },    { MP_BALTIC, 1257 },
    { MP_VIETNAM, 1258 },  { MP_THAI, 874 },      { MP_JAPANESE, 932 },   { MP_SCHINESE, 936 },
    { MP_KOREAN, 949 },    { MP_TCHINESE, 950 },  { MP_JOHAB, 1361 },     { MP_SYMBOL, 42 },
};

/* 每个族的一个"探测字符"，用于问 fontconfig "这个字体覆盖该族吗"。 */
struct FamilyProbe
{
    DWORD bit;
    FcChar32 ch;
};

const FamilyProbe kProbes[] = {
    { MP_LATIN1, 0x00C5 },   /* Å */
    { MP_CYRILLIC, 0x0410 }, /* А */
    { MP_CENTRALEU, 0x0104 }, /* Ą */
    { MP_GREEK, 0x0391 },    /* Α */
    { MP_TURKISH, 0x011E },  /* Ğ */
    { MP_HEBREW, 0x05D0 },   /* א */
    { MP_ARABIC, 0x0627 },   /* ا */
    { MP_BALTIC, 0x0100 },   /* Ā */
    { MP_VIETNAM, 0x1EA1 },  /* ạ */
    { MP_THAI, 0x0E01 },     /* ก */
    { MP_JAPANESE, 0x3042 }, /* あ */
    { MP_KOREAN, 0xAC00 },   /* 가 */
};

/* 汉字：GBK / Big5 / Shift-JIS / EUC-KR 的汉字区高度重叠，单字无法区分简体与
 * 繁体字体。这里要求两个常用汉字都覆盖才认为该字体"含中文"，把误判方向压到
 * "宁可说它不含" —— 误判成"含"会让 WebKit 停止回退，直接画出豆腐块。 */
const FcChar32 kHanProbes[] = { 0x4E2D /* 中 */, 0x6C49 /* 汉 */ };

/* 从 locale 环境变量猜"当前 ANSI 码页"。CP_ACP 在 POSIX 上没有系统值，
 * 而 WebKit 用 CP_ACP 的码页掩码当作"用户默认码页"的偏好位。 */
UINT DefaultAnsiCodePage()
{
    const char *vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
    std::string lang;
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]) && lang.empty(); ++i)
    {
        const char *v = getenv(vars[i]);
        if (v && *v)
            lang = v;
    }
    if (lang.empty())
        return 1252;

    for (size_t i = 0; i < lang.size(); ++i)
        lang[i] = (char)tolower((unsigned char)lang[i]);

    if (lang.find("zh_cn") != std::string::npos || lang.find("zh_hans") != std::string::npos ||
        lang.find("gb") != std::string::npos)
        return 936;
    if (lang.find("zh_tw") != std::string::npos || lang.find("zh_hk") != std::string::npos ||
        lang.find("zh_hant") != std::string::npos || lang.find("big5") != std::string::npos)
        return 950;
    if (lang.find("ja") != std::string::npos)
        return 932;
    if (lang.find("ko") != std::string::npos)
        return 949;
    if (lang.find("ru") != std::string::npos || lang.find("uk") != std::string::npos)
        return 1251;
    if (lang.find("el") != std::string::npos)
        return 1253;
    if (lang.find("he") != std::string::npos)
        return 1255;
    if (lang.find("ar") != std::string::npos)
        return 1256;
    if (lang.find("th") != std::string::npos)
        return 874;
    return 1252;
}

DWORD CodePageMask(UINT cp)
{
    if (cp == CP_ACP || cp == CP_THREAD_ACP || cp == CP_OEMCP)
        cp = DefaultAnsiCodePage();
    for (size_t i = 0; i < sizeof(kCodePages) / sizeof(kCodePages[0]); ++i)
    {
        if (kCodePages[i].cp == cp)
            return kCodePages[i].bit;
    }
    return 0;
}

UINT FamilyCodePage(UINT cp)
{
    DWORD mask = CodePageMask(cp);
    if (mask == 0)
        return cp; /* 未知码页就返回它自己：GetFamilyCodePage 的定义是"同族代表" */
    for (size_t i = 0; i < sizeof(kRepCodePages) / sizeof(kRepCodePages[0]); ++i)
    {
        if (kRepCodePages[i].bit == mask)
            return kRepCodePages[i].cp;
    }
    return cp;
}

/* 字符 → 码页掩码。按 Unicode 区块给出，不做"某码页到底收了哪些字"的精确
 * 表：那需要真正的码页表（CJK 各家的映射表），而调用方只拿它做判交。 */
DWORD CharCodePages(FcChar32 ch)
{
    if (ch < 0x80)
        return MP_ALL; /* ASCII 在每个码页里都在 */
    if (ch < 0x100)
        return MP_LATIN1 | MP_CENTRALEU | MP_TURKISH | MP_BALTIC | MP_VIETNAM | MP_CYRILLIC |
               MP_GREEK | MP_HEBREW;
    if (ch <= 0x017F) /* Latin Extended-A */
        return MP_CENTRALEU | MP_TURKISH | MP_BALTIC | MP_VIETNAM | MP_CYRILLIC;
    if (ch <= 0x024F) /* Latin Extended-B */
        return MP_VIETNAM | MP_CENTRALEU | MP_TURKISH | MP_BALTIC;
    if (ch >= 0x0370 && ch <= 0x03FF)
        return MP_GREEK;
    if (ch >= 0x0400 && ch <= 0x052F)
        return MP_CYRILLIC;
    if (ch >= 0x0590 && ch <= 0x05FF)
        return MP_HEBREW;
    if (ch >= 0x0600 && ch <= 0x06FF)
        return MP_ARABIC;
    if (ch >= 0x0E00 && ch <= 0x0E7F)
        return MP_THAI;
    if (ch >= 0x1E00 && ch <= 0x1EFF)
        return MP_VIETNAM;
    if (ch >= 0x2000 && ch <= 0x206F) /* 通用标点 */
        return MP_ALL;
    if (ch >= 0x2E80 && ch <= 0x2FFF) /* CJK 部首 / 康熙部首 */
        return MP_JAPANESE | MP_SCHINESE | MP_TCHINESE | MP_KOREAN;
    if (ch >= 0x3000 && ch <= 0x303F) /* CJK 标点 */
        return MP_ALL | MP_JAPANESE | MP_SCHINESE | MP_TCHINESE | MP_KOREAN;
    if (ch >= 0x3040 && ch <= 0x30FF) /* 假名 */
        return MP_JAPANESE;
    if (ch >= 0x3400 && ch <= 0x4DBF) /* 扩展 A */
        return MP_SCHINESE | MP_TCHINESE | MP_JAPANESE | MP_KOREAN;
    if (ch >= 0x4E00 && ch <= 0x9FFF) /* 基本汉字 */
        return MP_SCHINESE | MP_TCHINESE | MP_JAPANESE | MP_KOREAN;
    if (ch >= 0xAC00 && ch <= 0xD7AF) /* 韩文音节 */
        return MP_KOREAN;
    if (ch >= 0xF900 && ch <= 0xFAFF) /* 兼容汉字 */
        return MP_SCHINESE | MP_TCHINESE | MP_JAPANESE;
    if (ch >= 0xFF00 && ch <= 0xFFEF) /* 全角 / 半角 */
        return MP_JAPANESE | MP_SCHINESE | MP_TCHINESE | MP_KOREAN;
    return MP_LATIN1;
}

/* 码页掩码 → 一个用来找字体的字符。优先 CJK（这些族最可能是"缺字"现场）。 */
WCHAR RepresentativeChar(DWORD mask)
{
    if (mask & (MP_SCHINESE | MP_TCHINESE))
        return 0x4E2D;
    if (mask & MP_JAPANESE)
        return 0x3042;
    if (mask & MP_KOREAN)
        return 0xAC00;
    if (mask & MP_CYRILLIC)
        return 0x0410;
    if (mask & MP_GREEK)
        return 0x0391;
    if (mask & MP_HEBREW)
        return 0x05D0;
    if (mask & MP_ARABIC)
        return 0x0627;
    if (mask & MP_THAI)
        return 0x0E01;
    if (mask & MP_VIETNAM)
        return 0x1EA1;
    if (mask & MP_CENTRALEU)
        return 0x0104;
    if (mask & MP_TURKISH)
        return 0x011E;
    if (mask & MP_BALTIC)
        return 0x0100;
    return 0x0041;
}

/* 一段文本"共用同一组码页"的最长前缀。返回消耗的字符数。
 *
 * 至少返回 1：调用方（SimpleFontData::containsCharacters）用它推进 offset，
 * 返回 0 会死循环。这是本函数唯一不能含糊的约束，所以它单独成函数，
 * 好让单测能直接盯住它。 */
LONG CodePagesOfRun(const WCHAR *src, long cch, DWORD priority, DWORD *outMask)
{
    if (!src || cch <= 0)
    {
        if (outMask)
            *outMask = 0;
        return 0;
    }

    DWORD acc = CharCodePages((FcChar32)src[0]);
    LONG i = 1;
    for (; i < cch; ++i)
    {
        DWORD cur = CharCodePages((FcChar32)src[i]);
        if ((acc & cur) == 0)
            break;
        acc &= cur;
    }

    /* 优先码页：调用方给的是"用户默认码页"的位。若累积结果与它有交，
     * 就收窄到它 —— 这正是 mlang 用 dwPriorityCodePages 的意图。 */
    if (priority && (acc & priority))
        acc &= priority;

    if (outMask)
        *outMask = acc;
    return i;
}

/* UTF-8 / Latin-1 → WCHAR。返回写入的字符数，*consumed 是吃掉的字节数。
 * 遇到截断序列就停在能解出的地方，不跳过也不猜；给了 fallback（IMultiLanguage2
 * 的 lpFallBack）则写出 fallback 并把该字节吃掉，这是"坏字节也要有输出"的
 * 唯一一条路。 */
UINT DecodeToUnicode(bool utf8, const unsigned char *in, UINT inSize, WCHAR *out, UINT capacity,
                     UINT *consumed, const WCHAR *fallback = NULL)
{
    UINT used = 0;
    UINT taken = 0;

    if (consumed)
        *consumed = 0;
    if (!in)
        return 0;

    while (taken < inSize && used < capacity)
    {
        FcChar32 ch = 0;
        UINT step = 1;

        if (!utf8)
        {
            ch = in[taken];
        }
        else
        {
            unsigned char c0 = in[taken];
            if (c0 < 0x80)
            {
                ch = c0;
            }
            else if ((c0 & 0xE0) == 0xC0 && taken + 1 < inSize)
            {
                ch = (FcChar32)(((c0 & 0x1F) << 6) | (in[taken + 1] & 0x3F));
                step = 2;
            }
            else if ((c0 & 0xF0) == 0xE0 && taken + 2 < inSize)
            {
                ch = (FcChar32)(((c0 & 0x0F) << 12) | ((in[taken + 1] & 0x3F) << 6) |
                                (in[taken + 2] & 0x3F));
                step = 3;
            }
            else if ((c0 & 0xF8) == 0xF0 && taken + 3 < inSize)
            {
                ch = (FcChar32)(((c0 & 0x07) << 18) | ((in[taken + 1] & 0x3F) << 12) |
                                ((in[taken + 2] & 0x3F) << 6) | (in[taken + 3] & 0x3F));
                step = 4;
            }
            else
            {
                if (!fallback)
                    break; /* 截断序列 */
                for (const WCHAR *f = fallback; *f && used < capacity; ++f)
                {
                    if (out)
                        out[used] = *f;
                    ++used;
                }
                ++taken;
                continue;
            }
        }

        if (out)
            out[used] = (WCHAR)ch;
        ++used;
        taken += step;
    }

    if (consumed)
        *consumed = taken;
    return used;
}

/* WCHAR → UTF-8 / Latin-1。返回写入的字节数，*consumed 是用掉的字符数。
 * Latin-1 装不下的码点停在那里 —— 不输出 '?'，那会让调用方以为转换成功；
 * 只有显式给了 fallback（lpFallBack / USEDEFCHAR）才写替代字符并继续。 */
UINT EncodeFromUnicode(bool utf8, const WCHAR *in, UINT count, unsigned char *out, UINT capacity,
                       UINT *consumed, const WCHAR *fallback = NULL)
{
    UINT used = 0;
    UINT taken = 0;

    if (consumed)
        *consumed = 0;
    if (!in)
        return 0;

    while (taken < count)
    {
        FcChar32 ch = (FcChar32)in[taken];
        UINT need = 1;
        if (utf8)
        {
            if (ch >= 0x10000)
                need = 4;
            else if (ch >= 0x800)
                need = 3;
            else if (ch >= 0x80)
                need = 2;
        }
        else if (ch > 0xFF)
        {
            if (!fallback)
                break;

            /* fallback 本身也要按目标码页编码；它里面再有转不了的字符就丢掉，
             * 不递归（真实 mlang 也不展开嵌套的 fallback）。 */
            bool overflow = false;
            for (const WCHAR *f = fallback; *f; ++f)
            {
                FcChar32 fc = (FcChar32)*f;
                if (fc > 0xFF)
                    continue;
                if (used + 1 > capacity)
                {
                    overflow = true;
                    break;
                }
                if (out)
                    out[used] = (unsigned char)fc;
                ++used;
            }
            if (overflow)
                break;
            ++taken;
            continue;
        }

        if (used + need > capacity)
            break;

        if (out)
        {
            if (!utf8 || need == 1)
            {
                out[used] = (unsigned char)ch;
            }
            else if (need == 2)
            {
                out[used] = (unsigned char)(0xC0 | (ch >> 6));
                out[used + 1] = (unsigned char)(0x80 | (ch & 0x3F));
            }
            else if (need == 3)
            {
                out[used] = (unsigned char)(0xE0 | (ch >> 12));
                out[used + 1] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                out[used + 2] = (unsigned char)(0x80 | (ch & 0x3F));
            }
            else
            {
                out[used] = (unsigned char)(0xF0 | (ch >> 18));
                out[used + 1] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                out[used + 2] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                out[used + 3] = (unsigned char)(0x80 | (ch & 0x3F));
            }
        }

        used += need;
        ++taken;
    }

    if (consumed)
        *consumed = taken;
    return used;
}

/* -------------------------------------------------------- 码页描述与检测 */

const WCHAR *CodePageDescription(UINT cp)
{
    switch (cp)
    {
    case 65001:
        return L"Unicode (UTF-8)";
    case 1200:
        return L"Unicode";
    case 1201:
        return L"Unicode (Big-Endian)";
    case 20127:
        return L"US-ASCII";
    case 28591:
        return L"Western European (ISO)";
    case 10000:
        return L"Macintosh";
    case 437:
        return L"OEM United States";
    case 850:
        return L"OEM Latin 1";
    default:
        break;
    }

    switch (CodePageMask(cp))
    {
    case MP_LATIN1:
        return L"Western European (Windows)";
    case MP_CYRILLIC:
        return L"Cyrillic (Windows)";
    case MP_CENTRALEU:
        return L"Central European (Windows)";
    case MP_GREEK:
        return L"Greek (Windows)";
    case MP_TURKISH:
        return L"Turkish (Windows)";
    case MP_HEBREW:
        return L"Hebrew (Windows)";
    case MP_ARABIC:
        return L"Arabic (Windows)";
    case MP_BALTIC:
        return L"Baltic (Windows)";
    case MP_VIETNAM:
        return L"Vietnamese (Windows)";
    case MP_THAI:
        return L"Thai (Windows)";
    case MP_JAPANESE:
        return L"Japanese (Shift-JIS)";
    case MP_SCHINESE:
        return L"Chinese Simplified (GB2312)";
    case MP_KOREAN:
        return L"Korean";
    case MP_TCHINESE:
        return L"Chinese Traditional (Big5)";
    case MP_JOHAB:
        return L"Korean (Johab)";
    case MP_SYMBOL:
        return L"Symbol";
    default:
        return NULL;
    }
}

ULONG LanguageIdOfCodePage(UINT cp)
{
    switch (CodePageMask(cp))
    {
    case MP_CYRILLIC:
        return 0x0419; /* ru-RU */
    case MP_CENTRALEU:
        return 0x0405; /* cs-CZ */
    case MP_GREEK:
        return 0x0408; /* el-GR */
    case MP_TURKISH:
        return 0x041F; /* tr-TR */
    case MP_HEBREW:
        return 0x040D; /* he-IL */
    case MP_ARABIC:
        return 0x0401; /* ar-SA */
    case MP_BALTIC:
        return 0x0425; /* lt-LT */
    case MP_VIETNAM:
        return 0x042A; /* vi-VN */
    case MP_THAI:
        return 0x041E; /* th-TH */
    case MP_JAPANESE:
        return 0x0411; /* ja-JP */
    case MP_SCHINESE:
        return 0x0804; /* zh-CN */
    case MP_KOREAN:
        return 0x0412; /* ko-KR */
    case MP_TCHINESE:
        return 0x0404; /* zh-TW */
    case MP_JOHAB:
        return 0x0412; /* ko-KR */
    case MP_SYMBOL:
        return 0;
    default:
        return 0x0409; /* en-US */
    }
}

struct DetectCandidate
{
    UINT cp;
    int percent;
    int confidence;
};

/* 严格的 UTF-8 合法性检查：起始字节的合法区间与每个续字节都要对。ASCII
 * 文本也算"合法 UTF-8"，由调用方用 hasMulti 区分"确实用了多字节"的情况。 */
bool ValidUtf8(const unsigned char *s, size_t n, bool *hasMulti)
{
    size_t i = 0;
    bool multi = false;

    while (i < n)
    {
        unsigned char c = s[i];
        if (c < 0x80)
        {
            ++i;
            continue;
        }

        size_t tail;
        if (c >= 0xC2 && c <= 0xDF)
            tail = 1;
        else if (c >= 0xE0 && c <= 0xEF)
            tail = 2;
        else if (c >= 0xF0 && c <= 0xF4)
            tail = 3;
        else
            return false;

        if (i + tail >= n)
            return false;
        for (size_t k = 1; k <= tail; ++k)
        {
            if ((s[i + k] & 0xC0) != 0x80)
                return false;
        }

        multi = true;
        i += tail + 1;
    }

    if (hasMulti)
        *hasMulti = multi;
    return true;
}

void AddCandidate(std::vector<DetectCandidate> &list, UINT cp, int percent, int confidence)
{
    if (cp == 0)
        return;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].cp == cp)
            return; /* 同一码页只保留第一次（也就是可信度最高的那次） */
    }

    DetectCandidate c;
    c.cp = cp;
    c.percent = percent;
    c.confidence = confidence;
    list.push_back(c);
}

/* 从一段字节给出编码候选，按可信度降序。
 *
 * BOM 是确定性证据；没有 BOM 时只有"UTF-8 整体合法"这一条硬判据。单字节
 * 码页（含首选码页）对任意字节序列都成立，所以它们的可信度永远低于已经
 * 通过校验的 UTF-8 —— 这正是真实检测器"宁可报低可信度也不漏候选"的做法。 */
std::vector<DetectCandidate> DetectEncodings(DWORD dwFlag, DWORD dwPrefWinCodePage, const unsigned char *src,
                                             size_t size)
{
    std::vector<DetectCandidate> list;
    if (!src || size == 0)
        return list;

    if (size >= 3 && src[0] == 0xEF && src[1] == 0xBB && src[2] == 0xBF)
    {
        AddCandidate(list, 65001, 100, 100);
        return list;
    }
    if (size >= 2 && src[0] == 0xFF && src[1] == 0xFE)
    {
        AddCandidate(list, 1200, 100, 100);
        return list;
    }
    if (size >= 2 && src[0] == 0xFE && src[1] == 0xFF)
    {
        AddCandidate(list, 1201, 100, 100);
        return list;
    }

    bool sevenBit = true;
    for (size_t i = 0; i < size; ++i)
    {
        if (src[i] >= 0x80)
        {
            sevenBit = false;
            break;
        }
    }

    bool hasMulti = false;
    bool utf8 = ValidUtf8(src, size, &hasMulti);

    UINT preferred = (UINT)dwPrefWinCodePage;
    if (preferred == 0 || preferred == CP_ACP || preferred == CP_THREAD_ACP || preferred == CP_OEMCP)
        preferred = DefaultAnsiCodePage();

    if (sevenBit)
    {
        AddCandidate(list, preferred, 100, 60);
        AddCandidate(list, 65001, 100, 50);
        AddCandidate(list, 1252, 100, 40);
    }
    else
    {
        if (utf8)
            AddCandidate(list, 65001, 100, hasMulti ? 90 : 70);
        AddCandidate(list, preferred, 100, 60);
        AddCandidate(list, 1252, 100, 40);
    }

    /* MLDETECTCP 的位限制（0 表示不限制）。 */
    if (dwFlag != 0)
    {
        for (size_t i = list.size(); i > 0; --i)
        {
            const DetectCandidate &c = list[i - 1];
            bool seven = c.cp == 20127;
            bool allowed = seven ? ((dwFlag & MLDETECTCP_7BIT) != 0) : ((dwFlag & MLDETECTCP_8BIT) != 0);
            if (!allowed)
                list.erase(list.begin() + (i - 1));
        }
    }

    std::stable_sort(list.begin(), list.end(), [](const DetectCandidate &a, const DetectCandidate &b) {
        return a.confidence > b.confidence;
    });
    return list;
}

/* ConvertString* 支持哪些码页：只有 UTF-8 与 Latin-1（及其别名）。检测结果
 * 落到别的码页时不能拿来做转换。 */
bool ConvertibleEncoding(UINT cp)
{
    return cp == CP_UTF8 || cp == 65001 || cp == 1252 || cp == 28591 || cp == 20127 || cp == CP_ACP;
}

/* ConvertString*Ex 的 fallback 解析：NOFALLBACKS 优先，其次是调用方给的串，
 * 最后才是 USEDEFCHAR 的 '?'。 */
const WCHAR *ResolveFallback(DWORD dwFlag, WCHAR *lpFallBack)
{
    static const WCHAR kDefChar[] = { 0x3F, 0 };

    if (dwFlag & MLCONVCHARF_NOFALLBACKS)
        return NULL;
    if (lpFallBack)
        return lpFallBack;
    if (dwFlag & (MLCONVCHARF_USEDEFCHAR | MLCONVCHARF_USEDEFCHAR2))
        return kDefChar;
    return NULL;
}

/* ------------------------------------------------------------ 字体覆盖查询 */

std::mutex &FontMutex()
{
    static std::mutex mtx;
    return mtx;
}

/* 把 family 解析成实际字体，返回它的 charset 覆盖了哪些族。 */
DWORD FontCodePagesOfFamily(const char *familyUtf8)
{
    DWORD mask = 0;

    FcPattern *pattern = FcPatternCreate();
    if (!pattern)
        return 0;
    if (familyUtf8 && *familyUtf8)
        FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)familyUtf8);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match)
        return 0;

    FcCharSet *charset = NULL;
    if (FcPatternGetCharSet(match, FC_CHARSET, 0, &charset) != FcResultMatch || !charset)
    {
        FcPatternDestroy(match);
        return 0;
    }

    for (size_t i = 0; i < sizeof(kProbes) / sizeof(kProbes[0]); ++i)
    {
        if (FcCharSetHasChar(charset, kProbes[i].ch))
            mask |= kProbes[i].bit;
    }

    bool han = true;
    for (size_t i = 0; i < sizeof(kHanProbes) / sizeof(kHanProbes[0]); ++i)
    {
        if (!FcCharSetHasChar(charset, kHanProbes[i]))
        {
            han = false;
            break;
        }
    }
    if (han)
        mask |= MP_SCHINESE | MP_TCHINESE;

    FcPatternDestroy(match);
    return mask;
}

/* 找一个覆盖 ch 且家族名不同于 baseFamily 的字体族。
 * 返回空串表示"没有别的字体能画它"。 */
std::string FindCoveringFamily(FcChar32 ch, const char *baseFamily)
{
    FcPattern *pattern = FcPatternCreate();
    if (!pattern)
        return std::string();

    FcCharSet *want = FcCharSetCreate();
    if (want)
    {
        FcCharSetAddChar(want, ch);
        FcPatternAddCharSet(pattern, FC_CHARSET, want);
        FcCharSetDestroy(want);
    }
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcFontSet *fonts = FcFontSort(NULL, pattern, FcTrue, NULL, &result);
    FcPatternDestroy(pattern);
    if (!fonts)
        return std::string();

    std::string found;
    for (int i = 0; i < fonts->nfont && found.empty(); ++i)
    {
        FcCharSet *charset = NULL;
        if (FcPatternGetCharSet(fonts->fonts[i], FC_CHARSET, 0, &charset) != FcResultMatch || !charset)
            continue;
        if (!FcCharSetHasChar(charset, ch))
            continue;

        FcChar8 *family = NULL;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) != FcResultMatch || !family)
            continue;
        if (baseFamily && *baseFamily && strcmp((const char *)family, baseFamily) == 0)
            continue; /* 与当前字体同名 —— 换它没有意义 */

        found = (const char *)family;
    }

    FcFontSetDestroy(fonts);
    return found;
}

/* mlang 自己创建、需要由 ReleaseFont 回收的字体。真实 mlang 的所有权模型是
 * "MapFont 出来的字体归 mlang，调用方只负责 GetObject + ReleaseFont"。 */
std::map<HFONT, bool> &MappedFonts()
{
    static std::map<HFONT, bool> fonts;
    return fonts;
}

class CLangFontLink2;

class CMultiLanguage : public IMultiLanguage2
{
public:
    CMultiLanguage();

    /* IUnknown */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFGUID riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();

    /* IMultiLanguage */
    HRESULT STDMETHODCALLTYPE GetFamilyCodePage(UINT uiCodePage, UINT *puiFamilyCodePage);
    HRESULT STDMETHODCALLTYPE IsConvertible(DWORD dwSrcEncoding, DWORD dwDstEncoding);
    HRESULT STDMETHODCALLTYPE ConvertStringToUnicode(DWORD *pdwMode, DWORD dwEncoding, CHAR *pSrcStr,
                                                     UINT *pcSrcSize, WCHAR *pDstStr, UINT *pcDstSize);
    HRESULT STDMETHODCALLTYPE ConvertStringFromUnicode(DWORD *pdwMode, DWORD dwEncoding, WCHAR *pSrcStr,
                                                       UINT *pcSrcSize, CHAR *pDstStr, UINT *pcDstSize);
    HRESULT STDMETHODCALLTYPE ConvertStringReset(void);

    /* IMultiLanguage2 */
    HRESULT STDMETHODCALLTYPE ConvertStringInIStream(DWORD *pdwMode, DWORD dwFlag, WCHAR *lpFallBack,
                                                     IStream *pstmIn, IStream *pstmOut);
    HRESULT STDMETHODCALLTYPE ConvertStringToUnicodeEx(DWORD *pdwMode, DWORD dwEncoding, CHAR *pSrcStr,
                                                       UINT *pcSrcSize, WCHAR *pDstStr, UINT *pcDstSize,
                                                       DWORD dwFlag, WCHAR *lpFallBack);
    HRESULT STDMETHODCALLTYPE ConvertStringFromUnicodeEx(DWORD *pdwMode, DWORD dwEncoding, WCHAR *pSrcStr,
                                                         UINT *pcSrcSize, CHAR *pDstStr, UINT *pcDstSize,
                                                         DWORD dwFlag, WCHAR *lpFallBack);
    HRESULT STDMETHODCALLTYPE DetectCodepageInIStream(DWORD dwFlag, DWORD dwPrefWinCodePage, IStream *pstmIn,
                                                      DetectEncodingInfo *lpEncoding, ULONG *pnNumEncoding);
    HRESULT STDMETHODCALLTYPE DetectInputCodepage(DWORD dwFlag, DWORD dwPrefWinCodePage, CHAR *pSrcStr,
                                                  INT *pcSrcSize, DetectEncodingInfo *lpEncoding,
                                                  INT *pnNumEncoding);
    HRESULT STDMETHODCALLTYPE ValidateCodePage(UINT uiCodePage, HWND hwnd);
    HRESULT STDMETHODCALLTYPE GetCodePageDescription(UINT uiCodePage, LPWSTR lpWideCharStr, int cchWideChar);
    HRESULT STDMETHODCALLTYPE IsCodePageInstallable(UINT uiCodePage);
    HRESULT STDMETHODCALLTYPE SetMimeDecodeOptions(DWORD dwMimeDecodeOptions);
    HRESULT STDMETHODCALLTYPE GetMimeDecodeOptions(DWORD *pdwMimeDecodeOptions);

private:
    LONG m_ref;
    CLangFontLink2 *m_fontLink;
    DWORD m_mimeDecodeOptions;
};

class CLangFontLink2 : public IMLangFontLink2
{
public:
    explicit CLangFontLink2(CMultiLanguage *owner)
        : m_owner(owner)
    {
    }

    /* IUnknown（引用计数与 CMultiLanguage 共用，生命周期由它管） */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFGUID riid, void **ppvObject);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();

    /* IMLangCodePages */
    HRESULT STDMETHODCALLTYPE GetCharCodePages(WCHAR chSrc, DWORD *pdwCodePages);
    HRESULT STDMETHODCALLTYPE GetStrCodePages(const WCHAR *pszSrc, long cchSrc, DWORD dwPriorityCodePages,
                                              DWORD *pdwCodePages, long *pcchCodePages);
    HRESULT STDMETHODCALLTYPE CodePageToCodePages(UINT uCodePage, DWORD *pdwCodePages);
    HRESULT STDMETHODCALLTYPE CodePagesToCodePage(DWORD dwCodePages, UINT uDefaultCodePage, UINT *puCodePage);

    /* IMLangFontLink2 */
    HRESULT STDMETHODCALLTYPE GetFontCodePages(HDC hDC, HFONT hFont, DWORD *pdwCodePages);
    HRESULT STDMETHODCALLTYPE ReleaseFont(HFONT hFont);
    HRESULT STDMETHODCALLTYPE ResetFontMapping(void);
    HRESULT STDMETHODCALLTYPE MapFont(HDC hDC, DWORD dwCodePages, WCHAR chSrc, HFONT *pFont);
    HRESULT STDMETHODCALLTYPE GetFontUnicodeRanges(HDC hDC, UINT *puiRanges, UNICODERANGE *pUranges);
    HRESULT STDMETHODCALLTYPE GetScriptFontInfo(SCRIPT_ID sid, DWORD dwFlags, UINT *puiFonts,
                                                LPSCRIPTFONTINFO pScriptFont);
    HRESULT STDMETHODCALLTYPE CodePageToScriptID(UINT uiCodePage, SCRIPT_ID *pSid);

private:
    CMultiLanguage *m_owner;
};

CMultiLanguage::CMultiLanguage()
    : m_ref(1)
    , m_mimeDecodeOptions(MIMEDECODEOPT_NONE)
{
    m_fontLink = new CLangFontLink2(this);
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::QueryInterface(REFGUID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualGUID(riid, IID_IUnknown))
    {
        *ppvObject = static_cast<IUnknown *>(this);
        AddRef();
        return S_OK;
    }
    if (IsEqualGUID(riid, IID_IMultiLanguage) || IsEqualGUID(riid, IID_IMultiLanguage2))
    {
        *ppvObject = static_cast<IMultiLanguage2 *>(this);
        AddRef();
        return S_OK;
    }
    if (IsEqualGUID(riid, IID_IMLangCodePages) || IsEqualGUID(riid, IID_IMLangFontLink2))
    {
        *ppvObject = static_cast<IMLangFontLink2 *>(m_fontLink);
        AddRef();
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CMultiLanguage::AddRef()
{
    return (ULONG)__sync_add_and_fetch(&m_ref, 1);
}

ULONG STDMETHODCALLTYPE CMultiLanguage::Release()
{
    LONG ref = __sync_sub_and_fetch(&m_ref, 1);
    if (ref <= 0)
    {
        delete m_fontLink;
        delete this;
    }
    return (ULONG)(ref > 0 ? ref : 0);
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::QueryInterface(REFGUID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IMLangCodePages) ||
        IsEqualGUID(riid, IID_IMLangFontLink2))
    {
        *ppvObject = static_cast<IMLangFontLink2 *>(this);
        m_owner->AddRef();
        return S_OK;
    }
    if (IsEqualGUID(riid, IID_IMultiLanguage) || IsEqualGUID(riid, IID_IMultiLanguage2))
    {
        *ppvObject = static_cast<IMultiLanguage2 *>(m_owner);
        m_owner->AddRef();
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE CLangFontLink2::AddRef()
{
    return m_owner->AddRef();
}

ULONG STDMETHODCALLTYPE CLangFontLink2::Release()
{
    /* m_owner 的 Release 可能把本对象一起删掉，所以这里不再触碰成员。 */
    return m_owner->Release();
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::GetFamilyCodePage(UINT uiCodePage, UINT *puiFamilyCodePage)
{
    if (!puiFamilyCodePage)
        return E_POINTER;
    *puiFamilyCodePage = FamilyCodePage(uiCodePage);
    return S_OK;
}

/* swinx 只认识 UTF-8 与 Latin-1（以及二者的"别名"码页）。问别的组合时
 * 明确说不能转，而不是给出一条可能悄悄截断的路径。 */
HRESULT STDMETHODCALLTYPE CMultiLanguage::IsConvertible(DWORD dwSrcEncoding, DWORD dwDstEncoding)
{
    UINT src = (UINT)dwSrcEncoding;
    UINT dst = (UINT)dwDstEncoding;

    bool srcOk = src == CP_UTF8 || src == 1252 || src == 28591 || src == 20127 || src == CP_ACP;
    bool dstOk = dst == CP_UTF8 || dst == 1252 || dst == 28591 || dst == 20127 || dst == CP_ACP;

    return (srcOk && dstOk) ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringToUnicode(DWORD *pdwMode, DWORD dwEncoding,
                                                                 CHAR *pSrcStr, UINT *pcSrcSize,
                                                                 WCHAR *pDstStr, UINT *pcDstSize)
{
    (void)pdwMode; /* 无状态转换：不做 shift-in/out 之类的有状态码页 */
    if (!pcSrcSize || !pcDstSize)
        return E_POINTER;
    if ((*pcSrcSize > 0 && !pSrcStr) || (*pcDstSize > 0 && !pDstStr))
        return E_POINTER;

    UINT src = (UINT)dwEncoding;
    bool utf8 = src == CP_UTF8;
    bool latin1 = src == 1252 || src == 28591 || src == 20127 || src == CP_ACP;
    if (!utf8 && !latin1)
        return E_FAIL;

    UINT consumed = 0;
    UINT written = DecodeToUnicode(utf8, (const unsigned char *)pSrcStr, *pcSrcSize, pDstStr, *pcDstSize,
                                   &consumed);

    *pcSrcSize = consumed;
    *pcDstSize = written;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringFromUnicode(DWORD *pdwMode, DWORD dwEncoding,
                                                                   WCHAR *pSrcStr, UINT *pcSrcSize,
                                                                   CHAR *pDstStr, UINT *pcDstSize)
{
    (void)pdwMode;
    if (!pcSrcSize || !pcDstSize)
        return E_POINTER;
    if ((*pcSrcSize > 0 && !pSrcStr) || (*pcDstSize > 0 && !pDstStr))
        return E_POINTER;

    UINT dst = (UINT)dwEncoding;
    bool utf8 = dst == CP_UTF8;
    bool latin1 = dst == 1252 || dst == 28591 || dst == 20127 || dst == CP_ACP;
    if (!utf8 && !latin1)
        return E_FAIL;

    UINT consumed = 0;
    UINT written = EncodeFromUnicode(utf8, pSrcStr, *pcSrcSize, (unsigned char *)pDstStr, *pcDstSize,
                                     &consumed);

    *pcSrcSize = consumed;
    *pcDstSize = written;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringReset(void)
{
    return S_OK; /* 转换器无状态 */
}

/* -------------------------------------------------------- IMultiLanguage2 */

/* MIME(RFC 1522/2047) 的流式解码要一张 charset → 解码器表，本文件没有，
 * swinx 里也没有可验证的 IStream 实现。与其"看起来成功、内容却没解"，不如
 * 让调用方立刻知道这条路不通。 */
HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringInIStream(DWORD *pdwMode, DWORD dwFlag, WCHAR *lpFallBack,
                                                                 IStream *pstmIn, IStream *pstmOut)
{
    (void)pdwMode;
    (void)dwFlag;
    (void)lpFallBack;
    (void)pstmIn;
    (void)pstmOut;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringToUnicodeEx(DWORD *pdwMode, DWORD dwEncoding, CHAR *pSrcStr,
                                                                  UINT *pcSrcSize, WCHAR *pDstStr,
                                                                  UINT *pcDstSize, DWORD dwFlag, WCHAR *lpFallBack)
{
    (void)pdwMode;
    if (!pcSrcSize || !pcDstSize)
        return E_POINTER;
    if ((*pcSrcSize > 0 && !pSrcStr) || (*pcDstSize > 0 && !pDstStr))
        return E_POINTER;

    UINT src = (UINT)dwEncoding;

    /* AUTODETECT：用 DetectInputCodepage 的同一套判据，取第一个我们能转换的
     * 候选；检测不出或全是不可转换的码页时仍按调用方给的编码走。 */
    if ((dwFlag & MLCONVCHARF_AUTODETECT) && *pcSrcSize > 0)
    {
        std::vector<DetectCandidate> found =
            DetectEncodings(MLDETECTCP_NONE, CP_ACP, (const unsigned char *)pSrcStr, *pcSrcSize);
        for (size_t i = 0; i < found.size(); ++i)
        {
            if (ConvertibleEncoding(found[i].cp))
            {
                src = found[i].cp;
                break;
            }
        }
    }

    bool utf8 = src == CP_UTF8 || src == 65001;
    bool latin1 = src == 1252 || src == 28591 || src == 20127 || src == CP_ACP;
    if (!utf8 && !latin1)
        return E_FAIL;

    UINT consumed = 0;
    UINT written = DecodeToUnicode(utf8, (const unsigned char *)pSrcStr, *pcSrcSize, pDstStr, *pcDstSize,
                                   &consumed, ResolveFallback(dwFlag, lpFallBack));

    *pcSrcSize = consumed;
    *pcDstSize = written;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::ConvertStringFromUnicodeEx(DWORD *pdwMode, DWORD dwEncoding,
                                                                    WCHAR *pSrcStr, UINT *pcSrcSize, CHAR *pDstStr,
                                                                    UINT *pcDstSize, DWORD dwFlag,
                                                                    WCHAR *lpFallBack)
{
    (void)pdwMode;
    if (!pcSrcSize || !pcDstSize)
        return E_POINTER;
    if ((*pcSrcSize > 0 && !pSrcStr) || (*pcDstSize > 0 && !pDstStr))
        return E_POINTER;

    UINT dst = (UINT)dwEncoding;
    bool utf8 = dst == CP_UTF8 || dst == 65001;
    bool latin1 = dst == 1252 || dst == 28591 || dst == 20127 || dst == CP_ACP;
    if (!utf8 && !latin1)
        return E_FAIL;

    UINT consumed = 0;
    UINT written = EncodeFromUnicode(utf8, pSrcStr, *pcSrcSize, (unsigned char *)pDstStr, *pcDstSize, &consumed,
                                     ResolveFallback(dwFlag, lpFallBack));

    *pcSrcSize = consumed;
    *pcDstSize = written;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::DetectInputCodepage(DWORD dwFlag, DWORD dwPrefWinCodePage, CHAR *pSrcStr,
                                                              INT *pcSrcSize, DetectEncodingInfo *lpEncoding,
                                                              INT *pnNumEncoding)
{
    if (!pcSrcSize || !pnNumEncoding)
        return E_POINTER;
    if (*pcSrcSize < 0)
        return E_INVALIDARG;
    if (*pcSrcSize > 0 && !pSrcStr)
        return E_POINTER;

    std::vector<DetectCandidate> found =
        DetectEncodings(dwFlag, dwPrefWinCodePage, (const unsigned char *)pSrcStr, (size_t)*pcSrcSize);

    size_t total = found.size();
    size_t written = 0;
    INT capacity = *pnNumEncoding;

    if (lpEncoding)
    {
        if (capacity > 0)
        {
            written = total < (size_t)capacity ? total : (size_t)capacity;
            for (size_t i = 0; i < written; ++i)
            {
                lpEncoding[i].nLangID = LanguageIdOfCodePage(found[i].cp);
                lpEncoding[i].nCodePage = found[i].cp;
                lpEncoding[i].nDocPercent = found[i].percent;
                lpEncoding[i].nConfidence = found[i].confidence;
            }
        }
    }
    else
    {
        written = total; /* 没有输出缓冲：只报候选总数 */
    }

    *pnNumEncoding = (INT)written;
    return written > 0 ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::DetectCodepageInIStream(DWORD dwFlag, DWORD dwPrefWinCodePage,
                                                                 IStream *pstmIn, DetectEncodingInfo *lpEncoding,
                                                                 ULONG *pnNumEncoding)
{
    if (!pstmIn || !pnNumEncoding)
        return E_POINTER;

    /* 检测是只读操作：读完把位置还原，不能把调用方的流游标留在一段之后。 */
    ULARGE_INTEGER saved;
    saved.QuadPart = 0;
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    HRESULT hr = pstmIn->Seek(zero, STREAM_SEEK_CUR, &saved);
    if (FAILED(hr))
        return hr;

    unsigned char buffer[4096];
    ULONG got = 0;
    hr = pstmIn->Read(buffer, (ULONG)sizeof(buffer), &got);
    if (FAILED(hr))
        return hr;

    LARGE_INTEGER back;
    back.QuadPart = (LONGLONG)saved.QuadPart;
    pstmIn->Seek(back, STREAM_SEEK_SET, NULL);

    std::vector<DetectCandidate> found = DetectEncodings(dwFlag, dwPrefWinCodePage, buffer, (size_t)got);

    ULONG total = (ULONG)found.size();
    ULONG written = 0;
    ULONG capacity = *pnNumEncoding;

    if (lpEncoding)
    {
        if (capacity > 0)
        {
            written = total < capacity ? total : capacity;
            for (ULONG i = 0; i < written; ++i)
            {
                lpEncoding[i].nLangID = LanguageIdOfCodePage(found[i].cp);
                lpEncoding[i].nCodePage = found[i].cp;
                lpEncoding[i].nDocPercent = found[i].percent;
                lpEncoding[i].nConfidence = found[i].confidence;
            }
        }
    }
    else
    {
        written = total;
    }

    *pnNumEncoding = written;
    return written > 0 ? S_OK : S_FALSE;
}

/* POSIX 上没有"码页缺失 → 提示安装"的 UI，因此 hwnd 不参与判断：只回答
 * 这个码页在我们的表里能不能用。 */
HRESULT STDMETHODCALLTYPE CMultiLanguage::ValidateCodePage(UINT uiCodePage, HWND hwnd)
{
    (void)hwnd;
    return CodePageMask(uiCodePage) != 0 ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::GetCodePageDescription(UINT uiCodePage, LPWSTR lpWideCharStr,
                                                                 int cchWideChar)
{
    const WCHAR *desc = CodePageDescription(uiCodePage);
    if (!desc)
        return E_FAIL;
    if (!lpWideCharStr || cchWideChar <= 0)
        return E_INVALIDARG;

    int len = (int)wcslen(desc);
    if (len >= cchWideChar)
        len = cchWideChar - 1;
    memcpy(lpWideCharStr, desc, (size_t)len * sizeof(WCHAR));
    lpWideCharStr[len] = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::IsCodePageInstallable(UINT uiCodePage)
{
    return CodePageMask(uiCodePage) != 0 ? S_OK : S_FALSE;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::SetMimeDecodeOptions(DWORD dwMimeDecodeOptions)
{
    m_mimeDecodeOptions = dwMimeDecodeOptions;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CMultiLanguage::GetMimeDecodeOptions(DWORD *pdwMimeDecodeOptions)
{
    if (!pdwMimeDecodeOptions)
        return E_POINTER;
    *pdwMimeDecodeOptions = m_mimeDecodeOptions;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::GetCharCodePages(WCHAR chSrc, DWORD *pdwCodePages)
{
    if (!pdwCodePages)
        return E_POINTER;
    *pdwCodePages = CharCodePages((FcChar32)chSrc);
    return S_OK;
}

/* 语义：从 pszSrc 开始，能"共用同一组码页"的最长前缀。
 * 停止条件是两个字符的码页集交集为空（例如拉丁字符后面跟着泰文）。
 * 至少消耗 1 个字符 —— 调用方（SimpleFontData::containsCharacters）用
 * 返回值推进 offset，返回 0 会让它死循环。 */
HRESULT STDMETHODCALLTYPE CLangFontLink2::GetStrCodePages(const WCHAR *pszSrc, long cchSrc,
                                                          DWORD dwPriorityCodePages,
                                                          DWORD *pdwCodePages, long *pcchCodePages)
{
    if (!pszSrc || cchSrc <= 0)
        return E_INVALIDARG;

    LONG used = CodePagesOfRun(pszSrc, cchSrc, dwPriorityCodePages, pdwCodePages);
    if (pcchCodePages)
        *pcchCodePages = used;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::CodePageToCodePages(UINT uCodePage, DWORD *pdwCodePages)
{
    if (!pdwCodePages)
        return E_POINTER;
    *pdwCodePages = CodePageMask(uCodePage);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::CodePagesToCodePage(DWORD dwCodePages, UINT uDefaultCodePage,
                                                              UINT *puCodePage)
{
    if (!puCodePage)
        return E_POINTER;

    for (size_t i = 0; i < sizeof(kRepCodePages) / sizeof(kRepCodePages[0]); ++i)
    {
        if (dwCodePages & kRepCodePages[i].bit)
        {
            *puCodePage = kRepCodePages[i].cp;
            return S_OK;
        }
    }

    *puCodePage = uDefaultCodePage;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::GetFontCodePages(HDC hDC, HFONT hFont, DWORD *pdwCodePages)
{
    if (!pdwCodePages)
        return E_POINTER;

    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));

    HFONT target = hFont;
    if (!target && hDC)
    {
        HGDIOBJ current = GetCurrentObject(hDC, OBJ_FONT);
        target = (HFONT)current;
    }
    if (!target || GetObjectA(target, sizeof(lf), &lf) != sizeof(lf))
    {
        *pdwCodePages = 0;
        return S_OK; /* 拿不到字体就报"什么都不覆盖"，调用方自然会走回退 */
    }

    std::lock_guard<std::mutex> lock(FontMutex());
    *pdwCodePages = FontCodePagesOfFamily(lf.lfFaceName);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::ReleaseFont(HFONT hFont)
{
    if (!hFont)
        return E_INVALIDARG;

    std::lock_guard<std::mutex> lock(FontMutex());
    std::map<HFONT, bool>::iterator it = MappedFonts().find(hFont);
    if (it == MappedFonts().end())
        return E_FAIL; /* 不是我们映射出去的字体：不碰它，免得替调用方 free */

    MappedFonts().erase(it);
    DeleteObject(hFont);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::ResetFontMapping(void)
{
    /* 真实 mlang 的语义是"清掉映射缓存"。已交到调用方手里的 HFONT 不在这里
     * 回收 —— 那些要等 ReleaseFont，否则调用方会持有一个已失效的句柄。 */
    std::lock_guard<std::mutex> lock(FontMutex());
    MappedFonts().clear();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::MapFont(HDC hDC, DWORD dwCodePages, WCHAR chSrc, HFONT *pFont)
{
    if (!pFont)
        return E_POINTER;
    *pFont = NULL;

    if (!hDC)
        return E_INVALIDARG;

    LOGFONTA base;
    memset(&base, 0, sizeof(base));
    HGDIOBJ current = GetCurrentObject(hDC, OBJ_FONT);
    if (current)
        GetObjectA(current, sizeof(base), &base);

    WCHAR probe = chSrc ? chSrc : RepresentativeChar(dwCodePages);
    if (!probe)
        return S_OK;

    std::string family;
    {
        std::lock_guard<std::mutex> lock(FontMutex());
        family = FindCoveringFamily((FcChar32)probe, base.lfFaceName);
    }
    if (family.empty())
        return S_OK; /* 没有别的字体能画：不是错误，交回 NULL 让调用方走别的路 */

    LOGFONTA lf = base;
    strncpy(lf.lfFaceName, family.c_str(), LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;

    HFONT font = CreateFontIndirectA(&lf);
    if (!font)
        return S_OK;

    {
        std::lock_guard<std::mutex> lock(FontMutex());
        MappedFonts()[font] = true;
    }

    *pFont = font;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::GetFontUnicodeRanges(HDC hDC, UINT *puiRanges,
                                                               UNICODERANGE *pUranges)
{
    if (!puiRanges)
        return E_POINTER;
    if (!hDC)
        return E_INVALIDARG;

    /* 复用 GDI 侧的 GetFontUnicodeRanges（fontapi.cpp）：它已经按 DC 选中的
     * LOGFONTA 用 FreeType 抠出了 cmap 的区间表，没必要在这里再算一遍。 */
    DWORD bytes = ::GetFontUnicodeRanges(hDC, NULL);
    if (bytes == 0 || bytes == GDI_ERROR || bytes < sizeof(GLYPHSET))
    {
        *puiRanges = 0;
        return S_OK;
    }

    std::vector<BYTE> buffer(bytes);
    if (::GetFontUnicodeRanges(hDC, (LPGLYPHSET)&buffer[0]) == 0)
    {
        *puiRanges = 0;
        return S_OK;
    }

    const GLYPHSET *set = (const GLYPHSET *)&buffer[0];
    UINT count = set->cRanges;
    UINT capacity = *puiRanges;
    *puiRanges = count;

    if (!pUranges)
        return S_OK;

    UINT n = count < capacity ? count : capacity;
    for (UINT i = 0; i < n; ++i)
    {
        pUranges[i].wcFrom = (WCHAR)set->ranges[i].wcLow;
        pUranges[i].wcTo = (WCHAR)(set->ranges[i].wcLow + set->ranges[i].cGlyphs - 1);
    }
    return S_OK;
}

/* 脚本（OpenType script tag）维度的 API 需要一份 script→字体 的数据库，
 * swinx 没有。明确报"不支持"，让调用方落到别的分支上。 */
HRESULT STDMETHODCALLTYPE CLangFontLink2::GetScriptFontInfo(SCRIPT_ID sid, DWORD dwFlags, UINT *puiFonts,
                                                            LPSCRIPTFONTINFO pScriptFont)
{
    (void)sid;
    (void)dwFlags;
    (void)puiFonts;
    (void)pScriptFont;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE CLangFontLink2::CodePageToScriptID(UINT uiCodePage, SCRIPT_ID *pSid)
{
    (void)uiCodePage;
    (void)pSid;
    return E_NOTIMPL;
}

} // namespace

/* 供 src/objbase.cpp 的 CoCreateInstance 使用。定义在这里而不是共享层，
 * 是因为它依赖 fontconfig；共享层用弱符号引用，没有这个实现的平台
 * （FreeRTOS）自动退化成 E_NOTIMPL。 */
extern "C" HRESULT CreateMLangInstance(REFIID riid, void **ppv)
{
    if (!ppv)
        return E_POINTER;

    CMultiLanguage *instance = new CMultiLanguage();
    HRESULT hr = instance->QueryInterface(riid, ppv);
    instance->Release();
    return hr;
}
