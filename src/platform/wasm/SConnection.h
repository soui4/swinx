/*
 * SConnection.h -- WebAssembly platform connection.
 *
 * Forked from src/platform/freertos/SConnection.h, which is itself modeled on
 * src/platform/mobile/SConnection.h (the framework's non-xcb platform shape):
 * the connection owns a Win32-style message queue, timers, keyboard state and
 * caret, and exposes the Win32 window-management surface the swinx core
 * (wnd.cpp / nativewnd.cpp / uimsg.cpp ...) expects.
 *
 * Differences from the FreeRTOS port -- Emscripten ships a POSIX-ish libc,
 * real <mutex>/<thread> and pthreads, so the bare-metal scaffolding is gone:
 *   - the Win32 kernel-object layer (events / mutexes / semaphores /
 *     interlocked) comes from src/platform/posix/sysobjs.cpp + syncapi.cpp,
 *     not from this directory's winobjs.cpp;
 *   - no STL shims: the toolchain's <mutex>/<thread>/<chrono> are used as-is;
 *   - no g_platformAPI injection point: the queue is driven directly by
 *     postMsg() from the JS host's event handlers;
 *   - timers are managed inside the connection (GetTickCount based) instead of
 *     the OS timer service;
 *   - rendering is software: windows paint into cairo image surfaces and
 *     commitCanvas() blits dirty regions into framebuffer.h's framebuffer,
 *     whose present callback is where the JS host copies pixels to a canvas.
 */
#ifndef _SWINX_WASM_SCONNECTION_H_
#define _SWINX_WASM_SCONNECTION_H_

#include <windows.h>
#include <hkl.h> // HKL 取值编码约定（swinx 内部头）
#include <map>
#include <list>
#include <set>
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

struct cairo_surface_t;

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

    // input injection support (see input.h): driver/test level state setters
    // that keep GetKeyState / GetCursorPos consistent with injected events
    void SetKeyDown(UINT vk, bool down);
    void ToggleKeyState(UINT vk);
    void SetCursorPos(int x, int y);

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
    // 键盘布局（HKL）抽象：浏览器页无系统输入法，仅做存储/循环
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
    // 多显示器接口（单块 framebuffer，退化成单屏实现）
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
    /* 延迟上屏。commitCanvas() 只把改动区域并进 m_pendingCommits，真正的 blit +
     * JS 回调推迟到最外层消息派发结束（AfterProcMsg）或下一次取消息之前。
     * 一次 WM_PAINT 里框架默认处理、应用的 BeginPaint/EndPaint、WM_NCPAINT 各自
     * ReleaseDC 一次，同步上屏就是几倍的 memcpy 和几回 JS 回调。
     * 只对重叠的矩形取并集：互不相交的两块并起来是更大的包围盒，面积反而上升，
     * 这时先把欠的那块上屏、再重新记账（presentPending）。 */
    struct PendingCommit
    {
        RECT rcSrc;  // 窗口画布坐标（帧缓冲坐标在上屏时按窗口位置重算）
    };
    void flushPendingCommits();
    void presentPending(HWND hWnd, const RECT &rcPending);
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
    /* 多顶层窗口（弹出窗）支持。z 序 = 创建序（底→顶）：弹出窗（SDropDownWnd
     * 等）总是后建，天然在主窗之上；销毁时从表中摘除。m_bScreenDirty 置位后，
     * 下一次 flushPendingCommits() 把所有可见且画过的顶层窗按 z 序整幅重合成
     * ——这同时解决了"弹出窗盖住主窗"与"弹出窗关闭后主窗的曝露区"两件事，
     * 不需要平台再去为底层窗口补 WM_PAINT（每个窗口的画布保留着最近一帧，
     * 重新 blit 就是正确画面）。 */
    std::vector<HWND> shellZOrder() const { return m_zTop; }
    bool screenDirty() const { return m_bScreenDirty; }
    void markScreenDirty() { m_bScreenDirty = true; }
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
    std::list<Msg *> m_msgQueue;
    Msg *m_msgPeek;
    bool m_bMsgNeedFree;
    std::list<Msg *> m_msgStack;
    std::list<CbTask *> m_lstCallbackTask;
    std::list<TimerInfo> m_lstTimer;
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
    std::map<HWND, HCURSOR> m_wndCursor;
    std::map<std::string, UINT> m_regMessages;
    UINT m_nextRegMsg;
    BYTE m_keyboardState[256];
    int m_cursorCount;
    int m_screenW, m_screenH;
    int m_curX = 0, m_curY = 0;   // virtual cursor (input injection)
    CaretInfo m_caretInfo;
    // 延迟上屏：待合并的改动区域 + 消息派发嵌套深度（只在最外层那次结束后上屏）
    std::map<HWND, PendingCommit> m_pendingCommits;
    // 多顶层窗口（弹出窗）合成：z 序表 + 至少上屏过一帧的窗口集合（未画过的
    // 窗口画布是全 0，整幅合成时跳过它，避免弹出窗首帧前闪黑块）
    std::vector<HWND> m_zTop;
    std::set<HWND> m_committed;
    // SetWindowPos/SetWindowSize 钩子是 const 的，脏标记只能挂 mutable
    mutable bool m_bScreenDirty;
    int m_msgRecursion;
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
    std::map<tid_t, SConnection *> m_conns;
};

#endif // _SWINX_WASM_SCONNECTION_H_
