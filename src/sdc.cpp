#include <windows.h>
#include "sdc.h"
#if defined(__APPLE__)
#undef interface
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "log.h"
#include "handle.h"
#define kLogTag "sdc"

struct DCState
{
    DCState(HPEN _hpen, HBRUSH _hbr, HFONT _hfont)
    {
        pen = (HPEN)RefGdiObj(_hpen);
        brush = (HBRUSH)RefGdiObj(_hbr);
        hfont = (HFONT)RefGdiObj(_hfont);
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
    int graphicsMode;
    POINT brushOrg;
};

HDC__::HDC__(HWND _hwnd)
    : hwnd(_hwnd)
    , bmp((HBITMAP)GetStockObject(NULL_BITMAP))
    , crText(RGBA(0, 0, 0, 0xff))
    , crBk(RGBA(255, 255, 255, 255))
    , pen((HPEN)GetStockObject(BLACK_PEN))
    , brush((HBRUSH)GetStockObject(WHITE_BRUSH))
    , hfont((HFONT)GetStockObject(SYSTEM_FONT))
    , uGetFlags(0)
    , bkMode(OPAQUE)
    , nSave(0)
    , textAlign(TA_TOP | TA_LEFT)
    , rop2(R2_EXT_OVER)
    , polyFillMode(ALTERNATE)
    , graphicsMode(GM_COMPATIBLE)
    , brushOrg({ 0, 0 })
#ifdef __OHOS__
    , hasDirty(FALSE)
    , dirtyRect({ 0, 0, 0, 0 })
#endif
    , pathRecording(FALSE)
    , recording(nullptr)
{
    ptOrigin.x = ptOrigin.y = 0;
    AddSwinxGdiObject();
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

HDC__::~HDC__()
{
    SLOG_STMD() << "delete sdc:" << this;
    ReleaseSwinxGdiObject();
    // 录制 DC 未走 CloseEnhMetaFile 就被销毁：释放录制缓冲，避免泄漏
    if (recording)
    {
        DeleteObject(recording);
        recording = nullptr;
    }
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

int HDC__::SaveState()
{
#if defined(__APPLE__)
    if (!cgCtx)
        return 0;
    auto state = std::make_shared<DCState>((HPEN)pen, (HBRUSH)brush, (HFONT)hfont);
    state->ptOrigin = ptOrigin;
    state->worldMtx = *worldMtx;
    state->miterLimit = miterLimit;
    state->crText = crText;
    state->crBk = crBk;
    state->bkMode = bkMode;
    state->textAlign = textAlign;
    state->rop2 = rop2;
    state->polyFillMode = polyFillMode;
    state->graphicsMode = graphicsMode;
    state->brushOrg = brushOrg;
    stateStack.push_back(state);
    CGContextSaveGState(cgCtx);
    // Win32 SaveDC returns a nonzero identifier (the first save returns 1);
    // callers such as cairo treat 0 as failure, so the counter is 1-based.
    return ++nSave;
#else
    if (!cairo)
        return 0;

    auto state = std::make_shared<DCState>((HPEN)pen, (HBRUSH)brush, (HFONT)hfont);
    state->ptOrigin = ptOrigin;
    state->mtx = mtx;
    state->crText = crText;
    state->crBk = crBk;
    state->bkMode = bkMode;
    state->textAlign = textAlign;
    state->rop2 = rop2;
    state->polyFillMode = polyFillMode;
    state->graphicsMode = graphicsMode;
    state->brushOrg = brushOrg;

    stateStack.push_back(state);
    cairo_save(cairo);
    // See the APPLE branch: the identifier returned by SaveDC is 1-based.
    return ++nSave;
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
    dc->graphicsMode = state->graphicsMode;
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

BOOL HDC__::RestoreState(int nState)
{
#if defined(__APPLE__)
    if (!cgCtx)
        return FALSE;
#else
    if (!cairo)
        return FALSE;
#endif
    if (nState > nSave)
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
        // Saved state N is the one recorded by SaveState() while nSave was N-1,
        // so restoring it unwinds to level N-1.
        while (nSave >= nState)
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
