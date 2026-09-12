#include <windows.h>
#include <strapi.h>
#include "cairo_show_text2.h"
#include "FontFallback.h"
#include <assert.h>
#include <string.h>
#include <vector>
/* Size in bytes of the buffer to use off the stack per functions.
 * Mostly used by text functions.  For larger allocations, they'll
 * malloc(). */
#ifndef CAIRO_STACK_BUFFER_SIZE
#define CAIRO_STACK_BUFFER_SIZE (512 * sizeof(int))
#endif

#define CAIRO_STACK_ARRAY_LENGTH(T) (CAIRO_STACK_BUFFER_SIZE / sizeof(T))

#undef ARRAY_LENGTH
#define ARRAY_LENGTH(__array) ((int)(sizeof(__array) / sizeof(__array[0])))

void cairo_draw_line(cairo_t *cr, float x0, float y0, float x1, float y1)
{
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
}

// Shape one run of text with `font` at pen position (x, y) and append the
// positioned glyph values to outGlyphs.  Advances x/y by the run's total
// advance.  Returns the number of glyphs appended.
static int ShapeAppend(cairo_scaled_font_t *font, const char *str, int len,
                       double &x, double &y, std::vector<cairo_glyph_t> &outGlyphs)
{
    cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
    cairo_glyph_t *glyphs = stack_glyphs;
    int num_glyphs = ARRAY_LENGTH(stack_glyphs);
    cairo_status_t status = cairo_scaled_font_text_to_glyphs(font, x, y, str, len, &glyphs, &num_glyphs, NULL, NULL, NULL);
    if (status != CAIRO_STATUS_SUCCESS || num_glyphs <= 0)
    {
        if (glyphs && glyphs != stack_glyphs)
            cairo_glyph_free(glyphs);
        return 0;
    }
    cairo_text_extents_t ext;
    cairo_scaled_font_glyph_extents(font, glyphs + num_glyphs - 1, 1, &ext);
    x = glyphs[num_glyphs - 1].x + ext.x_advance;
    y = glyphs[num_glyphs - 1].y + ext.y_advance;
    outGlyphs.insert(outGlyphs.end(), glyphs, glyphs + num_glyphs);
    if (glyphs != stack_glyphs)
        cairo_glyph_free(glyphs);
    return num_glyphs;
}

size_t cairo_break_text(cairo_t *cr, const char *utf8, size_t length, float maxWidth)
{
    std::vector<TextRun> runs;
    SplitTextRuns(cr, utf8, (int)length, runs);
    cairo_scaled_font_t *primary = cairo_get_scaled_font(cr);
    cairo_text_extents_t extents;
    size_t ret = 0;
    float wid = 0.f;
    const char *p1 = utf8;
    const char *p2 = utf8 + length;
    size_t runIdx = 0;
    while (p1 < p2 && wid < maxWidth)
    {
        const char *next = (const char *)_mbsinc((const uint8_t *)p1);
        int len = (int)(next - p1);
        // locate the run containing p1 and shape with its font
        cairo_scaled_font_t *font = primary;
        while (runIdx < runs.size() && utf8 + runs[runIdx].offset + runs[runIdx].len <= p1)
            runIdx++;
        if (runIdx < runs.size() && p1 >= utf8 + runs[runIdx].offset && runs[runIdx].scaled)
            font = runs[runIdx].scaled;
        int num_glyphs = 1;
        cairo_glyph_t stack_glyphs[5];
        cairo_glyph_t *glyphs = stack_glyphs;
        cairo_scaled_font_text_to_glyphs(font, 0, 0, p1, len, &glyphs, &num_glyphs, NULL, NULL, NULL);
        if (num_glyphs)
        {
            assert(num_glyphs == 1);
            assert(glyphs == stack_glyphs);
            cairo_scaled_font_glyph_extents(font, glyphs, num_glyphs, &extents);
            wid += extents.x_advance;
            if (glyphs != stack_glyphs)
            {
                cairo_glyph_free(glyphs);
            }
        }
        ret += len;
        p1 = next;
    }
    return ret;
}

void cairo_show_text2(cairo_t *cr, const char *text, int len)
{
    cairo_text_extents_t extents;
    cairo_scaled_font_t *primary, *font;
    cairo_glyph_t *glyphs;
    cairo_text_cluster_t *clusters;
    cairo_bool_t has_show_text_glyphs;
    double x, y;
    int num_glyphs, num_clusters;
    cairo_text_cluster_flags_t cluster_flags;

    if (len < 0)
        len = strlen(text);
    if (len <= 0)
        return;

    std::vector<TextRun> runs;
    SplitTextRuns(cr, text, len, runs);

    has_show_text_glyphs = cairo_surface_has_show_text_glyphs(cairo_get_target(cr));
    cairo_get_current_point(cr, &x, &y);
    primary = cairo_get_scaled_font(cr);
    cairo_scaled_font_reference(primary); // survive set_scaled_font of fallbacks

    for (size_t r = 0; r < runs.size(); r++)
    {
        font = runs[r].scaled ? runs[r].scaled : primary;
        cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
        glyphs = stack_glyphs;
        num_glyphs = ARRAY_LENGTH(stack_glyphs);
        if (has_show_text_glyphs)
        {
            cairo_text_cluster_t stack_clusters[CAIRO_STACK_ARRAY_LENGTH(cairo_text_cluster_t)];
            clusters = stack_clusters;
            num_clusters = ARRAY_LENGTH(stack_clusters);
            cairo_status_t status = cairo_scaled_font_text_to_glyphs(font, x, y, text + runs[r].offset, runs[r].len, &glyphs, &num_glyphs, &clusters, &num_clusters, &cluster_flags);
            if (status == CAIRO_STATUS_SUCCESS && num_glyphs)
            {
                cairo_set_scaled_font(cr, font);
                cairo_show_text_glyphs(cr, nullptr, 0, glyphs, num_glyphs, clusters, num_clusters, cluster_flags);
                cairo_glyph_extents(cr, glyphs + num_glyphs - 1, 1, &extents);
                x = glyphs[num_glyphs - 1].x + extents.x_advance;
                y = glyphs[num_glyphs - 1].y + extents.y_advance;
            }
            if (glyphs && glyphs != stack_glyphs)
                cairo_glyph_free(glyphs);
            if (clusters && clusters != stack_clusters)
                cairo_text_cluster_free(clusters);
        }
        else
        {
            clusters = NULL;
            num_clusters = 0;
            cairo_status_t status = cairo_scaled_font_text_to_glyphs(font, x, y, text + runs[r].offset, runs[r].len, &glyphs, &num_glyphs, NULL, NULL, NULL);
            if (status == CAIRO_STATUS_SUCCESS && num_glyphs)
            {
                cairo_set_scaled_font(cr, font);
                cairo_show_glyphs(cr, glyphs, num_glyphs);
                cairo_glyph_extents(cr, glyphs + num_glyphs - 1, 1, &extents);
                x = glyphs[num_glyphs - 1].x + extents.x_advance;
                y = glyphs[num_glyphs - 1].y + extents.y_advance;
            }
            if (glyphs && glyphs != stack_glyphs)
                cairo_glyph_free(glyphs);
        }
    }

    cairo_move_to(cr, x, y);
    cairo_set_scaled_font(cr, primary);
    cairo_scaled_font_destroy(primary);
}

int cairo_text_extents2(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents)
{
    if (len < 0)
        len = strlen(utf8);
    memset(extents, 0, sizeof(*extents));
    if (len == 0)
        return 0;
    std::vector<TextRun> runs;
    SplitTextRuns(cr, utf8, len, runs);
    cairo_scaled_font_t *primary = cairo_get_scaled_font(cr);
    int numGlyphs = 0;
    double x = 0, y = 0;
    bool firstBox = true;
    double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    for (size_t r = 0; r < runs.size(); r++)
    {
        cairo_scaled_font_t *font = runs[r].scaled ? runs[r].scaled : primary;
        std::vector<cairo_glyph_t> glyphs;
        int n = ShapeAppend(font, utf8 + runs[r].offset, runs[r].len, x, y, glyphs);
        if (n > 0)
        {
            cairo_text_extents_t rext;
            cairo_scaled_font_glyph_extents(font, glyphs.data(), n, &rext);
            double l = rext.x_bearing, t = rext.y_bearing;
            double rr = rext.x_bearing + rext.width, b = rext.y_bearing + rext.height;
            if (firstBox)
            {
                xMin = l;
                yMin = t;
                xMax = rr;
                yMax = b;
                firstBox = false;
            }
            else
            {
                if (l < xMin)
                    xMin = l;
                if (t < yMin)
                    yMin = t;
                if (rr > xMax)
                    xMax = rr;
                if (b > yMax)
                    yMax = b;
            }
            extents->x_advance += rext.x_advance;
            extents->y_advance += rext.y_advance;
            numGlyphs += n;
        }
    }
    if (!firstBox)
    {
        extents->x_bearing = xMin;
        extents->y_bearing = yMin;
        extents->width = xMax - xMin;
        extents->height = yMax - yMin;
    }
    return numGlyphs;
}

int cairo_text_extents2_ex(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents, int *pndx)
{
    if (len < 0)
        len = strlen(utf8);
    if (len == 0)
        return 0;
    std::vector<TextRun> runs;
    SplitTextRuns(cr, utf8, len, runs);
    cairo_scaled_font_t *primary = cairo_get_scaled_font(cr);
    memset(extents, 0, sizeof(*extents));
    extents->x_advance = extents->y_advance = 0;
    int total = 0;
    double x = 0, y = 0;
    cairo_scaled_font_t *lastFont = primary;
    cairo_glyph_t lastGlyph = {0, 0, 0};
    bool hasLast = false;
    for (size_t r = 0; r < runs.size(); r++)
    {
        cairo_scaled_font_t *font = runs[r].scaled ? runs[r].scaled : primary;
        std::vector<cairo_glyph_t> glyphs;
        int n = ShapeAppend(font, utf8 + runs[r].offset, runs[r].len, x, y, glyphs);
        if (n <= 0)
            continue;
        // per-glyph cumulative advances (glyph i+1 x position == advance of glyph i)
        for (int i = 0; i < n - 1; i++)
            pndx[total + i] = (int)glyphs[i + 1].x;
        lastFont = font;
        lastGlyph = glyphs[n - 1];
        hasLast = true;
        total += n;
    }
    if (!hasLast)
        return 0;
    // keep original semantics: bearings from the last glyph, advance = total
    cairo_scaled_font_glyph_extents(lastFont, &lastGlyph, 1, extents);
    extents->x_advance = x; // x already accumulates the advance of every glyph
    extents->y_advance = y;
    pndx[total - 1] = (int)extents->x_advance;
    return total;
}

void cairo_text_path2(cairo_t *cr, const char *utf8, int length)
{
    cairo_scaled_font_t *primary, *font;
    cairo_text_extents_t extents;
    double x, y;
    int num_glyphs;

    if (length < 0)
        length = strlen(utf8);

    // Return early if no text
    if (length == 0)
        return;

    std::vector<TextRun> runs;
    SplitTextRuns(cr, utf8, length, runs);

    // Get current point for glyph positioning
    cairo_get_current_point(cr, &x, &y);
    primary = cairo_get_scaled_font(cr);
    cairo_scaled_font_reference(primary);

    bool moved = false;
    for (size_t r = 0; r < runs.size(); r++)
    {
        font = runs[r].scaled ? runs[r].scaled : primary;
        cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
        cairo_glyph_t *glyphs = stack_glyphs;
        num_glyphs = ARRAY_LENGTH(stack_glyphs);
        cairo_status_t status = cairo_scaled_font_text_to_glyphs(font, x, y, utf8 + runs[r].offset, runs[r].len, &glyphs, &num_glyphs, NULL, NULL, NULL);
        if (status == CAIRO_STATUS_SUCCESS && num_glyphs > 0)
        {
            // Add glyphs to the current path (glyph positions are absolute,
            // the current point is not involved)
            cairo_set_scaled_font(cr, font);
            cairo_glyph_path(cr, glyphs, num_glyphs);

            // Move current point to the end of the text, just like cairo_text_path does
            // This allows for chaining multiple calls to cairo_text_path2
            cairo_glyph_extents(cr, glyphs + num_glyphs - 1, 1, &extents);
            x = glyphs[num_glyphs - 1].x + extents.x_advance;
            y = glyphs[num_glyphs - 1].y + extents.y_advance;
            moved = true;
        }
        if (glyphs && glyphs != stack_glyphs)
        {
            cairo_glyph_free(glyphs);
        }
    }

    if (moved)
        cairo_move_to(cr, x, y);
    cairo_set_scaled_font(cr, primary);
    cairo_scaled_font_destroy(primary);
}
