#ifndef _SUIWINDOW_H_
#define _SUIWINDOW_H_

#include <ctypes.h>
#include <string>
#include "SConnBase.h"

// SUIWindow 提供 iOS 平台下 UIView/UIWindow 的封装，
// 对应 macOS cocoa 平台的 SNsWindow，使用 UIKit 替代 AppKit。
// HWND 直接映射到 SUIView（UIView 子类）的桥接指针。

HWND createUiWindow(HWND hParent,DWORD dwStyle,DWORD dwExStyle, BOOL bAutoDbkClick, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener);
void closeUiWindow(HWND hWnd);
BOOL IsUiWindow(HWND hWnd);
BOOL showUiWindow(HWND hWnd,int nCmdShow);
BOOL setUiWindowPos(HWND hWnd, int x, int y);
BOOL setUiWindowSize(HWND hWnd, int cx, int cy);
HWND getUiWindow(HWND hWnd, int code);
BOOL setUiActiveWindow(HWND hWnd);
HWND getUiActiveWindow();

BOOL setUiFocusWindow(HWND hWnd);
void invalidateUiWindow(HWND hWnd, LPCRECT rc);
BOOL isUiWindowVisible(HWND hWnd);
BOOL getUiWindowRect(HWND hWnd, RECT *rc);
HWND hwndFromPoint(HWND hWnd,POINT pt);
BOOL setUiWindowZorder(HWND hWnd, HWND hWndInsertAfter);
BOOL setUiWindowCapture(HWND hWnd);
BOOL releaseUiWindowCapture(HWND hWnd);
BOOL setUiWindowAlpha(HWND hWnd,BYTE byAlpha);
BYTE getUiWindowAlpha(HWND hWnd);

HWND getUiForegroundWindow();
BOOL setUiForegroundWindow(HWND hWnd);
BOOL setUiWindowToTop(HWND hWnd);
BOOL setUiMsgTransparent(HWND hWnd,BOOL bTransparent);
void updateUiWindow(HWND hWnd, const RECT &rc);
BOOL sendUiSysCommand(HWND hWnd, int nCmd) ;
BOOL setUiWindowIcon(HWND hWnd, HICON hIcon, BOOL bBigIcon);
BOOL setUiParent(HWND hWnd, HWND hParent);
BOOL flashUiWindow(HWND hwnd,
        DWORD dwFlags,
        UINT uCount,
        DWORD dwTimeout);
BOOL isUiDropTarget(HWND hWnd);
BOOL setUiDropTarget(HWND hWnd, BOOL bEnable);

BOOL setUiWindowCursor(HWND hWnd, HCURSOR cursor);

struct IDataObject;
struct IDropSource;
HRESULT doUiDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,
                          DWORD *pdwEffect);

int getUiDpi(bool bx);

HWND findUiKeyWindow();

BOOL setUiWindowRgn(HWND hWnd, const RECT *prc, int nCount);
int  getUiWindowId(HWND hWnd);
BOOL enableUiWindow(HWND hWnd, BOOL bEnable);
void enableUiWindowIme(HWND hWnd, BOOL bEnable);
BOOL isUiWindowEnableIme(HWND hWnd);

/** 显式打开/关闭软键盘（对齐 Win32 ShowSoftKeyboard / Android showSoftKeyboard）。
 *  @param hWnd  目标窗口 HWND
 *  @param bShow YES 打开（需要窗口为第一响应者或可 becomeFirstResponder）；NO 关闭
 *  @return 操作是否成功
 */
BOOL showUiSoftKeyboard(HWND hWnd, BOOL bShow);

/** 查询当前软键盘是否可见（iOS 原生没有直接 API，通过全局标记 + 通知维护）。 */
BOOL isUiSoftKeyboardVisible(void);

/** 获取当前软键盘高度（物理像素），未打开返回 0。 */
int  getUiSoftKeyboardHeight(void);

void setUiWindowToolWindow(HWND hWnd, BOOL bToolWindow);

BOOL isUiWindowMinimized(HWND hWnd);
BOOL isUiWindowMaximized(HWND hWnd);

// 供 OnEnumWindows 使用，从 UIView 获取 HWND
HWND getHwndFromUiView(void *view);

#endif//_SUIWINDOW_H_
