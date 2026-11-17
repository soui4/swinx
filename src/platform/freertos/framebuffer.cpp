/*
 * framebuffer.cpp -- FreeRTOS software framebuffer backing store.
 *
 * Without SwinxFbInit() a static buffer of SWINX_FB_WIDTH x SWINX_FB_HEIGHT
 * (320x240 ARGB32 == 300KB) is used, living in .bss.  SwinxFbInit()
 * reconfigures it at runtime for another panel size, either into a buffer
 * the caller provides or into a heap block allocated here.
 *
 * Multi-window composition is first-come blit (commitCanvas is called from
 * the UI thread only), so no lock is taken here.
 */
#include "framebuffer.h"

#include <stdlib.h>
#include <string.h>

static FBPIXEL s_defaultBits[SWINX_FB_WIDTH * SWINX_FB_HEIGHT];
static FBPIXEL *s_fbBits = s_defaultBits;
static SwinxFbPresentCb s_presentCb = 0;
static int s_w = SWINX_FB_WIDTH;
static int s_h = SWINX_FB_HEIGHT;
static bool s_heapOwned = false;

FBPIXEL *SwinxFbBits(void)
{
    return s_fbBits;
}

int SwinxFbWidth(void)
{
    return s_w;
}

int SwinxFbHeight(void)
{
    return s_h;
}

int SwinxFbStride(void)
{
    return s_w;
}

int SwinxFbInit(int width, int height, FBPIXEL *bits)
{
    if (width <= 0 || height <= 0)
        return -1;

    FBPIXEL *p = bits;
    bool heap = false;
    if (!p)
    {
        p = (FBPIXEL *)malloc((size_t)width * height * sizeof(FBPIXEL));
        if (!p)
            return -1;
        heap = true;
    }

    if (s_heapOwned)
        free(s_fbBits);
    s_fbBits = p;
    s_w = width;
    s_h = height;
    s_heapOwned = heap;
    memset(s_fbBits, 0, (size_t)width * height * sizeof(FBPIXEL));
    return 0;
}

void SwinxSetFbPresentCb(SwinxFbPresentCb cb)
{
    s_presentCb = cb;
}

void SwinxFbClear(uint32_t argb)
{
    const int n = s_w * s_h;
    for (int i = 0; i < n; ++i)
        s_fbBits[i] = argb;
}

void swinxFbPresent(const RECT *dirty)
{
    if (s_presentCb)
        s_presentCb(dirty);
}
