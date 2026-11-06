/*
 * swinx/platform/freertos/dlghelper.cpp
 *
 * FreeRTOS home of the common-dialog / shell platform hooks, replacing
 * src/shellapi.cpp (fork/exec ShellExecute) and src/shellobj.cpp (OLE
 * drag-drop) plus the per-platform dlghelper (mobile/ios/cocoa/linux) on
 * the FreeRTOS build.  Mirrors src/platform/mobile/dlghelper.cpp: every
 * hook fails gracefully because bare metal has neither a shell nor
 * interactive dialogs.
 */
#include <windows.h>
#include <commdlg.h>
#include "SwinxUtils.h"

BOOL SGetOpenFileNameA(LPOPENFILENAMEA, DlgMode)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL SChooseColor(HWND, const COLORREF *, COLORREF *)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* --- shell / drag-drop (src/shellapi.cpp + src/shellobj.cpp on POSIX) ---- */

HRESULT RegisterDragDrop(HWND hWnd, LPDROPTARGET pTarget)
{
    (void)hWnd;
    (void)pTarget;
    return E_FAIL;
}

BOOL ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show)
{
    (void)hwnd;
    (void)op;
    (void)file;
    (void)params;
    (void)dir;
    (void)show;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* 异步文件选择：裸金属没有对话框，同步钩子本身就是显式拒绝，这里仍然按异步契约把
 * 那个失败（nResult = -1）回调出去，而不是让 SwinxPickFileAsyncA 直接返回 0 ——
 * 调用方只写一条回调路径就能覆盖所有平台（契约见 src/SwinxUtils.h）。 */
int swinx_pick_file_async(LPOPENFILENAMEA p, DlgMode mode, SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    return swinx_pick_file_deliver(SGetOpenFileNameA(p, mode) ? 1 : 0, p->lpstrFile, pfnDone, pCtx);
}
