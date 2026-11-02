/*
 * swinx/platform/wasm/dlghelper.cpp
 *
 * WASM home of the common-dialog platform hooks.  Unlike src/platform/
 * freertos/dlghelper.cpp, this file must not re-implement ShellExecute or
 * RegisterDragDrop: the wasm build compiles src/platform/posix/shellapi.cpp
 * + shellobj.cpp (Emscripten ships a full libc, so the POSIX implementations
 * link), and duplicate definitions would break the archive.  What remains is
 * the dialog surface a browser page cannot serve -- no native file/color/font
 * picker -- so every hook fails explicitly, mirroring
 * src/platform/mobile/dlghelper.cpp.
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

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
