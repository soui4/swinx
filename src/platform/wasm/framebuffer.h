/*
 * framebuffer.h -- WebAssembly software framebuffer.
 *
 * A wasm module has no display server: windows are virtual _Window objects
 * whose canvases are cairo image surfaces.  commitCanvas() blits the dirty
 * region of a window canvas into this framebuffer, then hands the dirty rect
 * (screen coordinates) to the registered present callback.  On this port the
 * callback is where the JS host attaches: it copies the dirty rect into the
 * Canvas ImageData (or a WebGL texture) -- the same seam the FreeRTOS port
 * uses for its LCD driver, which is why this file is a fork of
 * src/platform/freertos/framebuffer.h rather than a shared directory.
 *
 * Size is build-time configurable: -DSWINX_FB_WIDTH / -DSWINX_FB_HEIGHT
 * (defaults 320x240, ARGB32, 4 bytes/pixel, stride == width).  swinx/wasm.cmake
 * sets both as CACHE STRING from one place so libswinx.a and its consumers
 * cannot disagree; the host page still has to match the canvas to the buffer,
 * which it should read at runtime from SwinxFbWidth()/SwinxFbHeight() instead
 * of hardcoding.
 */
#ifndef _SWINX_WASM_FRAMEBUFFER_H_
#define _SWINX_WASM_FRAMEBUFFER_H_

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

/* HiDPI：SWINX_FB_WIDTH/HEIGHT 是**逻辑**尺寸（应用坐标 800x600），
 * 物理帧缓冲 = 逻辑 x 缩放因子。缩放因子在 main() 最开始
 * SwinxSetCanvasScale(browser devicePixelRatio) 一次，之后不再变 ——
 * 浏览器缩放/拖去另一台显示器导致的 dpr 变化要刷新页面才生效。 */
void SwinxSetCanvasScale(double scale);
double SwinxGetCanvasScale(void);

/* framebuffer bits: ARGB32 premultiplied, native uint32 per pixel
 * (little-endian value layout == 0xAARRGGBB) */
uint32_t *SwinxFbBits(void);
int SwinxFbWidth(void);        /* 物理（像素） */
int SwinxFbHeight(void);       /* 物理（像素） */
int SwinxFbStride(void);       /* 物理像素，== SwinxFbWidth() */
int SwinxFbLogicalWidth(void);  /* 逻辑（应用坐标），== SWINX_FB_WIDTH */
int SwinxFbLogicalHeight(void);

/* called after each commitCanvas blit with the dirty rect in screen
 * coordinates (may be NULL to unset) */
typedef void (*SwinxFbPresentCb)(const RECT *dirty);
void SwinxSetFbPresentCb(SwinxFbPresentCb cb);

void SwinxFbClear(uint32_t argb);

/* 上屏计数（诊断与无头断言用，不参与渲染逻辑）：
 *   requests —— commitCanvas 被要求的次数，每个 ReleaseDC 一次；
 *   presents —— 真正 memcpy + 回调 JS 的次数。
 * 端口把同一次派发里的多次 request 合并成一次 present，所以 presents 明显小于
 * requests 才是期望形态；两者相等说明合并没有生效。 */
int SwinxFbRequests(void);
int SwinxFbPresents(void);
void SwinxFbCountRequest(void);

/* internal: invoked by SConnection's deferred present after a blit */
void swinxFbPresent(const RECT *dirty);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_WASM_FRAMEBUFFER_H_
