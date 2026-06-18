#ifndef _OHOS_WINDOW_H_
#define _OHOS_WINDOW_H_

#include <ctypes.h>
#include <cairo.h>

class SConnBase;

HWND createOhosWindow(HWND hParent, DWORD dwStyle, DWORD dwExStyle, BOOL bAutoDblClick, LPCSTR pszTitle, int x, int y, int cx, int cy, SConnBase *listener);
void closeOhosWindow(HWND hWnd);
BOOL showOhosWindow(HWND hWnd, int nCmdShow);
BOOL setOhosWindowPos(HWND hWnd, int x, int y);
void beginOhosMainWindowMove(HWND hWnd);
void endOhosMainWindowMove(HWND hWnd);
void beginOhosMainWindowResize(HWND hWnd);
void endOhosMainWindowResize(HWND hWnd);
BOOL setOhosWindowSize(HWND hWnd, int cx, int cy);
HWND getOhosWindow(HWND hWnd, UINT uCmd);
BOOL setOhosActiveWindow(HWND hWnd);
HWND getOhosActiveWindow();
BOOL setOhosFocusWindow(HWND hWnd);
HWND getOhosFocusWindow();
void invalidateOhosWindow(HWND hWnd, LPCRECT rc);
BOOL requestOhosWindowsRepaint();
BOOL isOhosWindowVisible(HWND hWnd);
BOOL getOhosWindowRect(HWND hWnd, RECT *rc);
HWND ohosHwndFromPoint(HWND hWnd, POINT pt);
BOOL mapOhosPointToWindow(HWND hWndFrom, HWND hWndTo, LPPOINT ppt);
BOOL setOhosWindowZorder(HWND hWnd, HWND hWndInsertAfter);
BOOL setOhosWindowCapture(HWND hWnd);
BOOL releaseOhosWindowCapture(HWND hWnd);
BOOL setOhosWindowAlpha(HWND hWnd, BYTE byAlpha);
BOOL setOhosMsgTransparent(HWND hWnd, BOOL bTransparent);
void updateOhosWindow(HWND hWnd, const RECT &rc);
void commitOhosWindow(HWND hWnd, cairo_surface_t *surface, const RECT &rc);
void commitOhosWindowFromHdc(HWND hWnd, HDC hdc, const RECT &rc);
BOOL sendOhosSysCommand(HWND hWnd, int nCmd);
BOOL setOhosWindowCursor(HWND hWnd, HCURSOR cursor);
BOOL getOhosCursorPos(LPPOINT ppt);
BOOL setOhosCursorPos(POINT pt);
int getOhosDpi(BOOL bx);
BOOL setOhosParent(HWND hWnd, HWND hParent);
BOOL setOhosWindowRgn(HWND hWnd, const RECT *prc, int nCount);
BOOL enableOhosWindow(HWND hWnd, BOOL bEnable);
BOOL isOhosWindowMinimized(HWND hWnd);
BOOL isOhosWindowMaximized(HWND hWnd);

#endif // _OHOS_WINDOW_H_
