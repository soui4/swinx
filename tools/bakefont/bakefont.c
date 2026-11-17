/*
 * bakefont.c -- bake a TrueType font into the bitmap font tables the
 * FreeRTOS GDI backend draws with (swinx/src/gdi/freertos/sfr_font_data.h).
 *
 * Why bake instead of linking FreeType on the target:
 *
 *   * the target is a 320x240 / 4 MB-flash class device: FreeType costs
 *     ~300 KB of code plus a glyph cache, the baked tables cost ~5-9 KB per
 *     (em size, weight) face;
 *   * baked advances are integers, so per-character measurement and drawing
 *     agree exactly -- the FreeRTOS port's truncation bug came from a font
 *     with fractional advances (see sfr_font.cpp);
 *   * pixels are deterministic: no hinting difference between host preview
 *     and the panel.
 *
 * The trade-off, stated up front: the baked faces cover only the codepoints
 * you ask for, at the em sizes you ask for (other sizes snap to the nearest
 * baked one and are scaled by cairo).  --sizes is the *em* size, i.e. the
 * same number swinx passes to cairo_set_font_size(abs(lfHeight)); the line
 * box then comes out as the face's ascent+descent (Lato: ~1.08 em), not as
 * the requested height.  To add a script, re-run with
 * a wider --chars range and a font that has those glyphs.
 *
 * Build (host tool, needs FreeType headers + library):
 *
 *   gcc -O2 -o bakefont bakefont.c $(pkg-config --cflags --libs freetype2)
 *
 * Run:
 *
 *   ./bakefont --regular /path/to/Regular.ttf --bold /path/to/Bold.ttf \
 *              --sizes 12,13,14,15,16 --chars 0x20-0x7E \
 *              --out ../../src/gdi/freertos/sfr_font_data.h
 *
 * The generated file is checked in: the target build must not depend on
 * FreeType or on a font file existing on the build machine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define MAX_GLYPHS 4096
#define MAX_SIZES 16
#define MAX_RANGES 16
#define MAX_NAME 64

typedef struct
{
    unsigned first, last;
} Range;

typedef struct
{
    uint32_t code;
    uint8_t w, h;
    int8_t left, top;
    uint8_t adv;
    uint32_t off;
} Glyph;

static FT_Library g_lib;

/* ------------------------------------------------------------------------- */
/* arguments                                                                 */
/* ------------------------------------------------------------------------- */
static int parse_ranges(const char *spec, Range *out, int nMax)
{
    int n = 0;
    const char *p = spec;
    while (*p && n < nMax)
    {
        char *end = NULL;
        unsigned a = (unsigned)strtoul(p, &end, 0);
        if (end == p)
            return -1;
        unsigned b = a;
        p = end;
        if (*p == '-')
        {
            p++;
            b = (unsigned)strtoul(p, &end, 0);
            if (end == p)
                return -1;
            p = end;
        }
        if (b < a || b > 0x10FFFFu)
            return -1;
        out[n].first = a;
        out[n].last = b;
        n++;
        if (*p == ',')
            p++;
        else if (*p)
            return -1;
    }
    return n;
}

static int parse_sizes(const char *spec, int *out, int nMax)
{
    int n = 0;
    const char *p = spec;
    while (*p && n < nMax)
    {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p || v < 6 || v > 96)
            return -1;
        out[n++] = (int)v;
        p = end;
        if (*p == ',')
            p++;
        else if (*p)
            return -1;
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* baking                                                                    */
/* ------------------------------------------------------------------------- */

/* Load a face at exactly the requested pixel size.
 *
 * The size is an *em* size, not a GDI cell height: swinx passes abs(lfHeight)
 * straight to cairo_set_font_size() on every platform, so a "14" request is a
 * 14 px em there.  Baking at the same number keeps this port consistent with
 * the others, and makes the 1:1 (crisp, integer-advance) case exact for the
 * sizes an app actually asks for.  One consequence worth knowing: the line
 * box is the font's ascent+descent (~1.08 em for Lato), not the requested
 * number. */
static FT_Face open_face_pt(const char *path, int px, int *sizeOut, int *ascOut, int *descOut)
{
    FT_Face face = NULL;
    if (FT_New_Face(g_lib, path, 0, &face) != 0)
    {
        fprintf(stderr, "bakefont: cannot open %s\n", path);
        return NULL;
    }
    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0)
    {
        fprintf(stderr, "bakefont: cannot set %d px on %s\n", px, path);
        FT_Done_Face(face);
        return NULL;
    }
    *sizeOut = px;
    *ascOut = (int)(face->size->metrics.ascender >> 6);
    *descOut = -(int)(face->size->metrics.descender >> 6);
    if (*descOut < 0)
        *descOut = 0;
    return face;
}

static int cmp_glyph(const void *a, const void *b)
{
    uint32_t ca = ((const Glyph *)a)->code, cb = ((const Glyph *)b)->code;
    return ca < cb ? -1 : (ca > cb ? 1 : 0);
}

/* Render every requested codepoint at the face's current pixel size. */
static int bake_face(FT_Face face, const Range *ranges, int nRanges,
                     Glyph *glyphs, int *glyphCount, unsigned char **bitsOut, uint32_t *bitsLenOut,
                     unsigned *maxAdvOut)
{
    int n = 0;
    size_t cap = 4096, len = 0;
    unsigned char *bits = (unsigned char *)malloc(cap);
    if (!bits)
        return 0;

    for (int r = 0; r < nRanges; r++)
    {
        for (unsigned cp = ranges[r].first; cp <= ranges[r].last; cp++)
        {
            if (FT_Get_Char_Index(face, (FT_ULong)cp) == 0)
                continue; /* not in this font: leave it to the '?' fallback */
            if (FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER) != 0)
                continue;
            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap *bm = &slot->bitmap;
            if (bm->pixel_mode != FT_PIXEL_MODE_GRAY)
                continue; /* only the 8-bit anti-aliased renderer is understood */

            int w = (int)bm->width, h = (int)bm->rows;
            int adv = (int)(slot->advance.x >> 6);
            if (adv <= 0)
                adv = w;
            if (adv > 255)
                adv = 255;
            if (w > 255 || h > 255)
                continue;
            if (n >= MAX_GLYPHS)
                break;

            size_t need = len + (size_t)w * h;
            if (need > cap)
            {
                while (cap < need)
                    cap *= 2;
                unsigned char *nb = (unsigned char *)realloc(bits, cap);
                if (!nb)
                {
                    free(bits);
                    return 0;
                }
                bits = nb;
            }
            for (int y = 0; y < h; y++)
                memcpy(bits + len + (size_t)y * w, bm->buffer + (size_t)y * bm->pitch, (size_t)w);
            len += (size_t)w * h;

            Glyph *g = &glyphs[n++];
            g->code = cp;
            g->w = (uint8_t)w;
            g->h = (uint8_t)h;
            g->left = (int8_t)slot->bitmap_left;
            g->top = (int8_t)slot->bitmap_top;
            g->adv = (uint8_t)adv;
            g->off = (uint32_t)(len - (size_t)w * h);
            if ((unsigned)adv > *maxAdvOut)
                *maxAdvOut = (unsigned)adv;
        }
    }

    qsort(glyphs, (size_t)n, sizeof(*glyphs), cmp_glyph);
    /* sorting invalidates the offsets unless we move the pixels with them;
     * bake order is already ascending by codepoint, so the sort is a no-op
     * guard -- assert it stays that way instead of re-basing offsets. */
    for (int i = 1; i < n; i++)
    {
        if (glyphs[i - 1].code >= glyphs[i].code)
        {
            fprintf(stderr, "bakefont: glyph order broken at %u\n", glyphs[i].code);
            free(bits);
            return 0;
        }
    }

    *glyphCount = n;
    *bitsOut = bits;
    *bitsLenOut = (uint32_t)len;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* output                                                                    */
/* ------------------------------------------------------------------------- */
typedef struct
{
    int cell;       /* requested lfHeight */
    int px;         /* FT pixel size actually used */
    int ascent, descent;
    int bold;
    int glyphCount;
    unsigned maxAdv;
    uint32_t bitsLen;
    unsigned char *bits;
    Glyph *glyphs;
} BakedFace;

static void emit_bytes(FILE *f, const unsigned char *p, uint32_t n, int indent)
{
    for (uint32_t i = 0; i < n; i++)
    {
        if (i % 20 == 0)
            fprintf(f, "%*s", indent, "");
        fprintf(f, "0x%02x,", p[i]);
        if (i % 20 == 19 || i + 1 == n)
            fprintf(f, "\n");
    }
}

static const char *base_name(const char *path)
{
    const char *p = path, *ret = path;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
            ret = p + 1;
        p++;
    }
    return ret;
}

static int write_header(const char *path, const char *fontName, const char *latinName,
                        const char *boldName, BakedFace *faces, int nFaces)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "bakefont: cannot write %s\n", path);
        return 0;
    }
    fprintf(f,
            "/*\n"
            " * sfr_font_data.h -- GENERATED by swinx/tools/bakefont/bakefont.c, do not edit.\n"
            " *\n"
            " * Baked from \"%s\"%s%s%s\n"
            " * (SIL Open Font License 1.1).  These tables are a *rendering* of the\n"
            " * typeface at fixed cell heights -- the font software itself is not shipped and\n"
            " * is not needed at build or run time.  Re-bake with a wider --chars range (and a\n"
            " * font that covers the script) to add glyphs; see tools/bakefont/README.md.\n"
            " *\n"
            " * Layout of one face: %d glyphs, %u bytes of 8-bit alpha, sorted by codepoint.\n"
            " * Every advance is an integer pixel count, which is what makes measuring and\n"
            " * drawing agree on this target (see sfr_font.cpp).\n"
            " */\n"
            "#ifndef SFR_FONT_DATA_H\n"
            "#define SFR_FONT_DATA_H\n"
            "\n"
            "#include <stdint.h>\n"
            "\n"
            "/* One glyph.  w/h == 0 means \"no ink\" (space): only adv matters. */\n"
            "typedef struct SfrBakedGlyph\n"
            "{\n"
            "    uint16_t code;   /* unicode codepoint */\n"
            "    uint8_t w, h;    /* bitmap size in px */\n"
            "    int8_t left;     /* bitmap_left: pen x -> bitmap x */\n"
            "    int8_t top;      /* bitmap_top: baseline -> bitmap top */\n"
            "    uint8_t adv;     /* advance width in px (always >= 1) */\n"
            "    uint32_t off;    /* byte offset into the face's bits[] */\n"
            "} SfrBakedGlyph;\n"
            "\n"
            "/* One (pixel size, weight) pair. */\n"
            "typedef struct SfrBakedFace\n"
            "{\n"
            "    uint16_t px;        /* cairo font size == abs(lfHeight) == em px */\n"
            "    int8_t ascent;      /* px above the baseline */\n"
            "    int8_t descent;     /* px below the baseline (positive) */\n"
            "    uint8_t bold;\n"
            "    uint16_t glyphCount;\n"
            "    uint8_t maxAdv;\n"
            "    uint32_t bitsLen;\n"
            "    const SfrBakedGlyph *glyphs;\n"
            "    const uint8_t *bits;\n"
            "} SfrBakedFace;\n"
            "\n",
            base_name(latinName), boldName ? " and \"" : "", boldName ? base_name(boldName) : "", boldName ? "\"" : "",
            faces[0].glyphCount, faces[0].bitsLen);

    for (int i = 0; i < nFaces; i++)
    {
        BakedFace *bf = &faces[i];
        fprintf(f, "/* %d px, ascent %d / descent %d, weight %s, %d glyphs */\n",
                bf->px, bf->ascent, bf->descent, bf->bold ? "bold" : "regular", bf->glyphCount);
        fprintf(f, "static const uint8_t sfr_bits_%d[] = {\n", i);
        emit_bytes(f, bf->bits, bf->bitsLen, 4);
        fprintf(f, "};\n\n");
        fprintf(f, "static const SfrBakedGlyph sfr_glyphs_%d[] = {\n", i);
        for (int g = 0; g < bf->glyphCount; g++)
        {
            Glyph *gl = &bf->glyphs[g];
            fprintf(f, "    { 0x%04x, %3u, %3u, %3d, %3d, %3u, %6uu },\n",
                    gl->code, gl->w, gl->h, gl->left, gl->top, gl->adv, gl->off);
        }
        fprintf(f, "};\n\n");
    }

    fprintf(f, "#define SFR_BAKED_FACE_COUNT %d\n\n", nFaces);
    fprintf(f, "static const SfrBakedFace sfr_baked_faces[SFR_BAKED_FACE_COUNT] = {\n");
    for (int i = 0; i < nFaces; i++)
    {
        BakedFace *bf = &faces[i];
        fprintf(f, "    { %u, %d, %d, %u, %d, %u, %uu, sfr_glyphs_%d, sfr_bits_%d },\n",
                (unsigned)bf->cell, bf->ascent, bf->descent, (unsigned)bf->bold,
                bf->glyphCount, bf->maxAdv, bf->bitsLen, i, i);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "#endif /* SFR_FONT_DATA_H */\n");
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    const char *regular = NULL, *bold = NULL, *out = NULL, *chars = "0x20-0x7E", *name = "SfrSans";
    int sizes[MAX_SIZES];
    int nSizes = 0;
    Range ranges[MAX_RANGES];
    int nRanges = 1;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--regular") && i + 1 < argc)
            regular = argv[++i];
        else if (!strcmp(argv[i], "--bold") && i + 1 < argc)
            bold = argv[++i];
        else if (!strcmp(argv[i], "--sizes") && i + 1 < argc)
        {
            nSizes = parse_sizes(argv[++i], sizes, MAX_SIZES);
            if (nSizes <= 0)
            {
                fprintf(stderr, "bakefont: bad --sizes\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--chars") && i + 1 < argc)
            chars = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc)
            name = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else
        {
            fprintf(stderr, "bakefont: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!regular || !out || !nSizes)
    {
        fprintf(stderr,
                "usage: bakefont --regular <ttf> [--bold <ttf>] --sizes 12,13,14,15,16\n"
                "                [--chars 0x20-0x7E[,0x4E00-0x9FFF]] [--name SfrSans] --out <header>\n");
        return 2;
    }
    nRanges = parse_ranges(chars, ranges, MAX_RANGES);
    if (nRanges <= 0)
    {
        fprintf(stderr, "bakefont: bad --chars\n");
        return 2;
    }
    if (FT_Init_FreeType(&g_lib) != 0)
    {
        fprintf(stderr, "bakefont: FreeType init failed\n");
        return 1;
    }

    BakedFace faces[MAX_SIZES * 2];
    int nFaces = 0;
    const char *sources[2] = { regular, bold };
    for (int w = 0; w < 2; w++)
    {
        if (!sources[w])
            continue;
        for (int i = 0; i < nSizes; i++)
        {
            int px = 0, asc = 0, desc = 0;
            FT_Face face = open_face_pt(sources[w], sizes[i], &px, &asc, &desc);
            if (!face)
                return 1;
            Glyph *glyphs = (Glyph *)calloc(MAX_GLYPHS, sizeof(Glyph));
            unsigned char *bits = NULL;
            uint32_t bitsLen = 0;
            int count = 0;
            unsigned maxAdv = 0;
            if (!bake_face(face, ranges, nRanges, glyphs, &count, &bits, &bitsLen, &maxAdv))
            {
                fprintf(stderr, "bakefont: bake failed for %s @ cell %d\n", sources[w], sizes[i]);
                FT_Done_Face(face);
                return 1;
            }
            FT_Done_Face(face);
            BakedFace *bf = &faces[nFaces++];
            bf->cell = sizes[i];
            bf->px = px;
            bf->ascent = asc;
            bf->descent = desc;
            bf->bold = (w == 1);
            bf->glyphCount = count;
            bf->maxAdv = maxAdv;
            bf->bits = bits;
            bf->bitsLen = bitsLen;
            bf->glyphs = glyphs;
            fprintf(stderr, "bakefont: %s %2d px, asc %2d desc %2d, %3d glyphs, %6u B\n",
                    bf->bold ? "bold   " : "regular", bf->px, bf->ascent, bf->descent,
                    bf->glyphCount, bf->bitsLen);
        }
    }

    int ok = write_header(out, name, regular, bold, faces, nFaces);
    FT_Done_FreeType(g_lib);
    if (!ok)
        return 1;
    fprintf(stderr, "bakefont: wrote %s (%d faces)\n", out, nFaces);
    return 0;
}
