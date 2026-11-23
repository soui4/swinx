#include "STrayIconMgr.h"
#include "xcb_event32.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb_icccm.h>
#include "SConnection.h"
#include "log.h"
#define kLogTag "traywnd"

/* 与 Wayland 托盘同名开关：SWINX_TRAY_TRACE=1 时打印点击坐标换算结果和
 * 图标/槽位尺寸，便于在目标机上确认「菜单跑到左上角」「图标被拉伸」这类
 * 只能靠现场观察的问题。默认关闭，不产生任何输出。 */
static bool trayTrace()
{
    static int s_trace = -1;
    if (s_trace < 0)
    {
        const char *v = getenv("SWINX_TRAY_TRACE");
        s_trace = (v && *v && *v != '0') ? 1 : 0;
    }
    return s_trace != 0;
}

// 只有按键消息需要给 GetCursorPos 补全局坐标：应用收到 WM_*BUTTONDOWN/UP 之后
// 会立刻查光标来定位自绘菜单（SMenuEx::TrackPopupMenu）。鼠标移动不补，避免
// 指针早已移出图标后 GetCursorPos 还在 500ms 内返回旧位置。
static bool IsButtonNotify(UINT uMsg)
{
    switch (uMsg)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

LRESULT TrayWnd::NotifyOwner(UINT uMsg, WPARAM wp __attribute__((unused)), LPARAM lp)
{
    if (m_iconData->hWnd && (m_iconData->uFlags & NIF_MESSAGE))
    {
        if (IsButtonNotify(uMsg))
        {
            // 回调里（应用弹右键菜单）要用 GetCursorPos 定位，必须给出**根坐标**。
            // 托盘图标窗口在 dock 之后会被面板（tray manager）reparent 到它自己的
            // embedder 窗口里，此后 ConfigureNotify 报的 x/y 是相对那个 embedder 的，
            // swinx 缓存的窗口矩形（ClientToScreen 的依据）也就变成了面板内坐标。
            // 实测按 ClientToScreen 换算会得到贴屏幕左上角的点，菜单跟着跑到左上角。
            // 这里直接问服务器：把点击的窗口内坐标 translate 到 root。
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SConnection *conn = SConnMgr::instance()->getConnection();
            BOOL bOk = FALSE;
            if (conn && conn->connection && conn->screen)
            {
                xcb_translate_coordinates_cookie_t cookie = xcb_translate_coordinates(
                    conn->connection, xcbFromHwnd(m_hWnd), conn->screen->root, pt.x, pt.y);
                xcb_translate_coordinates_reply_t *reply =
                    xcb_translate_coordinates_reply(conn->connection, cookie, NULL);
                if (reply)
                {
                    pt.x = reply->dst_x;
                    pt.y = reply->dst_y;
                    free(reply);
                    bOk = TRUE;
                }
            }
            if (!bOk)
                bOk = ClientToScreen(m_hWnd, &pt);
            if (bOk && conn)
                conn->SetCursorPosHint(pt);
            if (trayTrace())
                fprintf(stderr, "[tray-x11] notify msg=%u client=(%d,%d) root=(%d,%d) ok=%d\n",
                        (unsigned)uMsg, (int)GET_X_LPARAM(lp), (int)GET_Y_LPARAM(lp),
                        (int)pt.x, (int)pt.y, (int)bOk);
        }
        PostMessage(m_iconData->hWnd, m_iconData->uCallbackMessage, m_iconData->uID, uMsg);
    }
    return 0;
}

bool TrayWnd::IconPixelSize(HICON hIcon, int &cx, int &cy)
{
    if (!hIcon)
        return false;
    if (hIcon == m_hIconSized)
    {
        cx = m_cxIconSized;
        cy = m_cyIconSized;
        return cx > 0 && cy > 0;
    }
    int w = 0, h = 0;
    ICONINFO info = {};
    if (GetIconInfo(hIcon, &info))
    {
        /* 只认颜色位图：单色图标（hbmColor == NULL）的 hbmMask 是 AND+XOR 两张
         * 叠在一起的（高 = 2 * 图标高），拿它当尺寸会算出错误的宽高比；而且
         * DrawIconEx 这条路径本来也只支持 32bpp 颜色位图。 */
        BITMAP bm = {};
        if (info.hbmColor && GetObject(info.hbmColor, sizeof(bm), &bm) &&
            bm.bmWidth > 0 && bm.bmHeight != 0)
        {
            w = (int)bm.bmWidth;
            h = bm.bmHeight > 0 ? (int)bm.bmHeight : -(int)bm.bmHeight;
        }
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
    }
    m_hIconSized = hIcon;
    m_cxIconSized = w;
    m_cyIconSized = h;
    cx = w;
    cy = h;
    return w > 0 && h > 0;
}

void TrayWnd::OnPaint(HDC hdc)
{
    PAINTSTRUCT ps;
    hdc = BeginPaint(m_hWnd, &ps);
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    // Clear with transparent color
    ClearRect(hdc, &rc, RGBA(0, 0, 0, 0));

    if (m_iconData && m_iconData->hIcon && width > 0 && height > 0)
    {
        // 图标窗口的尺寸由面板决定，槽位不保证是正方形（高 24 宽 32、老面板的
        // 1×24 都出现过）。把图标直接拉满客户区就会横向/纵向变形，所以按最小边
        // 取正方形绘制区、保持原始宽高比缩放后居中：槽位是正方形时与"拉满"等价，
        // 非正方形时只留边距，不产生形变。
        int box = width < height ? width : height;
        int cxIcon = box, cyIcon = box;
        int iw = 0, ih = 0;
        if (IconPixelSize(m_iconData->hIcon, iw, ih))
        {
            if (iw >= ih)
            {
                cxIcon = box;
                cyIcon = (box * ih + iw / 2) / iw;
            }
            else
            {
                cyIcon = box;
                cxIcon = (box * iw + ih / 2) / ih;
            }
        }
        if (cxIcon < 1)
            cxIcon = 1;
        if (cyIcon < 1)
            cyIcon = 1;
        if (trayTrace())
        {
            // 只在尺寸变化时打一行，避免动画图标每帧刷屏
            static int s_dbgWinW = -1, s_dbgWinH = -1, s_dbgIcoW = -1, s_dbgIcoH = -1;
            if (width != s_dbgWinW || height != s_dbgWinH || iw != s_dbgIcoW || ih != s_dbgIcoH)
            {
                s_dbgWinW = width;
                s_dbgWinH = height;
                s_dbgIcoW = iw;
                s_dbgIcoH = ih;
                fprintf(stderr, "[tray-x11] paint slot=%dx%d icon=%dx%d draw=%dx%d at=(%d,%d)\n",
                        width, height, iw, ih, cxIcon, cyIcon,
                        (width - cxIcon) / 2, (height - cyIcon) / 2);
            }
        }
        DrawIconEx(hdc, (width - cxIcon) / 2, (height - cyIcon) / 2, m_iconData->hIcon, cxIcon, cyIcon, 0, 0, DI_NORMAL);
    }        
    EndPaint(m_hWnd, &ps);
}

LRESULT TrayWnd::OnSize(UINT uMsg __attribute__((unused)), WPARAM wParam __attribute__((unused)), LPARAM lParam __attribute__((unused)))
{
    Invalidate();
    return 0;
}

//-------------------------------------------------
enum
{
    SystemTrayRequestDock = 0,
    SystemTrayBeginMessage = 1,
    SystemTrayCancelMessage = 2
};

// 托盘提示文本：WM_NAME 是 ICCCM 的老属性（严格说只该放 latin1），现代面板
// 普遍改读 _NET_WM_NAME。两个都按 UTF-8 写，中文提示才不会变成乱码。
static void SetTrayTipText(SConnection *conn, xcb_window_t wnd, const char *szTip)
{
    if (!szTip || !*szTip)
        return;
    const int len = (int)strlen(szTip);
    xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, wnd, XCB_ATOM_WM_NAME, conn->atoms.UTF8_STRING, 8, len, szTip);
    xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, wnd, conn->atoms._NET_WM_NAME, conn->atoms.UTF8_STRING, 8, len, szTip);
}

STrayIconMgr::STrayIconMgr(SConnection *pConn)
    : m_pConn(pConn)
    , m_trayMgr(XCB_NONE)
{
    m_trayMgr = locateTrayWindow(m_pConn->connection, m_pConn->atoms._NET_SYSTEM_TRAY_S0);
}

STrayIconMgr::~STrayIconMgr()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        free(*it);
    }
    m_lstTrays.clear();
}

BOOL STrayIconMgr::AddIcon(PNOTIFYICONDATAA lpData)
{
    if (!m_trayMgr)
        return FALSE;
    if (m_pConn->atoms._NET_SYSTEM_TRAY_OPCODE == 0 || m_pConn->atoms._NET_SYSTEM_TRAY_S0 == 0)
        return FALSE;
    if (!IsWindow(lpData->hWnd))
        return FALSE;
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (findIcon(lpData) != m_lstTrays.end())
        return FALSE;
    TrayIconData *icon = (TrayIconData *)malloc(sizeof(TrayIconData));
    memcpy(icon, lpData, sizeof(NOTIFYICONDATAA));
    // create a tray window to contain image.
    icon->hTray = new TrayWnd(icon);

    DWORD dwStyle = WS_POPUP;
    DWORD dwExStyle = 0;
    // If the tray manager advertises a 32-bit ARGB visual via
    // _NET_SYSTEM_TRAY_VISUAL, the icon window MUST be created using a 32-bit
    // ARGB visual; otherwise transparent pixels of the icon (and the result of
    // ClearRect with alpha=0) get rendered as opaque black, which on Ubuntu
    // shows up as black halos around the icon. WS_EX_COMPOSITED tells the
    // window factory to use the rgba_visual cached on SConnection.
    if (visualHasAlphaChannel())
        dwExStyle |= WS_EX_COMPOSITED;

    // The size we pass here is just the initial request; the tray manager will
    // reparent the window into its container and resize it according to its layout.
    const int TRAY_ICON_SIZE = 24;
    icon->hTray->CreateWindowA(dwExStyle, CLS_WINDOWA, "trayicon", dwStyle, 0, 0, TRAY_ICON_SIZE, TRAY_ICON_SIZE, hwndFromXcb(m_pConn->screen->root), 0, 0);


    // Set WM_NAME/_NET_WM_NAME for tooltip
    SetTrayTipText(m_pConn, xcbFromHwnd(icon->hTray->m_hWnd), icon->szTip);

    xcb_size_hints_t size_hints = {};
    size_hints.flags = 0x30;  // PMinSize | PMaxSize
    size_hints.min_width = TRAY_ICON_SIZE;
    size_hints.min_height = TRAY_ICON_SIZE;
    size_hints.max_width = TRAY_ICON_SIZE;
    size_hints.max_height = TRAY_ICON_SIZE;

    xcb_change_property(m_pConn->connection,
                        XCB_PROP_MODE_REPLACE,
                        xcbFromHwnd(icon->hTray->m_hWnd),
                        m_pConn->atoms.WM_NORMAL_HINTS,
                        XCB_ATOM_WM_SIZE_HINTS,
                        32,
                        18,  // 18 个 32 位字段
                        &size_hints);

    // Send the dock request to the tray manager. After this point the manager
    // owns the window's geometry and visibility.
    xcb_client_message_event_t trayRequest = {};
    trayRequest.response_type = XCB_CLIENT_MESSAGE;
    trayRequest.format = 32;
    trayRequest.sequence = 0;
    trayRequest.window = m_trayMgr;
    trayRequest.type = m_pConn->atoms._NET_SYSTEM_TRAY_OPCODE;
    trayRequest.data.data32[0] = XCB_CURRENT_TIME;
    trayRequest.data.data32[1] = SystemTrayRequestDock;
    trayRequest.data.data32[2] = (uint32_t)xcbFromHwnd(icon->hTray->m_hWnd);
    xcb_send_event32(m_pConn->connection, 0, m_trayMgr, XCB_EVENT_MASK_NO_EVENT, trayRequest);
    xcb_flush(m_pConn->connection);

    m_lstTrays.push_back(icon);

    return TRUE;
}

BOOL STrayIconMgr::ModifyIcon(PNOTIFYICONDATAA lpData)
{
    if (!m_trayMgr)
        return FALSE;
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;

    TrayIconData *icon = *it;
    if (lpData->uFlags & NIF_MESSAGE)
        icon->uCallbackMessage = lpData->uCallbackMessage;
    if (lpData->uFlags & NIF_ICON)
    {
        assert(lpData->hIcon);
        icon->hIcon = lpData->hIcon;
        if (icon->hTray)
        {
            // 新图标的像素尺寸可能和旧的不同（动画帧尺寸一致也必须重算，
            // HICON 地址会被复用，缓存的尺寸不能跨图标沿用）
            icon->hTray->ResetIconSizeCache();
            icon->hTray->Invalidate();
        }
    }
    if (lpData->uFlags & NIF_TIP)
    {
        strcpy_s(icon->szTip, 128, lpData->szTip);
        if (icon->hTray)
        {
            SetTrayTipText(m_pConn, xcbFromHwnd(icon->hTray->m_hWnd), icon->szTip);
        }
    }
    if (lpData->uFlags & NIF_INFO)
    {
        strcpy_s(icon->szInfo, 256, lpData->szInfo);
    }
    if (lpData->uFlags & NIF_GUID)
    {
        memcpy(&icon->guidItem, &lpData->guidItem, sizeof(GUID));
    }

    return TRUE;
}

BOOL STrayIconMgr::DelIcon(PNOTIFYICONDATAA lpData)
{
    if (!m_trayMgr)
        return FALSE;
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;
    (*it)->hTray->DestroyWindow();
    delete (*it)->hTray;

    free(*it);
    m_lstTrays.erase(it);
    return TRUE;
}

STrayIconMgr::TRAYLIST::iterator STrayIconMgr::findIcon(PNOTIFYICONDATAA src)
{
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        if ((*it)->hWnd == src->hWnd && (*it)->uID == src->uID)
            return it;
    }
    return m_lstTrays.end();
}

xcb_window_t STrayIconMgr::locateTrayWindow(xcb_connection_t *connection, xcb_atom_t selection)
{
    xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(connection, selection);
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(connection, cookie, 0);
    if (!reply)
        return XCB_NONE;
    const xcb_window_t result = reply->owner;
    free(reply);
    return result;
}

bool STrayIconMgr::visualHasAlphaChannel()
{
    if (m_trayMgr == XCB_WINDOW_NONE)
        return false;

    xcb_atom_t tray_atom = m_pConn->atoms._NET_SYSTEM_TRAY_VISUAL;

    // Get the xcb property for the _NET_SYSTEM_TRAY_VISUAL atom
    xcb_get_property_cookie_t systray_atom_cookie;
    xcb_get_property_reply_t *systray_atom_reply;

    systray_atom_cookie = xcb_get_property_unchecked(m_pConn->connection, false, m_trayMgr, tray_atom, XCB_ATOM_VISUALID, 0, 1);
    systray_atom_reply = xcb_get_property_reply(m_pConn->connection, systray_atom_cookie, 0);

    if (!systray_atom_reply)
        return false;

    xcb_visualid_t systrayVisualId = XCB_NONE;
    if (systray_atom_reply->value_len > 0 && xcb_get_property_value_length(systray_atom_reply) > 0)
    {
        xcb_visualid_t *vids = (uint32_t *)xcb_get_property_value(systray_atom_reply);
        systrayVisualId = vids[0];
    }

    free(systray_atom_reply);

    if (systrayVisualId != XCB_NONE)
    {
        uint8_t depth = 32; // m_connection->primaryScreen()->depthOfVisual(systrayVisualId);
        return depth == 32;
    }

    return false;
}
