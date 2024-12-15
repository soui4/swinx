#include <shellapi.h>
#include "tostring.hpp"
#include <mutex>
#include <list>
#include <map>
#include <assert.h>

UINT WINAPI DragQueryFileA(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPSTR lpszFile, _In_ UINT cch) {
	return 0;
}

UINT WINAPI DragQueryFileW(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPWSTR lpszFile, _In_ UINT cch)
{
	UINT len = DragQueryFileA(hDrop, iFile, NULL, 0);
	if (len == 0)
		return 0;
	std::string str;
	str.resize(len);
	DragQueryFileA(hDrop, iFile, (char*)str.c_str(), len);
	int required = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), len, NULL, 0);
	if (!lpszFile)
		return required;
	if (cch < required)
		return 0;
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), len, lpszFile, cch);
	return required;
}

BOOL WINAPI DragQueryPoint(_In_ HDROP hDrop, _Out_ POINT* ppt) {
	return FALSE;
}

void WINAPI DragFinish(_In_ HDROP hDrop) {

}

void WINAPI DragAcceptFiles(_In_ HWND hWnd, _In_ BOOL fAccept) {

}

//----------------------------------------------------------------------
class SysTrayIconMgr{
	typedef std::list<PNOTIFYICONDATAA> TRAYLIST;
public:
	SysTrayIconMgr(){}
	~SysTrayIconMgr(){
		std::unique_lock<std::recursive_mutex> lock(m_mutex);
		for(auto it = m_lstTrays.begin();it!= m_lstTrays.end();it++){
			free(*it);
		}
		m_lstTrays.clear();
	}

public:
	BOOL NotifyIcon(DWORD  dwMessage, PNOTIFYICONDATAA lpData){
		switch(dwMessage){
			case NIM_ADD:return AddIcon(lpData);
			case NIM_DELETE:return DelIcon(lpData);
			case NIM_MODIFY:return ModifyIcon(lpData);
		}
		return FALSE;
    }

private:
	BOOL AddIcon(PNOTIFYICONDATAA lpData){
		std::unique_lock<std::recursive_mutex> lock(m_mutex);
		if(findIcon(lpData)!=m_lstTrays.end())
			return FALSE;
		PNOTIFYICONDATAA icon = (PNOTIFYICONDATAA)malloc(sizeof(NOTIFYICONDATAA));
		memcpy(icon,lpData,sizeof(NOTIFYICONDATAA));
		return TRUE;
	}

	BOOL ModifyIcon(PNOTIFYICONDATAA lpData){
		std::unique_lock<std::recursive_mutex> lock(m_mutex);
		auto it = findIcon(lpData);
		if(it==m_lstTrays.end())
			return FALSE;
		if(lpData->uFlags & NIF_MESSAGE)
			(*it)->uCallbackMessage=lpData->uCallbackMessage;
		if(lpData->uFlags & NIF_ICON)
		{
			assert(lpData->hIcon);
			(*it)->hIcon = lpData->hIcon;
		}
		if(lpData->uFlags & NIF_TIP){
			strcpy_s((*it)->szTip,128,lpData->szTip);
		}
		if(lpData->uFlags & NIF_INFO){
			strcpy_s((*it)->szInfo,256,lpData->szInfo);
		}
		if(lpData->uFlags & NIF_GUID){
			memcpy(&(*it)->guidItem,&lpData->guidItem,sizeof(GUID));
		}
		return TRUE;
	}

	BOOL DelIcon(PNOTIFYICONDATAA lpData){
		std::unique_lock<std::recursive_mutex> lock(m_mutex);
		auto it = findIcon(lpData);
		if(it==m_lstTrays.end())
			return FALSE;
		free(*it);
		m_lstTrays.erase(it);
		return TRUE;
	}

	TRAYLIST::iterator findIcon(PNOTIFYICONDATAA src){
		for(auto it=m_lstTrays.begin();it!=m_lstTrays.end();it++)
		{
			if((*it)->hWnd == src->hWnd && (*it)->uID == src->uID)
				return it;
		}
		return m_lstTrays.end();
	}
private:
	std::recursive_mutex m_mutex;
	
	TRAYLIST m_lstTrays;
}s_sysTrayIconMgr;

BOOL WINAPI Shell_NotifyIconA(
  DWORD            dwMessage,
  PNOTIFYICONDATAA lpData
){
	return s_sysTrayIconMgr.NotifyIcon(dwMessage,lpData);
}

BOOL WINAPI Shell_NotifyIconW(
  DWORD            dwMessage,
  PNOTIFYICONDATAW lpData
){
	NOTIFYICONDATAA dataA;
	if(0==WideCharToMultiByte(CP_UTF8,0,lpData->szInfo,-1,dataA.szInfo,256,nullptr,nullptr))
		return FALSE;
	if(0==WideCharToMultiByte(CP_UTF8,0,lpData->szTip,-1,dataA.szTip,128,nullptr,nullptr))
		return FALSE;
	if(0==WideCharToMultiByte(CP_UTF8,0,lpData->szInfoTitle,-1,dataA.szInfoTitle,64,nullptr,nullptr))
		return FALSE;
	return Shell_NotifyIconA(dwMessage,&dataA);
}
