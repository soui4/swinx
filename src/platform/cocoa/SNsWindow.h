#ifndef _SNSWINDOW_H_
#define _SNSWINDOW_H_

#include <ctypes.h>
#include <cairo.h>
#include <string>
#include "SConnBase.h"

HWND createNsWindow(HWND hParent,DWORD dwStyle,DWORD dwExStyle, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener);
void closeNsWindow(HWND hWnd);
BOOL IsNsWindow(HWND hWnd);
cairo_t * getNsWindowCanvas(HWND hWnd);
BOOL showNsWindow(HWND hWnd,int nCmdShow);
BOOL setNsWindowPos(HWND hWnd, int x, int y);
BOOL setNsWindowSize(HWND hWnd, int cx, int cy);
HWND getNsWindow(HWND hWnd, int code);
BOOL setNsActiveWindow(HWND hWnd);
HWND getNsActiveWindow();

BOOL setNsFocusWindow(HWND hWnd);
HWND getNsFocusWindow();
void invalidateNsWindow(HWND hWnd, LPCRECT rc);
BOOL isNsWindowVisible(HWND hWnd);
BOOL getNsWindowRect(HWND hWnd, RECT *rc);
HWND hwndFromPoint(HWND hWnd,POINT pt);
BOOL setNsWindowZorder(HWND hWnd, HWND hWndInsertAfter);
BOOL setNsWindowCapture(HWND hWnd);
BOOL releaseNsWindowCapture(HWND hWnd);
BOOL setNsWindowAlpha(HWND hWnd,BYTE byAlpha);
BYTE getNsWindowAlpha(HWND hWnd);

HWND getNsForegroundWindow();
BOOL setNsForegroundWindow(HWND hWnd);
BOOL setNsMsgTransparent(HWND hWnd,BOOL bTransparent);
void updateNsWindow(HWND hWnd, const RECT &rc);
BOOL sendNsSysCommand(HWND hWnd, int nCmd) ;
BOOL setNsWindowIcon(HWND hWnd, HICON hIcon, BOOL bBigIcon);
BOOL setNsParent(HWND hWnd, HWND hParent);
BOOL flashNsWindow(HWND hwnd,
        DWORD dwFlags,
        UINT uCount,
        DWORD dwTimeout);
BOOL isNsDropTarget(HWND hWnd);
BOOL setNsDropTarget(HWND hWnd, BOOL bEnable);

BOOL setNsWindowCursor(HWND hWnd, HCURSOR cursor);

struct IDataObject;
struct IDropSource;
HRESULT doNsDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,     
                          DWORD *pdwEffect);
BOOL getNsCursorPos(LPPOINT ppt);

int getNsDpi(bool bx);
#endif//_SNSWINDOW_H_