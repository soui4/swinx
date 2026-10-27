#include <sfr_config.h>
#include <windows.h>
#ifndef _USP10_H_
#define _USP10_H_

/* usp10.h —— Uniscribe 的最小可用子集。
 *
 * WebKit 的 win 端口用 Uniscribe 做复杂文本整形（阿拉伯/印度系/泰文）：
 *   - UniscribeController 用 ScriptItemize/ScriptShape/ScriptPlace/ScriptXtoCP
 *     把一段文本切成 run 并拿到字形与步进；
 *   - FontCacheWin 用 ScriptStringAnalyse/ScriptStringOut 反查"Uniscribe 最终
 *     会用哪个字体画这个字符"（靠 EMF 里 SelectObject 的记录）。
 *
 * swinx 的实现**不做整形**：一个字（UTF-16 码元）对应一个字形，clusters 为
 * 恒等映射。这样阿拉伯文的连写、印度系的 reorder 都不会发生 —— 也就是说
 * 这些文字会以"逐字排列"的形态出现，而不是"错误形状的乱码"。要真做整形，
 * 应当接 HarfBuzz（另一套 API 面），而不是在这里假装 Uniscribe。
 *
 */

typedef struct tag_SCRIPT_STATE
{
    WORD uBidiLevel : 5;
    WORD fOverrideDirection : 1;
    WORD fInhibitSymSwap : 1;
    WORD fCharShape : 1;
    WORD fDigitSubstitute : 1;
    WORD fInhibitLigate : 1;
    WORD fDisplayZWG : 1;
    WORD fArabicNumContext : 1;
    WORD fGcpClusters : 1;
    WORD fReserved : 1;
    WORD fEngineReserved : 2;
} SCRIPT_STATE;

typedef struct tag_SCRIPT_ANALYSIS
{
    WORD eScript;
    union {
        SCRIPT_STATE state;
        WORD wFlags;
    };
} SCRIPT_ANALYSIS;

typedef struct tag_SCRIPT_ITEM
{
    int iCharPos;
    SCRIPT_ANALYSIS a;
} SCRIPT_ITEM;

typedef struct tag_SCRIPT_CONTROL
{
    WORD uDefaultLanguage : 16;
    WORD fContextDigits : 1;
    WORD fInvertPreBoundDir : 1;
    WORD fInvertPostBoundDir : 1;
    WORD fLinkStringBefore : 1;
    WORD fLinkStringAfter : 1;
    WORD fNeutralOverride : 1;
    WORD fNumericOverride : 1;
    WORD fLegacyBidiClass : 1;
    WORD fMergeNeutralItems : 1;
    WORD fReserved : 7;
} SCRIPT_CONTROL;

typedef struct tag_SCRIPT_VISATTR
{
    WORD uJustification : 4;
    WORD fClusterStart : 1;
    WORD fDiacritic : 1;
    WORD fZeroWidth : 1;
    WORD fReserved : 1;
    WORD fShapeReserved : 8;
} SCRIPT_VISATTR;

typedef struct tag_GOFFSET
{
    LONG du;
    LONG dv;
} GOFFSET;

typedef struct tag_SCRIPT_LOGATTR
{
    BYTE fSoftBreak : 1;
    BYTE fWhiteSpace : 1;
    BYTE fCharStop : 1;
    BYTE fWordStop : 1;
    BYTE fInvalid : 1;
    BYTE fReserved : 3;
} SCRIPT_LOGATTR;

typedef struct tag_SCRIPT_FONTPROPERTIES
{
    int cBytes;
    WORD wgBlank;
    WORD wgDefault;
    WORD wgInvalid;
    WORD wgKashida;
    int iKashidaWidth;
} SCRIPT_FONTPROPERTIES;

typedef struct tag_SCRIPT_TABDEF
{
    int cTabStops;
    int iScale;
    int *pTabStops;
    int iFirstTabStop;
} SCRIPT_TABDEF;

typedef void *SCRIPT_CACHE;

typedef struct tag_SCRIPT_STRING_ANALYSIS *SCRIPT_STRING_ANALYSIS;

/* ScriptShape/ScriptPlace 的 justification 取值（SCRIPT_VISATTR.uJustification） */
#define SCRIPT_JUSTIFY_NONE             0
#define SCRIPT_JUSTIFY_ARABIC_BLANK     1
#define SCRIPT_JUSTIFY_CHARACTER        2
#define SCRIPT_JUSTIFY_RESERVED1        3
#define SCRIPT_JUSTIFY_BLANK            4
#define SCRIPT_JUSTIFY_ARABIC_NORMAL    5
#define SCRIPT_JUSTIFY_RESERVED2        6
#define SCRIPT_JUSTIFY_RESERVED3        7
#define SCRIPT_JUSTIFY_ARABIC_KASHIDA   8
#define SCRIPT_JUSTIFY_ARABIC_ALEF      9
#define SCRIPT_JUSTIFY_ARABIC_HAMZA     10
#define SCRIPT_JUSTIFY_ARABIC_WAW       11
#define SCRIPT_JUSTIFY_ARABIC_YEH       12
#define SCRIPT_JUSTIFY_ARABIC_ALEFISOLA 13
#define SCRIPT_JUSTIFY_ARABIC_DAL       14
#define SCRIPT_JUSTIFY_ARABIC_KAF       15

/* ScriptItemize 的 SCRIPT_ANALYSIS.eScript 取值 */
#define SCRIPT_UNDEFINED 0

/* ScriptStringAnalyse 的 dwFlags（值取 SDK 的 usp10.h） */
#define SSA_PASSWORD          0x00000001
#define SSA_TAB               0x00000002
#define SSA_CLIP              0x00000004
#define SSA_FIT               0x00000008
#define SSA_DWRITE            0x00000010
#define SSA_GCP               0x00000020
#define SSA_HIDEHOTKEY        0x00000040
#define SSA_HOTKEY            0x00000080
#define SSA_LINK              0x00000100
#define SSA_METAFILE          0x00000200
#define SSA_LPKANSIFALLBACK   0x00000400
#define SSA_PIDX              0x00000800
#define SSA_GLYPHS            0x00001000
#define SSA_RTL               0x00002000
#define SSA_DISABLEVERTLIG    0x00004000
#define SSA_REORDERDIACRITICS 0x00008000
#define SSA_FALLBACK          0x00010000
#define SSA_GLYPHOVERRIDE     0x00020000

#ifdef __cplusplus
extern "C"
{
#endif

    HRESULT WINAPI ScriptItemize(const WCHAR *pwcInChars, int cInChars, int cMaxItems, const SCRIPT_CONTROL *psControl, const SCRIPT_STATE *psState, SCRIPT_ITEM *pItems, int *pcItems);

    HRESULT WINAPI ScriptShape(HDC hdc, SCRIPT_CACHE *psc, const WCHAR *pwcChars, int cChars, int cMaxGlyphs, SCRIPT_ANALYSIS *psa, WORD *pwOutGlyphs, WORD *pwLogClust, SCRIPT_VISATTR *psva, int *pcGlyphs);

    HRESULT WINAPI ScriptPlace(HDC hdc, SCRIPT_CACHE *psc, const WORD *pwGlyphs, int cGlyphs, const SCRIPT_VISATTR *psva, SCRIPT_ANALYSIS *psa, int *piAdvance, GOFFSET *pGoffset, ABC *pABC);

    HRESULT WINAPI ScriptGetFontProperties(HDC hdc, SCRIPT_CACHE *psc, SCRIPT_FONTPROPERTIES *psfp);

    HRESULT WINAPI ScriptFreeCache(SCRIPT_CACHE *psc);

    HRESULT WINAPI ScriptXtoCP(int iX, int cChars, int cGlyphs, const WORD *pwLogClust, const SCRIPT_VISATTR *psva, const int *piAdvance, const SCRIPT_ANALYSIS *psa, int *piCP, int *piTrailing);

    /* pString 在 SDK 里就是 const void*（同一套 API 也服务 ANSI 串），所以这里无法靠
     * 类型拦住调用方：本实现按 WCHAR 逐元素读 cString 个，传 UTF-16 缓冲会静默读错。
     * FontCacheWin::getFontDataForCharacters 用 NativeWideCodeUnits 转换后再传。 */
    HRESULT WINAPI ScriptStringAnalyse(HDC hdc, const void *pString, int cString, int cGlyphs, int iCharset, DWORD dwFlags, int iReserved, SCRIPT_CONTROL *pControl, SCRIPT_STATE *pState, const int *piDx, SCRIPT_TABDEF *pTabdef, const BYTE *pbInClass, SCRIPT_STRING_ANALYSIS *pssa);

    HRESULT WINAPI ScriptStringOut(SCRIPT_STRING_ANALYSIS ssa, int iX, int iY, UINT uOptions, const RECT *prc, int iMinSel, int iMaxSel, BOOL fDisabled);

    HRESULT WINAPI ScriptStringFree(SCRIPT_STRING_ANALYSIS *pssa);

#ifdef __cplusplus
}
#endif

#endif /* _USP10_H_ */
