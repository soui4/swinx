#include <windows.h>
#include <wnd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <cairo/cairo-xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_icccm.h>
#include <xcb/randr.h>
#include <xcb/xfixes.h>
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
#include "SDragdrop.h"

#define kLogTag "wnd"

// xcb-icccm 3.8 support
#ifdef XCB_ICCCM_NUM_WM_SIZE_HINTS_ELEMENTS
#define xcb_get_wm_hints_reply         xcb_icccm_get_wm_hints_reply
#define xcb_get_wm_hints               xcb_icccm_get_wm_hints
#define xcb_get_wm_hints_unchecked     xcb_icccm_get_wm_hints_unchecked
#define xcb_set_wm_hints               xcb_icccm_set_wm_hints
#define xcb_set_wm_normal_hints        xcb_icccm_set_wm_normal_hints
#define xcb_size_hints_set_base_size   xcb_icccm_size_hints_set_base_size
#define xcb_size_hints_set_max_size    xcb_icccm_size_hints_set_max_size
#define xcb_size_hints_set_min_size    xcb_icccm_size_hints_set_min_size
#define xcb_size_hints_set_position    xcb_icccm_size_hints_set_position
#define xcb_size_hints_set_resize_inc  xcb_icccm_size_hints_set_resize_inc
#define xcb_size_hints_set_size        xcb_icccm_size_hints_set_size
#define xcb_size_hints_set_win_gravity xcb_icccm_size_hints_set_win_gravity
#define xcb_wm_hints_set_iconic        xcb_icccm_wm_hints_set_iconic
#define xcb_wm_hints_set_normal        xcb_icccm_wm_hints_set_normal
#define xcb_wm_hints_set_input         xcb_icccm_wm_hints_set_input
#define xcb_wm_hints_t                 xcb_icccm_wm_hints_t
#define XCB_WM_STATE_ICONIC            XCB_ICCCM_WM_STATE_ICONIC
#define XCB_WM_STATE_WITHDRAWN         XCB_ICCCM_WM_STATE_WITHDRAWN
#endif

enum QX11EmbedInfoFlags
{
    XEMBED_VERSION = 0,
    XEMBED_MAPPED = (1 << 0),
};

enum
{
    fMap = 1 << 0,
};


enum SbPart{
    SB_RAIL = 100
};

enum {
    TIMER_STARTAUTOSCROLL=50000,
    TIMER_AUTOSCROLL,
};
enum {
    SPAN_STARTAUTOSCROLL=500,
    SPAN_AUTOSCROLL=100,
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

static HRGN BuildColorKeyRgn(HWND hWnd);

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
    cairo_surface_t *surface = cairo_xcb_surface_create(wndObj->mConnection->connection, hwnd, 
        xcb_aux_find_visual_by_id(wndObj->mConnection->screen, wndObj->visualId), 
        std::max(cx, 1), std::max(cy, 1));
    wndObj->bmp = InitGdiObj(OBJ_BITMAP, surface);
    wndObj->hdc = new _SDC(hwnd);
    SelectObject(wndObj->hdc, wndObj->bmp);
    return TRUE;
}

static void AppendIconData(std::vector<uint32_t> &buf, HICON hIcon)
{
    ICONINFO info;
    if (GetIconInfo(hIcon, &info))
    {
        BITMAP bm;
        GetObject(info.hbmColor, sizeof(bm), &bm);
        if (bm.bmBitsPixel == 32)
        {
            int pos = buf.size();
            buf.resize(pos + bm.bmWidth * bm.bmHeight + 2);
            uint32_t *data = buf.data() + pos;
            data[0] = bm.bmWidth;
            data[1] = bm.bmHeight;
            memcpy(data + 2, bm.bmBits, bm.bmWidth * bm.bmHeight * 4);
        }
        if (info.hbmColor)
            DeleteObject(info.hbmColor);
        if (info.hbmMask)
            DeleteObject(info.hbmMask);
    }
}

static void WIN_UpdateIcon(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        std::vector<uint32_t> buf;
        AppendIconData(buf, wndObj->iconSmall);
        AppendIconData(buf, wndObj->iconBig);
        if (!buf.empty())
        {
            xcb_change_property(wndObj->mConnection->connection, XCB_PROP_MODE_REPLACE, hWnd, wndObj->mConnection->atoms._NET_WM_ICON, XCB_ATOM_CARDINAL, 32, buf.size(), buf.data());
        }
        else
        {
            xcb_delete_property(wndObj->mConnection->connection, hWnd, wndObj->mConnection->atoms._NET_WM_ICON);
        }
        xcb_flush(wndObj->mConnection->connection);
    }
}

struct MotifWmHints
{
    uint32_t flags, functions, decorations;
    uint32_t input_mode;
    uint32_t status;
};

enum
{
    MWM_HINTS_FUNCTIONS = (1L << 0),

    MWM_FUNC_ALL = (1L << 0),
    MWM_FUNC_RESIZE = (1L << 1),
    MWM_FUNC_MOVE = (1L << 2),
    MWM_FUNC_MINIMIZE = (1L << 3),
    MWM_FUNC_MAXIMIZE = (1L << 4),
    MWM_FUNC_CLOSE = (1L << 5),

    MWM_HINTS_DECORATIONS = (1L << 1),

    MWM_DECOR_ALL = (1L << 0),
    MWM_DECOR_BORDER = (1L << 1),
    MWM_DECOR_RESIZEH = (1L << 2),
    MWM_DECOR_TITLE = (1L << 3),
    MWM_DECOR_MENU = (1L << 4),
    MWM_DECOR_MINIMIZE = (1L << 5),
    MWM_DECOR_MAXIMIZE = (1L << 6),

    MWM_HINTS_INPUT_MODE = (1L << 2),

    MWM_INPUT_MODELESS = 0L,
    MWM_INPUT_PRIMARY_APPLICATION_MODAL = 1L,
    MWM_INPUT_FULL_APPLICATION_MODAL = 3L
};

static MotifWmHints getMotifWmHints(SConnection *c, HWND window)
{
    MotifWmHints hints;

    xcb_get_property_cookie_t get_cookie = xcb_get_property_unchecked(c->connection, 0, window, c->atoms._MOTIF_WM_HINTS, c->atoms._MOTIF_WM_HINTS, 0, 20);

    xcb_get_property_reply_t *reply = xcb_get_property_reply(c->connection, get_cookie, NULL);

    if (reply && reply->format == 32 && reply->type == c->atoms._MOTIF_WM_HINTS)
    {
        hints = *((MotifWmHints *)xcb_get_property_value(reply));
    }
    else
    {
        hints.flags = 0L;
        hints.functions = MWM_FUNC_ALL;
        hints.decorations = MWM_DECOR_ALL;
        hints.input_mode = 0L;
        hints.status = 0L;
    }

    free(reply);

    return hints;
}

static void setMotifWmHints(SConnection *c, HWND window, const MotifWmHints &hints)
{
    if (hints.flags != 0l)
    {
        xcb_change_property(c->connection, XCB_PROP_MODE_REPLACE, window, c->atoms._MOTIF_WM_HINTS, c->atoms._MOTIF_WM_HINTS, 32, 5, &hints);
    }
    else
    {
        xcb_delete_property(c->connection, window, c->atoms._MOTIF_WM_HINTS);
    }
}

static void setMotifWindowFlags(SConnection *c, HWND hWnd, DWORD dwStyle, DWORD dwExStyle)
{
    MotifWmHints mwmhints;
    mwmhints.flags = MWM_HINTS_DECORATIONS | MWM_HINTS_FUNCTIONS;
    mwmhints.functions = MWM_FUNC_RESIZE | MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE;
    mwmhints.decorations = 0;
    mwmhints.input_mode = 0L;
    mwmhints.status = 0L;

    if (dwStyle & WS_CAPTION)
    {
        mwmhints.flags |= MWM_HINTS_DECORATIONS;
        mwmhints.functions = MWM_FUNC_CLOSE | MWM_FUNC_MOVE;
        mwmhints.decorations |= MWM_DECOR_TITLE;
        if (dwStyle & WS_MINIMIZEBOX)
        {
            mwmhints.decorations |= MWM_DECOR_MINIMIZE;
        }
        if (dwStyle & WS_MAXIMIZEBOX)
        {
            mwmhints.decorations |= MWM_DECOR_MAXIMIZE;
        }
        if (dwStyle & WS_SYSMENU)
        {
            mwmhints.decorations |= MWM_DECOR_MENU;
        }
    }

    if (dwStyle & WS_SIZEBOX)
    {
        mwmhints.decorations |= MWM_DECOR_RESIZEH;
    }
    setMotifWmHints(c, hWnd, mwmhints);
}

static void SetWindowPosHint(SConnection *c, HWND hWnd, int x, int y, int cx, int cy)
{
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_size_hints_set_position(&hints, true, x, y);
    if (cx > 0 && cy > 0)
        xcb_size_hints_set_size(&hints, true, cx, cy);
    xcb_size_hints_set_win_gravity(&hints, XCB_GRAVITY_STATIC);
    xcb_set_wm_normal_hints(c->connection, hWnd, &hints);
}

static BOOL GetScrollBarRect(HWND hWnd, UINT uSb, RECT *pRc){
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    memset(pRc,0,sizeof(RECT));
    if(!(wndObj->showSbFlags & uSb))
        return FALSE;
    if(uSb != SB_VERT && uSb != SB_HORZ){
        memset(pRc,0,sizeof(RECT));
        return FALSE;
    }
    *pRc = wndObj->rc;
    OffsetRect(pRc,-pRc->left,-pRc->top);
    if (wndObj->dwStyle & WS_BORDER) {
        int cxEdge = GetSystemMetrics(SM_CXEDGE);
        int cyEdge = GetSystemMetrics(SM_CYEDGE);
        InflateRect(pRc, -cxEdge, -cyEdge);
    }
    int cxHscroll = GetSystemMetrics(SM_CXVSCROLL);
    int cyVscroll = GetSystemMetrics(SM_CYHSCROLL);
    if(uSb==SB_VERT){
        if(wndObj->showSbFlags & SB_HORZ)
            pRc->bottom -= cxHscroll;
        pRc->left = pRc->right- cyVscroll;
    }else{
        if(wndObj->showSbFlags & SB_VERT)
            pRc->right -= cyVscroll;
        pRc->top = pRc->bottom-cxHscroll;
    }
    return pRc->left < pRc->right && pRc->top<pRc->bottom;
}

static RECT GetScrollBarPartRect(BOOL bVert, const SCROLLINFO * pSi, int iPart, LPCRECT rcAll){
    int cx = GetSystemMetrics(bVert?SM_CXVSCROLL:SM_CYHSCROLL);
    int nThumbMin = GetSystemMetrics(bVert? SM_CYMINTRACK : SM_CXMINTRACK);

    __int64 nTrackPos = pSi->nTrackPos;
    RECT rcRet={0, 0, cx, 0};

    if (pSi->nPage == 0)
        return rcRet;
    int nMax = pSi->nMax;
    if (nMax < pSi->nMin + (int)pSi->nPage - 1)
        nMax = pSi->nMin + pSi->nPage - 1;

    if (nTrackPos == -1)
        nTrackPos = pSi->nPos;
    int nLength = bVert ? (rcAll->bottom-rcAll->top) : (rcAll->right-rcAll->left);
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
        InflateRect(&rcRet,0,-nArrowHei);
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
    OffsetRect(&rcRet,rcAll->left,rcAll->top);
    return rcRet;
}

static int ScrollBarHitTest(BOOL bVert, const SCROLLINFO * pSi, LPCRECT rcAll,POINT pt){
    RECT rc;
    const int parts[]={SB_THUMBTRACK,SB_LINEUP,SB_LINEDOWN,SB_PAGEUP,SB_PAGEDOWN};
    for(int i=0, c=ARRAYSIZE(parts);i<c;i++){
        RECT rc = GetScrollBarPartRect(bVert,pSi,parts[i],rcAll);
        if(PtInRect(&rc,pt))
            return parts[i];
    }
    return -1;    
}

static void RedrawNcRect(HWND hWnd, const RECT* lpRect) {
    if(IsRectEmpty(lpRect))
        return;
    HRGN rgn = CreateRectRgnIndirect(lpRect);
    SendMessageA(hWnd,WM_NCPAINT,(WPARAM)rgn,0);
    DeleteObject(rgn);
}

BOOL InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
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
    HRGN rgn = CreateRectRgnIndirect(lpRect);
    CombineRgn(wndObj->invalid.hRgn, wndObj->invalid.hRgn, rgn, RGN_OR);
    DeleteObject(rgn);

    wndObj->invalid.bErase = wndObj->invalid.bErase || bErase;
    // 发送曝光事件
    xcb_connection_t* connection = wndObj->mConnection->connection;
    xcb_expose_event_t expose_event;
    expose_event.response_type = XCB_EXPOSE;
    expose_event.window = hWnd;
    expose_event.x = 0;
    expose_event.y = 0;
    expose_event.width = 0;
    expose_event.height = 0;
    xcb_send_event(connection, false, hWnd, XCB_EVENT_MASK_EXPOSURE, (const char*)&expose_event);
    xcb_flush(connection);
    return TRUE;
}

static void SetWindowTransparent(HWND hWnd,BOOL bTransparent) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    BOOL transparent = (wndObj->dwExStyle & WS_EX_TRANSPARENT) != 0;
    if (!(transparent ^ bTransparent) || !wndObj->mConnection->hasXFixes())
        return;
    xcb_rectangle_t rectangle;

    xcb_rectangle_t* rect = 0;
    int nrect = 0;

    if (!bTransparent) {
        rectangle.x = 0;
        rectangle.y = 0;
        rectangle.width = wndObj->rc.right-wndObj->rc.left;
        rectangle.height = wndObj->rc.bottom - wndObj->rc.top;
        rect = &rectangle;
        nrect = 1;
    }

    xcb_xfixes_region_t region = xcb_generate_id(wndObj->mConnection->connection);
    xcb_xfixes_create_region(wndObj->mConnection->connection, region, nrect, rect);
    xcb_xfixes_set_window_shape_region_checked(wndObj->mConnection->connection, hWnd, XCB_SHAPE_SK_INPUT, 0, 0, region);
    xcb_xfixes_destroy_region(wndObj->mConnection->connection, region);

    if (bTransparent)
        wndObj->dwExStyle |= WS_EX_TRANSPARENT;
    else
        wndObj->dwExStyle &= ~WS_EX_TRANSPARENT;
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
    pWnd->tid = pthread_self();
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
        pWnd->dwStyle &=~WS_BORDER; //remove border
    }
    pWnd->dwExStyle = cs->dwExStyle;
    pWnd->hInstance = module;
    pWnd->clsAtom = clsAtom;
    pWnd->iconSmall = pWnd->iconBig = nullptr;
    pWnd->parent = hParent;
    pWnd->winproc = clsInfo.lpfnWndProc;
    pWnd->rc.left = cs->x;
    pWnd->rc.top = cs->y;
    pWnd->rc.right = cs->x + cs->cx;
    pWnd->rc.bottom = cs->y + cs->cy;
    pWnd->showSbFlags |= (cs->style & WS_HSCROLL)?SB_HORZ:0;
    pWnd->showSbFlags |= (cs->style & WS_VSCROLL)?SB_VERT:0;
    pWnd->visualId = conn->screen->root_visual;
    int depth = XCB_COPY_FROM_PARENT;
    if ((pWnd->dwExStyle & WS_EX_COMPOSITED) && !(pWnd->dwStyle & WS_CHILD) && conn->IsScreenComposited())
    {
        pWnd->dwStyle &= ~WS_CAPTION;
        pWnd->visualId = conn->rgba_visual->visual_id;
        depth = 32;
    }
    else {
        pWnd->dwExStyle &= ~WS_EX_COMPOSITED;
    }

    HWND hWnd = xcb_generate_id(conn->connection);

    xcb_colormap_t cmap = xcb_generate_id(conn->connection);
    xcb_create_colormap(conn->connection, XCB_COLORMAP_ALLOC_NONE, cmap, conn->screen->root, pWnd->visualId);

    const uint32_t evt_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

    const uint32_t mask = XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL| XCB_CW_OVERRIDE_REDIRECT | XCB_CW_SAVE_UNDER | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

    const uint32_t values[] = { 
                                XCB_NONE,//XCB_CW_BACK_PIXMAP
                                0,//XCB_CW_BORDER_PIXEL                                
                                (cs->dwExStyle&WS_EX_TOOLWINDOW)?1u:0,// XCB_CW_OVERRIDE_REDIRECT                                
                                0,// XCB_CW_SAVE_UNDER
                                evt_mask,// XCB_CW_EVENT_MASK
                                cmap//XCB_CW_COLORMAP
    };
    xcb_window_class_t wndCls = XCB_WINDOW_CLASS_INPUT_OUTPUT;
    if (isMsgWnd)
    {
        hParent = conn->screen->root;
        wndCls = XCB_WINDOW_CLASS_INPUT_ONLY;
    }else if (!(cs->style & WS_CHILD) || !hParent)
        hParent = conn->screen->root;
    xcb_void_cookie_t cookie = xcb_create_window_checked(conn->connection, depth, hWnd, hParent, cs->x, cs->y, std::max(cs->cx, 1u), std::max(cs->cy, 1u), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, pWnd->visualId, mask, values);
    xcb_free_colormap(conn->connection, cmap);

    xcb_generic_error_t *err = xcb_request_check(conn->connection, cookie);
    if (err)
    {
        printf("xcb_create_window failed, errcode=%d\n", err->error_code);
        free(err);
        delete pWnd;
        return 0;
    }
    xcb_change_window_attributes(conn->connection, hWnd, mask, values);

    {
        const uint32_t vals[2] = { (uint32_t)cs->x, (uint32_t)cs->y };
        xcb_configure_window(conn->connection, hWnd, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, vals);
    }
    {
        const uint32_t vals[2] = { cs->cx, cs->cy };
        xcb_configure_window(conn->connection, hWnd, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, vals);
    }
    SetWindowPosHint(conn, hWnd, cs->x, cs->y, cs->cx, cs->cy);

    xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, hWnd, conn->atoms.WM_PROTOCOLS, XCB_ATOM_ATOM, 32, 1, &conn->atoms.WM_DELETE_WINDOW);

    // set the PID to let the WM kill the application if unresponsive
    uint32_t pid = getpid();
    xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, hWnd, conn->atoms._NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, &pid);
    xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, hWnd, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, pWnd->title.length(), pWnd->title.c_str());

    setMotifWindowFlags(conn, hWnd, pWnd->dwStyle, pWnd->dwExStyle);
    {
        /* Add XEMBED info; this operation doesn't initiate the embedding. */
        uint32_t data[] = { XEMBED_VERSION, XEMBED_MAPPED };
        xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, hWnd, conn->atoms._XEMBED_INFO, conn->atoms._XEMBED_INFO, 32, 2, (void *)data);
    }
    xcb_flush(conn->connection);
    WndMgr::insertWindow(hWnd, pWnd);

    SetWindowLongPtrA(hWnd, GWLP_ID, cs->hMenu);
    if(!(cs->style & WS_CHILD))
        SetParent(hWnd, cs->hwndParent);
    InitWndDC(hWnd, cs->cx, cs->cy);
    
    if (0 == SendMessage(hWnd, WM_NCCREATE, 0, (LPARAM)cs) || 0 != SendMessage(hWnd, WM_CREATE, 0, (LPARAM)cs))
    {
        xcb_destroy_window(conn->connection, hWnd);
        xcb_flush(conn->connection);
        WndMgr::freeWindow(hWnd);
        hWnd = 0;
    }
    if (clsInfo.hIconSm)
    {
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)clsInfo.hIconSm);
    }
    if (clsInfo.hIcon)
    {
        SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)clsInfo.hIconSm);
    }
    if (isTransparent) 
    {
        SetWindowTransparent(hWnd, TRUE);
    }
    if (!isMsgWnd && cs->style & WS_VISIBLE)
    {
        ShowWindow(hWnd, SW_SHOW);
        InvalidateRect(hWnd, NULL, TRUE);
    }

    return hWnd;
}

HWND WINAPI CreateWindowA(LPCSTR lpClassName,
    LPCSTR lpWindowName,
    DWORD dwStyle,
    int x,
    int y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam
) {
    return CreateWindowExA(0, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

HWND WINAPI CreateWindowW(LPCWSTR lpClassName,
    LPCWSTR lpWindowName,
    DWORD dwStyle,
    int x,
    int y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam
) {
    return CreateWindowExW(0, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
}

/***********************************************************************
 *		CreateWindowExW (USER32.@)
 */
HWND WINAPI CreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data)
{
    std::string strClsName,strWndName;
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
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return GetAtomNameA(wndObj->clsAtom, lpClassName, nMaxCount);
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
    if (!IsWindow(hWnd))
        return FALSE;
    SendMessage(hWnd, WM_DESTROY, 0, 0);
    return TRUE;
}

BOOL WINAPI IsWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        return TRUE;
    SConnection *conn = SConnMgr::instance()->getConnection();
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(conn->connection, hWnd);
    xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(conn->connection, cookie, NULL);
    if (!reply)
        return FALSE;
    free(reply);
    return TRUE;
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
    if (wndObj->dwStyle & WS_BORDER) {
        ppt->x += cxEdge;
        ppt->y += cyEdge;
    }
    if (!(wndObj->dwStyle & WS_CHILD))
        return TRUE;
    HWND hParent = wndObj->parent;
    while (hParent) {
        WndObj wndObj = WndMgr::fromHwnd(hParent);
        ppt->x += wndObj->rc.left;
        ppt->y += wndObj->rc.top;
        if (wndObj->dwStyle & WS_BORDER) {
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
    if (wndObj->dwStyle & WS_BORDER) {
        ppt->x -= cxEdge;
        ppt->y -= cyEdge;
    }
    if (!(wndObj->dwStyle & WS_CHILD))
        return TRUE;

    HWND hParent = wndObj->parent;
    while (hParent) {
        WndObj wndObj = WndMgr::fromHwnd(hParent);
        ppt->x -= wndObj->rc.left;
        ppt->y -= wndObj->rc.top;
        if (wndObj->dwStyle & WS_BORDER) {
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
    //SLOG_FMTD("SetCapture hWnd=%d",(int)hWnd);
    return oldCapture;
}

BOOL ReleaseCapture()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    //SLOG_FMTD("ReleaseCapture hWnd=%d",(int)conn->GetCapture());
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

    POINT ptClick;
    if (!wndObj->mConnection->GetCursorPos(&ptClick))
        return -1;
    if (!(htCode >= HTCAPTION && htCode <= HTBOTTOMRIGHT) || htCode == HTVSCROLL ||htCode == HTHSCROLL)
        return -2;

    SLOG_STMI() << "HandleNcTestCode,code="<<htCode;
    wndObj->mConnection->SetTimerBlock(true);
    RECT rcWnd = wndObj->rc;
    BOOL bQuit = FALSE;
    SetCapture(hWnd);
    for (; !bQuit;)
    {
        MSG msg;
        if (!WaitMessage())
            continue;
        while (PeekMessage(&msg, hWnd, 0, 0, TRUE))
        {
            if (msg.message == WM_QUIT)
            {
                SLOG_STMI() << "HandleNcTestCode,WM_QUIT";
                bQuit = TRUE;
                wndObj->mConnection->postMsg(msg.hwnd, msg.message, msg.wParam, msg.lParam);
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
    ReleaseCapture();

    wndObj->mConnection->SetTimerBlock(false);
    SLOG_STMI() << "HandleNcTestCode,Quit";

    return 0;
}

static void UpdateWindowCursor(WndObj &wndObj, HWND hWnd, int htCode)
{
    if(htCode == HTCLIENT){
        WNDCLASSEXA wc;
        GetClassInfoExA(wndObj->hInstance, MAKEINTRESOURCE(wndObj->clsAtom), &wc);
        if (wc.hCursor)
            SetCursor(wc.hCursor);
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
            }
            static LPCSTR prev_id = 0;
            if (cursorId != prev_id)
            {
                printf("set cursor id=%d\n", (int)(UINT_PTR)cursorId);
                prev_id = cursorId;
            }
            SetCursor(LoadCursor(wndObj->hInstance, cursorId));
    }
}

static BOOL ActiveWindow(HWND hWnd, BOOL bMouseActive, UINT msg, UINT htCode)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if (wndObj->dwStyle & WS_CHILD)
    {
        do
        {
            HWND hParent = GetParent(hWnd);
            if (!hParent)
                break;
            WndObj wndParent = WndMgr::fromHwnd(hParent);
            if (!wndParent)
                return 0;
            if ((wndParent->dwExStyle & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW)) != 0 || (wndParent->dwStyle & WS_DISABLED) != 0)
                return 0;
            hWnd = hParent;
            wndObj = wndParent;
        } while (1);
    }
    BOOL bRet = FALSE;
    if ((wndObj->dwExStyle & (WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW)) == 0 && (wndObj->dwStyle & WS_DISABLED) == 0)
    {
        UINT maRet = 0;
        if (bMouseActive)
        {
            maRet = SendMessage(hWnd, WM_MOUSEACTIVATE, (WPARAM)hWnd, MAKELPARAM(htCode, msg));
        }
        if (!bMouseActive || maRet == MA_ACTIVATE || maRet == MA_ACTIVATEANDEAT)
        {
            HWND oldActive = wndObj->mConnection->GetActiveWnd();
            if (!wndObj->mConnection->SetActiveWindow(hWnd))
                return FALSE;
            PostMessage(hWnd, WM_ACTIVATE, bMouseActive ? WA_CLICKACTIVE : WA_ACTIVE, oldActive);
            if (IsWindow(oldActive))
            {
                PostMessage(oldActive, WM_ACTIVATE, WA_INACTIVE, hWnd);
            }
        }
        bRet = maRet == MA_ACTIVATEANDEAT || maRet == MA_NOACTIVATEANDEAT;
    }
    return bRet;
}

static void _DrawCaret(HWND hWnd, WndObj& wndObj) {
    const SConnection::CaretInfo* info = wndObj->mConnection->GetCaretInfo();
    assert(info->hOwner == hWnd);
    HDC hdc = GetDC(hWnd);
    if (info->hBmp) {
        BITMAP bm;
        GetObjectA(info->hBmp, sizeof(bm), &bm);
        HDC memdc = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(memdc, info->hBmp);
        BitBlt(hdc, info->x, info->y, bm.bmWidth,bm.bmHeight, memdc, 0, 0, R2_NOT);
        SelectObject(memdc, old);
        DeleteDC(memdc);
    }
    else {
        RECT rc = { info->x,info->y,info->x + info->nWidth,info->y + info->nHeight };
        InvertRect(hdc, &rc);
    }
    ReleaseDC(hWnd, hdc);
}

LRESULT CallWindowProc(WNDPROC proc, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return proc(hWnd,msg,wp,lp);
}

static BOOL CALLBACK Enum4DestroyOwned(HWND hwnd, LPARAM lParam) {
    HWND hTest = (HWND)lParam;
    if (GetParent(hwnd) == hTest) {
        DestroyWindow(hwnd);
    }
    return TRUE;
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
			wndObj->flags |= fMap;
		else
			wndObj->flags &= ~fMap;
		if (wp && wndObj->byAlpha != 0xff)
		{
			Sleep(50); // todo: fix it later.
			wndObj->mConnection->SetWindowOpacity(hWnd, wndObj->byAlpha);
		}
		return 0;
    case UM_XDND_DRAG_ENTER:
    {
        SLOG_STMI() << "UM_XDND_DRAG_ENTER!";
        DragEnterData* data = (DragEnterData*)lp;
        if (!wndObj->dropTarget || !wndObj->dragData)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        SDataObjectProxy* dragData = (SDataObjectProxy*)wndObj->dragData;
        DWORD grfKeyState = 0;//todo:hjx
        wndObj->dropTarget->DragEnter(wndObj->dragData, grfKeyState, data->pt, &dragData->m_dwEffect);
        return 0;
    }
    case UM_XDND_DRAG_LEAVE:
    {
        SLOG_STMI() << "UM_XDND_DRAG_LEAVE!";

        if (!wndObj->dropTarget || !wndObj->dragData)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        HRESULT hr = wndObj->dropTarget->DragLeave();
        wndObj->dragData->Release();
        wndObj->dragData = NULL;
        SLOG_STMI() << "UM_XDND_DRAG_LEAVE! set dragData to null";
        return 0;
    }
    case UM_XDND_DRAG_OVER:
    {
        //SLOG_STMI() << "UM_XDND_DRAG_OVER!";
        DragOverData* data = (DragOverData*)lp;
        if (!wndObj->dropTarget)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        SDataObjectProxy* dragData = (SDataObjectProxy*)wndObj->dragData;
        dragData->m_dwKeyState = data->dwKeyState;
        HRESULT hr = wndObj->dropTarget->DragOver(dragData->m_dwKeyState, data->pt, &dragData->m_dwEffect);
        wndObj->mConnection->SendXdndStatus(hWnd,dragData->m_hSource, hr == S_OK, dragData->m_dwEffect);
        return 0;
    }
    case UM_XDND_DRAG_DROP:
    {
        SLOG_STMI() << "UM_XDND_DRAG_DROP!";
        DragDropData* data = (DragDropData*)lp;
        if (!wndObj->dropTarget || !wndObj->dragData)
        {
            SLOG_STMW() << "should not run into here!";
            return 1;
        }
        SDataObjectProxy* dragData =(SDataObjectProxy *) wndObj->dragData;
        DWORD dwEffect = wp;
        HRESULT hr = wndObj->dropTarget->Drop(wndObj->dragData, dragData->m_dwKeyState, data->pt, &dwEffect);
        wndObj->mConnection->SendXdndFinish(hWnd, dragData->m_hSource, hr == S_OK, dwEffect);
        wndObj->dragData->Release();
        wndObj->dragData = NULL;
        SLOG_STMI() << "UM_XDND_DRAG_DROP, set dragData to null";
        return 0;
    }
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
		if (!bSkipMsg && GetCapture() != hWnd) {
			POINT pt = { GET_X_LPARAM(lp),GET_Y_LPARAM(lp) };
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			if (!PtInRect(&rcClient, pt)) {
				CallWindowProcPriv(proc, hWnd, msg + WM_NCLBUTTONDOWN - WM_LBUTTONDOWN, wp, lp);
				bSkipMsg = TRUE;
			}
		}
	}
	break;
	case WM_MOUSEMOVE:
	{
		POINT pt;
		wndObj->mConnection->GetCursorPos(&pt);
		int htCode = proc(hWnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
		UpdateWindowCursor(wndObj, hWnd, htCode);
		if (htCode != wndObj->htCode) {
			int oldHtCode = wndObj->htCode;
			wndObj->htCode = htCode;
			if (htCode != HTCLIENT && oldHtCode == HTCLIENT) {
				CallWindowProcPriv(proc, hWnd, WM_NCMOUSEHOVER, htCode, MAKELPARAM(pt.x, pt.y));
				bSkipMsg = TRUE;
			}
			if (htCode == HTCLIENT && oldHtCode != HTCLIENT) {
				CallWindowProcPriv(proc, hWnd, WM_NCMOUSELEAVE, htCode, MAKELPARAM(pt.x, pt.y));
				bSkipMsg = TRUE;
			}
		}
		else if (htCode != HTCLIENT) {
			CallWindowProcPriv(proc, hWnd, WM_NCMOUSEMOVE, htCode, MAKELPARAM(pt.x, pt.y));
			bSkipMsg = TRUE;
		}
	}
	break;
	case WM_MOUSEHOVER:
	{
		if (!wndObj->hoverInfo.uHoverState == HS_Leave && (wndObj->hoverInfo.dwFlags & TME_HOVER))
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
		if (wndObj->htCode != HTCLIENT) {
			POINT pt;
			wndObj->mConnection->GetCursorPos(&pt);
			CallWindowProcPriv(proc, hWnd, WM_NCMOUSELEAVE, HTNOWHERE, MAKELPARAM(pt.x, pt.y));
		}
		wndObj->htCode = HTNOWHERE;
		if (!wndObj->hoverInfo.uHoverState != HS_Leave)
		{
			wndObj->hoverInfo.uHoverState = HS_Leave;
			if (!wndObj->hoverInfo.dwFlags & TME_LEAVE)
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
				CallWindowProcPriv(proc, hWnd, WM_MOUSEHOVER, 0, MAKELPARAM(ptCursor.x, ptCursor.y)); // delay send mouse hover
			}
			bSkipMsg = TRUE;
		}
		if (wp == SConnection::TM_CARET && IsWindowVisible(hWnd)) {
			_DrawCaret(hWnd, wndObj);
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
	case WM_MOVE:
		OffsetRect(&wndObj->rc, GET_X_LPARAM(lp) - wndObj->rc.left, GET_Y_LPARAM(lp) - wndObj->rc.top);
		break;
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
			CallWindowProcPriv(proc, hWnd, WM_ERASEBKGND, (WPARAM)hdc, 0);
			wndObj->invalid.bErase = FALSE;
		}
		ReleaseDC(hWnd, hdc);
		break;
	}
	case UM_STATE:
		switch (wp)
		{
		case SIZE_MINIMIZED:
			wndObj->state = WS_Minimized;
			break;
		case SIZE_MAXIMIZED:
			wndObj->state = WS_Maximized;
			break;
		case SIZE_RESTORED:
			wndObj->state = WS_Normal;
			lp = MAKELPARAM(wndObj->rc.right - wndObj->rc.left, wndObj->rc.bottom - wndObj->rc.top);
			CallWindowProcPriv(proc, hWnd, WM_SIZE, 0, lp); // call size again
			break;
		}
		return 1;
	case WM_SIZE:
		wp = wndObj->state;
		SIZE sz = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		if (wndObj->bmp && (sz.cx != wndObj->rc.right - wndObj->rc.left || sz.cy != wndObj->rc.bottom - wndObj->rc.top))
		{
			wndObj->rc.right = wndObj->rc.left + sz.cx;
			wndObj->rc.bottom = wndObj->rc.top + sz.cy;
			cairo_xcb_surface_set_size((cairo_surface_t*)GetGdiObjPtr(wndObj->bmp), std::max(sz.cx, 1), std::max(sz.cy, 1));
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
		{//convert pos to remove border size
			POINT pt = { GET_X_LPARAM(lp2),GET_Y_LPARAM(lp2) };
			pt.x -= GetSystemMetrics(SM_CXBORDER);
			pt.y -= GetSystemMetrics(SM_CYBORDER);
			lp2 = MAKELPARAM(pt.x, pt.y);
		}
		ret = proc(hWnd, msg, wp, lp2);
		if (msg == WM_PAINT) {
			if (wndObj->bCaretVisible) {
				_DrawCaret(hWnd, wndObj);
			}
			if (lp) proc(hWnd, WM_NCPAINT, lp, 0);//call ncpaint
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
        //auto destroy all popup that owned by this
        EnumWindows(Enum4DestroyOwned, hWnd);
		//auto destory all children
		HWND hChild = GetWindow(hWnd, GW_CHILDLAST);
		while (hChild) {
			DestroyWindow(hChild);
			hChild = GetWindow(hWnd, GW_CHILDLAST);
		}
		if (wndObj->mConnection->GetCaretInfo()->hOwner == hWnd) {
			DestroyCaret();
		}
		CallWindowProcPriv(proc, hWnd, WM_NCDESTROY, 0, 0);
	}
	else if (msg == WM_NCDESTROY)
	{
		wndObj->bDestroyed = TRUE;
	}
	if (0 == --wndObj->msgRecusiveCount && wndObj->bDestroyed)
	{
		xcb_destroy_window(wndObj->mConnection->connection, hWnd);
		xcb_flush(wndObj->mConnection->connection);
		WndMgr::freeWindow(hWnd);
	}
	return ret;
}

SharedMemory *PostIpcMessage(SConnection *connCur, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, HANDLE &hEvt)
{
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(connCur->connection, hWnd);
    xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(connCur->connection, cookie, NULL);
    if (!reply)
        return nullptr;
    free(reply);

    uuid_t uuid;
    uuid_generate_random(uuid);
    std::string strName = IpcMsg::get_share_mem_name(uuid);
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
        ptr->lp = cds->cbData;
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
    hEvt = CreateEventA(nullptr, FALSE, FALSE, evtName.c_str());

    // the hWnd is valid window id.
    xcb_client_message_event_t client_msg_event = {
        XCB_CLIENT_MESSAGE,          //.response_type
        32,                          //.format
        0,                           //.sequence
        (xcb_window_t)hWnd,          //.window
        connCur->atoms.WM_WIN4XCB_IPC //.type
    };
    client_msg_event.data.data32[0] = msg;
    memcpy(client_msg_event.data.data32 + 1, uuid, sizeof(uuid));

    // Send the client message event
    xcb_send_event(connCur->connection, 0, hWnd, XCB_EVENT_MASK_NO_EVENT, (const char *)&client_msg_event);
    // Flush the request to the X server
    xcb_flush(connCur->connection);
    return shareMem;
}

static LRESULT _SendMessageTimeout(BOOL bWideChar, HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
    SConnection *connCur = SConnMgr::instance()->getConnection();
    WndObj pWnd = WndMgr::fromHwnd(hWnd);
    if (!pWnd)
    { // not the same process. send ipc message
        if(bWideChar){
            TRACE("ipc msg not support wide char api! msg=%u\n",msg);
            return 0;
        }
        HANDLE hEvt = INVALID_HANDLE_VALUE;
        SharedMemory *shareMem = PostIpcMessage(connCur, hWnd, msg, wp, lp, hEvt);
        if (!shareMem)
        {
            return -1;
        }
        int ret = WAIT_FAILED;
        if (fuFlags & SMTO_BLOCK)
        {
            WaitForSingleObject(hEvt, uTimeout);
        }
        else
        {
            MSG msg;
            for (;;)
            {
                ret = connCur->waitMutliObjectAndMsg(&hEvt, 1, uTimeout, FALSE, QS_ALLINPUT);
                if (ret == WAIT_OBJECT_0 + 1)
                {
                    if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
                    {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
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
            MsgLayout *ptr = (MsgLayout *)shareMem->buffer();
            *lpdwResult = ptr->ret;
        }

        delete shareMem;
        CloseHandle(hEvt);
        return -1;
    }

    SConnection *connWnd = SConnMgr::instance()->getConnection(pWnd->tid);
    if (!connWnd)
        return -1;
    if (connWnd == connCur)
    {
        // same thread,call wndproc directly.
        WNDPROC wndProc = (WNDPROC)GetWindowLongPtrA(hWnd, GWL_WNDPROC);
        assert(wndProc);
        if(bWideChar){
            MSG msgWrap;
            msgWrap.message = msg;
            msgWrap.wParam = wp;
            msgWrap.lParam = lp;
            *lpdwResult = CallWindowProcPriv(wndProc, hWnd,WM_MSG_W2A,0,(LPARAM)&msgWrap);
        }else{
            *lpdwResult = CallWindowProcPriv(wndProc, hWnd, msg, wp, lp);
        }
        return 1;
    }
    else
    {

        HANDLE hEvt = CreateEventA(nullptr, FALSE, FALSE, nullptr); // todo:hjx no name event, only for inter thread msg now
        SendReply *reply = new SendReply(hEvt);
        connWnd->postMsg2(bWideChar, hWnd, msg, wp, lp, reply);
        if (fuFlags & SMTO_BLOCK)
        {
            WaitForSingleObject(hEvt, uTimeout);
        }
        else
        {
            MSG msg;
            for (;;)
            {
                int ret = connCur->waitMutliObjectAndMsg(&hEvt, 1, uTimeout, FALSE, QS_ALLINPUT);
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
        return 1;
    }
}


LRESULT SendMessageTimeoutW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult) {
    return _SendMessageTimeout(TRUE,hWnd, msg, wp, lp, fuFlags, uTimeout, lpdwResult);
}

LRESULT SendMessageTimeoutA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
    return _SendMessageTimeout(FALSE,hWnd, msg, wp, lp, fuFlags, uTimeout, lpdwResult);
}

#define DEF_SENDMSG_TIMEOUT 5000
LRESULT SendMessageA(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DWORD_PTR ret = 0;
    SendMessageTimeoutA(hWnd, msg, wp, lp, 0, DEF_SENDMSG_TIMEOUT, &ret);
    return ret;
}

LRESULT SendMessageW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD_PTR ret = 0;
    SendMessageTimeoutW(hWnd, msg, wp, lp, 0, DEF_SENDMSG_TIMEOUT, &ret);
    return ret;
}

BOOL PostMessageW(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
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

BOOL _SendMessageCallback(BOOL bWideChar, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData)
{
    SConnection *connCur = SConnMgr::instance()->getConnection(GetCurrentThreadId());
    if (!connCur) // current thread must be a ui thread.
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
    {
        if(bWideChar){
            TRACE("ipc msg not support widechar, msg=%u\n",Msg);
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

BOOL SendMessageCallbackW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData) {
    return _SendMessageCallback(TRUE,hWnd, Msg, wParam, lParam, lpCallBack, dwData);
}

BOOL SendMessageCallbackA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpCallBack, ULONG_PTR dwData)
{
    return _SendMessageCallback(FALSE,hWnd, Msg, wParam, lParam, lpCallBack, dwData);
}

tid_t GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return 0;
    SConnection *pConn = wndObj->mConnection;
    if(lpdwProcessId){
        xcb_get_property_cookie_t cookie = xcb_get_property(pConn->connection, 0, hWnd, pConn->atoms._NET_WM_PID, XCB_ATOM_CARDINAL, 0, 1);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(pConn->connection, cookie, NULL);
        if (reply != NULL)
        {
            *lpdwProcessId = *(DWORD *)xcb_get_property_value(reply);
            free(reply);
        }
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
        return FALSE;
    WINDOWPOS wndPos;
    wndPos.hwnd = hWnd;
    wndPos.hwndInsertAfter = hWndInsertAfter;
    wndPos.x = x;
    wndPos.y = y;
    wndPos.cx = cx;
    wndPos.cy = cy;
    wndPos.flags = uFlags;
    SendMessage(hWnd, WM_WINDOWPOSCHANGING, 0, (LPARAM)&wndPos);
    if (wndPos.cx < 1)
        wndPos.cx = 1;
    if (wndPos.cy < 1)
        wndPos.cy = 1;
    SendMessage(hWnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wndPos);
    return TRUE;
}

static void onStyleChange(HWND hWnd, WndObj& wndObj, DWORD newStyle) {
    wndObj->dwStyle = newStyle;
    //update scrollbar flags.
    DWORD sbflag = 0;
    if (newStyle & WS_VSCROLL)
        sbflag |= SB_VERT;
    if (newStyle & WS_HSCROLL)
        sbflag |= SB_HORZ;
    if (wndObj->showSbFlags != sbflag) {
        wndObj->showSbFlags = sbflag;
        InvalidateRect(hWnd, &wndObj->rc, TRUE);
    }
}

static void onExStyleChange(HWND hWnd, WndObj& wndObj, DWORD newExStyle) {
    wndObj->dwExStyle = newExStyle;
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
            return get_win_data((char*)wndObj->extra + nIndex, size);
        }
        else {
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
        else {
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

DWORD WINAPI GetClassLongA(_In_ HWND hWnd, _In_ int nIndex) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj) return 0;
    ATOM atom = wndObj->clsAtom;
    return GetClassLongSize(atom, nIndex,sizeof(DWORD));
}

DWORD
WINAPI
GetClassLongW(
    _In_ HWND hWnd,
    _In_ int nIndex) {
    return GetClassLongA(hWnd, nIndex);
}

DWORD
WINAPI
SetClassLongA(
    _In_ HWND hWnd,
    _In_ int nIndex,
    _In_ LONG dwNewLong) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj) return 0;
    ATOM atom = wndObj->clsAtom;
    return SetClassLongSize(atom,nIndex,dwNewLong,sizeof(DWORD));
}

DWORD
WINAPI
SetClassLongW(
    _In_ HWND hWnd,
    _In_ int nIndex,
    _In_ LONG dwNewLong) {
    return SetClassLongA(hWnd, nIndex, dwNewLong);
}

ULONG_PTR
WINAPI
GetClassLongPtrA(
    _In_ HWND hWnd,
    _In_ int nIndex) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj) return 0;
    ATOM atom = wndObj->clsAtom;
    return GetClassLongSize(atom, nIndex, sizeof(ULONG_PTR));
}

ULONG_PTR
WINAPI
GetClassLongPtrW(
    _In_ HWND hWnd,
    _In_ int nIndex) {
    return GetClassLongPtrA(hWnd, nIndex);
}

ULONG_PTR
WINAPI
SetClassLongPtrA(
    _In_ HWND hWnd,
    _In_ int nIndex,
    _In_ LONG_PTR dwNewLong) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj) return 0;
    ATOM atom = wndObj->clsAtom;
    return SetClassLongSize(atom, nIndex, dwNewLong, sizeof(ULONG_PTR));
}

ULONG_PTR
WINAPI
SetClassLongPtrW(
    _In_ HWND hWnd,
    _In_ int nIndex,
    _In_ LONG_PTR dwNewLong) {
    return SetClassLongPtrA(hWnd, nIndex, dwNewLong);
}


BOOL CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    SConnection* pConn = SConnMgr::instance()->getConnection();
    if (hWnd) {
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
    SConnection* pConn = SConnMgr::instance()->getConnection();
    const SConnection::CaretInfo* caretInfo = pConn->GetCaretInfo();
    if (caretInfo->hOwner) {
        WndObj wndObj = WndMgr::fromHwnd(caretInfo->hOwner);
        if (wndObj && wndObj->bCaretVisible) {
            //clear caret
            _DrawCaret(caretInfo->hOwner, wndObj);
            wndObj->bCaretVisible = FALSE;
        }
    }
    return pConn->DestroyCaret();
}

BOOL HideCaret(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    SConnection* pConn = SConnMgr::instance()->getConnection();
    if (wndObj->mConnection != pConn)
        return FALSE;
    const SConnection::CaretInfo* info = pConn->GetCaretInfo();
    if (!pConn->HideCaret(hWnd))
        return FALSE;
    if (info->nVisible == 0) {
        if (wndObj->bCaretVisible) {
            //clear old caret
            _DrawCaret(hWnd, wndObj);
            wndObj->bCaretVisible = FALSE;
        }
    }
    return TRUE;
}

BOOL ShowCaret(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    SConnection* pConn = SConnMgr::instance()->getConnection();
    if (wndObj->mConnection != pConn)
        return FALSE;
    if (!pConn->ShowCaret(hWnd))
        return FALSE;
    if (pConn->GetCaretInfo()->nVisible == 1) {
        //auto show caret
        _DrawCaret(hWnd, wndObj);
        wndObj->bCaretVisible = TRUE;
    }
    return TRUE;
}

BOOL SetCaretPos(int X, int Y)
{
    SConnection* pConn = SConnMgr::instance()->getConnection();
    const SConnection::CaretInfo* caretInfo = pConn->GetCaretInfo();
    if (caretInfo->hOwner) {
        WndObj wndObj = WndMgr::fromHwnd(caretInfo->hOwner);
        assert(wndObj);
        if (wndObj->bCaretVisible) {
            //clear caret
            _DrawCaret(caretInfo->hOwner, wndObj);
        }
    }
    return pConn->SetCaretPos(X,Y);
}

BOOL GetCaretPos(LPPOINT lpPoint)
{
    SConnection* pConn = SConnMgr::instance()->getConnection();
    return pConn->GetCaretPos(lpPoint);
}

void SetCaretBlinkTime(UINT  blinkTime) {
    SConnection* pConn = SConnMgr::instance()->getConnection();
    pConn->SetCaretBlinkTime(blinkTime);
}

UINT  GetCaretBlinkTime() {
    SConnection* pConn = SConnMgr::instance()->getConnection();
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
    return conn->screen->root;
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
    return TRUE;
}

HWND SetActiveWindow(HWND hWnd)
{
    return ActiveWindow(hWnd, FALSE, 0, 0);
}

HWND GetParent(HWND hWnd)
{
    return (HWND)GetWindowLongPtrA(hWnd, GWLP_HWNDPARENT);
}

HWND SetParent(HWND hWnd, HWND hParent)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!(wndObj->dwStyle & WS_CHILD))
    {
        xcb_icccm_set_wm_transient_for(wndObj->mConnection->connection, hWnd, hParent);
    }
    else {
        xcb_reparent_window(wndObj->mConnection->connection, hWnd, hParent, 0, 0);
    }
    xcb_flush(wndObj->mConnection->connection);
    return (HWND)SetWindowLongPtrA(hWnd, GWLP_HWNDPARENT, hParent);
}

BOOL GetCursorPos(LPPOINT ppt)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetCursorPos(ppt);
}

// 根据给定的 X 和 Y 坐标查找屏幕上的窗口
xcb_window_t getWindowFromPoint(xcb_connection_t* connection, int16_t x, int16_t y) {
    xcb_query_tree_reply_t* tree;
    xcb_query_tree_cookie_t tree_cookie;
    xcb_window_t root, parent, * children;
    int children_num;

    root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
    tree_cookie = xcb_query_tree(connection, root);
    tree = xcb_query_tree_reply(connection, tree_cookie, NULL);

    if (!tree) {
        return XCB_NONE;
    }

    children = xcb_query_tree_children(tree);
    children_num = xcb_query_tree_children_length(tree);

    for (int i = 0; i < children_num; i++) {
        xcb_get_geometry_reply_t* geo = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, children[i]), NULL);

        if (geo && x >= geo->x && y >= geo->y && x < geo->x + geo->width && y < geo->y + geo->height) {
            parent = children[i];
            free(geo);
            break;
        }

        free(geo);
    }

    free(tree);

    return parent;
}

static void XcbGeo2Rect(xcb_get_geometry_reply_t* geo, RECT* rc) {
    rc->left = geo->x;
    rc->top = geo->y;
    rc->right = geo->x + geo->width;
    rc->bottom = geo->y + geo->height;
}

static HWND _WindowFromPoint(xcb_connection_t * connection, xcb_window_t parent, POINT pt)
{
    xcb_query_tree_reply_t* tree;
    xcb_query_tree_cookie_t tree_cookie;
    xcb_window_t * children;
    HWND ret = XCB_NONE;
    int children_num;

    tree_cookie = xcb_query_tree(connection, parent);
    tree = xcb_query_tree_reply(connection, tree_cookie, NULL);

    if (!tree) {
        return XCB_NONE;
    }
    xcb_get_geometry_reply_t* geoParent = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, parent), NULL);
    if (!geoParent)
        return XCB_NONE;
    RECT rcParent;
    XcbGeo2Rect(geoParent, &rcParent);
    free(geoParent);
    if (!PtInRect(&rcParent, pt))
        return XCB_NONE;
    children = xcb_query_tree_children(tree);
    children_num = xcb_query_tree_children_length(tree);
    
    for (int i = children_num-1; i>=0; i--) {
        if (!IsWindowVisible(children[i]))
            continue;
        xcb_get_geometry_reply_t* geo = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, children[i]), NULL);
        if (!geo) continue;
        RECT rc;
        XcbGeo2Rect(geo, &rc);
        free(geo);
        if (PtInRect(&rc,pt)) {
            ret = children[i];
            break;
        }
    }
    free(tree);
    if (ret != XCB_NONE) {
        pt.x -= rcParent.left;
        pt.y -= rcParent.top;
        ret = _WindowFromPoint(connection, ret, pt);
    }
    else {
        ret = parent;
    }
    return ret;
}

HWND WindowFromPoint(POINT pt) {
    SConnection* pConn = SConnMgr::instance()->getConnection();
    return _WindowFromPoint(pConn->connection, pConn->screen->root, pt);
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
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetFocus();
}

HWND SetFocus(HWND hWnd)
{
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->SetFocus(hWnd);
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

int GetUpdateRgn(HWND hWnd,    // handle to window
    HRGN hRgn,    // handle to region  
    BOOL bErase   // erase state
) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return ERROR;
    return CombineRgn(hRgn,wndObj->invalid.hRgn,nullptr,RGN_COPY);
}

BOOL UpdateWindow(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if (wndObj->nPainting)
        return FALSE;
    RECT rcBox;
    GetRgnBox(wndObj->invalid.hRgn, &rcBox);
    if (IsRectEmpty(&rcBox))
        return FALSE;

    SendMessageA(hWnd, WM_PAINT, 0, 0);
    return TRUE;
}

BOOL GetClientRect(HWND hWnd, RECT *pRc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    *pRc = wndObj->rc;
    OffsetRect(pRc, -pRc->left, -pRc->top);
    if(wndObj->dwStyle & WS_BORDER){
        int cxEdge = GetSystemMetrics(SM_CXEDGE);
        int cyEdge = GetSystemMetrics(SM_CYEDGE);
        pRc->right -= 2 * cxEdge;
        pRc->bottom -= 2 * cyEdge;
    }
    if(wndObj->showSbFlags & SB_VERT)
        pRc->right-= GetSystemMetrics(SM_CXVSCROLL);
    if(wndObj->showSbFlags & SB_HORZ)
        pRc->bottom -= GetSystemMetrics(SM_CYHSCROLL);
    if(pRc->right<0)
        pRc->right=0;
    if(pRc->bottom<0)
        pRc->bottom=0;
    return TRUE;
}

static BOOL _GetWndRect(HWND hWnd, RECT* rc) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        *rc = wndObj->rc;
    else
    {
        SConnection* conn = SConnMgr::instance()->getConnection();
        xcb_get_geometry_cookie_t cookie = xcb_get_geometry(conn->connection, hWnd);
        xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(conn->connection, cookie, nullptr);
        if (!reply)
            return FALSE;
        rc->left = reply->x;
        rc->top = reply->y;
        rc->right = reply->x + reply->width;
        rc->bottom = reply->y + reply->height;
    }
    return TRUE;
}

BOOL GetWindowRect(HWND hWnd, RECT* rc)
{
    if (!_GetWndRect(hWnd, rc))
        return FALSE;

    for (;;) {
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

static void ChangeNetWmState(SConnection *conn, xcb_window_t wnd, bool bSet, xcb_atom_t one, xcb_atom_t two)
{
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.window = wnd;
    event.format = 32;
    event.sequence = 0;
    event.type = conn->atoms._NET_WM_STATE;
    event.data.data32[0] = bSet ? 1 : 0;
    event.data.data32[1] = one;
    event.data.data32[2] = two;
    event.data.data32[3] = event.data.data32[4] = 0;
    xcb_send_event(conn->connection, false, conn->screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);
    xcb_flush(conn->connection);
}

static void SendSysCommand(SConnection *conn, xcb_window_t wnd, uint32_t cmd)
{
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.window = wnd;
    event.format = 32;
    event.sequence = 0;
    event.type = conn->atoms.WM_CHANGE_STATE;
    event.data.data32[0] = cmd;
    xcb_send_event(conn->connection, false, conn->screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);
    xcb_flush(conn->connection);
}

static void SendSysRestore(SConnection *conn, xcb_window_t wnd)
{

    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.window = wnd;
    event.format = 32;
    event.sequence = 0;
    event.type = conn->atoms._NET_WM_STATE;
    event.data.data32[0] = 0;
    event.data.data32[1] = conn->atoms._NET_WM_STATE_MAXIMIZED_VERT;
    event.data.data32[2] = conn->atoms._NET_WM_STATE_MAXIMIZED_HORZ;
    event.data.data32[3] = 0;
    event.data.data32[4] = 0;

    xcb_send_event(conn->connection, false, conn->screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);
    xcb_flush(conn->connection);
}

static int GetScrollBarPartState(const ScrollBar * sb, int iPart){
    int nState = sb->nState;
    if(iPart==sb->iHitTest){
        nState = sb->bDraging? BuiltinImage::St_Push:BuiltinImage::St_Hover;
    }
    return nState;
}

static BYTE GetScrollBarPartAlpha(const ScrollBar * sb, int iPart){
    BYTE byApha = 0xff;
    return byApha;//todo:hjx
}

static void OnNcPaint(HWND hWnd, WPARAM wp,LPARAM lp){
    //draw scrollbar and border
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    HRGN hrgn = (HRGN) wp;
    RECT rcWnd = wndObj->rc;
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top);
    int cxEdge = GetSystemMetrics(SM_CXBORDER);
    int cyEdge = GetSystemMetrics(SM_CYBORDER);
    if (wndObj->dwStyle & WS_BORDER) {
        InflateRect(&rcWnd, -cxEdge, -cyEdge);
    }
    if ((int)wp <= 1) {
        //redraw all nonclient area
        hrgn = CreateRectRgnIndirect(&rcWnd);
    }
    {
        HDC hdc = GetDCEx(hWnd,hrgn,DCX_WINDOW|DCX_INTERSECTRGN);
        if (wndObj->dwStyle & WS_BORDER) {
            int nSave=SaveDC(hdc);
            ExcludeClipRect(hdc, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);
            HBRUSH hbr = GetSysColorBrush(COLOR_WINDOWFRAME);
            RECT rcAll = rcWnd;
            InflateRect(&rcAll, cxEdge, cyEdge);
            FillRect(hdc, &rcAll, hbr);
            RestoreDC(hdc, nSave);
        }
        BuiltinImage * imgs = BuiltinImage::instance();
        {
            RECT rcSb;
            if(GetScrollBarRect(hWnd,SB_VERT,&rcSb) && RectInRegion(hrgn,&rcSb)){
                ScrollBar &sb = wndObj->sbVert;
                HDC memdc = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(memdc,rcSb.right-rcSb.left,rcSb.bottom-rcSb.top);
                HGDIOBJ oldBmp = SelectObject(memdc,bmp);
                SetViewportOrgEx(memdc,-rcSb.left,-rcSb.top,NULL);
                FillRect(memdc,&rcSb,GetStockObject(WHITE_BRUSH));//init alpha channel to 255
                {
                    RECT rc=GetScrollBarPartRect(TRUE,&sb,SB_LINELEFT,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_LINELEFT);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_LINELEFT);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Line_Up,TRUE,st,&rc,byAlpha);
                }
                {
                    RECT rc=GetScrollBarPartRect(TRUE,&sb,SB_RAIL,&rcSb);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Rail,TRUE,BuiltinImage::St_Normal,&rc,0xFF);
                }
                {
                    RECT rc=GetScrollBarPartRect(TRUE,&sb,SB_THUMBTRACK,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_THUMBTRACK);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_THUMBTRACK);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Thumb,TRUE,st,&rc,byAlpha);
                }
                {
                    RECT rc=GetScrollBarPartRect(TRUE,&sb,SB_LINEDOWN,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_LINEDOWN);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_LINEDOWN);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Line_Down,TRUE,st,&rc,byAlpha);
                }
                BitBlt(hdc,rcSb.left,rcSb.top,rcSb.right-rcSb.left,rcSb.bottom-rcSb.top,memdc,rcSb.left,rcSb.top,SRCCOPY);
                SelectObject(memdc,oldBmp);
                DeleteDC(memdc);
                DeleteObject(bmp);
            }
            if(GetScrollBarRect(hWnd,SB_HORZ,&rcSb) && RectInRegion(hrgn, &rcSb)){
                ScrollBar &sb = wndObj->sbHorz;
                HDC memdc = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(memdc,rcSb.right-rcSb.left,rcSb.bottom-rcSb.top);
                HGDIOBJ oldBmp = SelectObject(memdc,bmp);
                SetViewportOrgEx(memdc,-rcSb.left,-rcSb.top,NULL);
                FillRect(memdc,&rcSb,GetStockObject(BLACK_BRUSH));//init alpha channel to 255
                {
                    RECT rc=GetScrollBarPartRect(FALSE,&sb,SB_LINELEFT,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_LINELEFT);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_LINELEFT);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Line_Up,FALSE,st,&rc,byAlpha);
                }
                {
                    RECT rc=GetScrollBarPartRect(FALSE,&sb,SB_RAIL,&rcSb);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Rail,FALSE,BuiltinImage::St_Normal,&rc,0xFF);
                }
                {
                    RECT rc=GetScrollBarPartRect(FALSE,&sb,SB_THUMBTRACK,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_THUMBTRACK);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_THUMBTRACK);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Thumb,FALSE,st,&rc,byAlpha);
                }
                {
                    RECT rc=GetScrollBarPartRect(FALSE,&sb,SB_LINEDOWN,&rcSb);
                    int st = GetScrollBarPartState(&sb,SB_LINEDOWN);
                    BYTE byAlpha = GetScrollBarPartAlpha(&sb,SB_LINEDOWN);
                    imgs->drawScrollbarState(memdc,BuiltinImage::Sb_Line_Down,FALSE,st,&rc,byAlpha);
                }
                BitBlt(hdc,rcSb.left,rcSb.top,rcSb.right-rcSb.left,rcSb.bottom-rcSb.top,memdc,rcSb.left,rcSb.top,SRCCOPY);
                SelectObject(memdc,oldBmp);
                DeleteDC(memdc);
                DeleteObject(bmp);
            }
            if((wndObj->showSbFlags & SB_BOTH) == SB_BOTH){
                rcWnd.left = rcWnd.right-GetSystemMetrics(SM_CXVSCROLL);
                rcWnd.top = rcWnd.bottom - GetSystemMetrics(SM_CYHSCROLL);
                imgs->drawScrollbarState(hdc,(wndObj->dwStyle&WS_CHILD)?(BuiltinImage::Sb_Triangle+3):BuiltinImage::Sb_Triangle,FALSE,BuiltinImage::St_Normal,&rcWnd,0xff);
            }
        }

        ReleaseDC(hWnd,hdc);
    }
    if ((int)wp <= 1) {
        DeleteObject(hrgn);
    }
}

static LRESULT handleNcLbuttonDown(HWND hWnd,WPARAM wp,LPARAM lp){
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    RECT rcSbHorz,rcSbVert;
    GetScrollBarRect(hWnd,SB_HORZ,&rcSbHorz);
    GetScrollBarRect(hWnd,SB_VERT,&rcSbVert);
    POINT pt = {GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    ScrollBar * sb =nullptr;
    BOOL bVert=TRUE;
    RECT *pRcAll=&rcSbVert;
    if(PtInRect(&rcSbVert,pt))
        sb = &wndObj->sbVert;
    else if(PtInRect(&rcSbHorz,pt))
        sb = &wndObj->sbHorz, bVert=FALSE,pRcAll=&rcSbHorz;
    if(!sb)
        return 0;
    int iPart = ScrollBarHitTest(bVert,sb,pRcAll,pt);
    if(iPart == -1)
        return 0;
    SetCapture(hWnd);
    sb->bDraging=TRUE;
    sb->nState = BuiltinImage::St_Push;
    sb->iHitTest = iPart;
    POINT ptStart = pt;
    int pos=sb->nPos;
    RECT rcPart = GetScrollBarPartRect(bVert,sb,iPart,pRcAll);
    RECT rcRail = GetScrollBarPartRect(bVert,sb,SB_RAIL,pRcAll);
    int railLen = bVert?(rcRail.bottom-rcRail.top):(rcRail.right-rcRail.left);
    RedrawNcRect(hWnd,&rcPart);
    if(iPart!=SB_THUMBTRACK)
    {
        SendMessageA(hWnd,bVert?WM_VSCROLL:WM_HSCROLL,iPart,0);
        SetTimer(hWnd,TIMER_STARTAUTOSCROLL,SPAN_STARTAUTOSCROLL,NULL);
    }else{
        sb->nTrackPos = pos;
        SendMessageA(hWnd,bVert?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(SB_THUMBTRACK,pos),0);
    }
    //wait for lbuttonup msg
    for(;;){
        MSG msg = { 0 };
        WaitMessage();
        if (!PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE))
            continue;
        if(msg.message==WM_QUIT)
            break;
        PeekMessage(&msg,0,0,0,PM_REMOVE);
        if(msg.message == WM_LBUTTONUP){            
            pt = {GET_X_LPARAM(msg.lParam),GET_Y_LPARAM(msg.lParam)};
            break;
        }
        RECT rcInvalid {0,0,0,0};
        if(msg.message == WM_TIMER){
            if(msg.wParam == TIMER_STARTAUTOSCROLL){
                KillTimer(hWnd,TIMER_STARTAUTOSCROLL);
                SetTimer(hWnd,TIMER_AUTOSCROLL,SPAN_AUTOSCROLL,NULL);
            }else if(msg.wParam == TIMER_AUTOSCROLL){
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd,&pt);
                RECT rcPart = GetScrollBarPartRect(bVert,sb,iPart,pRcAll);
                if(PtInRect(&rcPart,pt))
                    SendMessageA(hWnd,bVert?WM_VSCROLL:WM_HSCROLL,iPart,0);
            }
        }else if(msg.message == WM_MOUSEMOVE){            
            POINT pt = {GET_X_LPARAM(msg.lParam),GET_Y_LPARAM(msg.lParam)};
            
            int iHitTest = ScrollBarHitTest(bVert,sb,pRcAll,pt);
            if(sb->iHitTest != iHitTest){
                if(sb->iHitTest!=-1){
                    RECT rc = GetScrollBarPartRect(bVert,sb,sb->iHitTest,pRcAll);
                    UnionRect(&rcInvalid,&rcInvalid,&rc);
                }
                sb->iHitTest = iHitTest;
                if(sb->iHitTest!=-1){
                    RECT rc = GetScrollBarPartRect(bVert,sb,sb->iHitTest,pRcAll);
                    UnionRect(&rcInvalid,&rcInvalid,&rc);
                }
            }
            if(iPart == SB_THUMBTRACK){
                //drag scrollbar.
                int diff = bVert?(pt.y-ptStart.y):(pt.x-ptStart.x);
                RECT rcThumb = GetScrollBarPartRect(bVert,sb,SB_THUMBTRACK,pRcAll);
                UnionRect(&rcInvalid,&rcInvalid,&rcThumb);
                int nThumbLen = bVert?(rcThumb.bottom-rcThumb.top):(rcThumb.right-rcThumb.left);
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
                UnionRect(&rcInvalid,&rcInvalid,&rcThumb);
                rcThumb = GetScrollBarPartRect(bVert, sb, SB_THUMBTRACK, pRcAll);
                UnionRect(&rcInvalid,&rcInvalid,&rcThumb);
                SendMessageA(hWnd,bVert?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(SB_THUMBTRACK,nNewTrackPos),0);
            }
            RedrawNcRect(hWnd,&rcInvalid);
        }else{
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    ReleaseCapture();

    if(iPart!=SB_THUMBTRACK)
    {
        KillTimer(hWnd,TIMER_STARTAUTOSCROLL);
        KillTimer(hWnd,TIMER_AUTOSCROLL);
    }else{
        SendMessageA(hWnd,bVert? WM_VSCROLL: WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,sb->nTrackPos),0);
        SendMessageA(hWnd, bVert ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0), 0);
        sb->nTrackPos = -1;
        RedrawNcRect(hWnd, &rcRail);
    }
    sb->iHitTest=-1;
    sb->bDraging=FALSE;

    SendMessageA(hWnd,WM_MOUSEMOVE,0,MAKELPARAM(pt.x,pt.y));

    return 0;
}

static LRESULT handlePrint(HWND hWnd, WPARAM wp, LPARAM lp) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    if (lp & PRF_CHECKVISIBLE && !IsWindowVisible(hWnd))
        return 0;
    HDC hdc = wndObj->hdc;
    wndObj->hdc = (HDC)wp;
    HRGN hRgn = wndObj->invalid.hRgn;
    BOOL bErase = wndObj->invalid.bErase;

    if (lp & (PRF_CLIENT|PRF_NONCLIENT) == (PRF_CLIENT | PRF_NONCLIENT)) {
        RECT rc = wndObj->rc;
        OffsetRect(&rc, -rc.left, -rc.top);
        wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
    }
    else if (lp & PRF_CLIENT) {
        RECT rc;
        GetClientRect(hWnd, &rc);
        wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
    }
    else if (lp & PRF_NONCLIENT) {        
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
    if (lp & PRF_CHILDREN) {
        HWND hChild = GetWindow(hWnd, GW_CHILD);
        while (hChild) {
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

static LRESULT handleSetFont(HWND hWnd, WPARAM wp, LPARAM lp) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    HFONT hFont = (HFONT)wp;
    BOOL bRedraw = LOWORD(lp);
    if (hFont) {
        SelectObject(wndObj->hdc, hFont);
    }
    else {
        SelectObject(wndObj->hdc, GetStockObject(SYSTEM_FONT));
    }
    if (bRedraw) {
        InvalidateRect(hWnd, nullptr, TRUE);
    }
    return TRUE;
}

static LRESULT handleInputLanguageChangeRequest(HWND hWnd, WPARAM wp, LPARAM lp) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    assert(wndObj);
    wndObj->mConnection->ActivateKeyboardLayout((HKL)lp);
    return 1;
}

static LRESULT handleInputLangage(HWND hWnd, WPARAM wp, LPARAM lp) {
    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild) {
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
    else {
        SetBkColor(hDC, GetSysColor(COLOR_3DFACE));
        return GetSysColorBrush(COLOR_3DFACE);
    }
    return GetSysColorBrush(COLOR_WINDOW);
}


LRESULT OnSetWindowText(HWND hWnd,WndObj &wndObj, WPARAM wp,LPARAM lp)
{
    LPCSTR lpszString = (LPCSTR)lp;
    wndObj->title = lpszString?lpszString:"";
    xcb_change_property(wndObj->mConnection->connection, XCB_PROP_MODE_REPLACE, hWnd, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, wndObj->title.length(), wndObj->title.c_str());

    if (!wndObj->parent && !wndObj->title.empty())
    {
        char szPath[MAX_PATH];
        GetModuleFileName(nullptr, szPath, MAX_PATH);
        char *szName = strrchr(szPath, '/') + 1;
        int nNameLen = strlen(szName);
        int nLen = nNameLen + 1 + wndObj->title.length() + 1;
        char *pszCls = new char[nLen];
        strcpy(pszCls, szName);
        strcpy(pszCls + nNameLen + 1, wndObj->title.c_str());
        xcb_change_property(wndObj->mConnection->connection, XCB_PROP_MODE_REPLACE, hWnd, wndObj->mConnection->atoms.WM_CLASS, XCB_ATOM_STRING, 8, nLen, pszCls);
        delete[] pszCls;
    }
    xcb_flush(wndObj->mConnection->connection);
    return TRUE;
}

LRESULT OnMsgW2A(HWND hWnd,WndObj &wndObj,WPARAM wp,LPARAM lp){
    MSG *msg =(MSG *)lp;
    LRESULT ret = 0;
    if(IsWindowUnicode(hWnd))
        return wndObj->winproc(hWnd, msg->message, msg->wParam, msg->lParam);

    switch(msg->message){
        case WM_GETTEXTLENGTH:
            ret = MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(),wndObj->title.length(), nullptr, 0);
            break;
        case WM_GETTEXT:
            {
                int len = MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(),wndObj->title.length(), nullptr, 0);
                if(wp < len)
                    ret = 0;
                else{
                    LPWSTR buf = (LPWSTR)lp;
                    MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(),wndObj->title.length(), buf, len);
                    if(wp>len) buf[len]=0;
                    ret = len;
                }
            }
            break;
        case WM_SETTEXT:
            {
                std::string str;
                tostring((LPCWSTR)lp,-1,str);
                ret = wndObj->winproc(hWnd,msg->message,0,(LPARAM)str.c_str());
            }
            break;
        default:        
            ret = wndObj->winproc(hWnd,msg->message,msg->wParam,msg->lParam);
            break;
    }
    return ret;
}

static void UpdateScroll(HWND hWnd,WndObj &wndObj,ScrollBar & sb, RECT &rcSb, int htSb){
        if(htSb != wndObj->sbVert.iHitTest){
            RECT rcInvalid={0};
            if(htSb != -1){
                RECT rcPart = GetScrollBarPartRect(TRUE,&wndObj->sbVert,htSb,&rcSb);
                UnionRect(&rcInvalid,&rcInvalid,&rcPart);
            }
            if(wndObj->sbVert.iHitTest != -1){
                RECT rcPart = GetScrollBarPartRect(TRUE,&wndObj->sbVert,wndObj->sbVert.iHitTest,&rcSb);
                UnionRect(&rcInvalid,&rcInvalid,&rcPart);
            }
            sb.iHitTest = htSb;
            RedrawNcRect(hWnd,&rcInvalid);
        }
}

static LRESULT OnNcMouseHover(HWND hWnd,WndObj &wndObj,WPARAM wp,LPARAM lp){
    int htCode = (int)wp;
    POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    MapWindowPoints(0,hWnd,&pt,1);//todo:hjx
    if(wndObj->dwStyle & WS_VSCROLL){
        RECT rcSb;
        GetScrollBarRect(hWnd,SB_VERT,&rcSb) ;
        ScrollBar &sb = wndObj->sbVert;
        int htSb = ScrollBarHitTest(TRUE,&sb,&rcSb,pt);
        UpdateScroll(hWnd,wndObj,sb,rcSb,htSb);
    }
    if(wndObj->dwStyle & WS_HSCROLL){
        RECT rcSb;
        GetScrollBarRect(hWnd,SB_HORZ,&rcSb) ;
        ScrollBar &sb = wndObj->sbHorz;
        int htSb = ScrollBarHitTest(TRUE,&sb,&rcSb,pt);
        UpdateScroll(hWnd,wndObj,sb,rcSb,htSb);
    }
    return 0;
}

static LRESULT OnNcMouseLeave(HWND hWnd,WndObj &wndObj,WPARAM wp,LPARAM lp){
    if(wndObj->dwStyle & WS_VSCROLL){
        RECT rcSb;
        GetScrollBarRect(hWnd,SB_VERT,&rcSb) ;
        ScrollBar &sb = wndObj->sbVert;
        UpdateScroll(hWnd,wndObj,sb,rcSb,-1);
    }
    if(wndObj->dwStyle & WS_HSCROLL){
        RECT rcSb;
        GetScrollBarRect(hWnd,SB_HORZ,&rcSb) ;
        ScrollBar &sb = wndObj->sbHorz;
        UpdateScroll(hWnd,wndObj,sb,rcSb,-1);
    }
    return 0;
}

static LRESULT OnNcHitTest(HWND hWnd,WndObj &wndObj,WPARAM wp,LPARAM lp){
    RECT rc;
    GetClientRect(hWnd,&rc);
    int wid = rc.right - rc.left;
    int hei = rc.bottom - rc.top;
    ClientToScreen(hWnd,(LPPOINT)&rc);
    rc.right=rc.left + wid;
    rc.bottom = rc.top+hei;

    POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    if(PtInRect(&rc,pt))
        return HTCLIENT;
    else{
        if(wndObj->dwStyle & WS_HSCROLL){
            RECT rcSb ;
            GetScrollBarRect(hWnd,SB_HORZ,&rcSb);
            MapWindowPoints(hWnd,0,(LPPOINT)&rcSb,2);
            if(PtInRect(&rcSb,pt)){return HTHSCROLL;}               
        }
        if(wndObj->dwStyle & WS_VSCROLL){
            RECT rcSb ;
            GetScrollBarRect(hWnd,SB_VERT,&rcSb);
            MapWindowPoints(hWnd,0,(LPPOINT)&rcSb,2);
            if(PtInRect(&rcSb,pt)){return HTVSCROLL;}               
        }
        return HTBORDER;
    }       
}

static LRESULT OnNcCreate(HWND hWnd, WndObj& wndObj, WPARAM wp, LPARAM lp) {
    CREATESTRUCT* cs = (CREATESTRUCT*)lp;
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
        return OnMsgW2A(hWnd,wndObj,wp,lp);
    case WM_NCCREATE:
        return OnNcCreate(hWnd, wndObj, wp, lp);
    case WM_SETTEXT:
        return OnSetWindowText(hWnd,wndObj,wp,lp);
    case WM_GETTEXTLENGTH:
        return wndObj->title.length();
    case WM_NCHITTEST:
        return OnNcHitTest(hWnd,wndObj,wp,lp);
    case WM_NCMOUSEMOVE:
    case WM_NCMOUSEHOVER:
        return OnNcMouseHover(hWnd,wndObj,wp,lp);
    case WM_NCMOUSELEAVE:
        return OnNcMouseLeave(hWnd,wndObj,wp,lp);
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
        return handleInputLanguageChangeRequest(hWnd,wp,lp);
    case WM_INPUTLANGCHANGE:
        return handleInputLangage(hWnd, wp, lp);
    case WM_SETFONT:
        return handleSetFont(hWnd, wp, lp);
    case WM_PRINT:
        return handlePrint(hWnd, wp, lp);
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        return handleNcLbuttonDown(hWnd,wp,lp);
    case WM_SETICON:
    {
        if (wp == ICON_SMALL)
        {
            HICON ret = wndObj->iconSmall;
            wndObj->iconSmall = (HICON)lp;
            WIN_UpdateIcon(hWnd);
            return (LRESULT)ret;
        }
        else if (wp == ICON_BIG)
        {
            HICON ret = wndObj->iconBig;
            wndObj->iconBig = (HICON)lp;
            WIN_UpdateIcon(hWnd);
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
        if (!(lpWndPos->flags & SWP_NOZORDER))
        {
            if (lpWndPos->hwndInsertAfter == HWND_TOPMOST || lpWndPos->hwndInsertAfter == HWND_TOP)
            {
                uint32_t val[] = { XCB_STACK_MODE_ABOVE };
                xcb_configure_window(wndObj->mConnection->connection, lpWndPos->hwnd, XCB_CONFIG_WINDOW_STACK_MODE, &val);
            }
            else
            {
                uint32_t val[] = { (uint32_t)lpWndPos->hwndInsertAfter, XCB_STACK_MODE_ABOVE };
                xcb_configure_window(wndObj->mConnection->connection, lpWndPos->hwnd, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, &val);
            }
            xcb_flush(wndObj->mConnection->connection);
        }
    }
    break;
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
            SendSysCommand(wndObj->mConnection, hWnd, XCB_ICCCM_WM_STATE_ICONIC);
            break;
        case SC_MAXIMIZE:
            ChangeNetWmState(wndObj->mConnection, hWnd, true, wndObj->mConnection->atoms._NET_WM_STATE_MAXIMIZED_HORZ, wndObj->mConnection->atoms._NET_WM_STATE_MAXIMIZED_VERT);
            break;
        case SC_RESTORE:
            SendSysRestore(wndObj->mConnection, hWnd);
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
    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS &wndPos = *(WINDOWPOS *)lp;
        if (!(wndPos.flags & SWP_NOMOVE))
        {
            const int32_t coords[] = { static_cast<int32_t>(wndPos.x), static_cast<int32_t>(wndPos.y) };
            xcb_configure_window(wndObj->mConnection->connection, hWnd, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, coords);
            SendMessage(hWnd, WM_MOVE, 0, MAKELPARAM(wndPos.x, wndPos.y));
        }
        if (!(wndPos.flags & SWP_NOSIZE))
        {
            const uint32_t coords[] = { static_cast<uint32_t>(wndPos.cx), static_cast<uint32_t>(wndPos.cy) };
            xcb_configure_window(wndObj->mConnection->connection, hWnd, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, coords);
            SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(wndPos.cx, wndPos.cy));
        }
        if ((wndPos.flags & (SWP_NOMOVE | SWP_NOSIZE)) != 0)
        {
            RECT &rc = wndObj->rc;
            SetWindowPosHint(wndObj->mConnection, hWnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
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
        xcb_flush(wndObj->mConnection->connection);
        if ((!wndPos.flags & SWP_NOREDRAW))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
        }
    }
    break;
    case WM_NCPAINT:{
        OnNcPaint(hWnd,wp,lp);
        break;
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
    BOOL bNew = nCmdShow == SW_SHOW || nCmdShow == SW_SHOWNOACTIVATE || nCmdShow == SW_SHOWNORMAL || nCmdShow == SW_SHOWNA;
    if (bVisible == bNew)
        return TRUE;
    if (bNew)
    {
        xcb_map_window(wndObj->mConnection->connection, hWnd);
        wndObj->dwStyle |= WS_VISIBLE;
        if (nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA && !(wndObj->dwStyle&WS_CHILD))
            SetActiveWindow(hWnd);
        InvalidateRect(hWnd, nullptr, TRUE);
    }
    else
    {
        xcb_unmap_window(wndObj->mConnection->connection, hWnd);
        wndObj->dwStyle &= ~WS_VISIBLE;
    }
    xcb_flush(wndObj->mConnection->connection);
    SendMessage(hWnd, WM_SHOWWINDOW, bNew, 0);
    return TRUE;
}

BOOL MoveWindow(HWND hWnd, int x, int y, int nWidth, int nHeight, BOOL bRepaint)
{
    return SetWindowPos(hWnd, 0, x, y, nWidth, nHeight, SWP_NOZORDER | (bRepaint ? 0 : SWP_NOREDRAW));
}

BOOL IsWindowVisible(HWND hWnd)
{
    SConnection* conn = SConnMgr::instance()->getConnection();
    xcb_connection_t *connection = conn->connection;

    xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(connection, hWnd);
    xcb_get_window_attributes_reply_t *reply = xcb_get_window_attributes_reply(connection, cookie, NULL);
    if (!reply)
        return FALSE;
    uint8_t mapState = reply->map_state;
    free(reply);
    return mapState == XCB_MAP_STATE_VIEWABLE;
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

int GetWindowTextW(HWND hWnd, LPWSTR lpszStringBuf, int nMaxCount) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    int nLen = MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
    wchar_t* buf = new wchar_t[nLen+1];
    MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), buf, nLen+1);

    int nRet = 0;
    if (nMaxCount > nLen)
        wcscpy(lpszStringBuf, buf), nRet = nLen;
    else
        wcsncpy(lpszStringBuf, buf, nMaxCount), nRet = nMaxCount;
    delete[]buf;
    return nRet;
}

int GetWindowTextA(HWND hWnd, LPSTR lpszStringBuf, int nMaxCount)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    int nRet = 0;
    if (nMaxCount > wndObj->title.length())
        strcpy(lpszStringBuf, wndObj->title.c_str()), nRet = wndObj->title.length();
    else
        strncpy(lpszStringBuf, wndObj->title.c_str(), nMaxCount), nRet = nMaxCount;
    return nRet;
}

int GetWindowTextLengthW(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
}

int GetWindowTextLengthA(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return wndObj->title.length();
}

BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpszString) {
    std::string str;
    tostring(lpszString,-1,str);
    return SetWindowTextA(hWnd,str.c_str());
}

BOOL SetWindowTextA(HWND hWnd, LPCSTR lpszString)
{
    return SendMessageA(hWnd,WM_SETTEXT,0,(LPARAM)lpszString);
}

HDC GetDC(HWND hWnd)
{
    return GetDCEx(hWnd,nullptr,0);
}

HDC GetDCEx(
  HWND  hWnd,
  HRGN  hrgnClip,
  DWORD flags
){
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
    if(nSave == 0){
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        if (wndObj->dwStyle & WS_BORDER) {
            OffsetRect(&rcClient, GetSystemMetrics(SM_CXEDGE), GetSystemMetrics(SM_CYEDGE));
        }
        if(flags & DCX_WINDOW)
        {
            SetViewportOrgEx(hdc, 0, 0, NULL);
            ExcludeClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
        }else{
            IntersectClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
            SetViewportOrgEx(hdc, rcClient.left, rcClient.top, NULL);
        }
    }

    if(hrgnClip){
        if(flags & DCX_EXCLUDERGN)
            ExtSelectClipRgn(hdc,hrgnClip,RGN_DIFF);
        else if(flags & DCX_INTERSECTRGN)
            ExtSelectClipRgn(hdc,hrgnClip,RGN_AND);
    }
    return hdc;
}

HDC GetWindowDC(HWND hWnd)
{
    return GetDCEx(hWnd,0,DCX_WINDOW);
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
    RestoreDC(hdc,-1);
    if(hdc->nSave > 0)
        return 1;
    if (wndObj->crKey != CR_INVALID)
    {
        HRGN hRgn = BuildColorKeyRgn(hWnd);
        SetWindowRgn(hWnd, hRgn, FALSE);
        if (hRgn)
            DeleteObject(hRgn);
    }
    xcb_flush(wndObj->mConnection->connection);
    return 1;
}

int MapWindowPoints(HWND hWndFrom, HWND hWndTo, LPPOINT lpPoint, UINT nCount)
{
    RECT rcFrom={0}, rcTo={0};
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

static HRGN BuildColorKeyRgn(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    HRGN hRgn = nullptr;
    static const COLORREF mask = 0x00ffffff;
    COLORREF crKey = wndObj->crKey & mask;
    if (crKey != CR_INVALID)
    {
        BITMAP bm;
        GetObject(wndObj->hdc->bmp, sizeof(bm), &bm);
        const uint32_t *bits = (const uint32_t *)bm.bmBits;
        std::vector<RECT> lstRc;
        for (int y = 0; y < bm.bmHeight; y++, bits += bm.bmWidth)
        {
            int x = 0;
            while (x < bm.bmWidth)
            {
                while (x < bm.bmWidth && (bits[x] & mask) == crKey)
                    x++;
                int start = x;
                while (x < bm.bmWidth && (bits[x] & mask) != crKey)
                    x++;
                if (start != x)
                {
                    RECT rc = { start, y, x, y + 1 };
                    lstRc.push_back(rc);
                }
            }
        }
        if (!lstRc.empty())
        {
            int len = sizeof(RGNDATAHEADER) + sizeof(RECT) * lstRc.size();
            RGNDATA *pRgn = (RGNDATA *)malloc(len);
            pRgn->rdh.nCount = lstRc.size();
            pRgn->rdh.iType = RDH_RECTANGLES;
            memcpy(pRgn->Buffer, lstRc.data(), sizeof(RECT) * lstRc.size());
            hRgn = ExtCreateRegion(nullptr, len, pRgn);
            free(pRgn);
        }
    }
    return hRgn;
}

BOOL SetLayeredWindowAttributes(HWND hWnd, COLORREF crKey, BYTE byAlpha, DWORD dwFlags)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    if (wndObj->dwExStyle & WS_EX_LAYERED == 0)
        return FALSE;
    if (dwFlags & LWA_ALPHA && byAlpha != wndObj->byAlpha)
    {
        wndObj->byAlpha = byAlpha;
        if (wndObj->flags & fMap)
            wndObj->mConnection->SetWindowOpacity(hWnd, byAlpha);
    }
    if (dwFlags & LWA_COLORKEY && wndObj->crKey != crKey)
    {
        wndObj->crKey = crKey;
        if (crKey == CR_INVALID)
            SetWindowRgn(hWnd, nullptr, FALSE);
        else
        {
            HRGN hRgn = BuildColorKeyRgn(hWnd);
            SetWindowRgn(hWnd, hRgn, FALSE);
            if (hRgn)
                DeleteObject(hRgn);
        }
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
    if (uCmd == GW_OWNER)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        return wndObj->owner;
    }

    SConnection *conn = SConnMgr::instance()->getConnection();
    xcb_query_tree_cookie_t cookie = xcb_query_tree(conn->connection, hWnd);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(conn->connection, cookie, NULL);
    if (!reply)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    HWND hRet = 0;
    xcb_window_t *children = xcb_query_tree_children(reply);
    HWND hParent = reply->parent ? reply->parent : reply->root;
    switch (uCmd)
    {
    case GW_CHILDFIRST:
        if (reply->children_len > 0)
            hRet = children[0];
        break;
    case GW_CHILDLAST:
        if (reply->children_len > 0)
            hRet = children[reply->children_len - 1];
        break;
    case GW_HWNDFIRST:
        hRet = GetWindow(hParent, GW_CHILDFIRST);
        break;
    case GW_HWNDLAST:
        hRet = GetWindow(hParent, GW_CHILDLAST);
        break;
    case GW_HWNDPREV:
        hRet = GetWndSibling(conn->connection, hParent, hWnd, FALSE);
        break;
    case GW_HWNDNEXT:
        hRet = GetWndSibling(conn->connection, hParent, hWnd, TRUE);
        break;
    }
    free(reply);
    return hRet;
}

//-------------------------------------------------------------
// scrollbar api
BOOL EnableScrollBar(HWND hWnd, UINT wSBflags, UINT wArrows)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
    
    {
        ScrollBar & sb = wndObj->sbHorz;
        sb.nState = wSBflags & SB_HORZ?(BuiltinImage::St_Normal):(BuiltinImage::St_Disable);
        sb.uArrowFlags = wArrows;
    }
    {
        ScrollBar & sb = wndObj->sbVert;
        sb.nState = wSBflags & SB_VERT?(BuiltinImage::St_Normal):(BuiltinImage::St_Disable);
        sb.uArrowFlags = wArrows;
    }    
    InvalidateRect(hWnd,NULL,TRUE);
    return TRUE;
}

BOOL ShowScrollBar(HWND hWnd, int wBar, BOOL bShow)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    if(bShow){
        if (wBar & SB_VERT)
        {
            wndObj->showSbFlags |= SB_VERT;
            wndObj->dwExStyle |= WS_VSCROLL;
        }
        if (wBar & SB_HORZ) {
            wndObj->showSbFlags |= SB_HORZ;
            wndObj->dwExStyle |= WS_HSCROLL;
        }
    }else{
        if (wBar & SB_VERT) {
            wndObj->showSbFlags &= ~SB_VERT;
            wndObj->dwExStyle &= ~WS_VSCROLL;
        }
        if (wBar & SB_HORZ) {
            wndObj->showSbFlags &= ~SB_HORZ;
            wndObj->dwExStyle &= ~WS_HSCROLL;
        }
    }
    InvalidateRect(hWnd,NULL,TRUE);
    return TRUE;
}

static BOOL GetScrollInfoByMask(LPSCROLLINFO dst, const LPCSCROLLINFO src, UINT fMask) {
    if (fMask & SIF_PAGE) {
        if (src->nPage <= 0)
            return FALSE;
        dst->nPage = src->nPage;
    }
    if (fMask & SIF_POS)
    {
        dst->nPos = src->nPos;
    }
    if (fMask & SIF_TRACKPOS) {
        dst->nTrackPos = src->nTrackPos;
    }
    if (fMask & SIF_RANGE) {
        if (src->nMax < src->nMin)
            return FALSE;
        dst->nMin = src->nMin;
        dst->nMax = src->nMax;
    }
    return TRUE;
}

static BOOL SetScrollInfoByMask(LPSCROLLINFO dst,const LPCSCROLLINFO src,UINT fMask){
    if(fMask & SIF_PAGE){
        if(src->nPage<=0)
            return FALSE;
        dst->nPage = src->nPage;
    }
    if(fMask & SIF_POS)
    {
        dst->nPos = src->nPos;
    }
    if(fMask & SIF_TRACKPOS){
        dst->nTrackPos = src->nTrackPos;
    }
    if(fMask & SIF_RANGE){
        if(src->nMax < src->nMin)
            return FALSE;
        dst->nMin = src->nMin;
        dst->nMax = src->nMax;
    }
    int posMax = dst->nMax - dst->nPage +1;
    if(dst->nTrackPos != -1 ){
        if (dst->nTrackPos < dst->nMin)
            dst->nTrackPos = dst->nMin;
        if(dst->nTrackPos > posMax)
        {
            dst->nTrackPos = posMax;
            if(dst->nTrackPos< dst->nMin) dst->nTrackPos = dst->nMin;
        }
    }
    if (dst->nPos < dst->nMin)
        dst->nPos = dst->nMin;
    if(dst->nPos > posMax)
    {
        dst->nPos = posMax;
        if(dst->nPos<dst->nMin) dst->nPos = dst->nMin;
    }
    return TRUE;
}

int SetScrollInfo(HWND hWnd, int fnBar, LPCSCROLLINFO lpsi, BOOL fRedraw)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    BOOL bRet=FALSE;
    BOOL bSwitch = FALSE;
    if(fnBar == SB_VERT){
        SCROLLINFO& sb = wndObj->sbVert;
        bRet = SetScrollInfoByMask(&sb,lpsi,lpsi->fMask);
        if (bRet) {
            BOOL bEnable = sb.nMax - sb.nMin + 1 > sb.nPage;
            BOOL bPrev = wndObj->dwStyle & WS_VSCROLL?TRUE:FALSE;
            if (bEnable != bPrev) {
                bSwitch = TRUE;
                if (bEnable) {
                    wndObj->dwStyle |= WS_VSCROLL;
                    wndObj->showSbFlags |= SB_VERT;
                }
                else {
                    wndObj->dwStyle &= ~WS_VSCROLL;
                    wndObj->showSbFlags &= ~SB_VERT;
                }
            }
        }
    }else if(fnBar == SB_HORZ){
        SCROLLINFO& sb = wndObj->sbHorz;
        bRet = SetScrollInfoByMask(&sb,lpsi,lpsi->fMask);
        if (bRet) {
            BOOL bEnable = sb.nMax - sb.nMin + 1 > sb.nPage;
            BOOL bPrev = wndObj->dwStyle & WS_HSCROLL ? TRUE : FALSE;
            if (bEnable != bPrev) {
                bSwitch = TRUE;
                if (bEnable) {
                    wndObj->dwStyle |= WS_HSCROLL;
                    wndObj->showSbFlags |= SB_HORZ;
                }
                else {
                    wndObj->dwStyle &= ~WS_HSCROLL;
                    wndObj->showSbFlags &= ~SB_HORZ;
                }
            }
        }
    }else{
        return FALSE;
    }
    if(bRet && fRedraw){
        if (bSwitch) {
            RECT rcWnd = wndObj->rc;
            OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top);
            RedrawNcRect(hWnd, &rcWnd);
        }
        else {
            RECT rcBar;
            if (GetScrollBarRect(hWnd, fnBar, &rcBar))
                RedrawNcRect(hWnd, &rcBar);
        }
    }
    return TRUE;
}

BOOL GetScrollInfo(HWND hWnd, int fnBar, LPSCROLLINFO lpsi)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    if(fnBar == SB_VERT){
        GetScrollInfoByMask(lpsi,&wndObj->sbVert,lpsi->fMask);
    }else if(fnBar == SB_HORZ){
        GetScrollInfoByMask(lpsi,&wndObj->sbHorz,lpsi->fMask);
    }else{
        return FALSE;
    }
    return TRUE;
}

int GetScrollPos(HWND hWnd, int nBar)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return 0;
    if(nBar != SB_VERT && nBar!=SB_HORZ)
        return 0;
    SCROLLINFO &si = nBar==SB_VERT?wndObj->sbVert:wndObj->sbHorz;
    return si.nPos;
}

int SetScrollPos(HWND hWnd, int nBar, int nPos, BOOL bRedraw)
{
    SCROLLINFO si;
    si.fMask = SIF_POS;
    si.nPos = nPos;
    return SetScrollInfo(hWnd,nBar,&si,bRedraw);
}

BOOL GetScrollRange(HWND hWnd, int nBar, LPINT lpMinPos, LPINT lpMaxPos)
{
    SCROLLINFO si;
    si.fMask = SIF_RANGE;
    if(!GetScrollInfo(hWnd,nBar,&si))
        return FALSE;
    if(lpMinPos)
     *lpMinPos = si.nMin;
    if(lpMaxPos)
     *lpMaxPos = si.nMax;
    return TRUE;
}

BOOL SetScrollRange(HWND hWnd, int nBar, int nMinPos, int nMaxPos, BOOL bRedraw)
{
    SCROLLINFO si;
    si.fMask = SIF_RANGE;
    si.nMax = nMaxPos;
    si.nMin = nMinPos;
    return SetScrollInfo(hWnd,nBar,&si,bRedraw);
}

BOOL WINAPI AdjustWindowRectEx(LPRECT rect, DWORD style, BOOL menu, DWORD exStyle)
{
    //todo:hjx
     
    //NONCLIENTMETRICSW ncm;

    //ncm.cbSize = sizeof(ncm);
    //SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);

    //adjust_window_rect(rect, style, menu, exStyle, &ncm);
    return TRUE;
}

int WINAPI GetDlgCtrlID(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    if (!(wndObj->dwStyle & WS_CHILD))
        return 0;
    return wndObj->wIDmenu;
}

LONG WINAPI GetMessageTime(VOID) {
    return SConnMgr::instance()->getConnection()->GetMsgTime();
}

DWORD WINAPI GetMessagePos(VOID) {
    return SConnMgr::instance()->getConnection()->GetMsgPos();
}

BOOL WINAPI IsChild(HWND hWndParent, HWND hWnd)
{
    HWND parent = GetParent(hWnd);
    while (parent) {
        if (parent == hWndParent)
            return TRUE;
        parent = GetParent(parent);
    }
    return FALSE;
}

BOOL TranslateMessage(LPMSG pMsg)
{
    SConnection* conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    conn->TranslateMessage(pMsg);
    return TRUE;
}

BOOL DispatchMessage(LPMSG pMsg)
{
    if (!pMsg->hwnd)
        return FALSE;
    WNDPROC wndProc = (WNDPROC)GetWindowLongPtrA(pMsg->hwnd, GWLP_WNDPROC);
    if (!wndProc)
        return FALSE;
    CallWindowProcPriv(wndProc, pMsg->hwnd, pMsg->message, pMsg->wParam, pMsg->lParam);
    return TRUE;
}

HWND WINAPI GetDlgItem(_In_opt_ HWND hDlg, _In_ int nIDDlgItem) {
    HWND hChild = GetWindow(hDlg, GW_CHILD);
    while(hChild){
        WndObj wndObj = WndMgr::fromHwnd(hChild);
        if (wndObj && wndObj->wIDmenu == nIDDlgItem)
            return hChild;
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
    return 0;
}

int WINAPI ScrollWindowEx(HWND hWnd,
    int dx,
    int dy,
    const RECT* prcScroll,
    const RECT* prcClip,
    HRGN hrgnUpdate,
    LPRECT prcUpdate,
    UINT flags
) {
    //todo:hjx
    return 0;
}

UINT WINAPI	RegisterWindowMessageA(_In_ LPCSTR lpString) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return WM_REG_FIRST + SAtoms::internAtom(conn->connection, 0, lpString);
}

UINT WINAPI	RegisterWindowMessageW(_In_ LPCWSTR lpString) {
    std::string str;
    tostring(lpString, -1, str);
    return RegisterWindowMessageA(str.c_str());
}

BOOL WINAPI IsWindowUnicode(HWND hWnd) {
    return FALSE;
}

BOOL WINAPI EnumWindows(WNDENUMPROC lpEnumFunc,   LPARAM lParam
) {
    return WndMgr::enumWindows(lpEnumFunc, lParam);
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
