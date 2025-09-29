#include <multimon.h>
#include "wndobj.h"
#include "SConnection.h"

// todo:hjx
BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC, LPARAM)
{
    return FALSE;
}

BOOL WINAPI EnumDisplayDevicesW(PVOID, DWORD, PDISPLAY_DEVICEW, DWORD)
{
    return FALSE;
}

BOOL WINAPI EnumDisplayDevicesA(PVOID, DWORD, PDISPLAY_DEVICEA, DWORD)
{
    return FALSE;
}

HMONITOR MonitorFromWindow(HWND hWnd, DWORD dwFlags)
{
    WndObj pWnd = WndMgr::fromHwnd(hWnd);
    if (!pWnd)
        return FALSE;
    SConnection *conn = SConnMgr::instance()->getConnection(pWnd->tid);
    return conn->GetScreen(dwFlags);
}

HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->GetScreen(dwFlags);
}

HMONITOR MonitorFromRect(LPCRECT lprc, // rectangle
                         DWORD dwFlags // determine return value
)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->GetScreen(dwFlags);
}

BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    return GetMonitorInfoA(hMonitor, lpmi);
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    if (lpmi == nullptr || lpmi->cbSize != sizeof(MONITORINFO))
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    lpmi->rcMonitor.left = 0;
    lpmi->rcMonitor.top = 0;
    lpmi->rcMonitor.right = pConn->GetScreenWidth(hMonitor);
    lpmi->rcMonitor.bottom = pConn->GetScreenHeight(hMonitor);
    lpmi->rcWork = lpmi->rcMonitor;
    pConn->GetWorkArea(hMonitor, &lpmi->rcWork);
    return TRUE;
}
