#include <windows.h>
#include "sdc.h"
#include "log.h"
#define kLogTag "sdc"

//todo:hjx, disable gdi object state tracking now. it will cause crash.
struct DCState
{
    DCState()//:pen(0),brush(0),hfont(0)
    {
    }
    ~DCState(){
        //if(pen) DeleteObject(pen);
        //if(brush) DeleteObject(brush);
        //if(hfont) DeleteObject(brush);
    }
    // HPEN pen;
    // HBRUSH brush;
    // HFONT hfont;
    cairo_matrix_t mtx;
    POINT ptOrigin;
    COLORREF crText;
    COLORREF crBk;
    int bkMode;
    UINT textAlign;
    int rop2;
    int polyFillMode;
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
    , cairo(nullptr)
    , textAlign(TA_TOP | TA_LEFT | TA_UPDATECP)
    , rop2(R2_EXT_OVER)
    , pathRecording(FALSE)
    , currentPath(nullptr)
    , polyFillMode(ALTERNATE)
{
    ptOrigin.x = ptOrigin.y = 0;
    cairo_matrix_init_identity(&mtx);
    SLOG_STMD() << "new sdc:" << this;
}

_SDC::~_SDC()
{
    SLOG_STMD() << "delete sdc:" << this << " cairo:" << cairo;
    if (currentPath)
        cairo_path_destroy(currentPath);
    if (cairo)
        cairo_destroy(cairo);
}

int _SDC::SaveState()
{
    if (!cairo)
        return 0;
    
    auto state = std::make_shared<DCState>();
    //state->pen = RefGdiObj(pen);
    //state->brush = RefGdiObj(brush);
    //state->hfont = RefGdiObj(hfont);
    state->ptOrigin = ptOrigin;
    state->mtx = mtx;
    state->crText = crText;
    state->crBk = crBk;
    state->bkMode = bkMode;
    state->textAlign = textAlign;
    state->rop2 = rop2;
    state->polyFillMode = polyFillMode;
    
    stateStack.push_back(state);
    cairo_save(cairo);
    return nSave++;
}

static void _RestoreState(_SDC *dc, const std::shared_ptr<DCState> &state){
    dc->crText = state->crText;
    dc->crBk = state->crBk;
    // if(dc->pen){
    //     DeleteObject(dc->pen);
    // }
    // dc->pen = RefGdiObj(state->pen);
    // if(dc->brush){
    //     DeleteObject(dc->brush);
    // }
    // dc->brush = RefGdiObj(state->brush);
    // if(dc->hfont){
    //     DeleteObject(dc->hfont);
    // }
    // dc->hfont = RefGdiObj(state->hfont);
    dc->bkMode = state->bkMode;
    dc->textAlign = state->textAlign;
    dc->rop2 = state->rop2;
    dc->polyFillMode = state->polyFillMode;
    dc->ptOrigin = state->ptOrigin;
    memcpy(&dc->mtx, &state->mtx, sizeof(cairo_matrix_t));
    cairo_restore(dc->cairo);
}

BOOL _SDC::RestoreState(int nState)
{
    if (!cairo)
        return FALSE;
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