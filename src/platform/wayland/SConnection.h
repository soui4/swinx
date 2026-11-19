/*
 * SConnection.h -- Wayland 平台连接
 *
 * 声明面沿用 src/platform/wasm/SConnection.h 的“非 xcb 平台形态”，保证
 * src/ 下的共享代码（wnd.cpp / msgpump.cpp / gdi.cpp 等）在不引入 XCB 的
 * 前提下能通过编译。
 *
 * 已实现：wl_display / registry / xdg_wm_base / wl_shm / wl_subcompositor /
 * wl_seat 连接；每窗口一个 wl_surface + SHM buffer 池 + frame callback，
 * configure/close 与 pointer/keyboard 事件回投（见 framebuffer.cpp）。
 *
 * 已接入 wl_output 输出尺寸与 xdg_popup 定位；仍未接入：每显示器缩放、
 * wl_data_device 跨进程剪贴板、xdg-decoration。
 */
#ifndef _SWINX_WAYLAND_SCONNECTION_H_
#define _SWINX_WAYLAND_SCONNECTION_H_

#include <windows.h>
#include <ole2.h> // IDataObject / IDropSource（DoDragDrop 契约）
#include <hkl.h>  // HKL 取值编码约定（swinx 内部头）
#include <map>
#include <list>
#include <set>
#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <string>
#include <stdint.h>
#include <SRwLock.hpp>
#include <sdc.h>
#include <uimsg.h>
#include "countmutex.h"

class _Window;
#include "SClipboard.h"
#ifdef SWINX_HAVE_SNI
#include "STrayIconMgr.h"
#endif

struct TimerInfo
{
    UINT_PTR id;
    HWND hWnd;
    UINT elapse;
    UINT fireRemain;
    TIMERPROC proc;
};

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
    /* 双击判定（Win32 CS_DBLCLKS 语义）：idx 0/1/2 = 左/右/中键，t 是本次按下
     * 的合成器时间戳。返回距同键上次按下的毫秒数，首次按下返回 -1u；返回时
     * 已记录本次时间，所以三连击会得到两次 DBLCLK（与 X11 的 m_tsPrevPress
     * 逐个更新一致）。Wayland 既没有 X11 那种服务端 DoubleClick 事件，也没有
     * Win32 的系统合成，只能在按键入口自己算。 */
    uint32_t OnButtonPress(int idx, uint32_t t);
    LONG GetMsgTime() const;
    DWORD GetMsgPos() const;
    DWORD GetQueueStatus(UINT flags);
    bool waitMsg(UINT timeOut = INFINITE);
    /* 睡到 wl_display 可读 / 消息队列有信号 / ms 超时；同时把 m_hQueueEvt 的
     * 唤醒信号读空（pipe 实现的 auto-reset event，不读会写满并阻塞 write）。 */
    void waitWakeup(DWORD ms);
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
    /* 按 Win32 父链规则取窗口光标（framebuffer 的光标是 seat 级共享状态，
     * 由这里做"用哪个窗口的光标"的解析）。 */
    HCURSOR CursorForWindow(HWND hWnd) const;
    void SetMouseFocus(HWND hWnd);
    HWND MouseFocusHwnd() const;
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
    // 多显示器接口（骨架阶段退化为单屏实现）
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
    cairo_surface_t *CreateWindowSurface(HWND hWnd, int cx, int cy);
    cairo_surface_t *ResizeSurface(cairo_surface_t *surface, HWND hWnd, int cx, int cy);
    DWORD GetWndProcessId(HWND hWnd);
    HWND WindowFromPoint(POINT pt);
    BOOL GetClientRect(HWND hWnd, RECT *pRc);
    void SendSysCommand(HWND hWnd, int nCmd);
    BOOL IsWindowVisible(HWND hWnd);
    HWND GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd);
    UINT RegisterMessage(LPCSTR lpString);
    UINT RegisterClipboardFormatA(LPCSTR pszName);
    BOOL NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData);
    /* Wayland 下图标由面板绘制、指针事件被面板消费，应用拿不到托盘鼠标消息；
     * 菜单只能导出成 dbusmenu 交给面板。这里把应用提供的菜单内容转给 STrayIconMgr，
     * 返回 TRUE 表示宿主接管菜单（应用不要再自绘 TrackPopupMenu）。 */
    BOOL SetTrayMenuProvider(HWND hWnd, UINT uID, const TRAYMENUPROVIDER *pProvider);
    BOOL UpdateTrayMenu(HWND hWnd, UINT uID);
    HMONITOR GetScreen(DWORD dwFlags) const;
    void updateWindow(HWND hWnd, const RECT &rc);
    void commitCanvas(HWND hWnd, const RECT &rc);
    /* 延迟上屏。commitCanvas() 只把改动区域并进 m_pendingCommits，真正的 blit +
     * wl_shm 提交推迟到最外层消息派发结束（AfterProcMsg）或下一次取消息之前。
     * 一次 WM_PAINT 里框架默认处理、应用的 BeginPaint/EndPaint、WM_NCPAINT 各自
     * ReleaseDC 一次，同步上屏就是几倍的 memcpy 和几回 commit。
     * 只对重叠的矩形取并集：互不相交的两块并起来是更大的包围盒，面积反而上升，
     * 这时先把欠的那块上屏、再重新记账（presentPending）。 */
    struct PendingCommit
    {
        RECT rcSrc;  // 窗口画布坐标（帧缓冲坐标在上屏时按窗口位置重算）
    };
    void flushPendingCommits();
    /* ---- 窗口 z 序（仅用于全量提交时的顺序遍历）----------------------------
     * 每个窗口现在有自己的 wl_surface，重叠与 z 序由 Wayland 合成器负责。
     * m_zTop 只存顶层窗口（底→顶 = 创建序），WS_CHILD 存在 m_children 里；
     * 子窗口的 rc 是父客户区坐标，顶层/WS_POPUP 的 rc 已是屏幕坐标，取屏幕
     * 矩形一律走 screenRect()。 */
    BOOL screenRect(HWND hWnd, RECT *prc) const;
    /* 建窗时父窗口 surface 还没就绪、被推迟的"对话框转 popup"，在这里补做。
     * 返回 1 = 处理完了（转成功，或已经不需要处理，表项已清）；0 = 还得等。 */
    int tryPendingDialogParent(HWND hWnd);
    /* 该 HWND 是否在"待转 xdg_popup"的登记表里。normalizeWlToplevelOrigin()
     * 用它保护对话框的 rc：转成 popup 之前 rc 还是 positioner 的锚点来源。 */
    bool isPendingDialog(HWND hWnd) const;
    void buildComposeOrder(std::vector<HWND> &order) const;
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
    HRESULT RegisterDragDrop(HWND hWnd, IDropTarget *pTarget);
    HRESULT RevokeDragDrop(HWND hWnd);
    IDropTarget *getDropTarget(HWND hWnd) const;
    HRESULT DoDragDrop(IDataObject *pDataObject, IDropSource *pDropSource, DWORD dwOKEffect, DWORD *pdwEffect);

    // wl_data_device 拖放事件回投（offer 是指针，仅在事件回调内有效）
    void onWlDndEnter(HWND hWnd, int x, int y, int keyState, void *offer);
    void onWlDndMotion(HWND hWnd, int x, int y, int keyState, void *offer);
    void onWlDndLeave(HWND hWnd);
    void onWlDndDrop(HWND hWnd, void *offer);
    void onWlImeCommit(HWND hWnd, const char *utf8, int serial);
    HWND OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth);
    void OnWindowDestroy(HWND hWnd, _Window *wnd);
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

    // framebuffer 的 Wayland 事件回投（仅在 UI 线程、事件泵内调用）。
    // 每窗口一条：configure/close 带上目标 HWND（GTK 每窗口一个 wl_surface）。
    void onWlConfigure(HWND hWnd, int cx, int cy);
    void onWlClose(HWND hWnd);
    void onWlPopupDone(HWND hWnd);
    void onWlPopupConfigure(HWND hWnd, int x, int y, int w, int h);

    /* 客户端自绘标题栏的拖动/缩放（对齐 GTK gdk_wayland_window_begin_move_drag
     * / begin_resize_drag）：Wayland 没有标题栏概念，合成器不会自动帮我们移动
     * 自绘 NC 的窗口，必须在 pointer press 拿到 serial 后主动发
     * xdg_toplevel_move/resize。htCode 是 WM_NCHITTEST 结果（HTCAPTION/各边）。 */
    void beginWlDrag(HWND hWnd, UINT htCode);

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
    HWND m_hFocus;      // 键盘焦点（SetFocus）
    HWND m_hMouseFocus; // 指针焦点（pointer enter），只用于光标/GetCursor
    HWND m_hActive;
    HWND m_hForeground;
    HKL  m_hkl = 0;
    std::map<HWND, HCURSOR> m_wndCursor;
    std::map<std::string, UINT> m_regMessages;
    UINT m_nextRegMsg;
    BYTE m_keyboardState[256];
    /* 每个按键上次按下的合成器时间戳（0 = 还没按过），供 OnButtonPress 判双击。 */
    uint32_t m_tsPrevPress[3] = { 0, 0, 0 };
    int m_cursorCount;
    int m_screenW, m_screenH;
    int m_curX = 0, m_curY = 0;   // virtual cursor (input injection)
    CaretInfo m_caretInfo;
    // 延迟上屏：待合并的改动区域 + 消息派发嵌套深度（只在最外层那次结束后上屏）
    std::map<HWND, PendingCommit> m_pendingCommits;
    // 多顶层窗口（弹出窗）合成：z 序表 + 至少上屏过一帧的窗口集合（未画过的
    // 窗口画布是全 0，整幅合成时跳过它，避免弹出窗首帧前闪黑块）
    std::vector<HWND> m_zTop;
    std::map<HWND, std::vector<HWND>> m_children;
    std::set<HWND> m_committed;
    /* HWND_MESSAGE 消息窗（SNotifyReceiver / SMsgLoop 的 msgWnd）从不绘制，
     * 但 OnWindowCreate 仍把它们放进 m_zTop；buildComposeOrder 遍历时为它们
     * 懒建出一个 TOPLEVEL surface（标题 "soui"、无内容），合成器任务列表就
     * 多出这些幽灵窗口。这里单独记下，合成/绘制时显式跳过。 */
    std::set<HWND> m_messageOnly;
    /* 待转 popup 的对话框：hwnd(内容窗或装饰窗) -> owner。建窗时父窗口的
     * xdg_surface 可能还没建好（例如对话框在父窗 WM_CREATE 里创建），
     * SwinxWlSurfaceSetDialogParent 会失败；记在这里，等自己的 surface 第一次
     * 同步时再试。不重试的话对话框会永久停在平级 xdg_toplevel，被合成器
     * 摆到工作区原点（左上角）。 */
    std::map<HWND, HWND> m_pendingDialogParent;
    mutable bool m_bScreenDirty;
    int m_msgRecursion;
    UINT m_caretBlinkTime;
    SClipboard *m_clipboard;
#ifdef SWINX_HAVE_SNI
    STrayIconMgr *m_trayMgr; // 系统托盘（SNI over D-Bus），懒创建
#endif
    // wl_data_device 拖放：登记的目标窗口 + 当前会话状态
    std::map<HWND, IDropTarget *> m_dropTargets;
    IDropTarget *m_dndTarget;
    IDataObject *m_dndDataObject;
    void *m_dndOffer;
    DWORD m_dndEffect;
    int m_dndKeyState;
    int m_dndX, m_dndY;      // 最后一次 enter/motion 的 surface 局部坐标
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

#endif // _SWINX_WAYLAND_SCONNECTION_H_
