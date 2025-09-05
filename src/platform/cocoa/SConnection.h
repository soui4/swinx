#ifndef _SCONN_H_
#define _SCONN_H_

#include <windows.h>
#include <map>
#include <list>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <vector>
#include <sdc.h>
#include <SRwLock.hpp>
#include <uimsg.h>
#include "os_state.h"
#include "SConnBase.h"
#include "SNsWindow.h"
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
class STrayIconMgr;


/// @brief 
class SConnection : public SConnBase{
  public:
 
    enum {
        TM_CARET = -100, //timer for caret blink
        TS_CARET = 500,//default caret blink elapse, 500ms
        TM_FLASH = -101, //timer for flash window

        TM_HOVERDELAY = -50,
    };

    SConnection(int screenNum);
    ~SConnection();

    public:
    void onTerminate() override;
    void OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDrawRect(HWND hWnd, const RECT &rc, cairo_t *ctx) override;
    void OnNsActive(HWND hWnd, BOOL bActive) override;
  public:
    void BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    void AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res);
  public:
    SHORT GetKeyState(int vk);
    bool GetKeyboardState(PBYTE lpKeyState);
    SHORT GetAsyncKeyState(int vk);
    UINT MapVirtualKey(UINT uCode, UINT uMapType) const;
  public:
      UINT GetDoubleClickTime() const ;
      LONG GetMsgTime() const;
      DWORD GetMsgPos() const;

    DWORD GetQueueStatus(UINT flags);
    bool waitMsg();
    int waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, bool fWaitAll, DWORD dwWaitMask);

    bool TranslateMessage(const MSG *pMsg);
    bool peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
    bool getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
    void postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp);
    void postMsg2(bool bWideChar, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply);

    UINT_PTR SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc);
    bool KillTimer(HWND hWnd, UINT_PTR id);
    HDC GetDC();
    bool ReleaseDC(HDC hdc);

    HWND SetCapture(HWND hCapture);
    bool ReleaseCapture();
    HWND GetCapture() const;
    HCURSOR SetCursor(HWND hWnd,HCURSOR cursor);
    HCURSOR GetCursor();
    bool DestroyCursor(HCURSOR cursor);

    void SetTimerBlock(bool bBlock);

    HWND GetActiveWnd() const;

    bool SetActiveWindow(HWND hWnd);

    HWND WindowFromPoint(POINT pt, HWND hWnd) const;
    
    bool IsWindow(HWND hWnd) const;

    void SetWindowPos(HWND hWnd,int x,int y) const;
    void SetWindowSize(HWND hWnd,int cx,int cy) const;
    
    bool MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const;

    bool GetCursorPos(LPPOINT ppt) const;

    int GetDpi(bool bx) const;

    void KillWindowTimer(HWND hWnd);

    HWND GetForegroundWindow();
    bool SetForegroundWindow(HWND hWnd);

    bool SetWindowOpacity(HWND hWnd, BYTE byAlpha);
    bool SetWindowRgn(HWND hWnd, HRGN hRgn);
    HKL  ActivateKeyboardLayout(HKL hKl);

    HBITMAP GetDesktopBitmap();

    HWND GetFocus() const ;
    bool SetFocus(HWND hWnd);

    bool IsDropTarget(HWND hWnd);

    bool FlashWindowEx(PFLASHWINFO info);
    int OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount);
    bool OnSetWindowText(HWND hWnd, _Window *wndObj, LPCSTR lpszString);
    int OnGetWindowTextLengthA(HWND hWnd);
    int OnGetWindowTextLengthW(HWND hWnd);
    int OnGetWindowTextA(HWND hWnd, char *buf, int bufLen);
    int OnGetWindowTextW(HWND hWnd,wchar_t *buf,int bufLen);
    HWND OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName);
    bool OnEnumWindows(HWND hParent, HWND hChildAfter,WNDENUMPROC lpEnumFunc, LPARAM lParam);
    HWND OnGetAncestor(HWND hwnd,UINT gaFlags);
    HMONITOR MonitorFromWindow(HWND hWnd, DWORD dwFlags);
    HMONITOR MonitorFromPoint(POINT pt,  DWORD dwFlags );
    HMONITOR MonitorFromRect(LPCRECT lprc, // rectangle
                             DWORD dwFlags // determine return value
    );
    int GetScreenWidth(HMONITOR hMonitor) const;
    int GetScreenHeight(HMONITOR hMonitor) const;
    HWND GetScreenWindow() const;
    void UpdateWindowIcon(HWND hWnd, _Window * wndObj);
    uint32_t GetVisualID(bool bScreen) const;
    uint32_t GetCmap() const;
    void SetZOrder(HWND hWnd, _Window * wndObj,HWND hWndInsertAfter);
    void SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len);
    uint32_t GetIpcAtom() const ;
    void postCallbackTask(CbTask *pTask);
    cairo_surface_t *CreateWindowSurface(HWND hWnd, uint32_t visualId, int cx, int cy);
    cairo_surface_t * ResizeSurface(cairo_surface_t *surface,HWND hWnd, uint32_t visualId, int width, int height);
    DWORD GetWndProcessId(HWND hWnd);
    HWND WindowFromPoint(POINT pt);
    bool GetClientRect(HWND hWnd, RECT *pRc);
    void SendSysCommand(HWND hWnd, int nCmd);
    bool IsWindowVisible(HWND hWnd);
    HWND GetWindow(HWND hWnd, _Window *wndObj,UINT uCmd);
    UINT RegisterMessage(LPCSTR lpString);
    bool NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData);
    HMONITOR GetScreen(DWORD dwFlags) const;
    void updateWindow(HWND hWnd, const RECT &rc);
    void commitCanvas(HWND hWnd, const RECT &rc);
    void EnableWindow(HWND hWnd, int bEnable);
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
    bool CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight);
    bool DestroyCaret();
    bool ShowCaret(HWND hWnd);
    bool HideCaret(HWND hWnd);
    bool SetCaretPos(int X,int Y);
    bool GetCaretPos(LPPOINT lpPoint);
    const CaretInfo* GetCaretInfo() const;
    void SetCaretBlinkTime(UINT blinkTime);
    UINT GetCaretBlinkTime() const ;

    void GetWorkArea(HMONITOR hMonitor, RECT* prc);
public:
    SClipboard* getClipboard() ;

    bool EmptyClipboard();

    bool IsClipboardFormatAvailable(UINT format);

    bool OpenClipboard(HWND hWndNewOwner);

    bool CloseClipboard();

    HWND GetClipboardOwner();

    HANDLE GetClipboardData(UINT uFormat);

    HANDLE SetClipboardData(UINT uFormat, HANDLE hMem);

    UINT RegisterClipboardFormatA(LPCSTR pszName);
  public:
      STrayIconMgr* GetTrayIconMgr() ;

      void EnableDragDrop(HWND hWnd, bool enable);
      HRESULT DoDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,     
                          DWORD *pdwEffect);

      HWND OnWindowCreate(_Window *wnd,CREATESTRUCT *cs,int depth);
      void OnWindowDestroy(HWND hWnd,_Window *wnd);
      void SetWindowVisible(HWND hWnd, _Window *wnd, bool bVisible, int nCmdShow);
      void SetParent(HWND hWnd, _Window *wnd,HWND parent);
      void SendExposeEvent(HWND hWnd, LPCRECT rc);
      void SetWindowMsgTransparent(HWND hWnd,_Window * wndObj,bool bTransparent);
      void AssociateHIMC(HWND hWnd,_Window *wndObj,HIMC hIMC);
    public:
        void flush();

        void sync();

        bool IsScreenComposited() const;
  protected:
      void updateMsgQueue(DWORD dwTimeout);

      int GetRectDisplayIndex(int x, int y, int w, int h);
      int GetDisplayBounds(int displayIndex, RECT *rect);
      void postMsg(Msg *pMsg);
      void stopEventWaiting();

      HWND _GetRoot(HWND hWnd);
      HWND _GetParent(HWND hWnd);
  private:
      CountMutex m_mutex;
      std::list<Msg*> m_msgQueue;
      Msg * m_msgPeek=nullptr;
      bool m_bMsgNeedFree = false;
      std::list<TimerInfo> m_lstTimer;
      bool m_bBlockTimer = false;
      uint64_t m_tsLastMsg=-1;
      std::atomic<bool> m_bQuit;

      std::list<Msg *> m_msgStack; // msg stack that are handling
      std::list<CbTask *> m_lstCallbackTask;
      
      tid_t m_tid;

      HDC m_deskDC;
      HBITMAP m_deskBmp;  
      HWND m_hWndCapture=NULL;  
      HWND m_hFocus = NULL;
      HWND m_hActive = NULL;
      HWND m_hForeground = NULL;
      CaretInfo m_caretInfo;
      UINT m_caretBlinkTime = TS_CARET;
      SClipboard* m_clipboard;
      STrayIconMgr* m_trayIconMgr;
      std::map<HWND,HCURSOR>          m_wndCursor;
};

class SConnMgr {
  friend class SConnection;

  private:
    HANDLE m_hHeap;
    SConnection * m_conn;
    tid_t m_tid;

  public:
    static SConnMgr *instance();

  public:
    SConnection *getConnection(tid_t tid = 0, int screenNum = 0);
    HANDLE getProcessHeap(){
        return m_hHeap;
    }
  private:
    SConnMgr();
    ~SConnMgr();
};

#endif //_SCONN_H_