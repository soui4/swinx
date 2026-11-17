/*
 * sfr_font.cpp -- the baked bitmap font, presented to cairo as a user font.
 *
 * See sfr_font.h for why the port bakes instead of linking FreeType.  What
 * follows is the mechanism, and the three details that make it behave:
 *
 *   * one cairo_font_face_t per weight, sized by cairo_set_font_size().  The
 *     face callbacks resolve the baked table from that font size, so a single
 *     face answers every size an app asks for (nearest baked size wins, and
 *     cairo scales the difference);
 *   * render_glyph returns metrics in *unscaled* units (dividing by the font
 *     size) because cairo multiplies them by the font matrix -- this is the
 *     same convention cairo's own "twin" font uses (cairo-font-face-twin.c:
 *     metrics->ascent = F(54) with F(g) = g/72);
 *   * the recording context handed to render_glyph is set up with the
 *     scaled font's matrix, so one user-space unit is `size` device pixels.
 *     Drawing the baked bitmap at scale 1/px therefore lands one bitmap pixel
 *     on one device pixel when the requested size equals the baked size, and
 *     scales proportionally otherwise.
 *
 * Glyph masks are cached per (face, glyph) as A8 surfaces: a 320x240 UI
 * redraws the same few dozen glyphs every frame, and re-uploading the bitmap
 * per draw would put a malloc in the paint path.
 */
#include <string.h>
#include <stdlib.h>

#include <cairo.h>

#include "sfr_font.h"
#include "sfr_font_data.h"

namespace
{

/* sfr_font.h exposes SfrFontFace as opaque so the (static) tables stay in
 * this translation unit; the cast is just that. */
inline const SfrBakedFace *asBaked(const SfrFontFace *face)
{
    return reinterpret_cast<const SfrBakedFace *>(face);
}

/* '?' is the missing-glyph stand-in; it is the first thing an app notices
 * when a string uses a script the baked tables do not cover, which is the
 * point -- silently dropping characters is worse. */
const unsigned kFallbackCodepoint = '?';

const SfrBakedFace *find_face(int px, int bold)
{
    const SfrBakedFace *best = NULL;
    int bestDiff = 0x7FFFFFFF;
    for (int pass = 0; pass < 2 && !best; pass++)
    {
        for (int i = 0; i < SFR_BAKED_FACE_COUNT; i++)
        {
            const SfrBakedFace *f = &sfr_baked_faces[i];
            /* pass 0: the requested weight only; pass 1: any weight (a
             * bold-only table should still render regular text) */
            if (pass == 0 && f->bold != (bold ? 1 : 0))
                continue;
            int diff = (int)f->px - px;
            if (diff < 0)
                diff = -diff;
            /* ties go to the larger face: shrinking a glyph blurs it, growing
             * one only wastes a pixel of line box */
            if (diff < bestDiff || (diff == bestDiff && best && f->px > best->px))
            {
                bestDiff = diff;
                best = f;
            }
        }
    }
    return best;
}

int face_index(const SfrBakedFace *face)
{
    return (int)(face - sfr_baked_faces);
}

/* binary search: the generator emits glyphs sorted by codepoint */
int find_glyph(const SfrBakedFace *face, unsigned int code)
{
    int lo = 0, hi = (int)face->glyphCount - 1;
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        unsigned int c = face->glyphs[mid].code;
        if (c == code)
            return mid;
        if (c < code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

cairo_surface_t **g_glyphMasks[SFR_BAKED_FACE_COUNT];
cairo_font_face_t *g_cairoFaces[2];
cairo_user_data_key_t g_faceKey;   /* weight binding */
cairo_user_data_key_t g_scaledKey; /* resolved face, cached on the scaled font */

struct WeightBinding
{
    int bold;
};
WeightBinding g_bindings[2];

/* A8 mask for one glyph: the baked alpha block, one bitmap pixel per byte. */
cairo_surface_t *glyph_mask(int fi, int gi)
{
    if (!g_glyphMasks[fi])
    {
        const SfrBakedFace *face = &sfr_baked_faces[fi];
        g_glyphMasks[fi] = (cairo_surface_t **)calloc(face->glyphCount, sizeof(cairo_surface_t *));
        if (!g_glyphMasks[fi])
            return NULL;
    }
    if (g_glyphMasks[fi][gi])
        return g_glyphMasks[fi][gi];

    const SfrBakedFace *face = &sfr_baked_faces[fi];
    const SfrBakedGlyph *g = &face->glyphs[gi];
    if (!g->w || !g->h)
        return NULL; /* no ink (space): nothing to cache, nothing to draw */

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, g->w, g->h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(surf);
        return NULL;
    }
    /* row by row: cairo's stride is aligned, the baked block is tight */
    unsigned char *dst = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    const unsigned char *src = face->bits + g->off;
    for (int y = 0; y < g->h; y++)
        memcpy(dst + (size_t)y * stride, src + (size_t)y * g->w, g->w);
    cairo_surface_mark_dirty(surf);
    g_glyphMasks[fi][gi] = surf;
    return surf;
}

/* cairo hands the user font the size through the font matrix; uniform scaling
 * is what every GDI consumer does, so xx is the size. */
double font_px(cairo_scaled_font_t *sf)
{
    cairo_matrix_t m;
    cairo_scaled_font_get_font_matrix(sf, &m);
    double s = m.xx;
    return (s < 1.0) ? 1.0 : s;
}

const SfrBakedFace *scaled_face(cairo_scaled_font_t *sf)
{
    const SfrBakedFace *face = (const SfrBakedFace *)cairo_scaled_font_get_user_data(sf, &g_scaledKey);
    if (face)
        return face;
    const WeightBinding *b = (const WeightBinding *)cairo_font_face_get_user_data(
        cairo_scaled_font_get_font_face(sf), &g_faceKey);
    face = find_face((int)(font_px(sf) + 0.5), b ? b->bold : 0);
    if (face)
        cairo_scaled_font_set_user_data(sf, &g_scaledKey, (void *)face, NULL);
    return face;
}

cairo_status_t on_init(cairo_scaled_font_t *sf, cairo_t *cr, cairo_font_extents_t *ext)
{
    (void)cr;
    const SfrBakedFace *face = scaled_face(sf);
    if (!face)
        return CAIRO_STATUS_USER_FONT_NOT_IMPLEMENTED;
    double px = font_px(sf);
    ext->ascent = face->ascent / px;
    ext->descent = face->descent / px;
    ext->height = ext->ascent + ext->descent;
    ext->max_x_advance = face->maxAdv / px;
    ext->max_y_advance = 0.0;
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t on_render_glyph(cairo_scaled_font_t *sf, unsigned long glyph, cairo_t *cr,
                               cairo_text_extents_t *ext)
{
    const SfrBakedFace *face = scaled_face(sf);
    memset(ext, 0, sizeof(*ext));
    if (!face)
        return CAIRO_STATUS_USER_FONT_NOT_IMPLEMENTED;
    if (glyph >= face->glyphCount)
        return CAIRO_STATUS_SUCCESS; /* nothing to draw, zero advance */

    double px = font_px(sf);
    const SfrBakedGlyph *g = &face->glyphs[glyph];
    /* unscaled units: cairo multiplies by the font matrix */
    ext->x_advance = g->adv / px;
    ext->y_advance = 0.0;
    ext->x_bearing = g->left / px;
    ext->y_bearing = -(double)g->top / px;
    ext->width = g->w / px;
    ext->height = g->h / px;

    if (!g->w || !g->h)
        return CAIRO_STATUS_SUCCESS;
    cairo_surface_t *mask = glyph_mask(face_index(face), (int)glyph);
    if (!mask)
        return CAIRO_STATUS_SUCCESS;

    /* The recording context is in font space (1 unit == px device pixels), so
     * 1/px puts one bitmap pixel on one device pixel at the baked size. */
    cairo_save(cr);
    cairo_scale(cr, 1.0 / face->px, 1.0 / face->px);
    /* mask (not paint): the caller's source, i.e. the text colour, is kept */
    cairo_mask_surface(cr, mask, g->left, -g->top);
    cairo_restore(cr);
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t on_unicode_to_glyph(cairo_scaled_font_t *sf, unsigned long ucs4, unsigned long *index)
{
    const SfrBakedFace *face = scaled_face(sf);
    if (!face)
        return CAIRO_STATUS_USER_FONT_NOT_IMPLEMENTED;
    int gi = find_glyph(face, (unsigned int)ucs4);
    if (gi < 0)
        gi = find_glyph(face, kFallbackCodepoint);
    *index = (gi < 0) ? 0 : (unsigned long)gi;
    return CAIRO_STATUS_SUCCESS;
}

} // namespace

/* ------------------------------------------------------------------------- */
/* public surface                                                            */
/* ------------------------------------------------------------------------- */

const SfrFontFace *SfrFontFaceFor(int px, int bold)
{
    return (const SfrFontFace *)find_face(px, bold);
}

int SfrFontGlyph(const SfrFontFace *face, unsigned int codepoint)
{
    return face ? find_glyph(asBaked(face), codepoint) : -1;
}

int SfrFontAdvance(const SfrFontFace *face, int glyph)
{
    if (!face || glyph < 0 || glyph >= (int)asBaked(face)->glyphCount)
        return 0;
    return asBaked(face)->glyphs[glyph].adv;
}

int SfrFontAscent(const SfrFontFace *face)
{
    return face ? asBaked(face)->ascent : 0;
}

int SfrFontDescent(const SfrFontFace *face)
{
    return face ? asBaked(face)->descent : 0;
}

int SfrFontPx(const SfrFontFace *face)
{
    return face ? asBaked(face)->px : 0;
}

cairo_font_face_t *SfrFontCairoFace(int bold)
{
    int slot = bold ? 1 : 0;
    if (g_cairoFaces[slot])
        return g_cairoFaces[slot];

    cairo_font_face_t *face = cairo_user_font_face_create();
    if (cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS)
    {
        cairo_font_face_destroy(face);
        return NULL;
    }
    g_bindings[slot].bold = slot;
    cairo_user_font_face_set_init_func(face, on_init);
    cairo_user_font_face_set_render_glyph_func(face, on_render_glyph);
    cairo_user_font_face_set_unicode_to_glyph_func(face, on_unicode_to_glyph);
    cairo_font_face_set_user_data(face, &g_faceKey, &g_bindings[slot], NULL);
    g_cairoFaces[slot] = face;
    return face;
}

void SfrFontShutdown(void)
{
    for (int i = 0; i < SFR_BAKED_FACE_COUNT; i++)
    {
        if (!g_glyphMasks[i])
            continue;
        for (int g = 0; g < (int)sfr_baked_faces[i].glyphCount; g++)
        {
            if (g_glyphMasks[i][g])
                cairo_surface_destroy(g_glyphMasks[i][g]);
        }
        free(g_glyphMasks[i]);
        g_glyphMasks[i] = NULL;
    }
    for (int i = 0; i < 2; i++)
    {
        if (g_cairoFaces[i])
            cairo_font_face_destroy(g_cairoFaces[i]);
        g_cairoFaces[i] = NULL;
    }
}
