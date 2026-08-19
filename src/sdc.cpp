#include <windows.h>
#include "sdc.h"
#if defined(__APPLE__)
#undef interface
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "log.h"
#define kLogTag "sdc"

struct DCState
{
    DCState(HPEN _hpen, HBRUSH _hbr, HFONT _hfont)
    {
        pen = RefGdiObj(_hpen);
        brush = RefGdiObj(_hbr);
        hfont = RefGdiObj(_hfont);
    }
    ~DCState()
    {
        if (pen)
            DeleteObject(pen);
        if (brush)
            DeleteObject(brush);
        if (hfont)
            DeleteObject(hfont);
    }
    HPEN pen;
    HBRUSH brush;
    HFONT hfont;
#if defined(__APPLE__)
    CGAffineTransform worldMtx;
    FLOAT miterLimit;
    POINT curPos;
#else
    cairo_matrix_t mtx;
#endif
    POINT ptOrigin;
    COLORREF crText;
    COLORREF crBk;
    int bkMode;
    UINT textAlign;
    int rop2;
    int polyFillMode;
    POINT brushOrg;
};

_SDC::_SDC(HWND _hwnd)
    : hwnd(_hwnd)
    , nSave(0)
    , crText(RGBA(0, 0, 0, 0xff))
    , crBk(RGBA(255, 255, 255, 255))
    , pen(GetStockObject(BLACK_PEN))
    , brush(GetStockObject(WHITE_BRUSH))
    , hfont(GetStockObject(SYSTEM_FONT))
    , bmp(GetStockObject(NULL_BITMAP))
    , bkMode(OPAQUE)
    , uGetFlags(0)
    , textAlign(TA_TOP | TA_LEFT | TA_UPDATECP)
    , rop2(R2_EXT_OVER)
    , pathRecording(FALSE)
    , polyFillMode(ALTERNATE)
    , brushOrg({0, 0})
#ifdef __OHOS__
    , hasDirty(FALSE)
    , dirtyRect({ 0, 0, 0, 0 })
#endif
{
    ptOrigin.x = ptOrigin.y = 0;
#if defined(__APPLE__)
    cgCtx = nullptr;
    cgCtxOwned = FALSE;
    worldMtx = new CGAffineTransform(CGAffineTransformIdentity);
    recordedPath = nullptr;
    miterLimit = 10.0f;
#else
    cairo = nullptr;
    cairo_matrix_init_identity(&mtx);
    currentPath = nullptr;
#endif
    SLOG_STMD() << "new sdc:" << this;
}

_SDC::~_SDC()
{
    SLOG_STMD() << "delete sdc:" << this;
#if defined(__APPLE__)
    if (recordedPath)
        CFRelease(recordedPath);
    if (cgCtx && cgCtxOwned)
        CGContextRelease(cgCtx);
    delete worldMtx;
#else
    if (currentPath)
        cairo_path_destroy(currentPath);
    if (cairo)
        cairo_destroy(cairo);
#endif
}

int _SDC::SaveState()
{
#if defined(__APPLE__)
    if (!cgCtx)
        return 0;
    auto state = std::make_shared<DCState>(pen, brush, hfont);
    state->ptOrigin = ptOrigin;
    state->worldMtx = *worldMtx;
    state->miterLimit = miterLimit;
    state->crText = crText;
    state->crBk = crBk;
    state->bkMode = bkMode;
    state->textAlign = textAlign;
    state->rop2 = rop2;
    state->polyFillMode = polyFillMode;
    state->brushOrg = brushOrg;
    stateStack.push_back(state);
    CGContextSaveGState(cgCtx);
    return nSave++;
#else
    if (!cairo)
        return 0;

    auto state = std::make_shared<DCState>(pen, brush, hfont);
    state->ptOrigin = ptOrigin;
    state->mtx = mtx;
    state->crText = crText;
    state->crBk = crBk;
    state->bkMode = bkMode;
    state->textAlign = textAlign;
    state->rop2 = rop2;
    state->polyFillMode = polyFillMode;
    state->brushOrg = brushOrg;

    stateStack.push_back(state);
    cairo_save(cairo);
    return nSave++;
#endif
}

static void _RestoreState(_SDC *dc, const std::shared_ptr<DCState> &state)
{
    dc->crText = state->crText;
    dc->crBk = state->crBk;
    dc->pen = state->pen;
    dc->brush = state->brush;
    dc->hfont = state->hfont;
    dc->bkMode = state->bkMode;
    dc->textAlign = state->textAlign;
    dc->rop2 = state->rop2;
    dc->polyFillMode = state->polyFillMode;
    dc->brushOrg = state->brushOrg;
    dc->ptOrigin = state->ptOrigin;
#if defined(__APPLE__)
    *dc->worldMtx = state->worldMtx;
    dc->miterLimit = state->miterLimit;
    if (dc->cgCtx)
        CGContextRestoreGState(dc->cgCtx);
#else
    memcpy(&dc->mtx, &state->mtx, sizeof(cairo_matrix_t));
    cairo_restore(dc->cairo);
#endif
}

BOOL _SDC::RestoreState(int nState)
{
#if defined(__APPLE__)
    if (!cgCtx)
        return FALSE;
#else
    if (!cairo)
        return FALSE;
#endif
    if (nState >= nSave)
        return FALSE;

    if (nState < 0)
    {
        if (stateStack.empty())
            return FALSE;
        auto state = stateStack.back();
        stateStack.pop_back();
        _RestoreState(this, state);
        --nSave;
    }
    else
    {
        while (nSave > nState)
        {
            if (stateStack.empty())
                return FALSE;

            auto state = stateStack.back();
            stateStack.pop_back();
            _RestoreState(this, state);
            --nSave;
        }
    }
    return TRUE;
}
