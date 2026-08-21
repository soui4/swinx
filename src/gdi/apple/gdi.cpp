#include <windows.h>
#undef interface
#include <gdi.h>
#include <math.h>
#include <png.h>
#include <assert.h>
#include <vector>
#include "handle.h"
#include "sdc.h"
#include "SConnection.h"
#include "tostring.hpp"
#include "uniconv.h"
#include "log.h"
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#ifndef kCTStrikethroughStyleAttributeName
#define kCTStrikethroughStyleAttributeName CFSTR("NSStrikethroughStyle")
#endif
#ifndef kCTStrikethroughColorAttributeName
#define kCTStrikethroughColorAttributeName CFSTR("NSStrikethroughColor")
#endif

static inline bool CGAffineTransformIsInvertible_shim(CGAffineTransform t) {
    double det = t.a * t.d - t.b * t.c;
    return det != 0.0 && isfinite(det);
}
#define CGAffineTransformIsInvertible(t) CGAffineTransformIsInvertible_shim(t)

#ifndef _GRADIENT_TRIANGLE_DEFINED
#define _GRADIENT_TRIANGLE_DEFINED
typedef struct _GRADIENT_TRIANGLE {
    ULONG Vertex1;
    ULONG Vertex2;
    ULONG Vertex3;
} GRADIENT_TRIANGLE, *PGRADIENT_TRIANGLE;
#endif

#ifndef _MACHO_UNICHAR
#define _MACHO_UNICHAR
typedef unsigned short unichar;
#endif

#define kLogTag "gdi"

// --- GdiBitmap: CGImageRef-backed bitmap, analogous to cairo_surface_t ---
//   image  : CGImageRef 是 lazy 创建的主体对象（类似 cairo_surface_t），引用 data 缓冲区。
//            CGImageRef 是不可变的，CoreGraphics 可能在首次绘制后内部缓存像素。
//            当 data 被修改（CGBitmapContext 绘制或外部直接写）后，必须调用 markDirty()
//            释放旧 CGImageRef，下次 getImage() 会重新创建以反映最新像素。
//   data   : 后端像素缓冲区，与 CGImageRef 共享同一块内存（NULL release callback）。
enum GdiBmpFormat {
    GDI_BMP_INVALID = -1,
    GDI_BMP_ARGB32  = 0,
    GDI_BMP_RGB24   = 1,
    GDI_BMP_A1      = 2,
};

struct GdiBitmap {
    CGImageRef image;   // lazy 创建，markDirty() 后释放，下次 getImage() 重建
    unsigned char *data;
    int width;
    int height;
    int stride;
    int format; // GdiBmpFormat
    bool ownsData;

    GdiBitmap() : image(nullptr), data(nullptr), width(0), height(0), stride(0), format(GDI_BMP_INVALID), ownsData(false) {}
    ~GdiBitmap(){
        if (image)
            CGImageRelease(image);
        if (ownsData && data)
            free(data);
    }
    // Lazy 创建 CGImageRef。返回 retained 引用（调用者负责 CGImageRelease）。
    CGImageRef getImage();

    // 标记 data 已修改，释放缓存的 CGImageRef，下次 getImage() 将重建。
    void markDirty()
    {
        if (image)
        {
            CGImageRelease(image);
            image = nullptr;
        }
    }
};

// 从 data 缓冲区创建 CGImageRef（NULL release callback，不持有 data）
static CGImageRef CreateCGImageFromData(unsigned char *data, int width, int height, int stride, int format)
{
    if (format != GDI_BMP_ARGB32 && format != GDI_BMP_RGB24)
        return nullptr;
    if (!data || width <= 0 || height <= 0)
        return nullptr;

    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, data, (size_t)stride * (size_t)height, NULL);
    if (!provider)
        return nullptr;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo;
    if (format == GDI_BMP_ARGB32)
        bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
    else
        bitmapInfo = kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little;
    CGImageRef image = CGImageCreate(width, height, 8, 32, stride, cs, bitmapInfo, provider, NULL, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease(cs);
    CGDataProviderRelease(provider);
    return image;
}


static GdiBitmap *GdiBitmapCreateForData(unsigned char *data, int width, int height, int stride, int format)
{
    GdiBitmap *bmp = new GdiBitmap();
    bmp->width = width;
    bmp->height = height;
    bmp->stride = stride;
    bmp->format = format;
    bmp->data = data;
    bmp->ownsData = false;
    return bmp;
}

static GdiBitmap *GdiBitmapCreate(int width, int height, int format)
{
    GdiBitmap *bmp = new GdiBitmap();
    bmp->width = width;
    bmp->height = height;
    bmp->format = format;
    if (format == GDI_BMP_A1)
        bmp->stride = (width + 7) / 8;
    else
        bmp->stride = width * 4;
    bmp->data = (unsigned char *)calloc((size_t)bmp->stride * (size_t)height, 1);
    bmp->ownsData = true;
    return bmp;
}


CGImageRef GdiBitmap::getImage()
    {
        if (!image)
            image = CreateCGImageFromData(data, width, height, stride, format);
        if (image)
            CGImageRetain(image);
        return image;
    }
// --- end GdiBitmap ---

EXTERN_C BOOL Swinx_DumpBmp(HBITMAP bmp, const char *path)
{
    return FALSE;
}

struct CairoColor
{
    double r, g, b, a;
    CairoColor(COLORREF crSrc)
    {
        r = GetRValue(crSrc) / 255.0;
        g = GetGValue(crSrc) / 255.0;
        b = GetBValue(crSrc) / 255.0;
        a = GetAValue(crSrc) / 255.0;
    }
};

struct LOGPENEX : LOGPEN
{
    std::vector<double> dash;
    HBRUSH patternBrush;
    LOGPENEX()
        : patternBrush(nullptr)
    {
    }
    ~LOGPENEX()
    {
        if (patternBrush)
            DeleteObject(patternBrush);
    }
};

static void gdi_bmp_free(void *ptr)
{
    delete (GdiBitmap *)ptr;
}

static void gdi_pen_free(void *ptr)
{
    LOGPEN *lpen = (LOGPEN *)ptr;
    if((lpen->lopnStyle & PS_STYLE_MASK) == PS_USERSTYLE)
    {
        LOGPENEX *lpex = (LOGPENEX *)ptr;
        delete lpex;
    }
    else
    {
        delete lpen;
    }
}

static CGImageRef CreateCGImageFromBitmap(HBITMAP hbmp)
{
    if (!hbmp)
        return NULL;
    GdiBitmap *surf = (GdiBitmap *)GetGdiObjPtr(hbmp);
    if (!surf)
        return NULL;
    return surf->getImage();
}

struct GradientDetail
{
    std::vector<GRADIENTITEM> items;
    GRADIENTINFO info;
};
// CGGradient 绘制所需的几何信息（替代 CGShadingRef，避免 CGFunctionRef 回调）
struct GradientDrawInfo
{
    CGGradientRef gradient;  // retained, released in dtor
    bool isRadial;
    CGPoint p0, p1;          // axial: start,end; radial: center0,center1
    CGFloat r0, r1;          // radial only
};

extern float getScale();
struct PatternInfo
{
    TILEMODE tileMode;
    BOOL useBmp;
    double alpha;
    union {
        HBITMAP bmp;
        GradientDetail *gradientDetail;
    } data;

    // Cached CG pattern (CGPatternRef for bitmap; GradientDrawInfo* for gradient) — kept in a void* to avoid header pollution
    void *cgPattern;   // retained CF type or heap struct, released in ~PatternInfo()
    int   cgPatternType; // 0=none, 1=CGPatternRef (bitmap tile), 2=GradientDrawInfo* (gradient)
    double cached_wid, cached_hei, cached_x, cached_y;

    PatternInfo()
        : cgPattern(nullptr)
        , cgPatternType(0)
        , cached_wid(0)
        , cached_hei(0)
        , cached_x(0)
        , cached_y(0)
    {
    }

    ~PatternInfo()
    {
        releaseCachedPattern();
        if (useBmp)
        {
            assert(data.bmp);
            DeleteObject(data.bmp);
        }
        else
        {
            delete data.gradientDetail;
        }
    }

    void releaseCachedPattern()
    {
        if (cgPattern)
        {
            if (cgPatternType == 2)
            {
                GradientDrawInfo *gdi = (GradientDrawInfo *)cgPattern;
                if (gdi->gradient) CGGradientRelease(gdi->gradient);
                delete gdi;
            }
            else
            {
                CFRelease(cgPattern);
            }
            cgPattern = nullptr;
        }
        cgPatternType = 0;
    }

    struct fPoint
    {
        double fX;
        double fY;
        void set(double x_, double y_)
        {
            fX = x_;
            fY = y_;
        }
        void offset(double dx, double dy)
        {
            fX += dx;
            fY += dy;
        }
    };

    static bool fequal(float a, float b)
    {
        return fabs(a - b) < 1e-10;
    }
    static void calc_linear_endpoint(float angle, double wid, double hei, fPoint skPts[2])
    {
        double halfWid = wid / 2;
        double halfHei = hei / 2;

        // 1. 归一化角度到 [0, 360)
        float a = fmodf(angle, 360.0f);
        if (a < 0) a += 360.0f;

        // 2. 处理轴对齐的特殊情况（0°, 90°, 180°, 270°）
        if (fequal(a, 90.0f) || fequal(a, 270.0f))
        {
            skPts[0].set((float)halfWid, 0.0f);
            skPts[1].set((float)halfWid, (float)hei);
            return;
        }
        if (fequal(a, 0.0f) || fequal(a, 180.0f))
        {
            skPts[0].set(0.0f, (float)halfHei);
            skPts[1].set((float)wid, (float)halfHei);
            return;
        }

        // 3. 确定象限，并计算参考角度（第一象限 0~90°）
        int quadrant = (int)(a / 90.0f);
        float ref_angle = a - quadrant * 90.0f;   // 0~90°
        float rad = ref_angle * M_PI / 180.0f;
        float tan_angle = tan(rad);
        float cot_angle = 1.0f / tan_angle;      // 避免除零（ref_angle不会是0或90）

        // 4. 计算第一象限下的两个交点（相对于矩形中心，中心为(0,0)）
        fPoint p1, p2;
        // 与左右边（x = -halfWid 和 x = halfWid）的交点 y 坐标
        float y_left  = -halfWid * tan_angle;
        float y_right =  halfWid * tan_angle;

        // 与上下边（y = -halfHei 和 y = halfHei）的交点 x 坐标
        float x_top    = -halfHei * cot_angle;
        float x_bottom =  halfHei * cot_angle;

        // 根据 y_right 是否在 [-halfHei, halfHei] 内选择使用哪一组交点
        if (fabs(y_right) <= halfHei + 1e-9f)
        {
            // 直线与左右边相交
            p1.set((float)-halfWid, (float)y_left);
            p2.set((float) halfWid, (float)y_right);
        }
        else
        {
            // 直线与上下边相交
            p1.set((float)x_top,    (float)-halfHei);
            p2.set((float)x_bottom, (float) halfHei);
        }

        // 5. 根据实际象限对交点坐标进行镜像变换
        fPoint transformed[2];
        switch (quadrant)
        {
        case 0: // 0° ~ 90°
            transformed[0] = p1;
            transformed[1] = p2;
            break;
        case 1: // 90° ~ 180°
            transformed[0].set(-p1.fX,  p1.fY);
            transformed[1].set(-p2.fX,  p2.fY);
            break;
        case 2: // 180° ~ 270°
            transformed[0].set(-p1.fX, -p1.fY);
            transformed[1].set(-p2.fX, -p2.fY);
            break;
        case 3: // 270° ~ 360°
        default:
            transformed[0].set( p1.fX, -p1.fY);
            transformed[1].set( p2.fX, -p2.fY);
            break;
        }

        // 6. 将交点平移到实际矩形坐标系（左上角为原点）
        skPts[0].set((float)(transformed[0].fX + halfWid),
            (float)(transformed[0].fY + halfHei));
        skPts[1].set((float)(transformed[1].fX + halfWid),
            (float)(transformed[1].fY + halfHei));
    }

    CGPatternRef createBitmapPattern(CGImageRef tileImg, double x, double y)
    {
        if (!tileImg) return nullptr;
        size_t tileW = CGImageGetWidth(tileImg);
        size_t tileH = CGImageGetHeight(tileImg);
        if (tileW == 0 || tileH == 0) return nullptr;
        CGRect patternBounds = CGRectMake(0, 0, (CGFloat)tileW, (CGFloat)tileH);
        // pattern matrix: tile is drawn in pattern space; we translate by (x,y) so
        // tile origin aligns with user-space (x,y), matching cairo's cairo_pattern_set_matrix(translate).
        float scale = 1.0f/getScale();
        CGAffineTransform mtxScale = CGAffineTransformMakeScale(scale,scale);
        CGAffineTransform mtxTrans = CGAffineTransformMakeTranslation(-x, y);
        CGAffineTransform mtxPattern = CGAffineTransformConcat(mtxTrans,mtxScale);
        CGPatternCallbacks callbacks = {
            0,
            // drawPattern callback: tile the image
            [](void *info, CGContextRef ctx) {
                CGImageRef img = (CGImageRef)info;
                if (!img) return;
                size_t w = CGImageGetWidth(img);
                size_t h = CGImageGetHeight(img);
                CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), img);
            },
            // releaseInfo callback: release the image when pattern is released
            [](void *info) {
                if (info) CGImageRelease((CGImageRef)info);
            }
        };
        // Retain a reference for the pattern callbacks (info = CGImageRef retained here)
        CGImageRef infoImg = CGImageCreateCopy(tileImg);
        return CGPatternCreate(infoImg, patternBounds, mtxPattern,
                              (CGFloat)tileW, (CGFloat)tileH,
                              kCGPatternTilingConstantSpacing, true, // colored pattern
                              &callbacks);
    }

    // Build / return the cached pattern for the given drawing rect & offset.
    // Returns true if a CG pattern (bitmap tile or gradient shading) was built and
    // should be applied via CGContextSetFillPattern / CGContextSetShading.
    // If this returns true, outPattern / outType are set (borrowed, don't release).
    bool getOrCreate(CGContextRef ctx, double wid, double hei, double x, double y,
                     void *&outPattern, int &outType)
    {
        (void)ctx; // unused (may be useful later for context-resident caches)
        if (cgPattern && cached_wid == wid && cached_hei == hei && cached_x == x && cached_y == y)
        {
            outPattern = cgPattern;
            outType   = cgPatternType;
            return true;
        }
        releaseCachedPattern();
        cached_wid = wid; cached_hei = hei; cached_x = x; cached_y = y;

        if (useBmp)
        {
            GdiBitmap *surf = (GdiBitmap *)GetGdiObjPtr(data.bmp);
            if (!surf) return false;
            CGImageRef tileImg = CreateCGImageFromBitmap(data.bmp);
            if (!tileImg) return false;
            CGPatternRef pat = createBitmapPattern(tileImg, x, y);
            CGImageRelease(tileImg);
            if (!pat) return false;
            cgPattern     = (void *)pat;
            cgPatternType = 1;
            outPattern    = cgPattern;
            outType       = 1;
            return true;
        }
        else
        {
            if (!data.gradientDetail || data.gradientDetail->items.empty()) return false;
            const GradientDetail &gd = *data.gradientDetail;
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGFunctionRef func = nullptr;
            if (gd.info.type == grad_linear)
            {
                fPoint endPts[2];
                calc_linear_endpoint(gd.info.angle, wid, hei, endPts);
                endPts[0].fX += x; endPts[0].fY += y;
                endPts[1].fX += x; endPts[1].fY += y;
                // Build gradient function from color stops
                size_t n = gd.items.size();
                std::vector<CGFloat> locations(n);
                std::vector<CGFloat> colors(n * 4);
                for (size_t i = 0; i < n; i++)
                {
                    CairoColor cr(gd.items[i].cr);
                    CGFloat a = (CGFloat)(cr.a * alpha);
                    locations[i]     = (CGFloat)gd.items[i].pos;
                    colors[i * 4 + 0] = (CGFloat)cr.r;
                    colors[i * 4 + 1] = (CGFloat)cr.g;
                    colors[i * 4 + 2] = (CGFloat)cr.b;
                    colors[i * 4 + 3] = a;
                }
                CGGradientRef gradient = CGGradientCreateWithColorComponents(cs, &colors[0], &locations[0], n);
                CGColorSpaceRelease(cs);
                if (!gradient) return false;
                GradientDrawInfo *gdi = new GradientDrawInfo;
                gdi->gradient = gradient;
                gdi->isRadial = false;
                gdi->p0 = CGPointMake(endPts[0].fX, endPts[0].fY);
                gdi->p1 = CGPointMake(endPts[1].fX, endPts[1].fY);
                gdi->r0 = gdi->r1 = 0;
                cgPattern     = (void *)gdi;
                cgPatternType = 2;
                outPattern    = cgPattern;
                outType       = 2;
                return true;
            }
            else if (gd.info.type == grad_radial)
            {
                float cx0 = (float)(wid * gd.info.radial.centerX + x);
                float cy0 = (float)(hei * gd.info.radial.centerY + y);
                float r   = (float)gd.info.radial.radius;
                size_t n = gd.items.size();
                std::vector<CGFloat> locations(n);
                std::vector<CGFloat> colors(n * 4);
                for (size_t i = 0; i < n; i++)
                {
                    CairoColor cr(gd.items[i].cr);
                    CGFloat a = (CGFloat)(cr.a * alpha);
                    locations[i]     = (CGFloat)gd.items[i].pos;
                    colors[i * 4 + 0] = (CGFloat)cr.r;
                    colors[i * 4 + 1] = (CGFloat)cr.g;
                    colors[i * 4 + 2] = (CGFloat)cr.b;
                    colors[i * 4 + 3] = a;
                }
                CGGradientRef gradient = CGGradientCreateWithColorComponents(cs, &colors[0], &locations[0], n);
                CGColorSpaceRelease(cs);
                if (!gradient) return false;
                GradientDrawInfo *gdi = new GradientDrawInfo;
                gdi->gradient = gradient;
                gdi->isRadial = true;
                gdi->p0 = CGPointMake(cx0, cy0);
                gdi->p1 = CGPointMake(cx0, cy0);
                gdi->r0 = 0;
                gdi->r1 = (CGFloat)r;
                cgPattern     = (void *)gdi;
                cgPatternType = 2;
                outPattern    = cgPattern;
                outType       = 2;
                return true;
            }
            CGColorSpaceRelease(cs);
            return false;
        }
    }
};

static void gdi_brush_free(void *ptr)
{
    LOGBRUSH *plogbr = (LOGBRUSH *)ptr;
    if (plogbr->lbStyle == BS_PATTERN)
    {
        PatternInfo *info = (PatternInfo *)plogbr->lbHatch;
        delete info;
    }
    delete plogbr;
}

static void gdi_font_free(void *ptr)
{
    delete (LOGFONTA *)ptr;
}

HGDIOBJ InitGdiObj(int type, void *ptr)
{
    FreeHandlePtr cbFree = nullptr;
    switch (type)
    {
    case OBJ_BITMAP:
        cbFree = gdi_bmp_free;
        break;
    case OBJ_PEN:
        cbFree = gdi_pen_free;
        break;
    case OBJ_BRUSH:
        cbFree = gdi_brush_free;
        break;
    case OBJ_FONT:
        cbFree = gdi_font_free;
        break;
    }
    return new _Handle(type, ptr, cbFree);
}

int GetObjectType(HGDIOBJ hgdiobj)
{
    return hgdiobj->type;
}

void *GetGdiObjPtr(HGDIOBJ hgdiobj)
{
    return hgdiobj->ptr;
}

void SetGdiObjPtr(HGDIOBJ hgdiObj, void *ptr)
{
    hgdiObj->ptr = ptr;
}

HGDIOBJ RefGdiObj(HGDIOBJ hgdiObj)
{
    return AddHandleRef(hgdiObj);
}

static bool IsNullPen(HPEN hpen)
{
    LOGPEN *pen = (LOGPEN *)GetGdiObjPtr(hpen);
    return pen->lopnStyle == PS_NULL;
}

// Brush kind returned to callers.  Some drawing APIs need to branch on the brush type:
// - kBrushColor:   fill uses CGContextFillPath/FillRect after SetRGBFillColor
// - kBrushPattern: fill uses CGContextFillPath/FillRect after SetFillPattern (colored pattern)
// - kBrushShading: fill uses CGContextDrawLinearGradient/CGContextDrawRadialGradient instead of FillPath/FillRect
enum BrushKind { kBrushColor = 0, kBrushPattern = 1, kBrushShading = 2 };

static bool ApplyBrush(HDC hdc, HBRUSH hbr, double wid, double hei, double x, double y,
                       BrushKind *outKind = nullptr, void **outPatternObj = nullptr);


static bool ApplyPen(HDC hdc, HPEN hpen, double wid, double hei, double x, double y)
{
    CGContextRef ctx = hdc->cgCtx;
    LOGPEN *pen = (LOGPEN *)GetGdiObjPtr(hpen);
    if (pen->lopnStyle == PS_NULL)
        return false;

    // Cosmetic pens always have width 1; geometric pens use the specified width
    double width = pen->lopnWidth.x;
    CGContextSetLineWidth(ctx, width);

    // For pens created via ExtCreatePen (identified by PS_USERSTYLE), apply the brush if one is attached
    if ((pen->lopnStyle & PS_STYLE_MASK) == PS_USERSTYLE)
    {
        LOGPENEX *lpex = (LOGPENEX *)pen;
        if (lpex->patternBrush)
        {
            double patternWidth = (wid > 0) ? wid : 1.0;
            double patternHeight = (hei > 0) ? hei : 1.0;
            if (!ApplyBrush(hdc, lpex->patternBrush, patternWidth, patternHeight, x, y))
            {
                CairoColor cr(pen->lopnColor);
                CGContextSetRGBStrokeColor(ctx, cr.r, cr.g, cr.b, cr.a);
            }
        }
        else
        {
            CairoColor cr(pen->lopnColor);
            CGContextSetRGBStrokeColor(ctx, cr.r, cr.g, cr.b, cr.a);
        }
    }
    else
    {
        CairoColor cr(pen->lopnColor);
        CGContextSetRGBStrokeColor(ctx, cr.r, cr.g, cr.b, cr.a);
    }
    
    // Handle dash patterns for line style
    switch (pen->lopnStyle & PS_STYLE_MASK)
    {
    case PS_DASH:
    {
        static CGFloat dashes_dash[] = { 5.0, 5.0 };
        CGContextSetLineDash(ctx, 0.0, dashes_dash, ARRAYSIZE(dashes_dash));
    }
    break;
    case PS_DOT:
    {
        static CGFloat dashes_dot[] = { 1.0, 3.0 };
        CGContextSetLineDash(ctx, 0.0, dashes_dot, ARRAYSIZE(dashes_dot));
    }
    break;
    case PS_DASHDOT:
    {
        static CGFloat dashes_dashdot[] = { 5.0, 5.0, 1.0, 5.0 };
        CGContextSetLineDash(ctx, 0.0, dashes_dashdot, ARRAYSIZE(dashes_dashdot));
    }
    break;
    case PS_DASHDOTDOT:
    {
        static CGFloat dashes_dashdotdot[] = { 5.0, 5.0, 1.0, 5.0, 1.0, 5.0 };
        CGContextSetLineDash(ctx, 0.0, dashes_dashdotdot, ARRAYSIZE(dashes_dashdotdot));
    }
    break;
    case PS_SOLID:
    {
        CGContextSetLineDash(ctx, 0.0, nullptr, 0);
    }
    break;
    case PS_USERSTYLE:
    {
        LOGPENEX *lp = (LOGPENEX *)pen;
        CGContextSetLineDash(ctx, 0.0, (const CGFloat*)lp->dash.data(), lp->dash.size());
    }
    break;
    }
    CGContextSetLineCap(ctx, kCGLineCapSquare);
    switch (pen->lopnStyle & PS_ENDCAP_MASK)
    {
    case PS_ENDCAP_ROUND:
        CGContextSetLineCap(ctx, kCGLineCapRound);
        break;
    case PS_ENDCAP_SQUARE:
        CGContextSetLineCap(ctx, kCGLineCapSquare);
        break;
    case PS_ENDCAP_FLAT:
        CGContextSetLineCap(ctx, kCGLineCapButt);
        break;
    }
    CGContextSetLineJoin(ctx, kCGLineJoinMiter);
    switch (pen->lopnStyle & PS_JOIN_MASK)
    {
    case PS_JOIN_ROUND:
        CGContextSetLineJoin(ctx, kCGLineJoinRound);
        break;
    case PS_JOIN_BEVEL:
        CGContextSetLineJoin(ctx, kCGLineJoinBevel);
        break;
    case PS_JOIN_MITER:
        CGContextSetLineJoin(ctx, kCGLineJoinMiter);
        break;
    }
    return true;
}

// must call after ApplyPen, ApplyBrush
static void ApplyRop2(CGContextRef cr, int rop2)
{
    switch (rop2)
    {
    case R2_BLACK:
        CGContextSetRGBStrokeColor(cr, 0.0, 0.0, 0.0, 1.0);
        CGContextSetRGBFillColor(cr, 0.0, 0.0, 0.0, 1.0);
        CGContextSetBlendMode(cr, kCGBlendModeNormal);
        break;
    case R2_WHITE:
        CGContextSetRGBStrokeColor(cr, 1.0, 1.0, 1.0, 1.0);
        CGContextSetRGBFillColor(cr, 1.0, 1.0, 1.0, 1.0);
        CGContextSetBlendMode(cr, kCGBlendModeNormal);
        break;
    case R2_NOT:
    case DSTINVERT:
        CGContextSetBlendMode(cr, kCGBlendModeXOR);
        break;
    case R2_NOP:
        CGContextSetAlpha(cr, 0.0);
        break;
    case R2_COPYPEN:
    case SRCCOPY:
        CGContextSetBlendMode(cr, kCGBlendModeCopy);
        break;
    case R2_EXT_OVER:
        CGContextSetBlendMode(cr, kCGBlendModeNormal);
        break;
    case R2_EXT_IN:
        CGContextSetBlendMode(cr, kCGBlendModeSourceIn);
        break;
    case R2_EXT_OUT:
        CGContextSetBlendMode(cr, kCGBlendModeSourceOut);
        break;
    case R2_EXT_ATOP:
        CGContextSetBlendMode(cr, kCGBlendModeSourceAtop);
        break;
    case R2_EXT_DEST:
        CGContextSetBlendMode(cr, kCGBlendModeDestinationOver);
        break;
    case R2_EXT_DEST_OVER:
        CGContextSetBlendMode(cr, kCGBlendModeDestinationOver);
        break;
    case R2_EXT_DEST_IN:
        CGContextSetBlendMode(cr, kCGBlendModeDestinationIn);
        break;
    case R2_EXT_DEST_OUT:
        CGContextSetBlendMode(cr, kCGBlendModeDestinationOut);
        break;
    case R2_EXT_DEST_ATOP:
        CGContextSetBlendMode(cr, kCGBlendModeDestinationAtop);
        break;
    case R2_EXT_XOR:
        CGContextSetBlendMode(cr, kCGBlendModeXOR);
        break;
    case SRCAND:
    case R2_EXT_ADD:
        CGContextSetBlendMode(cr, kCGBlendModePlusLighter);
        break;
    case R2_EXT_SATURATE:
        CGContextSetBlendMode(cr, kCGBlendModeNormal);
        break;
    case SRCINVERT:
        CGContextSetBlendMode(cr, kCGBlendModeXOR);
        break;
    case R2_EXT_CLEAR:
        CGContextSetBlendMode(cr, kCGBlendModeClear);
        break;
    default:
        printf("not supported rop2: %d\n", rop2);
        break;
    }
}

static bool IsNullBrush(HBRUSH hbr)
{
    if (IS_INTRESOURCE(hbr))
        return false;
    LOGBRUSH *br = (LOGBRUSH *)GetGdiObjPtr(hbr);
    return br->lbStyle == BS_NULL;
}

static bool IsPatternBrush(HBRUSH hbr)
{
    if (IS_INTRESOURCE(hbr))
        return false;
    LOGBRUSH *br = (LOGBRUSH *)GetGdiObjPtr(hbr);
    return br->lbStyle == BS_PATTERN;
}

static bool ApplyBrush(HDC hdc, HBRUSH hbr, double wid, double hei, double x, double y,
                       BrushKind *outKind, void **outPatternObj)
{
    CGContextRef ctx = hdc->cgCtx;
    BrushKind kind = kBrushColor;
    void *patternObj = nullptr;
    if (hbr == 0)
        return false;
    if (IS_INTRESOURCE(hbr))
    {
        hbr = GetSysColorBrush((int)(UINT_PTR)hbr - 1);
        if (!hbr)
            return false;
    }
    if (hbr->type != OBJ_BRUSH)
        return false;
    LOGBRUSH *br = (LOGBRUSH *)GetGdiObjPtr(hbr);
    if (br->lbStyle == BS_NULL)
        return false;
    // Helper: set both fill and stroke color so ApplyBrush works for both fill and pen contexts.
    // (For pen-using pattern brushes we still normally fall through to color.)
    auto setColor = [&](double r, double g, double b, double a) {
        CGContextSetRGBFillColor(ctx, r, g, b, a);
        CGContextSetRGBStrokeColor(ctx, r, g, b, a);
    };
    bool ret = true;
    switch (br->lbStyle)
    {
    case BS_SOLID:
    {
        CairoColor cr(br->lbColor);
        setColor(cr.r, cr.g, cr.b, cr.a);
    }
    break;
    case BS_PATTERN:
    {
        PatternInfo *info = (PatternInfo *)br->lbHatch;
        if (!info) {
            setColor(1.0, 1.0, 1.0, 1.0);
            break;
        }
        // bitmap pattern: 使用 brushOrg 作为 pattern 平铺起点（对齐 cairo）
        if (info->useBmp) {
            x = -hdc->brushOrg.x;
            y = -hdc->brushOrg.y;
        }
        void *pat = nullptr;
        int patType = 0;
        if (info->getOrCreate(ctx, wid, hei, x, y, pat, patType))
        {
            if (patType == 1)
            {
                // Colored CGPattern: fill with CGContextSetFillPattern + pattern colorSpace
                CGPatternRef pattern = (CGPatternRef)pat;
                CGColorSpaceRef cs = CGColorSpaceCreatePattern(NULL);
                CGContextSetFillColorSpace(ctx, cs);
                CGContextSetStrokeColorSpace(ctx, cs);
                CGFloat alpha = (CGFloat)info->alpha;
                CGContextSetFillPattern(ctx, pattern, &alpha);
                CGContextSetStrokePattern(ctx, pattern, &alpha);
                CGColorSpaceRelease(cs);
                kind = kBrushPattern;
                patternObj = pat;
            }
            else if (patType == 2)
            {
                // GradientDrawInfo: callers should use CGContextDrawLinearGradient/RadialGradient (no fill/stroke call).
                // We still set a solid fallback color just in case the caller falls back.
                CairoColor cr(info->data.gradientDetail->items[0].cr);
                cr.a *= info->alpha;
                setColor(cr.r, cr.g, cr.b, cr.a);
                kind = kBrushShading;
                patternObj = pat;
            }
            else
            {
                setColor(1.0, 1.0, 1.0, 1.0);
            }
        }
        else
        {
            setColor(1.0, 1.0, 1.0, 1.0);
        }
    }
    break;
    default:
        ret = false;
    }
    if (outKind) *outKind = kind;
    if (outPatternObj) *outPatternObj = patternObj;
    return ret;
}

static void _ClearPathIfLeft(CGContextRef ctx) {
    if (!CGContextIsPathEmpty(ctx)) {
        CGContextBeginPath(ctx);
    }
}

// Draw the current path in ctx using the DC's brush and pen.
// Handles three cases: fill only, stroke only, or both fill and stroke.
// The caller must build the path in ctx before calling this function.
static void DrawPathFillStroke(CGContextRef ctx, HDC hdc, double wid, double hei, double x, double y)
{
    ApplyRop2(ctx, hdc->rop2);
    BrushKind brushKind = kBrushColor;
    void *patternObj = nullptr;
    BOOL hasBrush = ApplyBrush(hdc, hdc->brush, wid, hei, x, y, &brushKind, &patternObj);
    BOOL hasPen   = ApplyPen(hdc, hdc->pen,   wid, hei, x, y);

    if (hasBrush && brushKind == kBrushShading)
    {
        // Gradient fill: clip to current path, draw gradient, keep path for optional stroke.
        GradientDrawInfo *gdi = (GradientDrawInfo *)patternObj;
        CGContextSaveGState(ctx);
        CGContextClip(ctx); // consumes path, so stroke needs re-add
        if (gdi->isRadial)
            CGContextDrawRadialGradient(ctx, gdi->gradient, gdi->p0, gdi->r0, gdi->p1, gdi->r1, 0);
        else
            CGContextDrawLinearGradient(ctx, gdi->gradient, gdi->p0, gdi->p1, 0);
        CGContextRestoreGState(ctx);
        if (hasPen) {
            // Re-add path for stroke. Since we don't have it, clear it and leave stroke alone.
            // (Shading + stroke is rare; leave stroke skipped for correctness, caller expects fill anyway)
            _ClearPathIfLeft(ctx);
        }
        return;
    }

    if (hasBrush && hasPen) {
        CGContextDrawPath(ctx, kCGPathFillStroke);
    } else if (hasBrush) {
        CGContextFillPath(ctx);
    } else if (hasPen) {
        CGContextStrokePath(ctx);
    } else {
        _ClearPathIfLeft(ctx);
    }
}

// Stroke the current path in ctx using the DC's pen.
// The caller must build the path in ctx before calling this function.
static void DrawPathStroke(CGContextRef ctx, HDC hdc, double wid, double hei, double x, double y)
{
    ApplyRop2(ctx, hdc->rop2);
    if (ApplyPen(hdc, hdc->pen, wid, hei, x, y))
    {
        CGContextStrokePath(ctx);
    }
    else
    {
        _ClearPathIfLeft(ctx);
    }
}

static BOOL ApplyFont(HDC hdc)
{
    if (hdc->hfont)
    {
        LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
        if (!hdc->cgCtx)
            return FALSE;
        CGContextRef ctx = hdc->cgCtx;
        const char *fontName = lf->lfFaceName;
        bool needResolve = false;
        for (const char *p = lf->lfFaceName; *p; ++p)
        {
            if ((*p) & 0x80)
            {
                needResolve = true;
                break;
            }
        }
        if (needResolve)
        {
            static std::mutex mutex;
            static std::map<std::string, std::string> fontMap;
            std::lock_guard<std::mutex> lock(mutex);
            auto it = fontMap.find(lf->lfFaceName);
            if (it != fontMap.end())
            {
                fontName = it->second.c_str();
            }
            else
            {
                //todo: hjx

                // FcPattern *pat = FcPatternCreate();
                // FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)lf->lfFaceName);
                // FcConfigSubstitute(NULL, pat, FcMatchPattern);
                // FcDefaultSubstitute(pat);
                // FcResult result;
                // FcPattern *font = FcFontMatch(NULL, pat, &result);
                // if (font)
                // {
                //     FcChar8 *family = NULL;
                //     if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch && family)
                //     {
                //         char szFaceName[LF_FACESIZE];
                //         strncpy(szFaceName, (const char *)family, LF_FACESIZE - 1);
                //         szFaceName[LF_FACESIZE - 1] = '\0';
                //         auto res = fontMap.insert(std::make_pair(lf->lfFaceName, szFaceName));
                //         assert(res.second);
                //         fontName = res.first->second.c_str();
                //     }
                //     FcPatternDestroy(font);
                // }
                // if (fontName == lf->lfFaceName)
                // {
                //     fontMap.insert(std::make_pair(lf->lfFaceName, fontName));
                // }
                // FcPatternDestroy(pat);
            }
        }
        CGContextSelectFont(ctx, fontName[0] ? fontName : "Helvetica", abs(lf->lfHeight), kCGEncodingMacRoman);
        if (lf->lfWeight > 400)
        {
        }
        return TRUE;
    }
    return FALSE;
}

static CTFontRef CreateCTFontFromDC(HDC hdc)
{
    if (!hdc->hfont)
        return NULL;
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    if (!lf)
        return NULL;
    const char *fontName = lf->lfFaceName;
    bool needResolve = false;
    for (const char *p = lf->lfFaceName; *p; ++p)
    {
        if ((*p) & 0x80)
        {
            needResolve = true;
            break;
        }
    }
    if (needResolve)
    {
        static std::mutex mutex;
        static std::map<std::string, std::string> fontMap;
        std::lock_guard<std::mutex> lock(mutex);
        auto it = fontMap.find(lf->lfFaceName);
        if (it != fontMap.end())
        {
            fontName = it->second.c_str();
        }
        else
        {
            //todo:hjx
            // FcPattern *pat = FcPatternCreate();
            // FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)lf->lfFaceName);
            // FcConfigSubstitute(NULL, pat, FcMatchPattern);
            // FcDefaultSubstitute(pat);
            // FcResult result;
            // FcPattern *font = FcFontMatch(NULL, pat, &result);
            // if (font)
            // {
            //     FcChar8 *family = NULL;
            //     if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch && family)
            //     {
            //         char szFaceName[LF_FACESIZE];
            //         strncpy(szFaceName, (const char *)family, LF_FACESIZE - 1);
            //         szFaceName[LF_FACESIZE - 1] = '\0';
            //         auto res = fontMap.insert(std::make_pair(lf->lfFaceName, szFaceName));
            //         assert(res.second);
            //         fontName = res.first->second.c_str();
            //     }
            //     FcPatternDestroy(font);
            // }
            // if (fontName == lf->lfFaceName)
            // {
            //     fontMap.insert(std::make_pair(lf->lfFaceName, fontName));
            // }
            // FcPatternDestroy(pat);
        }
    }
    CFStringRef cfFontName = CFStringCreateWithCString(NULL, fontName[0] ? fontName : "Helvetica", kCFStringEncodingUTF8);
    CGFloat fontSize = abs(lf->lfHeight) > 0 ? (CGFloat)abs(lf->lfHeight) : 12.0;
    CTFontSymbolicTraits traits = 0;
    if (lf->lfWeight >= FW_BOLD)
        traits |= kCTFontBoldTrait;
    if (lf->lfItalic)
        traits |= kCTFontItalicTrait;
    CTFontRef ctFont = NULL;
    if (traits != 0)
    {
        CTFontRef baseFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
        if (baseFont)
        {
            ctFont = CTFontCreateCopyWithSymbolicTraits(baseFont, fontSize, NULL, traits, traits);
            CFRelease(baseFont);
        }
    }
    if (!ctFont)
    {
        ctFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
    }
    CFRelease(cfFontName);
    return ctFont;
}

static CTLineRef CreateCTLineWithDC(HDC hdc, LPCSTR str, int c, CGFloat *outAscent, CGFloat *outDescent, CGFloat *outAdvance)
{
    if (!str || c <= 0)
        return NULL;
    CTFontRef ctFont = CreateCTFontFromDC(hdc);
    if (!ctFont)
        return NULL;
    LOGFONTA *lf = hdc->hfont ? (LOGFONTA *)GetGdiObjPtr(hdc->hfont) : NULL;
    CairoColor crText(hdc->crText);
    CGColorRef textColor = CGColorCreateGenericRGB(crText.r, crText.g, crText.b, crText.a);
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 4, NULL, NULL);
    CFDictionarySetValue(attrs, kCTFontAttributeName, ctFont);
    CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, textColor);
    if (lf && lf->lfUnderline)
    {
        int style = kCTUnderlineStyleSingle;
        CFNumberRef underlineVal = CFNumberCreate(NULL, kCFNumberIntType, &style);
        CFDictionarySetValue(attrs, kCTUnderlineStyleAttributeName, underlineVal);
        CFRelease(underlineVal);
    }
    if (lf && lf->lfStrikeOut)
    {
        int style = kCTUnderlineStyleSingle;
        CFNumberRef strikeVal = CFNumberCreate(NULL, kCFNumberIntType, &style);
        CFDictionarySetValue(attrs, kCTStrikethroughStyleAttributeName, strikeVal);
        CFRelease(strikeVal);
    }
    CFStringRef cfStr = CFStringCreateWithBytes(NULL, (const UInt8 *)str, c, kCFStringEncodingUTF8, false);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, cfStr, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);
    if (line && (outAscent || outDescent || outAdvance))
    {
        // 注意：必须使用字体级 ascent/descent（CTFontGetAscent/CTFontGetDescent），
        // 而非 CTLineGetTypographicBounds 返回的行级 ascent。
        //
        // 原因：CTLineDraw 渲染 glyph 时，glyph 位置基于字体坐标系，glyph 顶部
        // 到基线的距离是"字体设计 ascent"（包含 internal leading）。
        // CTLineGetTypographicBounds 返回的 ascent 是行级（line 中所有 run 的
        // 实际字形 ascent 的最大值），通常比字体 ascent 小。
        //
        // 如果用行级 ascent 计算 drawY = top + line_ascent，则基线偏上，
        // CTLineDraw 实际把字符顶部画到 drawY - font_ascent < top，
        // 导致文字显示偏上（超过 pRect->top 边界）。
        //
        // 用字体级 ascent 计算 drawY = top + font_ascent 时，基线在
        // top + font_ascent，字符顶部在 top，正好对齐。
        CGFloat a = CTFontGetAscent(ctFont);
        CGFloat d = CTFontGetDescent(ctFont);
        CGFloat adv = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        if (outAscent) *outAscent = a;
        if (outDescent) *outDescent = d;
        if (outAdvance) *outAdvance = adv;
    }
    CFRelease(attrStr);
    CFRelease(cfStr);
    CFRelease(attrs);
    CFRelease(textColor);
    CFRelease(ctFont);
    return line;
}

static void ApplyRegion(CGContextRef ctx, HRGN hRgn)
{
    CGContextResetClip(ctx);
    DWORD dwCount = GetRegionData(hRgn, 0, nullptr);
    RGNDATA *pData = (RGNDATA *)malloc(dwCount);
    GetRegionData(hRgn, dwCount, pData);
    RECT *pRc = (RECT *)pData->Buffer;
    CGRect *rects = (CGRect *)malloc(sizeof(CGRect) * pData->rdh.nCount);
    for (int i = 0; i < pData->rdh.nCount; i++)
    {
        rects[i] = CGRectMake(pRc->left, pRc->top, pRc->right - pRc->left, pRc->bottom - pRc->top);
        pRc++;
    }
    CGContextClipToRects(ctx, rects, pData->rdh.nCount);
    free(rects);
    free(pData);
}

HPEN ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH *plbrush, DWORD cStyle, const DWORD *pstyle)
{
    if(!plbrush)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    // BS_NULL/BS_HOLLOW: create a null pen
    if (plbrush->lbStyle == BS_NULL || plbrush->lbStyle == BS_HOLLOW)
    {
        return CreatePen(PS_NULL, cWidth, plbrush->lbColor);
    }
    // BS_SOLID with no custom dash: use CreatePen with the style from iPenStyle
    if(plbrush->lbStyle == BS_SOLID && cStyle==0){
        return CreatePen(iPenStyle & PS_STYLE_MASK, cWidth, plbrush->lbColor);
    }
    if (plbrush->lbStyle == BS_PATTERN || plbrush->lbStyle == BS_BRUSH)
    {
        if ((iPenStyle & PS_TYPE_MASK) != PS_GEOMETRIC || !plbrush->lbHatch)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
    }
    LOGPENEX *lpex = new LOGPENEX;
    lpex->lopnColor = plbrush->lbColor;
    lpex->lopnWidth.x = cWidth;
    // Preserve endcap, join, and type from iPenStyle; use PS_USERSTYLE as style marker for LOGPENEX detection
    lpex->lopnStyle = (iPenStyle & ~PS_STYLE_MASK) | PS_USERSTYLE;
    if (plbrush->lbStyle == BS_PATTERN)
    {
        // Handle PS_PATTERN by creating a pattern brush
        HBITMAP hPatternBmp = (HBITMAP)plbrush->lbHatch;
        // Create a pattern brush from the bitmap
        HBRUSH hPatternBrush = CreatePatternBrush(hPatternBmp);
        if (!hPatternBrush)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            delete lpex;
            return 0;
        }
        // Store the pattern brush
        lpex->patternBrush = hPatternBrush;
    }
    else if (plbrush->lbStyle == BS_BRUSH)
    {
        lpex->patternBrush = (HBRUSH)RefGdiObj((HBRUSH)plbrush->lbHatch);
    }
    else
    {
        // BS_SOLID with custom dash, or other styles with user-defined dash pattern
        assert(cStyle > 0 && pstyle);
        lpex->dash.resize(cStyle);
        for (DWORD i = 0; i < cStyle; i++)
        {
            lpex->dash[i] = pstyle[i];
        }
    }

    return InitGdiObj(OBJ_PEN, lpex);
}

int GetObjectW(HGDIOBJ h, int c, LPVOID pv)
{
    if (h && h->type == OBJ_FONT)
    {
        if (c < sizeof(LOGFONTW))
            return 0;
        LOGFONTA lf;
        GetObjectA(h, sizeof(lf), &lf);
        LOGFONTW *lfw = (LOGFONTW *)pv;
        memcpy(lfw, &lf, FIELD_OFFSET(LOGFONTW, lfFaceName));
        MultiByteToWideChar(CP_UTF8, 0, lf.lfFaceName, -1, lfw->lfFaceName, LF_FACESIZE);
        return c;
    }
    else
    {
        return GetObjectA(h, c, pv);
    }
}

int GetObjectA(HGDIOBJ h, int c, LPVOID pv)
{
    if (!h || !h->ptr)
        return 0;
    int ret = 0;
    switch (h->type)
    {
    case OBJ_BITMAP:
        if (c >= sizeof(BITMAP))
        {
            BITMAP *bm = (BITMAP *)pv;
            GdiBitmap *pixmap = (GdiBitmap *)h->ptr;
            int fmt = pixmap->format;
            if (fmt == GDI_BMP_INVALID)
                return 0;
            bm->bmWidth = pixmap->width;
            bm->bmHeight = pixmap->height;
            bm->bmPlanes = 1;
            switch (fmt)
            {
            case GDI_BMP_A1:
                bm->bmBitsPixel = 1;
                break;
            case GDI_BMP_ARGB32:
                bm->bmBitsPixel = 32;
                break;
            case GDI_BMP_RGB24:
                bm->bmBitsPixel = 24;
                break;
            default:
                assert(0);
                break;
            }

            bm->bmWidthBytes = ((bm->bmWidth * bm->bmBitsPixel) / 8 + 3) / 4 * 4;
            bm->bmType = BI_RGB;
            bm->bmBits = pixmap->data;
            ret = sizeof(BITMAP);
        }
        break;
    case OBJ_FONT:
        if (c >= sizeof(LOGFONTA))
        {
            ret = sizeof(LOGFONTA);
            memcpy(pv, h->ptr, ret);
        }
        break;
    case OBJ_PEN:
        if (c >= sizeof(LOGPEN))
        {
            ret = sizeof(LOGPEN);
            memcpy(pv, h->ptr, ret);
        }
        break;
    case OBJ_BRUSH:
        if (c >= sizeof(LOGBRUSH))
        {
            ret = sizeof(LOGBRUSH);
            memcpy(pv, h->ptr, ret);
        }
        break;
    }
    return ret;
}

HPEN CreatePen(int iStyle, int cWidth, COLORREF color)
{
    LOGPEN logPen = { (UINT)iStyle, { cWidth, 0 }, color };
    return CreatePenIndirect(&logPen);
}

HPEN CreatePenIndirect(const LOGPEN *plpen)
{
    LOGPEN *pData = new LOGPEN;
    memcpy(pData, plpen, sizeof(LOGPEN));
    assert((plpen->lopnStyle & PS_STYLE_MASK) != PS_USERSTYLE); // PS_USERSTYLE should be created by ExtCreatePen
    return InitGdiObj(OBJ_PEN, pData);
}

HFONT CreateFontIndirectA(const LOGFONTA *lplf)
{
    LOGFONTA *plog = new LOGFONTA;
    memcpy(plog, lplf, sizeof(LOGFONTA));
    return InitGdiObj(OBJ_FONT, plog);
}

HFONT CreateFontIndirectW(CONST LOGFONTW *lplf)
{
    LOGFONTA lf;
    memcpy(&lf, lplf, FIELD_OFFSET(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_UTF8, 0, lplf->lfFaceName, -1, lf.lfFaceName, LF_FACESIZE, nullptr, nullptr);
    return CreateFontIndirectA(&lf);
}

HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
    LOGFONTA lf;
    lf.lfCharSet = iCharSet;
    lf.lfHeight = cHeight;
    lf.lfWidth = cWidth;
    lf.lfEscapement = cEscapement;
    lf.lfOrientation = cOrientation;
    lf.lfWeight = cWeight;
    lf.lfItalic = bItalic;
    lf.lfUnderline = bUnderline;
    lf.lfStrikeOut = bStrikeOut;
    lf.lfClipPrecision = iClipPrecision;
    lf.lfOutPrecision = iOutPrecision;
    lf.lfQuality = iQuality;
    lf.lfPitchAndFamily = iPitchAndFamily;
    strcpy_s(lf.lfFaceName, ARRAYSIZE(lf.lfFaceName), pszFaceName);
    return CreateFontIndirectA(&lf);
}

HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
    char facename[LF_FACESIZE];
    if (WideCharToMultiByte(CP_UTF8, 0, pszFaceName, -1, facename, LF_FACESIZE, nullptr, nullptr) == 0)
        return 0;
    return CreateFontA(cHeight, cWeight, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, facename);
}

HBITMAP CreateDIBitmap(HDC hdc, const BITMAPINFOHEADER *pbmih, DWORD flInit, const VOID *pjBits, const BITMAPINFO *pbmi, UINT iUsage)
{
    if (iUsage != DIB_RGB_COLORS)
        return nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, pbmi, 0, nullptr, 0, 0);
    if (bmp)
    {
        int stride = ((pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biBitCount / 8) + 3) / 4 * 4;
        UpdateDIBPixmap(bmp, pbmi->bmiHeader.biWidth, pbmi->bmiHeader.biHeight, pbmi->bmiHeader.biBitCount, stride, pjBits);
    }
    return bmp;
}

HBRUSH CreateDIBPatternBrush(HGLOBAL h, UINT iUsage)
{
    // todo:hjx
    return nullptr;
}

HBRUSH CreateDIBPatternBrushPt(const VOID *lpPackedDIB, UINT iUsage)
{
    BITMAPINFO *pInfo = (BITMAPINFO *)lpPackedDIB;
    HBITMAP bmp = CreateDIBitmap(nullptr, &pInfo->bmiHeader, 1, pInfo + 1, pInfo, iUsage);
    if (!bmp)
    {
        return nullptr;
    }
    HBRUSH ret = CreatePatternBrush(bmp);
    DeleteObject(bmp);
    return ret;
}

HBRUSH CreatePatternBrush(HBITMAP bmp)
{
    return CreatePatternBrush2(bmp, kTileMode_Repeat);
}

HBRUSH CreatePatternBrush2(HBITMAP bmp, TILEMODE tileMode)
{
    if (!bmp)
        return nullptr;
    LOGBRUSH *plog = new LOGBRUSH;
    plog->lbStyle = BS_PATTERN;
    PatternInfo *info = new PatternInfo;
    info->alpha = 1.0;
    info->useBmp = TRUE;
    info->data.bmp = RefGdiObj(bmp);
    info->tileMode = tileMode;
    plog->lbHatch = (UINT_PTR)info;
    return InitGdiObj(OBJ_BRUSH, plog);
}

HBRUSH CreateGradientBrush(const GRADIENTITEM *pGradients, int nCount, const GRADIENTINFO *grad_info, BYTE byAlpha, TILEMODE tileMode)
{
    LOGBRUSH *plog = new LOGBRUSH;
    plog->lbStyle = BS_PATTERN;
    PatternInfo *info = new PatternInfo;
    info->alpha = 1.0 * byAlpha / 255.0;
    info->useBmp = FALSE;
    info->tileMode = tileMode;
    info->data.gradientDetail = new GradientDetail;
    info->data.gradientDetail->info = *grad_info;
    info->data.gradientDetail->items.resize(nCount);
    memcpy(info->data.gradientDetail->items.data(), pGradients, nCount * sizeof(GRADIENTITEM));
    plog->lbHatch = (UINT_PTR)info;
    return InitGdiObj(OBJ_BRUSH, plog);
}

HBRUSH CreateSolidBrush(COLORREF color)
{
    LOGBRUSH *plog = new LOGBRUSH;
    plog->lbStyle = BS_SOLID;
    plog->lbColor = color;
    return InitGdiObj(OBJ_BRUSH, plog);
}

HBITMAP CreateDIBSection(HDC hdc, const BITMAPINFO *lpbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset)
{
    int fmt = GDI_BMP_INVALID;
    switch (lpbmi->bmiHeader.biBitCount)
    {
    case 1:
        fmt = GDI_BMP_A1;
        break;
    case 32:
        fmt = GDI_BMP_ARGB32;
        break;
    case 24:
        fmt = GDI_BMP_RGB24;
        break;
    }
    if (fmt == GDI_BMP_INVALID)
        return 0;
    GdiBitmap *ret = GdiBitmapCreate(lpbmi->bmiHeader.biWidth, abs(lpbmi->bmiHeader.biHeight), fmt);
    if (!ret)
        return 0;
    if (ppvBits)
    {
        *ppvBits = ret->data;
    }
    return InitGdiObj(OBJ_BITMAP, ret);
}

HBITMAP CreateDIBSectionEx(int bitsPixel, int wid,int hei,int stride, VOID *pvBits){
    int fmt = GDI_BMP_INVALID;
    switch (bitsPixel)
    {
    case 1:
        fmt = GDI_BMP_A1;
        break;
    case 32:
        fmt = GDI_BMP_ARGB32;
        break;
    case 24:
        fmt = GDI_BMP_RGB24;
        break;
    }
    if (fmt == GDI_BMP_INVALID)
        return 0;
    GdiBitmap *ret = GdiBitmapCreateForData((unsigned char*)pvBits, wid, hei, stride, fmt);
    if (!ret)
        return 0;
    return InitGdiObj(OBJ_BITMAP, ret);
}

BOOL UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits)
{
    BITMAP bm = { 0 };
    GetObject(bmp, sizeof(bm), &bm);
    if (!bm.bmBits)
        return FALSE;
    int absHei = abs(hei);
    if (bm.bmWidth != wid || bm.bmHeight != absHei || bm.bmBitsPixel != bitsPixel)
        return FALSE;
    int surfaceStride = ((GdiBitmap *)GetGdiObjPtr(bmp))->stride;
    if (pjBits)
    {
        if (stride == surfaceStride)
            memcpy(bm.bmBits, pjBits, absHei * stride);
        else
        {
            char *src = (char *)pjBits;
            char *dst = (char *)bm.bmBits;
            int fmt = ((GdiBitmap *)GetGdiObjPtr(bmp))->format;
            if (bitsPixel == 24 && fmt == GDI_BMP_RGB24)
            {
                for (int i = 0; i < absHei; i++)
                {
                    char *lsrc = src;
                    char *ldst = dst;
                    for (int j = 0; j < wid; j++)
                    {
                        memcpy(ldst, lsrc, 3);
                        ldst += 4;
                        lsrc += 3;
                    }
                    dst += surfaceStride;
                    src += stride;
                }
            }
            else if (bitsPixel == 1 && fmt == GDI_BMP_A1)
            {
                // copy from kimi
                for (int y = 0; y < absHei; y++)
                {
                    for (int x = 0; x < wid; x++)
                    {
                        int bitmap_index = (y * ((wid + 7) / 8) + (x / 8));
                        int bmp_index = (y * surfaceStride + (x / 8));
                        uint8_t bitmap_bit = (src[bitmap_index] >> (7 - (x % 8))) & 1;

                        if (bitmap_bit)
                        {
                            dst[bmp_index] |= (1 << (7 - (x % 8)));
                        }
                        else
                        {
                            dst[bmp_index] &= ~(1 << (7 - (x % 8)));
                        }
                    }
                }
            }
            else
            {
                SLOG_STMW() << "invalid pixel map";
            }
        }
    }
    else
        memset(bm.bmBits, 0, absHei * surfaceStride);
    MarkPixmapDirty(bmp);
    return TRUE;
}

void MarkPixmapDirty(HBITMAP bmp)
{
    if (bmp && bmp->type == OBJ_BITMAP)
    {
        GdiBitmap *surf = (GdiBitmap *)GetGdiObjPtr(bmp);
        if (surf)
            surf->markDirty();
    }
}

HDC CreateCompatibleDC(HDC hdc)
{
    HWND hwnd = 0;
    SConnection *conn = nullptr;
    if (hdc == 0)
    {
        conn = SConnMgr::instance()->getConnection();
        if (!conn)
            return nullptr;

        hwnd = conn->GetScreenWindow();
    }
    else
    {
        hwnd = hdc->hwnd;
        tid_t tid = GetWindowThreadProcessId(hwnd, nullptr);
        conn = SConnMgr::instance()->getConnection(tid);
        if (!conn)
            return nullptr;
    }
    HDC ret = new _SDC(hwnd);
    SelectObject(ret, conn->GetDesktopBitmap());
    return ret;
}

BOOL DeleteDC(HDC hdc)
{
    delete hdc;
    return TRUE;
}

int WINAPI GetBkMode(HDC hdc)
{
    return hdc->bkMode;
}

int SetBkMode(HDC hdc, int mode)
{
    int ret = hdc->bkMode;
    hdc->bkMode = mode;
    return ret;
}

int SetGraphicsMode(HDC hdc, int iMode)
{
    return 0;
}

HBITMAP CreateCompatibleBitmap(HDC hdc, int cx, int cy)
{
    BITMAPINFO bmi;
    bmi.bmiHeader.biBitCount = 32; // todo:hjx
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = cy;
    return CreateDIBSection(hdc, &bmi, 0, nullptr, 0, 0);
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h)
{
    HGDIOBJ ret = 0;
    assert(h);
    switch (h->type)
    {
    case OBJ_PEN:
    {
        ret = hdc->pen;
        hdc->pen = h;
        break;
    }
    case OBJ_BRUSH:
    {
        ret = hdc->brush;
        hdc->brush = h;
        break;
    }
    case OBJ_FONT:
    {
        ret = hdc->hfont;
        hdc->hfont = h;
        break;
    }
    case OBJ_BITMAP:
    {
        if (h == hdc->bmp)
            break;
        ret = hdc->bmp;
        if (hdc->cgCtx && hdc->cgCtxOwned)
        {
            CGContextRelease(hdc->cgCtx);
            hdc->cgCtx = nullptr;
            hdc->cgCtxOwned = FALSE;
            // CGBitmapContext 可能已绘制到旧 bmp 的 data，标记 dirty 使 CGImageRef 失效
            if (ret)
            {
                GdiBitmap *oldSurf = (GdiBitmap *)GetGdiObjPtr(ret);
                if (oldSurf)
                    oldSurf->markDirty();
            }
        }
        if (GetGdiObjPtr(h))
        {
            BITMAP bm;
            GetObject(h,sizeof(bm),&bm);
            GdiBitmap *surf = (GdiBitmap *)GetGdiObjPtr(h);
            int width = surf->width;
            int height = surf->height;
            int stride = surf->stride;
            unsigned char *data = surf->data;
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            
            if (colorSpace && data && width >= 0 && height >= 0 && bm.bmBitsPixel==32)
            {
                CGBitmapInfo bmi = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
                if(width==0 || height==0){
                    static char emptyCanvas[4];
                    hdc->cgCtx = CGBitmapContextCreate(emptyCanvas, 1, 1, 8, 4, colorSpace, bmi);
                }else{
                    hdc->cgCtx = CGBitmapContextCreate(data, width, height, 8, stride, colorSpace, bmi);
                }
                CGColorSpaceRelease(colorSpace);
                if (hdc->cgCtx)
                {
                    hdc->cgCtxOwned = TRUE;
                    *hdc->worldMtx = CGAffineTransformIdentity;
                    hdc->ptOrigin.x = hdc->ptOrigin.y = 0;
                    // 新 CG context 的 CGContextSaveGState 栈是空的，必须同步清理 HDC 的
                    // stateStack 和 nSave，否则旧 context 上遗留的 SaveDC 状态会导致
                    // RestoreDC 在新 context 上 CGContextRestoreGState 无效（无对应 save），
                    // CTM 和 clip 无法正确恢复。
                    hdc->stateStack.clear();
                    hdc->nSave = 0;
                    CGContextTranslateCTM(hdc->cgCtx, 0, height);
                    CGContextScaleCTM(hdc->cgCtx, 1.0, -1.0);
                }
                hdc->bmp = h;
            }
        }
        break;
    }
    }
    return ret;
}

BOOL DeleteObject(HGDIOBJ hObj)
{
    return CloseHandle(hObj);
}

int SaveDC(HDC hdc)
{
    return hdc->SaveState();
}

BOOL RestoreDC(HDC hdc, int nSavedDC)
{
    return hdc->RestoreState(nSavedDC);
}

int GetClipRgn(HDC hdc, HRGN hrgn)
{
    if (!hdc->cgCtx)
        return 0;
    CGRect bb = CGContextGetClipBoundingBox(hdc->cgCtx);
    RECT rcClip;
    rcClip.left = bb.origin.x;
    rcClip.top = bb.origin.y;
    rcClip.right = bb.origin.x + bb.size.width;
    rcClip.bottom = bb.origin.y + bb.size.height;
    HRGN rgnSrc = CreateRectRgnIndirect(&rcClip);
    CombineRgn(hrgn, rgnSrc, nullptr, RGN_COPY);
    DeleteObject(rgnSrc);
    return RgnComplexity(hrgn) == NULLREGION ? 0 : 1;
}

int SelectClipRgn(HDC hdc, HRGN hrgn)
{
    if (!hdc->cgCtx)
        return 0;
    ApplyRegion(hdc->cgCtx, hrgn);
    return RgnComplexity(hrgn);
}

// 将区域作为交集应用到 CG context（不 reset clip，直接 CGContextClipToRects）
// CGContextClipToRects 的语义是：result = current_clip ∩ rects，正好等于 RGN_AND。
static void ApplyRegionIntersect(CGContextRef ctx, HRGN hRgn)
{
    DWORD dwCount = GetRegionData(hRgn, 0, nullptr);
    if (dwCount == 0) return;
    RGNDATA *pData = (RGNDATA *)malloc(dwCount);
    GetRegionData(hRgn, dwCount, pData);
    RECT *pRc = (RECT *)pData->Buffer;
    CGRect *rects = (CGRect *)malloc(sizeof(CGRect) * pData->rdh.nCount);
    for (int i = 0; i < pData->rdh.nCount; i++)
    {
        rects[i] = CGRectMake(pRc->left, pRc->top, pRc->right - pRc->left, pRc->bottom - pRc->top);
        pRc++;
    }
    CGContextClipToRects(ctx, rects, pData->rdh.nCount);
    free(rects);
    free(pData);
}

int ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode)
{
    if (mode == RGN_COPY)
    {
        if (!hdc->cgCtx)
            return 0;
        ApplyRegion(hdc->cgCtx, hrgn);
        return 0;
    }
    else if (mode == RGN_AND)
    {
        // RGN_AND: result = current_clip ∩ input_region
        // 直接用 CGContextClipToRects 做交集，不通过 GetClipRgn 获取包围盒。
        // GetClipRgn 只返回包围盒（CGContextGetClipBoundingBox），当 current_clip
        // 是非矩形时，包围盒大于实际 clip，导致结果 clip 过大。
        // CGContextClipToRects 的语义本身就是交集，直接使用即可。
        if (!hdc->cgCtx)
            return 0;
        ApplyRegionIntersect(hdc->cgCtx, hrgn);
        return 0;
    }
    else
    {
        HRGN rgnNow = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, rgnNow);
        int ret = CombineRgn(rgnNow, rgnNow, hrgn, mode);
        if (hdc->cgCtx)
            ApplyRegion(hdc->cgCtx, rgnNow);
        DeleteObject(rgnNow);
        return ret;
    }
}

int ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    HRGN hrgn = CreateRectRgn(left, top, right, bottom);
    int ret = ExtSelectClipRgn(hdc, hrgn, RGN_DIFF);
    DeleteObject(hrgn);
    return ret;
}

int IntersectClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    HRGN hrgn = CreateRectRgn(left, top, right, bottom);
    int ret = ExtSelectClipRgn(hdc, hrgn, RGN_AND);
    DeleteObject(hrgn);
    return ret;
}

HGDIOBJ GetCurrentObject(HDC hdc, UINT type)
{
    switch (type)
    {
    case OBJ_PEN:
        return hdc->pen;
    case OBJ_BRUSH:
        return hdc->brush;
    case OBJ_BITMAP:
        return hdc->bmp;
    case OBJ_FONT:
        return hdc->hfont;
    }
    return HGDIOBJ(0);
}

int GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage)
{
    return 0;
}

// 检查矩阵是否是单位矩阵
static int matrix_is_identity(const CGAffineTransform *matrix)
{
    return CGAffineTransformIsIdentity(*matrix);
}

static bool matrix_inverse(double A[3][3], double A_inv[3][3])
{
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);

    if (det == 0)
    {
        // printf("矩阵不可逆，因为行列式为零。\n");
        return false;
    }

    A_inv[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det;
    A_inv[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) / det;
    A_inv[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det;
    A_inv[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) / det;
    A_inv[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) / det;
    A_inv[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) / det;
    A_inv[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det;
    A_inv[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) / det;
    A_inv[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det;
    return true;
}

static bool cg_matrix_inverse(const CGAffineTransform *src, CGAffineTransform *inv)
{
    if (!CGAffineTransformIsInvertible(*src))
        return false;
    *inv = CGAffineTransformInvert(*src);
    return true;
}

BOOL InvertRgn(HDC hdc, HRGN hrgn)
{
    if (!hrgn)
        return FALSE;
    if (!hdc->cgCtx)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextSetBlendMode(ctx, kCGBlendModeXOR);
    CGContextFillRect(ctx, CGRectMake(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
    CGContextRestoreGState(ctx);
    return TRUE;
}

int GetClipBox(HDC hdc, LPRECT lprect)
{
    if (!hdc->cgCtx)
    {
        SetRectEmpty(lprect);
        return NULLREGION;
    }
    CGRect bb = CGContextGetClipBoundingBox(hdc->cgCtx);
    lprect->left = bb.origin.x;
    lprect->top = bb.origin.y;
    lprect->right = bb.origin.x + bb.size.width;
    lprect->bottom = bb.origin.y + bb.size.height;
    if (IsRectEmpty(lprect))
        return NULLREGION;
    return COMPLEXREGION;
}

BOOL FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)
{
    if (!hrgn || GetObjectType(hrgn) != OBJ_REGION)
        return FALSE;
    BOOL ret = FALSE;
    if (!hdc->cgCtx)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    double wid = rc.right - rc.left, hei = rc.bottom - rc.top;
    if (ApplyBrush(hdc, hbr, wid, hei, rc.left, rc.top))
    {
        ApplyRop2(ctx, hdc->rop2);
        CGContextFillRect(ctx, CGRectMake(0, 0, wid, hei));
        ret = TRUE;
    }
    CGContextRestoreGState(ctx);
    return ret;
}

BOOL FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr, int nWidth, int nHeight)
{
    if (!hrgn || GetObjectType(hrgn) != OBJ_REGION)
        return FALSE;
    if (!hdc->cgCtx)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    double rgn_wid = rc.right - rc.left, rgn_hei = rc.bottom - rc.top;
    ApplyPen(hdc, hdc->pen, rgn_wid, rgn_hei, rc.left, rc.top);
    ApplyRop2(ctx, hdc->rop2);
    CGContextStrokePath(ctx);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL WINAPI DrawFocusRect(HDC hdc,       // handle to device context
                          CONST RECT *rc // logical coordinates
)
{
    HBRUSH hOldBrush;
    HPEN hOldPen, hNewPen;
    INT oldDrawMode, oldBkMode;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextSetShouldAntialias(ctx, false);
    hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    hNewPen = CreatePen(PS_DOT, 1, 0);
    hOldPen = SelectObject(hdc, hNewPen);
    oldDrawMode = SetROP2(hdc, R2_NOT);
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SetROP2(hdc, oldDrawMode);
    SelectObject(hdc, hOldPen);
    DeleteObject(hNewPen);
    SelectObject(hdc, hOldBrush);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL PaintRgn(HDC hdc, HRGN hrgn)
{
    return FillRgn(hdc, hrgn, hdc->brush);
}


static void drawImage(CGContextRef context, CGRect targetRect, CGImageRef cgImage, CGRect srcRect, BOOL bXFlip=FALSE, BOOL bYFlip=FALSE)
{
    //as scanlines of cgImage are bottomup arranged, yflip should true as default. 
    CGFloat imageWidth = CGImageGetWidth(cgImage);
    CGFloat imageHeight = CGImageGetHeight(cgImage);
    
    // 计算缩放：目标大小 / 源大小
    float scaleX = targetRect.size.width / srcRect.size.width;
    float scaleY = targetRect.size.height / srcRect.size.height;
    // 验证源矩形
    CGRect validSrc = CGRectIntersection(srcRect, CGRectMake(0, 0, imageWidth, imageHeight));
    if (CGRectIsEmpty(validSrc)) return;
    //calc draw the whole image would occupied target rect.
    //step 1, offset target origin.
    float x_ori = targetRect.origin.x - srcRect.origin.x * scaleX;
    float y_ori = targetRect.origin.y - srcRect.origin.y * scaleY;
    //step 2, calc target size.
    float cx_target = targetRect.size.width * imageWidth / validSrc.size.width;
    float cy_target = targetRect.size.height * imageHeight / validSrc.size.height;

    float tx = x_ori + (bXFlip?cx_target:0);
    float ty = y_ori + (bYFlip?0:cy_target);

    CGContextTranslateCTM(context, tx,ty);
    CGContextScaleCTM(context, bXFlip?-1.0f:1.0f, bYFlip?1.0f:-1.0f);
    CGContextDrawImage(context, 
                      CGRectMake(0, 0, cx_target, cy_target), 
                      cgImage);    
}

BOOL AlphaBlend(HDC hdc, int x, int y, int wDst, int hDst, HDC hdcSrc, int x1, int y1, int wSrc, int hSrc, BLENDFUNCTION ftn)
{
    assert(hdc && hdcSrc);
    if(!hdc->cgCtx || !hdcSrc->cgCtx) return 0;
    CGContextRef ctx = hdc->cgCtx;
    CGImageRef srcImg = CreateCGImageFromBitmap(hdcSrc->bmp);
    if(!srcImg) return 0;
    CGContextSaveGState(ctx);
    // Convert x1/y1 from source-DC logical coords to image coords
    // (logical + worldMtx translation + viewport origin).
    x1 += hdcSrc->worldMtx->tx + hdcSrc->ptOrigin.x;
    y1 += hdcSrc->worldMtx->ty + hdcSrc->ptOrigin.y;
    CGRect clipRect = CGRectMake(x, y, wDst, hDst);
    CGContextClipToRect(ctx, clipRect);
    CGRect dstRect = CGRectMake(x, y, wDst, hDst);
    CGRect srcRect = CGRectMake(x1, y1, wSrc, hSrc);
    if (ftn.SourceConstantAlpha != 255)
        CGContextSetAlpha(ctx, ftn.SourceConstantAlpha * 1.0 / 255.0);
    drawImage(ctx, dstRect, srcImg,srcRect);
    CGContextRestoreGState(ctx);
    CGImageRelease(srcImg);
    return 0;
}

static BOOL AlphaBlendEx(HDC hdc, int x, int y, int wDst, int hDst, CGImageRef src, int x1, int y1, int wSrc, int hSrc, BLENDFUNCTION ftn, int filterLevel)
{
    assert(hdc);
    if(!hdc->cgCtx) return 0;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGRect dstRect = CGRectMake(x, y, wDst, hDst);
    CGRect srcRect = CGRectMake(x1,y1, wSrc,hSrc);
    CGContextClipToRect(ctx, dstRect);
    if (ftn.SourceConstantAlpha != 255)
        CGContextSetAlpha(ctx, ftn.SourceConstantAlpha * 1.0 / 255.0);
    CGInterpolationQuality quality;
    switch (filterLevel)
    {
    case FILTER_FAST:
        quality = kCGInterpolationNone;
        break;
    case FILTER_MIDIUM:
        quality = kCGInterpolationMedium;
        break;
    case FILTER_BEST:
        quality = kCGInterpolationHigh;
        break;
    default:
        quality = kCGInterpolationDefault;
        break;
    }
    CGContextSetInterpolationQuality(ctx, quality);
    drawImage(ctx, dstRect, src, srcRect);
    CGContextRestoreGState(ctx);
    return 0;
}

BOOL DrawBitmapEx(HDC hdc, LPCRECT pRcDest, HBITMAP bmp, LPCRECT pRcSrc, UINT expendMode, BYTE byAlpha /*=0xFF*/)
{
    if (!bmp)
        return FALSE;
    CGImageRef src = CreateCGImageFromBitmap(bmp);
    if(!src) return FALSE;
    int filterLevel = HIWORD(expendMode);
    expendMode = LOWORD(expendMode);

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, byAlpha, AC_SRC_ALPHA };
    if (expendMode == EXPEND_MODE_NONE)
    {
        ::AlphaBlendEx(hdc, pRcDest->left, pRcDest->top, pRcSrc->right - pRcSrc->left, pRcSrc->bottom - pRcSrc->top, src, pRcSrc->left, pRcSrc->top, pRcSrc->right - pRcSrc->left, pRcSrc->bottom - pRcSrc->top, bf, filterLevel);
    }
    else if (expendMode == EXPEND_MODE_STRETCH)
    {
        ::AlphaBlendEx(hdc, pRcDest->left, pRcDest->top, pRcDest->right - pRcDest->left, pRcDest->bottom - pRcDest->top, src, pRcSrc->left, pRcSrc->top, pRcSrc->right - pRcSrc->left, pRcSrc->bottom - pRcSrc->top, bf, filterLevel);
    }
    else // if(expendMode == EXPEND_MODE_TILE)
    {
        ::SaveDC(hdc);
        ::IntersectClipRect(hdc, pRcDest->left, pRcDest->top, pRcDest->right, pRcDest->bottom);
        int nWid = pRcSrc->right - pRcSrc->left;
        int nHei = pRcSrc->bottom - pRcSrc->top;
        for (int y = pRcDest->top; y < pRcDest->bottom; y += nHei)
        {
            for (int x = pRcDest->left; x < pRcDest->right; x += nWid)
            {
                ::AlphaBlendEx(hdc, x, y, nWid, nHei, src, pRcSrc->left, pRcSrc->top, nWid, nHei, bf, filterLevel);
            }
        }
        ::RestoreDC(hdc, -1);
    }
    CGImageRelease(src);
    return TRUE;
}

static BOOL IsRectNormal(const RECT *prc)
{
    return prc->left < prc->right && prc->top < prc->bottom;
}

BOOL DrawBitmap9Patch(HDC hdc, LPCRECT pRcDest, HBITMAP hBmp, LPCRECT pRcSrc, LPCRECT pRcSourMargin, UINT expendMode, BYTE byAlpha /*=0xFF*/)
{
    LONG xDest[4] = { pRcDest->left, pRcDest->left + pRcSourMargin->left, pRcDest->right - pRcSourMargin->right, pRcDest->right };
    LONG xSrc[4] = { pRcSrc->left, pRcSrc->left + pRcSourMargin->left, pRcSrc->right - pRcSourMargin->right, pRcSrc->right };
    LONG yDest[4] = { pRcDest->top, pRcDest->top + pRcSourMargin->top, pRcDest->bottom - pRcSourMargin->bottom, pRcDest->bottom };
    LONG ySrc[4] = { pRcSrc->top, pRcSrc->top + pRcSourMargin->top, pRcSrc->bottom - pRcSourMargin->bottom, pRcSrc->bottom };

    //首先保证九宫分割正常
    if (!(xSrc[0] <= xSrc[1] && xSrc[1] <= xSrc[2] && xSrc[2] <= xSrc[3]))
        return FALSE;
    if (!(ySrc[0] <= ySrc[1] && ySrc[1] <= ySrc[2] && ySrc[2] <= ySrc[3]))
        return FALSE;

    //调整目标位置
    int nDestWid = pRcDest->right - pRcDest->left;
    int nDestHei = pRcDest->bottom - pRcDest->top;

    if ((pRcSourMargin->left + pRcSourMargin->right) > nDestWid)
    { //边缘宽度大于目标宽度的处理
        if (pRcSourMargin->left >= nDestWid)
        { //只绘制左边部分
            xSrc[1] = xSrc[2] = xSrc[3] = xSrc[0] + nDestWid;
            xDest[1] = xDest[2] = xDest[3] = xDest[0] + nDestWid;
        }
        else if (pRcSourMargin->right >= nDestWid)
        { //只绘制右边部分
            xSrc[0] = xSrc[1] = xSrc[2] = xSrc[3] - nDestWid;
            xDest[0] = xDest[1] = xDest[2] = xDest[3] - nDestWid;
        }
        else
        { //先绘制左边部分，剩余的用右边填充
            int nRemain = xDest[3] - xDest[1];
            xSrc[2] = xSrc[3] - nRemain;
            xDest[2] = xDest[3] - nRemain;
        }
    }

    if (pRcSourMargin->top + pRcSourMargin->bottom > nDestHei)
    {
        if (pRcSourMargin->top >= nDestHei)
        { //只绘制上边部分
            ySrc[1] = ySrc[2] = ySrc[3] = ySrc[0] + nDestHei;
            yDest[1] = yDest[2] = yDest[3] = yDest[0] + nDestHei;
        }
        else if (pRcSourMargin->bottom >= nDestHei)
        { //只绘制下边部分
            ySrc[0] = ySrc[1] = ySrc[2] = ySrc[3] - nDestHei;
            yDest[0] = yDest[1] = yDest[2] = yDest[3] - nDestHei;
        }
        else
        { //先绘制左边部分，剩余的用右边填充
            int nRemain = yDest[3] - yDest[1];
            ySrc[2] = ySrc[3] - nRemain;
            yDest[2] = yDest[3] - nRemain;
        }
    }

    //定义绘制模式
    UINT mode[3][3] = { { EXPEND_MODE_NONE, expendMode, EXPEND_MODE_NONE }, { expendMode, expendMode, expendMode }, { EXPEND_MODE_NONE, expendMode, EXPEND_MODE_NONE } };
    Antialias oldAntialias = GetAntialiasMode(hdc);
    SetAntialiasMode(hdc, ANTIALIAS_NONE); //关闭抗锯齿，否则图片拼接边缘可能出现缝隙。
    for (int y = 0; y < 3; y++)
    {
        if (ySrc[y] == ySrc[y + 1])
            continue;
        for (int x = 0; x < 3; x++)
        {
            if (xSrc[x] == xSrc[x + 1])
                continue;
            RECT rcSrc = { xSrc[x], ySrc[y], xSrc[x + 1], ySrc[y + 1] };
            RECT rcDest = { xDest[x], yDest[y], xDest[x + 1], yDest[y + 1] };

            if (!IsRectNormal(&rcSrc) || !IsRectNormal(&rcDest))
                continue;
            DrawBitmapEx(hdc, &rcDest, hBmp, &rcSrc, mode[y][x], byAlpha);
        }
    }
    SetAntialiasMode(hdc, oldAntialias);

    return TRUE;
}

BOOL BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    assert(hdc && hdcSrc);
    if(!hdc->cgCtx || !hdcSrc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGImageRef src = CreateCGImageFromBitmap(hdcSrc->bmp);
    if(!src) return FALSE;

    // Convert x1/y1 from source-DC user (logical) coordinates to image
    // coordinates.  totalSrc maps logical → image (logical + ptOrigin + worldMtx),
    // so apply it directly (not its inverse).
    {
        CGAffineTransform totalSrc = *hdcSrc->worldMtx;
        totalSrc.tx += hdcSrc->ptOrigin.x;
        totalSrc.ty += hdcSrc->ptOrigin.y;
        CGPoint pt = CGPointMake((CGFloat)x1, (CGFloat)y1);
        pt = CGPointApplyAffineTransform(pt, totalSrc);
        x1 = (int)round(pt.x);
        y1 = (int)round(pt.y);
    }

    CGContextSaveGState(ctx);
    CGRect srcRect = CGRectMake(x1,y1, cx,cy);
    CGRect dstRect = CGRectMake(x, y, cx, cy);
    CGContextClipToRect(ctx, dstRect);
    switch (rop)
    {
    case SRCCOPY:
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        break;
    case SRCINVERT:
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        break;
    case SRCPAINT:
        CGContextSetBlendMode(ctx, kCGBlendModeNormal);
        break;
    case SRCAND:
        CGContextSetBlendMode(ctx, kCGBlendModeDestinationIn);
        break;
    case DSTINVERT:
        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        CGContextFillRect(ctx, dstRect);
        goto bl_done;
    }
    drawImage(ctx, dstRect, src,srcRect);
bl_done:
    CGContextRestoreGState(ctx);
    CGImageRelease(src);
    return TRUE;
}

BOOL StretchBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, int cx1, int cy1, DWORD rop)
{
    assert(hdc && hdcSrc);
    if(!hdc->cgCtx || !hdcSrc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGImageRef src = CreateCGImageFromBitmap(hdcSrc->bmp);
    if(!src) return FALSE;

    // 1. 应用源 DC transform（与 BitBlt 一致，对齐 cairo 的 mtxSrc.x0/y0 应用）
    CGAffineTransform totalSrc = *hdcSrc->worldMtx;
    totalSrc.tx += hdcSrc->ptOrigin.x;
    totalSrc.ty += hdcSrc->ptOrigin.y;
    CGPoint pt = CGPointMake((CGFloat)x1, (CGFloat)y1);
    pt = CGPointApplyAffineTransform(pt, totalSrc);
    x1 = (int)round(pt.x);
    y1 = (int)round(pt.y);

    CGContextSaveGState(ctx);

    // 2. clip 到目标矩形（标准化负 w/h，对齐 cairo_rectangle+cairo_clip 行为）
    CGRect dstRect = CGRectStandardize(CGRectMake(x, y, cx, cy));
    CGContextClipToRect(ctx, dstRect);
    // 5. 设置混合模式（对齐 cairo 的 operator 设置）
    switch (rop)
    {
    case SRCCOPY:
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        break;
    case SRCINVERT:
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        break;
    case SRCPAINT:
        CGContextSetBlendMode(ctx, kCGBlendModeNormal);
        break;
    case SRCAND:
        CGContextSetBlendMode(ctx, kCGBlendModeDestinationIn);
        break;
    case DSTINVERT:
        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        CGContextFillRect(ctx, dstRect);
        goto sb_done;
    }
    {
        CGRect srcRect = CGRectStandardize(CGRectMake(x1, y1, cx1, cy1));
        BOOL bXFlip = (cx < 0) ^ (cx1<0);
        BOOL bYFlip = (cy < 0) ^ (cy1<0);
        drawImage(ctx, dstRect, src, srcRect,bXFlip,bYFlip);
    }

sb_done:
    CGContextRestoreGState(ctx);
    CGImageRelease(src);
    return TRUE;
}

INT StretchDIBits(HDC hdc, INT x_dst, INT y_dst, INT width_dst, INT height_dst, INT x_src, INT y_src, INT width_src, INT height_src, const void *bits, const BITMAPINFO *bmi, UINT coloruse, DWORD rop)
{
    HBITMAP bmp = CreateDIBitmap(hdc, &bmi->bmiHeader, 1, bits, bmi, coloruse);
    if (!bmp)
    {
        return 0;
    }
    HDC memdc = CreateCompatibleDC(hdc);
    SelectObject(memdc, bmp);
    // Pass-through to StretchBlt with the caller's sign semantics, matching
    // cairo StretchDIBits.  Flip/shift is handled inside StretchBlt itself
    // by comparing dst/src dimension signs.
    BOOL ret = StretchBlt(hdc, x_dst, y_dst, width_dst, height_dst, memdc, x_src, y_src, width_src, height_src, rop);
    DeleteDC(memdc);
    DeleteObject(bmp);
    return height_src;
}

BOOL TransparentBlt(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, UINT crTransparent)
{
    if(!hdcDest->cgCtx || !hdcSrc->cgCtx) return FALSE;
    CGContextRef ctx = hdcDest->cgCtx;
    CGImageRef srcImg = CreateCGImageFromBitmap(hdcSrc->bmp);
    if(!srcImg) return FALSE;
    CGContextSaveGState(ctx);
    CGRect srcRect = CGRectMake(xoriginSrc,yoriginSrc, wSrc,hSrc);
    CGRect dstRect = CGRectMake(xoriginDest, yoriginDest, wDest, hDest);
    CGContextClipToRect(ctx, dstRect);
    drawImage(ctx, dstRect, srcImg,srcRect);
    CGContextRestoreGState(ctx);
    CGImageRelease(srcImg);
    return TRUE;
}

void SetStretchBltMode(HDC hdc, int mode)
{
    // todo:hjx
}

BOOL PatBlt(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ int w, _In_ int h, _In_ DWORD rop)
{
    BOOL ret = FALSE;
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGRect rc = CGRectMake(x, y, w, h);
    switch (rop)
    {
    case PATCOPY:
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        if (ApplyBrush(hdc, hdc->brush, w, h, x, y))
        {
            CGContextFillRect(ctx, rc);
            ret = TRUE;
        }
        break;
    case PATINVERT:
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        if (ApplyBrush(hdc, hdc->brush, w, h, x, y))
        {
            CGContextFillRect(ctx, rc);
            ret = TRUE;
        }
        break;
    case DSTINVERT:
        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextSetBlendMode(ctx, kCGBlendModeXOR);
        CGContextFillRect(ctx, rc);
        ret = TRUE;
        break;
    case BLACKNESS:
        CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        CGContextFillRect(ctx, rc);
        ret = TRUE;
        break;
    case WHITENESS:
        CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
        CGContextSetBlendMode(ctx, kCGBlendModeCopy);
        CGContextFillRect(ctx, rc);
        ret = TRUE;
        break;
    }
    CGContextRestoreGState(ctx);
    return ret;
}

static bool IsAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsNumber(char c)
{
    return c >= '0' && c <= '9';
}

static bool IsHex(char c)
{
    return IsNumber(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsDigit(char c)
{
    return IsNumber(c) || c == '.' || c == ',';
}

static LPCSTR SkipWord(LPCSTR p)
{
    if (IsAlpha(*p))
    {
        while (*p)
        {
            p = CharNextA(p);
            if (!IsAlpha(*p))
                break;
        }
    }
    return p;
}

static LPCSTR SkipNumber(LPCSTR p)
{
    if (*p && *(p + 1) && (strncmp(p, "0x", 2) == 0 || strncmp(p, "0X", 2) == 0))
    { // test for hex number
        p = p + 2;
        while (*p)
        {
            if (!IsHex(*p))
                break;
            p++;
        }
        return p;
    }
    else
    {
        while (*p)
        {
            if (!IsDigit(*p))
                break;
            p++;
        }
        return p;
    }
}

static LPCSTR WordNext(LPCSTR pszBuf, bool bWordbreak)
{
    assert(pszBuf);
    LPCSTR p = CharNextA(pszBuf);
    if (!bWordbreak)
        return p;
    LPCSTR pWord = SkipWord(pszBuf);
    if (pWord > pszBuf)
        return pWord;
    LPCSTR pNum = SkipNumber(pszBuf);
    if (pNum > pszBuf)
        return pNum;
    return p;
}

static LPCSTR nextChar(LPCSTR p)
{
    int len = mbtowc(nullptr, p, MB_CUR_MAX);
    assert(len > 0);
    return p + len;
}

static SIZE OnMeasureText(HDC hdc, LPCSTR pszBuf, int cchText)
{
    int i = 0;
    char word[6];
    LPCSTR p = pszBuf;
    LPCSTR pEnd = p + cchText;
    SIZE ret = { 0, 0 };
    CGFloat ascent = 0, descent = 0;
    bool gotMetrics = false;
    while (p < pEnd)
    {
        LPCSTR next = nextChar(p);
        assert(next - p <= 5);
        memcpy(word, p, (next - p));
        word[next - p] = 0;
        CGFloat chWid = 0, chAscent = 0, chDescent = 0;
        CTLineRef line = CreateCTLineWithDC(hdc, word, (int)(next - p), &chAscent, &chDescent, &chWid);
        if (line)
        {
            if (!gotMetrics)
            {
                ascent = chAscent;
                descent = chDescent;
                gotMetrics = true;
            }
            ret.cx += (LONG)chWid;
            CFRelease(line);
        }
        p = next;
    }
    ret.cy = (LONG)(ascent + descent);
    if (!gotMetrics)
        ret.cy = 16;
    return ret;
}

static void DrawTextDecLines(HDC hdc, double ascent, double descent, LPCSTR str, int len, int x, int y, double x_bearing, double x_advance)
{
    const LOGFONT *lf = (const LOGFONT *)GetGdiObjPtr(hdc->hfont);
    assert(lf);
    if (lf->lfStrikeOut || lf->lfUnderline)
    {
        if(!hdc->cgCtx) return;
        CGContextRef ctx = hdc->cgCtx;
        HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(hdc));
        ApplyPen(hdc, pen, 0, 0, 0, 0);
        ApplyRop2(ctx, hdc->rop2);
        double penWid = 1;
        COLORREF penCol = GetTextColor(hdc);
        CGContextSetRGBStrokeColor(ctx, GetRValue(penCol)/255.0, GetGValue(penCol)/255.0, GetBValue(penCol)/255.0, GetAValue(penCol)/255.0);
        CGContextSetLineWidth(ctx, 1.0);
        CGContextBeginPath(ctx);
        if (lf->lfStrikeOut)
        {
            double y_line = y + (ascent + descent) / 2.0;
            CGContextMoveToPoint(ctx, x + x_bearing, y_line);
            CGContextAddLineToPoint(ctx, x + x_advance, y_line);
        }
        if (lf->lfUnderline)
        {
            double y_line = y + ascent + descent + 1;
            CGContextMoveToPoint(ctx, x + x_bearing, y_line);
            CGContextAddLineToPoint(ctx, x + x_advance, y_line);
        }
        CGContextStrokePath(ctx);
        DeleteObject(pen);
    }
}

static void DrawSingleLine(HDC hdc, LPCSTR pszBuf, int iBegin, int cchText, LPRECT pRect, UINT uFormat)
{
    if(!hdc->cgCtx) return;
    CGContextRef ctx = hdc->cgCtx;
    CGFloat ascent = 0, descent = 0, x_advance = 0;
    CTLineRef line = CreateCTLineWithDC(hdc, pszBuf + iBegin, cchText, &ascent, &descent, &x_advance);
    double x_bearing = 0;
    double font_hei = ascent + descent;
    if (uFormat & DT_CALCRECT)
    {
        pRect->right = pRect->left + x_advance;
        pRect->bottom = pRect->top + font_hei;
    }
    else
    {
        int drawX = pRect->left;
        int drawY = pRect->top + ascent;
        switch (uFormat & (DT_LEFT | DT_CENTER | DT_RIGHT))
        {
        case DT_LEFT:
            drawX = pRect->left;
            break;
        case DT_CENTER:
            drawX = pRect->left + (pRect->right - pRect->left - (int)x_advance) / 2;
            break;
        case DT_RIGHT:
            drawX = pRect->right - (int)x_advance;
            break;
        }
        CGContextSaveGState(ctx);
        CGContextSetTextMatrix(ctx, CGAffineTransformMake(1, 0, 0, -1, drawX, drawY));
        if (line)
            CTLineDraw(line, ctx);
        CGContextRestoreGState(ctx);
        DrawTextDecLines(hdc, ascent, descent, pszBuf + iBegin, cchText, drawX, pRect->top, x_bearing, x_advance);
    }
    if (line)
        CFRelease(line);
}

#define DT_ELLIPSIS (DT_PATH_ELLIPSIS | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS)
#define CH_ELLIPSIS "..."

// 参照 cairo.gdi 的 drawLineEndWithEllipsis：当文本宽度超过 pRect 宽度时，
// 在末尾以省略号 "..." 截断显示；未超长时退化为普通单行绘制。
// 返回实际绘制的文本宽度（含省略号）。
static int DrawSingleLineWithEllipsis(HDC hdc, LPCSTR pszBuf, int iBegin, int cchText, LPRECT pRect, UINT uFormat)
{
    if (!hdc->cgCtx)
        return 0;
    int maxWidth = pRect->right - pRect->left;
    // 测量整段文本宽度
    CGFloat ascent = 0, descent = 0, x_advance = 0;
    CTLineRef line = CreateCTLineWithDC(hdc, pszBuf + iBegin, cchText, &ascent, &descent, &x_advance);
    if (line)
        CFRelease(line);
    if (x_advance <= maxWidth)
    {
        // 未超长：按普通单行绘制（内部处理 DT_CALCRECT/对齐/下划线）
        DrawSingleLine(hdc, pszBuf, iBegin, cchText, pRect, uFormat);
        return (int)x_advance;
    }
    // 超长：先扣除省略号宽度，再按字符累加确定截断点
    CGFloat ellA = 0, ellD = 0, ellWid = 0;
    CTLineRef ellLine = CreateCTLineWithDC(hdc, CH_ELLIPSIS, 3, &ellA, &ellD, &ellWid);
    if (ellLine)
        CFRelease(ellLine);
    int limit = maxWidth - (int)ellWid;
    if (limit < 0)
        limit = 0;
    int i = 0;
    int fWid = 0;
    LPCSTR p = pszBuf + iBegin;
    while (i < cchText)
    {
        LPCSTR next = nextChar(p);
        int chLen = (int)(next - p);
        if (chLen <= 0)
            break;
        CGFloat chA = 0, chD = 0, chWid = 0;
        CTLineRef chLine = CreateCTLineWithDC(hdc, p, chLen, &chA, &chD, &chWid);
        if (chLine)
            CFRelease(chLine);
        if (fWid + (int)chWid > limit)
            break;
        fWid += (int)chWid;
        i += chLen;
        p = next;
    }
    // 拼接截断文本 + 省略号后按普通单行绘制
    int newLen = i + 3;
    char *pbuf = new char[newLen];
    memcpy(pbuf, pszBuf + iBegin, i);
    memcpy(pbuf + i, CH_ELLIPSIS, 3);
    DrawSingleLine(hdc, pbuf, 0, newLen, pRect, uFormat);
    delete[] pbuf;
    return fWid + (int)ellWid;
}

#define kDrawText_LineInterval 0

void DrawMultiLine(HDC hdc, LPCSTR pszBuf, int cchText, LPRECT pRect, UINT uFormat)
{
    int i = 0, nLine = 1;
    if (cchText == -1)
        cchText = (int)strlen(pszBuf);
    LPCSTR p1 = pszBuf;
    POINT pt = { pRect->left, pRect->top };
    SIZE szWord = OnMeasureText(hdc, "A", 1);
    int nLineHei = szWord.cy;
    int nRight = pRect->right;
    int nLineWid = pRect->right - pRect->left;
    pRect->right = pRect->left;

    LPCSTR pLineHead = p1, pLineTail = p1;

    LPCSTR pPrev = NULL;
    while (i < cchText)
    {
        LPCSTR p2 = WordNext(p1, uFormat & DT_WORDBREAK);
        assert(p2 > p1);
        if ((*p1 == _T('\n') && p2))
        {
            if (pLineTail > pLineHead && !(uFormat & DT_CALCRECT))
            {
                RECT rcText = { pRect->left, pt.y, nRight, pt.y + nLineHei };
                DrawSingleLine(hdc, pszBuf, (int)(pLineHead - pszBuf), (int)(pLineTail - pLineHead), &rcText, uFormat);
            }
            pt.y += nLineHei + kDrawText_LineInterval;
            pt.x = pRect->left;
            nLine++;
            i += (int)(p2 - p1);
            p1 = p2;
            pLineHead = p2;
            continue;
        }
        if (uFormat & DT_WORDBREAK && *p1 == 0x20 && pt.x == pRect->left && (!pPrev || *pPrev != 0x20))
        { // skip the first space for a new line.
            i += (int)(p2 - p1);
            pPrev = p1;
            p1 = p2;
            pLineTail = pLineHead = p2;
            continue;
        }
        szWord = OnMeasureText(hdc, p1, (int)(p2 - p1));
        if (pt.x + szWord.cx > nRight)
        { //检测到一行超过边界时还要保证当前行不为空

            if (pLineTail > pLineHead)
            {
                BOOL bVertOverflow = (pt.y + nLineHei + kDrawText_LineInterval > pRect->bottom);
                if (!(uFormat & DT_CALCRECT))
                {
                    RECT rcText = { pRect->left, pt.y, nRight, pt.y + nLineHei };
                    if (bVertOverflow && (uFormat & DT_ELLIPSIS))
                        // 最后一可见行：将当前行及超长词以省略号截断显示
                        DrawSingleLineWithEllipsis(hdc, pszBuf, (int)(pLineHead - pszBuf), (int)(p2 - pLineHead), &rcText, uFormat);
                    else
                        DrawSingleLine(hdc, pszBuf, (int)(pLineHead - pszBuf), (int)(pLineTail - pLineHead), &rcText, uFormat);
                }
                // 显示多行文本时，如果下一行文字的高度超过了文本框，则不再输出下一行文字内容。
                if (bVertOverflow)
                { //将绘制限制在有效区。
                    pLineHead = pLineTail;
                    break;
                }

                pLineHead = p1;

                pt.y += nLineHei + kDrawText_LineInterval;
                pt.x = pRect->left;
                nLine++;

                continue;
            }
            else
            { // word is too long to draw in a single line
                LPCSTR p3 = p1;
                SIZE szChar;
                szWord.cx = 0;
                while (p3 < p2)
                {
                    LPCSTR p4 = CharNextA(p3);
                    szChar = OnMeasureText(hdc, p3, (int)(p4 - p3));
                    if (szWord.cx + szChar.cx > nLineWid)
                    {
                        if (p3 == p1)
                        { // a line will contain at least one char.
                            p2 = p4;
                            szWord.cx = szChar.cx;
                        }
                        else
                        {
                            p2 = p3;
                        }
                        break;
                    }
                    szWord.cx += szChar.cx;
                    p3 = p4;
                }
            }
        }
        pt.x += szWord.cx;
        if (pt.x > pRect->right && uFormat & DT_CALCRECT)
            pRect->right = pt.x;
        i += (int)(p2 - p1);
        pPrev = p1;
        pLineTail = p1 = p2;
    }

    if (uFormat & DT_CALCRECT)
    {
        if (pRect->bottom > pt.y + nLineHei)
            pRect->bottom = pt.y + nLineHei;
    }
    else if (pLineTail > pLineHead)
    {
        RECT rcText = { pRect->left, pt.y, nRight, pt.y + nLineHei };
        DrawSingleLine(hdc, pszBuf, (int)(pLineHead - pszBuf), (int)(pLineTail - pLineHead), &rcText, uFormat);
    }
}

int DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
    if (cchText < 0)
        cchText = wcslen(lpchText);
    int len = WideCharToMultiByte(CP_UTF8, 0, lpchText, cchText, nullptr, 0, nullptr, nullptr);
    char *buf = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, lpchText, cchText, buf, len, nullptr, nullptr);
    int nRet = DrawTextA(hdc, buf, len, lprc, format);
    delete[] buf;
    return nRet;
}

int DrawTextA(HDC hdc, LPCSTR pszBuf, int cchText, LPRECT pRect, UINT uFormat)
{
    if (cchText < 0)
        cchText = strlen(pszBuf);
    assert(pRect);
    RECT rc = *pRect;
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    BOOL bCalc = (uFormat & DT_CALCRECT)==DT_CALCRECT;
    if (hdc->bkMode == OPAQUE && !bCalc)
    {
        CGContextSaveGState(ctx);
        COLORREF crBk = hdc->crBk;
        CGContextSetRGBFillColor(ctx, GetRValue(crBk)/255.0, GetGValue(crBk)/255.0, GetBValue(crBk)/255.0, GetAValue(crBk)/255.0);
        CGContextFillRect(ctx, CGRectMake(pRect->left, pRect->top, pRect->right - pRect->left, pRect->bottom - pRect->top));
        CGContextRestoreGState(ctx);
    }
    CGContextSaveGState(ctx);
    // 裁剪到 pRect（除非 DT_NOCLIP）；参照 cairo.gdi 在绘制前 cairo_clip
    if (!(uFormat & DT_NOCLIP) && !bCalc)
    {
        CGRect clipRect = CGRectMake((CGFloat)pRect->left, (CGFloat)pRect->top,
                                    (CGFloat)(pRect->right - pRect->left),
                                    (CGFloat)(pRect->bottom - pRect->top));
        CGContextClipToRect(ctx, clipRect);
    }
    ApplyFont(hdc);
    const LOGFONT *lf = (const LOGFONT *)GetGdiObjPtr(hdc->hfont);
    assert(lf);
    COLORREF crTxt = hdc->crText;
    CGContextSetRGBFillColor(ctx, GetRValue(crTxt)/255.0, GetGValue(crTxt)/255.0, GetBValue(crTxt)/255.0, GetAValue(crTxt)/255.0);
    BOOL bEllipsis = (uFormat & DT_ELLIPSIS) != 0;
    if (uFormat & DT_SINGLELINE)
    {
        if (bCalc)
        {
            // 测量：宽度=文本宽，高度=行高，不含垂直对齐偏移（与 cairo 一致）
            if (bEllipsis)
                DrawSingleLineWithEllipsis(hdc, pszBuf, 0, cchText, pRect, uFormat);
            else
                DrawSingleLine(hdc, pszBuf, 0, cchText, pRect, uFormat);
        }
        else
        {
            SIZE szFont = OnMeasureText(hdc, "A", 1);
            int lineSpan = szFont.cy; // = ascent + descent
            int rectHeight = pRect->bottom - pRect->top;
            int offset = 0;
            if (uFormat & DT_VCENTER)
                offset = (rectHeight - lineSpan) / 2;
            else if (uFormat & DT_BOTTOM)
                offset = rectHeight - lineSpan;
            RECT rcLine = { pRect->left, pRect->top + offset, pRect->right, pRect->bottom };
            if (bEllipsis)
                DrawSingleLineWithEllipsis(hdc, pszBuf, 0, cchText, &rcLine, uFormat);
            else
                DrawSingleLine(hdc, pszBuf, 0, cchText, &rcLine, uFormat);
        }
    }
    else
    {
        DrawMultiLine(hdc, pszBuf, cchText, pRect, uFormat);
    }
    CGContextRestoreGState(ctx);
    return TRUE;
}

COLORREF GetBkColor(HDC hdc)
{
    return hdc->crBk;
}

COLORREF SetBkColor(HDC hdc, COLORREF cr)
{
    COLORREF ret = hdc->crBk;
    hdc->crBk = cr;
    return ret;
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
    std::string str;
    tostring(lpString, c, str);
    return TextOutA(hdc, x, y, str.c_str(), str.length());
}

// Add glyph outlines to the CGContext path (not recordedPath) so that
// EndPath's CGContextCopyPath can capture them, mirroring cairo_glyph_path.
// Also moves the current point to the end of the text (x + x_advance, y),
// mirroring cairo_text_path2's cairo_move_to at end.
static void AddGlyphsToCtxPath(HDC hdc, CTLineRef line, double x, double y, double x_advance)
{
    if (!hdc || !hdc->cgCtx || !line) return;

    CGContextRef ctx = hdc->cgCtx;
    CTFontRef ctFont = CreateCTFontFromDC(hdc);
    if (!ctFont) return;

    CGMutablePathRef workPath = CGPathCreateMutable();
    if (!workPath) {
        CFRelease(ctFont);
        return;
    }

    CFArrayRef runs = CTLineGetGlyphRuns(line);
    if (runs) {
        CFIndex nRuns = CFArrayGetCount(runs);
        for (CFIndex r = 0; r < nRuns; r++) {
            CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
            if (!run) continue;
            CFIndex nGlyphs = CTRunGetGlyphCount(run);
            if (nGlyphs <= 0) continue;

            CFAllocatorRef alloc = NULL;
            CGGlyph *glyphs = (CGGlyph *)CFAllocatorAllocate(alloc, sizeof(CGGlyph)*nGlyphs, 0);
            CGPoint *positions = (CGPoint *)CFAllocatorAllocate(alloc, sizeof(CGPoint)*nGlyphs, 0);
            if (glyphs && positions) {
                CTRunGetGlyphs(run, CFRangeMake(0, nGlyphs), glyphs);
                CTRunGetPositions(run, CFRangeMake(0, nGlyphs), positions);

                for (CFIndex g = 0; g < nGlyphs; g++) {
                    // CoreText glyphs are in y-up coordinates. Map to our
                    // flipped-y user space: X' = x + pos.x + gx,
                    // Y' = y - pos.y - gy.
                    CGAffineTransform t = CGAffineTransformMake(1, 0,
                                                                 0, -1,
                                                                 x + positions[g].x,
                                                                 y - positions[g].y);
                    CGPathRef glyphPath = CTFontCreatePathForGlyph(ctFont, glyphs[g], &t);
                    if (glyphPath) {
                        CGPathAddPath(workPath, NULL, glyphPath);
                        CGPathRelease(glyphPath);
                    }
                }
            }
            if (glyphs) CFAllocatorDeallocate(alloc, glyphs);
            if (positions) CFAllocatorDeallocate(alloc, positions);
        }
    }

    // Add accumulated glyph outlines to the CGContext path.
    CGContextAddPath(ctx, workPath);
    CGPathRelease(workPath);
    CFRelease(ctFont);

    // Move current point to the end of the text so chained text paths and
    // GetCurrentPositionEx behave like cairo_text_path2.
    CGContextMoveToPoint(ctx, x + x_advance, y);
}

BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
    if (c < 0)
        c = strlen(lpString);

    // Return early if no text to draw
    if (c == 0)
        return TRUE;

    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGFloat ascent = 0, descent = 0, x_advance = 0;
    CTLineRef line = CreateCTLineWithDC(hdc, lpString, c, &ascent, &descent, &x_advance);
    switch (hdc->textAlign & (TA_LEFT | TA_RIGHT | TA_CENTER))
    {
    case TA_RIGHT:
        x -= x_advance;
        break;
    case TA_CENTER:
        x -= x_advance / 2;
        break;
    }
    switch (hdc->textAlign & (TA_BASELINE | TA_BOTTOM | TA_TOP))
    {
    case TA_TOP:
        y += ascent;
        break;
    case TA_BASELINE:
        break;
    case TA_BOTTOM:
        y -= descent;
        break;
    }

    if (hdc->pathRecording)
    {
        // Path mode: add glyph outlines directly to ctx path. A surrounding
        // save/restore would discard the path (CGContextRestoreGState restores
        // the path), so operate outside save/restore — mirroring cairo's
        // cairo_text_path2 which accumulates into the cairo path.
        CGPoint oldPt;
        bool hasOldPt = !CGContextIsPathEmpty(ctx);
        if (hasOldPt)
            oldPt = CGContextGetPathCurrentPoint(ctx);
        CGContextMoveToPoint(ctx, x, y);
        if (line)
            AddGlyphsToCtxPath(hdc, line, x, y, x_advance);
        if (hdc->textAlign & TA_NOUPDATECP)
        {
            if (hasOldPt)
                CGContextMoveToPoint(ctx, oldPt.x, oldPt.y);
            else
                CGContextMoveToPoint(ctx, 0, 0);
        }
    }
    else
    {
        // Non-path mode: draw text within a save/restore so text matrix and
        // blend mode changes don't leak. CGContextRestoreGState also resets
        // the ctx path/current point to the entry state — mirroring cairo's
        // cairo_save/cairo_restore around cairo_show_text2. Do NOT call
        // CGContextMoveToPoint after restore: it would pollute the ctx path
        // and break subsequent drawing (e.g. Scintilla multi-line text).
        CGContextSaveGState(ctx);
        if (hdc->bkMode == OPAQUE)
        {
            // Inner save/restore isolates the background fill color so it
            // doesn't leak into CTLineDraw / DrawTextDecLines (matches 47bed2e).
            CGContextSaveGState(ctx);
            COLORREF crBk = GetBkColor(hdc);
            CGContextSetRGBFillColor(ctx, GetRValue(crBk)/255.0, GetGValue(crBk)/255.0, GetBValue(crBk)/255.0, GetAValue(crBk)/255.0);
            CGContextFillRect(ctx, CGRectMake(x, y - ascent, x_advance, ascent + descent));
            CGContextRestoreGState(ctx);
        }
        CGContextSetTextMatrix(ctx, CGAffineTransformMake(1, 0, 0, -1, x, y));
        if (line)
            CTLineDraw(line, ctx);
        DrawTextDecLines(hdc, ascent, descent, lpString, c, x, y - ascent, 0, x_advance);
        CGContextRestoreGState(ctx);
    }
    if (line)
        CFRelease(line);
    return TRUE;
}

static LONG TEXT_TabbedTextOut(HDC hdc, INT x, INT y, LPCSTR lpstr, INT count, INT cTabStops, const INT *lpTabPos, INT nTabOrg, BOOL fDisplayText)
{
    INT defWidth;
    SIZE extent;
    int i, j;
    int start = x;
    TEXTMETRICA tm;

    if (!lpstr || count == 0)
        return 0;
    if (count < 0)
        count = strlen(lpstr);
    if (!lpTabPos)
        cTabStops = 0;

    GetTextMetricsA(hdc, &tm);

    if (cTabStops == 1)
    {
        defWidth = *lpTabPos;
        cTabStops = 0;
    }
    else
    {
        defWidth = 8 * tm.tmAveCharWidth;
    }

    while (count > 0)
    {
        RECT r;
        INT x0;
        x0 = x;
        r.left = x0;
        /* chop the string into substrings of 0 or more <tabs>
         * possibly followed by 1 or more normal characters */
        for (i = 0; i < count; i++)
            if (lpstr[i] != '\t')
                break;
        for (j = i; j < count; j++)
            if (lpstr[j] == '\t')
                break;
        /* get the extent of the normal character part */
        GetTextExtentPointA(hdc, lpstr + i, j - i, &extent);
        /* and if there is a <tab>, calculate its position */
        if (i)
        {
            /* get x coordinate for the drawing of this string */
            for (; cTabStops >= i; lpTabPos++, cTabStops--)
            {
                if (nTabOrg + abs(*lpTabPos) > x)
                {
                    if (lpTabPos[i - 1] >= 0)
                    {
                        /* a left aligned tab */
                        x0 = nTabOrg + lpTabPos[i - 1];
                        x = x0 + extent.cx;
                        break;
                    }
                    else
                    {
                        /* if tab pos is negative then text is right-aligned
                         * to tab stop meaning that the string extends to the
                         * left, so we must subtract the width of the string */
                        if (nTabOrg - lpTabPos[i - 1] - extent.cx > x)
                        {
                            x = nTabOrg - lpTabPos[i - 1];
                            x0 = x - extent.cx;
                            break;
                        }
                    }
                }
            }
            /* if we have run out of tab stops and we have a valid default tab
             * stop width then round x up to that width */
            if ((cTabStops < i) && (defWidth > 0))
            {
                x0 = nTabOrg + ((x - nTabOrg) / defWidth + i) * defWidth;
                x = x0 + extent.cx;
            }
            else if ((cTabStops < i) && (defWidth < 0))
            {
                x = nTabOrg + ((x - nTabOrg + extent.cx) / -defWidth + i) * -defWidth;
                x0 = x - extent.cx;
            }
        }
        else
            x += extent.cx;

        if (!extent.cy)
            extent.cy = tm.tmHeight;

        if (fDisplayText)
        {
            r.top = y;
            r.right = x;
            r.bottom = y + extent.cy;
            ExtTextOutA(hdc, x0, y, GetBkMode(hdc) == OPAQUE ? ETO_OPAQUE : 0, &r, lpstr + i, j - i, NULL);
        }
        count -= j;
        lpstr += j;
    }

    return MAKELONG(x - start, extent.cy);
}

DWORD WINAPI GetTabbedTextExtentA(HDC hdc,                        // handle to DC
                                  LPCSTR lpString,                // character string
                                  int nCount,                     // number of characters
                                  int nTabPositions,              // number of tab positions
                                  CONST LPINT lpnTabStopPositions // array of tab positions
)
{
    return TEXT_TabbedTextOut(hdc, 0, 0, lpString, nCount, nTabPositions, lpnTabStopPositions, 0, FALSE);
}

DWORD WINAPI GetTabbedTextExtentW(HDC hDC,                        // handle to DC
                                  LPCWSTR lpString,               // character string
                                  int nCount,                     // number of characters
                                  int nTabPositions,              // number of tab positions
                                  CONST LPINT lpnTabStopPositions // array of tab positions
)
{
    std::string str;
    tostring(lpString, nCount, str);
    return GetTabbedTextExtentA(hDC, str.c_str(), str.length(), nTabPositions, lpnTabStopPositions);
}

LONG WINAPI TabbedTextOutA(HDC hdc,                         // handle to DC
                           int X,                           // x-coord of start
                           int Y,                           // y-coord of start
                           LPCSTR lpString,                 // character string
                           int nCount,                      // number of characters
                           int nTabPositions,               // number of tabs in array
                           CONST LPINT lpnTabStopPositions, // array of tab positions
                           int nTabOrigin                   // start of tab expansion
)
{
    return TEXT_TabbedTextOut(hdc, X, Y, lpString, nCount, nTabPositions, lpnTabStopPositions, nTabOrigin, TRUE);
}

LONG WINAPI TabbedTextOutW(HDC hDC,                         // handle to DC
                           int X,                           // x-coord of start
                           int Y,                           // y-coord of start
                           LPCWSTR lpString,                // character string
                           int nCount,                      // number of characters
                           int nTabPositions,               // number of tabs in array
                           CONST LPINT lpnTabStopPositions, // array of tab positions
                           int nTabOrigin                   // start of tab expansion
)
{
    std::string str;
    tostring(lpString, nCount, str);
    return TabbedTextOutA(hDC, X, Y, str.c_str(), str.length(), nTabPositions, lpnTabStopPositions, nTabOrigin);
}

BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
{
    CGFloat ascent = 0, descent = 0, x_advance = 0;
    if (c > 0)
    {
        CTLineRef line = CreateCTLineWithDC(hdc, lpString, c, &ascent, &descent, &x_advance);
        if (line)
            CFRelease(line);
    }
    psizl->cx = x_advance;
    psizl->cy = ascent + descent;
    return TRUE;
}

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
    std::string str;
    tostring(lpString, c, str);
    return GetTextExtentPoint32A(hdc, str.c_str(), str.length(), psizl);
}

BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE psizl)
{
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32A(hdc, lpszString, cchString, psizl);
    if (cchString < 0)
        cchString = (int)strlen(lpszString);
    CGFloat ascent = 0, descent = 0;
    bool gotMetrics = false;
    int totalWid = 0;
    int i = 0;
    while (i < cchString)
    {
        int chLen = swinx::UTF8CharLength(lpszString[i]);
        if(i + chLen > cchString) chLen = cchString - i;
        CGFloat chAscent = 0, chDescent = 0, chWid = 0;
        CTLineRef line = CreateCTLineWithDC(hdc, lpszString + i, chLen, &chAscent, &chDescent, &chWid);
        if (line)
        {
            if (!gotMetrics)
            {
                ascent = chAscent;
                descent = chDescent;
                gotMetrics = true;
            }
            CFRelease(line);
        }
        if (totalWid + chWid > nMaxExtent)
        {
            if (lpnFit)
                *lpnFit = i;
            break;
        }
        totalWid += (int)chWid;
        if (lpnDx)
        {
            for (int j = 0; j < chLen; j++)
                lpnDx[i + j] = totalWid;
        }
        i += chLen;
    }
    if (lpnFit && i == cchString)
        *lpnFit = cchString;
    psizl->cx = totalWid;
    psizl->cy = (LONG)(ascent + descent);
    return TRUE;
}

BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE psizl)
{
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32W(hdc, lpszString, cchString, psizl);
    std::string str;
    tostring(lpszString, cchString, str);
    const char *lpszStringA = str.c_str();
    int cchStringA = (int)str.length();
    CGFloat ascent = 0, descent = 0;
    bool gotMetrics = false;
    int totalWid = 0;
    int i = 0, iW = 0;
    while (i < cchStringA)
    {
        int chLen = swinx::UTF8CharLength(lpszStringA[i]);
        if(i + chLen > cchStringA) chLen = cchStringA - i;
        CGFloat chAscent = 0, chDescent = 0, chWid = 0;
        CTLineRef line = CreateCTLineWithDC(hdc, lpszStringA + i, chLen, &chAscent, &chDescent, &chWid);
        if (line)
        {
            if (!gotMetrics)
            {
                ascent = chAscent;
                descent = chDescent;
                gotMetrics = true;
            }
            CFRelease(line);
        }
        int wChars = swinx::WideCharLength(lpszString[iW]);
        if (totalWid + chWid > nMaxExtent)
        {
            if (lpnFit)
                *lpnFit = iW;
            break;
        }
        totalWid += (int)chWid;
        if (lpnDx)
        {
            for (int j = 0; j < wChars; j++)
                lpnDx[iW + j] = totalWid;
        }
        i += chLen;
        iW += wChars;
    }
    if (lpnFit && iW == cchString)
        *lpnFit = cchString;
    psizl->cx = totalWid;
    psizl->cy = (LONG)(ascent + descent);
    return TRUE;
}

/* the various registry keys that are used to store parameters */
enum parameter_key
{
    COLORS_KEY,
    DESKTOP_KEY,
    KEYBOARD_KEY,
    MOUSE_KEY,
    METRICS_KEY,
    SOUND_KEY,
    VERSION_KEY,
    SHOWSOUNDS_KEY,
    KEYBOARDPREF_KEY,
    SCREENREADER_KEY,
    AUDIODESC_KEY,
    NB_PARAM_KEYS
};
struct sysparam_entry
{
};

struct sysparam_rgb_entry
{
    enum parameter_key base_key;
    const char *regval;
    COLORREF val;
};

static struct sysparam_rgb_entry system_colors[] = {
#define RGB_ENTRY(name, val, reg) \
    {                             \
        COLORS_KEY, reg, (val)    \
    }
    RGB_ENTRY(COLOR_SCROLLBAR, RGB(200, 200, 200), "Scrollbar"),
    RGB_ENTRY(COLOR_BACKGROUND, RGB(0, 0, 0), "Background"),
    RGB_ENTRY(COLOR_ACTIVECAPTION, RGB(153, 180, 209), "ActiveTitle"),
    RGB_ENTRY(COLOR_INACTIVECAPTION, RGB(191, 205, 219), "InactiveTitle"),
    RGB_ENTRY(COLOR_MENU, RGB(240, 240, 240), "Menu"),
    RGB_ENTRY(COLOR_WINDOW, RGB(255, 255, 255), "Window"),
    RGB_ENTRY(COLOR_WINDOWFRAME, RGB(100, 100, 100), "WindowFrame"),
    RGB_ENTRY(COLOR_MENUTEXT, RGB(0, 0, 0), "MenuText"),
    RGB_ENTRY(COLOR_WINDOWTEXT, RGB(0, 0, 0), "WindowText"),
    RGB_ENTRY(COLOR_CAPTIONTEXT, RGB(0, 0, 0), "TitleText"),
    RGB_ENTRY(COLOR_ACTIVEBORDER, RGB(180, 180, 180), "ActiveBorder"),
    RGB_ENTRY(COLOR_INACTIVEBORDER, RGB(244, 247, 252), "InactiveBorder"),
    RGB_ENTRY(COLOR_APPWORKSPACE, RGB(171, 171, 171), "AppWorkSpace"),
    RGB_ENTRY(COLOR_HIGHLIGHT, RGB(0, 120, 215), "Hilight"),
    RGB_ENTRY(COLOR_HIGHLIGHTTEXT, RGB(255, 255, 255), "HilightText"),
    RGB_ENTRY(COLOR_BTNFACE, RGB(240, 240, 240), "ButtonFace"),
    RGB_ENTRY(COLOR_BTNSHADOW, RGB(160, 160, 160), "ButtonShadow"),
    RGB_ENTRY(COLOR_GRAYTEXT, RGB(109, 109, 109), "GrayText"),
    RGB_ENTRY(COLOR_BTNTEXT, RGB(0, 0, 0), "ButtonText"),
    RGB_ENTRY(COLOR_INACTIVECAPTIONTEXT, RGB(0, 0, 0), "InactiveTitleText"),
    RGB_ENTRY(COLOR_BTNHIGHLIGHT, RGB(255, 255, 255), "ButtonHilight"),
    RGB_ENTRY(COLOR_3DDKSHADOW, RGB(105, 105, 105), "ButtonDkShadow"),
    RGB_ENTRY(COLOR_3DLIGHT, RGB(227, 227, 227), "ButtonLight"),
    RGB_ENTRY(COLOR_INFOTEXT, RGB(0, 0, 0), "InfoText"),
    RGB_ENTRY(COLOR_INFOBK, RGB(255, 255, 225), "InfoWindow"),
    RGB_ENTRY(COLOR_ALTERNATEBTNFACE, RGB(0, 0, 0), "ButtonAlternateFace"),
    RGB_ENTRY(COLOR_HOTLIGHT, RGB(0, 102, 204), "HotTrackingColor"),
    RGB_ENTRY(COLOR_GRADIENTACTIVECAPTION, RGB(185, 209, 234), "GradientActiveTitle"),
    RGB_ENTRY(COLOR_GRADIENTINACTIVECAPTION, RGB(215, 228, 242), "GradientInactiveTitle"),
    RGB_ENTRY(COLOR_MENUHILIGHT, RGB(51, 153, 255), "MenuHilight"),
    RGB_ENTRY(COLOR_MENUBAR, RGB(240, 240, 240), "MenuBar")
#undef RGB_ENTRY
};

COLORREF GetSysColor(int i)
{
    if (i >= 0 && i < ARRAYSIZE(system_colors))
        return system_colors[i].val;
    return RGBA(255, 255, 255, 255);
}

class SysColorBrush {
  public:
    SysColorBrush()
    {
        for (int i = 0; i <= COLOR_MENUBAR; i++)
        {
            hSysColorBr[i] = CreateSolidBrush(system_colors[i].val);
        }
    }
    ~SysColorBrush()
    {
        for (int i = 0; i <= COLOR_MENUBAR; i++)
        {
            DeleteObject(hSysColorBr[i]);
        }
    }

    HBRUSH hSysColorBr[COLOR_MENUBAR + 1];
};

HBRUSH GetSysColorBrush(int i)
{
    static SysColorBrush sysColorBrs;
    if (i < 0 || i > COLOR_MENUBAR)
        return nullptr;
    return sysColorBrs.hSysColorBr[i];
}

class SysColorPen {
  public:
    SysColorPen()
    {
        for (int i = 0; i <= COLOR_MENUBAR; i++)
        {
            hSysColorPen[i] = CreatePen(PS_SOLID, 1, system_colors[i].val);
        }
    }
    ~SysColorPen()
    {
        for (int i = 0; i <= COLOR_MENUBAR; i++)
        {
            DeleteObject(hSysColorPen[i]);
        }
    }

    HPEN hSysColorPen[COLOR_MENUBAR + 1];
};

HPEN GetSysColorPen(int i)
{
    static SysColorPen sysColorPens;
    return sysColorPens.hSysColorPen[i];
}

// Standard stock brushes (created via CreateSolidBrush, same lifecycle pattern as GetSysColorBrush)
class StockBrush {
  public:
    StockBrush()
    {
        hStockBr[0] = CreateSolidBrush(RGBA(255, 255, 255, 255)); // WHITE_BRUSH
        hStockBr[1] = CreateSolidBrush(RGBA(192, 192, 192, 255)); // LTGRAY_BRUSH
        hStockBr[2] = CreateSolidBrush(RGBA(128, 128, 128, 255)); // GRAY_BRUSH
        hStockBr[3] = CreateSolidBrush(RGBA(64, 64, 64, 255));   // DKGRAY_BRUSH
        hStockBr[4] = CreateSolidBrush(RGBA(0, 0, 0, 255));     // BLACK_BRUSH
    }
    ~StockBrush()
    {
        for (int i = 0; i < 5; i++)
        {
            DeleteObject(hStockBr[i]);
        }
    }

    HBRUSH hStockBr[5];
};

static HBRUSH GetStockBrush(int i)
{
    static StockBrush stockBrs;
    int idx = -1;
    switch (i)
    {
    case WHITE_BRUSH:  idx = 0; break;
    case LTGRAY_BRUSH: idx = 1; break;
    case GRAY_BRUSH:   idx = 2; break;
    case DKGRAY_BRUSH: idx = 3; break;
    case BLACK_BRUSH:  idx = 4; break;
    }
    if (idx < 0) return nullptr;
    return stockBrs.hStockBr[idx];
}

HGDIOBJ GetStockObject(int i)
{
    switch (i)
    {
    case NULL_BITMAP:
    {
        static _Handle bmp(OBJ_BITMAP, nullptr, nullptr);
        return &bmp;
    }
    case NULL_BRUSH:
    {
        static LOGBRUSH log;
        log.lbStyle = BS_NULL;
        log.lbColor = RGBA(0, 0, 0, 0);
        static _Handle br(OBJ_BRUSH, &log, nullptr);
        return &br;
    }
    case WHITE_BRUSH:
    case LTGRAY_BRUSH:
    case GRAY_BRUSH:
    case DKGRAY_BRUSH:
    case BLACK_BRUSH:
    {
        return (HGDIOBJ)GetStockBrush(i);
    }
    case NULL_PEN:
    {
        static LOGPEN log;
        log.lopnStyle = PS_NULL;
        log.lopnWidth.x = 0;
        static _Handle pen(OBJ_PEN, &log, nullptr);
        return &pen;
    }
    case BLACK_PEN:
    {
        static LOGPEN log;
        log.lopnStyle = PS_SOLID;
        log.lopnWidth.x = 1;
        log.lopnColor = RGBA(0, 0, 0, 255);
        static _Handle pen(OBJ_PEN, &log, nullptr);
        return &pen;
    }
    case WHITE_PEN:
    {
        static LOGPEN log;
        log.lopnStyle = PS_SOLID;
        log.lopnWidth.x = 1;
        log.lopnColor = RGBA(255, 255, 255, 255);
        static _Handle pen(OBJ_PEN, &log, nullptr);
        return &pen;
    }
    case SYSTEM_FONT:
    case DEFAULT_GUI_FONT:
    {
        static LOGFONTA lf = { 0 };
#ifdef _WIN32
        strcpy(lf.lfFaceName, "宋体");
#else
        strcpy(lf.lfFaceName, "simsun");
#endif //_WIN32
        lf.lfHeight = 20;
        lf.lfWeight = 400;
        static _Handle font(OBJ_FONT, &lf, nullptr);
        return &font;
    }
    }
    return HGDIOBJ(0);
}

BOOL Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double wid = right - left, hei = bottom - top;

    // Build path in user coordinates (save/restore for CTM-isolation, path survives)
    CGContextSaveGState(ctx);
    CGContextAddRect(ctx, CGRectMake(left, top, wid, hei));
    CGContextRestoreGState(ctx);

    if (hdc->pathRecording)
    {
        return TRUE;
    }
    CGContextSaveGState(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left, top);
    CGContextRestoreGState(ctx);
    return TRUE;
}

static void addRoundRectToPath(CGMutablePathRef path, CGRect rect, double rx, double ry)
{
    if (rx <= 0.0 || ry <= 0.0)
    {
        CGPathAddRect(path, NULL, rect);
        return;
    }
    double x = CGRectGetMinX(rect);
    double y = CGRectGetMinY(rect);
    double w = CGRectGetWidth(rect);
    double h = CGRectGetHeight(rect);
    double max_x = x + w;
    double max_y = y + h;
    CGPathMoveToPoint(path, NULL, x + rx, y);
    CGPathAddArcToPoint(path, NULL, max_x, y, max_x, y + ry, rx);
    CGPathAddArcToPoint(path, NULL, max_x, max_y, max_x - rx, max_y, ry);
    CGPathAddArcToPoint(path, NULL, x, max_y, x, max_y - ry, rx);
    CGPathAddArcToPoint(path, NULL, x, y, x + rx, y, ry);
    CGPathCloseSubpath(path);
}

static void addRoundRectToContext(CGContextRef ctx, CGRect rect, double rx, double ry)
{
    if (rx <= 0.0 || ry <= 0.0)
    {
        CGContextAddRect(ctx, rect);
        return;
    }
    double x = CGRectGetMinX(rect);
    double y = CGRectGetMinY(rect);
    double w = CGRectGetWidth(rect);
    double h = CGRectGetHeight(rect);
    double max_x = x + w;
    double max_y = y + h;
    CGContextMoveToPoint(ctx, x + rx, y);
    CGContextAddArcToPoint(ctx, max_x, y, max_x, y + ry, rx);
    CGContextAddArcToPoint(ctx, max_x, max_y, max_x - rx, max_y, ry);
    CGContextAddArcToPoint(ctx, x, max_y, x, max_y - ry, rx);
    CGContextAddArcToPoint(ctx, x, y, x + rx, y, ry);
    CGContextClosePath(ctx);
}

BOOL RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double wid = right - left, hei = bottom - top;
    double rx = width / 2.0, ry = height / 2.0;
    CGRect rc = CGRectMake(left, top, wid, hei);

    // Build path in user coordinates (save/restore for CTM-isolation, path survives)
    CGContextSaveGState(ctx);
    addRoundRectToContext(ctx, rc, rx, ry);
    CGContextRestoreGState(ctx);

    if (hdc->pathRecording)
    {
        return TRUE;
    }
    CGContextSaveGState(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left, top);
    CGContextRestoreGState(ctx);
    return TRUE;
}

int SetPolyFillMode(HDC hdc, int mode)
{
    int ret = hdc->polyFillMode;
    hdc->polyFillMode = mode;
    return ret;
}

BOOL Polyline(HDC hdc, const POINT *apt, int cpt)
{
    if(!hdc->cgCtx || cpt < 2) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (hdc->pathRecording)
    {
        CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
        for (int i = 1; i < cpt; i++)
        {
            CGContextAddLineToPoint(ctx, apt[i].x, apt[i].y);
        }
        return TRUE;
    }
    CGContextSaveGState(ctx);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
    for (int i = 1; i < cpt; i++)
    {
        CGContextAddLineToPoint(ctx, apt[i].x, apt[i].y);
    }
    double x1, y1, x2, y2;
    x1 = x2 = apt[0].x; y1 = y2 = apt[0].y;
    for(int i = 1; i < cpt; i++)
    {
        if(apt[i].x < x1) x1 = apt[i].x;
        if(apt[i].x > x2) x2 = apt[i].x;
        if(apt[i].y < y1) y1 = apt[i].y;
        if(apt[i].y > y2) y2 = apt[i].y;
    }
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL PolyBezier(HDC hdc, const POINT *apt, DWORD cpt)
{
    if(!hdc->cgCtx || !apt || cpt < 4) return FALSE;
    if ((cpt - 1) % 3 != 0) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (hdc->pathRecording)
    {
        CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
        for (DWORD i = 1; i < cpt; i += 3)
        {
            if (i + 2 >= cpt) break;
            CGContextAddCurveToPoint(ctx, apt[i].x, apt[i].y, apt[i+1].x, apt[i+1].y, apt[i+2].x, apt[i+2].y);
        }
        return TRUE;
    }
    CGContextSaveGState(ctx);
    CGContextBeginPath(ctx);
    CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
    for (DWORD i = 1; i < cpt; i += 3)
    {
        if (i + 2 >= cpt) break;
        CGContextAddCurveToPoint(ctx, apt[i].x, apt[i].y, apt[i+1].x, apt[i+1].y, apt[i+2].x, apt[i+2].y);
    }
    double x1, y1, x2, y2;
    x1 = x2 = apt[0].x; y1 = y2 = apt[0].y;
    for(DWORD i = 1; i < cpt; i++)
    {
        if(apt[i].x < x1) x1 = apt[i].x;
        if(apt[i].x > x2) x2 = apt[i].x;
        if(apt[i].y < y1) y1 = apt[i].y;
        if(apt[i].y > y2) y2 = apt[i].y;
    }
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (!apt || cpt < 3)
        return FALSE;
    if (cpt % 3 != 0)
        return FALSE;

    if (hdc->pathRecording)
    {
        for (DWORD i = 0; i < cpt; i += 3)
        {
            if (i + 2 >= cpt)
                break;
            CGContextAddCurveToPoint(ctx, apt[i].x, apt[i].y,
                                 apt[i + 1].x, apt[i + 1].y,
                                 apt[i + 2].x, apt[i + 2].y);
        }
        return TRUE;
    }

    if (!CGContextIsPathEmpty(ctx) == false)
        return FALSE;

    for (DWORD i = 0; i < cpt; i += 3)
    {
        if (i + 2 >= cpt)
            break;
        CGContextAddCurveToPoint(ctx, apt[i].x, apt[i].y,
                                 apt[i + 1].x, apt[i + 1].y,
                                 apt[i + 2].x, apt[i + 2].y);
    }

    CGPoint finalPt = CGContextGetPathCurrentPoint(ctx);
    CGRect bb = CGContextGetPathBoundingBox(ctx);
    double x1=bb.origin.x, y1=bb.origin.y, x2=bb.origin.x+bb.size.width, y2=bb.origin.y+bb.size.height;
    CGContextSaveGState(ctx);
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    CGContextRestoreGState(ctx);
    CGContextMoveToPoint(ctx, finalPt.x, finalPt.y);
    return TRUE;
}

int ClearRect(HDC hdc, const RECT *lprc, COLORREF cr)
{
    if(!hdc->cgCtx) return 0;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    double wid = lprc->right - lprc->left, hei = lprc->bottom - lprc->top;
    CGRect rc = CGRectMake(lprc->left, lprc->top, wid, hei);
    CairoColor cr2(cr);
    CGContextSetRGBFillColor(ctx, cr2.r, cr2.g, cr2.b, cr2.a);
    CGContextSetBlendMode(ctx, kCGBlendModeCopy);
    CGContextFillRect(ctx, rc);
    CGContextRestoreGState(ctx);
    return 1;
}

int FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
    int ret = 0;
    if(!hdc->cgCtx) return 0;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    // ApplyRop2 (blend mode) must be set before ApplyBrush, matching DrawPathFillStroke order.
    CGBlendMode blend = kCGBlendModeNormal;
    switch(hdc->rop2) {
        case R2_EXT_XOR: blend = kCGBlendModeXOR; break;
        case R2_NOT: blend = kCGBlendModeDestinationOut; break;
        default: blend = kCGBlendModeNormal; break;
    }
    CGContextSetBlendMode(ctx, blend);
    double wid = lprc->right - lprc->left, hei = lprc->bottom - lprc->top;
    BrushKind brushKind = kBrushColor;
    void *patternObj = nullptr;
    if (ApplyBrush(hdc, hbr, wid, hei, lprc->left, lprc->top, &brushKind, &patternObj))
    {
        if (brushKind == kBrushShading)
        {
            GradientDrawInfo *gdi = (GradientDrawInfo *)patternObj;
            CGContextSaveGState(ctx);
            CGContextClipToRect(ctx, CGRectMake(lprc->left, lprc->top, wid, hei));
            if (gdi->isRadial)
                CGContextDrawRadialGradient(ctx, gdi->gradient, gdi->p0, gdi->r0, gdi->p1, gdi->r1, 0);
            else
                CGContextDrawLinearGradient(ctx, gdi->gradient, gdi->p0, gdi->p1, 0);
            CGContextRestoreGState(ctx);
        }
        else
        {
            CGContextFillRect(ctx, CGRectMake(lprc->left, lprc->top, wid, hei));
        }
        ret = 1;
    }
    CGContextRestoreGState(ctx);
    return ret;
}

int FrameRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    double rc_wid = lprc->right - lprc->left, rc_hei = lprc->bottom - lprc->top;
    ApplyPen(hdc, hdc->pen, rc_wid, rc_hei, lprc->left, lprc->top);
    CGBlendMode blend = kCGBlendModeNormal;
    switch(hdc->rop2) {
        case R2_EXT_XOR: blend = kCGBlendModeXOR; break;
        case R2_NOT: blend = kCGBlendModeDestinationOut; break;
        default: blend = kCGBlendModeNormal; break;
    }
    CGContextSetBlendMode(ctx, blend);
    CGContextStrokeRect(ctx, CGRectMake(lprc->left, lprc->top, rc_wid, rc_hei));
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL InvertRect(HDC hdc, const RECT *lprc)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, 1.0, 1.0, 1.0, 1.0);
    CGContextSetBlendMode(ctx, kCGBlendModeXOR);
    CGContextFillRect(ctx, CGRectMake(lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top));
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lpPoint)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (lpPoint)
    {
        if (!CGContextIsPathEmpty(ctx)) {
            CGPoint cur = CGContextGetPathCurrentPoint(ctx);
            lpPoint->x = (int)cur.x;
            lpPoint->y = (int)cur.y;
        } else {
            lpPoint->x = 0;
            lpPoint->y = 0;
        }
    }
    CGContextMoveToPoint(ctx, x, y);
    return TRUE;
}

BOOL GetCurrentPositionEx(HDC hdc, LPPOINT lpPoint)
{
    if(!hdc->cgCtx || !lpPoint) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (!CGContextIsPathEmpty(ctx)) {
        CGPoint cur = CGContextGetPathCurrentPoint(ctx);
        lpPoint->x = (int)cur.x;
        lpPoint->y = (int)cur.y;
    } else {
        lpPoint->x = 0;
        lpPoint->y = 0;
    }
    return TRUE;
}

BOOL LineTo(HDC hdc, int nXEnd, int nYEnd)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (hdc->pathRecording)
    {
        CGContextAddLineToPoint(ctx, nXEnd, nYEnd);
        return TRUE;
    }
    CGContextAddLineToPoint(ctx, nXEnd, nYEnd);
    CGRect bb = CGContextGetPathBoundingBox(ctx);
    double x1=bb.origin.x, y1=bb.origin.y, x2=bb.origin.x+bb.size.width, y2=bb.origin.y+bb.size.height;
    CGContextSaveGState(ctx);
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    CGContextRestoreGState(ctx);
    // Restore current point (DrawPathStroke consumed the path)
    CGContextMoveToPoint(ctx, nXEnd, nYEnd);
    return TRUE;
}

BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double wid = right - left, hei = bottom - top;
    double cx = (left + right) / 2.0;
    double cy = (top + bottom) / 2.0;

    // Build ellipse path in user coordinates - path survives save/restore of CTM
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, cx, cy);
    CGContextScaleCTM(ctx, wid, hei);
    CGContextMoveToPoint(ctx, 0.5, 0);
    CGContextAddArc(ctx, 0, 0, 0.5, 0, M_PI * 2, false);
    CGContextRestoreGState(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    CGContextSaveGState(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left, top);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL Pie(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    double dx3 = (double)(x3 - cx) / wid;
    double dx4 = (double)(x4 - cx) / wid;
    double dy3 = (double)(y3 - cy) / hei;
    double dy4 = (double)(y4 - cy) / hei;
    // Match cairo: arc1=atan2(y3,x3), arc2=atan2(y4,x4); cairo arc(arc2, arc1) CCW
    double arc_start = atan2(dy4, dx4);  // start (matches cairo arc2)
    double arc_end   = atan2(dy3, dx3);  // end   (matches cairo arc1)

    // Build pie path in user coordinates (path survives save/restore)
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, cx, cy);
    CGContextScaleCTM(ctx, wid, hei);
    CGContextMoveToPoint(ctx, 0, 0);
    CGContextAddLineToPoint(ctx, dx4, dy4);
    CGContextAddArc(ctx, 0, 0, 0.5, arc_start, arc_end, false); // false = CCW, same as cairo
    CGContextClosePath(ctx);
    CGContextRestoreGState(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }

    CGContextSaveGState(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, x1, y1);
    CGContextRestoreGState(ctx);
    return TRUE;
}
BOOL Arc(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    double dx3 = (double)(x3 - cx) / wid;
    double dx4 = (double)(x4 - cx) / wid;
    double dy3 = (double)(y3 - cy) / hei;
    double dy4 = (double)(y4 - cy) / hei;
    // Match cairo: arc(arc2, arc1) CCW, arc2=atan2(y4,x4), arc1=atan2(y3,x3)
    double arc_start = atan2(dy4, dx4);
    double arc_end   = atan2(dy3, dx3);

    // Build arc path in user coordinates (path survives save/restore)
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, cx, cy);
    CGContextScaleCTM(ctx, wid, hei);
    CGContextMoveToPoint(ctx, 0.5 * cos(arc_start), 0.5 * sin(arc_start));
    CGContextAddArc(ctx, 0, 0, 0.5, arc_start, arc_end, false); // false = CCW
    CGContextRestoreGState(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    CGContextSaveGState(ctx);
    DrawPathStroke(ctx, hdc, wid, hei, x1, y1);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL Chord(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    double cx = (x1 + x2) / 2.0;
    double cy = (y1 + y2) / 2.0;
    double dx3 = (double)(x3 - cx) / wid;
    double dx4 = (double)(x4 - cx) / wid;
    double dy3 = (double)(y3 - cy) / hei;
    double dy4 = (double)(y4 - cy) / hei;
    // Match cairo: arc(arc2, arc1) CCW
    double arc_start = atan2(dy4, dx4);
    double arc_end   = atan2(dy3, dx3);

    // Build chord path in user coordinates (path survives save/restore)
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, cx, cy);
    CGContextScaleCTM(ctx, wid, hei);
    CGContextMoveToPoint(ctx, 0.5 * cos(arc_start), 0.5 * sin(arc_start));
    CGContextAddArc(ctx, 0, 0, 0.5, arc_start, arc_end, false); // false = CCW
    CGContextClosePath(ctx);
    CGContextRestoreGState(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    CGContextSaveGState(ctx);
    DrawPathStroke(ctx, hdc, wid, hei, x1, y1);
    CGContextRestoreGState(ctx);
    return TRUE;
}

// Compute the "total" user transform = worldMtx * T(ptOrigin).
// This matches the cairo implementation's update_transform, which computes
// mtx * T(ptOrigin) (matrix multiplication, NOT simple tx/ty addition).
// The simple addition (total.tx += ptOrigin.x) is only correct when worldMtx
// is a pure translation or identity; with rotation/scale/skew it produces
// wrong results, causing centering errors and matrix drift/accumulation.
static inline CGAffineTransform calc_total(HDC hdc)
{
    if (hdc->ptOrigin.x == 0 && hdc->ptOrigin.y == 0)
        return *hdc->worldMtx;
    // total = worldMtx * T(ox, oy)
    // For CGAffineTransform [a,b,c,d,tx,ty] * T(ox,oy):
    //   result = [a, b, c, d, tx + a*ox + c*oy, ty + b*ox + d*oy]
    CGAffineTransform total = *hdc->worldMtx;
    total.tx += hdc->worldMtx->a * (CGFloat)hdc->ptOrigin.x + hdc->worldMtx->c * (CGFloat)hdc->ptOrigin.y;
    total.ty += hdc->worldMtx->b * (CGFloat)hdc->ptOrigin.x + hdc->worldMtx->d * (CGFloat)hdc->ptOrigin.y;
    return total;
}

// Apply the delta from oldTotal to the current hdc total to the CG context.
// CoreGraphics has no CGContextSetCTM (absolute set); only CGContextConcatCTM
// (multiply).  So we compute delta = inv(oldTotal) * newTotal and concat that.
// The caller must pass the total computed *before* it mutated hdc members.
static void update_transform(HDC hdc, CGAffineTransform oldTotal)
{
    if(!hdc->cgCtx) return;
    CGContextRef ctx = hdc->cgCtx;
    CGAffineTransform newTotal = calc_total(hdc);
    CGAffineTransform delta = CGAffineTransformConcat(CGAffineTransformInvert(oldTotal), newTotal);
    CGContextConcatCTM(ctx, delta);
}

BOOL SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    if (lppt)
    {
        lppt->x = hdc->ptOrigin.x;
        lppt->y = hdc->ptOrigin.y;
    }
    CGAffineTransform oldTotal = calc_total(hdc);
    hdc->ptOrigin.x = x;
    hdc->ptOrigin.y = y;
    update_transform(hdc, oldTotal);
    return TRUE;
}

BOOL GetViewportOrgEx(HDC hdc, LPPOINT lpPoint)
{
    if (!lpPoint)
        return FALSE;
    lpPoint->x = hdc->ptOrigin.x;
    lpPoint->y = hdc->ptOrigin.y;
    return TRUE;
}

BOOL OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    x += hdc->ptOrigin.x;
    y += hdc->ptOrigin.y;
    return SetViewportOrgEx(hdc, x, y, lppt);
}

BOOL WINAPI SetWindowOrgEx(HDC hdc,        // handle to device context
                           int X,          // new x-coordinate of window origin
                           int Y,          // new y-coordinate of window origin
                           LPPOINT lpPoint // original window origin
)
{
    // todo:hjx
    return SetViewportOrgEx(hdc, X, Y, lpPoint);
}

BOOL WINAPI SetWindowExtEx(HDC hdc,      // handle to device context
                           int nXExtent, // new horizontal window extent
                           int nYExtent, // new vertical window extent
                           LPSIZE lpSize // original window extent
)
{
    // todo:hjx
    return FALSE;
}

BOOL GetWorldTransform(HDC hdc, LPXFORM lpxf)
{
    lpxf->eM11 = hdc->worldMtx->a;
    lpxf->eM12 = hdc->worldMtx->b;
    lpxf->eM21 = hdc->worldMtx->c;
    lpxf->eM22 = hdc->worldMtx->d;
    lpxf->eDx = hdc->worldMtx->tx;
    lpxf->eDy = hdc->worldMtx->ty;
    return TRUE;
}

BOOL SetWorldTransform(HDC hdc, const XFORM *lpxf)
{
    CGAffineTransform oldTotal = calc_total(hdc);
    *hdc->worldMtx = CGAffineTransformMake(lpxf->eM11, lpxf->eM12, lpxf->eM21, lpxf->eM22, lpxf->eDx, lpxf->eDy);
    update_transform(hdc, oldTotal);
    return TRUE;
}

BOOL ModifyWorldTransform(HDC hdc,              // handle to device context
                          const XFORM *lpXform, // transformation data
                          DWORD iMode           // modification mode
)
{
    CGAffineTransform oldTotal = calc_total(hdc);
    CGAffineTransform mtx = CGAffineTransformMake(lpXform->eM11, lpXform->eM12, lpXform->eM21, lpXform->eM22, lpXform->eDx, lpXform->eDy);
    switch (iMode)
    {
    case MWT_IDENTITY:
        *hdc->worldMtx = CGAffineTransformIdentity;
        break;
    case MWT_LEFTMULTIPLY:
        *hdc->worldMtx = CGAffineTransformConcat(mtx, *hdc->worldMtx);
        break;
    case MWT_RIGHTMULTIPLY:
        *hdc->worldMtx = CGAffineTransformConcat(*hdc->worldMtx, mtx);
        break;
    default:
        return FALSE;
    }
    update_transform(hdc, oldTotal);
    return TRUE;
}

int SetROP2(HDC hdc, int rop2)
{
    int ret = hdc->rop2;
    hdc->rop2 = rop2;
    return ret;
}

COLORREF SetTextColor(HDC hdc, COLORREF color)
{
    COLORREF ret = hdc->crText;
    hdc->crText = color;
    return ret;
}

COLORREF GetTextColor(HDC hdc)
{
    return hdc->crText;
}

BOOL SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    if (!hdc)
        return FALSE;
    if (lppt)
        *lppt = hdc->brushOrg;
    hdc->brushOrg.x = x;
    hdc->brushOrg.y = y;
    return TRUE;
}

BOOL GetBrushOrgEx(HDC hdc, LPPOINT lppt)
{
    if (!hdc || !lppt)
        return FALSE;
    *lppt = hdc->brushOrg;
    return TRUE;
}

HBITMAP CreateBitmap(int nWidth,         // bitmap width, in pixels
                     int nHeight,        // bitmap height, in pixels
                     UINT cPlanes,       // number of color planes
                     UINT cBitsPerPel,   // number of bits to identify color
                     CONST VOID *lpvBits // color data array
)
{
    if (cPlanes != 1)
        return nullptr;
    GdiBitmap *ret = nullptr;
    switch (cBitsPerPel)
    {
    case 1: // mono color
        ret = GdiBitmapCreate(nWidth, nHeight, GDI_BMP_A1);
        if (!ret)
            break;
        if (lpvBits)
        {
            memcpy(ret->data, lpvBits, ret->stride * nHeight);
        }
        break;
    case 32:
        ret = GdiBitmapCreate(nWidth, nHeight, GDI_BMP_ARGB32);
        if (!ret)
            break;
        if (lpvBits)
        {
            memcpy(ret->data, lpvBits, ret->stride * nHeight);
        }
        break;
    }
    if (ret)
    {
        return InitGdiObj(OBJ_BITMAP, ret);
    }
    else
    {
        return nullptr;
    }
}

static double color16_to_double(USHORT v)
{
    return v * 1.0 / 0xffff;
}

static void NormalizeRect(RECT *prc)
{
    if (prc->left > prc->right)
    {
        std::swap(prc->left, prc->right);
    }
    if (prc->top > prc->bottom)
    {
        std::swap(prc->top, prc->bottom);
    }
}

BOOL GradientFill(HDC hdc, TRIVERTEX *pVertices, ULONG nVertices, void *pMesh, ULONG nMeshElements, DWORD dwMode)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (dwMode == GRADIENT_FILL_TRIANGLE) {
        for (ULONG i = 0; i < nMeshElements; i++) {
            PGRADIENT_TRIANGLE tri = &((PGRADIENT_TRIANGLE)pMesh)[i];
            if (tri->Vertex1 >= nVertices || tri->Vertex2 >= nVertices || tri->Vertex3 >= nVertices)
                return FALSE;
            TRIVERTEX *v0 = pVertices + tri->Vertex1;
            TRIVERTEX *v1 = pVertices + tri->Vertex2;
            TRIVERTEX *v2 = pVertices + tri->Vertex3;
            double r = (color16_to_double(v0->Red) + color16_to_double(v1->Red) + color16_to_double(v2->Red)) / 3.0;
            double g = (color16_to_double(v0->Green) + color16_to_double(v1->Green) + color16_to_double(v2->Green)) / 3.0;
            double b = (color16_to_double(v0->Blue) + color16_to_double(v1->Blue) + color16_to_double(v2->Blue)) / 3.0;
            double a = (color16_to_double(v0->Alpha) + color16_to_double(v1->Alpha) + color16_to_double(v2->Alpha)) / 3.0;
            CGContextSetRGBFillColor(ctx, r, g, b, a);
            CGContextMoveToPoint(ctx, v0->x, v0->y);
            CGContextAddLineToPoint(ctx, v1->x, v1->y);
            CGContextAddLineToPoint(ctx, v2->x, v2->y);
            CGContextClosePath(ctx);
            CGContextFillPath(ctx);
        }
        return TRUE;
    }
    PGRADIENT_RECT pGradientRect = (PGRADIENT_RECT)pMesh;
    for (ULONG i = 0; i < nMeshElements; i++)
    {
        if (pGradientRect[i].UpperLeft >= nVertices || pGradientRect[i].LowerRight >= nVertices)
            return FALSE;
        TRIVERTEX *vertix0 = pVertices + pGradientRect[i].UpperLeft;
        TRIVERTEX *vertix1 = pVertices + pGradientRect[i].LowerRight;
        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGFloat colors[8] = {
            color16_to_double(vertix0->Red), color16_to_double(vertix0->Green),
            color16_to_double(vertix0->Blue), color16_to_double(vertix0->Alpha),
            color16_to_double(vertix1->Red), color16_to_double(vertix1->Green),
            color16_to_double(vertix1->Blue), color16_to_double(vertix1->Alpha)
        };
        CGGradientRef gradient = CGGradientCreateWithColorComponents(colorSpace, colors, NULL, 2);
        CGColorSpaceRelease(colorSpace);
        RECT rc = { vertix0->x, vertix0->y, vertix1->x, vertix1->y };
        NormalizeRect(&rc);
        CGContextSaveGState(ctx);
        CGContextClipToRect(ctx, CGRectMake(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
        CGPoint start, end;
        if (dwMode == GRADIENT_FILL_RECT_H) {
            start = CGPointMake(vertix0->x, vertix0->y);
            end = CGPointMake(vertix1->x, vertix0->y);
        } else {
            start = CGPointMake(vertix0->x, vertix0->y);
            end = CGPointMake(vertix0->x, vertix1->y);
        }
        CGContextDrawLinearGradient(ctx, gradient, start, end, 0);
        CGGradientRelease(gradient);
        CGContextRestoreGState(ctx);
    }
    return TRUE;
}

int GetDeviceCaps(HDC hdc, int cap)
{
    switch (cap)
    {
    case BITSPIXEL:
        return 32; // todo:hjx
    case PLANES:
        return 1;
    case LOGPIXELSX:
        return GetSystemScale() * 96 / 100;
    case LOGPIXELSY:
        return GetSystemScale() * 96 / 100;
    case TECHNOLOGY:
        return DT_RASDISPLAY; // todo:hjx
    }

    return 0;
}

struct _IconObj
{
    BOOL fIcon;
    DWORD xHotspot;
    DWORD yHotspot;
    HBITMAP hbmMask;
    HBITMAP hbmColor;
    WORD cursorId;
};

void SetCursorID(HICON hIcon, WORD cursorId)
{
    hIcon->cursorId = cursorId;
}

WORD GetCursorID(HICON hIcon)
{
    return hIcon->cursorId;
}

POINT GetIconHotSpot(HICON hIcon)
{
    POINT ret = { (LONG)hIcon->xHotspot, (LONG)hIcon->yHotspot };
    return ret;
}

BOOL GetIconInfo(HICON hIcon, PICONINFO piconinfo)
{
    if (!hIcon)
        return FALSE;
    piconinfo->fIcon = hIcon->fIcon;
    piconinfo->xHotspot = hIcon->xHotspot;
    piconinfo->yHotspot = hIcon->yHotspot;
    if (hIcon->hbmColor)
    {
        BITMAP bm;
        GetObject(hIcon->hbmColor, sizeof(bm), &bm);
        piconinfo->hbmColor = CreateBitmap(bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel, bm.bmBits);
    }
    else
    {
        piconinfo->hbmColor = nullptr;
    }
    if (hIcon->hbmMask)
    {
        BITMAP bm;
        GetObject(hIcon->hbmMask, sizeof(bm), &bm);
        piconinfo->hbmMask = CreateBitmap(bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel, bm.bmBits);
    }
    else
    {
        piconinfo->hbmMask = nullptr;
    }
    return TRUE;
}

HICON CreateIconIndirect(PICONINFO piconinfo)
{
    _IconObj *icon = new _IconObj;
    icon->fIcon = piconinfo->fIcon;
    icon->xHotspot = piconinfo->xHotspot;
    icon->yHotspot = piconinfo->yHotspot;
    icon->hbmColor = RefGdiObj(piconinfo->hbmColor);
    icon->hbmMask = RefGdiObj(piconinfo->hbmMask);
    return icon;
}

BOOL DrawIcon(HDC hDC, int X, int Y, HICON hIcon)
{
    return DrawIconEx(hDC, X, Y, hIcon, -1, -1, 0, NULL, DI_NORMAL);
}

BOOL DrawIconEx(HDC hDC, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur, HBRUSH hbrFlickerFreeDraw, UINT diFlags)
{
    if (!hIcon || !hIcon->hbmColor)
        return FALSE;
    BITMAP bm;
    GetObject(hIcon->hbmColor, sizeof(bm), &bm);
    if (bm.bmBitsPixel != 32)
        return FALSE;
    if (cxWidth < 0)
        cxWidth = bm.bmWidth;
    if (cyWidth < 0)
        cyWidth = bm.bmHeight;

    if(!hDC->cgCtx) return FALSE;
    CGContextRef ctx = hDC->cgCtx;
    GdiBitmap *surf = (GdiBitmap *)GetGdiObjPtr(hIcon->hbmColor);
    if (!surf) return FALSE;
    CGImageRef imageRef = surf->getImage();
    if (!imageRef) return FALSE;
    CGContextSaveGState(ctx);
    CGRect destRect = CGRectMake(xLeft, yTop, cxWidth, cyWidth);
    CGRect srcRect = CGRectMake(0,0, cxWidth,cyWidth);
    drawImage(ctx, destRect, imageRef,srcRect);
    CGContextRestoreGState(ctx);
    CGImageRelease(imageRef);
    return TRUE;
}

BOOL DestroyIcon(HICON hIcon)
{
    if (!hIcon)
        return FALSE;
    if (hIcon->hbmColor)
        DeleteObject(hIcon->hbmColor);
    if (hIcon->hbmMask)
        DeleteObject(hIcon->hbmMask);
    delete hIcon;
    return TRUE;
}

BOOL WINAPI GetTextMetricsA(HDC hdc, TEXTMETRICA *txtMetric)
{
    assert(txtMetric);
    if (!hdc)
    {
        assert(hdc);
    }
    if (!ApplyFont(hdc))
        return FALSE;
    memset(txtMetric, 0, sizeof(TEXTMETRICA));
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    if (!lf) return FALSE;
    const char *fontName = lf->lfFaceName;
    CFStringRef cfFontName = CFStringCreateWithCString(NULL, fontName, kCFStringEncodingUTF8);
    CGFloat fontSize = abs(lf->lfHeight) > 0 ? abs(lf->lfHeight) : 12.0;
    CTFontRef ctFont = CTFontCreateWithName(cfFontName, fontSize, NULL);
    CFRelease(cfFontName);
    if (!ctFont) return FALSE;
    CGFloat ascent = CTFontGetAscent(ctFont);
    CGFloat descent = CTFontGetDescent(ctFont);
    CGFloat leading = CTFontGetLeading(ctFont);
    txtMetric->tmAscent = (LONG)ceil(ascent);
    txtMetric->tmDescent = (LONG)ceil(descent);
    txtMetric->tmHeight = (LONG)ceil(ascent + descent + leading);
    unichar xChar = 'x';
    CFStringRef xStr = CFStringCreateWithCharacters(NULL, &xChar, 1);
    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef values[] = { ctFont };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void **)keys, (const void **)values, 1, NULL, NULL);
    CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, xStr, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attrStr);
    double w = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
    txtMetric->tmAveCharWidth = (LONG)ceil(w);
    txtMetric->tmExternalLeading = (LONG)leading;
    CFRelease(line);
    CFRelease(attrStr);
    CFRelease(attrs);
    CFRelease(xStr);
    CFRelease(ctFont);
    txtMetric->tmDigitizedAspectX = 100;
    txtMetric->tmDigitizedAspectY = 100;
    txtMetric->tmItalic = lf->lfItalic;
    txtMetric->tmUnderlined = lf->lfUnderline;
    txtMetric->tmStruckOut = lf->lfStrikeOut;
    txtMetric->tmCharSet = lf->lfCharSet;
    txtMetric->tmPitchAndFamily = lf->lfPitchAndFamily;
    return TRUE;
}

BOOL WINAPI GetTextMetricsW(HDC hdc, TEXTMETRICW *txtMetric)
{
    TEXTMETRICA metricA;
    if (!GetTextMetricsA(hdc, &metricA))
        return FALSE;
    memset(txtMetric, 0, sizeof(TEXTMETRICW));
    txtMetric->tmAscent = metricA.tmAscent;
    txtMetric->tmDescent = metricA.tmDescent;
    txtMetric->tmHeight = metricA.tmHeight;
    txtMetric->tmAveCharWidth = metricA.tmAveCharWidth;
    txtMetric->tmExternalLeading = metricA.tmExternalLeading; // todo:hjx
    txtMetric->tmDigitizedAspectX = metricA.tmDigitizedAspectX;
    txtMetric->tmDigitizedAspectY = metricA.tmDigitizedAspectY;

    txtMetric->tmItalic = metricA.tmItalic;
    txtMetric->tmUnderlined = metricA.tmUnderlined;
    txtMetric->tmStruckOut = metricA.tmStruckOut;
    txtMetric->tmCharSet = metricA.tmCharSet;
    txtMetric->tmPitchAndFamily = metricA.tmPitchAndFamily;
    txtMetric->tmOverhang = metricA.tmOverhang;

    return TRUE;
}

int GetTextFaceA(HDC hdc, int nCount, LPSTR lpFaceName)
{
    assert(hdc->hfont);
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    int len = strlen(lf->lfFaceName);
    if (!lpFaceName)
        return len + 1;
    if (nCount < len + 1)
        return 0;
    strcpy(lpFaceName, lf->lfFaceName);
    return len + 1;
}

int GetTextFaceW(HDC hdc, int nCount, LPWSTR lpFaceName)
{
    assert(hdc->hfont);
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    return MultiByteToWideChar(CP_UTF8, 0, lf->lfFaceName, -1, lpFaceName, nCount);
}

BOOL Polygon_Priv(HDC hdc, const POINT *apt, int cpt)
{
    if (!hdc || !hdc->cgCtx || cpt < 2)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (hdc->pathRecording)
    {
        CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
        for (int i = 1; i < cpt; i++)
        {
            CGContextAddLineToPoint(ctx, apt[i].x, apt[i].y);
        }
        CGContextClosePath(ctx);
        return TRUE;
    }
    CGContextMoveToPoint(ctx, apt[0].x, apt[0].y);
    for (int i = 1; i < cpt; i++)
    {
        CGContextAddLineToPoint(ctx, apt[i].x, apt[i].y);
    }
    CGContextClosePath(ctx);
    CGContextSaveGState(ctx);
    CGRect bb = CGContextGetPathBoundingBox(ctx);
    DrawPathFillStroke(ctx, hdc,  bb.size.width, bb.size.height, bb.origin.x, bb.origin.y);
    CGContextRestoreGState(ctx);
    return TRUE;
}

UINT WINAPI SetTextAlign(HDC hdc, UINT align)
{
    UINT ret = hdc->textAlign;
    hdc->textAlign = align;
    return ret;
}

UINT WINAPI GetTextAlign(HDC hdc)
{
    return hdc->textAlign;
}

COLORREF WINAPI GetNearestColor(HDC hdc,         // handle to DC
                                COLORREF crColor // color to be matched
)
{
    return crColor;
}

BOOL WINAPI ExtTextOutA(HDC hdc,          // handle to DC
                        int X,            // x-coordinate of reference point
                        int Y,            // y-coordinate of reference point
                        UINT fuOptions,   // text-output options
                        CONST RECT *lprc, // optional dimensions
                        LPCSTR lpString,  // string
                        UINT cbCount,     // number of characters in string
                        CONST INT *lpDx   // array of spacing values
)
{
    if(!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    if (cbCount < 0)
        cbCount = (UINT)strlen(lpString);
    CGFloat ascent = 0, descent = 0, lineWid = 0;
    CTLineRef line = CreateCTLineWithDC(hdc, lpString, (int)cbCount, &ascent, &descent, &lineWid);
    double x = X, y = Y;
    switch (hdc->textAlign & (TA_RIGHT | TA_CENTER))
    {
    case TA_RIGHT:
        x -= lineWid;
        break;
    case TA_CENTER:
        x -= lineWid / 2;
        break;
    }
    switch (hdc->textAlign & (TA_BASELINE | TA_BOTTOM | TA_TOP))
    {
    case TA_TOP:
        y += ascent;
        break;
    case TA_BASELINE:
        break;
    case TA_BOTTOM:
        y -= descent;
        break;
    }

    if (hdc->pathRecording)
    {
        CGPoint oldPt;
        bool hasOldPt = !CGContextIsPathEmpty(ctx);
        if (hasOldPt)
            oldPt = CGContextGetPathCurrentPoint(ctx);
        CGContextMoveToPoint(ctx, x, y);
        if (line)
            AddGlyphsToCtxPath(hdc, line, x, y, lineWid);
        if (hdc->textAlign & TA_NOUPDATECP)
        {
            if (hasOldPt)
                CGContextMoveToPoint(ctx, oldPt.x, oldPt.y);
            else
                CGContextMoveToPoint(ctx, 0, 0);
        }
    }
    else
    {
        // Non-path mode: wrap everything in save/restore so ctx state (text
        // matrix, fill color, clip, path) is fully restored on exit — matches
        // 47bed2e and cairo's cairo_save/cairo_restore around cairo_show_text.
        // OPAQUE fill uses an inner save/restore to isolate the fill color.
        // Do NOT call CGContextMoveToPoint after restore — it pollutes the
        // ctx path and breaks subsequent drawing (e.g. Scintilla).
        CGContextSaveGState(ctx);
        if (lprc && (fuOptions & ETO_CLIPPED))
            CGContextClipToRect(ctx, CGRectMake(lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top));
        if (lprc && (fuOptions & ETO_OPAQUE))
        {
            CairoColor cr(hdc->crBk);
            CGContextSetRGBFillColor(ctx, cr.r, cr.g, cr.b, cr.a);
            CGContextFillRect(ctx, CGRectMake(lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top));
        }
        CGContextSetTextMatrix(ctx, CGAffineTransformMake(1, 0, 0, -1, x, y));
        if (line)
            CTLineDraw(line, ctx);
        CGContextRestoreGState(ctx);
    }
    if (line)
        CFRelease(line);
    return TRUE;
}

BOOL WINAPI ExtTextOutW(HDC hdc,          // handle to DC
                        int X,            // x-coordinate of reference point
                        int Y,            // y-coordinate of reference point
                        UINT fuOptions,   // text-output options
                        CONST RECT *lprc, // optional dimensions
                        LPCWSTR lpString, // string
                        UINT cbCount,     // number of characters in string
                        CONST INT *lpDx   // array of spacing values
)
{
    std::string str;
    tostring(lpString, cbCount, str);
    return ExtTextOutA(hdc, X, Y, fuOptions, lprc, str.c_str(), str.length(), lpDx);
}

Antialias WINAPI GetAntialiasMode(HDC hdc)
{
    if(!hdc->cgCtx) return (Antialias)0;
    return Antialias::ANTIALIAS_DEFAULT;
}

Antialias WINAPI SetAntialiasMode(HDC hdc, Antialias mode)
{
    if(!hdc->cgCtx) return (Antialias)0;
    Antialias ret = Antialias::ANTIALIAS_DEFAULT;
    if (mode == Antialias::ANTIALIAS_NONE) {
        CGContextSetShouldAntialias(hdc->cgCtx, false);
        CGContextSetAllowsAntialiasing(hdc->cgCtx, false);
    } else {
        CGContextSetShouldAntialias(hdc->cgCtx, true);
        CGContextSetAllowsAntialiasing(hdc->cgCtx, true);
    }
    return ret;
}

static unsigned char *getPixelData(HDC hdc, int x, int y)
{
    if (!hdc->bmp)
        return nullptr;
    CGPoint pt = CGPointMake(x, y);
    pt = CGPointApplyAffineTransform(pt, *hdc->worldMtx);
    GdiBitmap *surface = (GdiBitmap *)GetGdiObjPtr(hdc->bmp);
    int fmt = surface->format;
    if (fmt != GDI_BMP_ARGB32)
        return nullptr;
    int wid = surface->width;
    int hei = surface->height;
    if (pt.x >= wid || pt.y >= hei)
        return nullptr;
    unsigned char *data = surface->data;
    int offset = ((int)pt.y * wid + (int)pt.x) * 4;
    return data + offset;
}

COLORREF GetPixel(IN HDC hdc, IN int x, IN int y)
{
    const unsigned char *data = getPixelData(hdc, x, y);
    if (!data)
        return 0;
    unsigned char r = data[0];
    unsigned char g = data[1];
    unsigned char b = data[2];
    unsigned char a = data[3];
    return RGBA(r, g, b, a);
}

COLORREF SetPixel(IN HDC hdc, IN int x, IN int y, IN COLORREF color)
{
    COLORREF ret = GetPixel(hdc, x, y);
    CairoColor cr(color);
    if(!hdc->cgCtx) return ret;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextSetRGBFillColor(ctx, cr.r, cr.g, cr.b, cr.a);
    CGContextSetShouldAntialias(ctx, false);
    CGContextFillRect(ctx, CGRectMake(x, y, 1.0, 1.0));
    CGContextRestoreGState(ctx);
    return ret;
}

UINT WINAPI RealizePalette(_In_ HDC hdc)
{
    return 0;
}

HPALETTE WINAPI SelectPalette(_In_ HDC hdc, _In_ HPALETTE hPal, _In_ BOOL bForceBkgd)
{
    return nullptr;
}

BOOL WINAPI DPtoLP(HDC hdc,          // handle to device context
                   LPPOINT lpPoints, // array of points
                   int nCount        // count of points in array
)
{
    // todo:hjx
    return TRUE;
}

BOOL WINAPI LPtoDP(HDC hdc,          // handle to device context
                   LPPOINT lpPoints, // array of points
                   int nCount        // count of points in array
)
{
    // todo:hjx
    return TRUE;
}

BOOL WINAPI GetCharWidthA(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    *lpBuffer = 0;
    for (char c = (char)iFirst; c <= (char)iLast; c++)
    {
        SIZE sz;
        GetTextExtentPoint32A(hdc, &c, 1, &sz);
        *lpBuffer += sz.cx;
    }
    return TRUE;
}
BOOL WINAPI GetCharWidthW(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    *lpBuffer = 0;
    for (wchar_t c = (wchar_t)iFirst; c <= (wchar_t)iLast; c++)
    {
        SIZE sz;
        GetTextExtentPoint32W(hdc, &c, 1, &sz);
        *lpBuffer += sz.cx;
    }
    return TRUE;
}

HDC WINAPI CreateICA(LPCSTR lpszDriver,    // driver name
                     LPCSTR lpszDevice,    // device name
                     LPCSTR lpszOutput,    // port or file name
                     CONST void *lpdvmInit // optional initialization data
)
{
    return CreateCompatibleDC(0);
}

HDC WINAPI CreateICW(LPCWSTR lpszDriver,   // driver name
                     LPCWSTR lpszDevice,   // device name
                     LPCWSTR lpszOutput,   // port or file name
                     CONST void *lpdvmInit // optional initialization data
)
{
    std::string strDriver, strDevice, strOutput;
    tostring(lpszDriver, -1, strDriver);
    tostring(lpszDevice, -1, strDevice);
    tostring(lpszOutput, -1, strOutput);
    return CreateICA(strDriver.c_str(), strDevice.c_str(), strOutput.c_str(), lpdvmInit);
}

#if defined(CAIRO_HAS_QUARTZ_FONT) && CAIRO_HAS_QUARTZ_FONT
extern int macos_register_font(const char *path);
#endif
int AddFontResourceExA(LPCSTR lpszFilename, // font file name
                       DWORD fl,            // font characteristics
                       PVOID pdv            // reserved
)
{
    CFStringRef cfName = CFStringCreateWithCString(kCFAllocatorDefault, lpszFilename, kCFStringEncodingUTF8);
    if (!cfName) return FALSE;
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, cfName, kCFURLPOSIXPathStyle, false);
    CFRelease(cfName);
    if (!url) return FALSE;
    BOOL ok = FALSE;
    CFErrorRef err = NULL;
    if (CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, &err)) {
        ok = TRUE;
    } else {
        if (err) CFRelease(err);
    }
    CFRelease(url);
    return ok ? TRUE : FALSE;
}

int AddFontResourceA(LPCSTR lpszFilename)
{
    return AddFontResourceExA(lpszFilename, 0, 0);
}

int AddFontResourceW(LPCWSTR lpszFilename)
{
    std::string str;
    tostring(lpszFilename, -1, str);
    return AddFontResourceExA(str.c_str(), 0, 0);
}

int AddFontResourceExW(LPCWSTR lpszFilename, // font file name
                       DWORD fl,             // font characteristics
                       PVOID pdv             // reserved
)
{
    std::string str;
    tostring(lpszFilename, -1, str);
    return AddFontResourceExA(str.c_str(), fl, pdv);
}

// ========================================================================
// Path API Implementation
// ========================================================================

BOOL BeginPath(HDC hdc)
{
    if (!hdc || !hdc->cgCtx)
        return FALSE;
    if (hdc->recordedPath)
    {
        CGPathRelease(hdc->recordedPath);
        hdc->recordedPath = nullptr;
    }
    // Save current point before clearing path (mirrors cairo's BeginPath).
    bool hasCurrentPt = !CGContextIsPathEmpty(hdc->cgCtx);
    CGPoint currentPt;
    if (hasCurrentPt)
        currentPt = CGContextGetPathCurrentPoint(hdc->cgCtx);
    CGContextBeginPath(hdc->cgCtx);
    if (hasCurrentPt)
        CGContextMoveToPoint(hdc->cgCtx, currentPt.x, currentPt.y);
    hdc->pathRecording = TRUE;
    return TRUE;
}

BOOL EndPath(HDC hdc)
{
    if (!hdc || !hdc->cgCtx || !hdc->pathRecording)
        return FALSE;
    // Copy path from ctx to recordedPath, similar to cairo_copy_path.
    // Note: do NOT clear the ctx path (no CGContextBeginPath) — the current
    // point must survive EndPath so GetCurrentPositionEx reflects the last
    // text/figure position, matching cairo's behavior.
    if (hdc->recordedPath)
    {
        CGPathRelease(hdc->recordedPath);
        hdc->recordedPath = nullptr;
    }
    hdc->recordedPath = CGContextCopyPath(hdc->cgCtx);
    hdc->pathRecording = FALSE;
    return hdc->recordedPath != nullptr;
}

BOOL AbortPath(HDC hdc)
{
    if (!hdc)
        return FALSE;
    if (hdc->recordedPath)
    {
        CGPathRelease(hdc->recordedPath);
        hdc->recordedPath = nullptr;
    }
    hdc->pathRecording = FALSE;
    if (hdc->cgCtx)
        CGContextBeginPath(hdc->cgCtx);
    return TRUE;
}

BOOL CloseFigure(HDC hdc)
{
    if (!hdc || !hdc->cgCtx)
        return FALSE;
    CGContextClosePath(hdc->cgCtx);
    return TRUE;
}

BOOL StrokePath(HDC hdc)
{
    if (!hdc || !hdc->cgCtx || !hdc->recordedPath)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, hdc->recordedPath);
    CGRect bb = CGPathGetBoundingBox(hdc->recordedPath);
    DrawPathStroke(ctx, hdc, bb.size.width, bb.size.height, bb.origin.x, bb.origin.y);
    CGContextRestoreGState(ctx);
    CGPathRelease(hdc->recordedPath);
    hdc->recordedPath = nullptr;
    return TRUE;
}

BOOL FillPath(HDC hdc)
{
    if (!hdc || !hdc->cgCtx || !hdc->recordedPath)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, hdc->recordedPath);
    CGRect bb = CGPathGetBoundingBox(hdc->recordedPath);
    // ApplyRop2 must be set before ApplyBrush, matching DrawPathFillStroke order.
    CGBlendMode blend = kCGBlendModeNormal;
    switch(hdc->rop2) {
        case R2_EXT_XOR: blend = kCGBlendModeXOR; break;
        case R2_NOT: blend = kCGBlendModeDestinationOut; break;
        default: blend = kCGBlendModeNormal; break;
    }
    CGContextSetBlendMode(ctx, blend);
    if (ApplyBrush(hdc, hdc->brush, bb.size.width, bb.size.height, bb.origin.x, bb.origin.y))
    {
        CGContextFillPath(ctx);
    }
    CGContextRestoreGState(ctx);
    CGPathRelease(hdc->recordedPath);
    hdc->recordedPath = nullptr;
    return TRUE;
}

BOOL StrokeAndFillPath(HDC hdc)
{
    if (!hdc || !hdc->cgCtx || !hdc->recordedPath)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, hdc->recordedPath);
    CGRect bb = CGPathGetBoundingBox(hdc->recordedPath);

    DrawPathFillStroke(ctx, hdc, bb.size.width, bb.size.height, bb.origin.x, bb.origin.y);
    CGContextRestoreGState(ctx);
    CGPathRelease(hdc->recordedPath);
    hdc->recordedPath = nullptr;
    return TRUE;
}

HRGN PathToRegion(HDC hdc)
{
    if (!hdc || !hdc->cgCtx || !hdc->recordedPath)
        return nullptr;
    CGContextSaveGState(hdc->cgCtx);
    CGContextResetClip(hdc->cgCtx);
    CGContextAddPath(hdc->cgCtx,hdc->recordedPath);
    CGContextClip(hdc->cgCtx);
    HRGN hrgn = CreateRectRgn(0, 0, 0, 0);
    GetClipRgn(hdc,hrgn);
    CGContextRestoreGState(hdc->cgCtx);
    CGPathRelease(hdc->recordedPath);
    hdc->recordedPath = nullptr;
    return hrgn;
}

struct AppleGetPathData {
    LPPOINT lpPoints;
    LPBYTE lpTypes;
    int nSize;
    int pointIndex;
};

static void apple_get_path_applier(void *info, const CGPathElement *element) {
    AppleGetPathData *d = (AppleGetPathData*)info;
    if (!d || (d->lpPoints && d->pointIndex >= d->nSize)) return;
    
    switch(element->type) {
        case kCGPathElementMoveToPoint:
            if (d->lpPoints) {
                d->lpPoints[d->pointIndex].x = (LONG)element->points[0].x;
                d->lpPoints[d->pointIndex].y = (LONG)element->points[0].y;
                d->lpTypes[d->pointIndex] = PT_MOVETO;
            }
            d->pointIndex++;
            break;
        case kCGPathElementAddLineToPoint:
            if (d->lpPoints) {
                d->lpPoints[d->pointIndex].x = (LONG)element->points[0].x;
                d->lpPoints[d->pointIndex].y = (LONG)element->points[0].y;
                d->lpTypes[d->pointIndex] = PT_LINETO;
            }
            d->pointIndex++;
            break;
        case kCGPathElementAddQuadCurveToPoint:
            if (d->lpPoints) {
                d->lpPoints[d->pointIndex].x = (LONG)element->points[0].x;
                d->lpPoints[d->pointIndex].y = (LONG)element->points[0].y;
                d->lpTypes[d->pointIndex] = PT_BEZIERTO;
            }
            d->pointIndex++;
            if (d->lpPoints && d->pointIndex < d->nSize) {
                d->lpPoints[d->pointIndex].x = (LONG)element->points[1].x;
                d->lpPoints[d->pointIndex].y = (LONG)element->points[1].y;
                d->lpTypes[d->pointIndex] = PT_BEZIERTO;
            }
            d->pointIndex++;
            if (d->lpPoints && d->pointIndex < d->nSize) {
                d->lpPoints[d->pointIndex].x = (LONG)element->points[1].x;
                d->lpPoints[d->pointIndex].y = (LONG)element->points[1].y;
                d->lpTypes[d->pointIndex] = PT_BEZIERTO;
            }
            d->pointIndex++;
            break;
        case kCGPathElementAddCurveToPoint:
            for (int j = 0; j < 3; j++) {
                if (d->lpPoints && d->pointIndex < d->nSize) {
                    d->lpPoints[d->pointIndex].x = (LONG)element->points[j].x;
                    d->lpPoints[d->pointIndex].y = (LONG)element->points[j].y;
                    d->lpTypes[d->pointIndex] = PT_BEZIERTO;
                }
                d->pointIndex++;
            }
            break;
        case kCGPathElementCloseSubpath:
            if (d->lpPoints && d->pointIndex > 0)
                d->lpTypes[d->pointIndex - 1] |= PT_CLOSEFIGURE;
            break;
    }
}

BOOL SelectClipPath(HDC hdc, int mode)
{
    if (!hdc || !hdc->cgCtx || !hdc->recordedPath)
        return FALSE;
    CGContextRef ctx = hdc->cgCtx;
    CGPathRef saved_path = hdc->recordedPath;
    hdc->recordedPath = nullptr;
    
    switch (mode)
    {
    case RGN_AND:
        CGContextAddPath(ctx, saved_path);
        CGContextClip(ctx);
        break;
    case RGN_OR:
    case RGN_XOR:
    case RGN_DIFF:
    {
        HRGN hrgn1 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn1);
        CGContextSaveGState(ctx);
        CGContextResetClip(ctx);
        CGContextAddPath(ctx, saved_path);
        CGContextClip(ctx);
        HRGN hrgn2 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn2);
        CombineRgn(hrgn1, hrgn1, hrgn2, mode);
        CGContextRestoreGState(ctx);
        SelectClipRgn(hdc, hrgn1);
        DeleteObject(hrgn1);
        DeleteObject(hrgn2);
        break;
    }
    case RGN_COPY:
    default:
        CGContextResetClip(ctx);
        CGContextAddPath(ctx, saved_path);
        CGContextClip(ctx);
        break;
    }
    CGPathRelease(saved_path);
    return TRUE;
}

int GetPath(HDC hdc, LPPOINT lpPoints, LPBYTE lpTypes, int nSize)
{
    if (!hdc || !hdc->recordedPath)
        return -1;
    AppleGetPathData d;
    d.lpPoints = nullptr;
    d.lpTypes = nullptr;
    d.nSize = 0;
    d.pointIndex = 0;
    CGPathApply(hdc->recordedPath, &d, apple_get_path_applier);
    int total = d.pointIndex;
    if (!lpPoints || !lpTypes || nSize == 0)
        return total;
    if (nSize < total)
        return total;
    d.lpPoints = lpPoints;
    d.lpTypes = lpTypes;
    d.nSize = nSize;
    d.pointIndex = 0;
    CGPathApply(hdc->recordedPath, &d, apple_get_path_applier);
    return d.pointIndex;
}

BOOL PolyDraw(HDC hdc, LPPOINT lppt, LPBYTE lpbTypes, int cpt)
{
    if (!hdc || !lppt || !lpbTypes || cpt <= 0)
        return FALSE;
    if (!hdc->cgCtx) return FALSE;
    CGContextRef ctx = hdc->cgCtx;

    for (int i = 0; i < cpt; i++)
    {
        BYTE type = lpbTypes[i];
        POINT pt = lppt[i];

        if (type & PT_MOVETO)
        {
            CGContextMoveToPoint(ctx, pt.x, pt.y);
        }
        else if (type & PT_LINETO)
        {
            CGContextAddLineToPoint(ctx, pt.x, pt.y);
        }
        else if (type & PT_BEZIERTO)
        {
            if (i + 2 < cpt && (lpbTypes[i + 1] & PT_BEZIERTO) && (lpbTypes[i + 2] & PT_BEZIERTO))
            {
                POINT pt1 = lppt[i + 1];
                POINT pt2 = lppt[i + 2];
                CGContextAddCurveToPoint(ctx, pt.x, pt.y, pt1.x, pt1.y, pt2.x, pt2.y);
                i += 2;
            }
        }

        if (type & PT_CLOSEFIGURE)
        {
            CGContextClosePath(ctx);
        }
    }

    if (hdc->pathRecording)
        return TRUE;

    CGRect bb = CGContextGetPathBoundingBox(ctx);
    CGContextSaveGState(ctx);
    DrawPathStroke(ctx, hdc, bb.size.width, bb.size.height, bb.origin.x, bb.origin.y);
    CGContextRestoreGState(ctx);
    return TRUE;
}

BOOL SetMiterLimit(HDC hdc, FLOAT eNewLimit, PFLOAT peOldLimit)
{
    if (!hdc)
        return FALSE;
    if (!hdc->cgCtx) return FALSE;
    CGFloat oldLimit = hdc->miterLimit;
    if (peOldLimit) *peOldLimit = (FLOAT)oldLimit;
    {
        CGContextSetMiterLimit(hdc->cgCtx, eNewLimit);
        hdc->miterLimit = eNewLimit;
    }    
    return TRUE;
}

BOOL GetMiterLimit(HDC hdc, PFLOAT peLimit)
{
    if (!hdc || !peLimit)
        return FALSE;
    if (!hdc->cgCtx) return FALSE;
    *peLimit = hdc->miterLimit;
    return TRUE;
}

