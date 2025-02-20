#ifndef _SIM_CONTEXT_H_
#define _SIM_CONTEXT_H_

extern "C"{
#include <imclient.h>
#include <encoding.h>
}
#include <unknwn.h>
#include "SUnkImpl.h"

typedef struct _IMContext : SUnkImpl<IUnknown>{

    xcb_xim_t* xim;
    xcb_xic_t xic;

    _IMContext():xim(nullptr),xic(0){

    }

    IUNKNOWN_BEGIN(IUnknown)
    IUNKNOWN_END()
}IMContext;

#endif//_SIM_CONTEXT_H_