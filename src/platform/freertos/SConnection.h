/*
 * SConnection.h -- FreeRTOS platform connection.
 *
 * Modeled on src/platform/mobile/SConnection.h (the framework's non-xcb
 * platform shape): the connection owns a Win32-style message queue, timers,
 * keyboard state and caret, and exposes the Win32 window-management surface
 * the swinx core (wnd.cpp / nativewnd.cpp / uimsg.cpp ...) expects.  All
 * blocking / waiting primitives are built on the FreeRTOS-backed Win32
 * compat layer in this directory (winobjs.cpp, declared via sysapi.h) plus
 * the STL shims (platform/freertos/stl).
 *
 * FreeRTOS specifics vs mobile:
 *   - no g_platformAPI injection point: the queue is driven directly by
 *     postMsg() from input drivers / app code;
 *   - timers are managed inside the connection (tick based) instead of the
 *     OS timer service;
 *   - rendering is software: windows paint into cairo image surfaces and
 *     commitCanvas() blits dirty regions into framebuffer.h's framebuffer.
 */
#ifndef _SWINX_FREERTOS_SCONNECTION_H_
#define _SWINX_FREERTOS_SCONNECTION_H_

#include <windows.h>
#include <hkl.h> // HKL 取值编码约定（swinx 内部头）
#include <map>
#include <list>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <SRwLock.hpp>
#include <sdc.h>
#include <uimsg.h>
#include "SConnBase.h"
#include "SClipboard.h"
#include "countmutex.h"

struct TimerInfo
{
    UINT_PTR id;
    HWND hWnd;
    UINT elapse;
    UINT fireRemain;
    TIMERPROC proc;
};

class _Window;

class SConnection {
public:
    enum {
        TM_CARET = -100,
        TS_CARET = 500,
        TM_FLASH = -101,
        TM_HOVERDELAY = -50,
    };

    SConnection(int screenNum);
    ~SConnection();

    void BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    void AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res);

    SHORT GetKeyState(int vk);
    BOOL GetKeyboardState(PBYTE lpKeyState);
    SHORT GetAsyncKeyState(int vk);
    UINT MapVirtualKey(UINT uCode, UINT uMapType) const;

    UINT GetDoubleClickTime() const;
    LONG GetMsgTime() const;
    DWORD GetMsgPos() const;
    DWORD GetQueueStatus(UINT flags);
    bool waitMsg(UINT timeOut = INFINITE);
    int waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL fWaitAll, DWORD dwWaitMask);

    BOOL TranslateMessage(const MSG *pMsg);
    BOOL peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
    BOOL getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    void postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp);
    void postMsg2(BOOL bWideChar, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply);
    void postCallbackTask(CbTask *pTask);

    UINT_PTR SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc);
    BOOL KillTimer(HWND hWnd, UINT_PTR id);
    HDC GetDC();
    BOOL ReleaseDC(HDC hdc);

    HWND SetCapture(HWND hCapture);
    BOOL ReleaseCapture();
    HWND GetCapture() const;
    HCURSOR SetCursor(HWND hWnd, HCURSOR cursor);
    HCURSOR GetCursor();
    BOOL DestroyCursor(HCURSOR cursor);
    void SetTimerBlock(bool bBlock);

    HWND GetActiveWnd() const;
    BOOL SetActiveWindow(HWND hWnd);
    HWND WindowFromPoint(POINT pt, HWND hWnd) const;
    BOOL IsWindow(HWND hWnd) const;
    void SetWindowPos(HWND hWnd, int x, int y) const;
    void SetWindowSize(HWND hWnd, int cx, int cy) const;
    BOOL MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const;
    BOOL GetCursorPos(LPPOINT ppt) const;
    int GetDpi(BOOL bx) const;
    void KillWindowTimer(HWND hWnd);

    HWND GetForegroundWindow();
    BOOL SetForegroundWindow(HWND hWnd);
    BOOL BringWindowToTop(HWND hWnd);
    BOOL SetWindowOpacity(HWND hWnd, BYTE byAlpha);
    BOOL SetWindowRgn(HWND hWnd, HRGN hRgn);
    HKL ActivateKeyboardLayout(HKL hKl);
    // 键盘布局（HKL）抽象：FreeRTOS 无系统输入法，仅做存储/循环
    HKL GetKeyboardLayout(DWORD idThread);
    UINT GetKeyboardLayoutList(int nBuff, HKL *lpList);
    HBITMAP GetDesktopBitmap();

    HWND GetFocus() const;
    BOOL SetFocus(HWND hWnd);
    BOOL IsDropTarget(HWND hWnd);
    BOOL FlashWindowEx(PFLASHWINFO info);
    int OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount);
    BOOL OnSetWindowText(HWND hWnd, _Window *wndObj, LPCSTR lpszString);
    int OnGetWindowTextLengthA(HWND hWnd);
    int OnGetWindowTextLengthW(HWND hWnd);
    int OnGetWindowTextA(HWND hWnd, char *buf, int bufLen);
    int OnGetWindowTextW(HWND hWnd, wchar_t *buf, int bufLen);
    HWND OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName);
    BOOL OnEnumWindows(HWND hParent, HWND hChildAfter, WNDENUMPROC lpEnumFunc, LPARAM lParam);
    HWND OnGetAncestor(HWND hwnd, UINT gaFlags);
    HMONITOR MonitorFromWindow(HWND hWnd, DWORD dwFlags);
    HMONITOR MonitorFromPoint(POINT pt, DWORD dwFlags);
    HMONITOR MonitorFromRect(LPCRECT lprc, DWORD dwFlags);
    // 多显示器接口（FreeRTOS 单屏退化实现）
    int GetMonitorCount() const;
    HMONITOR GetMonitor(int index) const;
    HMONITOR GetPrimaryMonitor() const;
    bool GetMonitorRect(HMONITOR hMonitor, RECT *prc) const;
    bool GetMonitorWorkRect(HMONITOR hMonitor, RECT *prc) const;
    bool IsPrimaryMonitor(HMONITOR hMonitor) const;

    int GetScreenWidth(HMONITOR hMonitor) const;
    int GetScreenHeight(HMONITOR hMonitor) const;
    HWND GetScreenWindow() const;
    void UpdateWindowIcon(HWND hWnd, _Window *wndObj);
    uint32_t GetVisualID(BOOL bScreen) const;
    uint32_t GetCmap() const;
    void SetZOrder(HWND hWnd, _Window *wndObj, HWND hWndInsertAfter);
    void OnStyleChanged(HWND hWnd, _Window *wndObj, DWORD oldStyle, DWORD newStyle);
    void OnExStyleChanged(HWND hWnd, _Window *wndObj, DWORD oldStyle, DWORD newStyle);
    void SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len);
    uint32_t GetIpcAtom() const;
    cairo_surface_t *CreateWindowSurface(HWND hWnd, uint32_t visualId, int cx, int cy);
    cairo_surface_t *ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId, int cx, int cy);
    DWORD GetWndProcessId(HWND hWnd);
    HWND WindowFromPoint(POINT pt);
    BOOL GetClientRect(HWND hWnd, RECT *pRc);
    void SendSysCommand(HWND hWnd, int nCmd);
    BOOL IsWindowVisible(HWND hWnd);
    HWND GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd);
    UINT RegisterMessage(LPCSTR lpString);
    UINT RegisterClipboardFormatA(LPCSTR pszName);
    BOOL NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData);
    HMONITOR GetScreen(DWORD dwFlags) const;
    void updateWindow(HWND hWnd, const RECT &rc);
    void commitCanvas(HWND hWnd, const RECT &rc);
    void EnableWindow(HWND hWnd, BOOL bEnable);
    BOOL IsIconic(HWND hWnd) const;
    BOOL IsZoomed(HWND hWnd) const;
    int ShowCursor(BOOL bShow);

    UINT GetRawInputDeviceList(
            _Out_writes_opt_(*puiNumDevices) PRAWINPUTDEVICELIST pRawInputDeviceList,
            _Inout_ PUINT puiNumDevices,
            _In_ UINT cbSize);
    UINT GetRawInputDeviceInfoA(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize);
    UINT GetRawInputDeviceInfoW(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize);

    struct CaretInfo
    {
        HWND hOwner;
        HBITMAP hBmp;
        int nWidth;
        int nHeight;
        int x;
        int y;
        int nVisible;
    };

    BOOL CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight);
    BOOL DestroyCaret();
    BOOL ShowCaret(HWND hWnd);
    BOOL HideCaret(HWND hWnd);
    BOOL SetCaretPos(int X, int Y);
    BOOL GetCaretPos(LPPOINT lpPoint);
    const CaretInfo *GetCaretInfo() const;
    void SetCaretBlinkTime(UINT blinkTime);
    UINT GetCaretBlinkTime() const;
    void GetWorkArea(HMONITOR hMonitor, RECT *prc) const;

    SClipboard *getClipboard();
    BOOL EmptyClipboard();
    BOOL IsClipboardFormatAvailable(UINT format);
    BOOL OpenClipboard(HWND hWndNewOwner);
    BOOL CloseClipboard();
    HWND GetClipboardOwner();
    HANDLE GetClipboardData(UINT uFormat);
    HANDLE SetClipboardData(UINT uFormat, HANDLE hMem);

    void EnableDragDrop(HWND hWnd, BOOL enable);
    HRESULT DoDragDrop(IDataObject *pDataObject, IDropSource *pDropSource, DWORD dwOKEffect, DWORD *pdwEffect);
    HWND OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth);
    void OnWindowDestroy(HWND hWnd, _Window *wnd);
    void SetWindowVisible(HWND hWnd, _Window *wnd, BOOL bVisible, int nCmdShow);
    void SetParent(HWND hWnd, _Window *wnd, HWND parent);
    void SendExposeEvent(HWND hWnd, LPCRECT rc, BOOL bForce = FALSE);
    void SetWindowMsgTransparent(HWND hWnd, _Window *wndObj, BOOL bTransparent);
    void AssociateHIMC(HWND hWnd, _Window *wndObj, HIMC hIMC);

    void flush();
    void sync();
    BOOL IsScreenComposited() const;

protected:
    _Window *CreateVirtualWindowObject();
    void updateMsgQueue(DWORD dwTimeout);
    void postMsg(Msg *pMsg);

private:
    CountMutex m_mutex;
    HANDLE m_hQueueEvt;   // auto-reset event: wakes waitMsg() on new message
    swinx_stl::list<Msg *> m_msgQueue;
    Msg *m_msgPeek;
    bool m_bMsgNeedFree;
    swinx_stl::list<Msg *> m_msgStack;
    swinx_stl::list<CbTask *> m_lstCallbackTask;
    swinx_stl::list<TimerInfo> m_lstTimer;
    bool m_bBlockTimer;
    UINT_PTR m_nextTimerId;
    uint64_t m_tsLastMsg;
    std::atomic<bool> m_bQuit;
    tid_t m_tid;

    HDC m_deskDC;
    HBITMAP m_deskBmp;
    int m_screenNum;
    HWND m_hWndCapture;
    HWND m_hFocus;
    HWND m_hActive;
    HWND m_hForeground;
    HKL  m_hkl = 0;
    swinx_stl::map<HWND, HCURSOR> m_wndCursor;
    swinx_stl::map<swinx_stl::string, UINT> m_regMessages;
    UINT m_nextRegMsg;
    BYTE m_keyboardState[256];
    int m_cursorCount;
    int m_screenW, m_screenH;
    CaretInfo m_caretInfo;
    UINT m_caretBlinkTime;
    SClipboard *m_clipboard;
};

class SConnMgr {
    friend class SConnection;

public:
    static SConnMgr *instance();
    SConnection *getConnection(tid_t tid = 0, int screenNum = 0);
    HANDLE getProcessHeap();

private:
    SConnMgr();
    ~SConnMgr();

    HANDLE m_hHeap;
    std::recursive_mutex m_mutex;
    swinx_stl::map<tid_t, SConnection *> m_conns;
};

#endif // _SWINX_FREERTOS_SCONNECTION_H_
