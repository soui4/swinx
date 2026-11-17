/*
 * framebuffer.h -- FreeRTOS software framebuffer.
 *
 * The FreeRTOS port has no display server: windows are virtual _Window
 * objects whose canvases are cairo image surfaces.  commitCanvas() blits
 * the dirty region of a window canvas into this framebuffer, then hands
 * the dirty rect (screen coordinates) to the registered present callback
 * -- on real hardware that callback pushes the region to the LCD driver;
 * in tests it records the flush or dumps the buffer via semihosting.
 *
 * Size is runtime-configurable: call SwinxFbInit(width, height, bits) before
 * the UI starts (bits == NULL allocates internally).  Without it the buffer
 * falls back to SWINX_FB_WIDTH x SWINX_FB_HEIGHT (build-time defaults,
 * 320x240 ARGB32, 4 bytes/pixel, stride == width).  The pixel format is
 * ARGB32 premultiplied on host builds, opaque RGB565 on ESP-IDF.
 */
#ifndef _SWINX_FREERTOS_FRAMEBUFFER_H_
#define _SWINX_FREERTOS_FRAMEBUFFER_H_

#include <ctypes.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SWINX_FB_WIDTH
#define SWINX_FB_WIDTH 320
#endif
#ifndef SWINX_FB_HEIGHT
#define SWINX_FB_HEIGHT 240
#endif

/* Route (1) experiment: on ESP-IDF the panel is RGB565, so keep the software
 * compositor in that format too.  present() can then hand the framebuffer
 * straight to the LCD driver and the extra 150KB ARGB32->RGB565 conversion
 * buffer disappears.  Host builds keep ARGB32 -- SOUI_PLATFORM_ESPIDF is a
 * PUBLIC compile definition of the esp32s3 `soui` component only. */
#if defined(SOUI_PLATFORM_ESPIDF) && !defined(SWINX_FB_RGB565)
/* Route (1) A/B switch.  0 = legacy ARGB32 compositor + 150KB conversion
 * buffer (baseline); 1 = RGB565 compositor with direct strided present. */
#define SWINX_FB_RGB565 1
#endif

#if SWINX_FB_RGB565
typedef uint16_t FBPIXEL;
#else
typedef uint32_t FBPIXEL;
#endif

/* framebuffer bits: ARGB32 premultiplied (0xAARRGGBB) on host builds,
 * or opaque RGB565 on ESP-IDF.  All getters below reflect the last
 * SwinxFbInit() (or the build-time defaults if it was never called). */
FBPIXEL *SwinxFbBits(void);
int SwinxFbWidth(void);
int SwinxFbHeight(void);
int SwinxFbStride(void);   /* pixels, == SwinxFbWidth() */

/* Configure the framebuffer at runtime, before the UI thread starts blitting.
 *   bits == NULL : allocate internally (malloc; caller never frees).
 *   bits != NULL : use the caller's buffer (must hold width*height FBPIXELs).
 * Returns 0 on success, -1 on bad arguments or allocation failure (the
 * previous configuration is kept in that case).  May be called again to
 * reconfigure; an internally-allocated previous buffer is released. */
int SwinxFbInit(int width, int height, FBPIXEL *bits);

/* called after each commitCanvas blit with the dirty rect in screen
 * coordinates (may be NULL to unset) */
typedef void (*SwinxFbPresentCb)(const RECT *dirty);
void SwinxSetFbPresentCb(SwinxFbPresentCb cb);

void SwinxFbClear(uint32_t argb);

/* internal: invoked by SConnection::commitCanvas after a blit */
void swinxFbPresent(const RECT *dirty);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_FREERTOS_FRAMEBUFFER_H_
