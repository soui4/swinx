/*
 * framebuffer.cpp -- WebAssembly software framebuffer backing store.
 *
 * Heap allocation (HiDPI): the size depends on the canvas scale factor
 * (SwinxSetCanvasScale, called once from main() before anything reads fb
 * geometry), so a static .bss array no longer works.  The JS host re-reads
 * Module._SwinxFbBits() on every present (ALLOW_MEMORY_GROWTH may move the
 * heap), so a heap buffer is safe.  Multi-window composition is first-come
 * blit (commitCanvas is called from the UI thread only), so no lock is taken.
 *
 * SWINX_FB_WIDTH/HEIGHT stay LOGICAL (the app's 800x600 coordinate system);
 * the physical framebuffer is logical x scale (devicePixelRatio) so text and
 * vectors render sharp on HiDPI displays.
 */
#include "framebuffer.h"

#include <stdlib.h>

static uint32_t *s_fbBits = nullptr;
static int s_fbW = 0;          /* 物理 */
static int s_fbH = 0;
static double s_scale = 1.0;
static SwinxFbPresentCb s_presentCb = 0;
static int s_cRequests = 0;
static int s_cPresents = 0;

void SwinxSetCanvasScale(double scale)
{
    if (!(scale > 0.5))
        scale = 1.0;
    if (scale > 3.0)
        scale = 3.0;
    s_scale = scale;
    s_fbW = (int)(SWINX_FB_WIDTH * scale + 0.5);
    s_fbH = (int)(SWINX_FB_HEIGHT * scale + 0.5);
    delete[] s_fbBits;
    s_fbBits = new uint32_t[(size_t)s_fbW * s_fbH];
    SwinxFbClear(0xFF000000u);
}

double SwinxGetCanvasScale(void)
{
    return s_scale;
}

uint32_t *SwinxFbBits(void)
{
    /* main() 之前就有人问地址（理论上不该发生）：退回 1x1 兜底，避免空指针 */
    if (!s_fbBits)
    {
        static uint32_t s_fallback;
        return &s_fallback;
    }
    return s_fbBits;
}

int SwinxFbWidth(void)
{
    return s_fbW > 0 ? s_fbW : SWINX_FB_WIDTH;
}

int SwinxFbHeight(void)
{
    return s_fbH > 0 ? s_fbH : SWINX_FB_HEIGHT;
}

int SwinxFbStride(void)
{
    return SwinxFbWidth();
}

int SwinxFbLogicalWidth(void)
{
    return SWINX_FB_WIDTH;
}

int SwinxFbLogicalHeight(void)
{
    return SWINX_FB_HEIGHT;
}

void SwinxSetFbPresentCb(SwinxFbPresentCb cb)
{
    s_presentCb = cb;
}

void SwinxFbClear(uint32_t argb)
{
    const int n = SwinxFbWidth() * SwinxFbHeight();
    uint32_t *p = s_fbBits;
    if (!p)
        return;
    for (int i = 0; i < n; ++i)
        p[i] = argb;
}

void SwinxFbCountRequest(void)
{
    ++s_cRequests;
}

int SwinxFbRequests(void)
{
    return s_cRequests;
}

int SwinxFbPresents(void)
{
    return s_cPresents;
}

void swinxFbPresent(const RECT *dirty)
{
    ++s_cPresents;
    if (s_presentCb)
        s_presentCb(dirty);
}
