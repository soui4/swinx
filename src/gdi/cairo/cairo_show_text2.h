#ifndef _CAIRO_SHOW_TEXT2_H_
#define _CAIRO_SHOW_TEXT2_H_
#include <cairo.h>

void cairo_show_text2(cairo_t *cr, const char *text, int len);
int cairo_text_extents2(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents);

// Per-character cumulative advances: pndx[0..len-1] (len = byte count, and every
// byte of a multi-byte character gets that character's width).  The return value
// is the number of entries written (len), or 0 when nothing was shaped.  Line
// width should be taken from extents->x_advance rather than pndx[len-1].
//
// pndx is optional: pass NULL when only extents (and hence the line width) is
// wanted and the per-character array is not needed.
int cairo_text_extents2_ex(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents, int *pndx);

size_t cairo_break_text(cairo_t *cr, const char *utf8, size_t length, float maxWidth);

void cairo_draw_line(cairo_t *cr, float x0, float y0, float x1, float y1);

void cairo_text_path2(cairo_t *cr, const char *utf8, int length);
#endif //_CAIRO_SHOW_TEXT2_H_
