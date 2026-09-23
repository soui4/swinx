#include <windows.h>
#include <list>
#include <algorithm>
#include <atomic>
#include <assert.h>
#include "hook.h"
#include "SRwLock.hpp"
#include "SConnection.h"
#ifdef __linux__
#include "SClipboard.h"
#endif //__linux
#include "debug.h"
#define kLogTag "hook"

using namespace swinx;

struct hook
{
    INT id;
    HOOKPROC proc;
    void *handle;
    tid_t tid;
    BOOL unicode;
    char module[MAX_PATH];
    std::atomic<int> refs;
};

// Reference counting keeps a hook object alive while its callback is running:
// the hook lists own one reference, and call_hook holds an extra reference for
// the duration of the hook proc, so UnhookWindowsHookEx from another thread
// only removes the hook from the chain and frees it once no callback is in
// flight (same observable behavior as Win32).
static void hook_acquire(hook *h)
{
    h->refs++;
}

static void hook_release(hook *h)
{
    if (--h->refs == 0)
        delete h;
}

static const char *const hook_names[WH_MAXHOOK - WH_MINHOOK] = { "WH_MSGFILTER", "WH_KEYBOARD", "WH_GETMESSAGE", "WH_CALLWNDPROC", "WH_SYSMSGFILTER", "WH_MOUSE", "WH_CALLWNDPROCRET" };

class HookMgr {
  public:
    HookMgr()
    {
    }

    ~HookMgr()
    {
        s_mutex.LockExclusive();
        for (int i = 0; i < WH_MAXHOOK - WH_MINHOOK + 1; i++)
        {
            for (auto &it : s_hooks[i])
            {
                delete it;
            }
            s_hooks[i].clear();
        }
        s_mutex.UnlockExclusive();
    }

    HHOOK set_windows_hook(INT id, HOOKPROC proc, HINSTANCE inst, tid_t tid, BOOL unicode);

    BOOL unhook(HHOOK hHook);

    LRESULT call_hook(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK get_first_hook(INT id);

    HHOOK get_next_hook(HHOOK hhk);

  private:
    UINT get_hook_timeout();

    swinx_stl::list<hook *> s_hooks[WH_MAXHOOK - WH_MINHOOK + 1];
    SRwLock s_mutex;
} s_hookMgr;

/***********************************************************************
 *		get_hook_timeout
 *
 */
UINT HookMgr::get_hook_timeout(void)
{
    return 2000;
}

/***********************************************************************
 *		set_windows_hook
 *
 * Implementation of SetWindowsHookExA and SetWindowsHookExW.
 */
HHOOK HookMgr::set_windows_hook(INT id, HOOKPROC proc, HINSTANCE inst, tid_t tid, BOOL unicode)
{
    char module[MAX_PATH] = "";
    DWORD len;

    if (!proc || id < WH_MINHOOK || id >= WH_MAXHOOK)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (inst && (!(len = GetModuleFileNameA(inst, module, MAX_PATH)) || len >= MAX_PATH))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    hook *info = new hook;
    info->id = id;
    info->handle = inst;
    info->proc = proc;
    info->tid = tid;
    info->unicode = unicode;
    strcpy(info->module, module);
    info->refs = 1; // one reference owned by the hook list
    s_mutex.LockExclusive();
    auto &lstHook = s_hooks[id];
    lstHook.push_front(info);
    s_mutex.UnlockExclusive();
    return info;
}

BOOL HookMgr::unhook(HHOOK hHook)
{
    if (!hHook)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    BOOL bRet = FALSE;
    s_mutex.LockExclusive();
    // hHook is a caller-supplied pointer that may be stale: unhooking an
    // already-unhooked handle passes a freed object here (valgrind reports
    // invalid reads). Win32 treats it as an invalid handle, so never
    // dereference it — search every hook list by pointer value instead,
    // exactly like a handle table lookup.
    for (int i = WH_MINHOOK; i < WH_MAXHOOK && !bRet; i++)
    {
        auto &lstHook = s_hooks[i];
        auto it = std::find(lstHook.begin(), lstHook.end(), hHook);
        if (it != lstHook.end())
        {
            lstHook.erase(it);
            // Drop the list's own reference. If a callback is still in flight
            // (call_hook holds a reference), the object is freed once it returns.
            hook_release(hHook);
            bRet = TRUE;
        }
    }
    s_mutex.UnlockExclusive();
    if (!bRet)
        SetLastError(ERROR_INVALID_HANDLE);
    return bRet;
}

HHOOK HookMgr::get_first_hook(INT id)
{
    HHOOK ret = NULL;
    if (id < WH_MINHOOK || id >= WH_MAXHOOK)
        return NULL;
    s_mutex.LockShared();
    if (!s_hooks[id].empty())
    {
        ret = s_hooks[id].front();
        hook_acquire(ret); // returned reference, consumed by call_hook
    }
    s_mutex.UnlockShared();
    return ret;
}

HHOOK HookMgr::get_next_hook(HHOOK hhk)
{
    if (!hhk)
        return NULL;
    HHOOK ret = NULL;
    s_mutex.LockShared();
    // hhk is guaranteed alive by the reference held for the in-flight callback.
    INT id = hhk->id;
    if (id >= WH_MINHOOK && id < WH_MAXHOOK)
    {
        auto &lstHook = s_hooks[id];
        auto it = std::find(lstHook.begin(), lstHook.end(), hhk);
        if (it != lstHook.end() && ++it != lstHook.end())
        {
            ret = *it;
            hook_acquire(ret); // returned reference, consumed by call_hook
        }
    }
    s_mutex.UnlockShared();
    return ret;
}

LRESULT HookMgr::call_hook(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    if (!hhk)
        return 0;
    LRESULT ret = 0;
    // The reference on hhk (acquired by the caller through get_first_hook /
    // get_next_hook) keeps the hook object alive for the whole callback, even
    // if another thread unhooks it meanwhile. The reference is consumed here.
    if (hhk->proc)
    {
        tid_t tid = GetCurrentThreadId();
        if (hhk->tid == tid)
            ret = hhk->proc(nCode, wParam, lParam);
        else
        {
            SConnection *conn = SConnMgr::instance()->getConnection(hhk->tid);
            if (!conn)
            {
                HHOOK next = get_next_hook(hhk);
                hook_release(hhk);
                return call_hook(next, nCode, wParam, lParam);
            }
            else
            {
                CallHookData data;
                data.proc = hhk->proc;
                data.code = nCode;
                data.wp = wParam;
                data.lp = lParam;
#ifdef __linux__
                // using clipboard owner of the target thread
                SendMessageTimeoutA(conn->getClipboard()->getClipboardOwner(), UM_CALLHOOK, 0, (LPARAM)&data, 0, get_hook_timeout(), &ret);
#endif //__linux__
            }
        }
    }
    hook_release(hhk);
    return ret;
}

/***********************************************************************
 *		SetWindowsHookA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookA(INT id, HOOKPROC proc)
{
    return SetWindowsHookExA(id, proc, 0, GetCurrentThreadId());
}

/***********************************************************************
 *		SetWindowsHookW (USER32.@)
 */
HHOOK WINAPI SetWindowsHookW(INT id, HOOKPROC proc)
{
    return SetWindowsHookExW(id, proc, 0, GetCurrentThreadId());
}

/***********************************************************************
 *		SetWindowsHookExA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExA(INT id, HOOKPROC proc, HINSTANCE inst, tid_t tid)
{
    return s_hookMgr.set_windows_hook(id, proc, inst, tid, FALSE);
}

/***********************************************************************
 *		SetWindowsHookExA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExW(INT id, HOOKPROC proc, HINSTANCE inst, tid_t tid)
{
    return s_hookMgr.set_windows_hook(id, proc, inst, tid, TRUE);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hhk)
{
    return s_hookMgr.unhook(hhk);
}

LRESULT WINAPI CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    // hhk stays valid because the in-flight call_hook holds a reference for
    // the duration of the hook proc. get_next_hook returns a referenced hook
    // whose reference is consumed by call_hook.
    HHOOK next = s_hookMgr.get_next_hook(hhk);
    return s_hookMgr.call_hook(next, nCode, wParam, lParam);
}

BOOL WINAPI CallHook(INT id, int nCode, WPARAM wParam, LPARAM lParam)
{
    HHOOK hhk = s_hookMgr.get_first_hook(id); // referenced, consumed by call_hook
    return s_hookMgr.call_hook(hhk, nCode, wParam, lParam);
}
