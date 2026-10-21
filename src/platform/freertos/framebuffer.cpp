/*
 * framebuffer.cpp -- FreeRTOS software framebuffer backing store.
 *
 * Static allocation (no heap): 320x240 ARGB32 == 300KB by default, lives
 * in .bss.  Multi-window composition is first-come blit (commitCanvas is
 * called from the UI thread only), so no lock is taken here.
 */
#include "framebuffer.h"

static uint32_t s_fbBits[SWINX_FB_WIDTH * SWINX_FB_HEIGHT];
static SwinxFbPresentCb s_presentCb = 0;

uint32_t *SwinxFbBits(void)
{
    return s_fbBits;
}

int SwinxFbWidth(void)
{
    return SWINX_FB_WIDTH;
}

int SwinxFbHeight(void)
{
    return SWINX_FB_HEIGHT;
}

int SwinxFbStride(void)
{
    return SWINX_FB_WIDTH;
}

void SwinxSetFbPresentCb(SwinxFbPresentCb cb)
{
    s_presentCb = cb;
}

void SwinxFbClear(uint32_t argb)
{
    const int n = SWINX_FB_WIDTH * SWINX_FB_HEIGHT;
    for (int i = 0; i < n; ++i)
        s_fbBits[i] = argb;
}

void swinxFbPresent(const RECT *dirty)
{
    if (s_presentCb)
        s_presentCb(dirty);
}
