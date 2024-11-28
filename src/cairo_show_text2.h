#ifndef _CAIRO_SHOW_TEXT2_H_
#define _CAIRO_SHOW_TEXT2_H_
#include <cairo/cairo.h>

void cairo_show_text2(cairo_t *cr, const char *text, int len);
int cairo_text_extents2(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents);
int cairo_text_extents2_ex(cairo_t *cr, const char *utf8, int len, cairo_text_extents_t *extents,int *pndx);

size_t cairo_break_text(cairo_t *cr, const char *utf8, size_t length, float maxWidth);

void cairo_draw_line(cairo_t *cr, float x0, float y0, float x1, float y1);
#endif //_CAIRO_SHOW_TEXT2_H_