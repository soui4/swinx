#include "STrayIconMgr.h"
#include <assert.h>
#include "SConnection.h"
#include "log.h"
#define kLogTag "traywnd"

LRESULT TrayWnd::NotifyOwner(UINT uMsg, WPARAM wp, LPARAM lp)
{
    if (m_iconData->hWnd && (m_iconData->uFlags & NIF_MESSAGE))
    {
        PostMessage(m_iconData->hWnd, m_iconData->uCallbackMessage, m_iconData->uID, uMsg);
    }
    return 0;
}

void TrayWnd::OnPaint(HDC hdc)
{
    PAINTSTRUCT ps;
    hdc = BeginPaint(m_hWnd, &ps);
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    ClearRect(hdc, &rc, 0);
    if (m_iconData->hIcon)
        DrawIconEx(hdc, 0, 0, m_iconData->hIcon, rc.right, rc.bottom, 0, 0, 0);
    EndPaint(m_hWnd, &ps);
}

//-------------------------------------------------
enum
{
    SystemTrayRequestDock = 0,
    SystemTrayBeginMessage = 1,
    SystemTrayCancelMessage = 2
};

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
    BOOL argbTray = this->visualHasAlphaChannel();
    icon->hTray->CreateWindowA(argbTray ? WS_EX_COMPOSITED : 0, CLS_WINDOW, "trayicon", WS_CHILD, 0, 0, 24, 24, m_pConn->screen->root, 0, 0);
    xcb_change_property(m_pConn->connection, XCB_PROP_MODE_REPLACE, icon->hTray->m_hWnd, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(icon->szTip), icon->szTip);

    xcb_client_message_event_t trayRequest;
    trayRequest.response_type = XCB_CLIENT_MESSAGE;
    trayRequest.format = 32;
    trayRequest.sequence = 0;
    trayRequest.window = m_trayMgr;
    trayRequest.type = m_pConn->atoms._NET_SYSTEM_TRAY_OPCODE;
    trayRequest.data.data32[0] = XCB_CURRENT_TIME;
    trayRequest.data.data32[1] = SystemTrayRequestDock;
    trayRequest.data.data32[2] = icon->hTray->m_hWnd;
    xcb_send_event(m_pConn->connection, 0, m_trayMgr, XCB_EVENT_MASK_NO_EVENT, (const char *)&trayRequest);
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
            icon->hTray->Invalidate();
        }
    }
    if (lpData->uFlags & NIF_TIP)
    {
        strcpy_s(icon->szTip, 128, lpData->szTip);
        if (icon->hTray)
        {
            xcb_change_property(m_pConn->connection, XCB_PROP_MODE_REPLACE, icon->hTray->m_hWnd, XCB_ATOM_WM_NAME, m_pConn->atoms.UTF8_STRING, 8, strlen(icon->szTip), icon->szTip);
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
