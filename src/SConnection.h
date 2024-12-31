#ifndef _SCONN_H_
#define _SCONN_H_

#include <windows.h>
#include <map>
#include <list>
#include <pthread.h>
#include <xcb/xcb.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <vector>
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

struct ISelectionListener {
  virtual void handleSelectionRequest(xcb_selection_request_event_t *e)=0;
};

class SKeyboard;
class SClipboard;
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
        NetWmStateDemandsAttention = 0x80
    };

    enum {
        TM_CARET = -100, //timer for caret blink
        TS_CARET = 500,//default caret blink elapse, 500ms
        TM_FLASH = -101, //timer for flash window

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
    int  m_forceDpi=false;
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

    HCURSOR SetCursor(HCURSOR cursor);
    HCURSOR GetCursor();
    //HCURSOR LoadCursor(LPCSTR pszName);
    BOOL DestroyCursor(HCURSOR cursor);

    void SetTimerBlock(bool bBlock)
    {
        m_bBlockTimer = bBlock;
    }

    HWND GetActiveWnd() const;

    BOOL SetActiveWindow(HWND hWnd);

    HWND GetParentWnd(HWND hWnd) const;

    HWND GetWindow(HWND hWnd, int code) const;

    HWND WindowFromPoint(POINT pt, HWND hWnd) const;

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
    HWND SetFocus(HWND hWnd);

    BOOL IsDropTarget(HWND hWnd);

    BOOL FlashWindowEx(PFLASHWINFO info);
    void changeNetWmState(HWND hWnd, bool set, xcb_atom_t one, xcb_atom_t two);
  public:
      struct CaretInfo {
          HWND hOwner;
          HBITMAP hBmp;
          int nWidth;
          int nHeight;
          int x;
          int y;
          int  nVisible;
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

    void GetWorkArea(RECT* prc);
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

    UINT RegisterClipboardFormatA(LPCSTR pszName);
  public:
      bool hasXFixes() const { return xfixes_first_event > 0; }
      STrayIconMgr* GetTrayIconMgr() { return m_trayIconMgr; }

      void AddSelectionListener(ISelectionListener* pListener);
      void RemoveSelectionListener(ISelectionListener* pListener);
      void EnableDragDrop(HWND hWnd, BOOL enable);
      void SendXdndStatus(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect);
      void SendXdndFinish(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect);
      xcb_atom_t clipFormat2Atom(UINT uFormat);
      uint32_t atom2ClipFormat(xcb_atom_t atom);
      std::shared_ptr< std::vector<char>> readXdndSelection(uint32_t fmt);
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
    bool event2Msg(bool bTimeout, UINT elapse, uint64_t ts);
    xcb_cursor_t createXcbCursor(HCURSOR cursor);
    uint32_t netWmStates(HWND hWnd);

    bool pushEvent(xcb_generic_event_t *e);

    static void *readProc(void *p);
    void _readProc();

    void updateWorkArea();
  private:
    std::mutex m_mutex4Evt;
    std::list<xcb_generic_event_t *> m_evtQueue;

    mutable std::recursive_mutex m_mutex4Msg;
    std::list<Msg *> m_msgQueue;
    xcb_timestamp_t m_tsSelection;
    xcb_timestamp_t m_tsPrevPress=-1;    
    xcb_timestamp_t m_tsDoubleSpan = 400;

    std::list<Msg *> m_msgStack; // msg stack that are handling
    std::list<CbTask *> m_lstCallbackTask;

    Msg *m_msgPeek;
    bool m_bMsgNeedFree;
    std::thread m_trdEvtReader;
    std::atomic<bool> m_bQuit;

    std::list<TimerInfo> m_lstTimer;
    bool m_bBlockTimer;

    HDC m_deskDC;
    HBITMAP m_deskBmp;

    HWND m_hWndCapture;
    HCURSOR m_hCursor;

    HWND m_hWndActive;
    HWND m_hFocus;
    std::map<HCURSOR, xcb_cursor_t> m_sysCursor;

    SKeyboard *m_keyboard;
    SClipboard* m_clipboard;
    STrayIconMgr* m_trayIconMgr;

    HANDLE m_evtSync;
    tid_t m_tid;
    HKL   m_hkl = 0;

    CaretInfo m_caretInfo;
    UINT m_caretBlinkTime = TS_CARET;
    BOOL m_bComposited = FALSE;
    RECT m_rcWorkArea = { 0 };
    uint32_t xfixes_first_event = 0;

    std::list<ISelectionListener*> m_lstSelListener;
};

class SConnMgr {
    swinx::SRwLock m_rwLock;
    std::map<pthread_t, SConnection *> m_conns;
    HANDLE m_hHeap;

    friend class SConnection;

  public:
    static SConnMgr *instance();

  public:
    SConnection *getConnection(pthread_t tid = 0, int screenNum = 0);
    HANDLE getProcessHeap(){
        return m_hHeap;
    }
  private:
    void removeConn(SConnection *pObj);

    SConnMgr();
    ~SConnMgr();
};

#endif //_SCONN_H_