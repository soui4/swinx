#include <windows.h>
#include "sdc.h"
#include "log.h"
#define kLogTag "sdc"

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
{
    ptOrigin.x = ptOrigin.y = 0;
    cairo_matrix_init_identity(&mtx);
    SLOG_STMD() << "new sdc:" << this;
}

_SDC::~_SDC()
{
    SLOG_STMD() << "delete sdc:" << this << " cairo:" << cairo;
    if (cairo)
        cairo_destroy(cairo);
}

int _SDC::SaveState()
{
    if (!cairo)
        return 0;
    cairo_save(cairo);
    return nSave++;
}

BOOL _SDC::RestoreState(int nState)
{
    if (!cairo)
        return FALSE;
    if (nState >= nSave)
        return FALSE;
    if (nState < 0)
    {
        cairo_restore(cairo);
        --nSave;
    }
    else
    {
        while (nSave > nState)
        {
            cairo_restore(cairo);
            --nSave;
        }
    }
    return TRUE;
}