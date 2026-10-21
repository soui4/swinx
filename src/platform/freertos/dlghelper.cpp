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
