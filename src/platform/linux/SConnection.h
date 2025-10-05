#ifndef _SCONN_H_
#define _SCONN_H_

#include <windows.h>
#include <map>
#include <list>
#include <pthread.h>
#include <xcb/xcb.h>
#include <cairo-xcb.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <vector>
#include <imm.h>
#include <countmutex.h>
#include "SImContext.h"
#include "sdc.h"
#include "SRwLock.hpp"
#include "uimsg.h"
#include "atoms.h"
#include "STrayIconMgr.h"
#include "cursormgr.h"


struct TimerInfo
{
    UINT_PTR id;
    HWND hWnd;
    UINT elapse;
    UINT fireRemain;
    TIMERPROC proc;
};

struct hook_table;

struct IEventChecker {
    virtual bool checkEvent(xcb_generic_event_t* e) const = 0;
};

class SKeyboard;
class SClipboard;
class _Window;

class SConnection {
    friend class SClipboard;
  public:
    enum NetWmState
    {
        NetWmStateAbove = 0x1,
        NetWmStateBelow = 0x2,
        NetWmStateFullScreen = 0x4,
        NetWmStateMaximizedHorz = 0x8,
        NetWmStateMaximizedVert = 0x10,
        NetWmStateModal = 0x20,
        NetWmStateStaysOnTop = 0x40,
        NetWmStateDemandsAttention = 0x80,
        NetWMStateFocus = 0x100,
    };

    enum {
        TM_CARET = -100, //timer for caret blink
        TS_CARET = 500,//default caret blink elapse, 500ms
        TM_FLASH = -101, //timer for flash window
        TM_DELAY = -102, //timer for delay WM_PAINT message
        TM_HOVERDELAY = -50,
    };

    SConnection(int screenNum);
    ~SConnection();

  public:
    struct hook_table *m_hook_table;
    xcb_connection_t *connection = nullptr;
    xcb_screen_t *screen = nullptr;
    const xcb_setup_t *m_setup = nullptr;
    xcb_visualtype_t * rgba_visual=nullptr;
    int  m_forceDpi=-1;
    SAtoms atoms;
  public:
    SHORT GetKeyState(int vk);
    BOOL GetKeyboardState(PBYTE lpKeyState);
    SHORT GetAsyncKeyState(int vk);
    UINT MapVirtualKey(UINT uCode, UINT uMapType) const;
  public:
      UINT GetDoubleClickTime() const {
          return m_tsDoubleSpan;
      }
      LONG GetMsgTime() const;
      DWORD GetMsgPos() const;

    DWORD GetQueueStatus(UINT flags);
    bool waitMsg();
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
    HCURSOR SetCursor(HWND hWnd,HCURSOR cursor);
    HCURSOR GetCursor();
    BOOL DestroyCursor(HCURSOR cursor);

    void SetTimerBlock(bool bBlock)
    {
        m_bBlockTimer = bBlock;
    }

    HWND GetActiveWnd() const;

    BOOL SetActiveWindow(HWND hWnd);

    HWND WindowFromPoint(POINT pt, HWND hWnd) const;
    
    BOOL IsWindow(HWND hWnd) const;

    void SetWindowPos(HWND hWnd,int x,int y) const;
    void SetWindowSize(HWND hWnd,int cx,int cy) const;
    
    BOOL MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const;

    BOOL GetCursorPos(LPPOINT ppt) const;

    int GetDpi(BOOL bx) const;

    void KillWindowTimer(HWND hWnd);

    HWND GetForegroundWindow();
    BOOL SetForegroundWindow(HWND hWnd);

    BOOL SetWindowOpacity(HWND hWnd, BYTE byAlpha);
    BOOL SetWindowRgn(HWND hWnd, HRGN hRgn);
    HKL  ActivateKeyboardLayout(HKL hKl);

    HBITMAP GetDesktopBitmap(){return m_deskBmp;}

    HWND GetFocus() const {
        return m_hFocus;
    }
    BOOL SetFocus(HWND hWnd);

    BOOL IsDropTarget(HWND hWnd);

    BOOL FlashWindowEx(PFLASHWINFO info);
    void changeNetWmState(HWND hWnd, bool set, xcb_atom_t one, xcb_atom_t two);
    int OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount);
    BOOL OnSetWindowText(HWND hWnd, _Window *wndObj, LPCSTR lpszString);
    int OnGetWindowTextLengthA(HWND hWnd);
    int OnGetWindowTextLengthW(HWND hWnd);
    int OnGetWindowTextA(HWND hWnd, char *buf, int bufLen);
    int OnGetWindowTextW(HWND hWnd,wchar_t *buf,int bufLen);
    HWND OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName);
    BOOL OnEnumWindows(HWND hParent, HWND hChildAfter,WNDENUMPROC lpEnumFunc, LPARAM lParam);
    HWND OnGetAncestor(HWND hwnd,UINT gaFlags);

    cairo_surface_t * CreateWindowSurface(HWND hWnd,uint32_t visualId,int cx, int cy);
    cairo_surface_t * ResizeSurface(cairo_surface_t *surface,HWND hWnd, uint32_t visualId, int width, int height);
    
    void SendSysCommand(HWND hWnd,int nCmd);
    BOOL EnableWindow(HWND hWnd, BOOL bEnable);
  public:
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
    BOOL SetCaretPos(int X,int Y);
    BOOL GetCaretPos(LPPOINT lpPoint);
    const CaretInfo* GetCaretInfo() const {
        return &m_caretInfo;
    }
    void SetCaretBlinkTime(UINT blinkTime);
    UINT GetCaretBlinkTime() const {
        return m_caretBlinkTime;
    }

    void GetWorkArea(HMONITOR hMonitor,RECT* prc);
public:
    SClipboard* getClipboard() {
        return m_clipboard;
    }

    BOOL EmptyClipboard();

    BOOL IsClipboardFormatAvailable(UINT format);

    BOOL OpenClipboard(HWND hWndNewOwner);

    BOOL CloseClipboard();

    HWND GetClipboardOwner();

    HANDLE GetClipboardData(UINT uFormat);

    HANDLE SetClipboardData(UINT uFormat, HANDLE hMem);

  public:
      bool hasXFixes() const { return xfixes_first_event > 0; }
      STrayIconMgr* GetTrayIconMgr() { return m_trayIconMgr; }

      void EnableDragDrop(HWND hWnd, BOOL enable);
      void SendXdndStatus(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect);
      void SendXdndFinish(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect);
      xcb_atom_t clipFormat2Atom(UINT uFormat);
      uint32_t atom2ClipFormat(xcb_atom_t atom);
      std::shared_ptr< std::vector<char>> readSelection(bool bXdnd,uint32_t fmt);

      HWND OnWindowCreate(_Window *wnd,CREATESTRUCT *cs,int depth);
      void OnWindowDestroy(HWND hWnd,_Window *wnd);
      void SetWindowVisible(HWND hWnd, _Window *wnd, BOOL bVisible, int nCmdShow);
      void SetParent(HWND hWnd, _Window *wnd,HWND parent);
      void SendExposeEvent(HWND hWnd, LPCRECT rc,BOOL bForce=FALSE);
      void SetWindowMsgTransparent(HWND hWnd,_Window * wndObj,BOOL bTransparent);
      void AssociateHIMC(HWND hWnd,_Window *wndObj,HIMC hIMC);

    void UpdateWindowIcon(HWND hWnd, _Window * wndObj);
    int GetScreenWidth(HMONITOR hMonitor) const;
    int GetScreenHeight(HMONITOR hMonitor) const;
    HWND GetScreenWindow() const;

    uint32_t GetVisualID(BOOL bScreen) const;
    uint32_t GetCmap() const;
    void SetZOrder(HWND hWnd, _Window * wndObj,HWND hWndInsertAfter);
    void OnStyleChanged(HWND hWnd, _Window* wndObj,DWORD oldStyle,DWORD newStyle);
    void OnExStyleChanged(HWND hWnd, _Window* wndObj,DWORD oldStyle,DWORD newStyle);
    void SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len);
    uint32_t GetIpcAtom() const {
        return atoms.WM_WIN4XCB_IPC;
    }
    DWORD GetWndProcessId(HWND hWnd);
    HWND WindowFromPoint(POINT pt);
    BOOL GetClientRect(HWND hWnd, RECT *pRc);
    BOOL IsWindowVisible(HWND hWnd);
    HWND GetWindow(HWND hWnd, _Window *wndObj,UINT uCmd);
    UINT RegisterMessage(LPCSTR lpString);
    UINT RegisterClipboardFormatA(LPCSTR lpString);
    BOOL NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData);
    HMONITOR GetScreen(DWORD dwFlags) const;
    void updateWindow(HWND hWnd, const RECT &rc);
    void commitCanvas(HWND hWnd, const RECT &rc);
  public:
    void BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    void AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res);

    public:
        xcb_timestamp_t getSectionTs();
        void flush() {
            xcb_flush(connection);
        }

        void sync()
        {
            // from xcb_aux_sync
            xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
            free(xcb_get_input_focus_reply(connection, cookie, 0));
        }

        xcb_generic_event_t* checkEvent(int type);

        xcb_generic_event_t* checkEvent(IEventChecker* checker);

        BOOL IsScreenComposited() const {
            return m_bComposited;
        }
  private:
    int _waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, DWORD dwWaitMask);
    void readXResources();
    void initializeXFixes();
    void clearSystemCursor();
    bool event2Msg(bool bTimeout, int elapse, uint64_t ts);
    void OnFocusChanged(HWND hFocus);
    void OnActiveChange(HWND hWnd,BOOL bActive);
    bool existTimer(HWND hWnd, UINT_PTR id) const;
    xcb_cursor_t createXcbCursor(HCURSOR cursor);
    uint32_t netWmStates(HWND hWnd);
    void updateWmclass(HWND hWnd, _Window *wndObj);
    DWORD XdndAction2Effect(xcb_atom_t action);

    xcb_atom_t XdndEffect2Action(DWORD dwEffect);

    bool pushEvent(xcb_generic_event_t *e);

    static void *readProc(void *p);
    void _readProc();

    void updateWorkArea();
    xcb_cursor_t getXcbCursor(HCURSOR cursor);
    void postMsg(Msg *pMsg);
    uint32_t GetDoubleClickSpan();
  private:
    static void xim_commit_string(xcb_xim_t *im, xcb_xic_t ic, uint32_t flag, char *str,
      uint32_t length, uint32_t *keysym, size_t nKeySym,
      void *user_data);
    static void xim_forward_event(xcb_xim_t *im, xcb_xic_t ic, xcb_key_press_event_t *event,
        void *user_data);
    static void xim_open_callback(xcb_xim_t *im, void *user_data);
    static void xim_create_ic_callback(xcb_xim_t *im, xcb_xic_t new_ic, void *user_data);
    static void xim_logger(const char *fmt, ...);
  private:
    std::mutex m_mutex4Evt;
    std::list<xcb_generic_event_t *> m_evtQueue;

    mutable CountMutex m_mutex4Msg;
    std::list<Msg *> m_msgQueue;
    xcb_timestamp_t m_tsSelection;
    xcb_timestamp_t m_tsPrevPress[3]={-1u,-1u,-1u};    
    xcb_timestamp_t m_tsDoubleSpan = 400;

    std::list<Msg *> m_msgStack; // msg stack that are handling
    std::list<CbTask *> m_lstCallbackTask;

    Msg *m_msgPeek;
    bool m_bMsgNeedFree;
    std::thread m_trdEvtReader;
    std::atomic<bool> m_bQuit;

    std::list<TimerInfo> m_lstTimer;
    bool m_bBlockTimer;
    uint64_t m_tsLastMsg=-1;
    uint64_t m_tsLastPaint=-1;
    bool     m_bDelayPaint = false;
    HDC m_deskDC;
    HBITMAP m_deskBmp;

    HWND m_hWndCapture;

    HWND m_hWndActive;
    HWND m_hFocus;
    xcb_window_t m_setting_owner=0;
    std::map<HCURSOR, xcb_cursor_t> m_sysCursor;
    std::map<HWND,HCURSOR>          m_wndCursor;
    SKeyboard *m_keyboard;
    SClipboard* m_clipboard;
    STrayIconMgr* m_trayIconMgr;

    HANDLE m_evtSync;
    tid_t m_tid;
    HKL   m_hkl = 0;
    xcb_xim_t *m_xim;

    CaretInfo m_caretInfo;
    UINT m_caretBlinkTime = TS_CARET;
    BOOL m_bComposited = FALSE;
    RECT m_rcWorkArea = { 0 };
    uint32_t xfixes_first_event = 0;
};

class SConnMgr {
    swinx::SRwLock m_rwLock;
    std::map<tid_t, SConnection *> m_conns;
    HANDLE m_hHeap;

    friend class SConnection;

  public:
    static SConnMgr *instance();

  public:
    SConnection *getConnection(tid_t tid = 0, int screenNum = 0);
    HANDLE getProcessHeap(){
        return m_hHeap;
    }
  private:
    void removeConn(SConnection *pObj);

    SConnMgr();
    ~SConnMgr();
};

#endif //_SCONN_H_