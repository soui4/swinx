#include <multimon.h>
#include <stdio.h>
#include <string.h>
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

/* MONITORINFO 公共部分；MONITORINFOEX 的设备名由调用方另行填充。 */
static BOOL _getMonitorInfoBase(HMONITOR hMonitor, LPMONITORINFO lpmi, char *szDevice, size_t nDeviceLen)
{
    // Win32 允许调用方传入 MONITORINFOEX（cbSize 更大），只要不小于 MONITORINFO
    if (lpmi == nullptr || lpmi->cbSize < sizeof(MONITORINFO))
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
    if (szDevice && nDeviceLen)
    { // MONITORINFOEX 追加的设备名：与 EnumDisplayDevices 生成的命名保持一致
        szDevice[0] = '\0';
        int nCount = pConn->GetMonitorCount();
        for (int i = 0; i < nCount; i++)
        {
            if (pConn->GetMonitor(i) == hMonitor)
            {
                snprintf(szDevice, nDeviceLen, "\\\\.\\DISPLAY%d", i + 1);
                break;
            }
        }
    }
    return TRUE;
}

BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    char szDevice[CCHDEVICENAME] = { 0 };
    if (!_getMonitorInfoBase(hMonitor, lpmi, lpmi && lpmi->cbSize >= sizeof(MONITORINFOEXW) ? szDevice : nullptr, sizeof(szDevice)))
        return FALSE;
    if (lpmi->cbSize >= sizeof(MONITORINFOEXW))
        for (int i = 0; i < CCHDEVICENAME; i++)
            ((MONITORINFOEXW *)lpmi)->szDevice[i] = (WCHAR)(unsigned char)szDevice[i];
    return TRUE;
}

BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi)
{
    char szDevice[CCHDEVICENAME] = { 0 };
    if (!_getMonitorInfoBase(hMonitor, lpmi, lpmi && lpmi->cbSize >= sizeof(MONITORINFOEXA) ? szDevice : nullptr, sizeof(szDevice)))
        return FALSE;
    if (lpmi->cbSize >= sizeof(MONITORINFOEXA))
    {
        MONITORINFOEXA *pmiEx = (MONITORINFOEXA *)lpmi;
        memcpy(pmiEx->szDevice, szDevice, CCHDEVICENAME);
    }
    return TRUE;
}

/* 把 EnumDisplayDevices 生成的 "\\\\.\\DISPLAYn" 解析成 0 基显示器序号。
 * 无法解析时返回 -1。 */
static int _monDeviceIndexFromName(const char *szDevice)
{
    if (!szDevice || !szDevice[0])
        return -1;
    const char *p = szDevice + strlen(szDevice);
    while (p > szDevice && p[-1] >= '0' && p[-1] <= '9')
        p--;
    if (*p == '\0')
        return -1; // 末尾不是数字
    int n = 0;
    while (*p >= '0' && *p <= '9')
        n = n * 10 + (*p++ - '0');
    return n > 0 ? n - 1 : -1;
}

/* 按 0 基序号取显示器；nMonitorIndex < 0 表示取主显示器（无主显示器时取第一台）。
 * 失败返回 NULL。 */
static HMONITOR _monMonitorByIndex(SConnection *pConn, int nMonitorIndex, int *pnRealIndex)
{
    int nCount = pConn->GetMonitorCount();
    if (nCount <= 0)
        return NULL;
    HMONITOR hMonitor = NULL;
    if (nMonitorIndex >= 0)
    {
        if (nMonitorIndex >= nCount)
            return NULL;
        hMonitor = pConn->GetMonitor(nMonitorIndex);
    }
    else
    {
        int nPrimary = -1;
        for (int i = 0; i < nCount; i++)
        {
            HMONITOR h = pConn->GetMonitor(i);
            if (h && pConn->IsPrimaryMonitor(h))
            {
                nPrimary = i;
                break;
            }
        }
        if (nPrimary < 0)
            nPrimary = 0;
        hMonitor = pConn->GetMonitor(nPrimary);
        if (hMonitor)
            nMonitorIndex = nPrimary;
    }
    if (hMonitor && pnRealIndex)
        *pnRealIndex = nMonitorIndex;
    return hMonitor;
}

/* DEVMODE 里除 dmDeviceName / dmFormName 之外的字段在 A/W 两个版本中布局一致，
 * 可以逐字段搬运。跳过 dmSize / dmDriverExtra（由调用方缓冲区决定），
 * 也跳过与 dmOrientation/dmPosition 共用的匿名联合体。 */
#define SWINX_DEVMODE_COPY_FIELDS(dst, src)              \
    do {                                                 \
        (dst).dmSpecVersion = (src).dmSpecVersion;       \
        (dst).dmDriverVersion = (src).dmDriverVersion;   \
        (dst).dmFields = (src).dmFields;                 \
        (dst).dmColor = (src).dmColor;                   \
        (dst).dmDuplex = (src).dmDuplex;                 \
        (dst).dmYResolution = (src).dmYResolution;       \
        (dst).dmTTOption = (src).dmTTOption;             \
        (dst).dmCollate = (src).dmCollate;               \
        (dst).dmLogPixels = (src).dmLogPixels;           \
        (dst).dmBitsPerPel = (src).dmBitsPerPel;         \
        (dst).dmPelsWidth = (src).dmPelsWidth;           \
        (dst).dmPelsHeight = (src).dmPelsHeight;         \
        (dst).dmDisplayFlags = (src).dmDisplayFlags;     \
        (dst).dmDisplayFrequency = (src).dmDisplayFrequency; \
        (dst).dmICMMethod = (src).dmICMMethod;           \
        (dst).dmICMIntent = (src).dmICMIntent;           \
        (dst).dmMediaType = (src).dmMediaType;           \
        (dst).dmDitherType = (src).dmDitherType;         \
        (dst).dmReserved1 = (src).dmReserved1;           \
        (dst).dmReserved2 = (src).dmReserved2;           \
        (dst).dmPanningWidth = (src).dmPanningWidth;     \
        (dst).dmPanningHeight = (src).dmPanningHeight;   \
    } while (0)

/* swinx 没有显示模式表，只支持「当前设置」（ENUM_CURRENT_SETTINGS）。
 * 像素深度与刷新率没有跨平台的查询入口，分别按 32bpp / 60Hz 报告——
 * WebKit 只消费 dmBitsPerPel 与 dmColor（见 platform/win/PlatformScreenWin.cpp）。 */
static BOOL _devModeForMonitor(LPDEVMODEW pdm, int nMonitorIndex)
{
    if (!pdm)
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (!pConn)
        return FALSE;
    int nRealIndex = 0;
    HMONITOR hMonitor = _monMonitorByIndex(pConn, nMonitorIndex, &nRealIndex);
    if (!hMonitor)
        return FALSE;
    RECT rc;
    if (!pConn->GetMonitorRect(hMonitor, &rc))
        return FALSE;

    memset(pdm, 0, sizeof(DEVMODEW));
    pdm->dmSize = sizeof(DEVMODEW);
    pdm->dmDriverExtra = 0;
    pdm->dmSpecVersion = 0x0401; // DM_SPECVERSION
    pdm->dmDriverVersion = 0x0401;
    pdm->dmBitsPerPel = 32;
    pdm->dmPelsWidth = (DWORD)(rc.right - rc.left);
    pdm->dmPelsHeight = (DWORD)(rc.bottom - rc.top);
    pdm->dmDisplayFrequency = 60;
    pdm->dmColor = DMCOLOR_COLOR;
    pdm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_COLOR;

    char szName[CCHDEVICENAME];
    snprintf(szName, sizeof(szName), "\\\\.\\DISPLAY%d", nRealIndex + 1);
    for (int i = 0; i < CCHDEVICENAME; i++)
        pdm->dmDeviceName[i] = (WCHAR)(unsigned char)szName[i];
    return TRUE;
}

BOOL WINAPI EnumDisplaySettingsW(LPCWSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEW lpDevMode)
{
    if (!lpDevMode)
        return FALSE;
    // 没有模式表：除「当前设置」以外的 iModeNum 一律视为枚举结束
    if (iModeNum != ENUM_CURRENT_SETTINGS && iModeNum != ENUM_REGISTRY_SETTINGS)
        return FALSE;
    if (lpDevMode->dmSize < sizeof(DEVMODEW))
        return FALSE;

    int nIndex = -1;
    if (lpszDeviceName && lpszDeviceName[0])
    { // 只支持 swinx 自己生成的 ASCII 设备名
        char szName[CCHDEVICENAME];
        int i = 0;
        for (; i < CCHDEVICENAME - 1 && lpszDeviceName[i]; i++)
        {
            if (lpszDeviceName[i] > 0x7f)
                return FALSE;
            szName[i] = (char)lpszDeviceName[i];
        }
        szName[i] = '\0';
        nIndex = _monDeviceIndexFromName(szName);
        if (nIndex < 0)
            return FALSE;
    }

    DEVMODEW dm;
    if (!_devModeForMonitor(&dm, nIndex))
        return FALSE;
    for (int i = 0; i < CCHDEVICENAME; i++)
        lpDevMode->dmDeviceName[i] = dm.dmDeviceName[i];
    SWINX_DEVMODE_COPY_FIELDS(*lpDevMode, dm);
    lpDevMode->dmSize = (WORD)sizeof(DEVMODEW);
    lpDevMode->dmDriverExtra = 0;
    return TRUE;
}

BOOL WINAPI EnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEA lpDevMode)
{
    if (!lpDevMode)
        return FALSE;
    if (iModeNum != ENUM_CURRENT_SETTINGS && iModeNum != ENUM_REGISTRY_SETTINGS)
        return FALSE;
    if (lpDevMode->dmSize < sizeof(DEVMODEA))
        return FALSE;

    int nIndex = -1;
    if (lpszDeviceName && lpszDeviceName[0])
    {
        nIndex = _monDeviceIndexFromName(lpszDeviceName);
        if (nIndex < 0)
            return FALSE;
    }

    DEVMODEW dm;
    if (!_devModeForMonitor(&dm, nIndex))
        return FALSE;
    for (int i = 0; i < CCHDEVICENAME; i++)
        lpDevMode->dmDeviceName[i] = (char)(unsigned char)dm.dmDeviceName[i];
    SWINX_DEVMODE_COPY_FIELDS(*lpDevMode, dm);
    lpDevMode->dmSize = (WORD)sizeof(DEVMODEA);
    lpDevMode->dmDriverExtra = 0;
    for (int i = 0; i < CCHFORMNAME; i++)
        lpDevMode->dmFormName[i] = (char)(unsigned char)dm.dmFormName[i];
    return TRUE;
}
