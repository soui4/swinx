#ifndef _SIM_CONTEXT_H_
#define _SIM_CONTEXT_H_

extern "C"{
#include <imclient.h>
#include <encoding.h>
}
#include <unknwn.h>
#include "SUnkImpl.h"

typedef struct _IMContext : SUnkImpl<IUnknown>{
    xcb_window_t hWnd;
    xcb_xim_t* xim;
    xcb_xic_t xic;

    _IMContext(HWND _hWnd,xcb_xim_t *_xim):hWnd(_hWnd),xim(_xim){

    }

    IUNKNOWN_BEGIN(IUnknown)
    IUNKNOWN_END()
}IMContext;

#endif//_SIM_CONTEXT_H_