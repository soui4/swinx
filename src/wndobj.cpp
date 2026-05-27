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
    assert(pPrivData == nullptr);
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

//_Window *pWnd must be locked before call
WndObj::WndObj(_Window *pWnd)
    : wnd(pWnd)
{
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
static std::map<HWND, _Window *> s_wndMap;
static std::recursive_mutex s_wndMapMutex;

static _Window *get_win_ptr_and_lock(HWND hWnd)
{
    _Window *wnd = nullptr;
    {//重要：这里必须及时释放全局锁s_wndMapMutex防止死锁。调用wnd->AddRef防止wnd被释放掉。
        std::unique_lock<std::recursive_mutex> lock(s_wndMapMutex);
        auto it = s_wndMap.find(hWnd);
        if (it == s_wndMap.end())
            return nullptr;
        wnd = it->second;
        wnd->AddRef();
    }
    if(wnd){
        wnd->Lock();
        wnd->Release();
    }
    return wnd;
}

/**
 * @brief 获取窗口对象
 *
 * @param hWnd
 * @return WndObj
 * @note 窗口对象的引用计数会增加
 */
WndObj WndMgr::fromHwnd(HWND hWnd)
{
    _Window *wnd = get_win_ptr_and_lock(hWnd);
    return WndObj(wnd);
}

BOOL WndMgr::freeWindow(HWND hWnd)
{
    std::unique_lock<std::recursive_mutex> lock(s_wndMapMutex);
    auto it = s_wndMap.find(hWnd);
    if (it == s_wndMap.end())
        return FALSE;

    _Window *wndObj = it->second;
    s_wndMap.erase(it);

    // delete wndObj and release resource of the window object
    SLOG_STMD() << "freeWindow:" << hWnd;
    wndObj->Release();
    return TRUE;
}

BOOL WndMgr::insertWindow(HWND hWnd, _Window *pWnd)
{
    std::unique_lock<std::recursive_mutex> lock(s_wndMapMutex);
    SLOG_STMD() << "insertWindow:" << hWnd;
    auto res = s_wndMap.insert(std::make_pair(hWnd, pWnd));
    return res.second;
}
