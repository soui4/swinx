#ifndef _OSSTATE_H_
#define _OSSTATE_H_
#include "SConnBase.h"

namespace swinx
{
    bool init(SConnBase *pListener);
    void shutdown();
}
#endif //_OSSTATE_H_