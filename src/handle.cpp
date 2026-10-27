#include <windows.h>
#include "handle.h"
#include <log.h>
#include <atomic>
#define kLogTag "handle"

/* GR_GDIOBJECTS 的数据来源：当前存活的 GDI 对象数。swinx 的 GDI 句柄都包装成
 * _Handle（InitGdiObj / region.cpp 的 alloc_gdi_handle 创建），type 落在
 * OBJ_PEN..OBJ_ENHMETAFILE 区间；其余句柄类型（FILE_OBJ 等）从 20 起，不会混入。
 * HDC 不走 _Handle（由 sdc.cpp 自己 new/delete），在那里单独计数。 */
static std::atomic<int> s_gdiObjectCount(0);

static bool IsGdiObjectType(int type)
{
    return type >= OBJ_PEN && type <= OBJ_ENHMETAFILE;
}

void AddSwinxGdiObject()
{
    ++s_gdiObjectCount;
}

void ReleaseSwinxGdiObject()
{
    --s_gdiObjectCount;
}

int GetSwinxGdiObjectCount()
{
    return s_gdiObjectCount.load();
}

HANDLE WINAPI AddHandleRef(HANDLE hHandle)
{
    if (hHandle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!hHandle)
        return 0;
    _Handle *h = AsHandle(hHandle);
    h->mutex.lock();
    h->nRef++;
    h->mutex.unlock();
    return hHandle;
}

BOOL WINAPI CloseHandle(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!h)
        return TRUE;
    _Handle *handle = AsHandle(h);
    if (!handle->cbFree) // for static handle (stockobject etc.), cbFree is null and should not been free.
        return TRUE;
    handle->mutex.lock();
    BOOL bFree = --handle->nRef == 0;
    handle->mutex.unlock();
    if (bFree)
    {
        delete handle;
    }
    return TRUE;
}

HANDLE InitHandle(int type, void *ptr, FreeHandlePtr cbFree)
{
    return new _Handle(type, ptr, cbFree);
}

_Handle::_Handle(int _type, void *_ptr, FreeHandlePtr _cbFree)
    : type(_type)
    , nRef(1)
    , ptr(_ptr)
    , cbFree(_cbFree)
{
    if (IsGdiObjectType(type))
        AddSwinxGdiObject();
}

_Handle::~_Handle()
{
    if (IsGdiObjectType(type))
        ReleaseSwinxGdiObject();
    if (cbFree)
    {
        cbFree(ptr);
    }
}

/* GetGuiResources：Win32 里由 user32 提供，用来观察进程的 GDI / USER 对象占用
 * （WebCore 的 GDIObjectCounter 靠它做 Debug 泄漏检测）。swinx 没有系统级的
 * 进程对象表：GR_GDIOBJECTS 返回自己创建的 GDI 句柄数，GR_USEROBJECTS 未统计。
 * hProcess 只可能来自 GetCurrentProcess()，这里不校验。 */
DWORD WINAPI GetGuiResources(HANDLE hProcess, DWORD uiFlags)
{
    (void)hProcess;
    if (uiFlags == GR_GDIOBJECTS)
        return (DWORD)GetSwinxGdiObjectCount();
    return 0;
}
