#include <windows.h>
#include <wnd.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <assert.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include "class.h"
#include "SConnection.h"
#include "sdc.h"
#include "uimsg.h"
#include "hook.h"
#include "uimsg.h"
#include "sharedmem.h"
#include "tostring.hpp"
#include "wndobj.h"
#include "debug.h"
#ifdef __linux__
#include "SDragdrop.h"
#endif//__linux__
#include "synhandle.h"

#define kLogTag "wnd"

using namespace swinx;

enum
{
    kMapped = 1 << 0,
};

enum SbPart
{
    SB_RAIL = 100
};

enum
{
    TIMER_STARTAUTOSCROLL = -1000,
    TIMER_AUTOSCROLL,
};
enum
{
    SPAN_STARTAUTOSCROLL = 500,
    SPAN_AUTOSCROLL = 100,
};

static LONG_PTR get_win_data(const void *ptr, UINT size)
{
    if (size == sizeof(WORD))
    {
        WORD ret;
        memcpy(&ret, ptr, sizeof(ret));
        return ret;
    }
    else if (size == sizeof(DWORD))
    {
        DWORD ret;
        memcpy(&ret, ptr, sizeof(ret));
        return ret;
    }
    else
    {
        LONG_PTR ret;
        memcpy(&ret, ptr, sizeof(ret));
        return ret;
    }
}

/* helper for set_window_long */
static inline void set_win_data(void *ptr, LONG_PTR val, UINT size)
{
    if (size == sizeof(WORD))
    {
        WORD newval = val;
        memcpy(ptr, &newval, sizeof(newval));
    }
    else if (size == sizeof(DWORD))
    {
        DWORD newval = val;
        memcpy(ptr, &newval, sizeof(newval));
    }
    else
    {
        memcpy(ptr, &val, sizeof(val));
    }
}

static BOOL InitWndDC(HWND hwnd, int cx, int cy)
{
    assert(hwnd);
    WndObj wndObj = WndMgr::fromHwnd(hwnd);
    assert(wndObj);
    cairo_surface_t *surface = wndObj->mConnection->CreateWindowSurface(hwnd,wndObj->visualId,cx,cy);
    wndObj->bmp = InitGdiObj(OBJ_BITMAP, surface);
    wndObj->hdc = new _SDC(hwnd);
    SelectObject(wndObj->hdc, wndObj->bmp);
    return TRUE;
}

static BOOL GetScrollBarRect(HWND hWnd, UINT uSb, RECT *pRc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    memset(pRc, 0, sizeof(RECT));
    if (!(wndObj->showSbFlags & uSb))
        return FALSE;
    if (uSb != SB_VERT && uSb != SB_HORZ)
    {
        memset(pRc, 0, sizeof(RECT));
        return FALSE;
    }
    *pRc = wndObj->rc;
    OffsetRect(pRc, -pRc->left, -pRc->top);
    if (wndObj->dwStyle & WS_BORDER)
    {
        int cxEdge = GetSystemMetrics(SM_CXEDGE);
        int cyEdge = GetSystemMetrics(SM_CYEDGE);
        InflateRect(pRc, -cxEdge, -cyEdge);
    }
    int cxHscroll = GetSystemMetrics(SM_CXVSCROLL);
    int cyVscroll = GetSystemMetrics(SM_CYHSCROLL);
    if (uSb == SB_VERT)
    {
        if (wndObj->showSbFlags & SB_HORZ)
            pRc->bottom -= cxHscroll;
        pRc->left = pRc->right - cyVscroll;
    }
    else
    {
        if (wndObj->showSbFlags & SB_VERT)
            pRc->right -= cyVscroll;
        pRc->top = pRc->bottom - cxHscroll;
    }
    return pRc->left < pRc->right && pRc->top < pRc->bottom;
}

static RECT GetScrollBarPartRect(BOOL bVert, const SCROLLINFO *pSi, int iPart, LPCRECT rcAll)
{
    int cx = GetSystemMetrics(bVert ? SM_CXVSCROLL : SM_CYHSCROLL);
    int nThumbMin = GetSystemMetrics(bVert ? SM_CYMINTRACK : SM_CXMINTRACK);

    __int64 nTrackPos = pSi->nTrackPos;
    RECT rcRet = { 0, 0, cx, 0 };

    if (pSi->nPage == 0)
        return rcRet;
    int nMax = pSi->nMax;
    if (nMax < pSi->nMin + (int)pSi->nPage - 1)
        nMax = pSi->nMin + pSi->nPage - 1;

    if (nTrackPos == -1)
        nTrackPos = pSi->nPos;
    int nLength = bVert ? (rcAll->bottom - rcAll->top) : (rcAll->right - rcAll->left);
    if (nLength <= 0)
        return rcRet;

    int nArrowHei = cx;
    int nInterHei = nLength - 2 * nArrowHei;
    if (nInterHei < 0)
        nInterHei = 0;
    int nSlideHei = pSi->nPage * nInterHei / (nMax - pSi->nMin + 1);
    if (nMax == (int)(pSi->nMin + pSi->nPage - 1))
        nSlideHei = nInterHei;
    if (nSlideHei < nThumbMin)
        nSlideHei = nThumbMin;
    if (nInterHei < nThumbMin)
        nSlideHei = 0;
    int nEmptyHei = nInterHei - nSlideHei;
    if (nInterHei == 0)
        nArrowHei = nLength / 2;

    rcRet.bottom = nArrowHei;

    if (iPart == SB_RAIL)
    {
        rcRet.bottom = nLength;
        InflateRect(&rcRet, 0, -nArrowHei);
        goto end;
    }
    if (iPart == SB_LINEUP)
        goto end;
    rcRet.top = rcRet.bottom;
    if ((pSi->nMax - pSi->nMin - pSi->nPage + 1) == 0)
        rcRet.bottom += nEmptyHei / 2;
    else
        rcRet.bottom += (int)(nEmptyHei * nTrackPos / (pSi->nMax - pSi->nMin - pSi->nPage + 1));
    if (iPart == SB_PAGEUP)
        goto end;
    rcRet.top = rcRet.bottom;
    rcRet.bottom += nSlideHei;
    if (iPart == SB_THUMBTRACK)
        goto end;
    rcRet.top = rcRet.bottom;
    rcRet.bottom = nLength - nArrowHei;
    if (iPart == SB_PAGEDOWN)
        goto end;
    rcRet.top = rcRet.bottom;
    rcRet.bottom = nLength;
    if (iPart == SB_LINEDOWN)
        goto end;
end:
    if (!bVert)
    {
        rcRet.left = rcRet.top;
        rcRet.right = rcRet.bottom;
        rcRet.top = 0;
        rcRet.bottom = cx;
    }
    OffsetRect(&rcRet, rcAll->left, rcAll->top);
    return rcRet;
}

static int ScrollBarHitTest(BOOL bVert, const SCROLLINFO *pSi, LPCRECT rcAll, POINT pt)
{
    RECT rc;
    const int parts[] = { SB_THUMBTRACK, SB_LINEUP, SB_LINEDOWN, SB_PAGEUP, SB_PAGEDOWN };
    for (int i = 0, c = ARRAYSIZE(parts); i < c; i++)
    {
        RECT rc = GetScrollBarPartRect(bVert, pSi, parts[i], rcAll);
        if (PtInRect(&rc, pt))
            return parts[i];
    }
    return -1;
}

BOOL InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed)
        return FALSE;
    if (wndObj->nSizing > 0)
        return TRUE;
    RECT rcClient;
    if (!lpRect)
    {
        GetClientRect(hWnd, &rcClient);
        lpRect = &rcClient;
    }
    if (IsRectEmpty(lpRect))
        return FALSE;
    #ifdef __linux__
    HRGN rgn = CreateRectRgnIndirect(lpRect);
    CombineRgn(wndObj->invalid.hRgn, wndObj->invalid.hRgn, rgn, RGN_OR);
    DeleteObject(rgn);
    wndObj->invalid.bErase = wndObj->invalid.bErase || bErase;
    wndObj->mConnection->SendExposeEvent(hWnd,lpRect);
    #else
    RECT rcInvalid={0};
    GetRgnBox(wndObj->invalid.hRgn, &rcInvalid);
    if (IsRectEmpty(&rcInvalid))
    {
        rcInvalid = *lpRect;
    }
    else
    {
        UnionRect(&rcInvalid,&rcInvalid,lpRect);
    }

    HRGN rgn = CreateRectRgnIndirect(&rcInvalid);
    CombineRgn(wndObj->invalid.hRgn, wndObj->invalid.hRgn, rgn, RGN_OR);
    DeleteObject(rgn);
    wndObj->invalid.bErase = wndObj->invalid.bErase || bErase;
    wndObj->mConnection->SendExposeEvent(hWnd,&rcInvalid);
    #endif
    return TRUE;
}

/***********************************************************************
 *           WIN_CreateWindowEx
 *
 * Implementation of CreateWindowEx().
 */
static HWND WIN_CreateWindowEx(CREATESTRUCT *cs, LPCSTR className, HINSTANCE module)
{
    WNDCLASSEXA clsInfo = { 0 };
    ATOM clsAtom = GetClassInfoExA(module, className, &clsInfo);
    if (!clsAtom)
    {
        return FALSE;
    }
    _Window *pWnd = new _Window(clsInfo.cbWndExtra);
    pWnd->tid = (tid_t)pthread_self();
    pWnd->hdc = nullptr;

    SConnection *conn = SConnMgr::instance()->getConnection(pWnd->tid);
    HWND hParent = cs->hwndParent;
    BOOL isMsgWnd = hParent == HWND_MESSAGE;
    BOOL isTransparent = cs->dwExStyle & WS_EX_TRANSPARENT;
    cs->dwExStyle &= ~WS_EX_TRANSPARENT;
    if (isMsgWnd)
    {
        hParent = 0;
        cs->cx = cs->cy = 1;
    }
    pWnd->mConnection = conn;
    pWnd->state = WS_Normal;
    pWnd->dwStyle = cs->style & (~WS_VISIBLE); // remove visible

    if (pWnd->dwStyle & WS_CAPTION)
    {
        pWnd->dwStyle &= ~WS_BORDER; // remove border
    }
    RECT rcInit = { cs->x, cs->y, cs->x + (int)cs->cx, cs->y + (int)cs->cy };
    pWnd->dwExStyle = cs->dwExStyle;
    pWnd->hInstance = module;
    pWnd->clsAtom = clsAtom;
    pWnd->iconSmall = pWnd->iconBig = nullptr;
    pWnd->parent = hParent;
    pWnd->winproc = clsInfo.lpfnWndProc;
    pWnd->rc = rcInit;
    pWnd->showSbFlags |= (cs->style & WS_HSCROLL) ? SB_HORZ : 0;
    pWnd->showSbFlags |= (cs->style & WS_VSCROLL) ? SB_VERT : 0;
    pWnd->visualId = conn->GetVisualID(TRUE);
    int depth = XCB_COPY_FROM_PARENT;
    if ((pWnd->dwExStyle & WS_EX_COMPOSITED) && !(pWnd->dwStyle & WS_CHILD) && conn->IsScreenComposited())
    {
        pWnd->dwStyle &= ~WS_CAPTION;
        pWnd->visualId = conn->GetVisualID(FALSE);
        depth = 32;
    }
    else
    {
        pWnd->dwExStyle &= ~WS_EX_COMPOSITED;
    }

    HWND hWnd = conn->OnWindowCreate(pWnd, cs, depth);
    if (!hWnd)
    {
        free(pWnd);
        return 0;
    }
    WndMgr::insertWindow(hWnd, pWnd);

    SetWindowLongPtrA(hWnd, GWLP_ID, cs->hMenu);
    if (!(cs->style & WS_CHILD))
        SetParent(hWnd, cs->hwndParent);
    InitWndDC(hWnd, cs->cx, cs->cy);

    if (0 == SendMessage(hWnd, WM_NCCREATE, 0, (LPARAM)cs) || 0 != SendMessage(hWnd, WM_CREATE, 0, (LPARAM)cs))
    {
        conn->OnWindowDestroy(hWnd, pWnd);
        WndMgr::freeWindow(hWnd);
        return 0;
    }

    if (memcmp(&pWnd->rc, &rcInit, sizeof(RECT)) == 0)
    { // notify init size and pos
        SetWindowPos(hWnd, 0, cs->x, cs->y, cs->cx, cs->cy, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (clsInfo.hIconSm)
    {
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)clsInfo.hIconSm);
    }
    if (clsInfo.hIcon)
    {
        SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)clsInfo.hIconSm);
    }
    if (cs->dwExStyle & WS_EX_TOPMOST)
    {
        SetWindowPos(hWnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    }
    if (isTransparent)
    {
        conn->SetWindowMsgTransparent(hWnd, pWnd, TRUE);
    }
    if (!isMsgWnd && cs->style & WS_VISIBLE)
    {
        ShowWindow(hWnd, SW_SHOW);
        InvalidateRect(hWnd, NULL, TRUE);
    }

    return hWnd;
}

HWND WINAPI CreateWindowA(LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    return CreateWindowExA(0, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

HWND WINAPI CreateWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    return CreateWindowExW(0, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

/***********************************************************************
 *		CreateWindowExW (USER32.@)
 */
HWND WINAPI CreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data)
{
    std::string strClsName, strWndName;
    tostring(className, -1, strClsName);
    tostring(windowName, -1, strWndName);
    return CreateWindowExA(exStyle, strClsName.c_str(), strWndName.c_str(), style, x, y, width, height, parent, menu, instance, data);
}

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data)
{
    char szClassName[100];
    if (IS_INTRESOURCE(className))
    {
        if (!GetAtomName((ATOM)LOWORD(className), szClassName, 100))
            return 0;
        className = szClassName;
    }
    CREATESTRUCT cs;
    cs.lpCreateParams = data;
    cs.hInstance = instance;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.x = x;
    cs.y = y;
    cs.cx = width;
    cs.cy = height;
    cs.style = style;
    cs.lpszName = windowName;
    cs.lpszClass = className;
    cs.dwExStyle = exStyle;

    return WIN_CreateWindowEx(&cs, className, instance);
}

int GetClassNameA(HWND hWnd, LPSTR lpClassName, int nMaxCount)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->OnGetClassName(hWnd, lpClassName, nMaxCount);
}

int GetClassNameW(HWND hWnd, LPWSTR lpClassName, int nMaxCount)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return GetAtomNameW(wndObj->clsAtom, lpClassName, nMaxCount);
}

BOOL WINAPI DestroyWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        if (!wndObj->bDestroyed)
        {
            SendMessage(hWnd, WM_DESTROY, 0, 0);
        }
        return TRUE;
    }
    else
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        if (!conn->IsWindow(hWnd))
            return FALSE;
        PostMessage(hWnd, WM_DESTROY, 0, 0);
        return TRUE;
    }
}

BOOL WINAPI IsWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    return !wndObj->bDestroyed;
}

BOOL ClientToScreen(HWND hWnd, LPPOINT ppt)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    ppt->x += wndObj->rc.left;
    ppt->y += wndObj->rc.top;
    int cxEdge = GetSystemMetrics(SM_CXEDGE);
    int cyEdge = GetSystemMetrics(SM_CYEDGE);
    if (wndObj->dwStyle & WS_BORDER)
    {
        ppt->x += cxEdge;
        ppt->y += cyEdge;
    }
    if (!(wndObj->dwStyle & WS_CHILD))
        return TRUE;
    HWND hParent = wndObj->parent;
    while (hParent)
    {
        WndObj wndObj = WndMgr::fromHwnd(hParent);
        if (!wndObj) // todo:hjx
            break;
        ppt->x += wndObj->rc.left;
        ppt->y += wndObj->rc.top;
        if (wndObj->dwStyle & WS_BORDER)
        {
            ppt->x += cxEdge;
            ppt->y += cyEdge;
        }
        if (!(wndObj->dwStyle & WS_CHILD))
            break;
        hParent = wndObj->parent;
    }
    return TRUE;
}
BOOL ScreenToClient(HWND hWnd, LPPOINT ppt)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;

    ppt->x -= wndObj->rc.left;
    ppt->y -= wndObj->rc.top;
    int cxEdge = GetSystemMetrics(SM_CXEDGE);
    int cyEdge = GetSystemMetrics(SM_CYEDGE);
    if (wndObj->dwStyle & WS_BORDER)
    {
        ppt->x -= cxEdge;
        ppt->y -= cyEdge;
    }
    if (!(wndObj->dwStyle & WS_CHILD))
        return TRUE;

    HWND hParent = wndObj->parent;
    while (hParent)
    {
        WndObj wndObj = WndMgr::fromHwnd(hParent);
        if (!wndObj) // todo:hjx
            break;
        ppt->x -= wndObj->rc.left;
        ppt->y -= wndObj->rc.top;
        if (wndObj->dwStyle & WS_BORDER)
        {
            ppt->x -= cxEdge;
            ppt->y -= cyEdge;
        }
        if (!(wndObj->dwStyle & WS_CHILD))
            break;
        hParent = wndObj->parent;
    }
    return TRUE;
}

HWND SetCapture(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    HWND oldCapture = wndObj->mConnection->SetCapture(hWnd);
    SendMessage(hWnd, WM_CAPTURECHANGED, 0, hWnd);
    // SLOG_FMTD("SetCapture hWnd=%d",(int)hWnd);
    return oldCapture;
}

BOOL ReleaseCapture()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->ReleaseCapture();
}

HWND GetCapture()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetCapture();
}

static HRESULT HandleNcTestCode(HWND hWnd, UINT htCode)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return -1;
    if (!(htCode >= HTCAPTION && htCode <= HTBOTTOMRIGHT) || htCode == HTVSCROLL || htCode == HTHSCROLL)
        return -2;
    POINT ptClick;
    if (!wndObj->mConnection->GetCursorPos(&ptClick))
        return -1;
    SLOG_STMI() << "HandleNcTestCode,code=" << htCode;
    wndObj->mConnection->SetTimerBlock(true);
    RECT rcWnd = wndObj->rc;
    BOOL bQuit = FALSE;
    SetCapture(hWnd);
    SendMessageA(hWnd, WM_ENTERSIZEMOVE, 0, 0);
    for (; !bQuit;)
    {
        MSG msg;
        if (!WaitMessage())
            continue;
        while (PeekMessage(&msg, 0, 0, 0, TRUE))
        {
            if (CallMsgFilter(&msg, htCode == HTCAPTION ? MSGF_SIZE : MSGF_MOVE))
                continue;
            if(msg.message == WM_CANCELMODE){
                bQuit = TRUE;
                break;
            }
            if (msg.message == WM_QUIT)
            {
                SLOG_STMI() << "HandleNcTestCode,WM_QUIT";
                bQuit = TRUE;
                wndObj->mConnection->postMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
                break;
            }
            else if(msg.message == WM_CANCELMODE)
            {
                bQuit = TRUE;
                break;
            }
            else if (msg.message == WM_LBUTTONUP)
            {
                SLOG_STMI() << "HandleNcTestCode,WM_LBUTTONUP";
                bQuit = TRUE;
                break;
            }
            else if (msg.message == WM_MOUSEMOVE)
            {
                POINT ptNow;
                wndObj->mConnection->GetCursorPos(&ptNow);
                switch (htCode)
                {
                case HTCAPTION:
                { // move window
                    int32_t x = rcWnd.left + ptNow.x - ptClick.x;
                    int32_t y = rcWnd.top + ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                }
                break;
                case HTTOP:
                { //
                    RECT rcNow = rcWnd;
                    rcNow.top += ptNow.y - ptClick.y;
                    SLOG_STMI()<<"HandleNcTestCode SetWindowPos,cx="<<rcNow.right - rcNow.left<<" cy="<<rcNow.bottom - rcNow.top;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER);
                }
                break;
                case HTBOTTOM:
                { //
                    RECT rcNow = rcWnd;
                    rcNow.bottom += ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER | SWP_NOMOVE);
                }
                break;
                case HTLEFT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.left += ptNow.x - ptClick.x;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER);
                }
                break;
                case HTRIGHT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.right += ptNow.x - ptClick.x;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER | SWP_NOMOVE);
                }
                break;
                case HTTOPLEFT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.left += ptNow.x - ptClick.x;
                    rcNow.top += ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER);
                }
                break;
                case HTTOPRIGHT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.right += ptNow.x - ptClick.x;
                    rcNow.top += ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER);
                }
                break;
                case HTBOTTOMLEFT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.left += ptNow.x - ptClick.x;
                    rcNow.bottom += ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER);
                }
                break;
                case HTSIZE:
                case HTBOTTOMRIGHT:
                {
                    RECT rcNow = rcWnd;
                    rcNow.right += ptNow.x - ptClick.x;
                    rcNow.bottom += ptNow.y - ptClick.y;
                    SetWindowPos(hWnd, 0, rcNow.left, rcNow.top, rcNow.right - rcNow.left, rcNow.bottom - rcNow.top, SWP_NOZORDER | SWP_NOMOVE);
                }
                break;
                }
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    SendMessageA(hWnd, WM_EXITSIZEMOVE, 0, 0);
    ReleaseCapture();

    wndObj->mConnection->SetTimerBlock(false);
    SLOG_STMI() << "HandleNcTestCode,Quit";

    return 0;
}

static void UpdateWindowCursor(WndObj &wndObj, HWND hWnd, int htCode)
{
    if (htCode == HTCLIENT)
    {
        WNDCLASSEXA wc;
        GetClassInfoExA(wndObj->hInstance, MAKEINTRESOURCE(wndObj->clsAtom), &wc);
        if (wc.hCursor)
        {
            wndObj->mConnection->SetCursor(hWnd,wc.hCursor);
        }
    }
    else if (htCode > HTCAPTION && htCode < HTBORDER)
    {
        LPCSTR cursorId = nullptr;
        switch (htCode)
        {
        case HTLEFT:
        case HTRIGHT:
            cursorId = IDC_SIZEWE;
            break;
        case HTTOP:
        case HTBOTTOM:
            cursorId = IDC_SIZENS;
            break;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT:
            cursorId = IDC_SIZENWSE;
            break;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
            cursorId = IDC_SIZENESW;
            break;
        case HTSIZE:
            cursorId = IDC_SIZE;
            break;
        default:
            cursorId = IDC_ARROW;
            break;
        }
        //SLOG_STMI()<<"UpdateWindowCursor, hWnd="<<hWnd<<" cursor="<<(WORD)(ULONG_PTR)cursorId;
        wndObj->mConnection->SetCursor(hWnd,LoadCursor(wndObj->hInstance, cursorId));
    }
}

static BOOL ActiveWindow(HWND hWnd, BOOL bMouseActive, UINT msg, UINT htCode)
{
    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    if (hRoot)
        hWnd = hRoot;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;

    BOOL bRet = FALSE;
    if ((wndObj->dwExStyle & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) == 0 && (wndObj->dwStyle & WS_DISABLED) == 0)
    {
        UINT maRet = 0;
        HWND oldActive = wndObj->mConnection->GetActiveWnd();
        if (bMouseActive && oldActive != hWnd)
        {
            maRet = SendMessageA(hWnd, WM_MOUSEACTIVATE, (WPARAM)hWnd, MAKELPARAM(htCode, msg));
        }
        if (!bMouseActive || maRet == MA_ACTIVATE || maRet == MA_ACTIVATEANDEAT)
        {
            if (!wndObj->mConnection->SetActiveWindow(hWnd))
                return FALSE;
            SendMessageA(hWnd, WM_ACTIVATE, bMouseActive ? WA_CLICKACTIVE : WA_ACTIVE, oldActive);
            if (IsWindow(oldActive))
            {
                SendMessageA(oldActive, WM_ACTIVATE, WA_INACTIVE, hWnd);
            }
        }
        bRet = maRet == MA_ACTIVATEANDEAT || maRet == MA_NOACTIVATEANDEAT;
    }
    return bRet;
}

static void _InvalidCaret(HWND hWnd, WndObj &wndObj)
{
    const SConnection::CaretInfo *info = wndObj->mConnection->GetCaretInfo();
    assert(info->hOwner == hWnd);
    RECT rc = { info->x, info->y, info->x + info->nWidth, info->y + info->nHeight };
    InvalidateRect(hWnd, &rc, FALSE);
}

static void _DrawCaret(HWND hWnd, HDC hdc, WndObj &wndObj)
{
    const SConnection::CaretInfo *info = wndObj->mConnection->GetCaretInfo();
    assert(info->hOwner == hWnd);
    if (info->hBmp)
    {
        BITMAP bm;
        GetObjectA(info->hBmp, sizeof(bm), &bm);
        HDC memdc = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(memdc, info->hBmp);
        BitBlt(hdc, info->x, info->y, bm.bmWidth, bm.bmHeight, memdc, 0, 0, R2_NOT);
        SelectObject(memdc, old);
        DeleteDC(memdc);
    }
    else
    {
        RECT rc = { info->x, info->y, info->x + info->nWidth, info->y + info->nHeight };
        InvertRect(hdc, &rc);
    }
}

LRESULT CallWindowProc(WNDPROC proc, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return proc(hWnd, msg, wp, lp);
}

static int CALLBACK Enum4DestroyOwned(HWND hwnd, LPARAM lParam)
{
    HWND hTest = (HWND)lParam;
    if (GetParent(hwnd) == hTest)
    {
        DestroyWindow(hwnd);
    }
    return TRUE;
}

static LRESULT CallWindowObjProc(WndObj &wndObj, WNDPROC proc, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    assert(wndObj);
    if (wndObj->bDestroyed)
        return -1;
    LRESULT ret = 0;
    LONG cLock = wndObj->FreeLock();
    ret = proc(hWnd, msg, wp, lp);
    wndObj->RestoreLock(cLock);
    return ret;
}

static LRESULT CallWindowProcPriv(WNDPROC proc, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT ret = 0;
    BOOL bDestroyWnd = FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return -1;
    BOOL bSkipMsg = FALSE;
    switch (msg)
    {
    case UM_MAPNOTIFY:
        if (wp)
            wndObj->flags |= kMapped;
        else
            wndObj->flags &= ~kMapped;
        if (wp && wndObj->byAlpha != 0xff)
        {
            Sleep(50); // todo: fix it later.
            wndObj->mConnection->SetWindowOpacity(hWnd, wndObj->byAlpha);
        }
        return 0;
        #ifdef __linux__
    case UM_XDND_DRAG_ENTER:
    {
        SLOG_STMI() << "UM_XDND_DRAG_ENTER!";
        DragEnterData *pDragEnterData = (DragEnterData *)lp;
        assert(pDragEnterData->pData);
        if (!wndObj->dropTarget)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        if (wndObj->dragData)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        wndObj->dragData = pDragEnterData->pData;
        wndObj->dragData->AddRef();
        XDndDataObjectProxy *dragData = pDragEnterData->pData;
        DWORD grfKeyState = 0; // no key state is available here.
        wndObj->dropTarget->DragEnter(dragData, grfKeyState, pDragEnterData->pt, &dragData->m_dwEffect);
        return 0;
    }
    case UM_XDND_DRAG_LEAVE:
    {
        SLOG_STMI() << "UM_XDND_DRAG_LEAVE!";

        if (!wndObj->dropTarget)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        HRESULT hr = wndObj->dropTarget->DragLeave();
        if (wndObj->dragData)
        {
            wndObj->dragData->Release();
            wndObj->dragData = NULL;
            SLOG_STMI() << "UM_XDND_DRAG_LEAVE! set dragData to null";
        }
        return 0;
    }
    case UM_XDND_DRAG_OVER:
    {
        DragOverData *data = (DragOverData *)lp;
        if (!wndObj->dropTarget)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        XDndDataObjectProxy *dragData = (XDndDataObjectProxy *)wndObj->dragData;
        if (dragData)
        {
            dragData->m_dwKeyState = data->dwKeyState;
            dragData->m_ptOver = data->pt;
            HRESULT hr = wndObj->dropTarget->DragOver(dragData->m_dwKeyState, data->pt, &dragData->m_dwEffect);
            // SLOG_STMI()<<"UM_XDND_DRAG_OVER, hr="<<hr<<" effedt="<<dragData->m_dwEffect<<" accept="<<(hr==S_OK);
            wndObj->mConnection->SendXdndStatus(hWnd, dragData->getSource(), hr == S_OK, dragData->m_dwEffect);
        }
        else
        {
            SLOG_STME() << "!!!!dragData is nullptr";
        }
        return 0;
    }
    case UM_XDND_DRAG_DROP:
    {
        SLOG_STMI() << "UM_XDND_DRAG_DROP!";
        DragDropData *data = (DragDropData *)lp;
        if (!wndObj->dropTarget || !wndObj->dragData)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        XDndDataObjectProxy *dragData = (XDndDataObjectProxy *)wndObj->dragData;
        DWORD dwEffect = wp ? wp : dragData->m_dwEffect; // if wp is valid, using wp alse using dragover effect.
        HRESULT hr = wndObj->dropTarget->Drop(wndObj->dragData, dragData->m_dwKeyState, dragData->m_ptOver, &dwEffect);
        wndObj->mConnection->SendXdndFinish(hWnd, dragData->getSource(), hr == S_OK, dwEffect);
        if (wndObj->dragData)
        {
            wndObj->dragData->Release();
            wndObj->dragData = NULL;
        }
        SLOG_STMI() << "UM_XDND_DRAG_DROP, set dragData to null";
        return 0;
    }
    #endif//__linux__
    case WM_LBUTTONDOWN:
        if (bSkipMsg = (0 == HandleNcTestCode(hWnd, wndObj->htCode)))
            break;
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    {
        bSkipMsg = ActiveWindow(hWnd, TRUE, msg, wndObj->htCode);
        if (!bSkipMsg && GetCapture() != hWnd)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            if (!PtInRect(&rcClient, pt))
            {
                CallWindowObjProc(wndObj, proc, hWnd, msg + WM_NCLBUTTONDOWN - WM_LBUTTONDOWN, wp, lp);
                bSkipMsg = TRUE;
            }
        }
    }
    break;
    case WM_MOUSEMOVE:
    if(wp==0){
        POINT pt;
        wndObj->mConnection->GetCursorPos(&pt);
        int htCode = CallWindowObjProc(wndObj, proc,hWnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
        //SLOG_STMI()<<"hjx WM_MOUSEMOVE, pt2.x="<<pt.x<<", pt2.y="<<pt.y<<" htCode="<<htCode;
        if (CallWindowObjProc(wndObj, proc,hWnd, WM_SETCURSOR, hWnd, htCode) == 0)
        {
            UpdateWindowCursor(wndObj, hWnd, htCode);
        }
        if (htCode != wndObj->htCode)
        {
            int oldHtCode = wndObj->htCode;
            wndObj->htCode = htCode;
            if (htCode != HTCLIENT && oldHtCode == HTCLIENT)
            {
                CallWindowObjProc(wndObj, proc, hWnd, WM_NCMOUSEHOVER, htCode, MAKELPARAM(pt.x, pt.y));
                bSkipMsg = TRUE;
            }
            if (htCode == HTCLIENT && oldHtCode != HTCLIENT)
            {
                CallWindowObjProc(wndObj, proc, hWnd, WM_NCMOUSELEAVE, htCode, MAKELPARAM(pt.x, pt.y));
                bSkipMsg = TRUE;
            }
        }
        else if (htCode != HTCLIENT)
        {
            CallWindowObjProc(wndObj, proc, hWnd, WM_NCMOUSEMOVE, htCode, MAKELPARAM(pt.x, pt.y));
            bSkipMsg = TRUE;
        }
        if (!bSkipMsg && (wndObj->dwStyle & WS_DISABLED))
        {
            bSkipMsg = TRUE;
        }
    }
    break;
    case WM_MOUSEHOVER:
    {
        if (wndObj->hoverInfo.uHoverState != HS_Leave && (wndObj->hoverInfo.dwFlags & TME_HOVER))
        {
            wndObj->hoverInfo.uHoverState = HS_HoverDelay;
            DWORD delay = wndObj->hoverInfo.dwHoverTime;
            if (delay == HOVER_DEFAULT)
                delay = DEF_HOVER_DELAY;
            if (delay > 0)
            {
                SetTimer(hWnd, SConnection::TM_HOVERDELAY, delay, nullptr);
                bSkipMsg = TRUE;
            }
        }
        if (wndObj->hoverInfo.uHoverState == HS_HoverDelay)
        {
            wndObj->hoverInfo.uHoverState = HS_Hover;
        }
        else
        {
            bSkipMsg = TRUE;
        }
        break;
    }
    case WM_MOUSELEAVE:
    {
        if (wndObj->htCode != HTCLIENT)
        {
            POINT pt;
            wndObj->mConnection->GetCursorPos(&pt);
            CallWindowObjProc(wndObj, proc, hWnd, WM_NCMOUSELEAVE, HTNOWHERE, MAKELPARAM(pt.x, pt.y));
        }
        wndObj->htCode = HTNOWHERE;
        if (wndObj->hoverInfo.uHoverState != HS_Leave)//todo:hjx
        {
            wndObj->hoverInfo.uHoverState = HS_Leave;
            if (!(wndObj->hoverInfo.dwFlags & TME_LEAVE))
            {
                bSkipMsg = TRUE;
            }
        }

        break;
    }
    case WM_TIMER:
    {
        if (wp == SConnection::TM_HOVERDELAY)
        {
            KillTimer(hWnd, wp);
            POINT ptCursor;
            GetCursorPos(&ptCursor);
            RECT rcWnd;
            GetWindowRect(hWnd, &rcWnd);
            BOOL isHover = PtInRect(&rcWnd, ptCursor);
            if (isHover && wndObj->hoverInfo.uHoverState == HS_HoverDelay)
            {
                ScreenToClient(hWnd, &ptCursor);
                CallWindowObjProc(wndObj, proc, hWnd, WM_MOUSEHOVER, 0, MAKELPARAM(ptCursor.x, ptCursor.y)); // delay send mouse hover
            }
            bSkipMsg = TRUE;
        }
        if (wp == SConnection::TM_CARET && IsWindowVisible(hWnd))
        {
            _InvalidCaret(hWnd, wndObj);
            wndObj->bCaretVisible = !wndObj->bCaretVisible;
            bSkipMsg = TRUE;
        }
        if (wp == SConnection::TM_FLASH)
        {
            KillTimer(hWnd, wp);
            FLASHWINFO info = { sizeof(info), 0 };
            info.hwnd = hWnd;
            info.dwFlags = FLASHW_STOP;
            FlashWindowEx(&info);
            bSkipMsg = TRUE;
        }
        break;
    }
    case WM_PAINT:
    {
        HDC hdc = GetDC(hWnd);
        if (lp)
        {
            HGDIOBJ hrgn = (HGDIOBJ)lp;
            int cxEdge = GetSystemMetrics(SM_CXEDGE);
            int cyEdge = GetSystemMetrics(SM_CYEDGE);
            if (wndObj->dwStyle & WS_BORDER)
                OffsetRgn(hrgn, -cxEdge, -cxEdge);
            CombineRgn(wndObj->invalid.hRgn, wndObj->invalid.hRgn, hrgn, RGN_OR);
            if (wndObj->dwStyle & WS_BORDER)
                OffsetRgn(hrgn, cxEdge, cxEdge);
        }
        SelectClipRgn(hdc, wndObj->invalid.hRgn);
        wndObj->nPainting++;
        if (wndObj->invalid.bErase || lp != 0)
        {
            CallWindowObjProc(wndObj, proc, hWnd, WM_ERASEBKGND, (WPARAM)hdc, 0);
            wndObj->invalid.bErase = FALSE;
        }
        ReleaseDC(hWnd, hdc);
        break;
    }
    case UM_SETTINGS:
    {
        if(wp == SETTINGS_DPI){
            int oldDpi = HIWORD(lp);
            int newDpi = LOWORD(lp);
            float scale = newDpi*1.0f/oldDpi;
            RECT rc= wndObj->rc;
            POINT ptCenter = {(rc.left+rc.right)/2,(rc.top+rc.bottom)/2};
            int wid = rc.right-rc.left;
            int hei = rc.bottom -rc.top;
            ptCenter.x *= scale;
            ptCenter.y *= scale;
            wid *= scale;
            hei *= scale;
            rc.left = rc.right=ptCenter.x;
            rc.top = rc.bottom = ptCenter.y;
            InflateRect(&rc, wid/2, hei/2);
            SLOG_STMI()<<"WM_DPICHANGED, hWnd="<<hWnd<<" scale="<<scale<<" rc.left="<<rc.left<<" rc.top="<<rc.top<<" rc.right="<<rc.right<<" rc.bottom="<<rc.bottom;
            CallWindowProcPriv(proc, hWnd, WM_DPICHANGED, MAKELONG(newDpi, newDpi),(LPARAM)&rc);
        }
        return 1;
    }
    case UM_STATE:
        switch (wp)
        {
        case SIZE_MINIMIZED:
            wndObj->state = WS_Minimized;
            lp = 0;
            break;
        case SIZE_MAXIMIZED:
            wndObj->state = WS_Maximized;
            lp = MAKELPARAM(wndObj->rc.right - wndObj->rc.left, wndObj->rc.bottom - wndObj->rc.top);
            break;
        case SIZE_RESTORED:
            wndObj->state = WS_Normal;
            lp = MAKELPARAM(wndObj->rc.right - wndObj->rc.left, wndObj->rc.bottom - wndObj->rc.top);
            break;
        }
        CallWindowObjProc(wndObj, proc, hWnd, WM_SIZE, wp, lp); // call size again
        return 1;
    case WM_MOVE:
        OffsetRect(&wndObj->rc, GET_X_LPARAM(lp) - wndObj->rc.left, GET_Y_LPARAM(lp) - wndObj->rc.top);
        break;
    case WM_SIZE:
        wp = wndObj->state;
        SIZE sz = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (wndObj->bmp && (sz.cx != wndObj->rc.right - wndObj->rc.left || sz.cy != wndObj->rc.bottom - wndObj->rc.top))
        {
            wndObj->rc.right = wndObj->rc.left + sz.cx;
            wndObj->rc.bottom = wndObj->rc.top + sz.cy;
            cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
            surface = wndObj->mConnection->ResizeSurface(surface,hWnd,wndObj->visualId, sz.cx,sz.cy);
            SetGdiObjPtr(wndObj->bmp, surface);
            RECT rc = wndObj->rc;
            OffsetRect(&rc, -rc.left, -rc.top);
            InvalidateRect(hWnd, &rc, TRUE);
        }
        wndObj->nSizing++;
        break;
    }
    wndObj->msgRecusiveCount++;
    wndObj->mConnection->BeforeProcMsg(hWnd, msg, wp, lp);
    if (!bSkipMsg)
    {
        LPARAM lp2 = lp;
        if ((wndObj->dwStyle & WS_BORDER) && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL)
        { // convert pos to remove border size
            POINT pt = { GET_X_LPARAM(lp2), GET_Y_LPARAM(lp2) };
            pt.x -= GetSystemMetrics(SM_CXBORDER);
            pt.y -= GetSystemMetrics(SM_CYBORDER);
            lp2 = MAKELPARAM(pt.x, pt.y);
        }
        UINT htCode = wndObj->htCode;
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)
        { // call keyboard hook
            bSkipMsg = CallHook(WH_KEYBOARD, HC_ACTION, wp, lp);
        }
        else if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
        {
            MOUSEHOOKSTRUCT st;
            st.hwnd = hWnd;
            st.pt.x = GET_X_LPARAM(lp);
            st.pt.y = GET_Y_LPARAM(lp);
            st.wHitTestCode = htCode;
            bSkipMsg = CallHook(WH_MOUSE, HC_ACTION, msg, (LPARAM)&st);
        }
        {
            CWPSTRUCT st;
            st.hwnd = hWnd;
            st.message = msg;
            st.wParam = wp;
            st.lParam = lp;
            bSkipMsg = CallHook(WH_CALLWNDPROC, HC_ACTION, 0, (LPARAM)&st);
        }
        if (!bSkipMsg)
            ret = CallWindowObjProc(wndObj, proc, hWnd, msg, wp, lp2);
        {
            CWPRETSTRUCT st;
            st.hwnd = hWnd;
            st.message = msg;
            st.wParam = wp;
            st.lParam = lp;
            st.lResult = ret;
            CallHook(WH_CALLWNDPROCRET, HC_ACTION, 0, (LPARAM)&st);
        }
        if (msg == WM_PAINT)
        {
            if (wndObj->bCaretVisible)
            {
                _DrawCaret(hWnd, wndObj->hdc, wndObj);
            }
            if (lp)
                CallWindowObjProc(wndObj, proc, hWnd, WM_NCPAINT, lp, 0);            // call ncpaint
            SetRectRgn(wndObj->invalid.hRgn, 0, 0, 0, 0); // clear current region
        }
    }
    wndObj->mConnection->AfterProcMsg(hWnd, msg, wp, lp, ret);
    if (msg == WM_PAINT)
    {
        wndObj->nPainting--;
    }
    else if (msg == WM_SIZE)
    {
        wndObj->nSizing--;
    }
    else if (msg == WM_DESTROY)
    {
        // auto destroy all popup that owned by this
        EnumWindows(Enum4DestroyOwned, hWnd);
        // auto destory all children
        HWND hChild = GetWindow(hWnd, GW_CHILDLAST);
        while (hChild)
        {
            WndObj child = WndMgr::fromHwnd(hChild);
            if (child)
            {
                DestroyWindow(hChild);
            }
            else
            {
                // other process window, set it's parent to screen root
                wndObj->mConnection->SetParent(hChild, nullptr, 0);
            }
            hChild = GetWindow(hWnd, GW_CHILDLAST);
        }
        CallWindowObjProc(wndObj, proc, hWnd, WM_NCDESTROY, 0, 0);
        wndObj->bDestroyed = TRUE;
    }
    if (0 == --wndObj->msgRecusiveCount && wndObj->bDestroyed)
    {
        wndObj->mConnection->OnWindowDestroy(hWnd, wndObj.data());
        WndMgr::freeWindow(hWnd);
    }
    return ret;
}


SharedMemory *PostIpcMessage(SConnection *connCur, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, HANDLE &hEvt)
{
    if (!connCur->IsWindow(hWnd))
        return nullptr;
    suid_t uuid;
    IpcMsg::gen_suid(&uuid);
    std::string strName = IpcMsg::get_share_mem_name(uuid);
    //SLOG_STMI()<<"post ipc msg: share mem name="<<strName.c_str();
    SharedMemory *shareMem = new SharedMemory;

    int bufLen = 0;
    if (msg == WM_COPYDATA)
    {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lp;
        bufLen = sizeof(MsgLayout) + sizeof(DWORD) + cds->cbData;
        if (!shareMem->init(strName.c_str(), bufLen))
        {
            delete shareMem;
            return nullptr;
        }
        MsgLayout *ptr = (MsgLayout *)shareMem->buffer();
        ptr->ret = 0;
        ptr->wp = wp;
        ptr->lp = cds->dwData;
        DWORD *buf = (DWORD *)(ptr + 1);
        *buf = cds->cbData;
        if (cds->cbData > 0)
        {
            memcpy(buf + 1, cds->lpData, cds->cbData);
        }
    }
    else
    {
        bufLen = sizeof(MsgLayout);
        if (!shareMem->init(strName.c_str(), bufLen))
        {
            delete shareMem;
            return nullptr;
        }
        MsgLayout *ptr = (MsgLayout *)shareMem->buffer();
        ptr->ret = 0;
        ptr->wp = wp;
        ptr->lp = lp;
    }

    std::string evtName = IpcMsg::get_ipc_event_name(uuid);
    //SLOG_STMI()<<"post ipc msg: event name="<<evtName.c_str();

    hEvt = CreateEventA(nullptr, FALSE, FALSE, evtName.c_str());

    uint32_t data[5];
    data[0] = msg;
    memcpy(data + 1, uuid, sizeof(suid_t));
    connCur->SendClientMessage(hWnd, connCur->GetIpcAtom(), data, 5);
    return shareMem;
}

#define kMaxSendMsgStack 20
thread_local static int s_nCallStack = 0;
class CallStackCount {
  public:
    CallStackCount()
    {
        s_nCallStack++;
    }
    ~CallStackCount()
    {
        s_nCallStack--;
    }
};

static int CALLBACK CbEnumPopupWindow(HWND hwnd, LPARAM lParam){
    std::list<HWND> *lstPopups = (std::list<HWND> *)lParam;
    lstPopups->push_back(hwnd);
    return 1;
}

static LRESULT _SendMessageTimeout(BOOL bWideChar, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
    if(hWnd == HWND_BROADCAST){
        //send message to all popup window owned by this process.
        if(uTimeout == INFINITE || uTimeout>500)
            uTimeout=500;
        std::list<HWND> lstPopups;
        EnumWindows(CbEnumPopupWindow, (LPARAM)&lstPopups);
        for(auto it:lstPopups){
            _SendMessageTimeout(bWideChar,it,msg,wp,lp,fuFlags,uTimeout,NULL);
        }
        return 0;
    }
    CallStackCount stackCount;
    if (s_nCallStack > kMaxSendMsgStack)
        return -2;
    SConnection *connCur = SConnMgr::instance()->getConnection();
    WndObj pWnd = WndMgr::fromHwnd(hWnd);
    if (!pWnd)
    { // not the same process. send ipc message
        if (bWideChar)
        {
            SLOG_STME() << "ipc msg not support wide char api! msg=" << msg;
            return 0;
        }
        HANDLE hEvt = INVALID_HANDLE_VALUE;
        SharedMemory *shareMem = PostIpcMessage(connCur, hWnd, msg, wp, lp, hEvt);

        if (!shareMem)
        {
            return 0;
        }
        if (uTimeout == INFINITE)
            uTimeout = 1000;
        _SynHandle *handle = GetSynHandle(hEvt);
        // SLOG_STMI() << "ipc event name=" << handle->getName();
        int ret = WAIT_FAILED;
        if (fuFlags & SMTO_BLOCK)
        {
            ret = WaitForSingleObject(hEvt, uTimeout);
        }
        else
        {
            MSG msg2;
            for (;;)
            {
                ret = connCur->waitMutliObjectAndMsg(&hEvt, 1, uTimeout, FALSE, QS_ALLINPUT);
                if (ret == WAIT_OBJECT_0 + 1)
                {
                    if (PeekMessage(&msg2, 0, 0, 0, PM_REMOVE))
                    {
                        if (msg2.message == WM_QUIT)
                        {
                            connCur->postMsg(msg2.hwnd, msg2.message, msg2.wParam, msg2.lParam);
                            break;
                        }
                        TranslateMessage(&msg2);
                        DispatchMessage(&msg2);
                    }
                }
                else
                {
                    break;
                }
            }
        }
        if (ret == WAIT_OBJECT_0)
        {
            _SynHandle *handle2 = GetSynHandle(hEvt);
            SLOG_STMI() << "ipc return event name=" << handle2->getName();

            MsgLayout *ptr = (MsgLayout *)shareMem->buffer();
            SLOG_STMW() << "PostIpcMessage ret 0, result:" << ptr->ret << " timeout:" << uTimeout;
            if (lpdwResult)
                *lpdwResult = ptr->ret;
        }
        else
        {
            SLOG_STMW() << "PostIpcMessage ret:" << ret << " timeout:" << uTimeout;
        }

        delete shareMem;
        CloseHandle(hEvt);
        return ret == WAIT_OBJECT_0;
    }
    if(pWnd->bDestroyed)
        return 0;
    SConnection *connWnd = SConnMgr::instance()->getConnection(pWnd->tid);
    if (!connWnd)
        return 0;
    if (connWnd == connCur)
    {
        // same thread,call wndproc directly.
        WNDPROC wndProc = (WNDPROC)GetWindowLongPtrA(hWnd, GWL_WNDPROC);
        assert(wndProc);
        if (bWideChar)
        {
            MSG msgWrap;
            msgWrap.message = msg;
            msgWrap.wParam = wp;
            msgWrap.lParam = lp;
            *lpdwResult = CallWindowProcPriv(wndProc, hWnd, WM_MSG_W2A, 0, (LPARAM)&msgWrap);
        }
        else
        {
            *lpdwResult = CallWindowProcPriv(wndProc, hWnd, msg, wp, lp);
        }
        return 1;
    }
    else
    {
        int ret = 0;
        HANDLE hEvt = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        SendReply *reply = new SendReply(hEvt);
        connWnd->postMsg2(bWideChar, hWnd, msg, wp, lp, reply);
        if (fuFlags & SMTO_BLOCK)
        {
            ret = WaitForSingleObject(hEvt, uTimeout);
        }
        else
        {
            MSG msg;
            for (;;)
            {
                ret = connCur->waitMutliObjectAndMsg(&hEvt, 1, uTimeout, FALSE, QS_ALLINPUT);
                if (ret == WAIT_OBJECT_0 + 1)
                {
                    connCur->getMsg(&msg, hWnd, 0, 0);
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
                else
                {
                    break;
                }
            }
        }

        *lpdwResult = reply->ret;
        reply->Release();
        CloseHandle(hEvt);
        return ret == WAIT_OBJECT_0;
    }
}

LRESULT SendMessageTimeoutW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
    return _SendMessageTimeout(TRUE, hWnd, msg, wp, lp, fuFlags, uTimeout, lpdwResult);
}

LRESULT SendMessageTimeoutA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
    return _SendMessageTimeout(FALSE, hWnd, msg, wp, lp, fuFlags, uTimeout, lpdwResult);
}

BOOL SendNotifyMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

BOOL SendNotifyMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

#define DEF_SENDMSG_TIMEOUT 5000
LRESULT SendMessageA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DWORD_PTR ret = 0;
    SendMessageTimeoutA(hWnd, msg, wp, lp, 0, DEF_SENDMSG_TIMEOUT, &ret);
    return ret;
}

LRESULT SendMessageW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DWORD_PTR ret = 0;
    SendMessageTimeoutW(hWnd, msg, wp, lp, 0, DEF_SENDMSG_TIMEOUT, &ret);
    return ret;
}

BOOL PostMessageW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return PostMessageA(hWnd, msg, wp, lp);
}

BOOL PostMessageA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WndObj pWnd = WndMgr::fromHwnd(hWnd);
    if (!pWnd)
    { // post ipc message
        HANDLE hEvt = INVALID_HANDLE_VALUE;
        auto conn = SConnMgr::instance()->getConnection();
        SharedMemory *shareMem = PostIpcMessage(conn, hWnd, msg, wp, lp, hEvt);
        if (!shareMem)
        {
            return FALSE;
        }
        shareMem->detach();
        CloseHandle(hEvt);
        delete shareMem;
        return TRUE;
    }
    auto conn = SConnMgr::instance()->getConnection(pWnd->tid);
    conn->postMsg(hWnd, msg, wp, lp);
    return TRUE;
}

void PostQuitMessage(int nExitCode)
{
    PostThreadMessage(GetCurrentThreadId(), WM_QUIT, nExitCode, 0);
}

static BOOL _SendMessageCallback(BOOL bWideChar, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData)
{
    SConnection *connCur = SConnMgr::instance()->getConnection(GetCurrentThreadId());
    if (!connCur) // current thread must be a ui thread.
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
    {
        if (bWideChar)
        {
            TRACE("ipc msg not support widechar, msg=%u\n", Msg);
            return FALSE;
        }
        // impl ipc callback
        HANDLE hEvt = INVALID_HANDLE_VALUE;
        SharedMemory *shareMem = PostIpcMessage(connCur, hWnd, Msg, wParam, lParam, hEvt);
        if (!shareMem)
        {
            return -1;
        }
        CallbackTaskIpc *task = new CallbackTaskIpc(hEvt, shareMem);
        task->hwnd = hWnd;
        task->uMsg = Msg;
        task->dwData = dwData;
        task->asyncProc = lpCallBack;
        connCur->postCallbackTask(task);
        CloseHandle(hEvt);
        return TRUE;
    }
    else
    {
        HANDLE hEvt = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        SConnection *connDst = wndObj->mConnection;
        CallbackTask *task = new CallbackTask(hEvt);
        task->hwnd = hWnd;
        task->uMsg = Msg;
        task->dwData = dwData;
        task->asyncProc = lpCallBack;
        task->lResult = 0;
        connCur->postCallbackTask(task);
        CallbackReply *reply = new CallbackReply(hEvt, &task->lResult);
        task->Release();
        connDst->postMsg2(bWideChar, hWnd, Msg, wParam, lParam, reply);
        CloseHandle(hEvt);
        return TRUE;
    }
}

BOOL SendMessageCallbackW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData)
{
    return _SendMessageCallback(TRUE, hWnd, Msg, wParam, lParam, lpCallBack, dwData);
}

BOOL SendMessageCallbackA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData)
{
    return _SendMessageCallback(FALSE, hWnd, Msg, wParam, lParam, lpCallBack, dwData);
}

tid_t GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    SConnection *pConn = wndObj->mConnection;
    if (lpdwProcessId)
    {
        *lpdwProcessId = pConn->GetWndProcessId(hWnd);
    }
    return wndObj->tid;
}

BOOL SetForegroundWindow(HWND hWnd)
{
    SConnection *connCur = SConnMgr::instance()->getConnection();
    connCur->SetForegroundWindow(hWnd);
    return TRUE;
}

HWND GetForegroundWindow()
{
    SConnection *connCur = SConnMgr::instance()->getConnection();
    return connCur->GetForegroundWindow();
}

BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT uFlags)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        if (!conn)
            return FALSE;
        return conn->MoveWindow(hWnd, x, y, cx, cy);
    }
    WINDOWPOS wndPos;
    wndPos.hwnd = hWnd;
    wndPos.hwndInsertAfter = hWndInsertAfter;
    if (uFlags & SWP_NOMOVE)
    {
        x = wndObj->rc.left;
        y = wndObj->rc.top;
    }
    if (uFlags & SWP_NOSIZE)
    {
        cx = wndObj->rc.right - wndObj->rc.left;
        cy = wndObj->rc.bottom - wndObj->rc.top;
    }
    wndPos.x = x;
    wndPos.y = y;
    wndPos.cx = cx;
    wndPos.cy = cy;
    wndPos.flags = uFlags;
    SendMessage(hWnd, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wndPos);
    SendMessage(hWnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wndPos);
    return TRUE;
}

static void onStyleChange(HWND hWnd, WndObj &wndObj, DWORD newStyle)
{
    wndObj->dwStyle = newStyle;
    // update scrollbar flags.
    DWORD sbflag = 0;
    if (newStyle & WS_VSCROLL)
        sbflag |= SB_VERT;
    if (newStyle & WS_HSCROLL)
        sbflag |= SB_HORZ;
    if (wndObj->showSbFlags != sbflag)
    {
        wndObj->showSbFlags = sbflag;
        InvalidateRect(hWnd, &wndObj->rc, TRUE);
    }
}

static void onExStyleChange(HWND hWnd, WndObj &wndObj, DWORD newExStyle)
{
    wndObj->dwExStyle = newExStyle;
    if (newExStyle & WS_EX_TOPMOST)
    {
        wndObj->mConnection->SetZOrder(hWnd, wndObj.data(), HWND_TOPMOST);
    }else{
        wndObj->mConnection->SetZOrder(hWnd, wndObj.data(), HWND_NOTOPMOST);//todo:hjx
    }
}

static LONG_PTR GetWindowLongSize(HWND hWnd, int nIndex, uint32_t size)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    switch (nIndex)
    {
    case GWL_STYLE:
        return wndObj->dwStyle;
    case GWL_EXSTYLE:
        return wndObj->dwExStyle;
    case GWL_HINSTANCE:
        return (LONG_PTR)wndObj->hInstance;
    case GWL_HWNDPARENT:
        return wndObj->parent;
    case GWL_ID:
        return wndObj->wIDmenu;
    case GWL_USERDATA:
        return wndObj->userdata;
    case GWL_WNDPROC:
        return (LONG_PTR)wndObj->winproc;
    case GWLP_OPAQUE:
        return wndObj->objOpaque;
    default:
        if (nIndex >= 0 && (nIndex + size) <= wndObj->cbWndExtra)
        {
            return get_win_data((char *)wndObj->extra + nIndex, size);
        }
        else
        {
            TRACE("GetWindowLong Error, index =%d\n", nIndex);
        }
    }
    return 0;
}

static LONG_PTR SetWindowLongSize(HWND hWnd, int nIndex, LONG_PTR data, uint32_t size)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    LONG_PTR retval = GetWindowLongSize(hWnd, nIndex, size);
    switch (nIndex)
    {
    case GWL_STYLE:
        onStyleChange(hWnd, wndObj, data);
        break;
    case GWL_EXSTYLE:
        onExStyleChange(hWnd, wndObj, data);
        break;
    case GWL_HINSTANCE:
        wndObj->hInstance = (HINSTANCE)data;
        break;
    case GWL_HWNDPARENT:
        wndObj->parent = data;
        break;
    case GWL_ID:
        wndObj->wIDmenu = data;
        break;
    case GWL_USERDATA:
        wndObj->userdata = data;
        break;
    case GWL_WNDPROC:
        wndObj->winproc = (WNDPROC)data;
        break;
    case GWLP_OPAQUE:
        wndObj->objOpaque = data;
        break;
    default:
        if (nIndex >= 0 && (nIndex + size) <= wndObj->cbWndExtra)
        {
            set_win_data(wndObj->extra + nIndex, data, size);
        }
        else
        {
            TRACE("SetWindowLongA Error, index =%d\n", nIndex);
        }
    }
    return retval;
}

LONG GetWindowLongA(HWND hWnd, int nIndex)
{
    return (LONG)GetWindowLongSize(hWnd, nIndex, sizeof(LONG));
}

LONG GetWindowLongW(HWND hWnd, int nIndex)
{
    return (LONG)GetWindowLongSize(hWnd, nIndex, sizeof(LONG));
}

LONG_PTR GetWindowLongPtrA(HWND hWnd, int nIndex)
{
    return GetWindowLongSize(hWnd, nIndex, sizeof(LONG_PTR));
}

LONG_PTR GetWindowLongPtrW(HWND hWnd, int nIndex)
{
    return GetWindowLongSize(hWnd, nIndex, sizeof(LONG_PTR));
}

LONG SetWindowLongA(HWND hWnd, int nIndex, LONG data)
{
    return SetWindowLongSize(hWnd, nIndex, data, sizeof(LONG));
}
LONG SetWindowLongW(HWND hWnd, int nIndex, LONG data)
{
    return SetWindowLongSize(hWnd, nIndex, data, sizeof(LONG));
}

LONG_PTR SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR data)
{
    return SetWindowLongSize(hWnd, nIndex, data, sizeof(LONG_PTR));
}

LONG_PTR SetWindowLongPtrW(HWND hWnd, int nIndex, LONG_PTR data)
{
    return SetWindowLongSize(hWnd, nIndex, data, sizeof(LONG_PTR));
}

extern ULONG_PTR GetClassLongSize(ATOM atom, int nIndex, int sz);
extern ULONG_PTR SetClassLongSize(ATOM atom, int nIndex, ULONG_PTR data, int sz);

DWORD WINAPI GetClassLongA(_In_ HWND hWnd, _In_ int nIndex)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    ATOM atom = wndObj->clsAtom;
    return GetClassLongSize(atom, nIndex, sizeof(DWORD));
}

DWORD
WINAPI
GetClassLongW(_In_ HWND hWnd, _In_ int nIndex)
{
    return GetClassLongA(hWnd, nIndex);
}

DWORD
WINAPI
SetClassLongA(_In_ HWND hWnd, _In_ int nIndex, _In_ LONG dwNewLong)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    ATOM atom = wndObj->clsAtom;
    return SetClassLongSize(atom, nIndex, dwNewLong, sizeof(DWORD));
}

DWORD
WINAPI
SetClassLongW(_In_ HWND hWnd, _In_ int nIndex, _In_ LONG dwNewLong)
{
    return SetClassLongA(hWnd, nIndex, dwNewLong);
}

ULONG_PTR
WINAPI
GetClassLongPtrA(_In_ HWND hWnd, _In_ int nIndex)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    ATOM atom = wndObj->clsAtom;
    return GetClassLongSize(atom, nIndex, sizeof(ULONG_PTR));
}

ULONG_PTR
WINAPI
GetClassLongPtrW(_In_ HWND hWnd, _In_ int nIndex)
{
    return GetClassLongPtrA(hWnd, nIndex);
}

ULONG_PTR
WINAPI
SetClassLongPtrA(_In_ HWND hWnd, _In_ int nIndex, _In_ LONG_PTR dwNewLong)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    ATOM atom = wndObj->clsAtom;
    return SetClassLongSize(atom, nIndex, dwNewLong, sizeof(ULONG_PTR));
}

ULONG_PTR
WINAPI
SetClassLongPtrW(_In_ HWND hWnd, _In_ int nIndex, _In_ LONG_PTR dwNewLong)
{
    return SetClassLongPtrA(hWnd, nIndex, dwNewLong);
}

BOOL CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (hWnd)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
            return FALSE;
        if (wndObj->mConnection != pConn)
            return FALSE;
    }
    return pConn->CreateCaret(hWnd, hBitmap, nWidth, nHeight);
}

BOOL DestroyCaret(VOID)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    const SConnection::CaretInfo *caretInfo = pConn->GetCaretInfo();
    if (caretInfo->hOwner)
    {
        WndObj wndObj = WndMgr::fromHwnd(caretInfo->hOwner);
        if (wndObj && wndObj->bCaretVisible)
        {
            // clear caret
            wndObj->bCaretVisible = FALSE;
            _InvalidCaret(caretInfo->hOwner, wndObj);
        }
    }
    return pConn->DestroyCaret();
}

BOOL HideCaret(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (wndObj->mConnection != pConn)
        return FALSE;
    const SConnection::CaretInfo *info = pConn->GetCaretInfo();
    if (!pConn->HideCaret(hWnd))
        return FALSE;
    if (info->nVisible == 0)
    {
        if (wndObj->bCaretVisible)
        {
            // clear old caret
            wndObj->bCaretVisible = FALSE;
            _InvalidCaret(hWnd, wndObj);
        }
    }
    return TRUE;
}

BOOL ShowCaret(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    SConnection *pConn = SConnMgr::instance()->getConnection();
    if (wndObj->mConnection != pConn)
        return FALSE;
    if (!pConn->ShowCaret(hWnd))
        return FALSE;
    if (pConn->GetCaretInfo()->nVisible == 1)
    {
        // auto show caret
        wndObj->bCaretVisible = TRUE;
        _InvalidCaret(hWnd, wndObj);
    }
    return TRUE;
}

BOOL SetCaretPos(int X, int Y)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    const SConnection::CaretInfo *caretInfo = pConn->GetCaretInfo();
    WndObj wndObj = WndMgr::fromHwnd(caretInfo->hOwner);
    if (wndObj && wndObj->bCaretVisible)
    {
        // clear caret
        _InvalidCaret(caretInfo->hOwner, wndObj);
    }
    BOOL ret = pConn->SetCaretPos(X, Y);
    if(ret){
        WndObj wndObj = WndMgr::fromHwnd(caretInfo->hOwner);
        if(wndObj && wndObj->bCaretVisible)
            _InvalidCaret(caretInfo->hOwner, wndObj);
    }
    return ret;
}

BOOL GetCaretPos(LPPOINT lpPoint)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->GetCaretPos(lpPoint);
}

void SetCaretBlinkTime(UINT blinkTime)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    pConn->SetCaretBlinkTime(blinkTime);
}

UINT GetCaretBlinkTime()
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->GetCaretBlinkTime();
}

HWND GetActiveWindow()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetActiveWnd();
}

HWND GetDesktopWindow()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetScreenWindow();
}

BOOL IsWindowEnabled(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    return !(wndObj->dwStyle & WS_DISABLED);
}

BOOL EnableWindow(HWND hWnd, BOOL bEnable)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;

    if (bEnable)
        wndObj->dwStyle &= ~WS_DISABLED;
    else
        wndObj->dwStyle |= WS_DISABLED;
    wndObj->mConnection->EnableWindow(hWnd, bEnable);
    if (!bEnable)
    {
        // restore cursor to default cursor.
        WNDCLASSEXA clsInfo = { 0 };
        GetClassInfoExA(wndObj->hInstance, MAKEINTRESOURCE(wndObj->clsAtom), &clsInfo);
        if (clsInfo.hCursor)
        {
            wndObj->mConnection->SetCursor(hWnd, clsInfo.hCursor);
        }
    }
    return TRUE;
}

HWND SetActiveWindow(HWND hWnd)
{
    HWND hRet = GetActiveWindow();
    ActiveWindow(hWnd, FALSE, 0, 0);
    return hRet;
}

HWND GetParent(HWND hWnd)
{
    return (HWND)GetWindowLongPtrA(hWnd, GWLP_HWNDPARENT);
}

HWND GetAncestor(HWND hwnd, UINT gaFlags)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->OnGetAncestor(hwnd, gaFlags);
}

HWND SetParent(HWND hWnd, HWND hParent)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    wndObj->mConnection->SetParent(hWnd, wndObj.data(), hParent);
    return (HWND)SetWindowLongPtrA(hWnd, GWLP_HWNDPARENT, hParent);
}

BOOL GetCursorPos(LPPOINT ppt)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetCursorPos(ppt);
}

HWND WindowFromPoint(POINT pt)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->WindowFromPoint(pt);
}

UINT_PTR SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc)
{
    if (hWnd)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
            return 0;
        return wndObj->mConnection->SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
    }
    else
    {
        if (!lpTimerFunc)
            return 0;
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->SetTimer(hWnd, nIDEvent, uElapse, lpTimerFunc);
    }
}

BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent)
{
    if (hWnd)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
            return FALSE;
        return wndObj->mConnection->KillTimer(hWnd, uIDEvent);
    }
    else
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->KillTimer(hWnd, uIDEvent);
    }
}

HWND GetFocus()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetFocus();
}

HWND SetFocus(HWND hWnd)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    HWND oldFocus = conn->GetFocus();
    conn->SetFocus(hWnd);
    return oldFocus;
}

HDC BeginPaint(HWND hWnd, PAINTSTRUCT *ps)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    wndObj->Lock();
    ps->hdc = GetDC(hWnd);
    GetClipBox(ps->hdc, &ps->rcPaint);
    return ps->hdc;
}

BOOL EndPaint(HWND hWnd, const PAINTSTRUCT *ps)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    wndObj->Unlock();
    return ReleaseDC(hWnd, ps->hdc);
}

int GetUpdateRgn(HWND hWnd,  // handle to window
                 HRGN hRgn,  // handle to region
                 BOOL bErase // erase state
)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return ERROR;
    return CombineRgn(hRgn, wndObj->invalid.hRgn, nullptr, RGN_COPY);
}

BOOL UpdateWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed)
        return FALSE;
    if (wndObj->nPainting)
        return FALSE;
    RECT rcBox;
    GetRgnBox(wndObj->invalid.hRgn, &rcBox);
    if (IsRectEmpty(&rcBox))
        return FALSE;
    wndObj->mConnection->updateWindow(hWnd,rcBox);
    return TRUE;
}

BOOL GetClientRect(HWND hWnd, RECT *pRc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
    {
        // hWnd is owned by other process.
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->GetClientRect(hWnd, pRc);
    }
    *pRc = wndObj->rc;
    OffsetRect(pRc, -pRc->left, -pRc->top);
    if (wndObj->dwStyle & WS_BORDER)
    {
        int cxEdge = GetSystemMetrics(SM_CXEDGE);
        int cyEdge = GetSystemMetrics(SM_CYEDGE);
        pRc->right -= 2 * cxEdge;
        pRc->bottom -= 2 * cyEdge;
    }
    if (wndObj->showSbFlags & SB_VERT)
        pRc->right -= GetSystemMetrics(SM_CXVSCROLL);
    if (wndObj->showSbFlags & SB_HORZ)
        pRc->bottom -= GetSystemMetrics(SM_CYHSCROLL);
    if (pRc->right < 0)
        pRc->right = 0;
    if (pRc->bottom < 0)
        pRc->bottom = 0;
    return TRUE;
}

static BOOL _GetWndRect(HWND hWnd, RECT *rc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        *rc = wndObj->rc;
        return TRUE;
    }    
    else
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->GetClientRect(hWnd, rc);
    }
}

BOOL GetWindowRect(HWND hWnd, RECT *rc)
{
    if (!_GetWndRect(hWnd, rc))
        return FALSE;

    for (;;)
    {
        if (!(GetWindowLongA(hWnd, GWL_STYLE) & WS_CHILD))
            break;
        hWnd = GetParent(hWnd);
        if (!hWnd)
            break;
        RECT rcParent;
        if (!_GetWndRect(hWnd, &rcParent))
            break;
        OffsetRect(rc, rcParent.left, rcParent.top);
    }
    return TRUE;
}

static int GetScrollBarPartState(const ScrollBar *sb, int iPart)
{
    int nState = sb->nState;
    if (iPart == sb->iHitTest)
    {
        nState = sb->bDraging ? BuiltinImage::St_Push : BuiltinImage::St_Hover;
    }
    return nState;
}

static BYTE GetScrollBarPartAlpha(const ScrollBar *sb, int iPart)
{
    BYTE byApha = 0xff;
    return byApha; // todo:hjx
}

static void OnNcPaint(HWND hWnd, WPARAM wp, LPARAM lp)
{
    // draw scrollbar and border
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    HRGN hrgn = (HRGN)wp;
    RECT rcWnd = wndObj->rc;
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top);
    int cxEdge = GetSystemMetrics(SM_CXBORDER);
    int cyEdge = GetSystemMetrics(SM_CYBORDER);
    if (wndObj->dwStyle & WS_BORDER)
    {
        InflateRect(&rcWnd, -cxEdge, -cyEdge);
    }
    if ((int)wp <= 1)
    {
        // redraw all nonclient area
        hrgn = CreateRectRgnIndirect(&rcWnd);
    }
    {
        HDC hdc = GetDCEx(hWnd, hrgn, DCX_WINDOW | DCX_INTERSECTRGN);
        if (wndObj->dwStyle & WS_BORDER)
        {
            int nSave = SaveDC(hdc);
            ExcludeClipRect(hdc, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);
            HBRUSH hbr = GetSysColorBrush(COLOR_WINDOWFRAME);
            RECT rcAll = rcWnd;
            InflateRect(&rcAll, cxEdge, cyEdge);
            FillRect(hdc, &rcAll, hbr);
            RestoreDC(hdc, nSave);
        }
        BuiltinImage *imgs = BuiltinImage::instance();
        {
            RECT rcSb;
            if (GetScrollBarRect(hWnd, SB_VERT, &rcSb) && RectInRegion(hrgn, &rcSb))
            {
                ScrollBar &sb = wndObj->sbVert;
                HDC memdc = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(memdc, rcSb.right - rcSb.left, rcSb.bottom - rcSb.top);
                HGDIOBJ oldBmp = SelectObject(memdc, bmp);
                SetViewportOrgEx(memdc, -rcSb.left, -rcSb.top, NULL);
                FillRect(memdc, &rcSb, GetStockObject(WHITE_BRUSH)); // init alpha channel to 255
                {
                    RECT rc = GetScrollBarPartRect(TRUE, &sb, SB_LINELEFT, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_LINELEFT);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_LINELEFT);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Line_Up, TRUE, st, &rc, byAlpha);
                }
                {
                    RECT rc = GetScrollBarPartRect(TRUE, &sb, SB_RAIL, &rcSb);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Rail, TRUE, BuiltinImage::St_Normal, &rc, 0xFF);
                }
                {
                    RECT rc = GetScrollBarPartRect(TRUE, &sb, SB_THUMBTRACK, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_THUMBTRACK);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_THUMBTRACK);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Thumb, TRUE, st, &rc, byAlpha);
                }
                {
                    RECT rc = GetScrollBarPartRect(TRUE, &sb, SB_LINEDOWN, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_LINEDOWN);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_LINEDOWN);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Line_Down, TRUE, st, &rc, byAlpha);
                }
                BitBlt(hdc, rcSb.left, rcSb.top, rcSb.right - rcSb.left, rcSb.bottom - rcSb.top, memdc, rcSb.left, rcSb.top, SRCCOPY);
                SelectObject(memdc, oldBmp);
                DeleteDC(memdc);
                DeleteObject(bmp);
            }
            if (GetScrollBarRect(hWnd, SB_HORZ, &rcSb) && RectInRegion(hrgn, &rcSb))
            {
                ScrollBar &sb = wndObj->sbHorz;
                HDC memdc = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(memdc, rcSb.right - rcSb.left, rcSb.bottom - rcSb.top);
                HGDIOBJ oldBmp = SelectObject(memdc, bmp);
                SetViewportOrgEx(memdc, -rcSb.left, -rcSb.top, NULL);
                FillRect(memdc, &rcSb, GetStockObject(BLACK_BRUSH)); // init alpha channel to 255
                {
                    RECT rc = GetScrollBarPartRect(FALSE, &sb, SB_LINELEFT, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_LINELEFT);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_LINELEFT);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Line_Up, FALSE, st, &rc, byAlpha);
                }
                {
                    RECT rc = GetScrollBarPartRect(FALSE, &sb, SB_RAIL, &rcSb);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Rail, FALSE, BuiltinImage::St_Normal, &rc, 0xFF);
                }
                {
                    RECT rc = GetScrollBarPartRect(FALSE, &sb, SB_THUMBTRACK, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_THUMBTRACK);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_THUMBTRACK);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Thumb, FALSE, st, &rc, byAlpha);
                }
                {
                    RECT rc = GetScrollBarPartRect(FALSE, &sb, SB_LINEDOWN, &rcSb);
                    int st = GetScrollBarPartState(&sb, SB_LINEDOWN);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb, SB_LINEDOWN);
                    imgs->drawScrollbarState(memdc, BuiltinImage::Sb_Line_Down, FALSE, st, &rc, byAlpha);
                }
                BitBlt(hdc, rcSb.left, rcSb.top, rcSb.right - rcSb.left, rcSb.bottom - rcSb.top, memdc, rcSb.left, rcSb.top, SRCCOPY);
                SelectObject(memdc, oldBmp);
                DeleteDC(memdc);
                DeleteObject(bmp);
            }
            if ((wndObj->showSbFlags & SB_BOTH) == SB_BOTH)
            {
                rcWnd.left = rcWnd.right - GetSystemMetrics(SM_CXVSCROLL);
                rcWnd.top = rcWnd.bottom - GetSystemMetrics(SM_CYHSCROLL);
                imgs->drawScrollbarState(hdc, (wndObj->dwStyle & WS_CHILD) ? (BuiltinImage::Sb_Triangle + 3) : BuiltinImage::Sb_Triangle, FALSE, BuiltinImage::St_Normal, &rcWnd, 0xff);
            }
        }

        ReleaseDC(hWnd, hdc);
    }
    if ((int)wp <= 1)
    {
        DeleteObject(hrgn);
    }
}

static LRESULT handleNcLbuttonDown(HWND hWnd, WPARAM wp, LPARAM lp)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    RECT rcSbHorz, rcSbVert;
    GetScrollBarRect(hWnd, SB_HORZ, &rcSbHorz);
    GetScrollBarRect(hWnd, SB_VERT, &rcSbVert);
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    ScrollBar *sb = nullptr;
    BOOL bVert = TRUE;
    RECT *pRcAll = &rcSbVert;
    if (PtInRect(&rcSbVert, pt))
        sb = &wndObj->sbVert;
    else if (PtInRect(&rcSbHorz, pt))
        sb = &wndObj->sbHorz, bVert = FALSE, pRcAll = &rcSbHorz;
    if (!sb)
        return 0;
    int iPart = ScrollBarHitTest(bVert, sb, pRcAll, pt);
    if (iPart == -1)
        return 0;
    SetCapture(hWnd);
    sb->bDraging = TRUE;
    sb->nState = BuiltinImage::St_Push;
    sb->iHitTest = iPart;
    POINT ptStart = pt;
    int pos = sb->nPos;
    RECT rcPart = GetScrollBarPartRect(bVert, sb, iPart, pRcAll);
    RECT rcRail = GetScrollBarPartRect(bVert, sb, SB_RAIL, pRcAll);
    int railLen = bVert ? (rcRail.bottom - rcRail.top) : (rcRail.right - rcRail.left);
    InvalidateRect(hWnd, &rcPart,TRUE);
    if (iPart != SB_THUMBTRACK)
    {
        SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, iPart, 0);
        SetTimer(hWnd, TIMER_STARTAUTOSCROLL, SPAN_STARTAUTOSCROLL, NULL);
    }
    else
    {
        sb->nTrackPos = pos;
        SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, pos), 0);
    }
    // wait for lbuttonup msg
    for (;;)
    {
        MSG msg = { 0 };
        WaitMessage();
        if (!PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE))
            continue;
        if (msg.message == WM_QUIT)
            break;
        PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
        if(msg.message == WM_CANCELMODE)
            break;
        if (CallMsgFilter(&msg, MSGF_SCROLLBAR))
            continue;

        if (msg.message == WM_LBUTTONUP)
        {
            pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            break;
        }
        RECT rcInvalid{ 0, 0, 0, 0 };
        if (msg.hwnd == hWnd && msg.message == WM_TIMER)
        {
            if (msg.wParam == TIMER_STARTAUTOSCROLL)
            {
                KillTimer(hWnd, TIMER_STARTAUTOSCROLL);
                SetTimer(hWnd, TIMER_AUTOSCROLL, SPAN_AUTOSCROLL, NULL);
            }
            else if (msg.wParam == TIMER_AUTOSCROLL)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd, &pt);
                RECT rcPart = GetScrollBarPartRect(bVert, sb, iPart, pRcAll);
                if (PtInRect(&rcPart, pt))
                    SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, iPart, 0);
            }
        }
        else if (msg.hwnd == hWnd && msg.message == WM_MOUSEMOVE)
        {
            POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };

            int iHitTest = ScrollBarHitTest(bVert, sb, pRcAll, pt);
            if (sb->iHitTest != iHitTest)
            {
                if (sb->iHitTest != -1)
                {
                    RECT rc = GetScrollBarPartRect(bVert, sb, sb->iHitTest, pRcAll);
                    InvalidateRect(hWnd,&rc,TRUE);
                }
                sb->iHitTest = iHitTest;
                if (sb->iHitTest != -1)
                {
                    RECT rc = GetScrollBarPartRect(bVert, sb, sb->iHitTest, pRcAll);
                    InvalidateRect(hWnd,&rc,TRUE);
                }
            }
            if (iPart == SB_THUMBTRACK)
            {
                // drag scrollbar.
                int diff = bVert ? (pt.y - ptStart.y) : (pt.x - ptStart.x);
                RECT rcThumb = GetScrollBarPartRect(bVert, sb, SB_THUMBTRACK, pRcAll);
                int nThumbLen = bVert ? (rcThumb.bottom - rcThumb.top) : (rcThumb.right - rcThumb.left);
                int nEmptyHei = railLen - nThumbLen;
                int nSlide = (int)((nEmptyHei == 0) ? 0 : (diff * (__int64)(sb->nMax - sb->nMin - sb->nPage + 1) / nEmptyHei));
                int nNewTrackPos = pos + nSlide;
                if (nNewTrackPos < sb->nMin)
                {
                    nNewTrackPos = sb->nMin;
                }
                else if (nNewTrackPos > (int)(sb->nMax - sb->nMin - sb->nPage + 1))
                {
                    nNewTrackPos = sb->nMax - sb->nMin - sb->nPage + 1;
                }
                sb->nTrackPos = nNewTrackPos;
                InvalidateRect(hWnd,&rcRail,TRUE);
                SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, nNewTrackPos), 0);
            }
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    ReleaseCapture();

    if (iPart != SB_THUMBTRACK)
    {
        KillTimer(hWnd, TIMER_STARTAUTOSCROLL);
        KillTimer(hWnd, TIMER_AUTOSCROLL);
    }
    else
    {
        SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, sb->nTrackPos), 0);
        SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
        sb->nTrackPos = -1;
        InvalidateRect(hWnd, &rcRail,TRUE);
    }
    sb->iHitTest = -1;
    sb->bDraging = FALSE;

    SendMessageA(hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));

    return 0;
}

static LRESULT handlePrint(HWND hWnd, WPARAM wp, LPARAM lp)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    if (lp & PRF_CHECKVISIBLE && !IsWindowVisible(hWnd))
        return 0;
    HDC hdc = wndObj->hdc;
    wndObj->hdc = (HDC)wp;
    HRGN hRgn = wndObj->invalid.hRgn;
    BOOL bErase = wndObj->invalid.bErase;

    if ((lp & (PRF_CLIENT | PRF_NONCLIENT)) == (PRF_CLIENT | PRF_NONCLIENT))
    {
        RECT rc = wndObj->rc;
        OffsetRect(&rc, -rc.left, -rc.top);
        wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
    }
    else if (lp & PRF_CLIENT)
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
    }
    else if (lp & PRF_NONCLIENT)
    {
        RECT rc = wndObj->rc;
        OffsetRect(&rc, -rc.left, -rc.top);
        wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
        GetClientRect(hWnd, &rc);
        HRGN rgnClient = CreateRectRgnIndirect(&rc);
        CombineRgn(wndObj->invalid.hRgn, wndObj->invalid.hRgn, rgnClient, RGN_DIFF);
        DeleteObject(rgnClient);
    }
    wndObj->invalid.bErase = lp & PRF_ERASEBKGND;
    SendMessageA(hWnd, WM_PAINT, 0, 0);
    if (lp & PRF_CHILDREN)
    {
        HWND hChild = GetWindow(hWnd, GW_CHILD);
        while (hChild)
        {
            RECT rcChild;
            GetWindowRect(hChild, &rcChild);
            MapWindowPoints(0, hWnd, (LPPOINT)&rcChild, 2);
            OffsetViewportOrgEx(wndObj->hdc, -rcChild.left, -rcChild.top, NULL);
            SendMessage(hChild, WM_PRINT, wp, lp);
            OffsetViewportOrgEx(wndObj->hdc, rcChild.left, rcChild.top, NULL);
            hChild = GetWindow(hChild, GW_HWNDNEXT);
        }
    }
    wndObj->invalid.hRgn = hRgn;
    wndObj->invalid.bErase = bErase;
    wndObj->hdc = hdc;
    return 1;
}

static LRESULT handleSetFont(HWND hWnd, WPARAM wp, LPARAM lp)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    HFONT hFont = (HFONT)wp;
    BOOL bRedraw = LOWORD(lp);
    if (hFont)
    {
        SelectObject(wndObj->hdc, hFont);
    }
    else
    {
        SelectObject(wndObj->hdc, GetStockObject(SYSTEM_FONT));
    }
    if (bRedraw)
    {
        InvalidateRect(hWnd, nullptr, TRUE);
    }
    return TRUE;
}

static LRESULT handleInputLanguageChangeRequest(HWND hWnd, WPARAM wp, LPARAM lp)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    wndObj->mConnection->ActivateKeyboardLayout((HKL)lp);
    return 1;
}

static LRESULT handleInputLangage(HWND hWnd, WPARAM wp, LPARAM lp)
{
    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild)
    {
        SendMessageA(hWnd, WM_INPUTLANGCHANGE, wp, lp);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
    return 1;
}

static HBRUSH DEFWND_ControlColor(HDC hDC, UINT ctlType)
{
    if (ctlType == CTLCOLOR_SCROLLBAR)
    {
        HBRUSH hb = GetSysColorBrush(COLOR_SCROLLBAR);
        COLORREF bk = GetSysColor(COLOR_3DHILIGHT);
        SetTextColor(hDC, GetSysColor(COLOR_3DFACE));
        SetBkColor(hDC, bk);
        return hb;
    }

    SetTextColor(hDC, GetSysColor(COLOR_WINDOWTEXT));

    if ((ctlType == CTLCOLOR_EDIT) || (ctlType == CTLCOLOR_LISTBOX))
        SetBkColor(hDC, GetSysColor(COLOR_WINDOW));
    else
    {
        SetBkColor(hDC, GetSysColor(COLOR_3DFACE));
        return GetSysColorBrush(COLOR_3DFACE);
    }
    return GetSysColorBrush(COLOR_WINDOW);
}

static LRESULT OnSetWindowText(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    return wndObj->mConnection->OnSetWindowText(hWnd, wndObj.data(), (LPCSTR)lp);
}

LRESULT OnMsgW2A(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    MSG *msg = (MSG *)lp;
    LRESULT ret = 0;
    if (IsWindowUnicode(hWnd))
        return wndObj->winproc(hWnd, msg->message, msg->wParam, msg->lParam);

    switch (msg->message)
    {
    case WM_GETTEXTLENGTH:
        ret = MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
        break;
    case WM_GETTEXT:
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
        if (wp < len)
            ret = 0;
        else
        {
            LPWSTR buf = (LPWSTR)lp;
            MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), buf, len);
            if (wp > len)
                buf[len] = 0;
            ret = len;
        }
    }
    break;
    case WM_SETTEXT:
    {
        std::string str;
        tostring((LPCWSTR)lp, -1, str);
        ret = wndObj->winproc(hWnd, msg->message, 0, (LPARAM)str.c_str());
    }
    break;
    default:
        ret = wndObj->winproc(hWnd, msg->message, msg->wParam, msg->lParam);
        break;
    }
    return ret;
}

static void UpdateScroll(HWND hWnd, WndObj &wndObj, BOOL bVert,ScrollBar &sb, RECT &rcSb, int htSb)
{
    if (htSb != sb.iHitTest)
    {
        if (htSb != -1)
        {
            RECT rcPart = GetScrollBarPartRect(bVert, &sb, htSb, &rcSb);
            InvalidateRect(hWnd, &rcPart, TRUE);
        }
        if (sb.iHitTest != -1)
        {
            RECT rcPart = GetScrollBarPartRect(bVert, &sb, sb.iHitTest, &rcSb);
            InvalidateRect(hWnd, &rcPart, TRUE);
        }
        sb.iHitTest = htSb;
    }
}

static LRESULT OnNcMouseHover(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    MapWindowPoints(0, hWnd, &pt, 1);
    if (wndObj->dwStyle & WS_VSCROLL)
    {
        RECT rcSb;
        GetScrollBarRect(hWnd, SB_VERT, &rcSb);
        ScrollBar &sb = wndObj->sbVert;
        int htSb = ScrollBarHitTest(TRUE, &sb, &rcSb, pt);
        UpdateScroll(hWnd, wndObj, TRUE, sb, rcSb, htSb);
    }
    if (wndObj->dwStyle & WS_HSCROLL)
    {
        RECT rcSb;
        GetScrollBarRect(hWnd, SB_HORZ, &rcSb);
        ScrollBar &sb = wndObj->sbHorz;
        int htSb = ScrollBarHitTest(FALSE, &sb, &rcSb, pt);
        UpdateScroll(hWnd, wndObj,FALSE, sb, rcSb, htSb);
    }
    return 0;
}

static LRESULT OnNcMouseLeave(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    if (wndObj->dwStyle & WS_VSCROLL)
    {
        RECT rcSb;
        GetScrollBarRect(hWnd, SB_VERT, &rcSb);
        ScrollBar &sb = wndObj->sbVert;
        UpdateScroll(hWnd, wndObj,TRUE, sb, rcSb, -1);
    }
    if (wndObj->dwStyle & WS_HSCROLL)
    {
        RECT rcSb;
        GetScrollBarRect(hWnd, SB_HORZ, &rcSb);
        ScrollBar &sb = wndObj->sbHorz;
        UpdateScroll(hWnd, wndObj,FALSE, sb, rcSb, -1);
    }
    return 0;
}

static LRESULT OnNcHitTest(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    RECT rc;
    GetClientRect(hWnd, &rc);
    int wid = rc.right - rc.left;
    int hei = rc.bottom - rc.top;
    ClientToScreen(hWnd, (LPPOINT)&rc);
    rc.right = rc.left + wid;
    rc.bottom = rc.top + hei;

    POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    if (PtInRect(&rc, pt))
        return HTCLIENT;
    else
    {
        if (wndObj->dwStyle & WS_HSCROLL)
        {
            RECT rcSb;
            GetScrollBarRect(hWnd, SB_HORZ, &rcSb);
            MapWindowPoints(hWnd, 0, (LPPOINT)&rcSb, 2);
            if (PtInRect(&rcSb, pt))
            {
                return HTHSCROLL;
            }
        }
        if (wndObj->dwStyle & WS_VSCROLL)
        {
            RECT rcSb;
            GetScrollBarRect(hWnd, SB_VERT, &rcSb);
            MapWindowPoints(hWnd, 0, (LPPOINT)&rcSb, 2);
            if (PtInRect(&rcSb, pt))
            {
                return HTVSCROLL;
            }
        }
        return HTBORDER;
    }
}

static LRESULT OnNcCreate(HWND hWnd, WndObj &wndObj, WPARAM wp, LPARAM lp)
{
    CREATESTRUCT *cs = (CREATESTRUCT *)lp;
    OnSetWindowText(hWnd, wndObj, wp, (LPARAM)cs->lpszName);
    return TRUE;
}

LRESULT DefWindowProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return -1;
    switch (msg)
    {
    case WM_MSG_W2A:
        return OnMsgW2A(hWnd, wndObj, wp, lp);
    case WM_NCCREATE:
        return OnNcCreate(hWnd, wndObj, wp, lp);
    case WM_SETTEXT:
        return OnSetWindowText(hWnd, wndObj, wp, lp);
    case WM_GETTEXTLENGTH:
        return wndObj->title.length();
    case WM_NCHITTEST:
        return OnNcHitTest(hWnd, wndObj, wp, lp);
    case WM_NCMOUSEMOVE:
    case WM_NCMOUSEHOVER:
        return OnNcMouseHover(hWnd, wndObj, wp, lp);
    case WM_NCMOUSELEAVE:
        return OnNcMouseLeave(hWnd, wndObj, wp, lp);
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORSCROLLBAR:
        return (LRESULT)DEFWND_ControlColor((HDC)wp, msg - WM_CTLCOLORMSGBOX);
    case WM_CTLCOLOR:
        return (LRESULT)DEFWND_ControlColor((HDC)wp, HIWORD(lp));
    case WM_INPUTLANGCHANGEREQUEST:
        return handleInputLanguageChangeRequest(hWnd, wp, lp);
    case WM_INPUTLANGCHANGE:
        return handleInputLangage(hWnd, wp, lp);
    case WM_SETFONT:
        return handleSetFont(hWnd, wp, lp);
    case WM_PRINT:
        return handlePrint(hWnd, wp, lp);
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        return handleNcLbuttonDown(hWnd, wp, lp);
    case WM_SETICON:
    {
        if (wp == ICON_SMALL)
        {
            HICON ret = wndObj->iconSmall;
            wndObj->iconSmall = (HICON)lp;
            wndObj->mConnection->UpdateWindowIcon(hWnd, wndObj.data());
            return (LRESULT)ret;
        }
        else if (wp == ICON_BIG)
        {
            HICON ret = wndObj->iconBig;
            wndObj->iconBig = (HICON)lp;
            wndObj->mConnection->UpdateWindowIcon(hWnd, wndObj.data());
            return (LRESULT)ret;
        }
        break;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        FillRect(hdc, &rcClient, GetStockObject(WHITE_BRUSH));
        DrawTextA(hdc, wndObj->title.c_str(), wndObj->title.length(), &rcClient, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_ERASEBKGND:
    {
        WNDCLASSEXA info = { 0 };
        // GetClassInfoEx return the atom value instead an bool
        ATOM clsAtom = GetClassInfoExA(wndObj->hInstance, MAKEINTRESOURCE(wndObj->clsAtom), &info);
        if (clsAtom && info.hbrBackground)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect((HDC)wp, &rc, info.hbrBackground);
        }
    }
    break;
    case WM_GETMINMAXINFO:
        return -1; // not handle
    case WM_SYSCOMMAND:
    {
        WORD action = wp & 0xfff0;
        switch (action)
        {
        case SC_MOVE:
            if ((wp & 0x0f) == HTCAPTION)
            {
                HCURSOR oldCursor = SetCursor(LoadCursor(wndObj->hInstance, IDC_SIZE));
                HandleNcTestCode(hWnd, HTCAPTION);
                SetCursor(oldCursor);
            }
            break;
        case SC_MINIMIZE:
        case SC_MAXIMIZE:
        case SC_RESTORE:
            wndObj->mConnection->SendSysCommand(hWnd,action);
            break;
        case SC_CLOSE:
            SendMessage(hWnd, WM_CLOSE, 0, 0);
            break;
        }
    }
    break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_WINDOWPOSCHANGING:
    {
        WINDOWPOS *lpWndPos = (WINDOWPOS *)lp;
        if (!(lpWndPos->flags & (SWP_NOSIZE | SWP_NOMOVE)))
        {
            MINMAXINFO info = { 0 };
            if (0 == SendMessage(hWnd, WM_GETMINMAXINFO, 0, (LPARAM)&info))
            {
                if (lpWndPos->cx > info.ptMaxTrackSize.x)
                    lpWndPos->cx = info.ptMaxTrackSize.x;
                if (lpWndPos->cx < info.ptMinTrackSize.x)
                    lpWndPos->cx = info.ptMinTrackSize.x;

                if (lpWndPos->cy > info.ptMaxTrackSize.y)
                    lpWndPos->cy = info.ptMaxTrackSize.y;
                if (lpWndPos->cy < info.ptMinTrackSize.y)
                    lpWndPos->cy = info.ptMinTrackSize.y;

                if (lpWndPos->x > info.ptMaxPosition.x)
                    lpWndPos->x = info.ptMaxPosition.x;
                if (lpWndPos->y > info.ptMaxPosition.y)
                    lpWndPos->y = info.ptMaxPosition.y;
            }
        }
    }
    break;
    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS &wndPos = *(WINDOWPOS *)lp;
        RECT &rc = wndObj->rc;
        if (!(wndPos.flags & SWP_NOMOVE))
        {
            if ((wndPos.x != rc.left || wndPos.y != rc.top))
            {
                wndObj->mConnection->SetWindowPos(hWnd, wndPos.x, wndPos.y);
            }
            SendMessage(hWnd, WM_MOVE, 0, MAKELPARAM(wndPos.x, wndPos.y));
        }
        if (!(wndPos.flags & SWP_NOSIZE))
        {
            if ((wndPos.cx != rc.right - rc.left || wndPos.cy != rc.bottom - rc.top))
            {
                wndObj->mConnection->SetWindowSize(hWnd, wndPos.cx, wndPos.cy);
            }
            SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(wndPos.cx, wndPos.cy));
        }
        if (!(wndPos.flags & SWP_NOZORDER))
        {
            wndObj->mConnection->SetZOrder(hWnd, wndObj.data(), wndPos.hwndInsertAfter);
        }
        int showCmd = -1;
        if (wndPos.flags & SWP_SHOWWINDOW)
        {
            if (wndPos.flags & SWP_NOACTIVATE)
                showCmd = SW_SHOWNOACTIVATE;
            else
                showCmd = SW_SHOW;
        }
        else if (wndPos.flags & SWP_HIDEWINDOW)
        {
            showCmd = SW_HIDE;
        }
        if (showCmd != -1)
            ShowWindow(wndPos.hwnd, showCmd);
        if (!(wndPos.flags & SWP_NOREDRAW))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
        }
    }
    break;
    case WM_NCPAINT:
    {
        OnNcPaint(hWnd, wp, lp);
        break;
    }
    case WM_MOUSEACTIVATE:
    {
        if (wndObj->dwExStyle & WS_EX_NOACTIVATE)
            return MA_NOACTIVATE;
        if (wndObj->dwStyle & WS_DISABLED)
            return MA_NOACTIVATE;
        return MA_ACTIVATE;
    }
    }
    return 0;
}

BOOL ShowWindow(HWND hWnd, int nCmdShow)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    BOOL bVisible = IsWindowVisible(hWnd);
    BOOL bNew = nCmdShow == SW_SHOW || nCmdShow == SW_SHOWNOACTIVATE || nCmdShow == SW_SHOWNORMAL || nCmdShow == SW_SHOWNA || nCmdShow == SW_MAXIMIZE || nCmdShow == SW_MINIMIZE;
    if (bVisible == bNew)
        return TRUE;
    wndObj->mConnection->SetWindowVisible(hWnd, wndObj.data(), bNew, nCmdShow);
    SendMessage(hWnd, WM_SHOWWINDOW, bNew, 0);
    if (nCmdShow == SW_MAXIMIZE)
    {
        SendMessage(hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    }
    else if (nCmdShow == SW_MINIMIZE)
    {
        SendMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
    return TRUE;
}

BOOL MoveWindow(HWND hWnd, int x, int y, int nWidth, int nHeight, BOOL bRepaint)
{
    return SetWindowPos(hWnd, 0, x, y, nWidth, nHeight, SWP_NOZORDER | (bRepaint ? 0 : SWP_NOREDRAW));
}

BOOL IsWindowVisible(HWND hWnd)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->IsWindowVisible(hWnd);
}

BOOL IsZoomed(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    return wndObj->state == WS_Maximized;
}

BOOL IsIconic(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    return wndObj->state == WS_Minimized;
}

int GetWindowTextW(HWND hWnd, LPWSTR lpszStringBuf, int nMaxCount)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnGetWindowTextW(hWnd, lpszStringBuf, nMaxCount);
}

int GetWindowTextA(HWND hWnd, LPSTR lpszStringBuf, int nMaxCount)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnGetWindowTextA(hWnd, lpszStringBuf, nMaxCount);
}

int GetWindowTextLengthW(HWND hWnd)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnGetWindowTextLengthW(hWnd);
}

int GetWindowTextLengthA(HWND hWnd)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnGetWindowTextLengthA(hWnd);
}

BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpszString)
{
    std::string str;
    tostring(lpszString, -1, str);
    return SetWindowTextA(hWnd, str.c_str());
}

BOOL SetWindowTextA(HWND hWnd, LPCSTR lpszString)
{
    return SendMessageA(hWnd, WM_SETTEXT, 0, (LPARAM)lpszString);
}

HDC GetDC(HWND hWnd)
{
    return GetDCEx(hWnd, nullptr, 0);
}

HDC GetDCEx(HWND hWnd, HRGN hrgnClip, DWORD flags)
{
    if (!hWnd)
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->GetDC();
    }
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wndObj->hdc)
        return 0;
    HDC hdc = wndObj->hdc;
    int nSave = SaveDC(hdc);
    if (nSave == 0)
    {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        if (wndObj->dwStyle & WS_BORDER)
        {
            OffsetRect(&rcClient, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
        }
        if (flags & DCX_WINDOW)
        {
            SetViewportOrgEx(hdc, 0, 0, NULL);
            ExcludeClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
        }
        else
        {
            IntersectClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
            SetViewportOrgEx(hdc, rcClient.left, rcClient.top, NULL);
        }
    }

    if (hrgnClip)
    {
        if (flags & DCX_EXCLUDERGN)
            ExtSelectClipRgn(hdc, hrgnClip, RGN_DIFF);
        else if (flags & DCX_INTERSECTRGN)
            ExtSelectClipRgn(hdc, hrgnClip, RGN_AND);
    }
    return hdc;
}

HDC GetWindowDC(HWND hWnd)
{
    return GetDCEx(hWnd, 0, DCX_WINDOW);
}

int ReleaseDC(HWND hWnd, HDC hdc)
{
    if (!hWnd)
    {
        SConnection *conn = SConnMgr::instance()->getConnection();
        return conn->ReleaseDC(hdc);
    }
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    RestoreDC(hdc, -1);
    if (hdc->nSave > 0)
        return 1;
    RECT rc;
    GetClipBox(hdc, &rc);
    wndObj->mConnection->commitCanvas(hWnd,rc);
    return 1;
}

int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoint, UINT nCount)
{
    if (hWndFrom == hWndTo)
        return 0;
    RECT rcFrom = { 0 }, rcTo = { 0 };
    if (hWndFrom && !GetWindowRect(hWndFrom, &rcFrom))
        return 0;
    if (hWndTo && !GetWindowRect(hWndTo, &rcTo))
        return 0;
    int xDiff = rcTo.left - rcFrom.left;
    int yDiff = rcTo.top - rcFrom.top;

    for (UINT i = 0; i < nCount; i++)
    {
        lpPoint[i].x -= xDiff;
        lpPoint[i].y -= yDiff;
    }
    return MAKELONG(-xDiff, -yDiff);
}

UINT GetDpiForWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        return wndObj->mConnection->GetDpi(TRUE);
    return SConnMgr::instance()->getConnection()->GetDpi(TRUE);
}

BOOL SetLayeredWindowAttributes(HWND hWnd, COLORREF crKey, BYTE byAlpha, DWORD dwFlags)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if ((wndObj->dwExStyle & WS_EX_LAYERED) == 0)
        return FALSE;
    if (dwFlags & LWA_COLORKEY)
    {
        return FALSE;
    }
    if ((dwFlags & LWA_ALPHA) && byAlpha != wndObj->byAlpha)
    {
        wndObj->byAlpha = byAlpha;
        if (wndObj->flags & kMapped)
            wndObj->mConnection->SetWindowOpacity(hWnd, byAlpha);
    }

    return TRUE;
}

int SetWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    if (!wndObj->mConnection->SetWindowRgn(hWnd, hRgn))
        return 0;
    if (bRedraw)
        InvalidateRect(hWnd, nullptr, TRUE);
    return 1;
}

BOOL TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack)
{
    WndObj wndObj = WndMgr::fromHwnd(lpEventTrack->hwndTrack);
    if (!wndObj)
        return FALSE;
    if (lpEventTrack->dwFlags & TME_QUERY)
    {
        lpEventTrack->dwFlags = wndObj->hoverInfo.dwFlags;
        lpEventTrack->dwHoverTime = wndObj->hoverInfo.dwHoverTime;
        return TRUE;
    }
    wndObj->hoverInfo.dwFlags = lpEventTrack->dwFlags;
    wndObj->hoverInfo.dwHoverTime = lpEventTrack->dwHoverTime;
    return TRUE;
}

static HWND GetWndSibling(xcb_connection_t *conn, HWND hParent, HWND hWnd, BOOL bNext)
{
    xcb_query_tree_cookie_t cookie = xcb_query_tree(conn, hParent);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(conn, cookie, NULL);
    if (!reply)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    xcb_window_t *children = xcb_query_tree_children(reply);
    int idx_self = -1;
    for (int i = 0; i < reply->children_len; i++)
    {
        if (children[i] == hWnd)
        {
            idx_self = i;
            break;
        }
    }
    HWND hRet = 0;
    if (idx_self != -1)
    {
        if (bNext && idx_self < reply->children_len - 1)
            hRet = children[idx_self + 1];
        if (!bNext && idx_self > 0)
            hRet = children[idx_self - 1];
    }
    free(reply);
    return hRet;
}

HWND GetWindow(HWND hWnd, UINT uCmd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    else
        return wndObj->mConnection->GetWindow(hWnd, wndObj.data(), uCmd);
}

//-------------------------------------------------------------
// scrollbar api
BOOL EnableScrollBar(HWND hWnd, UINT wSBflags, UINT wArrows)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)

    {
        ScrollBar &sb = wndObj->sbHorz;
        sb.nState = wSBflags & SB_HORZ ? (BuiltinImage::St_Normal) : (BuiltinImage::St_Disable);
        sb.uArrowFlags = wArrows;
    }
    {
        ScrollBar &sb = wndObj->sbVert;
        sb.nState = wSBflags & SB_VERT ? (BuiltinImage::St_Normal) : (BuiltinImage::St_Disable);
        sb.uArrowFlags = wArrows;
    }
    InvalidateRect(hWnd, NULL, TRUE);
    return TRUE;
}

BOOL ShowScrollBar(HWND hWnd, int wBar, BOOL bShow)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if (bShow)
    {
        if (wBar & SB_VERT)
        {
            wndObj->showSbFlags |= SB_VERT;
            wndObj->dwExStyle |= WS_VSCROLL;
        }
        if (wBar & SB_HORZ)
        {
            wndObj->showSbFlags |= SB_HORZ;
            wndObj->dwExStyle |= WS_HSCROLL;
        }
    }
    else
    {
        if (wBar & SB_VERT)
        {
            wndObj->showSbFlags &= ~SB_VERT;
            wndObj->dwExStyle &= ~WS_VSCROLL;
        }
        if (wBar & SB_HORZ)
        {
            wndObj->showSbFlags &= ~SB_HORZ;
            wndObj->dwExStyle &= ~WS_HSCROLL;
        }
    }
    InvalidateRect(hWnd, NULL, TRUE);
    return TRUE;
}

static BOOL GetScrollInfoByMask(LPSCROLLINFO dst, const LPCSCROLLINFO src, UINT fMask)
{
    if (fMask & SIF_PAGE)
    {
        if (src->nPage <= 0)
            return FALSE;
        dst->nPage = src->nPage;
    }
    if (fMask & SIF_POS)
    {
        dst->nPos = src->nPos;
    }
    if (fMask & SIF_TRACKPOS)
    {
        dst->nTrackPos = src->nTrackPos;
    }
    if (fMask & SIF_RANGE)
    {
        if (src->nMax < src->nMin)
            return FALSE;
        dst->nMin = src->nMin;
        dst->nMax = src->nMax;
    }
    return TRUE;
}

static BOOL SetScrollInfoByMask(LPSCROLLINFO dst, const LPCSCROLLINFO src, UINT fMask)
{
    if (fMask & SIF_PAGE)
    {
        if (src->nPage <= 0)
            return FALSE;
        dst->nPage = src->nPage;
    }
    if (fMask & SIF_POS)
    {
        dst->nPos = src->nPos;
    }
    if (fMask & SIF_TRACKPOS)
    {
        dst->nTrackPos = src->nTrackPos;
    }
    if (fMask & SIF_RANGE)
    {
        if (src->nMax < src->nMin)
            return FALSE;
        dst->nMin = src->nMin;
        dst->nMax = src->nMax;
    }
    int posMax = dst->nMax - dst->nPage + 1;
    if (dst->nTrackPos != -1)
    {
        if (dst->nTrackPos < dst->nMin)
            dst->nTrackPos = dst->nMin;
        if (dst->nTrackPos > posMax)
        {
            dst->nTrackPos = posMax;
            if (dst->nTrackPos < dst->nMin)
                dst->nTrackPos = dst->nMin;
        }
    }
    if (dst->nPos < dst->nMin)
        dst->nPos = dst->nMin;
    if (dst->nPos > posMax)
    {
        dst->nPos = posMax;
        if (dst->nPos < dst->nMin)
            dst->nPos = dst->nMin;
    }
    return TRUE;
}

int SetScrollInfo(HWND hWnd, int fnBar, LPCSCROLLINFO lpsi, BOOL fRedraw)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    BOOL bRet = FALSE;
    BOOL bSwitch = FALSE;
    if (fnBar == SB_VERT)
    {
        SCROLLINFO &sb = wndObj->sbVert;
        bRet = SetScrollInfoByMask(&sb, lpsi, lpsi->fMask);
        if (bRet)
        {
            BOOL bEnable = sb.nMax - sb.nMin + 1 > sb.nPage;
            BOOL bPrev = wndObj->dwStyle & WS_VSCROLL ? TRUE : FALSE;
            if (bEnable != bPrev)
            {
                bSwitch = TRUE;
                if (bEnable)
                {
                    wndObj->dwStyle |= WS_VSCROLL;
                    wndObj->showSbFlags |= SB_VERT;
                }
                else
                {
                    wndObj->dwStyle &= ~WS_VSCROLL;
                    wndObj->showSbFlags &= ~SB_VERT;
                }
            }
        }
    }
    else if (fnBar == SB_HORZ)
    {
        SCROLLINFO &sb = wndObj->sbHorz;
        bRet = SetScrollInfoByMask(&sb, lpsi, lpsi->fMask);
        if (bRet)
        {
            BOOL bEnable = sb.nMax - sb.nMin + 1 > sb.nPage;
            BOOL bPrev = wndObj->dwStyle & WS_HSCROLL ? TRUE : FALSE;
            if (bEnable != bPrev)
            {
                bSwitch = TRUE;
                if (bEnable)
                {
                    wndObj->dwStyle |= WS_HSCROLL;
                    wndObj->showSbFlags |= SB_HORZ;
                }
                else
                {
                    wndObj->dwStyle &= ~WS_HSCROLL;
                    wndObj->showSbFlags &= ~SB_HORZ;
                }
            }
        }
    }
    else
    {
        return FALSE;
    }
    if (bRet && fRedraw)
    {
        if (bSwitch)
        {
            RECT rcWnd = wndObj->rc;
            OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top);
            InvalidateRect(hWnd, &rcWnd,TRUE);
        }
        else
        {
            RECT rcBar;
            if (GetScrollBarRect(hWnd, fnBar, &rcBar))
                InvalidateRect(hWnd, &rcBar,TRUE);
        }
    }
    return TRUE;
}

BOOL GetScrollInfo(HWND hWnd, int fnBar, LPSCROLLINFO lpsi)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if (fnBar == SB_VERT)
    {
        GetScrollInfoByMask(lpsi, &wndObj->sbVert, lpsi->fMask);
    }
    else if (fnBar == SB_HORZ)
    {
        GetScrollInfoByMask(lpsi, &wndObj->sbHorz, lpsi->fMask);
    }
    else
    {
        return FALSE;
    }
    return TRUE;
}

int GetScrollPos(HWND hWnd, int nBar)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    if (nBar != SB_VERT && nBar != SB_HORZ)
        return 0;
    SCROLLINFO &si = nBar == SB_VERT ? wndObj->sbVert : wndObj->sbHorz;
    return si.nPos;
}

int SetScrollPos(HWND hWnd, int nBar, int nPos, BOOL bRedraw)
{
    SCROLLINFO si;
    si.fMask = SIF_POS;
    si.nPos = nPos;
    return SetScrollInfo(hWnd, nBar, &si, bRedraw);
}

BOOL GetScrollRange(HWND hWnd, int nBar, LPINT lpMinPos, LPINT lpMaxPos)
{
    SCROLLINFO si;
    si.fMask = SIF_RANGE;
    if (!GetScrollInfo(hWnd, nBar, &si))
        return FALSE;
    if (lpMinPos)
        *lpMinPos = si.nMin;
    if (lpMaxPos)
        *lpMaxPos = si.nMax;
    return TRUE;
}

BOOL SetScrollRange(HWND hWnd, int nBar, int nMinPos, int nMaxPos, BOOL bRedraw)
{
    SCROLLINFO si;
    si.fMask = SIF_RANGE;
    si.nMax = nMaxPos;
    si.nMin = nMinPos;
    return SetScrollInfo(hWnd, nBar, &si, bRedraw);
}

BOOL WINAPI AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle)
{
    // todo:hjx

    // NONCLIENTMETRICSW ncm;

    // ncm.cbSize = sizeof(ncm);
    // SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);

    // adjust_window_rect(rect, style, menu, exStyle, &ncm);
    return TRUE;
}

int WINAPI GetDlgCtrlID(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    if (!(wndObj->dwStyle & WS_CHILD))
        return 0;
    return wndObj->wIDmenu;
}

LONG WINAPI GetMessageTime(VOID)
{
    return SConnMgr::instance()->getConnection()->GetMsgTime();
}

DWORD WINAPI GetMessagePos(VOID)
{
    return SConnMgr::instance()->getConnection()->GetMsgPos();
}

BOOL WINAPI IsChild(HWND hWndParent, HWND hWnd)
{
    HWND parent = GetParent(hWnd);
    while (parent)
    {
        if (parent == hWndParent)
            return TRUE;
        parent = GetParent(parent);
    }
    return FALSE;
}

BOOL TranslateMessage(const LPMSG pMsg)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    return conn->TranslateMessage(pMsg);
}

BOOL DispatchMessage(const LPMSG pMsg)
{
    if (!pMsg->hwnd)
        return FALSE;
    if (pMsg->message == WM_PAINT && !IsWindowVisible(pMsg->hwnd))
    {
        return FALSE;
    }
    if (pMsg->message == UM_CALLHOOK)
    { // call hook proc for inter thread
        SConnection *conn = SConnMgr::instance()->getConnection();
        if (!conn)
            return FALSE;
        CallHookData *data = (CallHookData *)pMsg->lParam;
        conn->BeforeProcMsg(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
        LRESULT ret = data->proc(data->code, data->wp, data->lp);
        conn->AfterProcMsg(pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam, ret);
        return TRUE;
    }
    WNDPROC wndProc = (WNDPROC)GetWindowLongPtrA(pMsg->hwnd, GWLP_WNDPROC);
    if (!wndProc)
        return FALSE;
    CallWindowProcPriv(wndProc, pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    return TRUE;
}

HWND WINAPI GetDlgItem(_In_opt_ HWND hDlg, _In_ int nIDDlgItem)
{
    HWND hChild = GetWindow(hDlg, GW_CHILD);
    while (hChild)
    {
        WndObj wndObj = WndMgr::fromHwnd(hChild);
        if (wndObj && wndObj->wIDmenu == nIDDlgItem)
            return hChild;
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
    return 0;
}

int WINAPI ScrollWindowEx(HWND hWnd, int dx, int dy, const RECT *prcScroll, const RECT *prcClip, HRGN hrgnUpdate, LPRECT prcUpdate, UINT flags)
{
    // todo:hjx
    return 0;
}

UINT WINAPI RegisterWindowMessageA(_In_ LPCSTR lpString)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->RegisterMessage(lpString);
}

UINT WINAPI RegisterWindowMessageW(_In_ LPCWSTR lpString)
{
    std::string str;
    tostring(lpString, -1, str);
    return RegisterWindowMessageA(str.c_str());
}

BOOL WINAPI IsWindowUnicode(HWND hWnd)
{
    return FALSE;
}

BOOL WINAPI EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnEnumWindows(0, 0, lpEnumFunc, lParam);
}

BOOL WINAPI FlashWindowEx(PFLASHWINFO pfwi)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->FlashWindowEx(pfwi);
}

BOOL WINAPI FlashWindow(HWND hWnd, BOOL bInvert)
{
    FLASHWINFO info = { sizeof(info), 0 };
    info.dwFlags = FLASHW_TRAY;
    info.dwTimeout = 200;
    info.uCount = 5;
    return FlashWindowEx(&info);
}

/***********************************************************************
 *           AnimateWindow (USER32.@)
 */
BOOL WINAPI AnimateWindow(HWND hwnd, DWORD time, DWORD flags)
{
    if (!IsWindow(hwnd) || (!(flags & AW_HIDE)) == IsWindowVisible(hwnd))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    // todo: hjx
    ShowWindow(hwnd, (flags & AW_HIDE) ? SW_HIDE : ((flags & AW_ACTIVATE) ? SW_SHOW : SW_SHOWNA));
    return TRUE;
}

HWND WINAPI FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName)
{
    return FindWindowExA(0, 0, lpClassName, lpWindowName);
}

HWND WINAPI FindWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName)
{
    return FindWindowExW(0, 0, lpClassName, lpWindowName);
}

HWND WINAPI FindWindowExA(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName)
{
    SConnection *pConn = SConnMgr::instance()->getConnection();
    return pConn->OnFindWindowEx(hParent, hChildAfter, lpClassName, lpWindowName);
}

HWND WINAPI FindWindowExW(HWND hParent, HWND hChildAfter, LPCWSTR lpClassName, LPCWSTR lpWindowName)
{
    std::string strCls, strWnd;
    tostring(lpWindowName, -1, strWnd);
    if (IS_INTRESOURCE(lpClassName))
    {
        return FindWindowExA(hParent, hChildAfter, (LPCSTR)lpClassName, lpWindowName ? strWnd.c_str() : nullptr);
    }
    else
    {
        tostring(lpClassName, -1, strCls);
        return FindWindowExA(hParent, hChildAfter, strCls.c_str(), lpWindowName ? strWnd.c_str() : nullptr);
    }
}