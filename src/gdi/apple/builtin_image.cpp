// Apple 平台的 BuiltinImage 实现：使用 CoreGraphics + ImageIO 解码内存 PNG 资源，
// 通过公开 GDI API（CreateDIBSection / DrawBitmap9Patch / DrawBitmapEx / GetObject）
// 创建与绘制 HBITMAP，全程不依赖 cairo。

#include "../builtin_image.h"
#undef interface
#include <gdi.h>
#include <sysapi.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "image.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

// ---------------------------------------------------------------------------
// 解码内存 PNG 字节 → 创建 HBITMAP（32 位 ARGB，顶向下 DIB）
// 使用公开 CreateDIBSection API，不直接依赖任何内部 GDI 对象格式。
// ---------------------------------------------------------------------------
static HBITMAP CreateBitmapFromPngData(const BYTE* pngData, int pngLen)
{
    if (!pngData || pngLen <= 0) return nullptr;

    CFDataRef dataRef = CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault,
        pngData,
        (CFIndex)pngLen,
        kCFAllocatorNull);
    if (!dataRef) return nullptr;

    CGImageSourceRef src = CGImageSourceCreateWithData(dataRef, nullptr);
    CFRelease(dataRef);
    if (!src) return nullptr;

    CGImageRef cgImg = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!cgImg) return nullptr;

    size_t width = CGImageGetWidth(cgImg);
    size_t height = CGImageGetHeight(cgImg);

    // 创建临时的 CGBitmapContext，把任意格式的 PNG 像素规范渲染为
    // ARGB32 预乘 Alpha（小端 32 位）——与 CreateDIBSection 32 位位图兼容。
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    const size_t dstBytesPerPixel = 4;
    size_t tmpStride = ((width * dstBytesPerPixel) + 3u) & ~3u;
    size_t tmpSize = tmpStride * height;
    void* tmpBuf = calloc(1, tmpSize);
    if (!tmpBuf) {
        CFRelease(cgImg);
        CGColorSpaceRelease(colorSpace);
        return nullptr;
    }

    CGBitmapInfo bmInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
    CGContextRef bmCtx = CGBitmapContextCreate(
        tmpBuf,
        width,
        height,
        8,                      // bitsPerComponent
        tmpStride,
        colorSpace,
        bmInfo);
    CGColorSpaceRelease(colorSpace);
    if (!bmCtx) {
        free(tmpBuf);
        CFRelease(cgImg);
        return nullptr;
    }

    // 不做 CTM 翻转：CGBitmapContext 原点在左下，直接绘制即可得到「底向上」
    // 顺序的 tmpBuf（offset 0 = 图像底部），与 memdc 位图的存储结构一致。
    CGRect imgRect = CGRectMake(0.0, 0.0, (CGFloat)width, (CGFloat)height);
    CGContextDrawImage(bmCtx, imgRect, cgImg);
    CGContextRelease(bmCtx);
    CFRelease(cgImg);

    // 调用公开 CreateDIBSection API 创建 GDI 位图，然后把 tmpBuf 里的像素拷贝过去
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)width;
    bmi.bmiHeader.biHeight = (LONG)height;   // positive → bottom-up DIB (same as memdc)
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = (WORD)(dstBytesPerPixel * 8);
    bmi.bmiHeader.biCompression = BI_RGB;

    VOID* dstBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &dstBits, nullptr, 0);
    if (hBmp && dstBits) {
        const BYTE* pSrc = (const BYTE*)tmpBuf;
        BYTE* pDst = (BYTE*)dstBits;
        // 取实际的 DIB 行步长：32 位图为 width*4 并 4 字节对齐
        size_t dstStride = ((((LONG)width) * dstBytesPerPixel) + 3u) & ~3u;
        for (size_t y = 0; y < height; y++) {
            memcpy(pDst + y * dstStride, pSrc + y * tmpStride, width * dstBytesPerPixel);
        }
    }
    free(tmpBuf);
    return hBmp;
}

// ============================================================================
// BuiltinImage 接口实现
// ============================================================================

BuiltinImage *BuiltinImage::instance()
{
    static BuiltinImage inst;
    return &inst;
}

struct BiConfig
{
    const BYTE *buf;
    int bufLen;
    int states;
    UINT expandMode;
    RECT margin;
};

BOOL BuiltinImage::Init()
{
    m_biInfo100 = new BiInfo[BI_COUNT];
    m_biInfo200 = new BiInfo[BI_COUNT];

    BiConfig cfg100[] = {
        { sys_scrollbar_png,        (int)sizeof(sys_scrollbar_png),        1, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 4, 4, 4, 4 } },
        { sys_msgbox_icons_png,     (int)sizeof(sys_msgbox_icons_png),     5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0, 0, 0, 0 } },
    };
    for (int i = 0; i < BI_COUNT; i++)
    {
        m_biInfo100[i].hBmp = CreateBitmapFromPngData(cfg100[i].buf, cfg100[i].bufLen);
        assert(m_biInfo100[i].hBmp);
        m_biInfo100[i].states = cfg100[i].states;
        m_biInfo100[i].expendMode = cfg100[i].expandMode;
        m_biInfo100[i].margin = cfg100[i].margin;
    }

    BiConfig cfg200[] = {
        { sys_scrollbar_200_png,    (int)sizeof(sys_scrollbar_200_png),    1, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 8, 8, 8, 8 } },
        { sys_msgbox_icons200_png,  (int)sizeof(sys_msgbox_icons200_png),  5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0, 0, 0, 0 } },
    };
    for (int i = 0; i < BI_COUNT; i++)
    {
        m_biInfo200[i].hBmp = CreateBitmapFromPngData(cfg200[i].buf, cfg200[i].bufLen);
        assert(m_biInfo200[i].hBmp);
        m_biInfo200[i].states = cfg200[i].states;
        m_biInfo200[i].expendMode = cfg200[i].expandMode;
        m_biInfo200[i].margin = cfg200[i].margin;
    }
    return TRUE;
}

BOOL BuiltinImage::drawBiState(HDC hdc, int imgId, ImgState st, const RECT *rcDst, BYTE byAlpha)
{
    if (imgId >= BI_COUNT) return FALSE;
    int scale = GetSystemScale();
    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + imgId;
    if (!biInfo->hBmp) return FALSE;

    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / 4;
    int hei = bm.bmHeight;
    RECT rcSrc = { 0, 0, wid, hei };
    OffsetRect(&rcSrc, st * wid, 0);
    DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    return TRUE;
}

BOOL BuiltinImage::drawBiIdx(HDC hdc, int imgId, int idx, const RECT *rcDst, BYTE byAlpha)
{
    if (imgId >= BI_COUNT) return FALSE;
    int scale = GetSystemScale();
    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + imgId;
    if (!biInfo->hBmp) return FALSE;

    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / biInfo->states;
    int hei = bm.bmHeight;
    RECT rcSrc = { 0, 0, wid, hei };
    OffsetRect(&rcSrc, idx * wid, 0);
    if (rcDst->right - rcDst->left < wid || rcDst->bottom - rcDst->top < hei)
    {
        DrawBitmapEx(hdc, rcDst, biInfo->hBmp, &rcSrc, EXPEND_MODE_STRETCH, byAlpha);
    }
    else
    {
        DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    }
    return TRUE;
}

BOOL BuiltinImage::drawScrollbarState(HDC hdc, int iPart, BOOL bVert, int st, const RECT *rcDst, BYTE byAlpha)
{
    int scale = GetSystemScale();
    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + BI_SCROLLBAR;
    if (!biInfo->hBmp) return FALSE;

    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / 9;
    int hei = bm.bmHeight / 4;
    RECT rcSrc = { 0, 0, wid, hei };
    if (iPart < Sb_Triangle)
    {
        int iCol = iPart;
        if (!bVert) iCol += 4;
        OffsetRect(&rcSrc, wid * iCol, hei * st);
    }
    else
    {
        OffsetRect(&rcSrc, wid * 8, hei * (iPart - Sb_Triangle));
    }
    DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);

    if (iPart == Sb_Thumb)
    {
        RECT rcSrc2 = { 0, 0, wid, hei };
        RECT rcTmp = *rcDst;
        if (bVert)
        {
            OffsetRect(&rcSrc2, wid * 8, hei * 1);
            InflateRect(&rcTmp, 0, -(rcTmp.bottom - rcTmp.top - hei) / 2);
        }
        else
        {
            OffsetRect(&rcSrc2, wid * 8, hei * 2);
            InflateRect(&rcTmp, -(rcTmp.right - rcTmp.left - wid) / 2, 0);
        }
        if (rcTmp.right - rcTmp.left <= rcDst->right - rcDst->left &&
            rcTmp.bottom - rcTmp.top  <= rcDst->bottom - rcDst->top)
        {
            DrawBitmapEx(hdc, &rcTmp, biInfo->hBmp, &rcSrc2, EXPEND_MODE_NONE, byAlpha);
        }
    }
    return TRUE;
}

BuiltinImage::BuiltinImage()
    : m_biInfo100(nullptr)
    , m_biInfo200(nullptr)
{
    Init();
}

BuiltinImage::~BuiltinImage()
{
    if (m_biInfo100) delete[] m_biInfo100;
    if (m_biInfo200) delete[] m_biInfo200;
}
