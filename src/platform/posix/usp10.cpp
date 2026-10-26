/* Uniscribe（usp10）的 POSIX 实现。
 *
 * 明确的实现边界：**不做整形**。
 *   - ScriptItemize 把整串当成一个 run（sScript = SCRIPT_UNDEFINED）；
 *   - ScriptShape 一个 UTF-16 码元对一个字形，clusters 恒等；
 *   - ScriptPlace 用 GetCharWidthI 取步进，GOFFSET 全 0。
 *
 * 这意味着阿拉伯文的连写、印度系的字形重排、泰文的上下标定位都不会发生 ——
 * 文本会"逐字线性排列"。这是有意的取舍：真做整形要接 HarfBuzz 的 API 面
 * （shape/position/reorder），在 usp10 的接口形状里假装做只会让调用方更难
 * 发现问题。拉丁/中日韩/等宽文本的表现是正常的。
 *
 * 唯一"有内容"的部分是字体回退：FontCacheWin 用
 * ScriptStringAnalyse(SSA_FALLBACK) + ScriptStringOut 问"这个字符最终会用
 * 哪个字体画"（靠 EMF 里 SelectObject 的记录回放）。这里用 fontconfig 找出
 * 覆盖该字符串的家族，把它的 HFONT 选进那个录制 DC —— 于是 CJK 回退能真的
 * 工作，而不是永远返回主字体。
 *
 * ⚠️ 宽度：入参字符串是 UTF-16 码元（见 usp10.h 的说明），交给 swinx 的
 * GDI 之前必须先转成本地 wchar_t 串。
 */

#include <windows.h>
#include <usp10.h>

#include <string.h>
#include <string>
#include <vector>

#include <fontconfig/fontconfig.h>

#include "debug.h"
#define kLogTag "usp10"

namespace
{

std::wstring toWideString(const SCRIPT_UCHAR *chars, int count)
{
    std::wstring out;
    out.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        out.push_back((wchar_t)chars[i]);
    return out;
}

/* 找出覆盖这些码点的字体家族（UTF-8 家族名）。找不到返回空串。
 * 保持调用方当前的家族优先：fontconfig 的排序已经把"与 baseFamily 最接近"
 * 的字体排在前面，按顺序取第一个覆盖全部字符的即可。 */
std::string findCoveringFamily(const std::vector<SCRIPT_UCHAR> &text, const char *baseFamily)
{
    if (text.empty())
        return std::string();

    FcPattern *pattern = FcPatternCreate();
    if (!pattern)
        return std::string();

    FcPatternAddString(pattern, FC_FAMILY,
                       (const FcChar8 *)((baseFamily && *baseFamily) ? baseFamily : "sans"));
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

        bool coversAll = true;
        for (size_t k = 0; k < text.size(); ++k)
        {
            if (!FcCharSetHasChar(charset, (FcChar32)text[k]))
            {
                coversAll = false;
                break;
            }
        }
        if (!coversAll)
            continue;

        FcChar8 *family = NULL;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family)
            found = (const char *)family;
    }

    FcFontSetDestroy(fonts);
    return found;
}

/* 以 DC 上当前字体的字号/字重为准，换成另一个家族。 */
HFONT createFontLike(HDC hdc, const std::string &familyUtf8)
{
    LOGFONTA lf;
    memset(&lf, 0, sizeof(lf));

    HGDIOBJ current = GetCurrentObject(hdc, OBJ_FONT);
    if (current)
        GetObjectA(current, sizeof(lf), &lf);

    strncpy(lf.lfFaceName, familyUtf8.c_str(), LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    return CreateFontIndirectA(&lf);
}

/* SSA_FALLBACK：当前字体画不全这些字符时，找一个能画全的。
 * 返回 0 表示"不需要 / 找不到"——两种情况下都沿用 DC 上已选的字体，
 * 与 Uniscribe 在找不到回退字体时的行为一致（画 .notdef）。 */
HFONT pickFallbackFont(HDC hdc, const std::vector<SCRIPT_UCHAR> &text)
{
    if (text.empty())
        return 0;

    std::wstring wide = toWideString(&text[0], (int)text.size());
    std::vector<WORD> glyphs(text.size());
    if (GetGlyphIndicesW(hdc, wide.c_str(), (int)text.size(), &glyphs[0], GGI_MARK_NONEXISTING_GLYPHS) ==
        GDI_ERROR)
        return 0;

    bool missing = false;
    for (size_t i = 0; i < glyphs.size(); ++i)
    {
        if (glyphs[i] == 0xFFFF)
        {
            missing = true;
            break;
        }
    }
    if (!missing)
        return 0;

    char currentFace[LF_FACESIZE] = { 0 };
    GetTextFaceA(hdc, LF_FACESIZE, currentFace);

    std::string family = findCoveringFamily(text, currentFace);
    if (family.empty())
        return 0;

    return createFontLike(hdc, family);
}

struct ScriptStringImpl
{
    HDC hdc;
    DWORD flags;
    std::vector<SCRIPT_UCHAR> text;
    HFONT fallbackFont;

    ScriptStringImpl()
        : hdc(0)
        , flags(0)
        , fallbackFont(0)
    {
    }
};

inline ScriptStringImpl *implOf(SCRIPT_STRING_ANALYSIS ssa)
{
    return (ScriptStringImpl *)ssa;
}

} // namespace

HRESULT WINAPI ScriptItemize(const SCRIPT_UCHAR *pwcInChars, int cInChars, int cMaxItems,
                             const SCRIPT_CONTROL *psControl, const SCRIPT_STATE *psState,
                             SCRIPT_ITEM *pItems, int *pcItems)
{
    (void)psControl; /* 不做整形，控制项没有落点 */

    if (!pcItems || cInChars < 0)
        return E_INVALIDARG;
    if (cInChars > 0 && !pwcInChars)
        return E_INVALIDARG;

    if (cInChars == 0)
    {
        *pcItems = 0;
        return S_OK;
    }

    if (!pItems || cMaxItems < 2)
    {
        /* 一个 run + 结尾哨兵要两项。Uniscribe 用 E_OUTOFMEMORY 表示"缓冲不够"，
         * WebKit 靠这个码把 m_items 翻倍重试。 */
        *pcItems = 2;
        return E_OUTOFMEMORY;
    }

    memset(&pItems[0], 0, sizeof(SCRIPT_ITEM));
    pItems[0].iCharPos = 0;
    pItems[0].a.sScript = SCRIPT_UNDEFINED;
    if (psState)
        pItems[0].a.state = *psState;

    memset(&pItems[1], 0, sizeof(SCRIPT_ITEM));
    pItems[1].iCharPos = cInChars;

    *pcItems = 1;
    return S_OK;
}

HRESULT WINAPI ScriptShape(HDC hdc, SCRIPT_CACHE *psc, const SCRIPT_UCHAR *pwcChars, int cChars,
                           int cMaxGlyphs, SCRIPT_ANALYSIS *psa, WORD *pwOutGlyphs,
                           WORD *pwLogClust, SCRIPT_VISATTR *psva, int *pcGlyphs)
{
    (void)psc; /* swinx 的 usp10 不缓存任何东西，句柄恒为 NULL */
    (void)psa;

    if (!pwcChars || !pwOutGlyphs || !pwLogClust || !psva || !pcGlyphs || cChars < 0)
        return E_INVALIDARG;

    if (cChars == 0)
    {
        *pcGlyphs = 0;
        return S_OK;
    }

    /* 字形索引只能从"选着字体的 DC"拿到（swinx 的字体数据全在 HDC 上）。
     * 按 Uniscribe 的约定用 E_PENDING 要一个带字体的 DC，调用方会重试。 */
    if (!hdc)
        return E_PENDING;

    if (cMaxGlyphs < cChars)
        return E_OUTOFMEMORY;

    std::wstring wide = toWideString(pwcChars, cChars);
    if (GetGlyphIndicesW(hdc, wide.c_str(), cChars, pwOutGlyphs, 0) == GDI_ERROR)
        return E_FAIL;

    for (int i = 0; i < cChars; ++i)
    {
        pwLogClust[i] = (WORD)i;
        memset(&psva[i], 0, sizeof(SCRIPT_VISATTR));
        psva[i].uJustification = SCRIPT_JUSTIFY_NONE;
        psva[i].fClusterStart = 1;
    }

    *pcGlyphs = cChars;
    return S_OK;
}

HRESULT WINAPI ScriptPlace(HDC hdc, SCRIPT_CACHE *psc, const WORD *pwGlyphs, int cGlyphs,
                           const SCRIPT_VISATTR *psva, SCRIPT_ANALYSIS *psa, int *piAdvance,
                           GOFFSET *pGoffset, ABC *pABC)
{
    (void)psc;
    (void)psva;
    (void)psa;

    if (!pwGlyphs || !piAdvance || !pGoffset || cGlyphs < 0)
        return E_INVALIDARG;

    if (cGlyphs == 0)
    {
        if (pABC)
            memset(pABC, 0, sizeof(ABC));
        return S_OK;
    }

    if (!hdc)
        return E_PENDING;

    /* GetCharWidthI 的 pgi 不是 const，拷一份再问。 */
    std::vector<WORD> glyphs(pwGlyphs, pwGlyphs + cGlyphs);
    if (!GetCharWidthI(hdc, 0, (UINT)cGlyphs, &glyphs[0], piAdvance))
        return E_FAIL;

    int total = 0;
    for (int i = 0; i < cGlyphs; ++i)
    {
        pGoffset[i].du = 0;
        pGoffset[i].dv = 0;
        total += piAdvance[i];
    }

    if (pABC)
    {
        pABC->abcA = 0;
        pABC->abcB = total;
        pABC->abcC = 0;
    }
    return S_OK;
}

HRESULT WINAPI ScriptGetFontProperties(HDC hdc, SCRIPT_CACHE *psc, SCRIPT_FONTPROPERTIES *psfp)
{
    (void)psc;

    if (!psfp)
        return E_INVALIDARG;
    if (psfp->cBytes != 0 && psfp->cBytes < (int)sizeof(SCRIPT_FONTPROPERTIES))
        return E_INVALIDARG;

    if (!hdc)
        return E_PENDING;

    /* 三个"特殊字形"一次问出来：空白、缺省字符（U+FFFF 不可用时的占位）、
     * 非法字符。FreeType 未命中时 GetGlyphIndicesW 给 0，也就是 .notdef，
     * 正是 Win32 在这里的语义。 */
    static const SCRIPT_UCHAR kProbe[3] = { (SCRIPT_UCHAR)' ', (SCRIPT_UCHAR)0xFFFF,
                                            (SCRIPT_UCHAR)0xFFFD };
    std::wstring wide = toWideString(kProbe, 3);
    WORD glyphs[3] = { 0, 0, 0 };
    if (GetGlyphIndicesW(hdc, wide.c_str(), 3, glyphs, 0) == GDI_ERROR)
        return E_FAIL;

    psfp->cBytes = sizeof(SCRIPT_FONTPROPERTIES);
    psfp->wgBlank = glyphs[0];
    psfp->wgDefault = glyphs[1];
    psfp->wgInvalid = glyphs[2];
    psfp->wgKashida = glyphs[0];
    psfp->iKashidaWidth = 0;

    return S_OK;
}

HRESULT WINAPI ScriptFreeCache(SCRIPT_CACHE *psc)
{
    if (!psc)
        return E_INVALIDARG;

    /* 本实现不分配缓存：字体数据直接来自 HDC，没有可缓存的东西，
     * *psc 始终是 NULL。这里仍然把指针清掉，让"释放后不可再用"这条契约
     * 对调用方依然成立。 */
    *psc = NULL;
    return S_OK;
}

HRESULT WINAPI ScriptXtoCP(int iX, int cChars, int cGlyphs, const WORD *pwLogClust,
                           const SCRIPT_VISATTR *psva, const int *piAdvance,
                           const SCRIPT_ANALYSIS *psa, int *piCP, int *piTrailing)
{
    (void)psva;
    (void)psa;

    if (!pwLogClust || !piAdvance || !piCP || !piTrailing || cGlyphs < 0)
        return E_INVALIDARG;

    *piCP = 0;
    *piTrailing = 0;

    if (iX < 0)
        return S_OK;

    int accumulated = 0;
    for (int i = 0; i < cGlyphs; ++i)
    {
        int next = accumulated + piAdvance[i];
        if (iX < next)
        {
            *piCP = (int)pwLogClust[i];
            /* 光标落在字形后半段就算 trailing —— WebKit 用它决定插入位置
             * 是"字符之前"还是"字符之后"。 */
            *piTrailing =
                (piAdvance[i] > 0 && iX - accumulated >= (piAdvance[i] + 1) / 2) ? 1 : 0;
            return S_OK;
        }
        accumulated = next;
    }

    *piCP = cChars;
    return S_OK;
}

HRESULT WINAPI ScriptStringAnalyse(HDC hdc, const void *pString, int cString, int cGlyphs,
                                   int iCharset, DWORD dwFlags, int iReserved,
                                   SCRIPT_CONTROL *pControl, SCRIPT_STATE *pState,
                                   const int *piDx, SCRIPT_TABDEF *pTabdef,
                                   const BYTE *pbInClass, SCRIPT_STRING_ANALYSIS *pssa)
{
    /* 不做整形，这些参数没有落点；保留形参是为了与 SDK 的签名和调用点一致。 */
    (void)cGlyphs;
    (void)iCharset;
    (void)iReserved;
    (void)pControl;
    (void)pState;
    (void)piDx;
    (void)pTabdef;
    (void)pbInClass;

    if (!hdc || !pssa)
        return E_INVALIDARG;
    if (cString < 0 || (cString > 0 && !pString))
        return E_INVALIDARG;

    ScriptStringImpl *impl = new ScriptStringImpl;
    impl->hdc = hdc;
    impl->flags = dwFlags;

    if (cString > 0)
    {
        const SCRIPT_UCHAR *chars = (const SCRIPT_UCHAR *)pString; /* UTF-16，见头文件说明 */
        impl->text.assign(chars, chars + cString);
        if (dwFlags & SSA_FALLBACK)
            impl->fallbackFont = pickFallbackFont(hdc, impl->text);
    }

    *pssa = (SCRIPT_STRING_ANALYSIS)impl;
    return S_OK;
}

HRESULT WINAPI ScriptStringOut(SCRIPT_STRING_ANALYSIS ssa, int iX, int iY, UINT uOptions,
                               const RECT *prc, int iMinSel, int iMaxSel, BOOL fDisabled)
{
    (void)uOptions;
    (void)prc;
    (void)iMinSel;
    (void)iMaxSel;
    (void)fDisabled;

    ScriptStringImpl *impl = implOf(ssa);
    if (!impl)
        return E_INVALIDARG;

    if (impl->flags & SSA_METAFILE)
    {
        /* 这里**只选字体、不落笔**：swinx 的 EMF 实现只记录
         * EMR_EXTCREATEFONTINDIRECTW 一种记录，FontCacheWin 回放它就是为了
         * 知道"Uniscribe 会选哪个字体"。真往下画反而会多出一条它不认识的记录。 */
        if (impl->fallbackFont)
            SelectObject(impl->hdc, impl->fallbackFont);
        return S_OK;
    }

    if (!impl->text.empty())
    {
        std::wstring wide = toWideString(&impl->text[0], (int)impl->text.size());
        TextOutW(impl->hdc, iX, iY, wide.c_str(), (int)wide.size());
    }
    return S_OK;
}

HRESULT WINAPI ScriptStringFree(SCRIPT_STRING_ANALYSIS *pssa)
{
    if (!pssa)
        return E_INVALIDARG;

    ScriptStringImpl *impl = implOf(*pssa);
    if (impl)
    {
        if (impl->fallbackFont)
            DeleteObject(impl->fallbackFont);
        delete impl;
        *pssa = NULL;
    }
    return S_OK;
}
