#include <windows.h>
#include <gdi.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include "sdc.h"
#include "log.h"
#define kLogTag "fontapi"

/*
 * cairo（linux/android/ohos）GDI 后端的「按字形工作」字体 API。
 *
 * WebKit 的 Windows 字体代码（FontCacheWin / GlyphPageTreeNodeCairoWin /
 * SimpleFontData*Win / FontPlatformDataWin）需要一组按「字形」维度工作的 GDI
 * 入口：按 glyph index 取宽度、取字形度量、取 OUTLINETEXTMETRIC、取字体覆盖的
 * Unicode 区间、按家族枚举字体。
 *
 * 实现方式：
 *   - 用 fontconfig 把 (family, weight, slant) 解析成字体文件；
 *   - 用 FreeType 读字形索引/推进量/度量；
 *   - HDC 侧只读 _SDC::hfont（存 LOGFONTA）。
 *
 * apple（macos/ios）后端不使用 fontconfig —— 它的绘制走 Core Graphics/CoreText，
 * 因此这些入口由 src/gdi/apple/fontapi.cpp 用 CoreText 单独实现，保证「取字形
 * 索引」与「实际绘制」用的是同一个字体。两个实现只在各自后端目录里编译。
 *
 * 字符串约定与 swinx 其它 API 一致：
 *   - W 后缀：wchar_t 字符串（本工程内为 UTF-32，见 WCHAR_SIZE）；
 *   - A 后缀：UTF-8 字符串。
 * 也就是说 GetGlyphIndicesW 的 c 是 wchar_t 个数（每个 wchar_t 一个码点）。
 */

/* ------------------------------------------------------------------ 基础设施 */

static FT_Library GetFtLibrary()
{
    static FT_Library lib = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
        if (FT_Init_FreeType(&lib) != 0)
            lib = nullptr;
    });
    return lib;
}

struct ResolvedFont
{
    FT_Face face;
    int pixelSize;
    std::string family;   // fontconfig 解析出的家族 / 风格 / 全名（UTF-8）
    std::string style;
    std::string fullName;

    ResolvedFont()
        : face(nullptr)
        , pixelSize(0)
    {
    }
    ~ResolvedFont()
    {
        if (face)
            FT_Done_Face(face);
    }
};

/* FreeType 的 FT_Face 不是线程安全的（FT_Load_Glyph 会改写 glyph slot），
 * 而且字体缓存也在这个锁里，所以所有 FreeType 访问统一串行化。字形查询本身很短，
 * 这个粒度的争用可以接受。 */
static std::mutex s_ftMutex;
static std::map<std::string, std::shared_ptr<ResolvedFont> > s_faceCache;

static int WinWeightToFc(LONG weight)
{
    if (weight <= 0)
        return FC_WEIGHT_REGULAR;
    if (weight < FW_LIGHT)
        return FC_WEIGHT_THIN;
    if (weight < FW_NORMAL)
        return FC_WEIGHT_LIGHT;
    if (weight == FW_NORMAL)
        return FC_WEIGHT_REGULAR;
    if (weight < FW_BOLD)
        return FC_WEIGHT_MEDIUM;
    if (weight == FW_BOLD)
        return FC_WEIGHT_BOLD;
    if (weight < FW_BLACK)
        return FC_WEIGHT_EXTRABOLD;
    return FC_WEIGHT_BLACK;
}

static LONG FcWeightToWin(int weight)
{
    if (weight < FC_WEIGHT_REGULAR)
        return FW_LIGHT;
    if (weight == FC_WEIGHT_REGULAR)
        return FW_NORMAL;
    if (weight < FC_WEIGHT_BOLD)
        return FW_MEDIUM;
    if (weight == FC_WEIGHT_BOLD)
        return FW_BOLD;
    if (weight < FC_WEIGHT_BLACK)
        return FW_EXTRABOLD;
    return FW_BLACK;
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

/* 把 LOGFONTA 描述的字体解析成真实的字体文件并打开 FT_Face。
 * 调用方必须持有 s_ftMutex。 */
static std::shared_ptr<ResolvedFont> ResolveFontLocked(const LOGFONTA *lf)
{
    if (!lf)
        return std::shared_ptr<ResolvedFont>();

    char key[256];
    snprintf(key, sizeof(key), "%s#%d#%d#%d", lf->lfFaceName, (int)lf->lfWeight, lf->lfItalic ? 1 : 0, (int)abs(lf->lfHeight));
    std::map<std::string, std::shared_ptr<ResolvedFont> >::iterator it = s_faceCache.find(key);
    if (it != s_faceCache.end())
        return it->second;

    FT_Library lib = GetFtLibrary();
    if (!lib)
        return std::shared_ptr<ResolvedFont>();

    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return std::shared_ptr<ResolvedFont>();
    if (lf->lfFaceName[0])
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)lf->lfFaceName);
    FcPatternAddInteger(pat, FC_WEIGHT, WinWeightToFc(lf->lfWeight));
    FcPatternAddInteger(pat, FC_SLANT, lf->lfItalic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult fcRes = FcResultNoMatch;
    FcPattern *matched = FcFontMatch(nullptr, pat, &fcRes);
    FcPatternDestroy(pat);
    if (!matched)
        return std::shared_ptr<ResolvedFont>();

    FcChar8 *file = nullptr;
    FcChar8 *fcFamily = nullptr;
    FcChar8 *fcStyle = nullptr;
    FcChar8 *fcFullName = nullptr;
    int index = 0;
    bool ok = FcPatternGetString(matched, FC_FILE, 0, &file) == FcResultMatch && file != nullptr;
    FcPatternGetInteger(matched, FC_INDEX, 0, &index);
    FcPatternGetString(matched, FC_FAMILY, 0, &fcFamily);
    FcPatternGetString(matched, FC_STYLE, 0, &fcStyle);
    FcPatternGetString(matched, FC_FULLNAME, 0, &fcFullName);

    std::shared_ptr<ResolvedFont> rf;
    if (ok)
    {
        FT_Face face = nullptr;
        if (FT_New_Face(lib, (const char *)file, index, &face) == 0 && face)
        {
            int px = abs(lf->lfHeight);
            if (px <= 0)
                px = 12;
            FT_Select_Charmap(face, FT_ENCODING_UNICODE); // 失败则沿用默认 charmap
            if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) == 0)
            {
                rf.reset(new ResolvedFont);
                rf->face = face;
                rf->pixelSize = px;
                if (fcFamily)
                    rf->family = (const char *)fcFamily;
                if (fcStyle)
                    rf->style = (const char *)fcStyle;
                if (fcFullName)
                    rf->fullName = (const char *)fcFullName;
                if (rf->family.empty())
                    rf->family = lf->lfFaceName;
                if (rf->fullName.empty())
                    rf->fullName = rf->family;
            }
            else
            {
                FT_Done_Face(face);
            }
        }
    }
    FcPatternDestroy(matched);

    if (!rf)
        return rf; // 解析失败不写缓存，避免把失败状态固化下来
    if (s_faceCache.size() > 64)
        s_faceCache.clear(); // 简单的容量控制：超过上限就整体丢弃
    s_faceCache[key] = rf;
    return rf;
}

/* 取 HDC 当前字体对应的 FT_Face。调用方必须持有 s_ftMutex。 */
static std::shared_ptr<ResolvedFont> ResolveFontFromDcLocked(HDC hdc)
{
    if (!hdc || !hdc->hfont)
        return std::shared_ptr<ResolvedFont>();
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    return ResolveFontLocked(lf);
}

/* 字体单位 → 像素（FreeType 的 26.6 之外的 sfnt 字段都是按 unitsPerEm 定义的） */
static double ScaleUnits(FT_Face face, double value)
{
    if (!face || face->units_per_EM == 0)
        return 0;
    return value * (double)face->size->metrics.y_ppem / (double)face->units_per_EM;
}

/* ------------------------------------------------------------- 字形索引/宽度 */

DWORD WINAPI GetGlyphIndicesW(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl)
{
    if (!hdc || !lpstr || !pgi || c < 0)
        return GDI_ERROR;
    if (c == 0)
        return 0;

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf)
        return GDI_ERROR;

    const bool bMarkMissing = (fl & GGI_MARK_NONEXISTING_GLYPHS) != 0;
    for (int i = 0; i < c; i++)
    {
        FT_ULong cp = (FT_ULong)(uint32_t)lpstr[i];
        if (cp > 0x10FFFF)
        {
            pgi[i] = bMarkMissing ? 0xFFFF : 0;
            continue;
        }
        FT_UInt gi = FT_Get_Char_Index(rf->face, cp);
        // 未命中时 FreeType 返回 0（.notdef）；Win32 在 GGI_MARK_NONEXISTING_GLYPHS
        // 下用 0xFFFF 标记缺字。
        if (gi == 0 && bMarkMissing)
            gi = 0xFFFF;
        pgi[i] = gi > 0xFFFF ? 0xFFFF : (WORD)gi;
    }
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
 * 的返回值上（返回设备空间度量）。cairo 的 win32 字体后端依赖这一点：它给 HDC
 * 设置 logical->device 的缩放（例如 1/32），然后把返回的黑盒除以 x_scale。
 * FreeType 给出的是「按 lfHeight 像素大小」的字形空间度量，这里乘上世界变换的
 * 线性部分，才能和 Windows 的语义一致；否则字形包围盒会放大 32 倍（或被裁成
 * 1 像素宽）。 */
static void ApplyWorldTransformToGlyphMetrics(HDC hdc, GLYPHMETRICS *gm)
{
    // 世界变换存在 hdc->mtx 里：
    //   x' = xx*x + xy*y + x0, y' = yx*x + yy*y + y0
    // 平移分量不参与（GDI 只变换线性部分），且字形度量始终相对基线取值。
    const cairo_matrix_t &m = hdc->mtx;
    if (m.xx == 1 && m.yx == 0 && m.xy == 0 && m.yy == 1)
        return;

    double left = (double)gm->gmptGlyphOrigin.x;
    double top = (double)gm->gmptGlyphOrigin.y;
    double right = left + (double)gm->gmBlackBoxX;
    double bottom = top - (double)gm->gmBlackBoxY;

    double xs[4], ys[4];
    xs[0] = m.xx * left + m.xy * top;    ys[0] = m.yx * left + m.yy * top;
    xs[1] = m.xx * right + m.xy * top;   ys[1] = m.yx * right + m.yy * top;
    xs[2] = m.xx * left + m.xy * bottom; ys[2] = m.yx * left + m.yy * bottom;
    xs[3] = m.xx * right + m.xy * bottom; ys[3] = m.yx * right + m.yy * bottom;

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

    double adv = (double)gm->gmCellIncX;
    gm->gmCellIncX = (short)ceil(m.xx * adv);
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

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf)
        return GDI_ERROR;

    FT_UInt gi = (fuFormat & GGO_GLYPH_INDEX) ? (FT_UInt)uChar : FT_Get_Char_Index(rf->face, (FT_ULong)uChar);
    if (FT_Load_Glyph(rf->face, gi, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0)
        return GDI_ERROR;

    const FT_Glyph_Metrics &m = rf->face->glyph->metrics;
    memset(lpgm, 0, sizeof(GLYPHMETRICS));
    lpgm->gmBlackBoxX = (UINT)ceil((double)m.width / 64.0);
    lpgm->gmBlackBoxY = (UINT)ceil((double)m.height / 64.0);
    lpgm->gmptGlyphOrigin.x = (LONG)floor((double)m.horiBearingX / 64.0);
    lpgm->gmptGlyphOrigin.y = (LONG)floor((double)m.horiBearingY / 64.0);
    lpgm->gmCellIncX = (short)ceil((double)m.horiAdvance / 64.0);
    lpgm->gmCellIncY = 0;
    ApplyWorldTransformToGlyphMetrics(hdc, lpgm);
    return sizeof(GLYPHMETRICS);
}

DWORD WINAPI GetGlyphOutlineA(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, CONST MAT2 *lpmat2)
{
    if (fuFormat & GGO_GLYPH_INDEX)
        return GetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
    // A 版本把 uChar 当 UTF-8 字符处理
    if (uChar > 0x10FFFF)
        return GDI_ERROR;
    return GetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
}

BOOL WINAPI GetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT lpBuffer)
{
    if (!hdc || !lpBuffer)
        return FALSE;

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf)
        return FALSE;

    for (UINT i = 0; i < cgi; i++)
    {
        FT_UInt gi = pgi ? (FT_UInt)pgi[i] : (FT_UInt)(giFirst + i);
        if (FT_Load_Glyph(rf->face, gi, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0)
        {
            lpBuffer[i] = 0;
            continue;
        }
        lpBuffer[i] = (int)ceil((double)rf->face->glyph->metrics.horiAdvance / 64.0);
    }
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

/* 读原始 sfnt 表。
 * GDI 的 dwTable 是 MAKEFOURCC 口径 —— 首字符在最低字节（WebKit 传 'eman'
 * 表示 "name"、0x20464643 表示 "CFF "），而 FreeType 的 tag 是 FT_MAKE_TAG
 * 口径 —— 首字符在最高字节（"head" = 0x68656164）。两者字节序相反，直接把
 * GDI 的值交给 FreeType 会一张表都查不到，所以先按 Win32 口径翻转再查；
 * 万一调用方（从 FreeType/DirectWrite 移植的代码）已经按 FT 口径传值，
 * 再用原值查一次，两种写法都能工作。 */
static int LoadSfntTable(FT_Face face, FT_ULong gdiTag, FT_Long offset,
                         FT_Byte *buffer, FT_ULong *length)
{
    FT_ULong swapped = ((gdiTag & 0xFFu) << 24) | ((gdiTag & 0xFF00u) << 8) |
                       ((gdiTag >> 8) & 0xFF00u) | (gdiTag >> 24);
    int error = FT_Load_Sfnt_Table(face, swapped, offset, buffer, length);
    if (error != 0 && swapped != gdiTag)
        error = FT_Load_Sfnt_Table(face, gdiTag, offset, buffer, length);
    return error;
}

DWORD WINAPI GetFontData(HDC hdc, DWORD dwTable, DWORD dwOffset, LPVOID pvBuffer, DWORD cbData)
{
    if (!hdc || dwTable == 0)
        return GDI_ERROR; // 不支持「整个字体文件」的读取（Type1 判定因此恒为 false）

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf || !FT_IS_SFNT(rf->face))
        return GDI_ERROR;

    FT_ULong length = pvBuffer ? cbData : 0;
    if (LoadSfntTable(rf->face, (FT_ULong)dwTable, (FT_Long)dwOffset,
                      (FT_Byte *)pvBuffer, &length) != 0)
        return GDI_ERROR;
    return (DWORD)length;
}

DWORD WINAPI GetCharacterPlacementW(HDC hdc, LPCWSTR lpString, int nCount, int nMaxExtent,
                                    LPGCP_RESULTSW lpResults, DWORD dwFlags)
{
    (void)dwFlags;
    if (!hdc || !lpString || !lpResults || nCount <= 0)
        return 0;
    if (lpResults->lStructSize != sizeof(GCP_RESULTSW))
        return 0;

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf)
        return 0;

    // 先算出完整的「glyph + 步进」序列，再按调用方的缓冲区大小决定写多少。
    std::vector<WORD> glyphs((size_t)nCount);
    std::vector<int> dx((size_t)nCount);
    int totalWidth = 0;
    UINT fitted = 0;
    for (int i = 0; i < nCount; i++)
    {
        FT_ULong cp = (FT_ULong)(uint32_t)lpString[i];
        FT_UInt gi = (cp <= 0x10FFFF) ? FT_Get_Char_Index(rf->face, cp) : 0;
        int width = 0;
        if (FT_Load_Glyph(rf->face, gi, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0)
            width = (int)ceil((double)rf->face->glyph->metrics.horiAdvance / 64.0);
        if (nMaxExtent > 0 && totalWidth + width > nMaxExtent)
            break;
        glyphs[fitted] = (WORD)(gi > 0xFFFF ? 0xFFFF : gi);
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

    if (lpResults->lpDx)
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

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontFromDcLocked(hdc);
    if (!rf)
        return 0;

    std::vector<WCRANGE> ranges;
    FT_UInt gindex = 0;
    FT_ULong ch = FT_Get_First_Char(rf->face, &gindex);
    int nGlyphs = 0;
    FT_ULong runLow = 0;
    FT_ULong runCount = 0;
    while (gindex)
    {
        nGlyphs++;
        // GLYPHSET 按 UTF-16 组织，这里只统计 BMP（WK 的 currentFontContainsCharacter
        // 也只看 UChar），代理对不做展开。
        if (ch <= 0xFFFF)
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
        ch = FT_Get_Next_Char(rf->face, ch, &gindex);
    }
    if (runCount)
    {
        WCRANGE r = { (WCHAR)runLow, (USHORT)(runCount > 0xFFFF ? 0xFFFF : runCount) };
        ranges.push_back(r);
    }

    DWORD need = (DWORD)(offsetof(GLYPHSET, ranges) + ranges.size() * sizeof(WCRANGE));
    if (!lpgs)
        return need;

    lpgs->cbThis = need;
    lpgs->flAccel = 0; // 不保证 glyph index 落在 8 位内
    lpgs->cGlyphsSupported = (DWORD)nGlyphs;
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

static void BuildOutlineInfo(HDC hdc, OutlineInfo *out)
{
    *out = OutlineInfo(); // 值初始化：POD 字段清零、string 字段清空
    GetTextMetricsW(hdc, &out->tm);

    LOGFONTA *lf = hdc && hdc->hfont ? (LOGFONTA *)GetGdiObjPtr(hdc->hfont) : nullptr;

    std::lock_guard<std::mutex> lock(s_ftMutex);
    std::shared_ptr<ResolvedFont> rf = ResolveFontLocked(lf);
    if (!rf)
    {
        // 拿不到字体文件时退化：只给 TEXTMETRIC 与面名
        if (lf)
        {
            out->family = lf->lfFaceName;
            out->face = lf->lfFaceName;
            out->fullName = lf->lfFaceName;
        }
        out->ascent = out->tm.tmAscent;
        out->descent = out->tm.tmDescent;
        out->lineGap = (UINT)out->tm.tmExternalLeading;
        out->emSquare = (UINT)(out->tm.tmHeight > 0 ? out->tm.tmHeight : 1);
        out->fsSelection = (lf && lf->lfItalic) ? 1 : 0;
        return;
    }

    FT_Face face = rf->face;
    const FT_Size_Metrics &sm = face->size->metrics;
    out->family = rf->family;
    out->style = rf->style.empty() ? rf->family : rf->style;
    out->face = rf->family;
    out->fullName = rf->fullName;
    out->emSquare = face->units_per_EM ? face->units_per_EM : (UINT)out->tm.tmHeight;
    out->ascent = (int)ceil((double)sm.ascender / 64.0);
    out->descent = (int)ceil(-(double)sm.descender / 64.0);
    out->lineGap = (UINT)(sm.height > 0 ? ceil((double)sm.height / 64.0) - out->ascent - out->descent : 0);
    out->fontBox.left = (LONG)floor(ScaleUnits(face, face->bbox.xMin));
    out->fontBox.top = (LONG)ceil(ScaleUnits(face, face->bbox.yMax));
    out->fontBox.right = (LONG)ceil(ScaleUnits(face, face->bbox.xMax));
    out->fontBox.bottom = (LONG)floor(ScaleUnits(face, face->bbox.yMin));
    // 下划线/删除线在 FreeType 里分别是 FT_Face 的字段与 OS/2 表的字段，
    // 都以字体单位给出（FT_Size_Metrics 里没有这些字段）。
    if (face->underline_thickness)
    {
        out->underscoreSize = (UINT)ceil(ScaleUnits(face, face->underline_thickness));
        out->underscorePosition = (int)floor(ScaleUnits(face, face->underline_position));
    }

    TT_OS2 *os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2 && os2->version != 0xFFFF)
    {
        // fsSelection / fsType / usWeightClass / panose 各版本都有；
        // sxHeight / sCapHeight 是 OS/2 version 2 起才有的字段。
        out->fsSelection = os2->fsSelection;
        out->fsType = os2->fsType;
        memcpy(&out->panose, os2->panose, sizeof(PANOSE));
        if (!out->tm.tmWeight)
            out->tm.tmWeight = (LONG)os2->usWeightClass;
        if (os2->yStrikeoutSize)
        {
            out->strikeoutSize = (UINT)ceil(ScaleUnits(face, os2->yStrikeoutSize));
            out->strikeoutPosition = (int)floor(ScaleUnits(face, os2->yStrikeoutPosition));
        }
        if (os2->version >= 2)
        {
            out->xHeight = (UINT)ceil(ScaleUnits(face, os2->sxHeight));
            out->capEmHeight = (UINT)ceil(ScaleUnits(face, os2->sCapHeight));
        }
        if (!out->xHeight)
            out->xHeight = (UINT)(out->ascent * 0.56); // 与 WK 的兜底估值一致
    }
    else if (!out->xHeight)
    {
        out->xHeight = (UINT)(out->ascent * 0.56);
    }
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

/* 列出「请求的家族（经 fontconfig 替换解析后）的所有面」。
 * 调用方不持有 s_ftMutex（本函数自己取 fontconfig，不碰 FreeType）。 */
static void ListFamilyFaces(const char *familyUtf8, std::vector<FamilyFace> *out)
{
    if (!out)
        return;

    // 请求的家族名在 Linux/macOS 上未必存在（多是 Windows 字体名），
    // 先用 FcFontMatch 解析成系统上真实家族，语义上接近 renderer 的选字体。
    std::string resolved = familyUtf8 ? familyUtf8 : "";
    if (!resolved.empty())
    {
        FcPattern *req = FcPatternCreate();
        if (req)
        {
            FcPatternAddString(req, FC_FAMILY, (const FcChar8 *)resolved.c_str());
            FcConfigSubstitute(nullptr, req, FcMatchPattern);
            FcDefaultSubstitute(req);
            FcResult fcRes = FcResultNoMatch;
            FcPattern *m = FcFontMatch(nullptr, req, &fcRes);
            FcPatternDestroy(req);
            if (m)
            {
                FcChar8 *fam = nullptr;
                if (FcPatternGetString(m, FC_FAMILY, 0, &fam) == FcResultMatch && fam)
                    resolved = (const char *)fam;
                FcPatternDestroy(m);
            }
        }
    }

    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return;
    if (!resolved.empty())
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)resolved.c_str());
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, (const char *)nullptr);
    FcFontSet *set = os ? FcFontList(FcConfigGetCurrent(), pat, os) : nullptr;
    if (os)
        FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    if (!set)
        return;

    std::set<std::string> seen;
    for (int i = 0; i < set->nfont; i++)
    {
        FcPattern *f = set->fonts[i];
        FcChar8 *fam = nullptr;
        FcChar8 *style = nullptr;
        int weight = FC_WEIGHT_NORMAL;
        int slant = FC_SLANT_ROMAN;
        if (FcPatternGetString(f, FC_FAMILY, 0, &fam) != FcResultMatch || !fam)
            continue;
        FcPatternGetString(f, FC_STYLE, 0, &style);
        FcPatternGetInteger(f, FC_WEIGHT, 0, &weight);
        FcPatternGetInteger(f, FC_SLANT, 0, &slant);

        FamilyFace ff;
        ff.family = (const char *)fam;
        ff.style = style ? (const char *)style : "";
        ff.weight = FcWeightToWin(weight);
        ff.italic = slant == FC_SLANT_ROMAN ? 0 : 1;
        std::string key = ff.family + "#" + std::to_string((long)ff.weight) + "#" + (ff.italic ? "1" : "0");
        if (!seen.insert(key).second)
            continue; // 同家族同字重的多个文件只报一次
        out->push_back(ff);
    }
    FcFontSetDestroy(set);
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
