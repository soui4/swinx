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
 * Size is build-time configurable: -DSWINX_FB_WIDTH / -DSWINX_FB_HEIGHT
 * (defaults 320x240, ARGB32, 4 bytes/pixel, stride == width).
 */
#ifndef _SWINX_FREERTOS_FRAMEBUFFER_H_
#define _SWINX_FREERTOS_FRAMEBUFFER_H_

#include <ctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SWINX_FB_WIDTH
#define SWINX_FB_WIDTH 320
#endif
#ifndef SWINX_FB_HEIGHT
#define SWINX_FB_HEIGHT 240
#endif

/* framebuffer bits: ARGB32 premultiplied, native uint32 per pixel
 * (little-endian value layout == 0xAARRGGBB) */
uint32_t *SwinxFbBits(void);
int SwinxFbWidth(void);
int SwinxFbHeight(void);
int SwinxFbStride(void);   /* pixels, == SWINX_FB_WIDTH */

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
