#include "STrayIconMgr.h"
#include <assert.h>

STrayIconMgr::~STrayIconMgr() {
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++) {
		free(*it);
	}
	m_lstTrays.clear();
}

BOOL STrayIconMgr::AddIcon(PNOTIFYICONDATAA lpData) {
	std::unique_lock<std::recursive_mutex> lock(m_mutex);
	if (findIcon(lpData) != m_lstTrays.end())
		return FALSE;
	PNOTIFYICONDATAA icon = (PNOTIFYICONDATAA)malloc(sizeof(NOTIFYICONDATAA));
	memcpy(icon, lpData, sizeof(NOTIFYICONDATAA));
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