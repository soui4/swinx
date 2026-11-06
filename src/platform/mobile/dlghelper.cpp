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

/* 异步文件选择：宿主桥（g_platformAPI，现 v4）里没有文件选择这一项，同步钩子本身就是
 * 显式拒绝；这里仍按异步契约把失败（nResult = -1）回调出去，调用方只写一条回调路径
 * 就能覆盖所有平台（契约见 src/SwinxUtils.h）。 */
int swinx_pick_file_async(LPOPENFILENAMEA p, DlgMode mode, SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    return swinx_pick_file_deliver(SGetOpenFileNameA(p, mode) ? 1 : 0, p->lpstrFile, pfnDone, pCtx);
}
