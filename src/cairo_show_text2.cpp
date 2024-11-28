#include <windows.h>
#include "cairo_show_text2.h"
#include <assert.h>
/* Size in bytes of buffer to use off the stack per functions.
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

size_t cairo_break_text(cairo_t *cr, const char *utf8, size_t length, float maxWidth)
{
    cairo_text_extents_t extents;
    cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
    cairo_glyph_t stack_glyphs[5];
    cairo_glyph_t *glyphs = stack_glyphs;
    int num_glyphs = 1;
    size_t ret = 0;
    float wid = 0.f;
    const char *p1 = utf8;
    const char *p2 = utf8 + length;
    while (p1 < p2 && wid < maxWidth)
    {
        const char *next = (const char *)_mbsinc((const uint8_t *)p1);
        int len = next - p1;
        cairo_scaled_font_text_to_glyphs(scaled_font, 0, 0, p1, len, &glyphs, &num_glyphs, NULL, NULL, NULL);
        if (num_glyphs)
        {
            assert(num_glyphs == 1);
            assert(glyphs == stack_glyphs);
            cairo_glyph_extents(cr, glyphs, num_glyphs, &extents);
            wid += extents.x_advance;
            if (glyphs != stack_glyphs)
            {
                cairo_glyph_free(glyphs);
            }
        }
        ret++;
        p1 = next;
    }
    return ret;
}

void cairo_show_text2(cairo_t *cr, const char *text, int len)
{
    cairo_text_extents_t extents;
    cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
    cairo_glyph_t *glyphs, *last_glyph;
    cairo_text_cluster_t *clusters;
    cairo_bool_t has_show_text_glyphs;
    cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
    cairo_text_cluster_t stack_clusters[CAIRO_STACK_ARRAY_LENGTH(cairo_text_cluster_t)];
    double x, y;
    int utf8_len, num_glyphs, num_clusters;
    cairo_text_cluster_flags_t cluster_flags;

    has_show_text_glyphs = cairo_surface_has_show_text_glyphs(cairo_get_target(cr));
    if (len < 0)
        len = strlen(text);

    glyphs = stack_glyphs;
    num_glyphs = ARRAY_LENGTH(stack_glyphs);

    if (has_show_text_glyphs)
    {
        clusters = stack_clusters;
        num_clusters = ARRAY_LENGTH(stack_clusters);
    }
    else
    {
        clusters = NULL;
        num_clusters = 0;
    }
    cairo_get_current_point(cr, &x, &y);
    cairo_status_t status = cairo_scaled_font_text_to_glyphs(scaled_font, x, y, text, len, &glyphs, &num_glyphs, &clusters, &num_clusters, &cluster_flags);
    if (num_glyphs)
    {
        cairo_show_text_glyphs(cr, nullptr, 0, glyphs, num_glyphs, clusters, num_clusters, cluster_flags);
        last_glyph = glyphs + num_glyphs - 1;
        cairo_glyph_extents(cr, last_glyph, 1, &extents);
        x = last_glyph->x + extents.x_advance;
        y = last_glyph->y + extents.y_advance;
        cairo_move_to(cr, x, y);
    }

    if (glyphs && glyphs != stack_glyphs)
        cairo_glyph_free(glyphs);
    if (clusters && clusters != stack_clusters)
        cairo_text_cluster_free(clusters);
}

int cairo_text_extents2(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents)
{
    cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
    cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
    cairo_glyph_t *glyphs;
    int num_glyphs;
    if (len < 0)
        len = strlen(utf8);
    glyphs = stack_glyphs;
    num_glyphs = ARRAY_LENGTH(stack_glyphs);
    extents->x_advance = extents->y_advance = 0;
    cairo_scaled_font_text_to_glyphs(scaled_font, 0, 0, utf8, len, &glyphs, &num_glyphs, NULL, NULL, NULL);
    if (num_glyphs)
    {
        cairo_glyph_extents(cr, glyphs, num_glyphs, extents);
    }
    if (glyphs && glyphs != stack_glyphs)
        cairo_glyph_free(glyphs);
    return num_glyphs;
}

int cairo_text_extents2_ex(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents,int *pndx)
{
    cairo_glyph_t stack_glyphs[CAIRO_STACK_ARRAY_LENGTH(cairo_glyph_t)];
    cairo_scaled_font_t *scaled_font = cairo_get_scaled_font(cr);
    cairo_glyph_t *glyphs;
    int num_glyphs;
    if (len < 0)
        len = strlen(utf8);
    glyphs = stack_glyphs;
    num_glyphs = ARRAY_LENGTH(stack_glyphs);
    extents->x_advance = extents->y_advance = 0;
    cairo_scaled_font_text_to_glyphs(scaled_font, 0, 0, utf8, len, &glyphs, &num_glyphs, NULL, NULL, NULL);
    if (num_glyphs)
    {
        for (int i = 0; i < num_glyphs-1; i++) {
            pndx[i] = glyphs[i+1].x;
        }
        cairo_glyph_t  *last_glyph = glyphs + num_glyphs - 1;
        cairo_glyph_extents(cr, last_glyph, 1, extents);
        extents->x_advance += last_glyph->x;
        pndx[num_glyphs - 1] = extents->x_advance;
    }
    if (glyphs && glyphs != stack_glyphs)
        cairo_glyph_free(glyphs);
    return num_glyphs;
}
