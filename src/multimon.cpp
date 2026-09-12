#include <multimon.h>
#include "SConnection.h"

// 多显示器公共实现层。几何查询全部下沉到各平台 SConnection：
//   - linux: XCB RANDR（每个活跃 CRTC 对应一台显示器，根窗口坐标 = Win32 全局坐标）
//   - cocoa: NSScreen（坐标按"主屏左上为原点"的 Win32 约定翻转，见 SConnection.mm）
//   - ios/mobile: 单屏退化实现
// HMONITOR 由平台定义（linux = crtc id；cocoa = NSScreen*），跨平台不可混用。

// 与 Win32 IntersectRect 等价的本地实现，避免依赖 gdi 模块
static bool _monIntersectRect(RECT *prcDst, const RECT *a, const RECT *b)
{
    if (!prcDst || !a || !b)
        return false;
    prcDst->left = a->left > b->left ? a->left : b->left;
    prcDst->top = a->top > b->top ? a->top : b->top;
    prcDst->right = a->right < b->right ? a->right : b->right;
    prcDst->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    return prcDst->left < prcDst->right && prcDst->top < prcDst->bottom;
}

BOOL WINAPI EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM lData)
{
    if (lpfnEnum == nullptr)
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn)
        return FALSE;
    int nCount = pConn->GetMonitorCount();
    for (int i = 0; i < nCount; i++)
    {
        HMONITOR hMonitor = pConn->GetMonitor(i);
        if (!hMonitor)
            continue;
        RECT rc;
        if (!pConn->GetMonitorRect(hMonitor, &rc))
            continue;
        if (lprcClip)
        {
            // 指定裁剪矩形时只枚举与其相交的显示器，回调矩形为交集（Win32 语义）
            RECT rcInter;
            if (!_monIntersectRect(&rcInter, &rc, lprcClip))
                continue;
            rc = rcInter;
        }
        if (!lpfnEnum(hMonitor, hdc, &rc, lData))
            return FALSE; // 回调返回 FALSE 时终止枚举（Win32 语义）
    }
    return TRUE;
}

BOOL WINAPI EnumDisplayDevicesW(PVOID, DWORD, PDISPLAY_DEVICEW, DWORD)
{
    // 设备级枚举（DDI）暂不提供，仅支持显示器级枚举
    return FALSE;
}

BOOL WINAPI EnumDisplayDevicesA(PVOID lpReserved, DWORD iDevNum, PDISPLAY_DEVICEA lpDisplayDevice, DWORD)
{
    if (lpReserved != NULL)
        return FALSE;
    if (lpDisplayDevice == NULL || lpDisplayDevice->cb != sizeof(DISPLAY_DEVICEA))
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn)
        return FALSE;
    int nCount = pConn->GetMonitorCount();
    if ((int)iDevNum >= nCount)
        return FALSE;
    HMONITOR hMonitor = pConn->GetMonitor((int)iDevNum);
    RECT rc;
    if (!hMonitor || !pConn->GetMonitorRect(hMonitor, &rc))
        return FALSE;
    char szName[32];
    snprintf(szName, sizeof(szName), "\\\\.\\DISPLAY%d", (int)iDevNum + 1);
    strncpy(lpDisplayDevice->DeviceName, szName, CCHDEVICENAME - 1);
    lpDisplayDevice->DeviceName[CCHDEVICENAME - 1] = '\0';
    strncpy(lpDisplayDevice->DeviceString, "Generic Display", sizeof(lpDisplayDevice->DeviceString) - 1);
    lpDisplayDevice->DeviceString[sizeof(lpDisplayDevice->DeviceString) - 1] = '\0';
    lpDisplayDevice->StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
    if (pConn->IsPrimaryMonitor(hMonitor))
        lpDisplayDevice->StateFlags |= DISPLAY_DEVICE_PRIMARY_DEVICE;
    lpDisplayDevice->DeviceID[0] = '\0';
    lpDisplayDevice->DeviceKey[0] = '\0';
    return TRUE;
}

HMONITOR MonitorFromWindow(HWND hWnd, DWORD dwFlags)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn)
        return NULL;
    return pConn->MonitorFromWindow(hWnd, dwFlags);
}

HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn)
        return NULL;
    return pConn->MonitorFromPoint(pt, dwFlags);
}

HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn || !lprc)
        return NULL;
    return pConn->MonitorFromRect(lprc, dwFlags);
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
    if (!pConn)
        return FALSE;
    RECT rcMonitor;
    if (!pConn->GetMonitorRect(hMonitor, &rcMonitor))
        return FALSE;
    RECT rcWork;
    if (!pConn->GetMonitorWorkRect(hMonitor, &rcWork))
        rcWork = rcMonitor;
    lpmi->rcMonitor = rcMonitor;
    lpmi->rcWork = rcWork;
    lpmi->dwFlags = pConn->IsPrimaryMonitor(hMonitor) ? MONITORINFOF_PRIMARY : 0;
    return TRUE;
}
