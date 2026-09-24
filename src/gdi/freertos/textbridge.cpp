/*
 * textbridge.cpp -- FreeRTOS font-layer bridge for the gdi/cairo backend.
 *
 * The cairo GDI backend (src/gdi/cairo) resolves text fonts through
 * fontconfig + freetype (FontFallback.cpp).  FreeRTOS has no font
 * filesystem and no fontconfig cache, so this bridge implements the same
 * FontFallback.h surface with honest FreeRTOS semantics:
 *
 *   - SwinXGetCachedFontFace() returns NULL, which makes gdi.cpp's
 *     ApplyFont() fall back to cairo's built-in toy font face;
 *   - the fallback chain is disabled (single-font behaviour);
 *   - text splitting always yields the single primary-font run.
 *
 * With every CAIRO_HAS_*_FONT feature disabled, cairo's toy face resolves
 * through no render backend, so show_text() fails gracefully with
 * CAIRO_STATUS_FONT_TYPE... -- i.e. text drawing is a no-op until a
 * FreeRTOS font source (builtin bitmap font / embedded TTF) lands.
 */
#include <windows.h>
#include <cairo.h>
#include "FontFallback.h"

cairo_font_face_t *SwinXGetCachedFontFace(const LOGFONTA *lf)
{
    (void)lf;
    return NULL; // -> ApplyFont() uses cairo_select_font_face (toy face)
}

void SwinXFontFallbackPrefetch()
{
    // no system fonts to enumerate
}

void SwinXFontFallbackShutdown()
{
    // nothing cached, nothing to release
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
    // fallback chain disabled: single-font behaviour
}

void SplitTextRuns(cairo_t *cr, const char *utf8, int len, swinx_stl::vector<TextRun> &runs)
{
    (void)cr;
    runs.clear();
    TextRun run;
    run.offset = 0;
    run.len = len;
    run.scaled = NULL; // NULL -> use the context's current (primary) font
    runs.push_back(run);
}
