#include "wndobj.h"
#include "log.h"
#define kLogTag "_Window"

_Window::_Window(size_t extraLen)
    : cRef(1)
    , nPainting(0)
    , cbWndExtra(extraLen)
    , nSizing(0)
    , crKey(CR_INVALID)
    , byAlpha(0xff)
    , flags(0)
    , msgRecusiveCount(0)
    , bDestroyed(FALSE)
    , bCaretVisible(FALSE)
    , showSbFlags(0)
    , hdc(0)
    , bmp(0)
    , iconSmall(0)
    , iconBig(0)
    , objOpaque(0)
    , htCode(HTNOWHERE)
    , visualId(0)
    , dropTarget(NULL)
    , dragData(NULL)
    , userdata(0)
    , cmap(0)
    , hIMC(NULL)
    , hSysMenu(0)
    , pPrivData(nullptr)
{
    invalid.hRgn = CreateRectRgn(0, 0, 0, 0);
    invalid.bErase = TRUE;
    hoverInfo.dwHoverTime = DEF_HOVER_DELAY;
    hoverInfo.dwFlags = 0;
    hoverInfo.uHoverState = HS_Leave;
    if (cbWndExtra > 0)
    {
        extra = (char *)calloc(cbWndExtra, 1);
    }
    else
    {
        extra = nullptr;
    }
}

_Window::~_Window()
{
    assert(cmap == 0);
    assert(hIMC == 0);
    assert(pPrivData==nullptr);
    if (dropTarget)
    {
        dropTarget->Release();
    }
    if (dragData)
    {
        dragData->Release();
    }
    if (hdc)
    {
        DeleteDC(hdc);
        hdc = nullptr;
    }
    if (bmp)
    {
        DeleteObject(bmp);
        bmp = nullptr;
    }
    if (invalid.hRgn)
    {
        DeleteObject(invalid.hRgn);
        invalid.hRgn = NULL;
    }
    if (hSysMenu)
    {
        DestroyMenu(hSysMenu);
        hSysMenu = 0;
    }
    if (extra)
        free(extra);
}

//---------------------------------------------------------------
WndObj::WndObj(const WndObj &src)
{
    wnd = src.wnd;
    if (wnd)
    {
        wnd->Lock();
    }
}

WndObj::WndObj(_Window *pWnd)
    : wnd(pWnd)
{
    if (wnd)
    {
        wnd->Lock();
    }
}

WndObj::~WndObj()
{
    if (wnd)
    {
        wnd->Unlock();
    }
}

void WndObj::operator=(const WndObj &src)
{
    if (wnd)
    {
        wnd->Unlock();
        wnd = nullptr;
    }
    wnd = src.wnd;
    if (wnd)
    {
        wnd->Lock();
    }
}

//---------------------------------------------------------
static std::map<HWND, _Window *> map_wnd;
static std::recursive_mutex mutex_wnd;

static _Window *get_win_ptr(HWND hWnd)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_wnd);
    auto it = map_wnd.find(hWnd);
    if (it == map_wnd.end())
        return nullptr;
    return it->second;
}

WndObj WndMgr::fromHwnd(HWND hWnd)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_wnd);
    _Window *wnd = get_win_ptr(hWnd);
    return WndObj(wnd);
}

BOOL WndMgr::freeWindow(HWND hWnd)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_wnd);
    auto it = map_wnd.find(hWnd);
    if (it == map_wnd.end())
        return FALSE;

    _Window *wndObj = it->second;
    map_wnd.erase(it);

    // delete wndObj and release resource of the window object
    SLOG_STMD() << "freeWindow:" << hWnd;
    wndObj->Release();
    return TRUE;
}

BOOL WndMgr::insertWindow(HWND hWnd, _Window *pWnd)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_wnd);
    SLOG_STMD() << "insertWindow:" << hWnd;
    auto res = map_wnd.insert(std::make_pair(hWnd, pWnd));
    return res.second;
}
