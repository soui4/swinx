#ifndef _SHELL_API_H_
#define _SHELL_API_H_

#include <windows.h>

UINT WINAPI DragQueryFileA(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPSTR lpszFile, _In_ UINT cch);
UINT WINAPI DragQueryFileW(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPWSTR lpszFile, _In_ UINT cch);
#ifdef UNICODE
#define DragQueryFile  DragQueryFileW
#else
#define DragQueryFile  DragQueryFileA
#endif // !UNICODE
BOOL WINAPI DragQueryPoint(_In_ HDROP hDrop, _Out_ POINT * ppt);
void WINAPI DragFinish(_In_ HDROP hDrop);
void WINAPI DragAcceptFiles(_In_ HWND hWnd, _In_ BOOL fAccept);

#endif//_SHELL_API_H_