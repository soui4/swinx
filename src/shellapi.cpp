#include <shellapi.h>
#include "tostring.hpp"
#include <mutex>
#include <list>
#include <map>
#include <assert.h>
#include "SConnection.h"

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
static STrayIconMgr* getTrayIconMgr() {
	return SConnMgr::instance()->getConnection()->GetTrayIconMgr();
}

BOOL WINAPI Shell_NotifyIconA(
  DWORD            dwMessage,
  PNOTIFYICONDATAA lpData
){
	return getTrayIconMgr()->NotifyIcon(dwMessage,lpData);
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
