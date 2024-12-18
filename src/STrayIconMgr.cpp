#include "STrayIconMgr.h"
#include <assert.h>
#include "SConnection.h"

enum {
	SystemTrayRequestDock = 0,
	SystemTrayBeginMessage = 1,
	SystemTrayCancelMessage = 2
};

STrayIconMgr::~STrayIconMgr() {
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++) {
		free(*it);
	}
	m_lstTrays.clear();
}

BOOL STrayIconMgr::AddIcon(PNOTIFYICONDATAA lpData) {
	if (m_pConn->atoms._NET_SYSTEM_TRAY_OPCODE == 0 || m_pConn->atoms._NET_SYSTEM_TRAY_S0==0)
		return FALSE;
	if (!IsWindow(lpData->hWnd))
		return FALSE;
	xcb_window_t trayWindow = locateTrayWindow(m_pConn->connection, m_pConn->atoms._NET_SYSTEM_TRAY_S0);
	if (!trayWindow)
		return FALSE;
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	if (findIcon(lpData) != m_lstTrays.end())
		return FALSE;
	PNOTIFYICONDATAA icon = (PNOTIFYICONDATAA)malloc(sizeof(NOTIFYICONDATAA));
	memcpy(icon, lpData, sizeof(NOTIFYICONDATAA));

	xcb_client_message_event_t trayRequest;
	trayRequest.response_type = XCB_CLIENT_MESSAGE;
	trayRequest.format = 32;
	trayRequest.sequence = 0;
	trayRequest.window = trayWindow;
	trayRequest.type = m_pConn->atoms._NET_SYSTEM_TRAY_OPCODE;
	trayRequest.data.data32[0] = XCB_CURRENT_TIME;
	trayRequest.data.data32[1] = SystemTrayRequestDock;
	trayRequest.data.data32[2] = lpData->hWnd;
	xcb_send_event(m_pConn->connection, 0, trayWindow, XCB_EVENT_MASK_NO_EVENT, (const char*)&trayRequest);
	xcb_flush(m_pConn->connection);

	m_lstTrays.push_back(icon);

	return TRUE;
}

BOOL STrayIconMgr::ModifyIcon(PNOTIFYICONDATAA lpData) {
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	auto it = findIcon(lpData);
	if (it == m_lstTrays.end())
		return FALSE;
	if (lpData->uFlags & NIF_MESSAGE)
		(*it)->uCallbackMessage = lpData->uCallbackMessage;
	if (lpData->uFlags & NIF_ICON)
	{
		assert(lpData->hIcon);
		(*it)->hIcon = lpData->hIcon;
	}
	if (lpData->uFlags & NIF_TIP) {
		strcpy_s((*it)->szTip, 128, lpData->szTip);
	}
	if (lpData->uFlags & NIF_INFO) {
		strcpy_s((*it)->szInfo, 256, lpData->szInfo);
	}
	if (lpData->uFlags & NIF_GUID) {
		memcpy(&(*it)->guidItem, &lpData->guidItem, sizeof(GUID));
	}
	return TRUE;
}

BOOL STrayIconMgr::DelIcon(PNOTIFYICONDATAA lpData) {
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	auto it = findIcon(lpData);
	if (it == m_lstTrays.end())
		return FALSE;
	free(*it);
	m_lstTrays.erase(it);
	return TRUE;
}

STrayIconMgr::TRAYLIST::iterator STrayIconMgr::findIcon(PNOTIFYICONDATAA src) {
	for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
	{
		if ((*it)->hWnd == src->hWnd && (*it)->uID == src->uID)
			return it;
	}
	return m_lstTrays.end();
}

xcb_window_t STrayIconMgr::locateTrayWindow(xcb_connection_t* connection, xcb_atom_t selection)
{
	xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(connection, selection);
	xcb_get_selection_owner_reply_t* reply = xcb_get_selection_owner_reply(connection, cookie, 0);
	if (!reply)
		return 0;
	const xcb_window_t result = reply->owner;
	free(reply);
	return result;
}
