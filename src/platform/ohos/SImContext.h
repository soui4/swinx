#ifndef _OHOS_SIM_CONTEXT_H_
#define _OHOS_SIM_CONTEXT_H_

#include <unknwn.h>
#include "SUnkImpl.h"

typedef struct _IMContext : SUnkImpl<IUnknown> {
    _IMContext() {}

    IUNKNOWN_BEGIN(IUnknown)
    IUNKNOWN_END()
} IMContext;

#endif // _OHOS_SIM_CONTEXT_H_
