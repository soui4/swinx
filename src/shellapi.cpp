#include <shellapi.h>
#include "tostring.hpp"

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