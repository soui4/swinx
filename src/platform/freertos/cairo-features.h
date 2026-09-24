/*
 * cairo-features.h -- FreeRTOS stub for the config header that cairo's build
 * normally generates.  Every optional backend/feature is off: the FreeRTOS
 * port only compiles against the core cairo types (cairo_surface_t etc.)
 * until the software-framebuffer renderer lands.
 */
#ifndef CAIRO_FEATURES_H_SWINX_FREERTOS
#define CAIRO_FEATURES_H_SWINX_FREERTOS

#define CAIRO_HAS_IMAGE_SURFACE 1

/* everything else intentionally left undefined:
   CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFACE, CAIRO_HAS_SVG_SURFACE,
   CAIRO_HAS_WIN32_SURFACE, CAIRO_HAS_XCB_SURFACE, CAIRO_HAS_XLIB_SURFACE,
   CAIRO_HAS_FT_FONT, CAIRO_HAS_WIN32_FONT, CAIRO_HAS_FC_FONT, ... */

#endif // CAIRO_FEATURES_H_SWINX_FREERTOS
