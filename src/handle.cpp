#include <windows.h>
#include "handle.h"

HANDLE WINAPI AddHandleRef(HANDLE hHandle)
{
    if (hHandle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!hHandle)
        return 0;
    hHandle->mutex.lock();
    hHandle->nRef++;
    hHandle->mutex.unlock();
    return hHandle;
}

BOOL WINAPI CloseHandle(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!h)
        return TRUE;
    if (!h->cbFree) //for static handle (stockobject etc.), cbFree is null and should not been free.
        return TRUE;
    h->mutex.lock();
    BOOL bFree = --h->nRef == 0;
    h->mutex.unlock();
    if (bFree)
    {
        delete h;
    }
    return TRUE;
}

HANDLE InitHandle(int type, void *ptr, FreeHandlePtr cbFree)
{
    return new _Handle(type, ptr, cbFree);
}

_Handle::_Handle(int _type, void *_ptr, FreeHandlePtr _cbFree)
    : nRef(1)
    , type(_type)
    , ptr(_ptr)
    , cbFree(_cbFree)
{
}

_Handle::~_Handle()
{
    if (cbFree)
    {
        cbFree(ptr);
    }
}
