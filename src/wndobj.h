#ifndef _WINDOBJ_H_
#define _WINDOBJ_H_

#include "SConnection.h"
#include "builtin_image.h"

enum WndState
{
    WS_Normal = 0,
    WS_Minimized = 1,
    WS_Maximized = 2,
};

enum HoverState
{
    HS_Leave = 0,
    HS_Hover,
    HS_HoverDelay,
};

struct HoverInfo
{
    DWORD dwFlags;
    UINT uHoverState;  /*hover state*/
    DWORD dwHoverTime; /*hover time*/
};


struct ScrollBar : SCROLLINFO {
    BuiltinImage::ImgState nState;//normal,hover,push, disable
    UINT iHitTest;
    BYTE byFade;
    BOOL bDraging;
    UINT uArrowFlags;
    ScrollBar() :nState(BuiltinImage::St_Normal), byFade(0xff), bDraging(FALSE), uArrowFlags(0), iHitTest(-1) {
        cbSize = sizeof(SCROLLINFO);
        nPos = 0;
        nMin = 0;
        nMax = 0;
        nPage = 0;
        nTrackPos = -1;
    }
};

#define DEF_HOVER_DELAY 5          // hover delay 5ms
#define CR_INVALID      0x00ffffff // RGBA(255,255,255,0)

class _Window
{
private:
    std::recursive_mutex mutex;
    LONG cRef;
public:
    UINT_PTR objOpaque;    
    tid_t tid;
    SConnection* mConnection;
    HDC hdc;
    HBITMAP bmp;
    std::string title;
    RECT rc;
    WndState state;
    ATOM clsAtom;
    HICON iconBig;
    HICON iconSmall;
    int nPainting;
    int nSizing;
    int msgRecusiveCount;
    BOOL bDestroyed;
    BOOL bCaretVisible;
    HoverInfo hoverInfo;
    int  htCode;
    struct
    {
        HRGN hRgn;
        BOOL bErase;
    } invalid;
    COLORREF crKey;
    BYTE byAlpha;
    ScrollBar  sbVert;
    ScrollBar  sbHorz;
    UINT showSbFlags;
    HWND parent;         /* Window parent */
    HWND owner;          /* Window owner */
    WNDPROC winproc;     /* Window procedure */
    HINSTANCE hInstance; /* Window hInstance (from CreateWindow) */
    UINT dwStyle;        /* Window style (from CreateWindow) */
    UINT dwExStyle;      /* Extended style (from CreateWindowEx) */
    UINT_PTR wIDmenu;    /* ID or hmenu (from CreateWindow) */
    UINT helpContext;    /* Help context ID */
    UINT flags;          /* Misc. flags (see below) */
    xcb_visualid_t visualId;
    LPDROPTARGET   dropTarget;
    IDataObject* dragData;
    DWORD_PTR userdata;  /* User private data */
    int cbWndExtra;      /* class cbWndExtra at window creation */
    char* extra;

    _Window(size_t extraLen);
    ~_Window();

    void Lock() {
        AddRef();
        mutex.lock();
    }

    void Unlock() {
        mutex.unlock();
        Release();
    }
    LONG AddRef() {
        return InterlockedIncrement(&cRef);
    }

    LONG Release() {
        LONG ret = InterlockedDecrement(&cRef);
        if (ret == 0) {
            delete this;
        }
        return ret;
    }
};

class WndObj {
    friend class WndMgr;
public:
    WndObj(const WndObj& src);
    ~WndObj();

    _Window* operator->()
    {
        return wnd;
    }

    bool operator!() const
    {
        return wnd == nullptr;
    }

    operator bool() const
    {
        return wnd != nullptr;
    }

    void operator = (const WndObj& src);
private:
    WndObj(_Window* pWnd);
    _Window* wnd;
};

class WndMgr {
public:
    static WndObj fromHwnd(HWND hWnd);
    static BOOL freeWindow(HWND hWnd);
    static BOOL insertWindow(HWND hWnd, _Window* pWnd);
    static BOOL enumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam);
};
#endif//_WINDOBJ_H_