#include <multimon.h>
#include "wndobj.h"

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
    /*
    xcb_randr_get_screen_resources_cookie_t resources_cookie = xcb_randr_get_screen_resources(connection, screen->root);
    xcb_randr_get_screen_resources_reply_t *resources_reply = xcb_randr_get_screen_resources_reply(connection, resources_cookie, NULL);
    if(resources_reply)
    {
        int len = xcb_randr_get_screen_resources_outputs_length(resources_reply);
        xcb_randr_output_t * randr_ptr = xcb_randr_get_screen_resources_outputs(resources_reply);

        xcb_randr_get_output_info_cookie_t output_cookie = xcb_randr_get_output_info(connection, randr_ptr[resources_reply->num_crtcs-1], XCB_CURRENT_TIME);
        xcb_randr_get_output_info_reply_t *output_info_reply = xcb_randr_get_output_info_reply(connection, output_cookie, NULL);

        if (output_info_reply) {
            xcb_randr_get_crtc_info_cookie_t crtc_cookie = xcb_randr_get_crtc_info(connection, output_info_reply->crtc, XCB_CURRENT_TIME);
            xcb_randr_get_crtc_info_reply_t *crtc_reply = xcb_randr_get_crtc_info_reply(connection, crtc_cookie, NULL);
            if (crtc_reply) {
                lpmi->rcWork.left = crtc_reply->x;
                lpmi->rcWork.top= crtc_reply->y;
                lpmi->rcWork.right =crtc_reply->x+ crtc_reply->width;
                lpmi->rcWork.bottom =crtc_reply->y+ crtc_reply->height;
                free(crtc_reply);
            }
            free(output_info_reply);
        }
        free(resources_reply);
    }
    */
    return TRUE;
}
