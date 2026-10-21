/*
 * cairo-features.h -- FreeRTOS stub for the config header that cairo's build
 * normally generates.  Image surface + PNG (memory-stream) only; every other
 * backend/feature is off.
 *
 * NOTE: the guard must be the upstream name CAIRO_FEATURES_H -- cairo's
 * cairo-mutex-list-private.h keys on it to decide whether the mutex
 * declarations are live or no-ops.
 */
#ifndef CAIRO_FEATURES_H
#define CAIRO_FEATURES_H

#define CAIRO_HAS_IMAGE_SURFACE 1
#define CAIRO_HAS_PNG_FUNCTIONS 1   /* vendored libpng+zlib, memory-stream IO only */

/* everything else intentionally left undefined:
   CAIRO_HAS_PDF_SURFACE, CAIRO_HAS_PS_SURFACE, CAIRO_HAS_SVG_SURFACE,
   CAIRO_HAS_WIN32_SURFACE, CAIRO_HAS_XCB_SURFACE, CAIRO_HAS_XLIB_SURFACE,
   CAIRO_HAS_FT_FONT, CAIRO_HAS_WIN32_FONT, CAIRO_HAS_FC_FONT, ... */

#endif // CAIRO_FEATURES_H
