/*
 * textbridge.cpp -- FreeRTOS font-layer bridge for the gdi/cairo backend.
 *
 * The cairo GDI backend (src/gdi/cairo) resolves text fonts through
 * fontconfig + freetype (FontFallback.cpp).  FreeRTOS has neither a font
 * filesystem nor a fontconfig cache, so this bridge implements the same
 * FontFallback.h surface on top of the port's *baked* bitmap font
 * (src/gdi/freertos/sfr_font.cpp, tables in sfr_font_data.h):
 *
 *   - SwinXGetCachedFontFace() returns a cairo user font face bound to the
 *     baked tables, so ApplyFont() in gdi.cpp needs no special case and every
 *     text path (measure, draw, path) reads the same advances;
 *   - the fallback chain stays disabled (single font family, no script
 *     fallback beyond the '?' stand-in) because there is nothing to fall back
 *     *to* on a board with one baked typeface;
 *   - text splitting yields the single primary-font run for the same reason.
 *
 * Consequences worth stating plainly:
 *
 *   * a codepoint that is not in the baked tables renders as '?'.  The demo
 *     UI is ASCII; an app that needs CJK re-bakes the tables with a wider
 *     --chars range (tools/bakefont/README.md), which is a data-size decision
 *     the app has to make anyway;
 *   * bold comes from a separate baked table, not a synthetic stroke, so the
 *     advance of a bold run is the typeface's own;
 *   * italic / underline / strikeout are left to the caller's geometry -- the
 *     baked tables carry upright glyphs only.
 */
#include <windows.h>
#include <cairo.h>
#include "FontFallback.h"
#include "sfr_font.h"

/* The size a face lookup should assume when nothing better is known. 14 is
 * the system default this port's demos configure (uidef/init.xml deffont). */
#define SFR_FONT_DEFAULT_PX 14

cairo_font_face_t *SwinXGetCachedFontFace(const LOGFONTA *lf)
{
    if (!lf)
        return NULL;
    /* Win32 weights: 400 normal, 700 bold. 500/600 are "medium"/"semibold"
     * and read closer to bold on a 320x240 panel, so the cut is at 500. */
    int bold = (lf->lfWeight >= 500) ? 1 : 0;
    cairo_font_face_t *face = SfrFontCairoFace(bold);
    return face ? cairo_font_face_reference(face) : NULL;
}

void SwinXFontFallbackPrefetch()
{
    // no system fonts to enumerate
}

void SwinXFontFallbackShutdown()
{
    SfrFontShutdown();
}

bool SwinXIsCjkFontAlias(const char *faceName)
{
    (void)faceName;
    return false;
}

FcPattern *BuildFontMatchPattern(const LOGFONTA *lf, bool zhHint,
                                 const char *const *extraFamilies, int nExtraFamilies,
                                 bool zhCharsetHint)
{
    (void)lf;
    (void)zhHint;
    (void)extraFamilies;
    (void)nExtraFamilies;
    (void)zhCharsetHint;
    return NULL; // no fontconfig on FreeRTOS
}

void AttachFontFallback(cairo_t *cr, const LOGFONTA *lf)
{
    (void)cr;
    (void)lf;
    // fallback chain disabled: single baked family
}

void SplitTextRuns(cairo_t *cr, const char *utf8, int len, std::vector<TextRun> &runs)
{
    (void)cr;
    runs.clear();
    TextRun run;
    run.offset = 0;
    run.len = len;
    run.scaled = NULL; // NULL -> use the context's current (primary) font
    runs.push_back(run);
}

/*
 * Glyph-index API.  gdi.cpp's GetTextMetricsA() uses this pair only to decide
 * the TMPF_FIXED_PITCH bit (a WebKit-era heuristic), but answering it honestly
 * costs nothing now that the glyph tables are in-process: indices are the
 * baked table indices, and the advances come from the same table the renderer
 * uses.  Both are stable across sizes (the table is per-weight, not per-size),
 * so the equality test the probe performs keeps its meaning.
 */
namespace
{
/* NOTE: this deliberately does *not* ask the DC for its font.
 * gdi.cpp's GetTextMetricsA() calls GetGlyphIndicesW/GetCharWidthI to compute
 * the TMPF_FIXED_PITCH bit, so querying the DC from here (GetTextMetricsA on
 * the same HDC) is unbounded recursion -- it was, until this comment existed.
 * Nothing is lost: the glyph table is per-weight, not per-size, so which
 * codepoints exist and whether their advances are equal does not depend on the
 * size the DC happens to use.  Only the absolute widths are the default
 * size's, and the probe compares equality. */
const SfrFontFace *glyphFace()
{
    return SfrFontFaceFor(SFR_FONT_DEFAULT_PX, 0);
}
} // namespace

DWORD WINAPI GetGlyphIndicesW(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl)
{
    (void)hdc; // the codepoint set is the same in every baked face
    if (!lpstr || !pgi || c < 0)
        return GDI_ERROR;
    const SfrFontFace *face = glyphFace();
    if (!face)
        return GDI_ERROR;
    for (int i = 0; i < c; i++)
    {
        int gi = SfrFontGlyph(face, (unsigned)lpstr[i]);
        if (gi < 0)
            gi = (fl & GGI_MARK_NONEXISTING_GLYPHS) ? 0xFFFF : 0; // 0 == notdef, like gdi32
        pgi[i] = (WORD)gi;
    }
    return (DWORD)c;
}

BOOL WINAPI GetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT lpBuffer)
{
    if (!lpBuffer || (int)cgi < 0)
        return FALSE;
    (void)hdc;
    const SfrFontFace *face = glyphFace();
    if (!face)
        return FALSE;
    for (UINT i = 0; i < cgi; i++)
    {
        int gi = pgi ? (int)pgi[i] : (int)(giFirst + i);
        lpBuffer[i] = SfrFontAdvance(face, gi);
    }
    return TRUE;
}
