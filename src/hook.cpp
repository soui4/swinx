#include <windows.h>
#include <list>
#include <algorithm>
#include <assert.h>
#include "hook.h"
#include "SRwLock.hpp"
#include "SConnection.h"
#include "SClipboard.h"
#include "debug.h"
#define kLogTag "hook"

using namespace swinx;

struct hook
{
    INT id;
    HOOKPROC proc;
    void *handle;
    DWORD tid;
    BOOL unicode;
    char module[MAX_PATH];
};


static const char *const hook_names[WH_MAXHOOK - WH_MINHOOK + 1] = { "WH_MSGFILTER", "WH_KEYBOARD", "WH_GETMESSAGE", "WH_CALLWNDPROC", "WH_SYSMSGFILTER", "WH_MOUSE", "WH_CALLWNDPROCRET"};

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

    HHOOK set_windows_hook(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid, BOOL unicode);

    BOOL unhook(HHOOK hHook);

    LRESULT call_next_hook(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK get_first_hook(INT id);
  private:
    UINT get_hook_timeout();

    std::list<hook *> s_hooks[WH_MAXHOOK - WH_MINHOOK + 1];
    SRwLock s_mutex;
}s_hookMgr;

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
HHOOK HookMgr::set_windows_hook(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid, BOOL unicode)
{
    char module[MAX_PATH];
    DWORD len;

    if (!proc)
    {
        SetLastError(ERROR_INVALID_FILTER_PROC);
        return 0;
    }
    if (inst && (!(len = GetModuleFileName(inst, module, MAX_PATH)) || len >= MAX_PATH))
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
    s_mutex.LockExclusive();
    auto &lstHook = s_hooks[id];
    lstHook.push_front(info);
    s_mutex.UnlockExclusive();
    TRACE("%s %p %x -> %p\n", hook_names[id - WH_MINHOOK], proc, tid, info);
    return info;
}

BOOL HookMgr::unhook(HHOOK hHook)
{
    BOOL bRet = FALSE;
    s_mutex.LockExclusive();
    auto &lstHook = s_hooks[hHook->id];
    auto it = std::find(lstHook.begin(), lstHook.end(), hHook);
    if (it != lstHook.end())
    {
        lstHook.erase(it);
        delete hHook;
        bRet = TRUE;
    }
    s_mutex.UnlockExclusive();
    return bRet;
}

HHOOK HookMgr::get_first_hook(INT id)
{
    HHOOK ret = NULL;
    s_mutex.LockShared();
    if (!s_hooks[id].empty())
    {
        ret = s_hooks[id].front();
    }
    s_mutex.UnlockShared();
    return ret;
}

LRESULT HookMgr::call_next_hook(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    assert(hhk);
    LRESULT ret = -1;
    s_mutex.LockShared();
    auto it = std::find(s_hooks[hhk->id].begin(), s_hooks[hhk->id].end(), hhk);
    HOOKPROC proc = NULL;
    tid_t tid = 0;
    if (it != s_hooks[hhk->id].end())
    {
        if (++it != s_hooks[hhk->id].end())
        {
            proc = (*it)->proc;
            tid = (*it)->tid;
        }
    }
    s_mutex.UnlockShared();
    if (proc)
    {
        if (tid == GetCurrentThreadId())
            ret = proc(nCode, wParam, lParam);
        else
        {
            SConnection *conn = SConnMgr::instance()->getConnection(tid);
            if (!conn)
            {
                return call_next_hook(hhk, nCode, wParam, lParam);
            }
            else
            {
                CallHookData data;
                data.proc = proc;
                data.code = nCode;
                data.wp = wParam;
                data.lp = lParam;
                SendMessageTimeoutA(conn->getClipboard()->getClipboardOwner(), UM_CALLHOOK, 0, (LPARAM)&data, 0, get_hook_timeout(), &ret);
            }
        }
    }
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
HHOOK WINAPI SetWindowsHookExA(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid)
{
    return s_hookMgr.set_windows_hook(id, proc, inst, tid, FALSE);
}

/***********************************************************************
 *		SetWindowsHookExA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExW(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid)
{
    return s_hookMgr.set_windows_hook(id, proc, inst, tid, TRUE);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK hhk)
{
    return s_hookMgr.unhook(hhk);
}

LRESULT WINAPI CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    return s_hookMgr.call_next_hook(hhk,nCode,wParam,lParam);
}

BOOL WINAPI CallHook(INT id, int nCode, WPARAM wParam, LPARAM lParam)
{
    HHOOK hHook = s_hookMgr.get_first_hook(id);
    if (!hHook)
        return FALSE;
    CallNextHookEx(hHook, nCode, wParam, lParam);
    return TRUE;
}
