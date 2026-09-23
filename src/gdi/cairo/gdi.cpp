#include <windows.h>
#include <gdi.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <math.h>
#include <png.h>
#include <assert.h>
#include <vector>
#include "handle.h"
#include "sdc.h"
#include "SConnection.h"
#include "cairo_show_text2.h"
#include "drawtext.h"
#include "FontFallback.h"
#include "tostring.h"
#include "uniconv.h"
#include "log.h"
#define kLogTag "gdi"

EXTERN_C BOOL Swinx_DumpBmp(HBITMAP bmp, const char *path)
{
    if (!bmp)
        return FALSE;
    return CAIRO_STATUS_SUCCESS == cairo_surface_write_to_png((cairo_surface_t *)GetGdiObjPtr(bmp), path);
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
    swinx_stl::vector<double> dash;
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
    cairo_surface_destroy((cairo_surface_t *)ptr);
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

struct GradientDetail
{
    swinx_stl::vector<GRADIENTITEM> items;
    GRADIENTINFO info;
};
struct PatternInfo
{
    TILEMODE tileMode;
    BOOL useBmp;
    double alpha;
    union {
        HBITMAP bmp;
        GradientDetail *gradientDetail;
    } data;

    cairo_pattern_t *pattern;

    double width, height;
    double x, y;
    PatternInfo()
        : pattern(nullptr)
        , width(0)
        , height(0)
        , x(0)
        , y(0)
    {
    }

    ~PatternInfo()
    {
        if (pattern)
        {
            cairo_pattern_destroy(pattern);
        }
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

    cairo_pattern_t *create(double wid, double hei, double x, double y)
    {
        if (pattern)
        {
            if (wid == this->width && hei == this->height && x == this->x && y == this->y)
                return pattern;
            cairo_pattern_destroy(pattern);
            pattern = nullptr;
        }
        cairo_pattern_t *ret = nullptr;
        if (useBmp)
        {
            ret = cairo_pattern_create_for_surface((cairo_surface_t *)GetGdiObjPtr(data.bmp));
            // Set pattern matrix to adjust position
            cairo_matrix_t matrix;
            cairo_matrix_init_translate(&matrix, x, y);
            cairo_pattern_set_matrix(ret, &matrix);
        }
        else
        {
            assert(data.gradientDetail);
            switch (data.gradientDetail->info.type)
            {
            case grad_linear:
            {
                fPoint endPts[2];
                calc_linear_endpoint(data.gradientDetail->info.angle, wid, hei, endPts);
                // Add offset to gradient points
                endPts[0].fX += x;
                endPts[0].fY += y;
                endPts[1].fX += x;
                endPts[1].fY += y;
                ret = cairo_pattern_create_linear(endPts[0].fX, endPts[0].fY, endPts[1].fX, endPts[1].fY);
            }
            break;
            case grad_radial:
            {
                float centerX = wid * data.gradientDetail->info.radial.centerX + x;
                float centerY = hei * data.gradientDetail->info.radial.centerY + y;
                ret = cairo_pattern_create_radial(centerX, centerY, 0, centerX, centerY, data.gradientDetail->info.radial.radius);
            }
            break;
            default:
                return nullptr;
            }
            for (uint32_t i = 0, cnt = data.gradientDetail->items.size(); i < cnt; i++)
            {
                CairoColor cr(data.gradientDetail->items[i].cr);
                cr.a *= alpha;
                cairo_pattern_add_color_stop_rgba(ret, data.gradientDetail->items[i].pos, cr.r, cr.g, cr.b, cr.a);
            }
        }
        if (!ret)
            return nullptr;
        cairo_extend_t mode = CAIRO_EXTEND_NONE;
        switch (tileMode)
        {
        case kTileMode_Clamp:
            mode = CAIRO_EXTEND_PAD;
            break;
        case kTileMode_Mirror:
            mode = CAIRO_EXTEND_REFLECT;
            break;
        case kTileMode_Repeat:
            mode = CAIRO_EXTEND_REPEAT;
            break;
        }
        cairo_pattern_set_extend(ret, mode);
        pattern = ret;
        this->width = wid;
        this->height = hei;
        this->x = x;
        this->y = y;
        return ret;
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

static bool ApplyBrush(HDC hdc, HBRUSH hbr, double wid, double hei, double x, double y);

static bool ApplyPen(HDC hdc, HPEN hpen, double wid, double hei, double x, double y)
{
    cairo_t *ctx = hdc->cairo;
    LOGPEN *pen = (LOGPEN *)GetGdiObjPtr(hpen);
    if (pen->lopnStyle == PS_NULL)
        return false;
    
    cairo_set_line_width(ctx, pen->lopnWidth.x);
    
    // Handle PS_PATTERN specially
    if ((pen->lopnStyle & PS_STYLE_MASK) == PS_USERSTYLE && (pen->lopnStyle & PS_TYPE_MASK) == PS_GEOMETRIC)
    {
        LOGPENEX *lpex = (LOGPENEX *)pen;
        if (lpex->patternBrush)
        {
            // Apply the pattern brush to get Cairo pattern
            // Use provided rectangle dimensions, or default to 1.0 if not provided
            double patternWidth = (wid > 0) ? wid : 1.0;
            double patternHeight = (hei > 0) ? hei : 1.0;
            if (!ApplyBrush(hdc, lpex->patternBrush, patternWidth, patternHeight, x, y))
            {
                // Fallback to solid color if pattern brush application fails
                CairoColor cr(pen->lopnColor);
                cairo_set_source_rgba(ctx, cr.r, cr.g, cr.b, cr.a);
            }
        }
        else
        {
            // Fallback to solid color if pattern brush is not available
            CairoColor cr(pen->lopnColor);
            cairo_set_source_rgba(ctx, cr.r, cr.g, cr.b, cr.a);
        }
    }
    else
    {
        // For non-pattern styles, use the pen color
        CairoColor cr(pen->lopnColor);
        cairo_set_source_rgba(ctx, cr.r, cr.g, cr.b, cr.a);
    }
    
    // Handle dash patterns for line style
    switch (pen->lopnStyle & PS_STYLE_MASK)
    {
    case PS_DASH:
    {
        static double dashes_dash[] = { 5.0, 5.0 };
        cairo_set_dash(ctx, dashes_dash, ARRAYSIZE(dashes_dash), 0.0);
    }
    break;
    case PS_DOT:
    {
        static double dashes_dot[] = { 1.0, 3.0 };
        cairo_set_dash(ctx, dashes_dot, ARRAYSIZE(dashes_dot), 0.0);
    }
    break;
    case PS_DASHDOT:
    {
        static double dashes_dashdot[] = { 5.0, 5.0, 1.0, 5.0 };
        cairo_set_dash(ctx, dashes_dashdot, ARRAYSIZE(dashes_dashdot), 0.0);
    }
    break;
    case PS_DASHDOTDOT:
    {
        static double dashes_dashdotdot[] = { 5.0, 5.0, 1.0, 5.0, 1.0, 5.0 };
        cairo_set_dash(ctx, dashes_dashdotdot, ARRAYSIZE(dashes_dashdotdot), 0.0);
    }
    break;
    case PS_SOLID:
    {
        cairo_set_dash(ctx, nullptr, 0, 0.0);
    }
    break;
    case PS_USERSTYLE:
    {
        LOGPENEX *lp = (LOGPENEX *)pen;
        cairo_set_dash(ctx, lp->dash.data(), lp->dash.size(), 0.0);
    }
    break;
    }
    cairo_set_line_cap(ctx, CAIRO_LINE_CAP_SQUARE);
    switch (pen->lopnStyle & PS_ENDCAP_MASK)
    {
    case PS_ENDCAP_ROUND:
        cairo_set_line_cap(ctx, CAIRO_LINE_CAP_ROUND);
        break;
    case PS_ENDCAP_SQUARE:
        cairo_set_line_cap(ctx, CAIRO_LINE_CAP_SQUARE);
        break;
    case PS_ENDCAP_FLAT:
        cairo_set_line_cap(ctx, CAIRO_LINE_CAP_BUTT);
        break;
    }
    cairo_set_line_join(ctx, CAIRO_LINE_JOIN_MITER);
    switch (pen->lopnStyle & PS_JOIN_MASK)
    {
    case PS_JOIN_ROUND:
        cairo_set_line_join(ctx, CAIRO_LINE_JOIN_ROUND);
        break;
    case PS_JOIN_BEVEL:
        cairo_set_line_join(ctx, CAIRO_LINE_JOIN_BEVEL);
        break;
    case PS_JOIN_MITER:
        cairo_set_line_join(ctx, CAIRO_LINE_JOIN_MITER);
        break;
    }
    return true;
}

// must call after ApplyPen, ApplyBrush
static void ApplyRop2(cairo_t *cr, int rop2)
{
    switch (rop2)
    {
    case R2_BLACK:
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        break;
    case R2_WHITE:
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        break;
    case R2_NOT:
    case DSTINVERT:
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        break;
    case R2_NOP:
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        break;
    case R2_COPYPEN:
    case SRCCOPY:
        // default, do nothing
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        break;
    case R2_EXT_OVER:
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        break;
    case R2_EXT_IN:
        cairo_set_operator(cr, CAIRO_OPERATOR_IN);
        break;
    case R2_EXT_OUT:
        cairo_set_operator(cr, CAIRO_OPERATOR_OUT);
        break;
    case R2_EXT_ATOP:
        cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
        break;
    case R2_EXT_DEST:
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST);
        break;
    case R2_EXT_DEST_OVER:
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
        break;
    case R2_EXT_DEST_IN:
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
        break;
    case R2_EXT_DEST_OUT:
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
        break;
    case R2_EXT_DEST_ATOP:
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_ATOP);
        break;
    case R2_EXT_XOR:
        cairo_set_operator(cr, CAIRO_OPERATOR_XOR);
        break;
    case SRCAND:
    case R2_EXT_ADD:
        cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
        break;
    case R2_EXT_SATURATE:
        cairo_set_operator(cr, CAIRO_OPERATOR_SATURATE);
        break;
    case SRCINVERT:
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        break;
    case R2_EXT_CLEAR:
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        break;
    default:
        printf("not supported rop2: %d\n", rop2);
        break;
    }
}

static bool ApplyBrush(HDC hdc, HBRUSH hbr, double wid, double hei, double x, double y)
{
    cairo_t *ctx = hdc->cairo;
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
    bool ret = true;
    switch (br->lbStyle)
    {
    case BS_SOLID:
    {
        CairoColor cr(br->lbColor);
        cairo_set_source_rgba(ctx, cr.r, cr.g, cr.b, cr.a);
    }
    break;
    case BS_PATTERN:
    {
        PatternInfo *info = (PatternInfo *)br->lbHatch;
        if(info->useBmp){
            x = -hdc->brushOrg.x;
            y = -hdc->brushOrg.y;
        }
        cairo_pattern_t *pattern = info->create(wid, hei, x, y);
        cairo_set_source(ctx, pattern);
        break;
    }
    default:
        ret = false;
    }
    return ret;
}

static void DrawPathFillStroke(cairo_t *ctx, HDC hdc, double wid, double hei, double x, double y)
{
    if (ApplyBrush(hdc, hdc->brush, wid, hei, x,y))
    {
        ApplyRop2(ctx, hdc->rop2);
        cairo_fill_preserve(ctx); // Preserve path for stroke
    }
    if (ApplyPen(hdc, hdc->pen, wid, hei, x, y))
    {
        ApplyRop2(ctx, hdc->rop2);
        cairo_stroke(ctx);
    }else{
        cairo_new_path(ctx); // Clear path if no stroke is applied
    }
}

static void DrawPathStroke(cairo_t *ctx, HDC hdc, double wid, double hei, double x, double y)
{
    if (ApplyPen(hdc, hdc->pen, wid, hei, x, y))
    {
        ApplyRop2(ctx, hdc->rop2);
        cairo_stroke(ctx);
    }else{
        cairo_new_path(ctx);
    }
}


static BOOL ApplyFont(HDC hdc)
{
    if (hdc->hfont)
    {
        LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
        cairo_t *cr = hdc->cairo;

        cairo_font_face_t *fontFace = SwinXGetCachedFontFace(lf);
        if (fontFace)
        {
            cairo_set_font_face(cr, fontFace);
            cairo_font_face_destroy(fontFace);
        }
        else
        {
            cairo_select_font_face(cr, "sans-serif", lf->lfItalic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, lf->lfWeight > 400 ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
        }
        cairo_set_font_size(cr, abs(lf->lfHeight));
        AttachFontFallback(cr, lf);
        return TRUE;
    }
    return FALSE;
}

static void ApplyRegion(cairo_t *ctx, HRGN hRgn)
{
    cairo_reset_clip(ctx);
    if (!hRgn)
        return; // NULL region: remove clipping entirely (SelectClipRgn(hdc,NULL))
    DWORD dwCount = GetRegionData(hRgn, 0, nullptr);
    RGNDATA *pData = (RGNDATA *)malloc(dwCount);
    GetRegionData(hRgn, dwCount, pData);
    RECT *pRc = (RECT *)pData->Buffer;
    for (int i = 0; i < (int)pData->rdh.nCount; i++)
    {
        cairo_rectangle(ctx, pRc->left, pRc->top, pRc->right - pRc->left, pRc->bottom - pRc->top);
        pRc++;
    }
    free(pData);
    cairo_clip(ctx);
}

// Clip coordinate-space contract of this backend (why the clip functions
// below work in the CURRENT LOGICAL space instead of raw device space):
//
// Real Win32 GDI keeps SelectClipRgn/ExtSelectClipRgn/GetClipRgn regions in
// raw device coordinates (verified empirically: with SetViewportOrgEx(30,50),
// SelectClipRgn((10,20,110,120)) clips at device (10,20,110,120) and
// GetClipRgn returns exactly that box, while GetClipBox reports the logical
// box (-20,-30,80,70)). SOUI's Windows render layer
// (components/render-gdi/win/render-gdi.cpp) compensates for that by
// offsetting PushClipRect/PushClipRegion with its mirrored m_ptOrg and
// offsetting GetClipRegion results back by -m_ptOrg.
//
// SOUI's Linux render layer (components/render-gdi/linux/render-gdi.cpp)
// performs NO such compensation — it has no m_ptOrg mirror. It therefore
// requires a backend whose clip API works in the current logical space:
// rects/regions pushed through ExtSelectClipRgn must be mapped through the
// active CTM (which update_transform composes from mtx + ptOrigin), and
// GetClipRgn/GetClipBox must report the clip in that same logical space.
// With an identity transform this coincides with Win32 device coordinates;
// with an active transform it equals what SOUI's Windows layer produces
// after its manual compensation. Getting this wrong blanks every
// org-offset item panel (mc list controls paint blank).

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
    if (h->type == OBJ_FONT)
    {
        if (c < (int)sizeof(LOGFONTW))
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
    if (!h->ptr)
        return 0;
    int ret = 0;
    switch (h->type)
    {
    case OBJ_BITMAP:
        if (c >= (int)sizeof(BITMAP))
        {
            BITMAP *bm = (BITMAP *)pv;
            cairo_surface_t *pixmap = (cairo_surface_t *)h->ptr;
            cairo_format_t fmt = cairo_image_surface_get_format(pixmap);
            if (fmt == CAIRO_FORMAT_INVALID)
                return 0;
            bm->bmWidth = cairo_image_surface_get_width(pixmap);
            bm->bmHeight = cairo_image_surface_get_height(pixmap);
            bm->bmPlanes = 1;
            switch (fmt)
            {
            case CAIRO_FORMAT_A1:
                bm->bmBitsPixel = 1;
                break;
            case CAIRO_FORMAT_ARGB32:
                bm->bmBitsPixel = 32;
                break;
            case CAIRO_FORMAT_RGB24:
                bm->bmBitsPixel = 24;
                break;
            default:
                assert(0);
                break;
            }

            bm->bmWidthBytes = ((bm->bmWidth * bm->bmBitsPixel) / 8 + 3) / 4 * 4;
            bm->bmType = BI_RGB;
            bm->bmBits = cairo_image_surface_get_data(pixmap);
            ret = sizeof(BITMAP);
        }
        break;
    case OBJ_FONT:
        if (c >= (int)sizeof(LOGFONTA))
        {
            ret = sizeof(LOGFONTA);
            memcpy(pv, h->ptr, ret);
        }
        break;
    case OBJ_PEN:
        if (c >= (int)sizeof(LOGPEN))
        {
            ret = sizeof(LOGPEN);
            memcpy(pv, h->ptr, ret);
        }
        break;
    case OBJ_BRUSH:
        if (c >= (int)sizeof(LOGBRUSH))
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
    assert((int)(plpen->lopnStyle & PS_STYLE_MASK) != PS_USERSTYLE); // PS_USERSTYLE should be created by ExtCreatePen
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

HFONT CreateFontW(int cHeight, int cWidth __attribute__((unused)), int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
    char facename[LF_FACESIZE];
    if (WideCharToMultiByte(CP_UTF8, 0, pszFaceName, -1, facename, LF_FACESIZE, nullptr, nullptr) == 0)
        return 0;
    return CreateFontA(cHeight, cWeight, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, facename);
}

HBITMAP CreateDIBitmap(HDC hdc, const BITMAPINFOHEADER *pbmih __attribute__((unused)), DWORD flInit __attribute__((unused)), const VOID *pjBits, const BITMAPINFO *pbmi, UINT iUsage)
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

HBRUSH CreateDIBPatternBrush(HGLOBAL h __attribute__((unused)), UINT iUsage __attribute__((unused)))
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

HBITMAP CreateDIBSection(HDC hdc __attribute__((unused)), const BITMAPINFO *lpbmi, UINT usage __attribute__((unused)), VOID **ppvBits, HANDLE hSection __attribute__((unused)), DWORD offset __attribute__((unused)))
{
    cairo_format_t fmt = CAIRO_FORMAT_INVALID;
    switch (lpbmi->bmiHeader.biBitCount)
    {
    case 1:
        fmt = CAIRO_FORMAT_A1;
        break;
    case 32:
        fmt = CAIRO_FORMAT_ARGB32;
        break;
    case 24:
        fmt = CAIRO_FORMAT_RGB24;
        break;
    }
    if (fmt == CAIRO_FORMAT_INVALID)
        return 0;
    cairo_surface_t *ret = cairo_image_surface_create(fmt, lpbmi->bmiHeader.biWidth, abs(lpbmi->bmiHeader.biHeight));
    if (!ret)
        return 0;
    cairo_status_t status = cairo_surface_status(ret);
    if (status != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(ret);
        return 0;
    }
    if (ppvBits)
    {
        *ppvBits = cairo_image_surface_get_data(ret);
    }
    return InitGdiObj(OBJ_BITMAP, ret);
}

HBITMAP CreateDIBSectionEx(int bitsPixel, int wid,int hei,int stride, VOID *pvBits){
    cairo_format_t fmt = CAIRO_FORMAT_INVALID;
    switch (bitsPixel)
    {
    case 1:
        fmt = CAIRO_FORMAT_A1;
        break;
    case 32:
        fmt = CAIRO_FORMAT_ARGB32;
        break;
    case 24:
        fmt = CAIRO_FORMAT_RGB24;
        break;
    }
    if (fmt == CAIRO_FORMAT_INVALID)
        return 0;
    cairo_surface_t *ret = cairo_image_surface_create_for_data((unsigned char*)pvBits, fmt, wid,hei, stride);
    if (!ret)
        return 0;
    cairo_status_t status = cairo_surface_status(ret);
    if (status != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(ret);
        return 0;
    }
    cairo_surface_mark_dirty(ret);
    return InitGdiObj(OBJ_BITMAP, ret);
}


BOOL UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits)
{
    BITMAP bm = {};
    GetObject(bmp, sizeof(bm), &bm);
    if (!bm.bmBits)
        return FALSE;
    if (bm.bmWidth != wid || bm.bmHeight != hei || bm.bmBitsPixel != bitsPixel)
        return FALSE;
    int surfaceStride = cairo_image_surface_get_stride((cairo_surface_t *)GetGdiObjPtr(bmp));
    if (pjBits)
    {
        if (stride == surfaceStride)
            memcpy(bm.bmBits, pjBits, hei * stride);
        else
        {
            char *src = (char *)pjBits;
            char *dst = (char *)bm.bmBits;
            int fmt = cairo_image_surface_get_format((cairo_surface_t *)GetGdiObjPtr(bmp));
            if (bitsPixel == 24 && fmt == CAIRO_FORMAT_RGB24)
            {
                for (int i = 0; i < hei; i++)
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
            else if (bitsPixel == 1 && fmt == CAIRO_FORMAT_A1)
            {
                for (int y = 0; y < hei; y++)
                {
                    for (int x = 0; x < wid; x++)
                    {
                        int bitmap_index = (y * ((wid + 7) / 8) + (x / 8));
                        int cairo_index = (y * surfaceStride + (x / 8));
                        uint8_t bitmap_bit = (src[bitmap_index] >> (7 - (x % 8))) & 1;

                        if (bitmap_bit)
                        {
                            dst[cairo_index] |= (1 << (7 - (x % 8)));
                        }
                        else
                        {
                            dst[cairo_index] &= ~(1 << (7 - (x % 8)));
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
        memset(bm.bmBits, 0, hei * surfaceStride);
    MarkPixmapDirty(bmp);
    return TRUE;
}

void MarkPixmapDirty(HBITMAP bmp)
{
    if (bmp && bmp->type == OBJ_BITMAP)
    {
        cairo_surface_mark_dirty((cairo_surface_t *)bmp->ptr);
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

int SetGraphicsMode(HDC hdc __attribute__((unused)), int iMode __attribute__((unused)))
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
        // recreate cairo_t object
        ret = hdc->bmp;
        cairo_antialias_t antialis = CAIRO_ANTIALIAS_GOOD;
        if (hdc->cairo)
        {
            antialis = cairo_get_antialias(hdc->cairo);
            cairo_destroy(hdc->cairo);
            hdc->cairo = nullptr;
        }
        hdc->bmp = h;
        if (GetGdiObjPtr(h))
        {
            hdc->cairo = cairo_create((cairo_surface_t *)GetGdiObjPtr(h));
            cairo_set_antialias(hdc->cairo, antialis);
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
    cairo_rectangle_list_t *rcList = cairo_copy_clip_rectangle_list(hdc->cairo);
    if (rcList->status == CAIRO_STATUS_CLIP_NOT_REPRESENTABLE)
    { // no clip set on the context (unbounded clip): Win32 GetClipRgn
      // reports "no clipping region" by returning 0
        cairo_rectangle_list_destroy(rcList);
        return 0;
    }
    if (rcList->status != CAIRO_STATUS_SUCCESS)
        return -1;
    int size = FIELD_OFFSET(RGNDATA, Buffer) + rcList->num_rectangles * sizeof(RECT);
    RGNDATA *pRgnData = (RGNDATA *)malloc(size);
    pRgnData->rdh.dwSize = size;
    pRgnData->rdh.iType = RDH_RECTANGLES;
    pRgnData->rdh.nCount = rcList->num_rectangles;

    // cairo reports the clip in user (logical) coordinates; export it
    // unchanged so callers receive the clip in the current logical space
    // (see the clip semantics note above ApplyRegion)
    RECT *pRc = (RECT *)pRgnData->Buffer;
    cairo_rectangle_t *pRcSrc = rcList->rectangles;
    for (int i = 0; i < rcList->num_rectangles; i++)
    {
        pRc->left = pRcSrc->x;
        pRc->top = pRcSrc->y;
        pRc->right = pRc->left + pRcSrc->width;
        pRc->bottom = pRc->top + pRcSrc->height;
        pRc++;
        pRcSrc++;
    }
    HRGN rgnSrc = ExtCreateRegion(NULL, pRgnData->rdh.nCount, pRgnData);
    free(pRgnData);
    cairo_rectangle_list_destroy(rcList);
    CombineRgn(hrgn, rgnSrc, nullptr, RGN_COPY);
    DeleteObject(rgnSrc);
    return RgnComplexity(hrgn) == NULLREGION ? 0 : 1;
}

int SelectClipRgn(HDC hdc, HRGN hrgn)
{
    // apply with the active CTM so the region is interpreted in the current
    // logical space (see the clip semantics note above ApplyRegion)
    ApplyRegion(hdc->cairo, hrgn);
    return RgnComplexity(hrgn);
}

int ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode)
{
    if (mode == RGN_COPY)
    {
        ApplyRegion(hdc->cairo, hrgn);
        return 0;
    }
    // combine in the current logical space: GetClipRgn exports the clip in
    // user coordinates and ApplyRegion maps it back through the CTM (see the
    // clip semantics note above ApplyRegion)
    HRGN rgnNow = CreateRectRgn(0, 0, 0, 0);
    // probe the raw clip state first: CLIP_NOT_REPRESENTABLE means "no clip
    // set" (unbounded), while a bounded-but-empty clip must stay empty after
    // the combine; GetClipRgn maps both to 0 and must be told apart here
    cairo_rectangle_list_t *rcList = cairo_copy_clip_rectangle_list(hdc->cairo);
    BOOL bNoClip = (rcList->status == CAIRO_STATUS_CLIP_NOT_REPRESENTABLE);
    cairo_rectangle_list_destroy(rcList);
    GetClipRgn(hdc, rgnNow);
    int ret;
    if (bNoClip && mode == RGN_AND)
    { // no current clip: intersecting with it is a no-op, so the result is
      // just hrgn itself (matches Win32)
        ret = RgnComplexity(hrgn);
        ApplyRegion(hdc->cairo, hrgn);
    }
    else
    {
        ret = CombineRgn(rgnNow, rgnNow, hrgn, mode);
        ApplyRegion(hdc->cairo, rgnNow);
    }
    DeleteObject(rgnNow);
    return ret;
}

int ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    // the rect is in logical coordinates; ExtSelectClipRgn applies it under
    // the active CTM, matching Win32 where Intersect/ExcludeClipRect take
    // logical units
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

int GetDIBits(HDC hdc __attribute__((unused)), HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage __attribute__((unused)))
{
    if (!hbm || !lpbmi)
        return 0;
    BITMAP bm = {};
    if (!GetObject(hbm, sizeof(bm), &bm))
        return 0;
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(hbm);
    cairo_format_t fmt = cairo_image_surface_get_format(surface);
    // only 4-byte-per-pixel formats are supported for conversion
    if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_RGB24)
        return 0;
    const unsigned char *data = cairo_image_surface_get_data(surface);
    if (!data)
        return 0;
    int wid = bm.bmWidth;
    int hei = bm.bmHeight;
    int srcStride = cairo_image_surface_get_stride(surface);

    if (!lpvBits)
    {
        // query mode: fill the header describing the bitmap
        BITMAPINFOHEADER &h = lpbmi->bmiHeader;
        h.biSize = sizeof(BITMAPINFOHEADER);
        h.biWidth = wid;
        h.biHeight = hei; // positive => bottom-up, like a Win32 DDB
        h.biPlanes = 1;
        h.biBitCount = bm.bmBitsPixel;
        h.biCompression = BI_RGB;
        h.biSizeImage = ((wid * h.biBitCount / 8) + 3) / 4 * 4 * hei;
        h.biXPelsPerMeter = 0;
        h.biYPelsPerMeter = 0;
        h.biClrUsed = 0;
        h.biClrImportant = 0;
        return 1;
    }

    // copy mode: convert into the caller-requested format
    int outBpp = lpbmi->bmiHeader.biBitCount;
    if (outBpp != 24 && outBpp != 32)
        return 0;
    bool topDown = lpbmi->bmiHeader.biHeight < 0;
    if (start >= (UINT)hei)
        return 0;
    UINT lines = (cLines < (UINT)(hei - start)) ? cLines : (UINT)(hei - start);
    int outStride = ((wid * outBpp / 8) + 3) / 4 * 4;
    unsigned char *dstBase = (unsigned char *)lpvBits;
    for (UINT j = 0; j < lines; j++)
    {
        // scan lines are counted from the bottom of a bottom-up DIB
        int srcRow = topDown ? (int)(start + j) : (hei - 1 - (int)(start + j));
        const unsigned char *src = data + (size_t)srcRow * srcStride;
        unsigned char *dst = dstBase + (size_t)j * outStride;
        // internal storage of both CAIRO_FORMAT_ARGB32 and CAIRO_FORMAT_RGB24
        // is 4-byte premultiplied BGRA
        if (outBpp == 32)
        {
            memcpy(dst, src, (size_t)wid * 4);
        }
        else
        {
            for (int x = 0; x < wid; x++)
            {
                dst[x * 3 + 0] = src[x * 4 + 0];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }
    }
    return (int)lines;
}

// 检查矩阵是否是单位矩阵
static int matrix_is_identity(const cairo_matrix_t *matrix)
{
    static cairo_matrix_t identity_matrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
    return matrix->xx == identity_matrix.xx && matrix->xy == identity_matrix.xy && matrix->yy == identity_matrix.yy && matrix->yx == identity_matrix.yx && matrix->x0 == identity_matrix.x0 && matrix->y0 == identity_matrix.y0;
}

BOOL InvertRgn(HDC hdc, HRGN hrgn)
{
    if (!hrgn)
        return FALSE;
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    cairo_set_source_rgb(ctx, 1.0, 1.0, 1.0);
    cairo_set_operator(ctx, CAIRO_OPERATOR_DIFFERENCE);
    cairo_rectangle(ctx, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    cairo_fill(ctx);

    cairo_restore(ctx);
    return TRUE;
}

int GetClipBox(HDC hdc, LPRECT lprect)
{
    double x1, y1, x2, y2;
    cairo_clip_extents(hdc->cairo, &x1, &y1, &x2, &y2);
    // cairo reports the clip extents in user (logical) coordinates; Win32
    // GetClipBox also returns logical coordinates, so no transform is needed
    // (see the clip semantics note above ApplyRegion)
    lprect->left = (int)floor((x1 < x2) ? x1 : x2);
    lprect->top = (int)floor((y1 < y2) ? y1 : y2);
    lprect->right = (int)ceil((x1 < x2) ? x2 : x1);
    lprect->bottom = (int)ceil((y1 < y2) ? y2 : y1);
    if (IsRectEmpty(lprect))
        return NULLREGION;
    return COMPLEXREGION;
}

BOOL FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)
{
    if (!hrgn || GetObjectType(hrgn) != OBJ_REGION)
        return FALSE;
    BOOL ret = FALSE;
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    double wid = rc.right - rc.left, hei = rc.bottom - rc.top;
    if (ApplyBrush(hdc, hbr, wid, hei, rc.left, rc.top))
    {
        ApplyRop2(ctx, hdc->rop2);
        cairo_rectangle(ctx, rc.left, rc.top, wid, hei);
        cairo_fill(ctx);
        ret = TRUE;
    }
    cairo_restore(ctx);
    return ret;
}

BOOL FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr __attribute__((unused)), int nWidth __attribute__((unused)), int nHeight __attribute__((unused)))
{
    if (!hrgn || GetObjectType(hrgn) != OBJ_REGION)
        return FALSE;
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    ApplyRegion(ctx, hrgn);
    RECT rc;
    GetRgnBox(hrgn, &rc);
    double rgn_wid = rc.right - rc.left, rgn_hei = rc.bottom - rc.top;
    ApplyPen(hdc, hdc->pen, rgn_wid, rgn_hei, rc.left, rc.top);
    ApplyRop2(ctx, hdc->rop2);
    cairo_stroke(ctx);
    cairo_restore(ctx);
    return TRUE;
}

BOOL WINAPI DrawFocusRect(HDC hdc,       // handle to device context
                          CONST RECT *rc // logical coordinates
)
{
    HBRUSH hOldBrush;
    HPEN hOldPen, hNewPen;
    INT oldDrawMode;
    cairo_antialias_t oldAntialias = cairo_get_antialias(hdc->cairo);
    cairo_set_antialias(hdc->cairo, CAIRO_ANTIALIAS_NONE);
    hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    hNewPen = CreatePen(PS_DOT, 1, 0);
    hOldPen = SelectObject(hdc, hNewPen);
    oldDrawMode = SetROP2(hdc, R2_NOT);
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SetROP2(hdc, oldDrawMode);
    SelectObject(hdc, hOldPen);
    DeleteObject(hNewPen);
    SelectObject(hdc, hOldBrush);
    cairo_set_antialias(hdc->cairo, oldAntialias);
    return TRUE;
}

BOOL PaintRgn(HDC hdc, HRGN hrgn)
{
    return FillRgn(hdc, hrgn, hdc->brush);
}

BOOL AlphaBlend(HDC hdc, int x, int y, int wDst, int hDst, HDC hdcSrc, int x1, int y1, int wSrc, int hSrc, BLENDFUNCTION ftn)
{
    assert(hdc && hdcSrc);
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    cairo_save(hdc->cairo);

    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo, &mtxSrc);
    x1 += mtxSrc.x0;
    y1 += mtxSrc.y0;

    cairo_rectangle(hdc->cairo, x, y, wDst, hDst);
    cairo_clip(hdc->cairo);

    cairo_translate(hdc->cairo, x, y);
    double scale_x = wDst * 1.0 / wSrc;
    double scale_y = hDst * 1.0 / hSrc;
    cairo_scale(hdc->cairo, scale_x, scale_y);

    cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
    ApplyRop2(hdc->cairo, hdc->rop2);
    if (ftn.SourceConstantAlpha != 255)
        cairo_paint_with_alpha(hdc->cairo, ftn.SourceConstantAlpha * 1.0 / 255.0);
    else
        cairo_paint(hdc->cairo);

    cairo_restore(hdc->cairo);
    return TRUE;
}

static BOOL AlphaBlendEx(HDC hdc, int x, int y, int wDst, int hDst, cairo_surface_t *src, int x1, int y1, int wSrc, int hSrc, BLENDFUNCTION ftn, int filterLevel)
{
    assert(hdc);
    cairo_save(hdc->cairo);

    cairo_rectangle(hdc->cairo, x, y, wDst, hDst);
    cairo_clip(hdc->cairo);

    cairo_translate(hdc->cairo, x, y);
    double scale_x = wDst * 1.0 / wSrc;
    double scale_y = hDst * 1.0 / hSrc;
    cairo_scale(hdc->cairo, scale_x, scale_y);

    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(src);
    cairo_filter_t filter;
    switch (filterLevel)
    {
    case FILTER_FAST:
        filter = CAIRO_FILTER_BEST;
        break;
    case FILTER_MIDIUM:
        filter = CAIRO_FILTER_GOOD;
        break;
    case FILTER_BEST:
        filter = CAIRO_FILTER_BILINEAR;
        break;
    default:
        filter = CAIRO_FILTER_FAST;
        break;
    }
    cairo_pattern_set_filter(pattern, filter);
    cairo_matrix_t mtx;
    cairo_matrix_init_translate(&mtx, x1, y1);
    cairo_pattern_set_matrix(pattern, &mtx);
    cairo_set_source(hdc->cairo, pattern);

    ApplyRop2(hdc->cairo, hdc->rop2);
    if (ftn.SourceConstantAlpha != 255)
        cairo_paint_with_alpha(hdc->cairo, ftn.SourceConstantAlpha * 1.0 / 255.0);
    else
        cairo_paint(hdc->cairo);
    cairo_pattern_destroy(pattern);
    cairo_restore(hdc->cairo);
    return TRUE;
}

BOOL DrawBitmapEx(HDC hdc, LPCRECT pRcDest, HBITMAP bmp, LPCRECT pRcSrc, UINT expendMode, BYTE byAlpha /*=0xFF*/)
{
    if (!bmp)
        return FALSE;
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(bmp);
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

static cairo_surface_t *cairo_surface_create_copy(cairo_surface_t *src)
{
    // create an copy of src
    int bmpWid = cairo_image_surface_get_width(src);
    int bmpHei = cairo_image_surface_get_height(src);
    cairo_format_t fmt = cairo_image_surface_get_format(src);
    cairo_surface_t *srcCpy = cairo_surface_create_similar_image(src, fmt, bmpWid, bmpHei);
    cairo_t *cr = cairo_create(srcCpy);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    return srcCpy;
}

#ifdef __OHOS__
static void markDcDirty(HDC hdc, int x, int y, int cx, int cy)
{
    if (!hdc || cx <= 0 || cy <= 0)
        return;
    RECT rc = { x, y, x + cx, y + cy };
    if (!hdc->hasDirty)
    {
        hdc->dirtyRect = rc;
        hdc->hasDirty = TRUE;
    }
    else
    {
        UnionRect(&hdc->dirtyRect, &hdc->dirtyRect, &rc);
    }
}
#endif

/* GDI 逐位光栅操作（SRCAND=dest&src / SRCPAINT=dest|src / SRCINVERT=dest^src）。
 * cairo 没有按位合成算子，OVER/DEST_IN/DIFFERENCE 只是 alpha 混合的近似——
 * 不透明源下 SRCPAINT 退化为 SRCCOPY、SRCAND 退化为无操作、SRCINVERT 给出
 * |d-s| 而非 d^s，与 Windows 效果不符（drawgroup11 实测），必须逐像素运算。
 *
 * 存储布局：ARGB32/RGB24 均为 4B/px premultiplied BGRA（RGB24 末字节无意义），
 * 所以 bpp 恒为 4。逐位运算作用于 B/G/R 三个字节；第 4 字节保留目标原值——
 * 不透明像素（alpha=FF）上 premultiplied==straight，逐位结果与真 GDI 一致，
 * 且避免 SRCINVERT 把 alpha 异或成 0 在 ARGB32 目标上凿出透明洞。
 *
 * surface 访问：image surface 直接读写 data；XCB（窗口 surface）等其它类型
 * 用 cairo_surface_map_to_image 映射目标/源矩形（X11 GetImage/PutImage），
 * unmap 时写回。坐标均为位图像素坐标（swinx 不使用 device_scale/offset，
 * 设备空间即像素空间）；目标矩形经目标 DC 当前变换取轴对齐包围盒（平移/
 * 缩放精确，旋转场景 GDI BitBlt 本身亦未定义）。源越界像素跳过（保留目标，
 * GDI 裁剪语义）。srcSpanX/srcSpanY 为源矩形跨度（BitBlt 传 cx/cy，
 * StretchBlt 传 cx2/cy2）。 */
/* 任意 surface（image 内存位图 / XCB 窗口 surface）的尺寸查询。
 *
 * vendored cairo 的 cairo-xcb.h 只有 create/set_size，没有尺寸 getter；
 * 唯一能取到 xcb surface 尺寸的后端方法是私有的 _cairo_xcb_surface_get_extents
 * （cairo-xcb-surface.c，挂在 backend->get_extents 上）。没有直接导出的
 * 公开 extents 查询，但公开 API cairo_clip_extents（since 1.4）的调用链
 * 正好覆盖它：
 *   cairo_clip_extents → _cairo_gstate_clip_extents →
 *   _cairo_gstate_int_clip_extents → _cairo_surface_get_extents（内部）→
 *   surface->backend->get_extents = _cairo_xcb_surface_get_extents。
 * 在一个**全新的 cairo context**（无 clip、CTM 恒等、无 device 变换）上，
 * clip extents 就是 surface extents 本身：(0,0,w,h)，单位即像素。
 * image surface 同样走这条链（后端 get_extents 返回位图尺寸），所以
 * 两种 surface 类型统一处理，无需分支、无需在别处记录尺寸。 */
static void surfSize(cairo_surface_t *s, int *w, int *h)
{
    *w = *h = 0;
    if (!s)
        return;
    cairo_t *cr = cairo_create(s);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS)
    {
        cairo_destroy(cr);
        return;
    }
    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    cairo_destroy(cr);
    /* 有效性守卫：有界 surface 的 extents 恒为 (0,0,w,h)。无界 surface
     * （recording 等）返回 ±INFINITY，直接强转 int 是 UB，必须先判。 */
    if (!(x1 == 0.0 && y1 == 0.0 && x2 > x1 && y2 > y1 &&
          x2 <= 2147483647.0 && y2 <= 2147483647.0))
        return;
    *w = (int)(x2 - x1);
    *h = (int)(y2 - y1);
}

static BOOL BitBltRasterOp(HDC hdcDst, int x, int y, int cx, int cy,
                           cairo_surface_t *srcSurf, int sx, int sy,
                           int srcSpanX, int srcSpanY, DWORD rop)
{
    if (cx <= 0 || cy <= 0 || srcSpanX <= 0 || srcSpanY <= 0)
        return FALSE;
    cairo_surface_t *dstSurf = (cairo_surface_t *)GetGdiObjPtr(hdcDst->bmp);
    if (!dstSurf && hdcDst->cairo)
        dstSurf = cairo_get_target(hdcDst->cairo); /* DC 上无选入位图时用绘制目标 */
    if (!dstSurf || !srcSurf)
        return FALSE;

    /* 目标 DC 用户矩形 -> 像素矩形：两角经当前变换，取轴对齐包围盒 */
    cairo_matrix_t mtx;
    cairo_get_matrix(hdcDst->cairo, &mtx);
    double ax = (double)x * mtx.xx + (double)y * mtx.xy + mtx.x0;
    double ay = (double)x * mtx.yx + (double)y * mtx.yy + mtx.y0;
    double bx = (double)(x + cx) * mtx.xx + (double)(y + cy) * mtx.xy + mtx.x0;
    double by = (double)(x + cx) * mtx.yx + (double)(y + cy) * mtx.yy + mtx.y0;
    int px = (int)floor(ax < bx ? ax : bx);
    int py = (int)floor(ay < by ? ay : by);
    int pw = (int)ceil(ax < bx ? bx : ax) - px;
    int ph = (int)ceil(ay < by ? by : ay) - py;
    if (pw <= 0 || ph <= 0)
        return TRUE; /* 目标矩形为空：无事可做 */

    /* 目标访问：image surface 直接取 data；其它类型 map_to_image，且仅映射
     * 与 surface 相交的目标矩形（map 的 extents 必须落在 surface 内） */
    int dw = 0, dh = 0;
    surfSize(dstSurf, &dw, &dh);
    int ex0 = px < 0 ? 0 : px, ey0 = py < 0 ? 0 : py;
    int ex1 = px + pw > dw ? dw : px + pw;
    int ey1 = py + ph > dh ? dh : py + ph;
    if (dw <= 0 || dh <= 0 || ex0 >= ex1 || ey0 >= ey1)
        return TRUE; /* 未知类型或与目标 surface 无交集 */
    cairo_surface_t *dmap = nullptr;
    unsigned char *ddata = nullptr;
    int dstride = 0;
    int dvx0 = 0, dvy0 = 0, dvw = 0, dvh = 0; /* 目标视图原点/尺寸（像素） */
    if (cairo_surface_get_type(dstSurf) == CAIRO_SURFACE_TYPE_IMAGE)
    {
        cairo_surface_flush(dstSurf);
        ddata = cairo_image_surface_get_data(dstSurf);
        if (!ddata)
            return FALSE;
        dstride = cairo_image_surface_get_stride(dstSurf);
        dvx0 = 0;
        dvy0 = 0;
        dvw = dw;
        dvh = dh;
    }
    else
    {
        cairo_rectangle_int_t dext = {ex0, ey0, ex1 - ex0, ey1 - ey0};
        cairo_surface_flush(dstSurf);
        dmap = cairo_surface_map_to_image(dstSurf, &dext);
        if (!dmap || cairo_surface_status(dmap) != CAIRO_STATUS_SUCCESS)
        {
            /* map 返回的错误 surface 也允许传给 unmap（其内部会 destroy） */
            if (dmap)
                cairo_surface_unmap_image(dstSurf, dmap);
            return FALSE;
        }
        cairo_format_t dfmt = cairo_image_surface_get_format(dmap);
        if (dfmt != CAIRO_FORMAT_ARGB32 && dfmt != CAIRO_FORMAT_RGB24)
        {
            /* 注意：map_to_image 返回的 image 由 unmap_image 负责销毁
             * （_cairo_surface_unmap_image 末尾即 cairo_surface_destroy），
             * 这里绝不能再 destroy，否则引用计数减穿触发断言。 */
            cairo_surface_unmap_image(dstSurf, dmap);
            return FALSE;
        }
        ddata = cairo_image_surface_get_data(dmap);
        dstride = cairo_image_surface_get_stride(dmap);
        if (!ddata)
        {
            cairo_surface_unmap_image(dstSurf, dmap);
            return FALSE;
        }
        dvx0 = dext.x;
        dvy0 = dext.y;
        dvw = dext.width;
        dvh = dext.height;
    }

    /* 源访问视图：与目标同一 surface 时复用其视图（同一块像素内存）；
     * 否则 image surface 全图、其它类型仅映射源矩形与 surface 的交集。
     * 统一约定：视图内像素 (gx,gy) 的地址 = sdata + (gy-viewY0)*sstride
     * + (gx-viewX0)*4，越界即跳过。 */
    int sw = 0, sh = 0;
    surfSize(srcSurf, &sw, &sh);
    const bool sameSurf = (dstSurf == srcSurf);
    cairo_surface_t *smap = nullptr;
    const unsigned char *sdata = nullptr;
    int sstride = 0;
    int svx0 = 0, svy0 = 0, svw = 0, svh = 0; /* 源视图原点/尺寸（像素） */
    if (sameSurf)
    {
        sdata = ddata;
        sstride = dstride;
        svx0 = dvx0;
        svy0 = dvy0;
        svw = dvw;
        svh = dvh;
    }
    else if (cairo_surface_get_type(srcSurf) == CAIRO_SURFACE_TYPE_IMAGE)
    {
        cairo_surface_flush(srcSurf);
        sdata = cairo_image_surface_get_data(srcSurf);
        if (!sdata)
        {
            if (dmap)
            {
                cairo_surface_unmap_image(dstSurf, dmap);
            }
            return FALSE;
        }
        sstride = cairo_image_surface_get_stride(srcSurf);
        svw = sw;
        svh = sh;
    }
    else
    {
        int sx0 = sx < 0 ? 0 : sx, sy0 = sy < 0 ? 0 : sy;
        int sx1 = sx + srcSpanX > sw ? sw : sx + srcSpanX;
        int sy1 = sy + srcSpanY > sh ? sh : sy + srcSpanY;
        if (sx0 < sx1 && sy0 < sy1)
        {
            cairo_rectangle_int_t sext = {sx0, sy0, sx1 - sx0, sy1 - sy0};
            cairo_surface_flush(srcSurf);
            smap = cairo_surface_map_to_image(srcSurf, &sext);
            if (smap && cairo_surface_status(smap) == CAIRO_STATUS_SUCCESS)
            {
                cairo_format_t sfmt = cairo_image_surface_get_format(smap);
                if (sfmt == CAIRO_FORMAT_ARGB32 || sfmt == CAIRO_FORMAT_RGB24)
                {
                    sdata = cairo_image_surface_get_data(smap);
                    sstride = cairo_image_surface_get_stride(smap);
                    svx0 = sext.x;
                    svy0 = sext.y;
                    svw = sext.width;
                    svh = sext.height;
                }
            }
            if (!sdata)
            {
                if (smap)
                {
                    cairo_surface_unmap_image(srcSurf, smap);
                    smap = nullptr;
                }
                if (dmap)
                {
                    cairo_surface_unmap_image(dstSurf, dmap);
                }
                return FALSE;
            }
        }
        else
        {
            /* 源矩形与 surface 无交集：全部越界，保留目标即可 */
            sdata = nullptr;
        }
    }

    /* dst 与 src 是同一块像素内存（同一 surface）时，先把源矩形快照出来，
     * 避免读写重叠互相破坏（真实 GDI 对重叠 blit 亦按"先读后写"处理）。
     * 快照是紧凑缓冲（行宽 cw*4），本身成为一个新视图。 */
    swinx_stl::vector<unsigned char> snap;
    if (sameSurf)
    {
        int cx0 = sx > svx0 ? sx : svx0;
        int cy0 = sy > svy0 ? sy : svy0;
        int cx1 = sx + srcSpanX < svx0 + svw ? sx + srcSpanX : svx0 + svw;
        int cy1 = sy + srcSpanY < svy0 + svh ? sy + srcSpanY : svy0 + svh;
        int cw = cx1 > cx0 ? cx1 - cx0 : 0;
        int ch = cy1 > cy0 ? cy1 - cy0 : 0;
        if (cw > 0 && ch > 0)
        {
            snap.resize((size_t)cw * ch * 4);
            for (int r = 0; r < ch; r++)
                memcpy(&snap[(size_t)r * cw * 4],
                       sdata + (size_t)(cy0 + r - svy0) * sstride + (size_t)(cx0 - svx0) * 4,
                       (size_t)cw * 4);
            sdata = &snap[0];
            sstride = cw * 4;
            svx0 = cx0;
            svy0 = cy0;
            svw = cw;
            svh = ch;
        }
        else
            sdata = nullptr; /* 源矩形完全越界 */
    }

    for (int j = 0; j < ph; j++)
    {
        int dy = py + j; /* 目标全局像素坐标 */
        if (dy - dvy0 < 0 || dy - dvy0 >= dvh)
            continue;
        unsigned char *drow = ddata + (size_t)(dy - dvy0) * dstride;
        int syj = sy + (int)((double)j * srcSpanY / ph); /* 源全局像素坐标 */
        const unsigned char *srow = (sdata && syj >= svy0 && syj - svy0 < svh)
                                        ? sdata + (size_t)(syj - svy0) * sstride
                                        : nullptr;
        for (int i = 0; i < pw; i++)
        {
            int dx = px + i;
            if (dx - dvx0 < 0 || dx - dvx0 >= dvw || !srow)
                continue;
            int sxi = sx + (int)((double)i * srcSpanX / pw);
            if (sxi < svx0 || sxi - svx0 >= svw)
                continue; /* 源位图之外：保留目标 */
            unsigned char *d = drow + (size_t)(dx - dvx0) * 4;
            const unsigned char *s = srow + (size_t)(sxi - svx0) * 4;
            switch (rop)
            {
            case SRCAND:
                d[0] &= s[0]; d[1] &= s[1]; d[2] &= s[2];
                break;
            case SRCPAINT:
                d[0] |= s[0]; d[1] |= s[1]; d[2] |= s[2];
                break;
            case SRCINVERT:
                d[0] ^= s[0]; d[1] ^= s[1]; d[2] ^= s[2];
                break;
            }
        }
    }
    if (smap)
    {
        /* unmap_image 内部会把 image surface 销毁并写回源 surface */
        cairo_surface_unmap_image(srcSurf, smap);
    }
    if (dmap)
    {
        /* 我们是直接写映射内存的，不经 cairo 绘制调用，image 的 serial 仍为
         * 0；unmap 靠 serial 判断"图像未被改动"并跳过写回。必须先 mark_dirty
         * 把 serial 顶上去，否则本次写入会被静默丢弃（表现为目标色块原样）。 */
        cairo_surface_mark_dirty(dmap);
        cairo_surface_unmap_image(dstSurf, dmap); /* unmap 时写回目标 surface */
    }
    else
        cairo_surface_mark_dirty(dstSurf);
    return TRUE;
}

BOOL BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    assert(hdc && hdcSrc);
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    if (hdc == hdcSrc)
    {
        // create an copy of src
        src = cairo_surface_create_copy(src);
    }

    cairo_save(hdc->cairo);
    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo, &mtxSrc);
    x1 += mtxSrc.x0;
    y1 += mtxSrc.y0;

    /* SRCAND/SRCPAINT/SRCINVERT 是逐位运算，cairo 算子无法表达——走逐像素
     * 路径；helper 无法处理的目标（无位图/ exotic 格式）回退旧的近似混合。
     * 注意 helper 必须在 clip/translate 之前调用：它内部按当前矩阵折算目标
     * 矩形，而下面的 translate 只是 fill 路径的绘制手段。 */
    BOOL rasterDone = FALSE;
    if (rop == SRCAND || rop == SRCPAINT || rop == SRCINVERT)
        rasterDone = BitBltRasterOp(hdc, x, y, cx, cy, src, x1, y1, cx, cy, rop);

    /* set_source_surface 在调用时刻按当前 CTM 固定 pattern 位置（实测：之后
     * 的 cairo_translate 不会移动已设置的 surface 源），必须保持在
     * clip/translate 之后调用，顺序不能提前——否则贴图整体偏移 (x,y)。 */
    cairo_rectangle(hdc->cairo, x, y, cx, cy);
    cairo_clip(hdc->cairo);
    cairo_translate(hdc->cairo, x, y);
    switch (rop)
    {
    case SRCCOPY:
        cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_SOURCE);
        break;
    case SRCINVERT:
        if (!rasterDone)
        {
            cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
            cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        }
        break;
    case SRCPAINT:
        if (!rasterDone)
        {
            cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
            cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_OVER);
        }
        break;
    case SRCAND:
        if (!rasterDone)
        {
            cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
            cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DEST_IN);
        }
        break;
    case DSTINVERT:
        cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    }
    if (!rasterDone)
    {
        cairo_rectangle(hdc->cairo, 0, 0, cx, cy);
        cairo_fill(hdc->cairo);
    }
    cairo_restore(hdc->cairo);
    if (hdc == hdcSrc)
    {
        // destroy surface copy
        cairo_surface_destroy(src);
    }
#ifdef __OHOS__
    markDcDirty(hdc, x, y, cx, cy);
#endif
    return TRUE;
}

BOOL StretchBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, int cx2, int cy2, DWORD rop)
{
    assert(hdc && hdcSrc);
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    if (hdc == hdcSrc)
    {
        // create an copy of src
        src = cairo_surface_create_copy(src);
    }
    cairo_save(hdc->cairo);
    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo, &mtxSrc);
    x1 += mtxSrc.x0;
    y1 += mtxSrc.y0;

    /* SRCAND/SRCPAINT/SRCINVERT 逐位运算走逐像素路径（见 BitBltRasterOp）；
     * 源跨度为负（镜像）时 helper 不支持，回退近似混合路径。注意必须在
     * 下面 translate/scale 之前调用——helper 内部按当前矩阵折算目标矩形，
     * 此处的 cairo_translate/cairo_scale 只是 fill 路径的绘制手段。 */
    BOOL rasterDone = FALSE;
    if (cx2 > 0 && cy2 > 0 && (rop == SRCAND || rop == SRCPAINT || rop == SRCINVERT))
        rasterDone = BitBltRasterOp(hdc, x, y, cx, cy, src, x1, y1, cx2, cy2, rop);

    cairo_rectangle(hdc->cairo, x, y, cx, cy);
    cairo_clip(hdc->cairo);
    cairo_rectangle(hdc->cairo, x, y, cx, cy);
    cairo_translate(hdc->cairo, x + cx / 2.0, y + cy / 2.0);
    double scale_x = cx * 1.0 / cx2;
    double scale_y = cy * 1.0 / cy2;
    cairo_scale(hdc->cairo, scale_x, scale_y);
    // 源 surface 定位：源矩形中心 (x1 + cx2/2, y1 + cy2/2) 映射到目标中心（当前原点）。
    // 必须使用有符号的 cx2/cy2，否则镜像（cx2<0）时源偏移错误。
    double src_ox = -(x1 + cx2 / 2.0);
    double src_oy = -(y1 + cy2 / 2.0);
    switch (rop)
    {
    case SRCCOPY:
        cairo_set_source_surface(hdc->cairo, src, src_ox, src_oy);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_SOURCE);
        break;
    case SRCINVERT:
        if (rasterDone)
            break;
        cairo_set_source_surface(hdc->cairo, src, src_ox, src_oy);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    case SRCPAINT:
        if (rasterDone)
            break;
        cairo_set_source_surface(hdc->cairo, src, src_ox, src_oy);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_OVER);
        break;
    case SRCAND:
        if (rasterDone)
            break;
        cairo_set_source_surface(hdc->cairo, src, src_ox, src_oy);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DEST_IN);
        break;
    case DSTINVERT:
        cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    }
    if (!rasterDone)
    {
        cairo_fill(hdc->cairo);
    }
    cairo_restore(hdc->cairo);
    if (hdc == hdcSrc)
    {
        // destroy surface copy
        cairo_surface_destroy(src);
    }
#ifdef __OHOS__
    markDcDirty(hdc, x, y, cx, cy);
#endif
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
    (void)StretchBlt(hdc, x_dst, y_dst, width_dst, height_dst, memdc, x_src, y_src, width_src, height_src, rop);
    DeleteDC(memdc);
    DeleteObject(bmp);
    return height_src;
}

BOOL TransparentBlt(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, UINT crTransparent)
{
    // Win32 TransparentBlt：源区中 RGB 等于 crTransparent 的像素不绘制（目标
    // 保留原内容），其余像素拉伸拷贝。cairo 的掩码/合成算子只承载 alpha、没有
    // 颜色比较算子（GDI 的 SetBkColor 掩码 blit 是把比较藏在驱动 blitter 内部），
    // 键控比较无论如何都要做一次逐像素扫描——所以扫描时直接生成"keyed 像素
    // alpha=0"的 premultiplied BGRA 拷贝，最后用 OVER 一次绘制到位：
    //   - keyed 像素（alpha=0）不改动目标；
    //   - 非 keyed 像素 alpha=255 时精确覆盖（RGB24/不透明 32bpp 源与真 GDI
    //     逐位一致；带部分 alpha 的 32bpp 源按 source-over 混合，符合实际用途）。
    cairo_t *crDest = hdcDest->cairo;
    cairo_surface_t *srcSurf = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    if (!crDest || !srcSurf)
        return FALSE;
    if (wSrc <= 0 || hSrc <= 0 || wDest <= 0 || hDest <= 0)
        return FALSE;
    cairo_format_t fmt = cairo_image_surface_get_format(srcSurf);
    if (fmt != CAIRO_FORMAT_ARGB32 && fmt != CAIRO_FORMAT_RGB24)
        return FALSE;
    cairo_surface_flush(srcSurf);
    const unsigned char *sdata = cairo_image_surface_get_data(srcSurf);
    if (!sdata)
        return FALSE;
    int bmpW = cairo_image_surface_get_width(srcSurf);
    int bmpH = cairo_image_surface_get_height(srcSurf);
    int srcStride = cairo_image_surface_get_stride(srcSurf);
    /* cairo 的 ARGB32 与 RGB24 都是 4B/px（RGB24 末字节无意义，stride=width*4），
     * 不能按 3B/px 索引——否则 RGB24 源从第 2 列起全部错位 */
    int bpp = 4;

    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo, &mtxSrc);
    xoriginSrc += (int)mtxSrc.x0;
    yoriginSrc += (int)mtxSrc.y0;

    int kB = GetBValue(crTransparent), kG = GetGValue(crTransparent), kR = GetRValue(crTransparent);

    // 逐像素键控：keyed → 全透明（alpha=0），其余原样保留 premultiplied BGRA
    //（含 32bpp 源的 per-pixel alpha；RGB24 源补 alpha=255）。扫描只读源、
    // 先于任何目标写入，自拷贝（hdcDest==hdcSrc）天然安全。
    cairo_surface_t *keyed = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, wSrc, hSrc);
    if (!keyed)
        return FALSE;
    unsigned char *kdata = cairo_image_surface_get_data(keyed);
    int kStride = cairo_image_surface_get_stride(keyed);
    for (int j = 0; j < hSrc; j++)
    {
        int by = yoriginSrc + j; // 数据行 row0=顶部，与 BitBlt 的 srcRect 映射一致
        const unsigned char *srow = (by >= 0 && by < bmpH) ? sdata + (size_t)by * srcStride : nullptr;
        unsigned char *krow = kdata + (size_t)j * kStride;
        for (int i = 0; i < wSrc; i++)
        {
            int bx = xoriginSrc + i;
            if (!srow || bx < 0 || bx >= bmpW)
                continue; // 源位图之外：保持全透明，保留目标
            const unsigned char *s = srow + (size_t)bx * bpp;
            if (s[0] == kB && s[1] == kG && s[2] == kR) // 只比 RGB，忽略 alpha（与真 GDI 一致）
                continue;
            unsigned char *d = krow + (size_t)i * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            d[3] = (fmt == CAIRO_FORMAT_ARGB32) ? s[3] : 0xFF;
        }
    }
    cairo_surface_mark_dirty(keyed);

    // 一次 OVER 绘制到位（keyed 像素 alpha=0 不改动目标，非 keyed 精确覆盖）。
    // 必须显式 OVER：若外层残留 SOURCE/COPY 类算子，alpha=0 的 keyed 像素会把目标擦掉。
    cairo_save(crDest);
    cairo_rectangle(crDest, xoriginDest, yoriginDest, wDest, hDest);
    cairo_clip(crDest);
    cairo_translate(crDest, xoriginDest, yoriginDest);
    cairo_scale(crDest, (double)wDest / wSrc, (double)hDest / hSrc);
    cairo_set_source_surface(crDest, keyed, 0, 0);
    cairo_pattern_set_extend(cairo_get_source(crDest), CAIRO_EXTEND_PAD);
    cairo_set_operator(crDest, CAIRO_OPERATOR_OVER);
    cairo_paint(crDest);
    cairo_restore(crDest);

    cairo_surface_destroy(keyed);
    return TRUE;
}

void SetStretchBltMode(HDC hdc __attribute__((unused)), int mode __attribute__((unused)))
{
    // todo:hjx
}

BOOL PatBlt(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ int w, _In_ int h, _In_ DWORD rop)
{
    BOOL ret = FALSE;
    cairo_t *cr = hdc->cairo;
    cairo_save(cr);
    if (ApplyBrush(hdc, hdc->brush, w, h, x, y))
    {
        switch (rop)
        {
        case PATCOPY:
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            break;
        case PATINVERT:
            cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
            break;
        case DSTINVERT:
            cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
            cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
            break;
        case BLACKNESS:
            cairo_set_source_rgb(hdc->cairo, 0.0, 0.0, 0.0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            break;
        case WHITENESS:
            cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            break;
        }
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
        ret = TRUE;
    }
    cairo_restore(cr);
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
    cairo_text_extents_t ext;
    char word[6];
    LPCSTR p = pszBuf;
    LPCSTR pEnd = p + cchText;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    SIZE ret = { 0, 0 };
    while (p < pEnd)
    {
        LPCSTR next = nextChar(p);
        assert(next - p <= 5);
        memcpy(word, p, (next - p));
        word[next - p] = 0;
        cairo_text_extents(hdc->cairo, word, &ext);
        ret.cx += ext.width;
        p = next;
    }
    ret.cy = font_ext.ascent + font_ext.descent;
    return ret;
}

static void DrawTextDecLines(HDC hdc, cairo_font_extents_t &font_ext, LPCSTR str __attribute__((unused)), int len __attribute__((unused)), int x, int y, const cairo_text_extents_t &text_ext)
{
    const LOGFONT *lf = (const LOGFONT *)GetGdiObjPtr(hdc->hfont);
    assert(lf);
    if (lf->lfStrikeOut || lf->lfUnderline)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(hdc));
        ApplyPen(hdc, pen, 0, 0, 0, 0);
        ApplyRop2(hdc->cairo, hdc->rop2);
        if (lf->lfStrikeOut)
        {
            double y_line = y + (font_ext.ascent + font_ext.descent) / 2.0;
            cairo_move_to(hdc->cairo, x + text_ext.x_bearing, y_line);
            cairo_line_to(hdc->cairo, x + text_ext.x_advance, y_line);
        }
        if (lf->lfUnderline)
        {
            double y_line = y + font_ext.ascent + font_ext.descent + 1;
            cairo_move_to(hdc->cairo, x + text_ext.x_bearing, y_line);
            cairo_line_to(hdc->cairo, x + text_ext.x_advance, y_line);
        }
        cairo_stroke(hdc->cairo);
        DeleteObject(pen);
    }
}

static void DrawSingleLine(HDC hdc, LPCSTR pszBuf, int iBegin, int cchText, LPRECT pRect, UINT uFormat)
{
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    cairo_text_extents_t ext;
    cairo_text_extents2(hdc->cairo, pszBuf + iBegin, cchText, &ext);
    double font_hei = font_ext.ascent + font_ext.descent;
    if (uFormat & DT_CALCRECT)
    {
        pRect->right = pRect->left + ext.x_advance;
        pRect->bottom = pRect->top + font_hei;
    }
    else
    {
        switch (uFormat & (DT_LEFT | DT_CENTER | DT_RIGHT))
        {
        case DT_LEFT:
            cairo_move_to(hdc->cairo, pRect->left, pRect->top + font_ext.ascent);
            break;
        case DT_CENTER:
            cairo_move_to(hdc->cairo, pRect->left + (pRect->right - pRect->left - ext.x_advance) / 2, pRect->top + font_ext.ascent);
            break;
        case DT_RIGHT:
            cairo_move_to(hdc->cairo, pRect->right - ext.x_advance, pRect->top + font_ext.ascent);
            break;
        }
        cairo_show_text2(hdc->cairo, pszBuf + iBegin, cchText);
        cairo_text_extents_t text_ext;
        cairo_text_extents2(hdc->cairo, pszBuf + iBegin, cchText, &text_ext);
        DrawTextDecLines(hdc, font_ext, pszBuf + iBegin, cchText, pRect->left, pRect->top, text_ext);
    }
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
                if (!(uFormat & DT_CALCRECT))
                {
                    RECT rcText = { pRect->left, pt.y, nRight, pt.y + nLineHei };
                    DrawSingleLine(hdc, pszBuf, (int)(pLineHead - pszBuf), (int)(pLineTail - pLineHead), &rcText, uFormat);
                }
                // 显示多行文本时，如果下一行文字的高度超过了文本框，则不再输出下一行文字内容。
                if (pt.y + nLineHei + kDrawText_LineInterval > pRect->bottom)
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
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    double text_wid = pRect->right - pRect->left, text_hei = pRect->bottom - pRect->top;
    ApplyPen(hdc, hdc->pen, text_wid, text_hei, pRect->left, pRect->top);
    if (hdc->bkMode == OPAQUE && !(uFormat & DT_CALCRECT))
    {
        // Win32: a single-line DrawText fills the background of the text
        // extent only (like TextOut), not the whole format rectangle.
        // Measure the laid-out text first, then place the fill rect the same
        // way the real draw positions the line.
        RECT rcFill = rc;
        if (uFormat & DT_SINGLELINE)
        {
            RECT rcMeasure = rc;
            cairo_draw_text(hdc->cairo, pszBuf, cchText, &rcMeasure,
                uFormat | DT_CALCRECT | DT_NOCLIP);
            int w = rcMeasure.right - rcMeasure.left;
            int h = rcMeasure.bottom - rcMeasure.top;
            int left = rc.left, top = rc.top;
            if (uFormat & DT_RIGHT)
                left += (rc.right - rc.left) - w;
            else if (uFormat & DT_CENTER)
                left += ((rc.right - rc.left) - w) / 2;
            if (uFormat & DT_BOTTOM)
                top += (rc.bottom - rc.top) - h;
            else if (uFormat & DT_VCENTER)
                top += ((rc.bottom - rc.top) - h) / 2;
            rcFill.left = left;
            rcFill.top = top;
            rcFill.right = left + w;
            rcFill.bottom = top + h;
        }
        CairoColor crBk(hdc->crBk);
        cairo_set_source_rgba(hdc->cairo, crBk.r, crBk.g, crBk.b, crBk.a);
        cairo_rectangle(hdc->cairo, rcFill.left, rcFill.top,
            rcFill.right - rcFill.left, rcFill.bottom - rcFill.top);
        cairo_fill(hdc->cairo);
    }
    CairoColor cr(hdc->crText);
    cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
    cairo_matrix_t mtx = {};
    cairo_get_matrix(hdc->cairo, &mtx);
    const LOGFONT *lf = (const LOGFONT *)GetGdiObjPtr(hdc->hfont);
    assert(lf);
    cairo_draw_text(hdc->cairo, pszBuf, cchText, pRect, uFormat);
    if (lf->lfUnderline || lf->lfStrikeOut)
    {
        cairo_font_extents_t font_ext;
        cairo_font_extents(hdc->cairo, &font_ext);
        HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(hdc));
        ApplyPen(hdc, pen, 0, 0, 0, 0);
        ApplyRop2(hdc->cairo, hdc->rop2);
        if (lf->lfStrikeOut)
        {
            double y_line = (pRect->top + pRect->bottom) / 2.0;
            cairo_move_to(hdc->cairo, pRect->left, y_line);
            cairo_line_to(hdc->cairo, pRect->right, y_line);
        }
        if (lf->lfUnderline)
        {
            double y_line = pRect->top + font_ext.ascent + font_ext.descent + 1;
            cairo_move_to(hdc->cairo, pRect->left, y_line);
            cairo_line_to(hdc->cairo, pRect->right, y_line);
        }
        cairo_stroke(hdc->cairo);
        DeleteObject(pen);
    }
    cairo_restore(hdc->cairo);
    if (!(uFormat & DT_CALCRECT))
    {
        *pRect = rc;
    }
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
    swinx_stl::string str;
    tostring(lpString, c, str);
    return TextOutA(hdc, x, y, str.c_str(), str.length());
}

BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
    if (c < 0)
        c = strlen(lpString);

    // Return early if no text to draw
    if (c == 0)
        return TRUE;

    // Win32: with TA_UPDATECP the x/y parameters are ignored and drawing
    // starts at the current position, which is then advanced by the text
    // width. Without it, the current position is left untouched.
    if (hdc->textAlign & TA_UPDATECP)
    {
        double cx, cy;
        cairo_get_current_point(hdc->cairo, &cx, &cy);
        x = (int)cx;
        y = (int)cy;
    }

    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    CairoColor cr(hdc->crText);
    cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);

    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);

    cairo_text_extents_t text_ext;
    cairo_text_extents2(hdc->cairo, lpString, c, &text_ext);
    switch (hdc->textAlign & (TA_LEFT | TA_RIGHT | TA_CENTER))
    {
    case TA_RIGHT:
        x -= text_ext.x_advance;
        break;
    case TA_CENTER:
        x -= text_ext.x_advance / 2;
        break;
    }
    switch (hdc->textAlign & (TA_BASELINE | TA_BOTTOM | TA_TOP))
    {
    case TA_TOP:
        y += font_ext.ascent;
        break;
    case TA_BASELINE:
        break;
    case TA_BOTTOM:
        y -= font_ext.descent;
        break;
    }
    if (hdc->bkMode == OPAQUE)
    {
        // fill bkcolor for text
        cairo_save(hdc->cairo);
        COLORREF crBk = GetBkColor(hdc);
        CairoColor cr(crBk);
        cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
        cairo_rectangle(hdc->cairo, x, y - font_ext.ascent, text_ext.x_advance, font_ext.ascent + font_ext.descent);
        cairo_fill(hdc->cairo);
        cairo_restore(hdc->cairo);
    }
    double old_x, old_y;
    cairo_get_current_point(hdc->cairo, &old_x, &old_y);

    cairo_move_to(hdc->cairo, x, y);

    // If recording path, add text outline to path
    if (hdc->pathRecording)
    {
        cairo_text_path2(hdc->cairo, lpString, c);
    }
    else
    {
        // Otherwise, show text normally
        cairo_show_text2(hdc->cairo, lpString, c);
        DrawTextDecLines(hdc, font_ext, lpString, c, x, y, text_ext);
    }

    // restore the current position unless TA_UPDATECP (Win32 semantics:
    // TextOut only updates the current position under TA_UPDATECP)
    if (!(hdc->textAlign & TA_UPDATECP))
        cairo_move_to(hdc->cairo, old_x, old_y);
    cairo_restore(hdc->cairo);
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

    // Win32: with TA_UPDATECP TabbedTextOut ignores x/y, starts at the
    // current position and updates it to the end of the text when done.
    // (GetTabbedTextExtent passes fDisplayText=FALSE and must not touch CP.)
    UINT oldAlign = 0;
    if (fDisplayText && (hdc->textAlign & TA_UPDATECP))
    {
        double cx, cy;
        cairo_get_current_point(hdc->cairo, &cx, &cy);
        x = (int)cx;
        y = (int)cy;
        start = x;
        // draw each substring at its computed x0: suspend UPDATECP while
        // emitting chunks, the CP is advanced once at the end
        oldAlign = hdc->textAlign;
        hdc->textAlign = oldAlign & ~TA_UPDATECP;
    }

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

    if (oldAlign)
    {
        // restore UPDATECP and advance the current position to the text end
        hdc->textAlign = oldAlign;
        cairo_move_to(hdc->cairo, x, y);
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
    swinx_stl::string str;
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
    swinx_stl::string str;
    tostring(lpString, nCount, str);
    return TabbedTextOutA(hDC, X, Y, str.c_str(), str.length(), nTabPositions, lpnTabStopPositions, nTabOrigin);
}

BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
{
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    cairo_text_extents_t ext;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    cairo_text_extents2(hdc->cairo, lpString, c, &ext);
    psizl->cx = ext.x_advance;
    psizl->cy = font_ext.ascent + font_ext.descent;
    cairo_restore(hdc->cairo);
    return TRUE;
}

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
    swinx_stl::string str;
    tostring(lpString, c, str);
    return GetTextExtentPoint32A(hdc, str.c_str(), str.length(), psizl);
}

BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE psizl)
{
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32A(hdc, lpszString, cchString, psizl);
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    cairo_text_extents_t ext;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    if (cchString < 0)
        cchString = strlen(lpszString);
    int *pCharWid = new int[cchString];
    int nWords = cairo_text_extents2_ex(hdc->cairo, lpszString, cchString, &ext, pCharWid);
    if (lpnDx)
    {
        if (lpnFit)
            *lpnFit = cchString;
        for (int i = 0, ichar = 0; i < nWords; i++)
        {
            int chars = swinx::UTF8CharLength(lpszString[ichar]);
            if (pCharWid[i] > nMaxExtent)
            {
                if (lpnFit)
                    *lpnFit = ichar;
                if (i > 0)
                    ext.x_advance = pCharWid[i - 1];
                else
                    ext.x_advance = 0;
                break;
            }
            for (int j = 0; j < chars; j++)
            {
                lpnDx[ichar + j] = pCharWid[i];
            }
            ichar += chars;
        }
    }
    delete[] pCharWid;

    psizl->cx = ext.x_advance;
    psizl->cy = font_ext.ascent + font_ext.descent;
    cairo_restore(hdc->cairo);
    return TRUE;
}

BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE psizl)
{
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32W(hdc, lpszString, cchString, psizl);
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    cairo_text_extents_t ext;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    swinx_stl::string str;
    tostring(lpszString, cchString, str);
    int *pCharWid = new int[str.length()];
    int nWords = cairo_text_extents2_ex(hdc->cairo, str.c_str(), str.length(), &ext, pCharWid);
    if (lpnDx)
    {
        if (lpnFit)
            *lpnFit = cchString;
        for (int i = 0, ichar = 0; i < nWords; i++)
        {
            int chars = swinx::WideCharLength(lpszString[ichar]);
            if (pCharWid[i] > nMaxExtent)
            {
                if (lpnFit)
                    *lpnFit = ichar;
                if (i > 0)
                    ext.x_advance = pCharWid[i - 1];
                else
                    ext.x_advance = 0;
                break;
            }
            for (int j = 0; j < chars; j++)
            {
                lpnDx[ichar + j] = pCharWid[i];
            }
            ichar += chars;
        }
    }
    delete[] pCharWid;

    psizl->cx = ext.x_advance;
    psizl->cy = font_ext.ascent + font_ext.descent;
    cairo_restore(hdc->cairo);
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
    if (i >= 0 && i < (int)ARRAYSIZE(system_colors))
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
        static LOGFONTA lf = {};
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
    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    double wid = right - left, hei = bottom - top;
    if (hdc->pathRecording)
    {
        // Win32 Rectangle() records the path with the right/bottom edges
        // excluded: PathToRegion(Rectangle(100,100,200,200)) yields the
        // region box (100,100,199,199) (verified on real Windows)
        cairo_rectangle(ctx, left, top, wid - 1, hei - 1);
        return TRUE;
    }
    cairo_rectangle(ctx, left, top, wid, hei);

    cairo_save(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left,top);
    cairo_restore(ctx);
    return TRUE;
}

static void drawRoundRect(cairo_t *cr, double x, double y, double width, double height, double rx, double ry)
{
    if (rx <= 0.0 || ry <= 0.0)
    {
        cairo_rectangle(cr, x, y, width, height);
        return;
    }
    double degrees = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_matrix_t mtx;
    cairo_get_matrix(cr, &mtx);
    cairo_translate(cr, x, y);
    if (rx == ry)
    {
        cairo_arc(cr, width - rx, rx, rx, -90 * degrees, 0 * degrees);
        cairo_arc(cr, width - rx, height - rx, rx, 0 * degrees, 90 * degrees);
        cairo_arc(cr, rx, height - rx, rx, 90 * degrees, 180 * degrees);
        cairo_arc(cr, rx, rx, rx, 180 * degrees, 270 * degrees);
    }
    else
    {
        double scale_y = ry / rx;
        height /= scale_y;
        cairo_scale(cr, 1, scale_y);
        cairo_arc(cr, width - rx, rx, rx, -90 * degrees, 0 * degrees);
        cairo_arc(cr, width - rx, height - rx, rx, 0 * degrees, 90 * degrees);
        cairo_arc(cr, rx, height - rx, rx, 90 * degrees, 180 * degrees);
        cairo_arc(cr, rx, rx, rx, 180 * degrees, 270 * degrees);
    }
    cairo_set_matrix(cr, &mtx);
    cairo_close_path(cr);
}

BOOL RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    double wid = right - left, hei = bottom - top;
    cairo_save(ctx);
    cairo_translate(ctx, left, top);
    drawRoundRect(ctx, 0, 0, wid, hei, width / 2, height / 2);
    cairo_restore(ctx);
    if (hdc->pathRecording)
    {
        return TRUE;
    }

    cairo_save(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left,top);
    cairo_restore(ctx);
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
    cairo_t *ctx = hdc->cairo;
    if (!ctx || cpt < 2)
        return FALSE;

    cairo_move_to(ctx, apt[0].x, apt[0].y);
    for (int i = 1; i < cpt; i++)
    {
        cairo_line_to(ctx, apt[i].x, apt[i].y);
    }

    if (hdc->pathRecording)
    {
        return TRUE;
    }
    cairo_save(ctx);
    double x1, y1, x2, y2;
    cairo_path_extents(ctx, &x1,&y1, &x2,&y2);
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    cairo_restore(ctx);
    return TRUE;
}


BOOL PolyBezier(HDC hdc, const POINT *apt, DWORD cpt)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx || !apt || cpt < 4)
        return FALSE;

    if ((cpt - 1) % 3 != 0)
        return FALSE;

    double saved_x, saved_y;
    bool has_current_point = cairo_has_current_point(ctx);
    if (has_current_point)
        cairo_get_current_point(ctx, &saved_x, &saved_y);

    cairo_move_to(ctx, apt[0].x, apt[0].y);
    for (DWORD i = 1; i < cpt; i += 3)
    {
        if (i + 2 >= cpt)
            break;
        cairo_curve_to(ctx, apt[i].x, apt[i].y,
                       apt[i + 1].x, apt[i + 1].y,
                       apt[i + 2].x, apt[i + 2].y);
    }

    if (!hdc->pathRecording)
    {
        cairo_save(ctx);
        double x1, y1, x2, y2;
        cairo_path_extents(ctx, &x1,&y1, &x2,&y2);
        DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
        cairo_restore(ctx);
    }

    // Restore original current position (PolyBezier doesn't change current position)
    if (has_current_point)
        cairo_move_to(ctx, saved_x, saved_y);
    else
        cairo_new_sub_path(ctx);

    return TRUE;
}

BOOL PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx || !apt || cpt < 3)
        return FALSE;

    if (cpt % 3 != 0)
        return FALSE;

    if (!cairo_has_current_point(ctx))
        return FALSE;

    for (DWORD i = 0; i < cpt; i += 3)
    {
        if (i + 2 >= cpt)
            break;
        cairo_curve_to(ctx, apt[i].x, apt[i].y,
                       apt[i + 1].x, apt[i + 1].y,
                       apt[i + 2].x, apt[i + 2].y);
    }

    if (!hdc->pathRecording)
    {
        double final_x, final_y;
        cairo_get_current_point(ctx, &final_x, &final_y);

        cairo_save(ctx);
        double x1, y1, x2, y2;
        cairo_path_extents(ctx, &x1,&y1, &x2,&y2);
        DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
        cairo_restore(ctx);

        cairo_move_to(ctx, final_x, final_y);
    }

    return TRUE;
}

int ClearRect(HDC hdc, const RECT *lprc, COLORREF cr)
{
    cairo_save(hdc->cairo);
    cairo_translate(hdc->cairo, lprc->left, lprc->top);
    double wid = lprc->right - lprc->left, hei = lprc->bottom - lprc->top;
    CairoColor cr2(cr);
    cairo_set_source_rgba(hdc->cairo, cr2.r, cr2.g, cr2.b, cr2.a);
    cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_SOURCE);
    cairo_rectangle(hdc->cairo, 0, 0, wid, hei);
    cairo_fill(hdc->cairo);
    cairo_restore(hdc->cairo);
    return 1;
}

int FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
    int ret = 0;
    cairo_save(hdc->cairo);
    double wid = lprc->right - lprc->left, hei = lprc->bottom - lprc->top;
    if (ApplyBrush(hdc, hbr, wid, hei, lprc->left, lprc->top))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_rectangle(hdc->cairo, lprc->left, lprc->top, wid, hei);
        cairo_fill(hdc->cairo);
        ret = 1;
    }
    cairo_restore(hdc->cairo);
    return ret;
}

int FrameRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    double rc_wid = lprc->right - lprc->left, rc_hei = lprc->bottom - lprc->top;
    if(hbr)
        ApplyBrush(hdc, hbr, rc_wid, rc_hei, lprc->left, lprc->top);
    else
        ApplyPen(hdc, hdc->pen, rc_wid, rc_hei, lprc->left, lprc->top);
    ApplyRop2(hdc->cairo, hdc->rop2);
    cairo_rectangle(hdc->cairo, lprc->left, lprc->top, rc_wid, rc_hei);
    cairo_stroke(ctx);
    cairo_restore(ctx);
    return TRUE;
}

BOOL InvertRect(HDC hdc, const RECT *lprc)
{
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    cairo_set_source_rgb(ctx, 1.0, 1.0, 1.0);
    cairo_set_operator(ctx, CAIRO_OPERATOR_DIFFERENCE);
    cairo_rectangle(ctx, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
    cairo_fill(ctx);
    cairo_restore(ctx);
    return TRUE;
}

BOOL MoveToEx(HDC hdc, int x, int y, LPPOINT lpPoint)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    if (lpPoint)
    {
        double cur_x, cur_y;
        cairo_get_current_point(ctx, &cur_x, &cur_y);
        lpPoint->x = cur_x;
        lpPoint->y = cur_y;
    }

    cairo_move_to(ctx, x, y);
    return TRUE;
}

BOOL GetCurrentPositionEx(HDC hdc, LPPOINT lpPoint)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx || !lpPoint)
        return FALSE;

    double cur_x, cur_y;
    cairo_get_current_point(ctx, &cur_x, &cur_y);
    lpPoint->x = (int)cur_x;
    lpPoint->y = (int)cur_y;
    return TRUE;
}

BOOL LineTo(HDC hdc, int nXEnd, int nYEnd)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    if (!hdc->pathRecording)
    {
        // Draw the segment in device space so that 1px lines land exactly on
        // pixel rows/columns like real GDI. Cairo strokes are centered on the
        // path, so a line at an integer coordinate covers two half-pixels and
        // looks washed out / shifted instead of covering the pixel like GDI.
        double x0, y0;
        cairo_get_current_point(ctx, &x0, &y0); // (0,0) if no MoveToEx yet, like GDI

        cairo_matrix_t mtx;
        cairo_get_matrix(ctx, &mtx);
        double x1 = x0, y1 = y0, x2 = nXEnd, y2 = nYEnd;
        cairo_matrix_transform_point(&mtx, &x1, &y1);
        cairo_matrix_transform_point(&mtx, &x2, &y2);

        LOGPEN *pen = (LOGPEN *)GetGdiObjPtr(hdc->pen);
        int penW = pen ? (int)pen->lopnWidth.x : 1;
        if (penW == 0)
            penW = 1; // width 0 means cosmetic 1px pen
        if (penW & 1)
        { // odd pens are pixel-aligned: shift onto pixel centers and extend
          // by half the pen width so the endpoint pixels are covered like GDI
            double dx = x2 - x1, dy = y2 - y1;
            double len = sqrt(dx * dx + dy * dy);
            if (len > 0)
            {
                double ext = penW / 2.0;
                x1 += 0.5 - dx / len * ext;
                y1 += 0.5 - dy / len * ext;
                x2 += 0.5 + dx / len * ext;
                y2 += 0.5 + dy / len * ext;
            }
            else
            {
                x1 += 0.5;
                y1 += 0.5;
            }
        }

        cairo_save(ctx);
        cairo_identity_matrix(ctx);
        cairo_new_path(ctx);
        cairo_move_to(ctx, x1, y1);
        cairo_line_to(ctx, x2, y2);
        DrawPathStroke(ctx, hdc,
            (x2 > x1 ? x2 - x1 : x1 - x2), (y2 > y1 ? y2 - y1 : y1 - y2),
            (x1 < x2 ? x1 : x2), (y1 < y2 ? y1 : y2));
        cairo_restore(ctx);
    }
    else
    {
        // path recording keeps user-space coordinates (GDI path semantics)
        cairo_line_to(ctx, nXEnd, nYEnd);
    }

    // GDI: after LineTo the current position is the segment end (user space)
    cairo_move_to(ctx, nXEnd, nYEnd);
    return TRUE;
}

BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    double cx = (left + right) / 2.0;
    double cy = (top + bottom) / 2.0;
    double wid = right - left, hei = bottom - top;


    // Create pie path
    cairo_save(ctx);
    cairo_translate(ctx, cx, cy);
    cairo_scale(ctx, wid, hei);
    cairo_move_to(ctx, 0.5, 0);
    cairo_arc(ctx, 0, 0, 0.5, 0, M_PI * 2);
    cairo_restore(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    cairo_save(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, left, top);
    cairo_restore(ctx);
    return TRUE;
}


BOOL Pie(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    if (!hdc)
        return FALSE;
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;

    cairo_t *ctx = hdc->cairo;
    assert(ctx);
    double cx = (x1 + x2) / 2;
    double cy = (y1 + y2) / 2;
    double dx3 = double(x3 - cx) / wid;
    double dx4 = double(x4 - cx) / wid;
    double dy3 = double(y3 - cy) / hei;
    double dy4 = double(y4 - cy) / hei;
    double arc1 = atan2(dy3, dx3);
    double arc2 = atan2(dy4, dx4);

    // Create pie path
    cairo_save(ctx);
    cairo_translate(ctx, cx, cy);
    cairo_scale(ctx, wid, hei);
    cairo_move_to(ctx, 0, 0);
    cairo_line_to(ctx, dx4, dy4);
    cairo_arc(ctx, 0, 0, 0.5, arc2, arc1);
    cairo_close_path(ctx);
    cairo_restore(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }

    cairo_save(ctx);
    DrawPathFillStroke(ctx, hdc, wid, hei, x1, y1);
    cairo_restore(ctx);
    return TRUE;
}

BOOL Arc(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;

    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    double cx = (x1 + x2) / 2;
    double cy = (y1 + y2) / 2;
    double dx3 = double(x3 - cx) / wid;
    double dx4 = double(x4 - cx) / wid;
    double dy3 = double(y3 - cy) / hei;
    double dy4 = double(y4 - cy) / hei;
    double arc1 = atan2(dy3, dx3);
    double arc2 = atan2(dy4, dx4);

    // Create arc path
    cairo_save(ctx);
    cairo_translate(ctx, cx, cy);
    cairo_scale(ctx, wid, hei);
    cairo_move_to(ctx, 0.5 * cos(arc2), 0.5 * sin(arc2));
    cairo_arc(ctx, 0, 0, 0.5, arc2, arc1);
    cairo_restore(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    cairo_save(ctx);
    DrawPathStroke(ctx, hdc, wid, hei, x1, y1);
    cairo_restore(ctx);
    return TRUE;
}

BOOL Chord(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;

    cairo_t *ctx = hdc->cairo;
    if (!ctx)
        return FALSE;

    double cx = (x1 + x2) / 2;
    double cy = (y1 + y2) / 2;
    double dx3 = double(x3 - cx) / wid;
    double dx4 = double(x4 - cx) / wid;
    double dy3 = double(y3 - cy) / hei;
    double dy4 = double(y4 - cy) / hei;
    double arc1 = atan2(dy3, dx3);
    double arc2 = atan2(dy4, dx4);

    // Create arc path
    cairo_save(ctx);
    cairo_translate(ctx, cx, cy);
    cairo_scale(ctx, wid, hei);
    cairo_move_to(ctx, 0.5 * cos(arc2), 0.5 * sin(arc2));
    cairo_arc(ctx, 0, 0, 0.5, arc2, arc1);
    cairo_close_path(ctx);
    cairo_restore(ctx);

    // If recording path, we're done
    if (hdc->pathRecording)
    {
        return TRUE;
    }
    cairo_save(ctx);
    DrawPathStroke(ctx, hdc, wid, hei, x1, y1);
    cairo_restore(ctx);
    return TRUE;
}

static void convert_xform_to_cairo_matrix(const XFORM *xform, cairo_matrix_t &cairo_matrix)
{
    // Windows XFORM 定义（列向量，MSDN）:
    //   x' = eM11*x + eM21*y + eDx
    //   y' = eM12*x + eM22*y + eDy
    // 即 a = eM11, b = eM12, c = eM21, d = eM22, tx = eDx, ty = eDy.
    // cairo_matrix 语义:
    //   x' = xx * x + xy * y + x0
    //   y' = yx * x + yy * y + y0
    cairo_matrix.xx = xform->eM11;
    cairo_matrix.xy = xform->eM21;
    cairo_matrix.yx = xform->eM12;
    cairo_matrix.yy = xform->eM22;
    cairo_matrix.x0 = xform->eDx;
    cairo_matrix.y0 = xform->eDy;
}

static void convert_cairo_matrix_to_xform(const cairo_matrix_t *cairo_matrix, XFORM &xform)
{
    // convert_xform_to_cairo_matrix 的逆映射
    xform.eM11 = cairo_matrix->xx;
    xform.eM21 = cairo_matrix->xy;
    xform.eM12 = cairo_matrix->yx;
    xform.eM22 = cairo_matrix->yy;
    xform.eDx = cairo_matrix->x0;
    xform.eDy = cairo_matrix->y0;
}

static void update_transform(HDC hdc)
{
    if (hdc->ptOrigin.x == 0 && hdc->ptOrigin.y == 0)
    {
        cairo_set_matrix(hdc->cairo, &hdc->mtx);
    }
    else if (matrix_is_identity(&hdc->mtx))
    {
        cairo_matrix_t mtx;
        cairo_matrix_init_translate(&mtx, hdc->ptOrigin.x, hdc->ptOrigin.y);
        cairo_set_matrix(hdc->cairo, &mtx);
    }
    else
    { // both origin and matrix are not identity, use mtxTrans *(preTrans*mtx*postTrans), mtxTrans equal to postTrans
        cairo_matrix_t preTrans, postTrans;
        cairo_matrix_init_translate(&preTrans, -hdc->ptOrigin.x, -hdc->ptOrigin.y);
        cairo_matrix_init_translate(&postTrans, hdc->ptOrigin.x, hdc->ptOrigin.y);
        // calc preTrans*mtx*postTrans
        cairo_matrix_t mtx;
        memcpy(&mtx, &hdc->mtx, sizeof(mtx));
        cairo_matrix_multiply(&mtx, &preTrans, &mtx);
        cairo_matrix_multiply(&mtx, &mtx, &postTrans);
        // calc mtxTrans *(preTrans*mtx*postTrans)
        cairo_matrix_multiply(&mtx, &postTrans, &mtx); // postTrans eqaul to mtxTrans
        // apply the total matrix
        cairo_set_matrix(hdc->cairo, &mtx);
    }
}

BOOL SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    if (lppt)
    {
        lppt->x = hdc->ptOrigin.x;
        lppt->y = hdc->ptOrigin.y;
    }
    hdc->ptOrigin.x = x;
    hdc->ptOrigin.y = y;
    update_transform(hdc);
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

BOOL WINAPI SetWindowExtEx(HDC hdc __attribute__((unused)),      // handle to device context
                           int nXExtent __attribute__((unused)), // new horizontal window extent
                           int nYExtent __attribute__((unused)), // new vertical window extent
                           LPSIZE lpSize __attribute__((unused)) // original window extent
)
{
    // todo:hjx
    return FALSE;
}

BOOL GetWorldTransform(HDC hdc, LPXFORM lpxf)
{
    convert_cairo_matrix_to_xform(&hdc->mtx, *lpxf);
    return TRUE;
}

BOOL SetWorldTransform(HDC hdc, const XFORM *lpxf)
{
    convert_xform_to_cairo_matrix(lpxf, hdc->mtx); // 传递指针
    update_transform(hdc);
    return TRUE;
}

BOOL ModifyWorldTransform(HDC hdc,              // handle to device context
                          const XFORM *lpXform, // transformation data
                          DWORD iMode           // modification mode
)
{
    cairo_matrix_t mtx;
    convert_xform_to_cairo_matrix(lpXform, mtx); // 传递指针
    switch (iMode)
    {
    case MWT_IDENTITY:
        cairo_matrix_init_identity(&hdc->mtx);
        break;
    case MWT_LEFTMULTIPLY:
        cairo_matrix_multiply(&hdc->mtx, &mtx, &hdc->mtx);
        break;
    case MWT_RIGHTMULTIPLY:
        cairo_matrix_multiply(&hdc->mtx, &hdc->mtx, &mtx);
        break;
    default:
        return FALSE;
    }
    update_transform(hdc);
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
    cairo_surface_t *ret = nullptr;
    switch (cBitsPerPel)
    {
    case 1: // mono color
        ret = cairo_image_surface_create(CAIRO_FORMAT_A1, nWidth, nHeight);
        if (!ret)
            break;
        if (lpvBits)
        {
            unsigned char *buf = cairo_image_surface_get_data(ret);
            memcpy(buf, lpvBits, (((nWidth + 31) >> 3) & ~3) * nHeight);
            cairo_surface_mark_dirty(ret);
        }
        break;
    case 32:
        ret = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, nWidth, nHeight);
        if (!ret)
            break;
        if (lpvBits)
        {
            unsigned char *buf = cairo_image_surface_get_data(ret);
            memcpy(buf, lpvBits, nWidth * 4 * nHeight);
            cairo_surface_mark_dirty(ret);
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
    if (dwMode == GRADIENT_FILL_TRIANGLE)
        return FALSE;
    PGRADIENT_RECT pGradientRect = (PGRADIENT_RECT)pMesh;
    // fill rect horz
    for (ULONG i = 0; i < nMeshElements; i++)
    {
        if (pGradientRect[i].UpperLeft >= nVertices || pGradientRect[i].LowerRight >= nVertices)
            return FALSE;
        TRIVERTEX *vertix0 = pVertices + pGradientRect[i].UpperLeft;
        TRIVERTEX *vertix1 = pVertices + pGradientRect[i].LowerRight;
        cairo_pattern_t *gradient = dwMode == GRADIENT_FILL_RECT_H ? cairo_pattern_create_linear(vertix0->x, vertix0->y, vertix1->x, vertix0->y) : // horz gradient
            cairo_pattern_create_linear(vertix0->x, vertix0->y, vertix0->x, vertix1->y);                                                           // vert gradient

        cairo_pattern_add_color_stop_rgba(gradient, 0, color16_to_double(vertix0->Red), color16_to_double(vertix0->Green), color16_to_double(vertix0->Blue), color16_to_double(vertix0->Alpha));
        cairo_pattern_add_color_stop_rgba(gradient, 1, color16_to_double(vertix1->Red), color16_to_double(vertix1->Green), color16_to_double(vertix1->Blue), color16_to_double(vertix1->Alpha));
        cairo_set_source(hdc->cairo, gradient);
        RECT rc = { vertix0->x, vertix0->y, vertix1->x, vertix1->y };
        NormalizeRect(&rc);
        cairo_rectangle(hdc->cairo, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        cairo_fill(hdc->cairo);
        cairo_pattern_destroy(gradient);
    }
    return TRUE;
}

int GetDeviceCaps(HDC hdc __attribute__((unused)), int cap)
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
    icon->hbmColor = RefGdiObj(piconinfo->hbmColor);
    icon->hbmMask = RefGdiObj(piconinfo->hbmMask);
    /* real Windows ignores the requested hotspot for icons (fIcon=TRUE)
       and forces it to the bitmap centre; only cursors keep the caller's
       hotspot (verified on real Windows: an 8x8 icon always reports 4,4) */
    if (piconinfo->fIcon)
    {
        BITMAP bm = {};
        if (icon->hbmColor && GetObject(icon->hbmColor, sizeof(bm), &bm))
        {
            icon->xHotspot = bm.bmWidth / 2;
            icon->yHotspot = bm.bmHeight / 2;
        }
        else if (icon->hbmMask && GetObject(icon->hbmMask, sizeof(bm), &bm))
        {
            icon->xHotspot = bm.bmWidth / 2;
            icon->yHotspot = (bm.bmHeight / 2) / 2;
        }
        else
        {
            icon->xHotspot = piconinfo->xHotspot;
            icon->yHotspot = piconinfo->yHotspot;
        }
    }
    else
    {
        icon->xHotspot = piconinfo->xHotspot;
        icon->yHotspot = piconinfo->yHotspot;
    }
    return icon;
}

BOOL DrawIcon(HDC hDC, int X, int Y, HICON hIcon)
{
    return DrawIconEx(hDC, X, Y, hIcon, -1, -1, 0, NULL, DI_NORMAL);
}

BOOL DrawIconEx(HDC hDC, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur __attribute__((unused)), HBRUSH hbrFlickerFreeDraw __attribute__((unused)), UINT diFlags __attribute__((unused)))
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

    HDC memdc = CreateCompatibleDC(hDC);
    HGDIOBJ oldBmp = SelectObject(memdc, hIcon->hbmColor);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hDC, xLeft, yTop, cxWidth, cyWidth, memdc, 0, 0, bm.bmWidth, bm.bmHeight, bf);
    SelectObject(memdc, oldBmp);
    DeleteDC(memdc);

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
    cairo_save(hdc->cairo);
    if (!ApplyFont(hdc))
        return FALSE;
    memset(txtMetric, 0, sizeof(TEXTMETRICA));
    cairo_font_extents_t font_extents;
    cairo_font_extents(hdc->cairo, &font_extents);
    txtMetric->tmAscent = font_extents.ascent;
    txtMetric->tmDescent = font_extents.descent;
    txtMetric->tmHeight = font_extents.height;

    cairo_text_extents_t txt_ext;
    cairo_text_extents(hdc->cairo, "x", &txt_ext);
    txtMetric->tmAveCharWidth = txt_ext.width;
    txtMetric->tmExternalLeading = txt_ext.x_bearing;
    txtMetric->tmDigitizedAspectX = 100;
    txtMetric->tmDigitizedAspectY = 100;

    LOGFONT *logFont = (LOGFONT *)GetGdiObjPtr(hdc->hfont);
    txtMetric->tmItalic = logFont->lfItalic;
    txtMetric->tmUnderlined = logFont->lfUnderline;
    txtMetric->tmStruckOut = logFont->lfStrikeOut;
    txtMetric->tmCharSet = logFont->lfCharSet;
    txtMetric->tmPitchAndFamily = logFont->lfPitchAndFamily;

    cairo_restore(hdc->cairo);
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

// Real Win32 GetTextFace semantics (probed on Windows): with a NULL buffer it
// returns the required size *including* the NUL; with a buffer it copies at
// most nCount-1 characters plus the NUL and returns the number of characters
// copied *excluding* the NUL (truncation instead of failure).
int GetTextFaceA(HDC hdc, int nCount, LPSTR lpFaceName)
{
    assert(hdc->hfont);
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    int len = strlen(lf->lfFaceName);
    if (!lpFaceName)
        return len + 1;
    if (nCount <= 0)
        return 0;
    int copy = len < nCount - 1 ? len : nCount - 1;
    memcpy(lpFaceName, lf->lfFaceName, copy);
    lpFaceName[copy] = '\0';
    return copy;
}

int GetTextFaceW(HDC hdc, int nCount, LPWSTR lpFaceName)
{
    assert(hdc->hfont);
    LOGFONTA *lf = (LOGFONTA *)GetGdiObjPtr(hdc->hfont);
    wchar_t face[LF_FACESIZE];
    int len = MultiByteToWideChar(CP_UTF8, 0, lf->lfFaceName, -1, face, LF_FACESIZE);
    if (len <= 0)
        return 0;
    len -= 1; // exclude the NUL
    if (!lpFaceName)
        return len + 1;
    if (nCount <= 0)
        return 0;
    int copy = len < nCount - 1 ? len : nCount - 1;
    memcpy(lpFaceName, face, copy * sizeof(wchar_t));
    lpFaceName[copy] = 0;
    return copy;
}

BOOL Polygon_Priv(HDC hdc, const POINT *apt, int cpt)
{
    if (!hdc || !hdc->cairo || cpt < 2)
        return FALSE;

    cairo_t *ctx = hdc->cairo;

    cairo_move_to(ctx, apt[0].x, apt[0].y);
    for (int i = 1; i < cpt; i++)
    {
        cairo_line_to(ctx, apt[i].x, apt[i].y);
    }
    cairo_close_path(ctx);

    if (!hdc->pathRecording)
    {
        cairo_save(ctx);
        double x1, y1, x2, y2;
        cairo_path_extents(ctx, &x1,&y1, &x2,&y2);
        cairo_fill_rule_t mode = hdc->polyFillMode == ALTERNATE ? CAIRO_FILL_RULE_EVEN_ODD : CAIRO_FILL_RULE_WINDING;
        cairo_set_fill_rule(ctx, mode);
        DrawPathFillStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
        cairo_restore(ctx);
    }

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

COLORREF WINAPI GetNearestColor(HDC hdc __attribute__((unused)),         // handle to DC
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
    cairo_save(hdc->cairo);
    if (lprc)
    {
        if (fuOptions & ETO_CLIPPED)
        {
            cairo_rectangle(hdc->cairo, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
            cairo_clip(hdc->cairo);
        }
        if (fuOptions & ETO_OPAQUE)
        {
            CairoColor cr(hdc->crBk);
            cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
            cairo_rectangle(hdc->cairo, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
            cairo_fill(hdc->cairo);
        }
    }
    if (!lpDx)
    {
        TextOutA(hdc, X, Y, lpString, cbCount);
    }
    else
    {
        ApplyFont(hdc);
        CairoColor cr(hdc->crText);
        cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);

        cairo_text_extents_t ext;
        int glyph_num = cairo_text_extents2(hdc->cairo, lpString, cbCount, &ext);
        int wid = 0;
        for (int i = 0; i < glyph_num; i++)
        {
            wid += lpDx[i];
        }
        double old_x, old_y;
        cairo_get_current_point(hdc->cairo, &old_x, &old_y);

        cairo_font_extents_t font_ext;
        cairo_font_extents(hdc->cairo, &font_ext);

        double x = X, y = Y;
        // Win32: with TA_UPDATECP the x/y parameters are ignored and drawing
        // starts at the current position (see TextOutA).
        if (hdc->textAlign & TA_UPDATECP)
        {
            cairo_get_current_point(hdc->cairo, &x, &y);
        }
        switch (hdc->textAlign & (TA_RIGHT | TA_CENTER))
        {
        case TA_RIGHT:
            x -= wid;
            break;
        case TA_CENTER:
            x -= wid / 2;
            break;
        }
        switch (hdc->textAlign & (TA_BASELINE | TA_BOTTOM | TA_TOP))
        {
        case TA_TOP:
            y += font_ext.ascent;
            break;
        case TA_BASELINE:
            break;
        case TA_BOTTOM:
            y -= font_ext.descent;
            break;
        }

        const char *p = lpString;
        for (int i = 0; i < glyph_num; i++)
        {
            int charLen = swinx::UTF8CharLength(*p);
            cairo_move_to(hdc->cairo, x, y);

            // If recording path, add text outline to path
            if (hdc->pathRecording)
            {
                cairo_text_path2(hdc->cairo, p, charLen);
            }
            else
            {
                cairo_show_text2(hdc->cairo, p, charLen);
            }

            x += lpDx[i];
            p += charLen;
        }
        if ((hdc->textAlign & TA_UPDATECP) == 0)
        {
            cairo_move_to(hdc->cairo, old_x, old_y);
        }
    }
    cairo_restore(hdc->cairo);
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
    swinx_stl::string str;
    tostring(lpString, cbCount, str);
    return ExtTextOutA(hdc, X, Y, fuOptions, lprc, str.c_str(), str.length(), lpDx);
}

Antialias WINAPI GetAntialiasMode(HDC hdc)
{
    return (Antialias)cairo_get_antialias(hdc->cairo);
}

Antialias WINAPI SetAntialiasMode(HDC hdc, Antialias mode)
{
    Antialias ret = (Antialias)cairo_get_antialias(hdc->cairo);
    cairo_set_antialias(hdc->cairo, (cairo_antialias_t)mode);
    return ret;
}

static unsigned char *getPixelData(HDC hdc, int x, int y)
{
    cairo_t *cr = hdc->cairo;
    if (!hdc->bmp)
        return nullptr;
    cairo_matrix_t mtx;
    cairo_get_matrix(cr, &mtx);
    double dx(x), dy(y);
    cairo_matrix_transform_point(&mtx, &dx, &dy);
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(hdc->bmp);
    cairo_format_t fmt = cairo_image_surface_get_format(surface);
    if (fmt != CAIRO_FORMAT_ARGB32)
        return nullptr;
    int wid = cairo_image_surface_get_width(surface);
    int hei = cairo_image_surface_get_height(surface);
    if (dx >= wid || dy >= hei)
        return nullptr;
    unsigned char *data = cairo_image_surface_get_data(surface);
    int offset = (dy * wid + dx) * 4;
    return data + offset;
}

COLORREF GetPixel(IN HDC hdc, IN int x, IN int y)
{
    const unsigned char *data = getPixelData(hdc, x, y);
    if (!data)
        return 0;
    // CAIRO_FORMAT_ARGB32 stores premultiplied BGRA in memory on
    // little-endian; un-premultiply to get the straight COLORREF back.
    unsigned int b = data[0];
    unsigned int g = data[1];
    unsigned int r = data[2];
    unsigned int a = data[3];
    if (a != 0xFF)
    {
        if (a == 0)
            return 0;
        r = r * 255 / a;
        g = g * 255 / a;
        b = b * 255 / a;
    }
    return RGBA(r, g, b, a);
}

COLORREF SetPixel(IN HDC hdc, IN int x, IN int y, IN COLORREF color)
{
    COLORREF ret = GetPixel(hdc, x, y);
    CairoColor cr(color);
    cairo_save(hdc->cairo);
    cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
    cairo_antialias_t old = cairo_get_antialias(hdc->cairo);
    cairo_set_antialias(hdc->cairo, CAIRO_ANTIALIAS_NONE);
    cairo_rectangle(hdc->cairo, x, y, 1.0, 1.0);
    cairo_fill(hdc->cairo);
    cairo_set_antialias(hdc->cairo, old);
    cairo_restore(hdc->cairo);
    return ret;
}

UINT WINAPI RealizePalette(_In_ HDC hdc __attribute__((unused)))
{
    return 0;
}

HPALETTE WINAPI SelectPalette(_In_ HDC hdc __attribute__((unused)), _In_ HPALETTE hPal __attribute__((unused)), _In_ BOOL bForceBkgd __attribute__((unused)))
{
    return nullptr;
}

BOOL WINAPI DPtoLP(HDC hdc,          // handle to device context
                   LPPOINT lpPoints, // array of points
                   int nCount        // count of points in array
)
{
    if (!hdc || !lpPoints || nCount <= 0)
        return FALSE;
    cairo_matrix_t mtx, inv;
    cairo_get_matrix(hdc->cairo, &mtx);
    inv = mtx;
    if (cairo_matrix_invert(&inv) != CAIRO_STATUS_SUCCESS)
        return FALSE; // non-invertible transform
    for (int i = 0; i < nCount; i++)
    {
        double x = lpPoints[i].x, y = lpPoints[i].y;
        cairo_matrix_transform_point(&inv, &x, &y);
        lpPoints[i].x = (int)floor(x + 0.5);
        lpPoints[i].y = (int)floor(y + 0.5);
    }
    return TRUE;
}

BOOL WINAPI LPtoDP(HDC hdc,          // handle to device context
                   LPPOINT lpPoints, // array of points
                   int nCount        // count of points in array
)
{
    if (!hdc || !lpPoints || nCount <= 0)
        return FALSE;
    cairo_matrix_t mtx;
    cairo_get_matrix(hdc->cairo, &mtx); // user (logical) -> device
    for (int i = 0; i < nCount; i++)
    {
        double x = lpPoints[i].x, y = lpPoints[i].y;
        cairo_matrix_transform_point(&mtx, &x, &y);
        lpPoints[i].x = (int)floor(x + 0.5);
        lpPoints[i].y = (int)floor(y + 0.5);
    }
    return TRUE;
}

BOOL WINAPI GetCharWidthA(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    if (!hdc || !lpBuffer || iFirst > iLast || iLast > 0x10FFFF)
        return FALSE;
    for (UINT c = iFirst; c <= iLast; c++)
    {
        // swinx's "A" APIs treat strings as UTF-8 (see TextOutA/
        // GetTextExtentPoint32A): encode the code point to a whole UTF-8
        // character via uniconv before measuring it.
        uint32_t uch = c;
        char buf[4];
        /* the NUL code point has no glyph; guard it explicitly since
           UTF8FromUTF32 now converts embedded NULs like Win32 does */
        size_t len = (uch == 0) ? 0 : swinx::UTF8FromUTF32(&uch, 1, buf, 4);
        int width = 0;
        if (len > 0)
        {
            SIZE sz;
            if (!GetTextExtentPoint32A(hdc, buf, (int)len, &sz))
                return FALSE;
            width = sz.cx;
        }
        // len == 0 only for the NUL code point, which has no glyph.
        lpBuffer[c - iFirst] = width;
    }
    return TRUE;
}
BOOL WINAPI GetCharWidthW(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer)
{
    if (!hdc || !lpBuffer || iFirst > iLast)
        return FALSE;
    for (UINT c = iFirst; c <= iLast; c++)
    {
        SIZE sz;
        WCHAR ch = (WCHAR)c;
        if (!GetTextExtentPoint32W(hdc, &ch, 1, &sz))
            return FALSE;
        lpBuffer[c - iFirst] = sz.cx;
    }
    return TRUE;
}

HDC WINAPI CreateICA(LPCSTR lpszDriver __attribute__((unused)),    // driver name
                     LPCSTR lpszDevice __attribute__((unused)),    // device name
                     LPCSTR lpszOutput __attribute__((unused)),    // port or file name
                     CONST void *lpdvmInit __attribute__((unused)) // optional initialization data
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
    swinx_stl::string strDriver, strDevice, strOutput;
    tostring(lpszDriver, -1, strDriver);
    tostring(lpszDevice, -1, strDevice);
    tostring(lpszOutput, -1, strOutput);
    return CreateICA(strDriver.c_str(), strDevice.c_str(), strOutput.c_str(), lpdvmInit);
}

#if defined(CAIRO_HAS_QUARTZ_FONT) && CAIRO_HAS_QUARTZ_FONT
extern int macos_register_font(const char *path);
#endif
int AddFontResourceExA(LPCSTR lpszFilename, // font file name
                       DWORD fl __attribute__((unused)),            // font characteristics
                       PVOID pdv __attribute__((unused))            // reserved
)
{
    int ret = 0;
    do
    {
        // 获取当前的 Fontconfig 配置
        FcConfig *config = FcConfigGetCurrent();
        if (!config)
        {
            SLOG_STMW() << "Failed to get current Fontconfig configuration";
            break;
        }

        // 添加字体File
        if (!FcConfigAppFontAddFile(config, (const FcChar8 *)lpszFilename))
        {
            SLOG_STMW() << "Failed to add font directory:" << lpszFilename;
            break;
        }

        // 应用新的配置
        FcConfigSetCurrent(config);

        // 更新字体缓存
        FcConfigBuildFonts(config);
        ret = 1;
    } while (false);
#if defined(CAIRO_HAS_QUARTZ_FONT) && CAIRO_HAS_QUARTZ_FONT
    return macos_register_font(lpszFilename);
#else
    return ret;
#endif // CAIRO_HAS_QUARTZ_FONT
}

int AddFontResourceA(LPCSTR lpszFilename)
{
    return AddFontResourceExA(lpszFilename, 0, 0);
}

int AddFontResourceW(LPCWSTR lpszFilename)
{
    swinx_stl::string str;
    tostring(lpszFilename, -1, str);
    return AddFontResourceExA(str.c_str(), 0, 0);
}

int AddFontResourceExW(LPCWSTR lpszFilename, // font file name
                       DWORD fl,             // font characteristics
                       PVOID pdv             // reserved
)
{
    swinx_stl::string str;
    tostring(lpszFilename, -1, str);
    return AddFontResourceExA(str.c_str(), fl, pdv);
}

// ========================================================================
// Path API Implementation
// ========================================================================

BOOL BeginPath(HDC hdc)
{
    if (!hdc || !hdc->cairo)
        return FALSE;

    // If already recording, abort current path
    if (hdc->pathRecording && hdc->currentPath)
    {
        cairo_path_destroy(hdc->currentPath);
        hdc->currentPath = nullptr;
    }

    // Save current point before clearing path
    double current_x, current_y;
    bool has_current_point = cairo_has_current_point(hdc->cairo);
    if (has_current_point)
        cairo_get_current_point(hdc->cairo, &current_x, &current_y);

    // Start new path recording
    cairo_new_path(hdc->cairo);

    // Restore current point if it existed
    if (has_current_point)
        cairo_move_to(hdc->cairo, current_x, current_y);

    hdc->pathRecording = TRUE;

    return TRUE;
}

BOOL EndPath(HDC hdc)
{
    if (!hdc || !hdc->cairo || !hdc->pathRecording)
        return FALSE;

    // Copy current path from cairo context
    if (hdc->currentPath)
        cairo_path_destroy(hdc->currentPath);

    hdc->currentPath = cairo_copy_path(hdc->cairo);
    hdc->pathRecording = FALSE;

    return hdc->currentPath && hdc->currentPath->status == CAIRO_STATUS_SUCCESS;
}

BOOL AbortPath(HDC hdc)
{
    if (!hdc)
        return FALSE;

    // Clean up current path
    if (hdc->currentPath)
    {
        cairo_path_destroy(hdc->currentPath);
        hdc->currentPath = nullptr;
    }

    hdc->pathRecording = FALSE;

    // Clear cairo path
    if (hdc->cairo)
        cairo_new_path(hdc->cairo);

    return TRUE;
}

BOOL CloseFigure(HDC hdc)
{
    if (!hdc || !hdc->cairo)
        return FALSE;

    // Close current figure in cairo path
    cairo_close_path(hdc->cairo);

    return TRUE;
}

BOOL StrokePath(HDC hdc)
{
    if (!hdc || !hdc->cairo || !hdc->currentPath)
        return FALSE;

    cairo_save(hdc->cairo);
    cairo_new_path(hdc->cairo);
    cairo_append_path(hdc->cairo, hdc->currentPath);
    double x1, y1, x2, y2;
    cairo_path_extents(hdc->cairo, &x1,&y1, &x2,&y2);
    DrawPathStroke(hdc->cairo, hdc, x2-x1, y2-y1, x1, y1);
    cairo_restore(hdc->cairo);

    cairo_path_destroy(hdc->currentPath);
    hdc->currentPath = nullptr;

    return TRUE;
}

BOOL FillPath(HDC hdc)
{
    if (!hdc || !hdc->cairo || !hdc->currentPath)
        return FALSE;

    cairo_save(hdc->cairo);
    cairo_new_path(hdc->cairo);
    cairo_append_path(hdc->cairo, hdc->currentPath);

    double x1, y1, x2, y2;
    cairo_path_extents(hdc->cairo, &x1, &y1, &x2, &y2);
    double width = x2 - x1;
    double height = y2 - y1;
    if (ApplyBrush(hdc, hdc->brush, width, height, x1, y1))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_fill(hdc->cairo);
    }

    cairo_restore(hdc->cairo);

    cairo_path_destroy(hdc->currentPath);
    hdc->currentPath = nullptr;

    return TRUE;
}

BOOL StrokeAndFillPath(HDC hdc)
{
    if (!hdc || !hdc->cairo || !hdc->currentPath)
        return FALSE;

    cairo_save(hdc->cairo);
    cairo_new_path(hdc->cairo);
    cairo_append_path(hdc->cairo, hdc->currentPath);

    double x1, y1, x2, y2;
    cairo_path_extents(hdc->cairo, &x1, &y1, &x2, &y2);
    double width = x2 - x1;
    double height = y2 - y1;

    DrawPathFillStroke(hdc->cairo, hdc, width, height, x1, y1);

    cairo_restore(hdc->cairo);

    cairo_path_destroy(hdc->currentPath);
    hdc->currentPath = nullptr;

    return TRUE;
}

HRGN PathToRegion(HDC hdc)
{
    if (!hdc || !hdc->cairo || !hdc->currentPath)
        return nullptr;
    cairo_path_t *path = hdc->currentPath;
    cairo_save(hdc->cairo);
    cairo_reset_clip(hdc->cairo);
    cairo_append_path(hdc->cairo, path);
    cairo_clip(hdc->cairo);
    HRGN hrgn = CreateRectRgn(0, 0, 0, 0);
    // must read the clip BEFORE cairo_restore() undoes it — after the
    // restore the clip is unbounded again and GetClipRgn would leave the
    // region empty (reported as an all-zero box by GetRgnBox)
    GetClipRgn(hdc, hrgn);
    cairo_restore(hdc->cairo);
    cairo_path_destroy(hdc->currentPath);
    hdc->currentPath = nullptr;
    return hrgn;
}

BOOL SelectClipPath(HDC hdc, int mode)
{
    if (!hdc || !hdc->cairo || !hdc->currentPath)
        return FALSE;

    // Save the current path to work with it
    cairo_path_t *saved_path = hdc->currentPath;
    hdc->currentPath = nullptr;

    switch (mode)
    {
    case RGN_AND:
        // Intersect with current clip
        cairo_append_path(hdc->cairo, saved_path);
        cairo_clip(hdc->cairo);
        break;

    case RGN_OR:
    {
        // Union: new clip = current clip union new path
        // Use cairo rectangle list approach for better performance
        cairo_rectangle_list_t *clip_rects = cairo_copy_clip_rectangle_list(hdc->cairo);
        cairo_reset_clip(hdc->cairo);

        // Add current clip rectangles to path
        cairo_rectangle_t *prect = clip_rects->rectangles;
        for (int i = 0; i < clip_rects->num_rectangles; i++, prect++)
        {
            cairo_rectangle(hdc->cairo, prect->x, prect->y, prect->width, prect->height);
        }
        cairo_rectangle_list_destroy(clip_rects);
        // Add the new path
        cairo_append_path(hdc->cairo, saved_path);

        // Apply combined clip (winding fill rule includes all sub-paths)
        cairo_clip(hdc->cairo);
        break;
    }
    case RGN_XOR:
    {
        HRGN hrgn1 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn1);
        cairo_reset_clip(hdc->cairo);
        cairo_append_path(hdc->cairo, saved_path);
        cairo_clip(hdc->cairo);
        HRGN hrgn2 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn2);
        CombineRgn(hrgn1, hrgn1, hrgn2, RGN_XOR);
        SelectClipRgn(hdc, hrgn1);
        DeleteObject(hrgn1);
        DeleteObject(hrgn2);
        break;
    }

    case RGN_DIFF:
    {
        HRGN hrgn1 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn1);
        cairo_reset_clip(hdc->cairo);
        cairo_append_path(hdc->cairo, saved_path);
        cairo_clip(hdc->cairo);
        HRGN hrgn2 = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, hrgn2);
        CombineRgn(hrgn1, hrgn1, hrgn2, RGN_DIFF);
        SelectClipRgn(hdc, hrgn1);
        DeleteObject(hrgn1);
        DeleteObject(hrgn2);
        break;
    }

    case RGN_COPY:
    default:
    {
        // Replace current clip with new path
        cairo_reset_clip(hdc->cairo);
        cairo_append_path(hdc->cairo, saved_path);
        cairo_clip(hdc->cairo);
        break;
    }
    }
    cairo_path_destroy(saved_path);
    return TRUE;
}

int GetPath(HDC hdc, LPPOINT lpPoints, LPBYTE lpTypes, int nSize)
{
    if (!hdc || !hdc->currentPath)
        return -1;

    cairo_path_t *path = hdc->currentPath;
    if (path->status != CAIRO_STATUS_SUCCESS)
        return -1;

    // Count the number of points needed
    int pointCount = 0;
    for (int i = 0; i < path->num_data; i += path->data[i].header.length)
    {
        cairo_path_data_t *data = &path->data[i];
        switch (data->header.type)
        {
        case CAIRO_PATH_MOVE_TO:
        case CAIRO_PATH_LINE_TO:
            pointCount++;
            break;
        case CAIRO_PATH_CURVE_TO:
            pointCount += 3; // 3 control points
            break;
        case CAIRO_PATH_CLOSE_PATH:
            // No additional points
            break;
        }
    }

    // If just querying size, return point count
    if (!lpPoints || !lpTypes || nSize == 0)
        return pointCount;

    // If buffer too small, return required size
    if (nSize < pointCount)
        return pointCount;

    // Fill the arrays
    int pointIndex = 0;
    for (int i = 0; i < path->num_data; i += path->data[i].header.length)
    {
        cairo_path_data_t *data = &path->data[i];
        switch (data->header.type)
        {
        case CAIRO_PATH_MOVE_TO:
            if (pointIndex < nSize)
            {
                lpPoints[pointIndex].x = (LONG)data[1].point.x;
                lpPoints[pointIndex].y = (LONG)data[1].point.y;
                lpTypes[pointIndex] = PT_MOVETO;
                pointIndex++;
            }
            break;
        case CAIRO_PATH_LINE_TO:
            if (pointIndex < nSize)
            {
                lpPoints[pointIndex].x = (LONG)data[1].point.x;
                lpPoints[pointIndex].y = (LONG)data[1].point.y;
                lpTypes[pointIndex] = PT_LINETO;
                pointIndex++;
            }
            break;
        case CAIRO_PATH_CURVE_TO:
            for (int j = 1; j <= 3 && pointIndex < nSize; j++, pointIndex++)
            {
                lpPoints[pointIndex].x = (LONG)data[j].point.x;
                lpPoints[pointIndex].y = (LONG)data[j].point.y;
                lpTypes[pointIndex] = PT_BEZIERTO;
            }
            break;
        case CAIRO_PATH_CLOSE_PATH:
            // Mark previous point as close figure
            if (pointIndex > 0)
                lpTypes[pointIndex - 1] |= PT_CLOSEFIGURE;
            break;
        }
    }

    return pointIndex;
}

BOOL PolyDraw(HDC hdc, LPPOINT lppt, LPBYTE lpbTypes, int cpt)
{
    if (!hdc || !lppt || !lpbTypes || cpt <= 0)
        return FALSE;

    cairo_t *ctx = hdc->cairo;
    for (int i = 0; i < cpt; i++)
    {
        BYTE type = lpbTypes[i];
        POINT pt = lppt[i];

        if (type & PT_MOVETO)
        {
            cairo_move_to(ctx, pt.x, pt.y);
        }
        else if (type & PT_LINETO)
        {
            cairo_line_to(ctx, pt.x, pt.y);
        }
        else if (type & PT_BEZIERTO)
        {
            if (i + 2 < cpt && (lpbTypes[i + 1] & PT_BEZIERTO) && (lpbTypes[i + 2] & PT_BEZIERTO))
            {
                POINT pt1 = lppt[i + 1];
                POINT pt2 = lppt[i + 2];
                cairo_curve_to(ctx, pt.x, pt.y, pt1.x, pt1.y, pt2.x, pt2.y);
                i += 2;
            }
        }

        if (type & PT_CLOSEFIGURE)
        {
            cairo_close_path(ctx);
        }
    }

    if (hdc->pathRecording)
        return TRUE;

    double x1, y1, x2, y2;
    cairo_path_extents(ctx, &x1, &y1, &x2, &y2);
    cairo_save(ctx);
    DrawPathStroke(ctx, hdc, x2-x1, y2-y1, x1, y1);
    cairo_restore(ctx);

    return TRUE;
}

BOOL SetMiterLimit(HDC hdc, FLOAT eNewLimit, PFLOAT peOldLimit)
{
    if (!hdc)
        return FALSE;
    double oldLimit = cairo_get_miter_limit(hdc->cairo);
    if (peOldLimit)
        *peOldLimit = (FLOAT)oldLimit;
    // Apply to cairo context if available
    assert(hdc->cairo);
    cairo_set_miter_limit(hdc->cairo, eNewLimit);
    return TRUE;
}

BOOL GetMiterLimit(HDC hdc, PFLOAT peLimit)
{
    if (!hdc || !peLimit)
        return FALSE;
    assert(hdc->cairo);
    *peLimit = (FLOAT)cairo_get_miter_limit(hdc->cairo);
    return TRUE;
}
