#include <windows.h>
#undef interface
#include <gdi.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include "sdc.h"
#include "log.h"
#define kLogTag "fontapi"

/*
 * apple（macos/ios）GDI 后端的「按字形工作」字体 API。
 *
 * WebKit 的 Windows 字体代码（FontCacheWin / GlyphPageTreeNodeWin /
 * SimpleFontData*Win / FontPlatformDataWin）需要一组按「字形」维度工作的 GDI
 * 入口：按 glyph index 取宽度、取字形度量、取 OUTLINETEXTMETRIC、取字体覆盖的
 * Unicode 区间、按家族枚举字体。
 *
 * apple 的绘制走 Core Graphics / CoreText（见 gdi.cpp），不经过 fontconfig，
 * 因此这里不能像 cairo 后端那样用 fontconfig 选字体、用 FreeType 取字形，否则
 * 「取到的字形索引」和「实际绘制用的字形」可能来自不同物理字体（伪造的 Windows
 * 字体名、CJK 集合字体尤其明显），画出来就是错位字形。
 *
 * 因此本文件全部用 CoreText 实现，并且和 gdi.cpp 的绘制路径共用同一个
 * SwinxCreateCTFontFromDC()：字形索引 / 度量 / 绘制三者天然一致。
 *
 * 字符串约定与 swinx 其它 API 一致：
 *   - W 后缀：wchar_t 字符串（本工程内为 UTF-32，见 WCHAR_SIZE）；
 *   - A 后缀：UTF-8 字符串。
 */

/* ------------------------------------------------------------------ 基础设施 */

static std::string CfStringToUtf8(CFStringRef s)
{
    if (!s)
        return std::string();
    CFIndex len = CFStringGetLength(s);
    if (len <= 0)
        return std::string();
    CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::vector<char> buf((size_t)max);
    if (!CFStringGetCString(s, &buf[0], max, kCFStringEncodingUTF8))
        return std::string();
    return std::string(&buf[0]);
}

static CFStringRef Utf8ToCfString(const char *s)
{
    if (!s || !s[0])
        return NULL;
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

static bool Utf8ToWide(const char *src, wchar_t *dst, int cchDst)
{
    if (!src || !dst || cchDst <= 0)
        return false;
    dst[0] = 0;
    if (!src[0])
        return true;
    return MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cchDst) > 0;
}

static std::wstring Utf8ToWideStr(const std::string &src)
{
    std::wstring ret;
    if (src.empty())
        return ret;
    int n = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), (int)src.size(), nullptr, 0);
    if (n <= 0)
        return ret;
    std::vector<wchar_t> buf((size_t)n + 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.c_str(), (int)src.size(), &buf[0], n);
    ret.assign(&buf[0], (size_t)n);
    return ret;
}

static std::string WideToUtf8Str(const wchar_t *src)
{
    std::string ret;
    if (!src || !src[0])
        return ret;
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return ret;
    std::vector<char> buf((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, src, -1, &buf[0], n, nullptr, nullptr);
    ret.assign(&buf[0]);
    return ret;
}

/* CTFont 是创建字形索引、度量与真正绘制共用的对象：三者必须是同一个字体，
 * 否则 WebKit 缓存的字形索引会在绘制时落到别的字体上。调用方负责 CFRelease
 * 返回值。 */
static CTFontRef CreateCTFontForLogFont(const LOGFONTA *lf)
{
    const char *fontName = (lf && lf->lfFaceName[0]) ? lf->lfFaceName : "Helvetica";
    CGFloat fontSize = (lf && abs(lf->lfHeight) > 0) ? (CGFloat)abs(lf->lfHeight) : 12.0;
    CFStringRef cfFontName = Utf8ToCfString(fontName);
    if (!cfFontName)
        cfFontName = CFStringCreateWithCString(kCFAllocatorDefault, "Helvetica", kCFStringEncodingUTF8);
    if (!cfFontName)
        return NULL;

    CTFontSymbolicTraits traits = 0;
    if (lf && lf->lfWeight >= FW_BOLD)
        traits |= kCTFontBoldTrait;
    if (lf && lf->lfItalic)
        traits |= kCTFontItalicTrait;

    CTFontRef ctFont = NULL;
    if (traits != 0)
    {
        CTFontRef baseFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
        if (baseFont)
        {
            ctFont = CTFontCreateCopyWithSymbolicTraits(baseFont, fontSize, NULL, traits, traits);
            CFRelease(baseFont);
        }
    }
    if (!ctFont)
        ctFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
    CFRelease(cfFontName);
    return ctFont;
}

/* HDC 当前选中的 LOGFONTA → CTFont。gdi.cpp 的绘制路径也调用本函数，
 * 保证「取字形」与「画字形」用同一个 CTFont。返回值为调用方持有的引用。 */
static std::mutex s_fontMutex;
static std::map<std::string, CTFontRef> s_fontCache;

CTFontRef SwinxCreateCTFontFromDC(HDC hdc)
{
    if (!hdc || !hdc->hfont)
        return NULL;
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    if (!lf)
        return NULL;

    char key[256];
    snprintf(key, sizeof(key), "%s#%d#%d#%d", lf->lfFaceName, (int)lf->lfWeight,
             lf->lfItalic ? 1 : 0, (int)abs(lf->lfHeight));

    std::lock_guard<std::mutex> lock(s_fontMutex);
    std::map<std::string, CTFontRef>::iterator it = s_fontCache.find(key);
    if (it != s_fontCache.end())
        return (CTFontRef)CFRetain(it->second);

    CTFontRef ctFont = CreateCTFontForLogFont(lf);
    if (!ctFont)
        return NULL;
    if (s_fontCache.size() > 64)
    {
        for (std::map<std::string, CTFontRef>::iterator i = s_fontCache.begin(); i != s_fontCache.end(); ++i)
            CFRelease(i->second);
        s_fontCache.clear();
    }
    s_fontCache[key] = ctFont;
    return (CTFontRef)CFRetain(ctFont);
}

/* 一个 UTF-32 码点 → CGGlyph（补充平面用代理对查询）。未命中返回 0。 */
static CGGlyph GlyphForCodePoint(CTFontRef font, uint32_t cp)
{
    if (!font || cp > 0x10FFFF)
        return 0;
    UniChar chars[2];
    CFIndex n = 1;
    if (cp <= 0xFFFF)
    {
        chars[0] = (UniChar)cp;
    }
    else
    {
        uint32_t v = cp - 0x10000;
        chars[0] = (UniChar)(0xD800 + (v >> 10));
        chars[1] = (UniChar)(0xDC00 + (v & 0x3FF));
        n = 2;
    }
    CGGlyph glyphs[2] = { 0, 0 };
    CTFontGetGlyphsForCharacters(font, chars, glyphs, n);
    return glyphs[0];
}

/* 批量把 UTF-32（wchar_t）串转成字形序列，并可选地给出每个字符的像素推进量。
 * 一次 CoreText 调用处理整串，避免逐字符跨越 CF 边界。 */
static void GetGlyphsForWide(CTFontRef font, const wchar_t *str, int count,
                             std::vector<CGGlyph> &glyphs, std::vector<CGSize> *advances)
{
    glyphs.assign((size_t)count, 0);
    if (!font || count <= 0)
    {
        if (advances)
            advances->assign((size_t)(count > 0 ? count : 0), CGSizeMake(0, 0));
        return;
    }

    std::vector<UniChar> u16;
    std::vector<int> firstUnit((size_t)count, -1);
    u16.reserve((size_t)count);
    for (int i = 0; i < count; i++)
    {
        uint32_t cp = (uint32_t)str[i];
        if (cp > 0x10FFFF)
            continue;
        firstUnit[i] = (int)u16.size();
        if (cp <= 0xFFFF)
            u16.push_back((UniChar)cp);
        else
        {
            uint32_t v = cp - 0x10000;
            u16.push_back((UniChar)(0xD800 + (v >> 10)));
            u16.push_back((UniChar)(0xDC00 + (v & 0x3FF)));
        }
    }

    if (!u16.empty())
    {
        std::vector<CGGlyph> cg(u16.size(), 0);
        CTFontGetGlyphsForCharacters(font, &u16[0], &cg[0], (CFIndex)u16.size());
        for (int i = 0; i < count; i++)
            if (firstUnit[i] >= 0)
                glyphs[i] = cg[(size_t)firstUnit[i]];
    }

    if (advances)
    {
        advances->assign((size_t)count, CGSizeMake(0, 0));
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyphs[0], &(*advances)[0], (CFIndex)count);
    }
}

/* ------------------------------------------------------------- 字形索引/宽度 */

DWORD WINAPI GetGlyphIndicesW(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl)
{
    if (!hdc || !lpstr || !pgi || c < 0)
        return GDI_ERROR;
    if (c == 0)
        return 0;

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return GDI_ERROR;

    std::vector<CGGlyph> glyphs;
    GetGlyphsForWide(font, lpstr, c, glyphs, NULL);
    const bool bMarkMissing = (fl & GGI_MARK_NONEXISTING_GLYPHS) != 0;
    for (int i = 0; i < c; i++)
    {
        WORD gi = (WORD)glyphs[i];
        // CoreText 对未覆盖的字符返回 0；Win32 在 GGI_MARK_NONEXISTING_GLYPHS
        // 下用 0xFFFF 标记缺字。
        if (gi == 0 && bMarkMissing)
            gi = 0xFFFF;
        pgi[i] = gi;
    }
    CFRelease(font);
    return (DWORD)c;
}

DWORD WINAPI GetGlyphIndicesA(HDC hdc, LPCSTR lpstr, int c, LPWORD pgi, DWORD fl)
{
    if (!hdc || !lpstr || !pgi || c < 0)
        return GDI_ERROR;
    if (c == 0)
        return 0;
    // swinx 的 A 系 API 按 UTF-8 解释字符串：按「字符」而不是「字节」出结果
    std::wstring wide = Utf8ToWideStr(std::string(lpstr, (size_t)c));
    if (wide.empty())
        return GDI_ERROR;
    return GetGlyphIndicesW(hdc, wide.c_str(), (int)wide.size(), pgi, fl);
}

/* Windows GDI 在 GM_ADVANCED 模式下会把 DC 的世界变换应用到 GetGlyphOutline
 * 的返回值上（返回设备空间度量）。CoreText 给出的是字形空间度量，这里乘上
 * CGAffineTransform 的线性部分，与 Windows 的语义一致。 */
static void ApplyWorldTransformToGlyphMetrics(HDC hdc, GLYPHMETRICS *gm)
{
    if (!hdc->worldMtx)
        return;
    const CGAffineTransform &m = *hdc->worldMtx;
    if (m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1)
        return;

    // 黑盒用基线上方左上角 + 下方右下角表示（y 轴向上），变换后重新取包围盒。
    double left = (double)gm->gmptGlyphOrigin.x;
    double top = (double)gm->gmptGlyphOrigin.y;
    double right = left + (double)gm->gmBlackBoxX;
    double bottom = top - (double)gm->gmBlackBoxY;

    double xs[4], ys[4];
    // (x', y') = (a*x + c*y, b*x + d*y)
    xs[0] = m.a * left + m.c * top;    ys[0] = m.b * left + m.d * top;
    xs[1] = m.a * right + m.c * top;   ys[1] = m.b * right + m.d * top;
    xs[2] = m.a * left + m.c * bottom; ys[2] = m.b * left + m.d * bottom;
    xs[3] = m.a * right + m.c * bottom; ys[3] = m.b * right + m.d * bottom;

    double minX = xs[0], maxX = xs[0], minY = ys[0], maxY = ys[0];
    for (int i = 1; i < 4; ++i)
    {
        if (xs[i] < minX) minX = xs[i];
        if (xs[i] > maxX) maxX = xs[i];
        if (ys[i] < minY) minY = ys[i];
        if (ys[i] > maxY) maxY = ys[i];
    }

    gm->gmptGlyphOrigin.x = (LONG)floor(minX);
    gm->gmptGlyphOrigin.y = (LONG)ceil(maxY);
    gm->gmBlackBoxX = (UINT)ceil(maxX - minX);
    gm->gmBlackBoxY = (UINT)ceil(maxY - minY);

    // 步进量按方向变换（不含平移），与 GDI 的 gmCellIncX 语义一致。
    double adv = (double)gm->gmCellIncX;
    gm->gmCellIncX = (short)ceil(m.a * adv);
}

DWORD WINAPI GetGlyphOutlineW(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, CONST MAT2 *lpmat2)
{
    (void)cjBuffer;
    (void)pvBuffer;
    (void)lpmat2;
    if (!hdc || !lpgm)
        return GDI_ERROR;
    if ((fuFormat & ~(UINT)(GGO_GLYPH_INDEX | GGO_UNHINTED)) != GGO_METRICS)
        return GDI_ERROR; // 只实现 GGO_METRICS（wkex 只用它）

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return GDI_ERROR;

    CGGlyph gi = (fuFormat & GGO_GLYPH_INDEX) ? (CGGlyph)uChar : GlyphForCodePoint(font, (uint32_t)uChar);

    // CoreText 的 bounding rect 以基线原点给出（y 轴向上），origin 是左下角；
    // GDI 的 gmptGlyphOrigin 是黑盒左上角，所以纵坐标取 rect 的上边。
    CGRect bbox = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &gi, NULL, 1);
    CGSize adv = CGSizeMake(0, 0);
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &gi, &adv, 1);
    CFRelease(font);

    memset(lpgm, 0, sizeof(GLYPHMETRICS));
    lpgm->gmBlackBoxX = (UINT)ceil(bbox.size.width);
    lpgm->gmBlackBoxY = (UINT)ceil(bbox.size.height);
    lpgm->gmptGlyphOrigin.x = (LONG)floor(bbox.origin.x);
    lpgm->gmptGlyphOrigin.y = (LONG)floor(bbox.origin.y + bbox.size.height);
    lpgm->gmCellIncX = (short)ceil(adv.width);
    lpgm->gmCellIncY = 0;
    ApplyWorldTransformToGlyphMetrics(hdc, lpgm);
    return sizeof(GLYPHMETRICS);
}

DWORD WINAPI GetGlyphOutlineA(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, CONST MAT2 *lpmat2)
{
    if (fuFormat & GGO_GLYPH_INDEX)
        return GetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
    // A 版本把 uChar 当 Unicode 码点处理（与 cairo 后端一致）
    if (uChar > 0x10FFFF)
        return GDI_ERROR;
    return GetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
}

BOOL WINAPI GetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT lpBuffer)
{
    if (!hdc || !lpBuffer)
        return FALSE;

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return FALSE;

    if (cgi > 0)
    {
        std::vector<CGGlyph> glyphs((size_t)cgi);
        for (UINT i = 0; i < cgi; i++)
            glyphs[i] = (CGGlyph)(pgi ? pgi[i] : (giFirst + i));
        std::vector<CGSize> adv((size_t)cgi, CGSizeMake(0, 0));
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyphs[0], &adv[0], (CFIndex)cgi);
        for (UINT i = 0; i < cgi; i++)
            lpBuffer[i] = (int)ceil(adv[i].width);
    }
    CFRelease(font);
    return TRUE;
}

BOOL WINAPI GetCharWidth32A(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    return GetCharWidthA(hdc, iFirst, iLast, lpBuffer);
}

BOOL WINAPI GetCharWidth32W(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    return GetCharWidthW(hdc, iFirst, iLast, lpBuffer);
}

/* ------------------------------------------------------------ 字体表 / 排版 */

DWORD WINAPI GetFontData(HDC hdc, DWORD dwTable, DWORD dwOffset, LPVOID pvBuffer, DWORD cbData)
{
    if (!hdc || dwTable == 0)
        return GDI_ERROR; // 不支持「整个字体文件」的读取（Type1 判定因此恒为 false）

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return GDI_ERROR;
    // GDI 的 dwTable 是 MAKEFOURCC 口径 —— 首字符在最低字节（WebKit 传 'eman'
    // 表示 "name"），而 CoreText 的 CTFontTableTag 是 FourCharCode 口径 ——
    // 首字符在最高字节（kCTFontTableHead == 'head' == 0x68656164）。两者字节序
    // 相反，先翻转再查；调用方若已按 FourCharCode 传值，再用原值查一次。
    DWORD gdiTag = dwTable;
    CTFontTableTag tag = (CTFontTableTag)(((gdiTag & 0xFFu) << 24) | ((gdiTag & 0xFF00u) << 8) |
                                          ((gdiTag >> 8) & 0xFF00u) | (gdiTag >> 24));
    CFDataRef table = CTFontCopyTable(font, tag, kCTFontTableOptionNoOptions);
    if (!table && tag != (CTFontTableTag)gdiTag)
        table = CTFontCopyTable(font, (CTFontTableTag)gdiTag, kCTFontTableOptionNoOptions);
    CFRelease(font);
    if (!table)
        return GDI_ERROR;

    CFIndex len = CFDataGetLength(table);
    if (len <= 0 || (CFIndex)dwOffset >= len)
    {
        CFRelease(table);
        return GDI_ERROR;
    }
    DWORD avail = (DWORD)(len - (CFIndex)dwOffset);
    if (!pvBuffer)
    {
        CFRelease(table);
        return avail;
    }
    DWORD cb = cbData < avail ? cbData : avail;
    memcpy(pvBuffer, CFDataGetBytePtr(table) + dwOffset, cb);
    CFRelease(table);
    return cb;
}

DWORD WINAPI GetCharacterPlacementW(HDC hdc, LPCWSTR lpString, int nCount, int nMaxExtent,
                                    LPGCP_RESULTSW lpResults, DWORD dwFlags)
{
    (void)dwFlags;
    if (!hdc || !lpString || !lpResults || nCount <= 0)
        return 0;
    if (lpResults->lStructSize != sizeof(GCP_RESULTSW))
        return 0;

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return 0;

    std::vector<CGGlyph> allGlyphs;
    std::vector<CGSize> allAdv;
    GetGlyphsForWide(font, lpString, nCount, allGlyphs, &allAdv);
    CFRelease(font);

    // 先算出完整的「glyph + 步进」序列，再按调用方的缓冲区大小决定写多少。
    std::vector<WORD> glyphs((size_t)nCount);
    std::vector<int> dx((size_t)nCount);
    int totalWidth = 0;
    UINT fitted = 0;
    for (int i = 0; i < nCount; i++)
    {
        WORD gi = (WORD)allGlyphs[i];
        int width = (int)ceil(allAdv[i].width);
        if (nMaxExtent > 0 && totalWidth + width > nMaxExtent)
            break;
        glyphs[fitted] = gi;
        dx[fitted] = width;
        totalWidth += width;
        fitted++;
    }

    lpResults->nMaxFit = (int)fitted;
    if (lpResults->nGlyphs < fitted)
    {
        // 缓冲区不够：按 Win32 的约定把 lpGlyphs 置 NULL，调用方据此换更大的缓冲区重试。
        lpResults->nGlyphs = fitted;
        lpResults->lpGlyphs = NULL;
        return (DWORD)((totalWidth & 0xFFFF) | ((fitted & 0xFFFF) << 16));
    }

    if (lpResults->lpDx && fitted)
        memcpy(lpResults->lpDx, &dx[0], fitted * sizeof(int));
    if (lpResults->lpGlyphs)
    {
        // lpGlyphs 是 WCHAR 数组：按 WCHAR 宽度逐个写（macOS 上 wchar_t 是 4 字节）。
        for (UINT i = 0; i < fitted; i++)
            lpResults->lpGlyphs[i] = (WCHAR)glyphs[i];
    }
    lpResults->nGlyphs = fitted;
    return (DWORD)((totalWidth & 0xFFFF) | ((fitted & 0xFFFF) << 16));
}

DWORD WINAPI GetCharacterPlacementA(HDC hdc, LPCSTR lpString, int nCount, int nMaxExtent,
                                    LPGCP_RESULTSA lpResults, DWORD dwFlags)
{
    if (!hdc || !lpString || !lpResults || nCount <= 0)
        return 0;
    if (lpResults->lStructSize != sizeof(GCP_RESULTSA))
        return 0;

    std::wstring wide = Utf8ToWideStr(std::string(lpString, (size_t)nCount));
    if (wide.empty())
        return 0;

    std::vector<int> dx(lpResults->nGlyphs ? lpResults->nGlyphs : 1);
    std::vector<WCHAR> glyphs(lpResults->nGlyphs ? lpResults->nGlyphs : 1);
    GCP_RESULTSW tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.lStructSize = sizeof(tmp);
    tmp.nGlyphs = lpResults->nGlyphs;
    tmp.lpDx = &dx[0];
    tmp.lpGlyphs = &glyphs[0];

    DWORD ret = GetCharacterPlacementW(hdc, wide.c_str(), (int)wide.size(), nMaxExtent, &tmp, dwFlags);
    lpResults->nGlyphs = tmp.nGlyphs;
    lpResults->nMaxFit = tmp.nMaxFit;
    if (!tmp.lpGlyphs)
        return ret; // 缓冲区不够，交给调用方重试
    if (lpResults->lpDx && !dx.empty())
        memcpy(lpResults->lpDx, &dx[0], tmp.nGlyphs * sizeof(int));
    if (lpResults->lpGlyphs)
    {
        // GCP_RESULTSA 的 lpGlyphs 是字节数组（字体 glyph 数 <= 256 时的 Win32 语义）。
        for (UINT i = 0; i < tmp.nGlyphs; i++)
            lpResults->lpGlyphs[i] = (char)(glyphs[i] & 0xFF);
    }
    return ret;
}

/* ------------------------------------------------------ Unicode 覆盖区间 */

DWORD WINAPI GetFontUnicodeRanges(HDC hdc, LPGLYPHSET lpgs)
{
    if (!hdc)
        return 0;

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
        return 0;

    // CoreText 的字符集就是「这个字体能直接映射到的 Unicode 码点」，比遍历
    // 字体的 cmap 更贴近实际绘制结果。GLYPHSET 按 UTF-16 组织，只统计 BMP。
    CFCharacterSetRef charset = CTFontCopyCharacterSet(font);
    CFDataRef bitmap = charset ? CFCharacterSetCreateBitmapRepresentation(kCFAllocatorDefault, charset) : NULL;
    if (charset)
        CFRelease(charset);
    if (!bitmap)
    {
        CFRelease(font);
        return 0;
    }

    const UInt8 *bits = CFDataGetBytePtr(bitmap);
    CFIndex nbytes = CFDataGetLength(bitmap);
    std::vector<WCRANGE> ranges;
    UINT runLow = 0;
    UINT runCount = 0;
    for (UINT ch = 0; ch <= 0xFFFF; ch++)
    {
        bool present = (bits && (CFIndex)(ch >> 3) < nbytes) && (bits[ch >> 3] & (UInt8)(1u << (ch & 7)));
        if (present)
        {
            if (runCount && ch == runLow + runCount)
                runCount++;
            else
            {
                if (runCount)
                {
                    WCRANGE r = { (WCHAR)runLow, (USHORT)(runCount > 0xFFFF ? 0xFFFF : runCount) };
                    ranges.push_back(r);
                }
                runLow = ch;
                runCount = 1;
            }
        }
        else if (runCount)
        {
            WCRANGE r = { (WCHAR)runLow, (USHORT)(runCount > 0xFFFF ? 0xFFFF : runCount) };
            ranges.push_back(r);
            runCount = 0;
        }
    }
    if (runCount)
    {
        WCRANGE r = { (WCHAR)runLow, (USHORT)(runCount > 0xFFFF ? 0xFFFF : runCount) };
        ranges.push_back(r);
    }

    DWORD need = (DWORD)(offsetof(GLYPHSET, ranges) + ranges.size() * sizeof(WCRANGE));
    DWORD glyphCount = (DWORD)CTFontGetGlyphCount(font);
    CFRelease(bitmap);
    CFRelease(font);

    if (!lpgs)
        return need;

    lpgs->cbThis = need;
    lpgs->flAccel = 0; // 不保证 glyph index 落在 8 位内
    lpgs->cGlyphsSupported = glyphCount;
    lpgs->cRanges = (DWORD)ranges.size();
    if (!ranges.empty())
        memcpy(lpgs->ranges, &ranges[0], ranges.size() * sizeof(WCRANGE));
    return need;
}

/* ---------------------------------------------------- OUTLINETEXTMETRIC */

struct OutlineInfo
{
    TEXTMETRICW tm;
    UINT emSquare;
    int ascent;
    int descent;
    UINT lineGap;
    UINT capEmHeight;
    UINT xHeight;
    RECT fontBox;
    UINT strikeoutSize;
    int strikeoutPosition;
    UINT underscoreSize;
    int underscorePosition;
    PANOSE panose;
    UINT fsSelection;
    UINT fsType;
    std::string family;
    std::string style;
    std::string face;
    std::string fullName;
};

static UINT16 SfntU16(const UInt8 *p)
{
    return (UINT16)(((UINT16)p[0] << 8) | (UINT16)p[1]);
}

static short SfntS16(const UInt8 *p)
{
    return (short)SfntU16(p);
}

/* head.unitsPerEm（head 表偏移 18，uint16，大端）。 */
static UINT ReadUnitsPerEm(CTFontRef font, UINT fallback)
{
    UINT upem = 0;
    CFDataRef head = CTFontCopyTable(font, kCTFontTableHead, kCTFontTableOptionNoOptions);
    if (head)
    {
        const UInt8 *p = CFDataGetBytePtr(head);
        if (p && CFDataGetLength(head) >= 20)
            upem = SfntU16(p + 18);
        CFRelease(head);
    }
    return upem ? upem : fallback;
}

/* OS/2 表里 Fontconfig/FreeType 原本提供的字段：字重、panose、fsSelection/
 * fsType、删除线、xHeight/capHeight。数值是字体单位，按 size/unitsPerEm 缩放。 */
static void ReadOS2Info(CTFontRef font, UINT upem, OutlineInfo *out)
{
    CFDataRef os2 = CTFontCopyTable(font, kCTFontTableOS2, kCTFontTableOptionNoOptions);
    if (!os2)
        return;
    const UInt8 *p = CFDataGetBytePtr(os2);
    CFIndex len = CFDataGetLength(os2);
    if (p && len >= 68)
    {
        UINT16 version = SfntU16(p + 0);
        UINT16 weightClass = SfntU16(p + 4);
        UINT16 fsType = SfntU16(p + 8);
        short strikeoutSize = SfntS16(p + 26);
        short strikeoutPosition = SfntS16(p + 28);
        UINT16 fsSelection = SfntU16(p + 62);
        CGFloat size = CTFontGetSize(font);
        double scale = upem ? (double)size / (double)upem : 0;

        out->fsSelection = fsSelection;
        out->fsType = fsType;
        memcpy(&out->panose, p + 32, sizeof(PANOSE));
        if (!out->tm.tmWeight)
            out->tm.tmWeight = (LONG)weightClass;
        if (strikeoutSize)
        {
            out->strikeoutSize = (UINT)ceil(strikeoutSize * scale);
            out->strikeoutPosition = (int)floor(strikeoutPosition * scale);
        }
        if (version >= 2 && len >= 90)
        {
            // sxHeight / sCapHeight 是 OS/2 version 2 起才有的字段
            out->xHeight = (UINT)ceil(SfntS16(p + 86) * scale);
            out->capEmHeight = (UINT)ceil(SfntS16(p + 88) * scale);
        }
    }
    CFRelease(os2);
}

static void BuildOutlineInfo(HDC hdc, OutlineInfo *out)
{
    *out = OutlineInfo(); // 值初始化：POD 字段清零、string 字段清空
    LOGFONTA *lf = hdc && hdc->hfont ? (LOGFONTA *)GetGdiObjPtr(hdc->hfont) : nullptr;

    CTFontRef font = SwinxCreateCTFontFromDC(hdc);
    if (!font)
    {
        // 拿不到字体时退化：只给 TEXTMETRIC 与面名
        if (lf)
        {
            out->family = lf->lfFaceName;
            out->face = lf->lfFaceName;
            out->fullName = lf->lfFaceName;
            out->tm.tmItalic = lf->lfItalic;
            out->tm.tmWeight = lf->lfWeight;
            out->tm.tmUnderlined = lf->lfUnderline;
            out->tm.tmStruckOut = lf->lfStrikeOut;
            out->tm.tmCharSet = lf->lfCharSet;
            out->tm.tmPitchAndFamily = (BYTE)((lf->lfPitchAndFamily & 0xF0) | TMPF_VECTOR | TMPF_TRUETYPE);
        }
        out->emSquare = 12;
        out->fsSelection = (lf && lf->lfItalic) ? 1 : 0;
        return;
    }

    CFStringRef cfFamily = CTFontCopyFamilyName(font);
    CFStringRef cfFull = CTFontCopyFullName(font);
    CFStringRef cfStyle = CTFontCopyName(font, kCTFontStyleNameKey);
    out->family = CfStringToUtf8(cfFamily);
    out->fullName = CfStringToUtf8(cfFull);
    out->style = CfStringToUtf8(cfStyle);
    if (cfFamily)
        CFRelease(cfFamily);
    if (cfFull)
        CFRelease(cfFull);
    if (cfStyle)
        CFRelease(cfStyle);
    if (out->family.empty())
        out->family = (lf && lf->lfFaceName[0]) ? lf->lfFaceName : "Helvetica";
    if (out->style.empty())
        out->style = out->family;
    if (out->fullName.empty())
        out->fullName = out->family;
    // GDI 的 otmpFaceName 是「面名」，多数平台与家族名一致（风格另由 otmpStyleName 表达）
    out->face = out->family;

    CGFloat ascent = CTFontGetAscent(font);
    CGFloat descent = CTFontGetDescent(font);
    CGFloat leading = CTFontGetLeading(font);
    out->ascent = (int)ceil(ascent);
    out->descent = (int)ceil(descent);
    out->lineGap = leading > 0 ? (UINT)ceil(leading) : 0;

    UINT fallbackEm = (UINT)(abs(lf ? lf->lfHeight : 0) > 0 ? abs(lf->lfHeight) : 12);
    out->emSquare = ReadUnitsPerEm(font, fallbackEm);

    // fontBox 与 FreeType 版本一致：上边取 yMax、下边取 yMin（y 轴向上）。
    CGRect bbox = CTFontGetBoundingBox(font);
    out->fontBox.left = (LONG)floor(bbox.origin.x);
    out->fontBox.top = (LONG)ceil(bbox.origin.y + bbox.size.height);
    out->fontBox.right = (LONG)ceil(bbox.origin.x + bbox.size.width);
    out->fontBox.bottom = (LONG)floor(bbox.origin.y);

    CGFloat underlineThickness = CTFontGetUnderlineThickness(font);
    if (underlineThickness > 0)
    {
        out->underscoreSize = (UINT)ceil(underlineThickness);
        out->underscorePosition = (int)floor(CTFontGetUnderlinePosition(font));
    }

    out->xHeight = (UINT)ceil(CTFontGetXHeight(font));
    out->capEmHeight = (UINT)ceil(CTFontGetCapHeight(font));
    ReadOS2Info(font, out->emSquare, out);

    if (!out->xHeight)
        out->xHeight = (UINT)(out->ascent * 0.56); // 与 WK 的兜底估值一致

    // TEXTMETRIC：CoreText 直接给出 ascent/descent/leading，不再依赖 gdi.cpp 的
    // GetTextMetrics（后者要求 HDC 已有 CGContext）。
    out->tm.tmAscent = out->ascent;
    out->tm.tmDescent = out->descent;
    out->tm.tmHeight = (LONG)ceil(ascent + descent + leading);
    out->tm.tmExternalLeading = (LONG)leading;
    CGGlyph xGlyph = GlyphForCodePoint(font, 'x');
    CGSize xAdv = CGSizeMake(0, 0);
    CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &xGlyph, &xAdv, 1);
    out->tm.tmAveCharWidth = (LONG)ceil(xAdv.width);
    out->tm.tmDigitizedAspectX = 100;
    out->tm.tmDigitizedAspectY = 100;
    if (lf)
    {
        out->tm.tmItalic = lf->lfItalic;
        out->tm.tmUnderlined = lf->lfUnderline;
        out->tm.tmStruckOut = lf->lfStrikeOut;
        out->tm.tmCharSet = lf->lfCharSet;
        out->tm.tmWeight = lf->lfWeight;
        // TMPF_FIXED_PITCH（bit 0）在真实 GDI 里是反着用的：比例字体返回 1，
        // 等宽字体返回 0（与 gdi.cpp 的 GetTextMetricsA 保持一致）。留 0 会让
        // WebKit 的 determinePitch() 把所有字体判成等宽，行宽塌缩。
        BYTE fixedPitchBit = TMPF_FIXED_PITCH;
        static const unsigned short probe[] = { 'i', 'I', 'l', '1', 'M', 'W', '0', 'm', '@' };
        CGGlyph probeGlyphs[9];
        CGSize probeAdvances[9];
        if (CTFontGetGlyphsForCharacters(font, probe, probeGlyphs, 9))
        {
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, probeGlyphs, probeAdvances, 9);
            CGFloat minAdvance = probeAdvances[0].width;
            CGFloat maxAdvance = probeAdvances[0].width;
            bool allMapped = true;
            for (int i = 0; i < 9; i++)
            {
                if (!probeGlyphs[i])
                    allMapped = false;
                if (probeAdvances[i].width < minAdvance)
                    minAdvance = probeAdvances[i].width;
                if (probeAdvances[i].width > maxAdvance)
                    maxAdvance = probeAdvances[i].width;
            }
            fixedPitchBit = (allMapped && maxAdvance - minAdvance < 0.01) ? 0 : TMPF_FIXED_PITCH;
        }
        out->tm.tmPitchAndFamily = (BYTE)((lf->lfPitchAndFamily & 0xF0) | TMPF_VECTOR | TMPF_TRUETYPE | fixedPitchBit);
        if (lf->lfItalic)
            out->fsSelection |= 1;
        if (lf->lfWeight >= FW_BOLD)
            out->fsSelection |= (1 << 5);
    }
    else if (!out->fsSelection)
    {
        out->fsSelection = 1 << 6; // REGULAR
    }
    CFRelease(font);
}

static UINT OutlineInfoBytes(const OutlineInfo &info, bool bWide)
{
    size_t n = 0;
    if (bWide)
        n = (Utf8ToWideStr(info.family).size() + Utf8ToWideStr(info.face).size() + Utf8ToWideStr(info.style).size() + Utf8ToWideStr(info.fullName).size() + 4) * sizeof(wchar_t);
    else
        n = info.family.size() + info.face.size() + info.style.size() + info.fullName.size() + 4;
    return (UINT)n;
}

UINT WINAPI GetOutlineTextMetricsW(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW potm)
{
    if (!hdc)
        return 0;
    OutlineInfo info;
    BuildOutlineInfo(hdc, &info);

    UINT cchNames = OutlineInfoBytes(info, true);
    UINT need = (UINT)sizeof(OUTLINETEXTMETRICW) + cchNames;
    if (!potm || cjCopy == 0)
        return need;
    if (cjCopy < sizeof(OUTLINETEXTMETRICW))
        return 0;

    memset(potm, 0, sizeof(OUTLINETEXTMETRICW));
    potm->otmSize = sizeof(OUTLINETEXTMETRICW);
    potm->otmTextMetrics = info.tm;
    potm->otmPanoseNumber = info.panose;
    potm->otmfsSelection = info.fsSelection;
    potm->otmfsType = info.fsType;
    potm->otmsCharSlopeRise = 1;
    potm->otmsCharSlopeRun = 0;
    potm->otmItalicAngle = 0;
    potm->otmEMSquare = info.emSquare;
    potm->otmAscent = info.ascent;
    potm->otmDescent = info.descent;
    potm->otmLineGap = info.lineGap;
    potm->otmsCapEmHeight = info.capEmHeight;
    potm->otmsXHeight = info.xHeight;
    potm->otmrcFontBox = info.fontBox;
    potm->otmMacAscent = info.ascent;
    potm->otmMacDescent = info.descent;
    potm->otmMacLineGap = info.lineGap;
    potm->otmusMinimumPPEM = 1;
    potm->otmsStrikeoutSize = info.strikeoutSize;
    potm->otmsStrikeoutPosition = info.strikeoutPosition;
    potm->otmsUnderscoreSize = info.underscoreSize;
    potm->otmsUnderscorePosition = info.underscorePosition;

    // otmp* 是「相对结构体基址」的字节偏移；缓冲区装不下名字时留 0
    if (cjCopy >= need)
    {
        char *base = (char *)potm;
        char *p = base + sizeof(OUTLINETEXTMETRICW);
        const std::string *names[4] = { &info.family, &info.face, &info.style, &info.fullName };
        for (int i = 0; i < 4; i++)
        {
            std::wstring w = Utf8ToWideStr(*names[i]);
            size_t cb = (w.size() + 1) * sizeof(wchar_t);
            memcpy(p, w.c_str(), cb);
            size_t off = (size_t)(p - base);
            switch (i)
            {
            case 0:
                potm->otmpFamilyName = (PWSTR)(uintptr_t)off;
                break;
            case 1:
                potm->otmpFaceName = (PWSTR)(uintptr_t)off;
                break;
            case 2:
                potm->otmpStyleName = (PWSTR)(uintptr_t)off;
                break;
            default:
                potm->otmpFullName = (PWSTR)(uintptr_t)off;
                break;
            }
            p += cb;
        }
    }
    return need;
}

UINT WINAPI GetOutlineTextMetricsA(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA potm)
{
    if (!hdc)
        return 0;
    OutlineInfo info;
    BuildOutlineInfo(hdc, &info);

    UINT cbNames = OutlineInfoBytes(info, false);
    UINT need = (UINT)sizeof(OUTLINETEXTMETRICA) + cbNames;
    if (!potm || cjCopy == 0)
        return need;
    if (cjCopy < sizeof(OUTLINETEXTMETRICA))
        return 0;

    memset(potm, 0, sizeof(OUTLINETEXTMETRICA));
    potm->otmSize = sizeof(OUTLINETEXTMETRICA);
    // TEXTMETRICA/W 除首尾字符字段外布局一致，逐字段搬运
    potm->otmTextMetrics.tmHeight = info.tm.tmHeight;
    potm->otmTextMetrics.tmAscent = info.tm.tmAscent;
    potm->otmTextMetrics.tmDescent = info.tm.tmDescent;
    potm->otmTextMetrics.tmInternalLeading = info.tm.tmInternalLeading;
    potm->otmTextMetrics.tmExternalLeading = info.tm.tmExternalLeading;
    potm->otmTextMetrics.tmAveCharWidth = info.tm.tmAveCharWidth;
    potm->otmTextMetrics.tmMaxCharWidth = info.tm.tmMaxCharWidth;
    potm->otmTextMetrics.tmWeight = info.tm.tmWeight;
    potm->otmTextMetrics.tmOverhang = info.tm.tmOverhang;
    potm->otmTextMetrics.tmDigitizedAspectX = info.tm.tmDigitizedAspectX;
    potm->otmTextMetrics.tmDigitizedAspectY = info.tm.tmDigitizedAspectY;
    potm->otmTextMetrics.tmFirstChar = (BYTE)info.tm.tmFirstChar;
    potm->otmTextMetrics.tmLastChar = (BYTE)info.tm.tmLastChar;
    potm->otmTextMetrics.tmDefaultChar = (BYTE)info.tm.tmDefaultChar;
    potm->otmTextMetrics.tmBreakChar = (BYTE)info.tm.tmBreakChar;
    potm->otmTextMetrics.tmItalic = info.tm.tmItalic;
    potm->otmTextMetrics.tmUnderlined = info.tm.tmUnderlined;
    potm->otmTextMetrics.tmStruckOut = info.tm.tmStruckOut;
    potm->otmTextMetrics.tmPitchAndFamily = info.tm.tmPitchAndFamily;
    potm->otmTextMetrics.tmCharSet = info.tm.tmCharSet;
    potm->otmPanoseNumber = info.panose;
    potm->otmfsSelection = info.fsSelection;
    potm->otmfsType = info.fsType;
    potm->otmsCharSlopeRise = 1;
    potm->otmsCharSlopeRun = 0;
    potm->otmItalicAngle = 0;
    potm->otmEMSquare = info.emSquare;
    potm->otmAscent = info.ascent;
    potm->otmDescent = info.descent;
    potm->otmLineGap = info.lineGap;
    potm->otmsCapEmHeight = info.capEmHeight;
    potm->otmsXHeight = info.xHeight;
    potm->otmrcFontBox = info.fontBox;
    potm->otmMacAscent = info.ascent;
    potm->otmMacDescent = info.descent;
    potm->otmMacLineGap = info.lineGap;
    potm->otmusMinimumPPEM = 1;
    potm->otmsStrikeoutSize = info.strikeoutSize;
    potm->otmsStrikeoutPosition = info.strikeoutPosition;
    potm->otmsUnderscoreSize = info.underscoreSize;
    potm->otmsUnderscorePosition = info.underscorePosition;

    if (cjCopy >= need)
    {
        char *base = (char *)potm;
        char *p = base + sizeof(OUTLINETEXTMETRICA);
        const std::string *names[4] = { &info.family, &info.face, &info.style, &info.fullName };
        for (int i = 0; i < 4; i++)
        {
            size_t cb = names[i]->size() + 1;
            memcpy(p, names[i]->c_str(), cb);
            size_t off = (size_t)(p - base);
            switch (i)
            {
            case 0:
                potm->otmpFamilyName = (PSTR)(uintptr_t)off;
                break;
            case 1:
                potm->otmpFaceName = (PSTR)(uintptr_t)off;
                break;
            case 2:
                potm->otmpStyleName = (PSTR)(uintptr_t)off;
                break;
            default:
                potm->otmpFullName = (PSTR)(uintptr_t)off;
                break;
            }
            p += cb;
        }
    }
    return need;
}

/* --------------------------------------------------------- 字体族枚举 */

struct FamilyFace
{
    std::string family;
    std::string style;
    LONG weight;
    BYTE italic;
};

/* CoreText 的 kCTFontWeightTrait 是 -1..1 的归一化字重，换算成 Win32 的 FW_*。 */
static LONG CtWeightTraitToWin(double t, CTFontSymbolicTraits traits)
{
    if (traits & kCTFontBoldTrait)
        return FW_BOLD;
    if (t >= 0.45)
        return FW_BOLD;
    if (t >= 0.30)
        return FW_SEMIBOLD;
    if (t >= 0.15)
        return FW_MEDIUM;
    if (t > -0.15)
        return FW_NORMAL;
    if (t > -0.45)
        return FW_LIGHT;
    return FW_EXTRALIGHT;
}

/* 把匹配到的字体描述符转成 FamilyFace。直接用 descriptor 属性，不实例化 CTFont：
 * 枚举全部字体时（空家族名）会有几百个描述符，逐个建 CTFont 意义不大。 */
static void AppendDescriptors(CFArrayRef matches, std::set<std::string> &seen, std::vector<FamilyFace> *out)
{
    CFIndex count = matches ? CFArrayGetCount(matches) : 0;
    for (CFIndex i = 0; i < count; i++)
    {
        CTFontDescriptorRef d = (CTFontDescriptorRef)CFArrayGetValueAtIndex(matches, i);
        CFTypeRef fam = CTFontDescriptorCopyAttribute(d, kCTFontFamilyNameAttribute);
        CFTypeRef styleName = CTFontDescriptorCopyAttribute(d, kCTFontStyleNameAttribute);
        CFTypeRef traitsRef = CTFontDescriptorCopyAttribute(d, kCTFontTraitsAttribute);

        FamilyFace ff;
        ff.family = CfStringToUtf8((CFStringRef)fam);
        ff.style = CfStringToUtf8((CFStringRef)styleName);
        ff.weight = FW_NORMAL;
        ff.italic = 0;
        if (traitsRef && CFGetTypeID(traitsRef) == CFDictionaryGetTypeID())
        {
            CFDictionaryRef traits = (CFDictionaryRef)traitsRef;
            SInt32 symbolic = 0;
            double weightTrait = 0;
            CFNumberRef num = (CFNumberRef)CFDictionaryGetValue(traits, kCTFontSymbolicTrait);
            if (num)
                CFNumberGetValue(num, kCFNumberSInt32Type, &symbolic);
            num = (CFNumberRef)CFDictionaryGetValue(traits, kCTFontWeightTrait);
            if (num)
                CFNumberGetValue(num, kCFNumberDoubleType, &weightTrait);
            ff.weight = CtWeightTraitToWin(weightTrait, (CTFontSymbolicTraits)symbolic);
            ff.italic = (symbolic & kCTFontItalicTrait) ? 1 : 0;
        }
        if (fam)
            CFRelease(fam);
        if (styleName)
            CFRelease(styleName);
        if (traitsRef)
            CFRelease(traitsRef);

        if (ff.family.empty())
            continue;
        if (ff.style.empty())
            ff.style = ff.family;
        std::string key = ff.family + "#" + std::to_string((long)ff.weight) + "#" + (ff.italic ? "1" : "0");
        if (!seen.insert(key).second)
            continue; // 同家族同字重的多个文件只报一次
        out->push_back(ff);
    }
}

static CFArrayRef CopyMatchingDescriptors(CFDictionaryRef attrs)
{
    CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(attrs);
    CFArrayRef matches = desc ? CTFontDescriptorCreateMatchingFontDescriptors(desc, NULL) : NULL;
    if (desc)
        CFRelease(desc);
    return matches;
}

/* 列出「请求的家族（经 CoreText 解析后）的所有面」。
 * 请求名在 macOS 上未必存在（多是 Windows 字体名），先让 CoreText 按与绘制
 * 路径相同的方式选一次字体，再枚举该系统上真实家族的所有面。 */
static void ListFamilyFaces(const char *familyUtf8, std::vector<FamilyFace> *out)
{
    if (!out)
        return;

    std::set<std::string> seen;
    std::string requested = familyUtf8 ? familyUtf8 : "";

    if (!requested.empty())
    {
        CFStringRef cfRequested = Utf8ToCfString(requested.c_str());
        if (cfRequested)
        {
            CTFontRef font = CTFontCreateWithName(cfRequested, 12.0, NULL);
            CFRelease(cfRequested);
            if (font)
            {
                CFStringRef fam = CTFontCopyFamilyName(font);
                std::string resolved = CfStringToUtf8(fam);
                if (fam)
                    CFRelease(fam);
                CFRelease(font);
                if (!resolved.empty())
                    requested = resolved;
            }
        }

        CFStringRef cfFamily = Utf8ToCfString(requested.c_str());
        if (!cfFamily)
            return;
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 1,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        if (!attrs)
        {
            CFRelease(cfFamily);
            return;
        }
        CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, cfFamily);
        CFArrayRef matches = CopyMatchingDescriptors(attrs);
        CFRelease(attrs);
        CFRelease(cfFamily);
        AppendDescriptors(matches, seen, out);
        if (matches)
            CFRelease(matches);
    }
    else
    {
        // 空家族名 = 枚举系统所有字体（Win32 EnumFontFamiliesEx 的语义）。
        // 空属性描述符会匹配全部已安装字体（这里约 500+ 个面）。
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                                 &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
        if (!attrs)
            return;
        CFArrayRef matches = CopyMatchingDescriptors(attrs);
        CFRelease(attrs);
        AppendDescriptors(matches, seen, out);
        if (matches)
            CFRelease(matches);
    }
}

int WINAPI EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
{
    (void)dwFlags;
    if (!hdc || !lpProc)
        return 0;

    std::string familyUtf8;
    if (lpLogfont && lpLogfont->lfFaceName[0])
        familyUtf8 = WideToUtf8Str(lpLogfont->lfFaceName);

    std::vector<FamilyFace> faces;
    ListFamilyFaces(familyUtf8.c_str(), &faces);
    if (faces.empty())
        return 0;

    TEXTMETRICW tm;
    bool bHasTm = GetTextMetricsW(hdc, &tm) != FALSE;
    LONG lfHeight = -12;
    LONG lfCharSet = DEFAULT_CHARSET;
    if (hdc->hfont)
    {
        LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
        if (lf)
        {
            lfHeight = lf->lfHeight;
            lfCharSet = lf->lfCharSet;
        }
    }

    int ret = 1;
    for (size_t i = 0; i < faces.size(); i++)
    {
        const FamilyFace &ff = faces[i];
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        if (!Utf8ToWide(ff.family.c_str(), lf.lfFaceName, LF_FACESIZE))
            continue;
        lf.lfHeight = lfHeight;
        lf.lfWeight = ff.weight;
        lf.lfItalic = ff.italic;
        lf.lfCharSet = (BYTE)lfCharSet;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

        TEXTMETRICW candidate = tm;
        if (!bHasTm)
            memset(&candidate, 0, sizeof(candidate));
        candidate.tmWeight = ff.weight;
        candidate.tmItalic = ff.italic;
        // 字体文件是 TrueType/OpenType，按 Win32 约定报 TMPF_TRUETYPE|TMPF_VECTOR
        ret = lpProc(&lf, &candidate, TMPF_TRUETYPE | TMPF_VECTOR, lParam);
        if (!ret)
            break;
    }
    return ret;
}

int WINAPI EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
{
    (void)dwFlags;
    if (!hdc || !lpProc)
        return 0;

    std::string familyUtf8;
    if (lpLogfont && lpLogfont->lfFaceName[0])
        familyUtf8 = lpLogfont->lfFaceName; // LOGFONTA 的面名在 swinx 里就是 UTF-8

    std::vector<FamilyFace> faces;
    ListFamilyFaces(familyUtf8.c_str(), &faces);
    if (faces.empty())
        return 0;

    TEXTMETRICA tm;
    bool bHasTm = GetTextMetricsA(hdc, &tm) != FALSE;
    LONG lfHeight = -12;
    LONG lfCharSet = DEFAULT_CHARSET;
    if (hdc->hfont)
    {
        LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
        if (lf)
        {
            lfHeight = lf->lfHeight;
            lfCharSet = lf->lfCharSet;
        }
    }

    int ret = 1;
    for (size_t i = 0; i < faces.size(); i++)
    {
        const FamilyFace &ff = faces[i];
        LOGFONTA lf;
        memset(&lf, 0, sizeof(lf));
        strncpy(lf.lfFaceName, ff.family.c_str(), LF_FACESIZE - 1);
        lf.lfFaceName[LF_FACESIZE - 1] = '\0';
        lf.lfHeight = lfHeight;
        lf.lfWeight = ff.weight;
        lf.lfItalic = ff.italic;
        lf.lfCharSet = (BYTE)lfCharSet;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

        TEXTMETRICA candidate = tm;
        if (!bHasTm)
            memset(&candidate, 0, sizeof(candidate));
        candidate.tmWeight = ff.weight;
        candidate.tmItalic = ff.italic;
        ret = lpProc(&lf, &candidate, TMPF_TRUETYPE | TMPF_VECTOR, lParam);
        if (!ret)
            break;
    }
    return ret;
}
