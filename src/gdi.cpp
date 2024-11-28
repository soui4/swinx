#include <windows.h>
#include <gdi.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <xcb/xcb_aux.h>
#include <math.h>
#include <png.h>
#include <assert.h>
#include <vector>
#include "handle.h"
#include "sdc.h"
#include "SConnection.h"
#include "cairo_show_text2.h"
#include "drawtext.h"
#include "tostring.hpp"
#include "uniconv.h"

#define SASSERT assert

static bool DumpBmp(HBITMAP bmp, const char *path)
{
    if (!bmp)
        return false;
    return CAIRO_STATUS_SUCCESS == cairo_surface_write_to_png((cairo_surface_t *)GetGdiObjPtr(bmp), path);
}

struct CairoColor
{
    double r, g, b, a;
    CairoColor(COLORREF crSrc){
        r = GetRValue(crSrc) / 255.0;
        g = GetGValue(crSrc) / 255.0;
        b = GetBValue(crSrc) / 255.0;
        a = GetAValue(crSrc) / 255.0;
    }
};

struct LOGPENEX : LOGPEN
{
    std::vector<double> dash;
};


static void gdi_bmp_free(void *ptr)
{
    cairo_surface_destroy((cairo_surface_t *)ptr);
}

static void gdi_pen_free(void *ptr)
{
    LOGPEN *lp = (LOGPEN *)ptr;
    if (lp->lopnStyle == PS_USERSTYLE)
        delete (LOGPENEX *)ptr;
    else
        delete (LOGPEN *)ptr;
}

struct GradientDetail
{
    std::vector<GRADIENTITEM> items;
    GRADIENTINFO info;
};
struct PatternInfo
{
    TILEMODE tileMode;
    BOOL useBmp;
    double alpha;
    union {
        HBITMAP bmp;
        GradientDetail *graidentDetail;
    } data;

    cairo_pattern_t *pattern;

    double width, height;
    PatternInfo()
        : pattern(nullptr)
        , width(0)
        , height(0)
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
            delete data.graidentDetail;
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
        if (fequal(angle, 90.0f) || fequal(angle, 270.0f))
        { // 90度
            skPts[0].set(halfWid, 0.0f);
            skPts[1].set(halfWid, hei);
        }
        else if (fequal(angle, 0.0f) || fequal(angle, 180.0f))
        { //水平方向
            skPts[0].set(0.f, halfHei);
            skPts[1].set(wid, halfHei);
        }
        else
        { //其它角度

            float angleInRadians = M_PI * angle / 180;
            double tanAngle = tan(angleInRadians);

            fPoint pt1a, pt2a; //与左右两条边相交的位置
            fPoint pt1b, pt2b; //与上下两条边相关的位置

            pt1a.fX = -halfWid, pt2a.fX = halfWid;
            pt1b.fY = -halfHei, pt2b.fY = halfHei;

            pt1a.fY = (-halfWid * tanAngle);
            pt2a.fY = -pt1a.fY;

            pt1b.fX = (-halfHei / tanAngle);
            pt2b.fX = -pt1b.fX;

            if (pt2a.fY > halfHei)
            { // using pt1a,pt2a
                skPts[0] = pt1a;
                skPts[1] = pt2a;
            }
            else
            { // using pt1b,pt2b
                skPts[0] = pt1b;
                skPts[1] = pt2b;
            }
            skPts[0].offset(halfWid, halfHei);
            skPts[1].offset(halfWid, halfHei);
        }
    }

    cairo_pattern_t *create(double wid, double hei)
    {
        if (pattern)
        {
            if (wid == this->width && hei == this->height)
                return pattern;
            cairo_pattern_destroy(pattern);
            pattern = nullptr;
        }
        cairo_pattern_t *ret = nullptr;
        if (useBmp)
        {
            ret = cairo_pattern_create_for_surface((cairo_surface_t *)GetGdiObjPtr(data.bmp));
        }
        else
        {
            assert(data.graidentDetail);
            switch (data.graidentDetail->info.type)
            {
            case grad_linear:
            {
                fPoint endPts[2];
                calc_linear_endpoint(data.graidentDetail->info.angle, wid, hei, endPts);
                ret = cairo_pattern_create_linear(endPts[0].fX, endPts[0].fY, endPts[1].fX, endPts[1].fY);
            }
            break;
            case grad_radial:
            {
                float centerX = wid * data.graidentDetail->info.radial.centerX;
                float centerY = hei * data.graidentDetail->info.radial.centerY;
                ret = cairo_pattern_create_radial(centerX, centerY, 0, centerX, centerY, data.graidentDetail->info.radial.radius);
            }
            break;
            default:
                return nullptr;
            }
            for (uint32_t i = 0, cnt = data.graidentDetail->items.size(); i < cnt; i++)
            {
                CairoColor cr(data.graidentDetail->items[i].cr);
                cr.a *= alpha;
                cairo_pattern_add_color_stop_rgba(ret, data.graidentDetail->items[i].pos, cr.r, cr.g, cr.b, cr.a);
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
    delete (LOGFONT *)ptr;
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

static bool ApplyPen(cairo_t *ctx, HPEN hpen)
{
    LOGPEN *pen = (LOGPEN *)GetGdiObjPtr(hpen);
    if (pen->lopnStyle == PS_NULL)
        return false;
    cairo_set_line_width(ctx, pen->lopnWidth.x);
    CairoColor cr(pen->lopnColor);
    cairo_set_source_rgba(ctx, cr.r, cr.g, cr.b, cr.a);
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

//must call after ApplyPen, ApplyBrush
static void ApplyRop2(cairo_t* cr, int rop2) {
    switch (rop2) {
    case R2_BLACK:
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        break;
    case R2_WHITE: 
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        break;
    case R2_NOT:
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);       
        break;
    case R2_NOP:
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
        break;
    case R2_COPYPEN:
        //default, do nothing
        break;
    default:
        printf("not supported rop2: %d\n", rop2);
        break;
    }
}

static bool IsNullBrush(HPEN hbr)
{
    LOGBRUSH *br = (LOGBRUSH *)GetGdiObjPtr(hbr);
    return br->lbStyle == BS_NULL;
}

static bool IsPatternBrush(HBRUSH hbr)
{
    LOGBRUSH *br = (LOGBRUSH *)GetGdiObjPtr(hbr);
    return br->lbStyle == BS_PATTERN;
}

static bool ApplyBrush(cairo_t *ctx, HBRUSH hbr, double wid, double hei)
{
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
        cairo_pattern_t *pattern = info->create(wid, hei);
        cairo_set_source(ctx, pattern);
        break;
    }
    default:
        ret = false;
    }
    return ret;
}

static BOOL ApplyFont(HDC hdc)
{
    if (hdc->hfont)
    {
        LOGFONT *lf = (LOGFONT *)GetGdiObjPtr(hdc->hfont);
        cairo_select_font_face(hdc->cairo, lf->lfFaceName, lf->lfItalic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, lf->lfWeight > 400 ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);

        cairo_set_font_size(hdc->cairo, abs(lf->lfHeight));
        return TRUE;
    }
    return FALSE;
}

static void ApplyRegion(cairo_t *ctx, HRGN hRgn)
{
    cairo_reset_clip(ctx);
    DWORD dwCount = GetRegionData(hRgn, 0, nullptr);
    RGNDATA *pData = (RGNDATA *)malloc(dwCount);
    GetRegionData(hRgn, dwCount, pData);
    RECT *pRc = (RECT *)pData->Buffer;
    for (int i = 0; i < pData->rdh.nCount; i++)
    {
        cairo_rectangle(ctx, pRc->left, pRc->top, pRc->right - pRc->left, pRc->bottom - pRc->top);
        pRc++;
    }
    free(pData);
    cairo_clip(ctx);
}

HPEN ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH *plbrush, DWORD cStyle, const DWORD *pstyle)
{
    if (plbrush->lbStyle != PS_SOLID || (cStyle > 0 && !pstyle))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LOGPENEX *lb = new LOGPENEX;
    lb->lopnColor = plbrush->lbColor;
    lb->lopnStyle = cStyle > 0 ? PS_USERSTYLE : PS_SOLID;
    lb->lopnWidth.x = cWidth;
    if (cStyle > 0)
    {
        lb->dash.resize(cStyle);
        for (DWORD i = 0; i < cStyle; i++)
        {
            lb->dash[i] = pstyle[i];
        }
    }
    return InitGdiObj(OBJ_PEN, lb);
}

int GetObjectW(HGDIOBJ h, int c, LPVOID pv) {
    if (h->type == OBJ_FONT) {
        if (c < sizeof(LOGFONTW))
            return 0;
        LOGFONTA lf;
        GetObjectA(h, sizeof(lf), &lf);
        LOGFONTW* lfw = (LOGFONTW*)pv;
        memcpy(lfw, &lf, FIELD_OFFSET(LOGFONTW, lfFaceName));
        MultiByteToWideChar(CP_UTF8, 0, lf.lfFaceName, -1, lfw->lfFaceName, LF_FACESIZE);
        return c;
    }
    else {
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
        if (c >= sizeof(BITMAP))
        {
            BITMAP *bm = (BITMAP *)pv;
            cairo_surface_t *pixmap = (cairo_surface_t *)h->ptr;
            cairo_format_t fmt = cairo_image_surface_get_format(pixmap);
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
            default:
                assert(0);
                break;
            }

            bm->bmWidthBytes = bm->bmWidth * bm->bmBitsPixel / 8;
            bm->bmType = BI_RGB;
            bm->bmBits = cairo_image_surface_get_data(pixmap);
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
    LOGPEN logPen = { (UINT)iStyle, {cWidth,0}, color };
    return CreatePenIndirect(&logPen);
}

HPEN CreatePenIndirect(const LOGPEN *plpen)
{
    LOGPEN *pData = new LOGPEN;
    memcpy(pData, plpen, sizeof(LOGPEN));
    return InitGdiObj(OBJ_PEN, pData);
}

HFONT CreateFontIndirectA(const LOGFONTA *lplf)
{
    LOGFONTA *plog = new LOGFONTA;
    memcpy(plog, lplf, sizeof(LOGFONTA));
    return InitGdiObj(OBJ_FONT, plog);
}

HFONT CreateFontIndirectW(CONST LOGFONTW* lplf) {
    LOGFONTA lf;
    memcpy(&lf, lplf, FIELD_OFFSET(LOGFONTA, lfFaceName));
    WideCharToMultiByte(CP_UTF8, 0,  lplf->lfFaceName, -1, lf.lfFaceName, LF_FACESIZE, nullptr, nullptr);
    return CreateFontIndirectA(&lf);
}

HFONT CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
    LOGFONT lf;
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
    return CreateFontIndirect(&lf);
}

HFONT CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName) {
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
        UpdateDIBPixmap(bmp, pbmi->bmiHeader.biWidth, pbmi->bmiHeader.biHeight, pbmi->bmiHeader.biBitCount, pbmi->bmiHeader.biWidth * pbmi->bmiHeader.biBitCount / 8, pjBits);
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
    info->data.graidentDetail = new GradientDetail;
    info->data.graidentDetail->info = *grad_info;
    info->data.graidentDetail->items.resize(nCount);
    memcpy(info->data.graidentDetail->items.data(), pGradients, nCount * sizeof(GRADIENTITEM));
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
    cairo_format_t fmt = CAIRO_FORMAT_INVALID;
    switch (lpbmi->bmiHeader.biBitCount)
    {
    case 1:
        fmt = CAIRO_FORMAT_A1;
        break;
    case 32:
        fmt = CAIRO_FORMAT_ARGB32;
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

BOOL UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits)
{
    BITMAP bm = { 0 };
    GetObject(bmp, sizeof(bm), &bm);
    if (!bm.bmBits)
        return FALSE;
    if (bm.bmWidth != wid || bm.bmHeight != hei || bm.bmBitsPixel != bitsPixel)
        return FALSE;
    if (pjBits)
        memcpy(bm.bmBits, pjBits, hei * stride);
    else
        memset(bm.bmBits, 0, hei * stride);
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
    SConnection *conn=nullptr;
    if (hdc == 0)
    {
        conn = SConnMgr::instance()->getConnection();
        hwnd = conn->screen->root;
    }
    else
    {
        hwnd = hdc->hwnd;
        tid_t tid = GetWindowThreadProcessId(hwnd,nullptr);
        conn = SConnMgr::instance()->getConnection(tid);
        if(!conn) 
            return nullptr;
    }
    HDC ret = new _SDC(hwnd);
    SelectObject(ret,conn->GetDesktopBitmap());
    return ret;
}

BOOL DeleteDC(HDC hdc)
{
    delete hdc;
    return TRUE;
}

int WINAPI GetBkMode(  HDC hdc ){
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
        // recreate cairo_t object
        assert(h != hdc->bmp);
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

    int size = FIELD_OFFSET(RGNDATA, Buffer) + rcList->num_rectangles * sizeof(RECT);
    RGNDATA *pRgnData = (RGNDATA *)malloc(size);
    pRgnData->rdh.dwSize = size;
    pRgnData->rdh.iType = RDH_RECTANGLES;
    pRgnData->rdh.nCount = rcList->num_rectangles;

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
    else
    {
        HRGN rgnNow = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, rgnNow);
        int ret = CombineRgn(rgnNow, rgnNow, hrgn, mode);
        ApplyRegion(hdc->cairo, rgnNow);
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
static int matrix_is_identity(const cairo_matrix_t *matrix)
{
    static cairo_matrix_t identity_matrix = { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
    return matrix->xx == identity_matrix.xx && matrix->xy == identity_matrix.xy && matrix->yy == identity_matrix.yy && matrix->yx == identity_matrix.yx && matrix->x0 == identity_matrix.x0 && matrix->y0 == identity_matrix.y0;
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

static bool cairo_matrix_inverse(const cairo_matrix_t *src, cairo_matrix_t *inv)
{
    double A[3][3];
    A[0][0] = src->xx;
    A[0][1] = src->yx;
    A[0][2] = 0;
    A[1][0] = src->xy;
    A[1][1] = src->yy;
    A[1][2] = 0;
    A[2][0] = src->x0;
    A[2][1] = src->y0;
    A[2][2] = 1;
    double A_inv[3][3];
    if (!matrix_inverse(A, A_inv))
        return false;
    inv->xx = A_inv[0][0];
    inv->yx = A_inv[0][1];
    inv->xy = A_inv[1][0];
    inv->yy = A_inv[1][1];
    inv->x0 = A_inv[2][0];
    inv->y0 = A_inv[2][1];
    return true;
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
    lprect->left = x1;
    lprect->top = y1;
    lprect->right = x2;
    lprect->bottom = y2;
    return 0;
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
    cairo_translate(ctx, rc.left, rc.top);
    double wid = rc.right - rc.left, hei = rc.bottom - rc.top;
    if (ApplyBrush(ctx, hbr, wid, hei))
    {
        cairo_rectangle(ctx, 0, 0, wid, hei);
        cairo_fill(ctx);
        ret = TRUE;
    }
    cairo_restore(ctx);
    return ret;
}

BOOL FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr, int nWidth, int nHeight)
{
    if (!hrgn || GetObjectType(hrgn) != OBJ_REGION)
        return FALSE;
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    ApplyRegion(ctx, hrgn);
    ApplyPen(ctx, hdc->pen);
    ApplyRop2(ctx, hdc->rop2);
    cairo_stroke(ctx);
    cairo_restore(ctx);
    return TRUE;
}

BOOL WINAPI DrawFocusRect(
    HDC hdc,          // handle to device context
    CONST RECT* rc  // logical coordinates
) {
    HBRUSH hOldBrush;
    HPEN hOldPen, hNewPen;
    INT oldDrawMode, oldBkMode;
    cairo_antialias_t oldAntialias = cairo_get_antialias(hdc->cairo);
    cairo_set_antialias(hdc->cairo,CAIRO_ANTIALIAS_NONE);
    hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    hNewPen = CreatePen(PS_DOT, 1, 0);
    hOldPen = SelectObject(hdc, hNewPen);
    oldDrawMode = SetROP2(hdc, R2_NOT);
    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);
    SetROP2(hdc, oldDrawMode);
    SelectObject(hdc, hOldPen);
    DeleteObject(hNewPen);
    SelectObject(hdc, hOldBrush);
    cairo_set_antialias(hdc->cairo,oldAntialias);
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
    cairo_get_matrix(hdcSrc->cairo,&mtxSrc);
    x1+=mtxSrc.x0;
    y1+=mtxSrc.y0;

    cairo_rectangle(hdc->cairo, x, y, wDst, hDst);
    cairo_clip(hdc->cairo);

    cairo_translate(hdc->cairo, x, y);
    double scale_x = wDst * 1.0 / wSrc;
    double scale_y = hDst * 1.0 / hSrc;
    cairo_scale(hdc->cairo, scale_x, scale_y);

    cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
    cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_ATOP);
    if (ftn.SourceConstantAlpha != 255)
        cairo_paint_with_alpha(hdc->cairo, ftn.SourceConstantAlpha * 1.0 / 255.0);
    else
        cairo_paint(hdc->cairo);

    cairo_restore(hdc->cairo);
    return 0;
}


static BOOL AlphaBlendEx(HDC hdc, int x, int y, int wDst, int hDst, cairo_surface_t * src, int x1, int y1, int wSrc, int hSrc, BLENDFUNCTION ftn, int filterLevel)
{
    assert(hdc);
    cairo_save(hdc->cairo);

    cairo_rectangle(hdc->cairo, x, y, wDst, hDst);
    cairo_clip(hdc->cairo);

    cairo_translate(hdc->cairo, x, y);
    double scale_x = wDst * 1.0 / wSrc;
    double scale_y = hDst * 1.0 / hSrc;
    cairo_scale(hdc->cairo, scale_x, scale_y);

    cairo_pattern_t * pattern = cairo_pattern_create_for_surface(src);
    cairo_filter_t filter = CAIRO_FILTER_FAST;
    switch(filterLevel){
        case FILTER_FAST: filter = CAIRO_FILTER_GOOD;break;
        case FILTER_MIDIUM: filter = CAIRO_FILTER_BEST;break;
        case FILTER_BEST: filter = CAIRO_FILTER_BILINEAR;break;
    }
    cairo_pattern_set_filter(pattern,filter);
    cairo_matrix_t mtx;
    cairo_matrix_init_translate(&mtx,x1,y1);
    cairo_pattern_set_matrix(pattern,&mtx);
    cairo_set_source(hdc->cairo, pattern);
    
    cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_OVER);
    if (ftn.SourceConstantAlpha != 255)
        cairo_paint_with_alpha(hdc->cairo, ftn.SourceConstantAlpha * 1.0 / 255.0);
    else
        cairo_paint(hdc->cairo);
    cairo_pattern_destroy(pattern);
    cairo_restore(hdc->cairo);
    return 0;
}

    BOOL DrawBitmapEx(HDC hdc, LPCRECT pRcDest,HBITMAP bmp,LPCRECT pRcSrc,UINT expendMode, BYTE byAlpha/*=0xFF*/ )
    {
        cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(bmp);
        int filterLevel = HIWORD(expendMode);
        expendMode = LOWORD(expendMode);

        BLENDFUNCTION bf={ AC_SRC_OVER,0,byAlpha,AC_SRC_ALPHA};
        if(expendMode == EXPEND_MODE_NONE)
        {
            ::AlphaBlendEx(hdc,pRcDest->left,pRcDest->top,pRcSrc->right-pRcSrc->left,pRcSrc->bottom-pRcSrc->top,
                src,pRcSrc->left,pRcSrc->top,pRcSrc->right-pRcSrc->left,pRcSrc->bottom-pRcSrc->top,bf,filterLevel);
        }else if(expendMode == EXPEND_MODE_STRETCH)
        {
            ::AlphaBlendEx(hdc,pRcDest->left,pRcDest->top,pRcDest->right-pRcDest->left,pRcDest->bottom-pRcDest->top,
                src,pRcSrc->left,pRcSrc->top,pRcSrc->right-pRcSrc->left,pRcSrc->bottom-pRcSrc->top,bf,filterLevel);
        }else//if(expendMode == EXPEND_MODE_TILE)
        {
            ::SaveDC(hdc);
            ::IntersectClipRect(hdc,pRcDest->left,pRcDest->top,pRcDest->right,pRcDest->bottom);
            int nWid=pRcSrc->right-pRcSrc->left;
            int nHei=pRcSrc->bottom-pRcSrc->top;
            for(int y=pRcDest->top ;y<pRcDest->bottom;y+=nHei)
            {
                for(int x=pRcDest->left; x<pRcDest->right; x+=nWid)
                {
                    ::AlphaBlendEx(hdc,x,y,nWid,nHei,
                        src,pRcSrc->left,pRcSrc->top,nWid,nHei,
                        bf,filterLevel);                    
                }
            }
            ::RestoreDC(hdc,-1);
        }
        return TRUE;
    }


    BOOL DrawBitmap9Patch(HDC hdc, LPCRECT pRcDest,HBITMAP hBmp, LPCRECT pRcSrc,LPCRECT pRcSourMargin,UINT expendMode,BYTE byAlpha/*=0xFF*/ )
    {        
        LONG xDest[4] = {pRcDest->left,pRcDest->left+pRcSourMargin->left,pRcDest->right-pRcSourMargin->right,pRcDest->right};
        LONG xSrc[4] = {pRcSrc->left,pRcSrc->left+pRcSourMargin->left,pRcSrc->right-pRcSourMargin->right,pRcSrc->right};
        LONG yDest[4] = {pRcDest->top,pRcDest->top+pRcSourMargin->top,pRcDest->bottom-pRcSourMargin->bottom,pRcDest->bottom};
        LONG ySrc[4] = {pRcSrc->top,pRcSrc->top+pRcSourMargin->top,pRcSrc->bottom-pRcSourMargin->bottom,pRcSrc->bottom};

        //首先保证九宫分割正常
        if(!(xSrc[0] <= xSrc[1] && xSrc[1] <= xSrc[2] && xSrc[2] <= xSrc[3])) return FALSE;
        if(!(ySrc[0] <= ySrc[1] && ySrc[1] <= ySrc[2] && ySrc[2] <= ySrc[3])) return FALSE;
        if (xDest[1] > xDest[2]) return FALSE;
        if (yDest[1] > yDest[2]) return FALSE;
        //调整目标位置
        int nDestWid=pRcDest->right-pRcDest->left;
        int nDestHei=pRcDest->bottom-pRcDest->top;

        if((pRcSourMargin->left + pRcSourMargin->right) > nDestWid)
        {//边缘宽度大于目标宽度的处理
            if(pRcSourMargin->left >= nDestWid)
            {//只绘制左边部分
                xSrc[1] = xSrc[2] = xSrc[3] = xSrc[0]+nDestWid;
                xDest[1] = xDest[2] = xDest[3] = xDest[0]+nDestWid;
            }else if(pRcSourMargin->right >= nDestWid)
            {//只绘制右边部分
                xSrc[0] = xSrc[1] = xSrc[2] = xSrc[3]-nDestWid;
                xDest[0] = xDest[1] = xDest[2] = xDest[3]-nDestWid;
            }else
            {//先绘制左边部分，剩余的用右边填充
                int nRemain=xDest[3]-xDest[1];
                xSrc[2] = xSrc[3]-nRemain;
                xDest[2] = xDest[3]-nRemain;
            }
        }

        if(pRcSourMargin->top + pRcSourMargin->bottom > nDestHei)
        {
            if(pRcSourMargin->top >= nDestHei)
            {//只绘制上边部分
                ySrc[1] = ySrc[2] = ySrc[3] = ySrc[0]+nDestHei;
                yDest[1] = yDest[2] = yDest[3] = yDest[0]+nDestHei;
            }else if(pRcSourMargin->bottom >= nDestHei)
            {//只绘制下边部分
                ySrc[0] = ySrc[1] = ySrc[2] = ySrc[3]-nDestHei;
                yDest[0] = yDest[1] = yDest[2] = yDest[3]-nDestHei;
            }else
            {//先绘制左边部分，剩余的用右边填充
                int nRemain=yDest[3]-yDest[1];
                ySrc[2] = ySrc[3]-nRemain;
                yDest[2] = yDest[3]-nRemain;
            }
        }

        //定义绘制模式
        UINT mode[3][3]={
            {EXPEND_MODE_NONE,expendMode,EXPEND_MODE_NONE},
            {expendMode,expendMode,expendMode},
            {EXPEND_MODE_NONE,expendMode,EXPEND_MODE_NONE}
        };

        for(int y=0;y<3;y++)
        {
            if(ySrc[y] == ySrc[y+1]) continue;
            for(int x=0;x<3;x++)
            {
                if(xSrc[x] == xSrc[x+1]) continue;
                RECT rcSrc = {xSrc[x],ySrc[y],xSrc[x+1],ySrc[y+1]};
                RECT rcDest ={xDest[x],yDest[y],xDest[x+1],yDest[y+1]};
                DrawBitmapEx(hdc, &rcDest,hBmp,&rcSrc,mode[y][x],byAlpha);
            }
        }

        return TRUE;
    }

static cairo_surface_t * cairo_surface_create_copy(cairo_surface_t *src){
        //create an copy of src
        int bmpWid = cairo_image_surface_get_width(src);
        int bmpHei = cairo_image_surface_get_height(src);
        cairo_format_t fmt = cairo_image_surface_get_format(src);
        cairo_surface_t * srcCpy = cairo_surface_create_similar_image(src,fmt,bmpWid,bmpHei);
        cairo_t * cr = cairo_create(srcCpy);
        cairo_set_source_surface(cr,src,0,0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        return srcCpy;
}

BOOL BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    assert(hdc && hdcSrc);
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    if(hdc == hdcSrc){
        //create an copy of src
        src = cairo_surface_create_copy(src);
    }
    cairo_save(hdc->cairo);
    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo,&mtxSrc);
    x1+=mtxSrc.x0;
    y1+=mtxSrc.y0;

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
        cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    case SRCPAINT:
        cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_OVER);
        break;
    case SRCAND:
        cairo_set_source_surface(hdc->cairo, src, -x1, -y1);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DEST_IN);
        break;
    case DSTINVERT:
        cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    }
    cairo_rectangle(hdc->cairo, 0, 0, cx, cy);
    cairo_fill(hdc->cairo);
    cairo_restore(hdc->cairo);
    if(hdc == hdcSrc){
        //destroy surface copy
        cairo_surface_destroy(src);
    }
    return TRUE;
}

BOOL StretchBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, int cx2, int cy2, DWORD rop)
{
    assert(hdc && hdcSrc);
    cairo_surface_t *src = (cairo_surface_t *)GetGdiObjPtr(hdcSrc->bmp);
    if(hdc == hdcSrc){
        //create an copy of src
        src = cairo_surface_create_copy(src);
    }
    cairo_save(hdc->cairo);
    cairo_matrix_t mtxSrc;
    cairo_get_matrix(hdcSrc->cairo,&mtxSrc);
    x1+=mtxSrc.x0;
    y1+=mtxSrc.y0;

    cairo_rectangle(hdc->cairo, x, y, cx, cy);
    cairo_clip(hdc->cairo);

    cairo_translate(hdc->cairo, x + cx / 2.0, y + cy / 2.0);
    double scale_x = cx * 1.0 / cx2;
    double scale_y = cy * 1.0 / cy2;
    cairo_scale(hdc->cairo, scale_x, scale_y);
    cx2 = abs(cx2);
    cy2 = abs(cy2);
    switch (rop)
    {
    case SRCCOPY:
        cairo_set_source_surface(hdc->cairo, src, -x1 - cx2 / 2.0, -y1 - cy2 / 2.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_SOURCE);
        break;
    case SRCINVERT:
        cairo_set_source_surface(hdc->cairo, src, -x1 - cx2 / 2.0, -y1 - cy2 / 2.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    case SRCPAINT:
        cairo_set_source_surface(hdc->cairo, src, -x1 - cx2 / 2.0, -y1 - cy2 / 2.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_OVER);
        break;
    case SRCAND:
        cairo_set_source_surface(hdc->cairo, src, -x1 - cx2 / 2.0, -y1 - cy2 / 2.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DEST_IN);
        break;
    case DSTINVERT:
        cairo_set_source_rgb(hdc->cairo, 1.0, 1.0, 1.0);
        cairo_set_operator(hdc->cairo, CAIRO_OPERATOR_DIFFERENCE);
        break;
    }
    cairo_rectangle(hdc->cairo, -cx / 2.0, -cy / 2.0, cx, cy);
    cairo_paint(hdc->cairo);
    cairo_restore(hdc->cairo);
    if(hdc == hdcSrc){
        //destroy surface copy
        cairo_surface_destroy(src);
    }
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
    BOOL ret = StretchBlt(hdc, x_dst, y_dst, width_dst, height_dst, memdc, x_src, y_src, width_src, height_src, rop);
    DeleteDC(memdc);
    DeleteObject(bmp);
    return height_src;
}

void SetStretchBltMode(HDC hdc, int mode)
{
    // todo:hjx
}

BOOL PatBlt(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ int w, _In_ int h, _In_ DWORD rop) {
    BOOL ret = FALSE;
    cairo_t* cr = hdc->cairo;
    cairo_save(cr);
    if (ApplyBrush(cr,hdc->brush,w,h)) {
        switch (rop) {
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

static bool IsAlpha(TCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsNumber(TCHAR c)
{
    return c >= '0' && c <= '9';
}

static bool IsHex(TCHAR c)
{
    return IsNumber(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsDigit(TCHAR c)
{
    return IsNumber(c) || c == '.' || c == ',';
}

static LPCTSTR SkipWord(LPCTSTR p)
{
    if (IsAlpha(*p))
    {
        while (*p)
        {
            p = CharNext(p);
            if (!IsAlpha(*p))
                break;
        }
    }
    return p;
}

static LPCTSTR SkipNumber(LPCTSTR p)
{
    if (*p && *(p + 1) && (_tcsncmp(p, _T("0x"), 2) == 0 || _tcsncmp(p, _T("0X"), 2) == 0))
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

static LPCTSTR WordNext(LPCTSTR pszBuf, bool bWordbreak)
{
    SASSERT(pszBuf);
    LPCTSTR p = CharNext(pszBuf);
    if (!bWordbreak)
        return p;
    LPCTSTR pWord = SkipWord(pszBuf);
    if (pWord > pszBuf)
        return pWord;
    LPCTSTR pNum = SkipNumber(pszBuf);
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
    int i = 0;
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

static void DrawTextDecLines(HDC hdc, cairo_font_extents_t &font_ext, LPCSTR str, int len, int x, int y, const cairo_text_extents_t & text_ext)
{
    const LOGFONT *lf = (const LOGFONT *)GetGdiObjPtr(hdc->hfont);
    assert(lf);
    if (lf->lfStrikeOut || lf->lfUnderline)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, GetTextColor(hdc));
        ApplyPen(hdc->cairo, pen);
        ApplyRop2(hdc->cairo, hdc->rop2);
        if (lf->lfStrikeOut)
        {
            double y_line = y + (font_ext.ascent + font_ext.descent) / 2.0;
            cairo_move_to(hdc->cairo, x + text_ext.x_bearing, y_line);
            cairo_line_to(hdc->cairo, x + text_ext.x_advance, y_line);
        }
        if (lf->lfUnderline)
        {
            double y_line = y + font_ext.ascent + 1;
            cairo_move_to(hdc->cairo, x + text_ext.x_bearing, y_line);
            cairo_line_to(hdc->cairo, x + text_ext.x_advance, y_line);
        }
        cairo_stroke(hdc->cairo);
        DeleteObject(pen);
    }
}

static void DrawSingleLine(HDC hdc, LPCTSTR pszBuf, int iBegin, int cchText, LPRECT pRect, UINT uFormat)
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
        cairo_text_extents2(hdc->cairo,pszBuf + iBegin, cchText,&text_ext);
        DrawTextDecLines(hdc, font_ext, pszBuf + iBegin, cchText, pRect->left, pRect->top,text_ext);
    }
}

#define kDrawText_LineInterval 0

void DrawMultiLine(HDC hdc, LPCTSTR pszBuf, int cchText, LPRECT pRect, UINT uFormat)
{
    int i = 0, nLine = 1;
    if (cchText == -1)
        cchText = (int)_tcslen(pszBuf);
    LPCTSTR p1 = pszBuf;
    POINT pt = { pRect->left, pRect->top };
    SIZE szWord = OnMeasureText(hdc, _T("A"), 1);
    int nLineHei = szWord.cy;
    int nRight = pRect->right;
    int nLineWid = pRect->right - pRect->left;
    pRect->right = pRect->left;

    LPCTSTR pLineHead = p1, pLineTail = p1;

    LPCTSTR pPrev = NULL;
    while (i < cchText)
    {
        LPCTSTR p2 = WordNext(p1, uFormat & DT_WORDBREAK);
        SASSERT(p2 > p1);
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
                LPCTSTR p3 = p1;
                SIZE szChar;
                szWord.cx = 0;
                while (p3 < p2)
                {
                    LPCTSTR p4 = CharNext(p3);
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

int DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format) {
    if (cchText < 0)
        cchText = wcslen(lpchText);
    int len = WideCharToMultiByte(CP_UTF8, 0, lpchText, cchText, nullptr, 0, nullptr, nullptr);
    char* buf = new char[len];
    WideCharToMultiByte(CP_UTF8, 0, lpchText, cchText, buf, len, nullptr, nullptr);
    int nRet = DrawTextA(hdc, buf, len, lprc, format);
    delete[]buf;
    return nRet;
}

int DrawTextA(HDC hdc, LPCSTR pszBuf, int cchText, LPRECT pRect, UINT uFormat)
{
    if (cchText < 0)
        cchText = strlen(pszBuf);

    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    CairoColor cr(hdc->crText);
    cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
    cairo_matrix_t mtx = { 0 };
    cairo_get_matrix(hdc->cairo, &mtx);
    cairo_draw_text(hdc->cairo, pszBuf, cchText, pRect, uFormat);
    cairo_restore(hdc->cairo);

    return TRUE;
}

COLORREF GetBkColor(  HDC hdc ){
    return hdc->crBk;
}

COLORREF SetBkColor(  HDC hdc ,COLORREF cr){
    COLORREF ret = hdc->crBk;
    hdc->crBk = cr;
    return ret;
}

BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
    std::string str;
    tostring(lpString,c,str);
    return TextOutA(hdc,x,y,str.c_str(),str.length());
}

BOOL TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
    if (c < 0)
        c = strlen(lpString);
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    CairoColor cr(hdc->crText);
    cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);

    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);

    cairo_text_extents_t text_ext;
    cairo_text_extents2(hdc->cairo, lpString,c, &text_ext);
    switch(hdc->textAlign & (TA_LEFT|TA_RIGHT|TA_CENTER)){
        case TA_RIGHT: x-=text_ext.x_advance;break;
        case TA_CENTER:x-=text_ext.x_advance/2;break;
    }
    switch(hdc->textAlign & (TA_BASELINE|TA_BOTTOM|TA_TOP)){
        case TA_TOP: y+= font_ext.ascent; break;
        case TA_BASELINE: break;
        case TA_BOTTOM: y-= font_ext.descent;break;
    }
    if(hdc->bkMode == OPAQUE){
        //fill bkcolor for text
        cairo_save(hdc->cairo);
        COLORREF crBk = GetBkColor(hdc);
        CairoColor cr(crBk);
        cairo_set_source_rgba(hdc->cairo,cr.r,cr.g,cr.b,cr.a);
        cairo_rectangle(hdc->cairo,x,y-font_ext.ascent,text_ext.x_advance,font_ext.ascent + font_ext.descent);
        cairo_fill(hdc->cairo);
        cairo_restore(hdc->cairo);
    }
    double old_x,old_y;
    cairo_get_current_point(hdc->cairo,&old_x,&old_y);
    cairo_move_to(hdc->cairo, x, y);    
    cairo_show_text2(hdc->cairo, lpString, c);
    DrawTextDecLines(hdc, font_ext, lpString, c, x, y,text_ext);
    if(hdc->textAlign & TA_NOUPDATECP)
        cairo_move_to(hdc->cairo,old_x,old_y);
    cairo_restore(hdc->cairo);
    return TRUE;
}

static LONG TEXT_TabbedTextOut(HDC hdc, INT x, INT y, LPCSTR lpstr,
    INT count, INT cTabStops, const INT* lpTabPos, INT nTabOrg,
    BOOL fDisplayText)
{
    INT defWidth;
    SIZE extent;
    int i, j;
    int start = x;
    TEXTMETRICA tm;

    if (!lpstr || count == 0) return 0;
    if (count < 0) count = strlen(lpstr);
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
            if (lpstr[i] != '\t') break;
        for (j = i; j < count; j++)
            if (lpstr[j] == '\t') break;
        /* get the extent of the normal character part */
        GetTextExtentPointA(hdc, lpstr + i, j - i, &extent);
        /* and if there is a <tab>, calculate its position */
        if (i) {
            /* get x coordinate for the drawing of this string */
            for (; cTabStops >= i; lpTabPos++, cTabStops--)
            {
                if (nTabOrg + abs(*lpTabPos) > x) {
                    if (lpTabPos[i - 1] >= 0) {
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
            if ((cTabStops < i) && (defWidth > 0)) {
                x0 = nTabOrg + ((x - nTabOrg) / defWidth + i) * defWidth;
                x = x0 + extent.cx;
            }
            else if ((cTabStops < i) && (defWidth < 0)) {
                x = nTabOrg + ((x - nTabOrg + extent.cx) / -defWidth + i)
                    * -defWidth;
                x0 = x - extent.cx;
            }
        }
        else
            x += extent.cx;

        if (!extent.cy) extent.cy = tm.tmHeight;

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


DWORD WINAPI GetTabbedTextExtentA(
    HDC hdc,                        // handle to DC
    LPCSTR lpString,               // character string
    int nCount,                     // number of characters
    int nTabPositions,              // number of tab positions
    CONST LPINT lpnTabStopPositions // array of tab positions
) {
    return TEXT_TabbedTextOut(hdc, 0, 0, lpString, nCount, nTabPositions, lpnTabStopPositions, 0, FALSE);
}

DWORD WINAPI GetTabbedTextExtentW(
    HDC hDC,                        // handle to DC
    LPCWSTR lpString,               // character string
    int nCount,                     // number of characters
    int nTabPositions,              // number of tab positions
    CONST LPINT lpnTabStopPositions // array of tab positions
) {
    std::string str;
    tostring(lpString, nCount, str);
    return GetTabbedTextExtentA(hDC, str.c_str(), str.length(), nTabPositions, lpnTabStopPositions);
}


LONG WINAPI TabbedTextOutA(HDC hdc,                         // handle to DC
    int X,                           // x-coord of start
    int Y,                           // y-coord of start
    LPCSTR lpString,                // character string
    int nCount,                      // number of characters
    int nTabPositions,               // number of tabs in array
    CONST LPINT lpnTabStopPositions, // array of tab positions
    int nTabOrigin                   // start of tab expansion
) {
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
) {
    std::string str;
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

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl) {
    std::string str;
    tostring(lpString,c,str);
    return GetTextExtentPoint32A(hdc, str.c_str(), str.length(), psizl);
}

BOOL WINAPI GetTextExtentExPointA(
    HDC hdc,
    LPCSTR lpszString,
    int cchString,
    int nMaxExtent,
    LPINT lpnFit,
    LPINT lpnDx,
    LPSIZE psizl
) {
    if(!lpnFit && !lpnDx)
        return GetTextExtentPoint32A(hdc,lpszString,cchString,psizl);
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    cairo_text_extents_t ext;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    if (cchString < 0) cchString = strlen(lpszString);
    int* pCharWid = new int[cchString];
    int nWords = cairo_text_extents2_ex(hdc->cairo, lpszString, cchString, &ext,pCharWid);
    if (lpnDx) {
        if (lpnFit) *lpnFit = cchString;
        for (int i = 0,ichar=0; i < nWords; i++) {
            int chars = swinx::UTF8CharLength(lpszString[ichar]);
            if (pCharWid[i] > nMaxExtent)
            {
                if (lpnFit) *lpnFit = ichar;                
                if(i>0) ext.x_advance =pCharWid[i - 1];
                else ext.x_advance = 0;
                break;
            }
            for (int j = 0; j < chars; j++) {
                lpnDx[ichar + j] = pCharWid[i];
            }
            ichar += chars;
        }
    }
    delete[]pCharWid;

    psizl->cx = ext.x_advance;
    psizl->cy = font_ext.ascent + font_ext.descent;
    cairo_restore(hdc->cairo);
    return TRUE;
}

BOOL WINAPI GetTextExtentExPointW(
    HDC hdc,
    LPCWSTR lpszString,
    int cchString,
    int nMaxExtent,
    LPINT lpnFit,
    LPINT lpnDx,
    LPSIZE psizl
) {
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32W(hdc, lpszString, cchString, psizl);
    cairo_save(hdc->cairo);
    ApplyFont(hdc);
    cairo_text_extents_t ext;
    cairo_font_extents_t font_ext;
    cairo_font_extents(hdc->cairo, &font_ext);
    std::string str;
    tostring(lpszString, cchString, str);
    int* pCharWid = new int[str.length()];
    int nWords = cairo_text_extents2_ex(hdc->cairo, str.c_str(), str.length(), &ext, pCharWid);
    if (lpnDx) {
        if (lpnFit) *lpnFit = cchString;
        for (int i = 0, ichar = 0; i < nWords; i++) {
            int chars = swinx::WideCharLength(lpszString[ichar]);
            if (pCharWid[i] > nMaxExtent)
            {
                if (lpnFit) *lpnFit = ichar;
                if (i > 0) ext.x_advance = pCharWid[i - 1];
                else ext.x_advance = 0;
                break;
            }
            for (int j = 0; j < chars; j++) {
                lpnDx[ichar + j] = pCharWid[i];
            }
            ichar += chars;
        }
    }
    delete[]pCharWid;

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
    RGB_ENTRY(COLOR_SCROLLBAR, RGB(212, 208, 200), "Scrollbar"),
    RGB_ENTRY(COLOR_BACKGROUND, RGB(58, 110, 165), "Background"),
    RGB_ENTRY(COLOR_ACTIVECAPTION, RGB(10, 36, 106), "ActiveTitle"),
    RGB_ENTRY(COLOR_INACTIVECAPTION, RGB(128, 128, 128), "InactiveTitle"),
    RGB_ENTRY(COLOR_MENU, RGB(212, 208, 200), "Menu"),
    RGB_ENTRY(COLOR_WINDOW, RGB(255, 255, 255), "Window"),
    RGB_ENTRY(COLOR_WINDOWFRAME, RGB(0, 0, 0), "WindowFrame"),
    RGB_ENTRY(COLOR_MENUTEXT, RGB(0, 0, 0), "MenuText"),
    RGB_ENTRY(COLOR_WINDOWTEXT, RGB(0, 0, 0), "WindowText"),
    RGB_ENTRY(COLOR_CAPTIONTEXT, RGB(255, 255, 255), "TitleText"),
    RGB_ENTRY(COLOR_ACTIVEBORDER, RGB(212, 208, 200), "ActiveBorder"),
    RGB_ENTRY(COLOR_INACTIVEBORDER, RGB(212, 208, 200), "InactiveBorder"),
    RGB_ENTRY(COLOR_APPWORKSPACE, RGB(128, 128, 128), "AppWorkSpace"),
    RGB_ENTRY(COLOR_HIGHLIGHT, RGB(10, 36, 106), "Hilight"),
    RGB_ENTRY(COLOR_HIGHLIGHTTEXT, RGB(255, 255, 255), "HilightText"),
    RGB_ENTRY(COLOR_BTNFACE, RGB(212, 208, 200), "ButtonFace"),
    RGB_ENTRY(COLOR_BTNSHADOW, RGB(128, 128, 128), "ButtonShadow"),
    RGB_ENTRY(COLOR_GRAYTEXT, RGB(128, 128, 128), "GrayText"),
    RGB_ENTRY(COLOR_BTNTEXT, RGB(0, 0, 0), "ButtonText"),
    RGB_ENTRY(COLOR_INACTIVECAPTIONTEXT, RGB(212, 208, 200), "InactiveTitleText"),
    RGB_ENTRY(COLOR_BTNHIGHLIGHT, RGB(255, 255, 255), "ButtonHilight"),
    RGB_ENTRY(COLOR_3DDKSHADOW, RGB(64, 64, 64), "ButtonDkShadow"),
    RGB_ENTRY(COLOR_3DLIGHT, RGB(212, 208, 200), "ButtonLight"),
    RGB_ENTRY(COLOR_INFOTEXT, RGB(0, 0, 0), "InfoText"),
    RGB_ENTRY(COLOR_INFOBK, RGB(255, 255, 225), "InfoWindow"),
    RGB_ENTRY(COLOR_ALTERNATEBTNFACE, RGB(181, 181, 181), "ButtonAlternateFace"),
    RGB_ENTRY(COLOR_HOTLIGHT, RGB(0, 0, 200), "HotTrackingColor"),
    RGB_ENTRY(COLOR_GRADIENTACTIVECAPTION, RGB(166, 202, 240), "GradientActiveTitle"),
    RGB_ENTRY(COLOR_GRADIENTINACTIVECAPTION, RGB(192, 192, 192), "GradientInactiveTitle"),
    RGB_ENTRY(COLOR_MENUHILIGHT, RGB(10, 36, 106), "MenuHilight"),
    RGB_ENTRY(COLOR_MENUBAR, RGB(212, 208, 200), "MenuBar")
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
    SysColorBrush() {
        for (int i = 0; i <= COLOR_MENUBAR; i++) {
            hSysColorBr[i] = CreateSolidBrush(system_colors[i].val);
        }
    }
    ~SysColorBrush() {
        for (int i = 0; i <= COLOR_MENUBAR; i++) {
            DeleteObject(hSysColorBr[i]);
        }
    }

    HBRUSH hSysColorBr[COLOR_MENUBAR+1];
};

HBRUSH GetSysColorBrush(int i) {
    static SysColorBrush sysColorBrs;
    return sysColorBrs.hSysColorBr[i];
}

class SysColorPen {
public:
    SysColorPen() {
        for (int i = 0; i <= COLOR_MENUBAR; i++) {
            hSysColorPen[i] = CreatePen(PS_SOLID,1,system_colors[i].val);
        }
    }
    ~SysColorPen() {
        for (int i = 0; i <= COLOR_MENUBAR; i++) {
            DeleteObject(hSysColorPen[i]);
        }
    }

    HPEN hSysColorPen[COLOR_MENUBAR + 1];
};

HPEN GetSysColorPen(int i) {
    static SysColorPen sysColorPens;
    return sysColorPens.hSysColorPen[i];
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
    case BLACK_BRUSH:
    {
        static LOGBRUSH log;
        log.lbStyle = BS_SOLID;
        log.lbColor = RGBA(0, 0, 0, 255);
        static _Handle br(OBJ_BRUSH, &log, nullptr);
        return &br;
    }
    case WHITE_BRUSH:
    {
        static LOGBRUSH log;
        log.lbStyle = BS_SOLID;
        log.lbColor = RGBA(255, 255, 255, 255);
        static _Handle br(OBJ_BRUSH, &log, nullptr);
        return &br;
    }
    case GRAY_BRUSH:
    {
        static LOGBRUSH log;
        log.lbStyle = BS_SOLID;
        log.lbColor = RGBA(128, 128, 128, 255);
        static _Handle br(OBJ_BRUSH, &log, nullptr);
        return &br;
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
        static LOGFONT lf = { 0 };
        strcpy(lf.lfFaceName, "Noto");
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
    cairo_save(ctx);
    cairo_translate(ctx, left, top);
    double wid = right - left, hei = bottom - top;
    if (ApplyBrush(hdc->cairo, hdc->brush, wid, hei))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_rectangle(ctx, 0, 0, wid, hei);
        cairo_fill(hdc->cairo);
    }
    if (ApplyPen(hdc->cairo, hdc->pen))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_rectangle(ctx, 0, 0, wid, hei);
        cairo_stroke(hdc->cairo);
    }
    cairo_restore(hdc->cairo);
    return TRUE;
}

void drawRoundRect(cairo_t *cr, double x, double y, double width, double height, double rx, double ry)
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

BOOL RoundRect(HDC hdc, int left, int top, int right, int bottom, int radio_x, int radio_y)
{
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    cairo_translate(ctx, left, top);
    double wid = right - left, hei = bottom - top;
    if (ApplyBrush(ctx, hdc->brush, wid, hei))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        drawRoundRect(ctx, 0, 0, wid, hei, radio_x, radio_y);
        cairo_fill(ctx);
    }
    if (ApplyPen(ctx, hdc->pen))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        drawRoundRect(ctx, 0, 0, wid, hei, radio_x, radio_y);
        cairo_stroke(ctx);
    }
    cairo_restore(ctx);
    return TRUE;
}

BOOL Polyline(HDC hdc, const POINT *apt, int cpt)
{
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);
    ApplyPen(ctx, hdc->pen);
    ApplyRop2(hdc->cairo, hdc->rop2);

    for (int i = 0; i < cpt - 1; i++)
    {
        cairo_move_to(ctx, apt[i].x, apt[i].y);
        cairo_line_to(ctx, apt[i + 1].x, apt[i + 1].y);
    }
    cairo_restore(ctx);
    return 0;
}

BOOL SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    cairo_matrix_t mtx;
    cairo_get_matrix(hdc->cairo, &mtx);
    if (lppt)
    {
        lppt->x = (int)floor(mtx.x0);
        lppt->y = (int)floor(mtx.y0);
    }

    mtx.x0 = x;
    mtx.y0 = y;

    cairo_set_matrix(hdc->cairo, &mtx);
    return TRUE;
}

BOOL GetViewportOrgEx(HDC hdc, LPPOINT lpPoint)
{
    if (!lpPoint)
        return FALSE;
    cairo_matrix_t mtx;
    cairo_get_matrix(hdc->cairo, &mtx);
    lpPoint->x = (int)floor(mtx.x0);
    lpPoint->y = (int)floor(mtx.y0);
    return TRUE;
}

BOOL OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    cairo_matrix_t mtx;
    cairo_get_matrix(hdc->cairo, &mtx);
    if (lppt)
    {
        lppt->x = (int)floor(mtx.x0);
        lppt->y = (int)floor(mtx.y0);
    }
    return SetViewportOrgEx(hdc, x + mtx.x0, y + mtx.y0, lppt);
}

BOOL WINAPI SetWindowOrgEx(HDC hdc,        // handle to device context
    int X,          // new x-coordinate of window origin
    int Y,          // new y-coordinate of window origin
    LPPOINT lpPoint // original window origin
) {
    //todo:hjx
    return SetViewportOrgEx(hdc, X, Y, lpPoint);
}

BOOL WINAPI SetWindowExtEx(HDC hdc,       // handle to device context
    int nXExtent,  // new horizontal window extent
    int nYExtent,  // new vertical window extent
    LPSIZE lpSize  // original window extent
) {
    //todo:hjx
    return FALSE;
}
int FillRect(HDC hdc, const RECT *lprc, HBRUSH hbr)
{
    int ret = 0;
    cairo_save(hdc->cairo);
    cairo_translate(hdc->cairo, lprc->left, lprc->top);
    double wid = lprc->right - lprc->left, hei = lprc->bottom - lprc->top;
    if (ApplyBrush(hdc->cairo, hbr, wid, hei))
    {
        cairo_rectangle(hdc->cairo, 0, 0, wid, hei);
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
    ApplyPen(ctx, hdc->pen);
    ApplyRop2(hdc->cairo, hdc->rop2);
    cairo_rectangle(hdc->cairo, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
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

BOOL LineTo(HDC hdc, int nXEnd, int nYEnd)
{
    cairo_t *ctx = hdc->cairo;
    if (!ApplyPen(ctx, hdc->pen))
        return FALSE;
    ApplyRop2(hdc->cairo, hdc->rop2);
    cairo_line_to(ctx, nXEnd, nYEnd);
    cairo_stroke(ctx);
    return TRUE;
}

BOOL Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
    cairo_t *ctx = hdc->cairo;
    double x = (left + right) / 2;
    double y = (top + bottom) / 2;
    double scale_x = (right - left) / 2;
    double scale_y = (bottom - top) / 2;
    double wid = right - left, hei = bottom - top;
    if (ApplyBrush(ctx, hdc->brush, wid, hei))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_save(ctx);
        cairo_translate(ctx, x, y);
        cairo_scale(ctx, scale_x, scale_y);
        cairo_move_to(ctx, 1, 0);
        cairo_arc(ctx, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(ctx);
        cairo_save(ctx);
        cairo_translate(ctx, left, top);        
        cairo_fill(ctx);
        cairo_restore(ctx);
    }
    if (ApplyPen(ctx, hdc->pen))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_save(ctx);
        cairo_translate(ctx, x, y);
        cairo_scale(ctx, scale_x, scale_y);
        cairo_move_to(ctx, 1, 0);
        cairo_arc(ctx, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(ctx);
        cairo_save(ctx);     
        cairo_stroke(ctx);
        cairo_restore(ctx);
    }

    return TRUE;
}

BOOL Pie(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;
    double cx = (x1 + x2) / 2;
    double cy = (y1 + y2) / 2;
    double dx3 = double(x3 - cx) / wid;
    double dx4 = double(x4 - cx) / wid;
    double dy3 = double(y3 - cy) / hei;
    double dy4 = double(y4 - cy) / hei;
    double arc1 = atan2(dy3, dx3);
    double arc2 = atan2(dy4, dx4);

    cairo_t *ctx = hdc->cairo;
    if (!IsNullBrush(hdc->brush))
    {
        cairo_save(ctx);
        cairo_translate(ctx, cx, cy);
        cairo_scale(ctx, wid, hei);
        cairo_move_to(ctx, 0, 0);
        cairo_line_to(ctx, dx4, dy4);
        cairo_arc(ctx, 0, 0, 0.5, arc2, arc1);
        cairo_close_path(ctx);
        cairo_restore(ctx);
        cairo_save(ctx);
        cairo_translate(ctx, x1, y1);
        ApplyBrush(ctx, hdc->brush, wid, hei);
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_fill(ctx);
        cairo_restore(ctx);
    }
    if (!IsNullPen(hdc->pen))
    {
        cairo_save(ctx);
        cairo_translate(ctx, cx, cy);
        cairo_scale(ctx, wid, hei);
        cairo_move_to(ctx, 0, 0);
        cairo_line_to(ctx, dx4, dy4);
        cairo_arc(ctx, 0, 0, 0.5, arc2, arc1);
        cairo_close_path(ctx);
        cairo_restore(ctx);
        cairo_save(ctx);
        ApplyPen(ctx, hdc->pen);
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_stroke(ctx);
        cairo_restore(ctx);
    }
    return TRUE;
}

BOOL Arc(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    double wid = x2 - x1;
    double hei = y2 - y1;
    if (wid == 0 || hei == 0)
        return FALSE;

    cairo_save(hdc->cairo);
    double cx = (x1 + x2) / 2;
    double cy = (y1 + y2) / 2;
    cairo_translate(hdc->cairo, cx, cy);
    cairo_scale(hdc->cairo, wid, hei);

    double dx3 = double(x3 - cx) / wid;
    double dx4 = double(x4 - cx) / wid;

    double dy3 = double(y3 - cy) / hei;
    double dy4 = double(y4 - cy) / hei;

    double arc1 = atan2(dy3, dx3);
    double arc2 = atan2(dy4, dx4);
    cairo_move_to(hdc->cairo, dx4, dy4);
    cairo_arc(hdc->cairo, 0, 0, 0.5, arc2, arc1);
    cairo_restore(hdc->cairo);
    cairo_save(hdc->cairo);
    if (ApplyPen(hdc->cairo, hdc->pen))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_stroke(hdc->cairo);
    }
    cairo_restore(hdc->cairo);
    return TRUE;
}

BOOL GetWorldTransform(HDC hdc, LPXFORM lpxf)
{
    cairo_matrix_t mtx;
    cairo_get_matrix(hdc->cairo, &mtx);
    lpxf->eM11 = mtx.xx;
    lpxf->eM12 = mtx.xy;
    lpxf->eM21 = mtx.yx;
    lpxf->eM22 = mtx.yy;
    lpxf->eDx = mtx.x0;
    lpxf->eDy = mtx.y0;
    return TRUE;
}

BOOL SetWorldTransform(HDC hdc, const XFORM *lpxf)
{
    cairo_matrix_t mtx;
    mtx.xx = lpxf->eM11;
    mtx.xy = lpxf->eM12;
    mtx.yx = lpxf->eM21;
    mtx.yy = lpxf->eM22;
    mtx.x0 = lpxf->eDx;
    mtx.y0 = lpxf->eDy;
    cairo_set_matrix(hdc->cairo, &mtx);
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
            unsigned char * buf = cairo_image_surface_get_data(ret);
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
            unsigned char* buf = cairo_image_surface_get_data(ret);
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

int GetDeviceCaps(HDC hdc, int cap)
{
    switch (cap) {
    case BITSPIXEL:
        return 32;// todo:hjx
    case PLANES:
        return 1;
    case LOGPIXELSX:
        return GetSystemScale()*96/100;
    case LOGPIXELSY:
        return GetSystemScale() * 96 / 100;
    case TECHNOLOGY:
        return DT_RASDISPLAY;//todo:hjx
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
};

BOOL GetIconInfo(HICON hIcon, PICONINFO piconinfo)
{
    if (!hIcon)
        return FALSE;
    piconinfo->fIcon = hIcon->fIcon;
    piconinfo->xHotspot = hIcon->xHotspot;
    piconinfo->yHotspot = hIcon->xHotspot;
    if (hIcon->hbmColor)
    {
        BITMAP bm;
        GetObject(hIcon->hbmColor,sizeof(bm),&bm);
        piconinfo->hbmColor = CreateBitmap(bm.bmWidth,bm.bmHeight,bm.bmPlanes,bm.bmBitsPixel,bm.bmBits);
    }
    else {
        piconinfo->hbmColor = nullptr;
    }
    if (hIcon->hbmMask)
    {
        BITMAP bm;
        GetObject(hIcon->hbmMask, sizeof(bm), &bm);
        piconinfo->hbmMask = CreateBitmap(bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel, bm.bmBits);
    }
    else {
        piconinfo->hbmMask = nullptr;
    }
    return TRUE;
}

HICON CreateIconIndirect(PICONINFO piconinfo)
{
    _IconObj *icon = new _IconObj;
    icon->fIcon = piconinfo->fIcon;
    icon->xHotspot = piconinfo->xHotspot;
    icon->yHotspot = piconinfo->xHotspot;
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
    // DumpBmp(hIcon->hbmColor,"/home/flyhigh/snapshot/drawicon-src.png");
    // DumpBmp(hDC->bmp,"/home/flyhigh/snapshot/drawicon-dst-before.png");
    HDC memdc = CreateCompatibleDC(hDC);
    HGDIOBJ oldBmp = SelectObject(memdc, hIcon->hbmColor);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_ALPHA;
    bf.SourceConstantAlpha = 0;
    AlphaBlend(hDC, xLeft, yTop, cxWidth, cyWidth, memdc, 0, 0, bm.bmWidth, bm.bmHeight, bf);
    SelectObject(memdc, oldBmp);
    DeleteDC(memdc);
    // DumpBmp(hDC->bmp,"/home/flyhigh/snapshot/drawicon-dst-after.png");

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
    if(!hdc){
        assert(hdc);
    }
    cairo_save(hdc->cairo);
    if (!ApplyFont(hdc))
        return FALSE;
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

BOOL WINAPI GetTextMetricsW(HDC hdc, TEXTMETRICW* txtMetric)
{
    TEXTMETRICA metricA;
    if (!GetTextMetricsA(hdc, &metricA))
        return FALSE;
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

BOOL Polygon(HDC hdc, const POINT *apt, int cpt)
{
    if (cpt < 2)
        return FALSE;
    cairo_t *ctx = hdc->cairo;
    cairo_save(ctx);

    cairo_new_sub_path(ctx);

    for (int i = 0; i < cpt - 1; i++)
    {
        cairo_move_to(ctx, apt[i].x, apt[i].y);
        cairo_line_to(ctx, apt[i + 1].x, apt[i + 1].y);
    }
    cairo_line_to(ctx, apt[0].x, apt[0].y);

    cairo_close_path(ctx);

    if (ApplyBrush(ctx, hdc->brush, 0, 0)) {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_fill(ctx);
    }

    if (ApplyPen(ctx, hdc->pen))
    {
        ApplyRop2(hdc->cairo, hdc->rop2);
        cairo_stroke(ctx);
    }

    cairo_restore(ctx);
    return TRUE;
}

UINT  WINAPI SetTextAlign(HDC hdc, UINT align) {
    UINT ret = hdc->textAlign;
    hdc->textAlign = align;
    return ret;
}

UINT WINAPI GetTextAlign(  HDC hdc){
    return hdc->textAlign;
}

COLORREF WINAPI GetNearestColor(HDC hdc,           // handle to DC
    COLORREF crColor   // color to be matched
) {
    return crColor;
}

BOOL WINAPI ExtTextOutA(HDC hdc,          // handle to DC
    int X,            // x-coordinate of reference point
    int Y,            // y-coordinate of reference point
    UINT fuOptions,   // text-output options
    CONST RECT* lprc, // optional dimensions  
    LPCSTR lpString, // string
    UINT cbCount,     // number of characters in string
    CONST INT* lpDx   // array of spacing values
) {
    cairo_save(hdc->cairo);
    if(cbCount<0) cbCount = strlen(lpString);
    if (lprc) {
        if (fuOptions & ETO_CLIPPED) {
            cairo_rectangle(hdc->cairo, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
            cairo_clip(hdc->cairo);
        }
        if (fuOptions & ETO_OPAQUE) {
            CairoColor cr(hdc->crBk);
            cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);
            cairo_rectangle(hdc->cairo, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top);
            cairo_fill(hdc->cairo);
        }
    }
    if(!lpDx){
        TextOutA(hdc,X,Y,lpString,cbCount);
    }else{
        ApplyFont(hdc);
        CairoColor cr(hdc->crText);
        cairo_set_source_rgba(hdc->cairo, cr.r, cr.g, cr.b, cr.a);

        cairo_text_extents_t ext;
        int glyph_num = cairo_text_extents2(hdc->cairo,lpString,cbCount,&ext);
        int wid = 0;
        for(int i=0;i<glyph_num;i++){
            wid += lpDx[i];
        }
        double old_x,old_y;
        cairo_get_current_point(hdc->cairo,&old_x,&old_y);

        double x = X, y= Y;
        switch(hdc->textAlign & (TA_RIGHT|TA_CENTER)){
            case TA_RIGHT:x-=wid;break;
            case TA_CENTER:x-=wid/2;break;
        }
        cairo_font_extents_t font_ext;
        cairo_font_extents(hdc->cairo, &font_ext);
        switch (hdc->textAlign & (TA_BASELINE | TA_BOTTOM | TA_TOP)) {
        case TA_TOP: y += font_ext.ascent; break;
        case TA_BASELINE: break;
        case TA_BOTTOM: y -= font_ext.descent; break;
        }

        const char *p = lpString;
        for(int i=0;i<glyph_num;i++){
            int charLen = swinx::UTF8CharLength(*p);
            cairo_move_to(hdc->cairo, x, y);
            cairo_show_text2(hdc->cairo, p, charLen);
            x+=lpDx[i];
            p+=charLen;
        }
        if((hdc->textAlign & TA_UPDATECP) == 0){
            cairo_move_to(hdc->cairo,old_x,old_y);
        }
    }
    cairo_restore(hdc->cairo);
    return TRUE;
}


BOOL WINAPI ExtTextOutW(HDC hdc,          // handle to DC
    int X,            // x-coordinate of reference point
    int Y,            // y-coordinate of reference point
    UINT fuOptions,   // text-output options
    CONST RECT* lprc, // optional dimensions  
    LPCWSTR lpString, // string
    UINT cbCount,     // number of characters in string
    CONST INT* lpDx   // array of spacing values
) {
    std::string str;
    tostring(lpString,cbCount,str);
    return ExtTextOutA(hdc,X,Y,fuOptions,lprc,str.c_str(),str.length(),lpDx);
}

Antialias WINAPI GetAntialiasMode(HDC hdc){
    return (Antialias)cairo_get_antialias(hdc->cairo);
}

Antialias WINAPI SetAntialiasMode(HDC hdc,Antialias mode){
    Antialias ret = (Antialias)cairo_get_antialias(hdc->cairo);
    cairo_set_antialias(hdc->cairo,(cairo_antialias_t)mode);
    return ret;
}

static unsigned char* getPixelData(HDC hdc, int x, int y) {
    cairo_t* cr = hdc->cairo;
    if (!hdc->bmp) return nullptr;
    cairo_matrix_t mtx;
    cairo_get_matrix(cr, &mtx);
    double dx(x), dy(y);
    cairo_matrix_transform_point(&mtx, &dx, &dy);
    cairo_surface_t* surface = (cairo_surface_t*)GetGdiObjPtr(hdc->bmp);
    cairo_format_t fmt = cairo_image_surface_get_format(surface);
    if (fmt != CAIRO_FORMAT_ARGB32)
        return nullptr;
    int wid = cairo_image_surface_get_width(surface);
    int hei = cairo_image_surface_get_height(surface);
    if (dx >= wid || dy >= hei)
        return nullptr;
    unsigned char* data = cairo_image_surface_get_data(surface);
    int offset = (dy * wid + dx) * 4;
    return data + offset;
}

COLORREF GetPixel(IN HDC hdc, IN int x, IN int y)
{
    const unsigned char* data = getPixelData(hdc, x, y);
    if (!data)
        return 0;
    unsigned char r = data[0];     
    unsigned char g = data[1]; 
    unsigned char b = data[2]; 
    unsigned char a = data[3]; 
    return RGBA(r,g,b,a);
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

UINT WINAPI RealizePalette(_In_ HDC hdc) {
    return 0;
}

HPALETTE WINAPI SelectPalette(_In_ HDC hdc, _In_ HPALETTE hPal, _In_ BOOL bForceBkgd) {
    return nullptr;
}

BOOL WINAPI DPtoLP(HDC hdc,           // handle to device context
    LPPOINT lpPoints,  // array of points
    int nCount         // count of points in array
) {
    //todo:hjx
    return TRUE;
}

BOOL WINAPI LPtoDP(HDC hdc,           // handle to device context
    LPPOINT lpPoints,  // array of points
    int nCount         // count of points in array
) {
    //todo:hjx
    return TRUE;
}

BOOL  WINAPI GetCharWidthA(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer) {
    *lpBuffer = 0;
    for (char c = (char)iFirst; c <= (char)iLast; c++) {
        SIZE sz;
        GetTextExtentPoint32A(hdc, &c, 1, &sz);
        *lpBuffer += sz.cx;
    }
    return TRUE;
}
BOOL  WINAPI GetCharWidthW(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer) {
    *lpBuffer = 0;
    for (wchar_t c = (wchar_t)iFirst; c <= (wchar_t)iLast; c++) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, &c, 1, &sz);
        *lpBuffer += sz.cx;
    }
    return TRUE;
}


HDC WINAPI CreateICA(LPCSTR lpszDriver,       // driver name
    LPCSTR lpszDevice,       // device name
    LPCSTR lpszOutput,       // port or file name
    CONST void* lpdvmInit  // optional initialization data
) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    HDC ret = new _SDC(conn->screen->root);
    SelectObject(ret, conn->GetDesktopBitmap());
    return ret;
}

HDC WINAPI CreateICW(LPCWSTR lpszDriver,       // driver name
    LPCWSTR lpszDevice,       // device name
    LPCWSTR lpszOutput,       // port or file name
    CONST void* lpdvmInit  // optional initialization data
) {
    std::string strDriver, strDevice, strOutput;
    tostring(lpszDriver, -1, strDriver);
    tostring(lpszDevice, -1, strDevice);
    tostring(lpszOutput, -1, strOutput);
    return CreateICA(strDriver.c_str(), strDevice.c_str(), strOutput.c_str(), lpdvmInit);
}
