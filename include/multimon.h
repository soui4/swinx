#ifndef _MULTIMON_H_
#define _MULTIMON_H_
#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    HMONITOR MonitorFromWindow(HWND hwnd, DWORD dwFlags);
    HMONITOR MonitorFromPoint(POINT pt,     // point
                              DWORD dwFlags // determine return value
    );

    HMONITOR MonitorFromRect(LPCRECT lprc, // rectangle
                             DWORD dwFlags // determine return value
    );

    BOOL GetMonitorInfoA(HMONITOR hMonitor, LPMONITORINFO lpmi);
    BOOL GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi);
#ifdef UNICODE
#define GetMonitorInfo GetMonitorInfoW
#else
#define GetMonitorInfo GetMonitorInfoA
#endif // UNICODE

    typedef BOOL(CALLBACK *MONITORENUMPROC)(HMONITOR, HDC, LPRECT, LPARAM);

    BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC, LPARAM);

    typedef struct _DISPLAY_DEVICEA
    {
        DWORD cb;
        CHAR DeviceName[32];
        CHAR DeviceString[128];
        DWORD StateFlags;
        CHAR DeviceID[128];
        CHAR DeviceKey[128];
    } DISPLAY_DEVICEA, *PDISPLAY_DEVICEA, *LPDISPLAY_DEVICEA;
    typedef struct _DISPLAY_DEVICEW
    {
        DWORD cb;
        WCHAR DeviceName[32];
        WCHAR DeviceString[128];
        DWORD StateFlags;
        WCHAR DeviceID[128];
        WCHAR DeviceKey[128];
    } DISPLAY_DEVICEW, *PDISPLAY_DEVICEW, *LPDISPLAY_DEVICEW;

    BOOL WINAPI EnumDisplayDevicesW(PVOID, DWORD, PDISPLAY_DEVICEW, DWORD);
    BOOL WINAPI EnumDisplayDevicesA(PVOID, DWORD, PDISPLAY_DEVICEA, DWORD);

#ifdef UNICODE
    typedef DISPLAY_DEVICEW DISPLAY_DEVICE;
    typedef PDISPLAY_DEVICEW PDISPLAY_DEVICE;
    typedef LPDISPLAY_DEVICEW LPDISPLAY_DEVICE;
#define EnumDisplayDevices EnumDisplayDevicesW
#else
typedef DISPLAY_DEVICEA DISPLAY_DEVICE;
typedef PDISPLAY_DEVICEA PDISPLAY_DEVICE;
typedef LPDISPLAY_DEVICEA LPDISPLAY_DEVICE;
#define EnumDisplayDevices EnumDisplayDevicesA
#endif // UNICODE

#define CCHFORMNAME   32
#define CCHDEVICENAME 32

    /* MONITORINFOEX 在 MONITORINFO 之后追加显示器设备名。
     * Win32 的 GetMonitorInfo 允许 cbSize 取 sizeof(MONITORINFOEX)——
     * WebKit 的 PlatformScreenWin 就是这么用的。 */
    typedef struct tagMONITORINFOEXA
    {
        DWORD cbSize;
        RECT rcMonitor;
        RECT rcWork;
        DWORD dwFlags;
        CHAR szDevice[CCHDEVICENAME];
    } MONITORINFOEXA, *LPMONITORINFOEXA;

    typedef struct tagMONITORINFOEXW
    {
        DWORD cbSize;
        RECT rcMonitor;
        RECT rcWork;
        DWORD dwFlags;
        WCHAR szDevice[CCHDEVICENAME];
    } MONITORINFOEXW, *LPMONITORINFOEXW;

#ifdef UNICODE
    typedef MONITORINFOEXW MONITORINFOEX;
    typedef LPMONITORINFOEXW LPMONITORINFOEX;
#else
typedef MONITORINFOEXA MONITORINFOEX;
typedef LPMONITORINFOEXA LPMONITORINFOEX;
#endif // UNICODE

// DISPLAY_DEVICE StateFlags (subset of Win32 constants actually set here)
#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP 0x00000001
#define DISPLAY_DEVICE_PRIMARY_DEVICE      0x00000004

    typedef struct _devicemodea
    {
        char dmDeviceName[CCHDEVICENAME];
        WORD dmSpecVersion;
        WORD dmDriverVersion;
        WORD dmSize;
        WORD dmDriverExtra;
        DWORD dmFields;
        union {
            struct
            {
                short dmOrientation;
                short dmPaperSize;
                short dmPaperLength;
                short dmPaperWidth;
                short dmScale;
                short dmCopies;
                short dmDefaultSource;
                short dmPrintQuality;
            };
            struct
            {
                POINTL dmPosition;
                DWORD dmDisplayOrientation;
                DWORD dmDisplayFixedOutput;
            };
        };
        short dmColor;
        short dmDuplex;
        short dmYResolution;
        short dmTTOption;
        short dmCollate;
        char dmFormName[CCHFORMNAME];
        WORD dmLogPixels;
        DWORD dmBitsPerPel;
        DWORD dmPelsWidth;
        DWORD dmPelsHeight;
        union {
            DWORD dmDisplayFlags;
            DWORD dmNup;
        };
        DWORD dmDisplayFrequency;
        DWORD dmICMMethod;
        DWORD dmICMIntent;
        DWORD dmMediaType;
        DWORD dmDitherType;
        DWORD dmReserved1;
        DWORD dmReserved2;
        DWORD dmPanningWidth;
        DWORD dmPanningHeight;
    } DEVMODEA, *PDEVMODEA, *NPDEVMODEA, *LPDEVMODEA;

    typedef struct _devicemodew
    {
        WCHAR dmDeviceName[CCHDEVICENAME];
        WORD dmSpecVersion;
        WORD dmDriverVersion;
        WORD dmSize;
        WORD dmDriverExtra;
        DWORD dmFields;
        union {
            struct
            {
                short dmOrientation;
                short dmPaperSize;
                short dmPaperLength;
                short dmPaperWidth;
                short dmScale;
                short dmCopies;
                short dmDefaultSource;
                short dmPrintQuality;
            };
            struct
            {
                POINTL dmPosition;
                DWORD dmDisplayOrientation;
                DWORD dmDisplayFixedOutput;
            };
        };
        short dmColor;
        short dmDuplex;
        short dmYResolution;
        short dmTTOption;
        short dmCollate;
        WCHAR dmFormName[CCHFORMNAME];
        WORD dmLogPixels;
        DWORD dmBitsPerPel;
        DWORD dmPelsWidth;
        DWORD dmPelsHeight;
        union {
            DWORD dmDisplayFlags;
            DWORD dmNup;
        };
        DWORD dmDisplayFrequency;
        DWORD dmICMMethod;
        DWORD dmICMIntent;
        DWORD dmMediaType;
        DWORD dmDitherType;
        DWORD dmReserved1;
        DWORD dmReserved2;
        DWORD dmPanningWidth;
        DWORD dmPanningHeight;
    } DEVMODEW, *PDEVMODEW, *NPDEVMODEW, *LPDEVMODEW;

#ifdef UNICODE
    typedef DEVMODEW DEVMODE, *PDEVMODE, *NPDEVMODE, *LPDEVMODE;
#else
typedef DEVMODEA DEVMODE, *PDEVMODE, *NPDEVMODE, *LPDEVMODE;
#endif // UNICODE

    /* DEVMODE.dmFields 位 */
#define DM_ORIENTATION        0x00000001
#define DM_PAPERSIZE          0x00000002
#define DM_PAPERLENGTH        0x00000004
#define DM_PAPERWIDTH         0x00000008
#define DM_SCALE              0x00000010
#define DM_POSITION           0x00000020
#define DM_NUP                0x00000040
#define DM_DISPLAYORIENTATION 0x00000080
#define DM_COPIES             0x00000100
#define DM_DEFAULTSOURCE      0x00000200
#define DM_PRINTQUALITY       0x00000400
#define DM_COLOR              0x00000800
#define DM_DUPLEX             0x00001000
#define DM_YRESOLUTION        0x00002000
#define DM_TTOPTION           0x00004000
#define DM_COLLATE            0x00008000
#define DM_FORMNAME           0x00010000
#define DM_LOGPIXELS          0x00020000
#define DM_BITSPERPEL         0x00040000
#define DM_PELSWIDTH          0x00080000
#define DM_PELSHEIGHT         0x00100000
#define DM_DISPLAYFLAGS       0x00200000
#define DM_DISPLAYFREQUENCY   0x00400000
#define DM_ICMMETHOD          0x00800000
#define DM_ICMINTENT          0x01000000
#define DM_MEDIATYPE          0x02000000
#define DM_DITHERTYPE         0x04000000
#define DM_PANNINGWIDTH       0x08000000
#define DM_PANNINGHEIGHT      0x10000000
#define DM_DISPLAYFIXEDOUTPUT 0x20000000

    /* dmDisplayFlags 取值 */
#define DM_INTERLACED     0x00000002
#define DMDISPLAYFLAGS_3D 0x00000008

    /* dmColor 取值 */
#define DMCOLOR_MONOCHROME 1
#define DMCOLOR_COLOR      2

#define ENUM_CURRENT_SETTINGS  ((DWORD)-1)
#define ENUM_REGISTRY_SETTINGS ((DWORD)-2)

    /* 只支持 ENUM_CURRENT_SETTINGS（swinx 没有显示模式表）；
     * lpszDeviceName 为 NULL 时取主显示器，否则按 "\\\\.\\DISPLAYn" 匹配。 */
    BOOL WINAPI EnumDisplaySettingsA(LPCSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEA lpDevMode);
    BOOL WINAPI EnumDisplaySettingsW(LPCWSTR lpszDeviceName, DWORD iModeNum, LPDEVMODEW lpDevMode);

#ifdef UNICODE
#define EnumDisplaySettings EnumDisplaySettingsW
#else
#define EnumDisplaySettings EnumDisplaySettingsA
#endif // UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_MULTIMON_H_