#include <multimon.h>
#include "wndobj.h"
#include "SConnection.h"

//todo:hjx
BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC, LPARAM) {
	return FALSE;
}

BOOL WINAPI EnumDisplayDevicesW(PVOID, DWORD, PDISPLAY_DEVICEW, DWORD) {
	return FALSE;
}

BOOL WINAPI EnumDisplayDevicesA(PVOID, DWORD, PDISPLAY_DEVICEA, DWORD) {
	return FALSE;
}


HMONITOR
MonitorFromWindow(HWND hWnd, DWORD dwFlags)
{
    WndObj pWnd = WndMgr::fromHwnd(hWnd);
    if (!pWnd)
        return FALSE;
    auto conn = SConnMgr::instance()->getConnection(pWnd->tid);
    return (HMONITOR)conn->screen;
}

HMONITOR MonitorFromPoint(POINT pt,      // point 
    DWORD dwFlags  // determine return value
) {
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return (HMONITOR)pConn->screen;
}

HMONITOR MonitorFromRect(LPCRECT lprc,    // rectangle
    DWORD dwFlags    // determine return value
) {
    //todo:hjx
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return (HMONITOR)pConn->screen;
}


BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) {
    return GetMonitorInfoA(hMonitor, lpmi);
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    if (lpmi == nullptr || lpmi->cbSize != sizeof(MONITORINFO))
        return FALSE;
    xcb_screen_t* screen = (xcb_screen_t*)hMonitor;

    lpmi->rcMonitor.left = 0;
    lpmi->rcMonitor.top = 0;
    lpmi->rcMonitor.right = screen->width_in_pixels;
    lpmi->rcMonitor.bottom = screen->height_in_pixels;

    lpmi->rcWork = lpmi->rcMonitor;
    SConnection* pConn = SConnMgr::instance()->getConnection();
    pConn->GetWorkArea(&lpmi->rcWork);
    return TRUE;
}
