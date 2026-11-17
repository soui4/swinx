/*
 * sfr_font.h -- bitmapped UI font for the FreeRTOS port.
 *
 * This port ships a *baked* font instead of linking FreeType: the tables in
 * sfr_font_data.h are 8-bit alpha renderings of a typeface at a handful of
 * pixel sizes, produced at development time by tools/bakefont (see the
 * README next to it).  Two properties matter and neither is an accident:
 *
 *   1. advances are integers, and
 *   2. measuring and drawing read the *same* table.
 *
 * Together they are what fixes this port's long-standing "GetTextExtent is
 * narrower than what gets drawn" bug: SOUI's text layout accumulates the
 * width of one word/character at a time (SOUI/src/control/SCmnCtrl.cpp:187
 * and :223), so any fractional advance is truncated once per run and the
 * line ends up wider than the box it was laid out for.  A cairo toy font
 * (the previous state of this port) has fractional advances; a baked bitmap
 * face does not.
 *
 * The typeface is exposed to cairo as a user font face, so every text path
 * (show_text, glyph extents, text_path, ...) goes through the same glyph
 * table and the same advances.
 */
#ifndef SFR_FONT_H
#define SFR_FONT_H

#include <cairo.h>

/**
 * A baked (weight, pixel size) face.  Opaque: the tables live in
 * sfr_font_data.h, which only sfr_font.cpp includes -- the arrays are
 * `static` there, so including the data header in more than one translation
 * unit would duplicate ~64 KB of glyphs into flash.
 */
struct SfrFontFace;

/** Nearest baked face for a cairo font size (= abs(lfHeight)) and weight. */
const struct SfrFontFace *SfrFontFaceFor(int px, int bold);

/** Glyph index for a codepoint, or -1 when the face has no such glyph. */
int SfrFontGlyph(const struct SfrFontFace *face, unsigned int codepoint);

/** Advance width of a glyph index, in whole pixels. */
int SfrFontAdvance(const struct SfrFontFace *face, int glyph);

/** Baseline distance above / below, in pixels (ascent + descent = line box). */
int SfrFontAscent(const struct SfrFontFace *face);
int SfrFontDescent(const struct SfrFontFace *face);

/** The em size this face was baked at. */
int SfrFontPx(const struct SfrFontFace *face);

/**
 * Cairo font face for a weight, created on first use and cached.  The
 * returned face is *borrowed*: do not destroy it.  Size comes from
 * cairo_set_font_size(), so one face serves every size.
 */
cairo_font_face_t *SfrFontCairoFace(int bold);

/** Release the cairo faces and the cached glyph masks (idle-time only). */
void SfrFontShutdown(void);

#endif /* SFR_FONT_H */
