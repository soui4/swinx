#include <windows.h>
#include "list.h"
#include "SConnection.h"
#include "hook.h"
#include <assert.h>
#include "debug.h"
#define kLogTag "hook"

struct hook_info
{
    INT id;
    void *proc;
    void *handle;
    DWORD pid, tid;
    BOOL prev_unicode, next_unicode;
    WCHAR module[MAX_PATH];
};

#define WH_WINEVENT (WH_MAXHOOK + 1)

static const char *const hook_names[WH_WINEVENT - WH_MINHOOK + 1] = { "WH_MSGFILTER", "WH_JOURNALRECORD", "WH_JOURNALPLAYBACK", "WH_KEYBOARD", "WH_GETMESSAGE", "WH_CALLWNDPROC", "WH_CBT", "WH_SYSMSGFILTER", "WH_MOUSE", "WH_HARDWARE", "WH_DEBUG", "WH_SHELL", "WH_FOREGROUNDIDLE", "WH_CALLWNDPROCRET", "WH_KEYBOARD_LL", "WH_MOUSE_LL", "WH_WINEVENT" };

/***********************************************************************
 *		get_ll_hook_timeout
 *
 */
static UINT get_ll_hook_timeout(void)
{
    /* FIXME: should retrieve LowLevelHooksTimeout in HKEY_CURRENT_USER\Control Panel\Desktop */
    return 2000;
}

/***********************************************************************
 *		set_windows_hook
 *
 * Implementation of SetWindowsHookExA and SetWindowsHookExW.
 */
static HHOOK set_windows_hook(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid, BOOL unicode)
{
    HHOOK handle = 0;
    char module[MAX_PATH];
    DWORD len;

    if (!proc)
    {
        SetLastError(ERROR_INVALID_FILTER_PROC);
        return 0;
    }

    if (tid) /* thread-local hook */
    {
        if (id == WH_JOURNALRECORD || id == WH_JOURNALPLAYBACK || id == WH_KEYBOARD_LL || id == WH_MOUSE_LL || id == WH_SYSMSGFILTER)
        {
            /* these can only be global */
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
    }
    else /* system-global hook */
    {
        if (id == WH_KEYBOARD_LL || id == WH_MOUSE_LL)
            inst = 0;
        else if (!inst)
        {
            SetLastError(ERROR_HOOK_NEEDS_HMOD);
            return 0;
        }
    }

    if (inst && (!(len = GetModuleFileName(inst, module, MAX_PATH)) || len >= MAX_PATH))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /*
        SERVER_START_REQ( set_hook )
        {
            req->id        = id;
            req->pid       = 0;
            req->tid       = tid;
            req->event_min = EVENT_MIN;
            req->event_max = EVENT_MAX;
            req->flags     = WINEVENT_INCONTEXT;
            req->unicode   = unicode;
            if (inst) // make proc relative to the module base
            {
                req->proc = wine_server_client_ptr( (void *)((char *)proc - (char *)inst) );
                wine_server_add_data( req, module, lstrlenW(module) * sizeof(WCHAR) );
            }
            else req->proc = wine_server_client_ptr( proc );

            if (!wine_server_call_err( req ))
            {
                handle = wine_server_ptr_handle( reply->handle );
                get_user_thread_info()->active_hooks = reply->active_hooks;
            }
        }
        SERVER_END_REQ;
        */
    TRACE("%s %p %x -> %p\n", hook_names[id - WH_MINHOOK], proc, tid, handle);
    return handle;
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
    return set_windows_hook(id, proc, inst, tid, FALSE);
}

/***********************************************************************
 *		SetWindowsHookExA (USER32.@)
 */
HHOOK WINAPI SetWindowsHookExW(INT id, HOOKPROC proc, HINSTANCE inst, DWORD tid)
{
    return set_windows_hook(id, proc, inst, tid, TRUE);
}
