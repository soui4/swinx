#include "SConnection.h"
#include <assert.h>
#include <functional>
#include <xcb/xcb_icccm.h>
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_aux.h>
#include <xcb/shape.h>
#include <xcb/xfixes.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <math.h>
#include "cursormgr.h"
#include "src/platform/cocoa/os_state.h"
#include "uimsg.h"
#include "keyboard.h"
#include "SClipboard.h"
#include "SDragdrop.h"
#include "tostring.hpp"
#include "clsmgr.h"
#include "wndobj.h"
#include "log.h"
#define kLogTag "SConnection"

static SConnMgr *s_connMgr = NULL;

static std::recursive_mutex s_cs;
typedef std::lock_guard<std::recursive_mutex> SAutoLock;

using namespace swinx;

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

SConnMgr *SConnMgr::instance()
{
    if (!s_connMgr)
    {
        SAutoLock lock(s_cs);
        if (!s_connMgr)
        {
            static SConnMgr inst;
            s_connMgr = &inst;
        }
    }
    return s_connMgr;
}

//----------------------------------------------------------
SConnMgr::SConnMgr()
{
    m_hHeap = HeapCreate(0, 0, 0);
}

SConnMgr::~SConnMgr()
{
    SAutoWriteLock autoLock(m_rwLock);
    auto it = m_conns.begin();
    while (it != m_conns.end())
    {
        delete it->second;
        it++;
    }
    m_conns.clear();
    CloseHandle(m_hHeap);
}

void SConnMgr::removeConn(SConnection *pObj)
{
    SAutoWriteLock autoLock(m_rwLock);
    tid_t tid = (tid_t)pthread_self();
    auto it = m_conns.find(tid);
    if (it == m_conns.end())
    {
        return;
    }
    assert(it->second == pObj);
    delete it->second;
    m_conns.erase(it);
}

SConnection *SConnMgr::getConnection(tid_t tid_, int screenNum)
{
    tid_t tid = tid_ != 0 ? tid_ : (tid_t)pthread_self();
    {
        SAutoReadLock autoLock(m_rwLock);
        auto it = m_conns.find(tid);
        if (it != m_conns.end())
        {
            return it->second;
        }
        if (tid_ != 0)
            return nullptr;
    }
    {
        // new connection for this thread
        SAutoWriteLock autoLock(m_rwLock);
        SConnection *state = new SConnection(screenNum);
        m_conns[tid] = state;
        return state;
    }
}

static uint32_t GetDoubleClickSpan(xcb_connection_t *connection, xcb_screen_t *screen)
{
    uint32_t ret = 400;
    xcb_window_t root_window = screen->root;

    xcb_atom_t atom = SAtoms::internAtom(connection, 0, "_NET_DOUBLE_CLICK_TIME");
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, root_window, atom, XCB_ATOM_CARDINAL, 0, 1024);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
    if (reply == NULL)
    {
        fprintf(stderr, "Failed to get property value\n");
        return ret;
    }

    if (reply->value_len == 1)
    {
        ret = *((uint32_t *)xcb_get_property_value(reply));
    }
    free(reply);
    return ret;
}

void SConnection::xim_forward_event(xcb_xim_t *im, xcb_xic_t ic, xcb_key_press_event_t *event, void *user_data)
{
    SConnection *conn = (SConnection *)user_data;
    conn->pushEvent((xcb_generic_event_t *)event);
}

void SConnection::xim_commit_string(xcb_xim_t *im, xcb_xic_t ic, uint32_t flag, char *str, uint32_t length, uint32_t *keysym, size_t nKeySym, void *user_data)
{
    SConnection *conn = (SConnection *)user_data;
    const char *utf8 = nullptr;
    if (xcb_xim_get_encoding(im) == XCB_XIM_UTF8_STRING)
    {
        utf8 = str;
    }
    else if (xcb_xim_get_encoding(im) == XCB_XIM_COMPOUND_TEXT)
    {
        size_t newLength = 0;
        utf8 = xcb_compound_text_to_utf8(str, length, &newLength);
        length = newLength;
    }
    if (utf8)
    {
        // SLOG_FMTI("key commit: %.*s\n", l, utf8);
        std::wstring buf;
        towstring(utf8, length, buf);
        for (int i = 0; i < buf.length(); i++)
        {
            SLOG_STMI() << "commit text " << i << " is " << buf.c_str()[i];
            Msg *pMsg = new Msg;
            pMsg->hwnd = conn->m_hFocus;
            pMsg->message = WM_IME_CHAR;
            pMsg->wParam = buf.c_str()[i];
            pMsg->lParam = 0;
            pMsg->time = GetTickCount();
            conn->postMsg(pMsg);
        }
    }
}

void SConnection::xim_logger(const char *fmt, ...)
{
    va_list argp, argp2;
    va_start(argp, fmt);
    va_copy(argp2, argp);
    int len = vsnprintf(NULL, 0, fmt, argp);
    char *buf = (char *)malloc(len + 1);
    vsnprintf(buf, len + 1, fmt, argp2);
    SLOG_STMD() << "xim_logger:" << buf;
    free(buf);
    va_end(argp);
}

void SConnection::xim_create_ic_callback(xcb_xim_t *im, xcb_xic_t new_ic, void *user_data)
{
    HWND hWnd = (HWND)user_data;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    HIMC hIMC = ImmGetContext(hWnd);

    hIMC->xic = new_ic;
    if (new_ic && wndObj->mConnection->GetFocus() == hWnd)
    {
        // delay set ic focus
        xcb_xim_set_ic_focus(im, new_ic);
        //        SLOG_STMI()<<"xcb_xim_set_ic_focus set windows "<<hWnd<<" xic="<<new_ic;
    }
}

void SConnection::xim_open_callback(xcb_xim_t *im, void *user_data)
{
    HWND hWnd = (HWND)user_data;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    uint32_t input_style = XCB_IM_PreeditPosition | XCB_IM_StatusArea;
    xcb_point_t spot;
    spot.x = wndObj->mConnection->m_caretInfo.x;
    spot.y = wndObj->mConnection->m_caretInfo.y + wndObj->mConnection->m_caretInfo.nHeight;
    HIMC hIMC = ImmGetContext(hWnd);
    if (!hIMC)
        return;
    xcb_xim_nested_list nested = xcb_xim_create_nested_list(im, XCB_XIM_XNSpotLocation, &spot, NULL);
    xcb_window_t wnd = (xcb_window_t)hWnd;
    if (0 && hIMC->xic)
    {
        xcb_xim_set_ic_values(im, hIMC->xic, nullptr, NULL, XCB_XIM_XNInputStyle, &input_style, XCB_XIM_XNClientWindow, &wnd, XCB_XIM_XNFocusWindow, &wnd, XCB_XIM_XNPreeditAttributes, &nested, NULL);
        xcb_xim_set_ic_focus(im, hIMC->xic);
    }
    else
    {
        xcb_xim_create_ic(im, xim_create_ic_callback, user_data, XCB_XIM_XNInputStyle, &input_style, XCB_XIM_XNClientWindow, &wnd, XCB_XIM_XNFocusWindow, &wnd, XCB_XIM_XNPreeditAttributes, &nested, NULL);
    }
    free(nested.data);
}

SConnection::SConnection(int screenNum)
    : m_keyboard(nullptr)
    , m_hook_table(nullptr)
    , m_forceDpi(-1)
    , connection(nullptr)
    , m_xim(nullptr)
{
    connection = xcb_connect(nullptr, &screenNum);
    if (int errCode = xcb_connection_has_error(connection) > 0)
    {
        printf("XCB Error: %d\n", errCode);
        connection = NULL;
        return;
    }

    /* Get the screen whose number is screenNum */

    m_setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(m_setup);

    // we want the screen at index screenNum of the iterator
    for (int i = 0; i < screenNum; ++i)
    {
        xcb_screen_next(&iter);
    }
    if (!iter.data)
    {
        // get the first screen
        iter = xcb_setup_roots_iterator(m_setup);
    }
    screen = iter.data;

    {
        xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
        for (; depth_iter.rem && rgba_visual == 0; xcb_depth_next(&depth_iter))
        {
            if (depth_iter.data->depth == 32)
            {
                xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
                for (; visual_iter.rem; xcb_visualtype_next(&visual_iter))
                {
                    xcb_visualtype_t *visual_type = visual_iter.data;
                    if (visual_type->_class == XCB_VISUAL_CLASS_TRUE_COLOR && visual_type->red_mask == 0xFF0000 && visual_type->green_mask == 0x00FF00 && visual_type->blue_mask == 0x0000FF)
                    {
                        rgba_visual = visual_type;
                        break;
                    }
                }
            }
        }
        if (rgba_visual)
            SLOG_STMI() << "32 bit visual id=" << rgba_visual->visual_id;
    }
    readXResources();
    initializeXFixes();
    if (rgba_visual)
    { // get composited for screen
        char szAtom[50];
        sprintf(szAtom, "_NET_WM_CM_S%d", screenNum);
        xcb_atom_t atom = SAtoms::internAtom(connection, FALSE, szAtom);
        xcb_get_selection_owner_cookie_t owner_cookie = xcb_get_selection_owner(connection, atom);
        xcb_get_selection_owner_reply_t *owner_reply = xcb_get_selection_owner_reply(connection, owner_cookie, NULL);
        m_bComposited = owner_reply->owner != 0;
        free(owner_reply);
        SLOG_STMI() << "enable composite=" << m_bComposited;
    }
    m_tid = GetCurrentThreadId();

    atoms.Init(connection, screenNum);
    m_tsDoubleSpan = GetDoubleClickSpan(connection, screen);

    m_bQuit = false;
    m_msgPeek = nullptr;
    m_bMsgNeedFree = false;
    m_hWndCapture = 0;
    m_hWndActive = 0;
    m_hFocus = 0;
    m_bBlockTimer = false;

    m_deskDC = new _SDC(screen->root);
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);

    memset(&m_caretInfo, 0, sizeof(m_caretInfo));

    m_rcWorkArea.left = m_rcWorkArea.top = 0;
    m_rcWorkArea.right = screen->width_in_pixels;
    m_rcWorkArea.bottom = screen->height_in_pixels;

    m_evtSync = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    m_trdEvtReader = std::move(std::thread(std::bind(&readProc, this)));

    xcb_compound_text_init();
    m_xim = xcb_xim_create(connection, screenNum, NULL);

    xcb_xim_im_callback xim_callback = {
        .forward_event = &SConnection::xim_forward_event,
        .commit_string = &SConnection::xim_commit_string,
    };
    xcb_xim_set_im_callback(m_xim, &xim_callback, this);
    xcb_xim_set_log_handler(m_xim, xim_logger);
    xcb_xim_set_use_compound_text(m_xim, true);
    xcb_xim_set_use_utf8_string(m_xim, true);

    m_keyboard = new SKeyboard(this);
    m_trayIconMgr = new STrayIconMgr(this);
    m_clipboard = new SClipboard(this);
}

SConnection::~SConnection()
{
    if (!connection)
    {
        return;
    }
    if (m_xim)
    {
        xcb_xim_close(m_xim);
        xcb_xim_destroy(m_xim);
        m_xim = nullptr;
    }

    delete m_keyboard;
    delete m_clipboard;
    delete m_trayIconMgr;
    for (auto it : m_sysCursor)
    {
        xcb_free_cursor(connection, it.second);
    }
    m_sysCursor.clear();

    delete m_deskDC;
    DeleteObject(m_deskBmp);
    DestroyCaret();

    SLOG_STMI() << "quit sconnection";
    // m_bQuit = true;

    xcb_window_t hTmp = xcb_generate_id(connection);
    xcb_create_window(connection,
                      XCB_COPY_FROM_PARENT, // depth -- same as root
                      hTmp,                 // window id
                      screen->root,         // parent window id
                      0, 0, 3, 3,
                      0,                           // border width
                      XCB_WINDOW_CLASS_INPUT_ONLY, // window class
                      screen->root_visual,         // visual
                      0,                           // value mask
                      0);                          // value list

    // the hWnd is valid window id.
    xcb_client_message_event_t client_msg_event = {
        XCB_CLIENT_MESSAGE, //.response_type
        32,                 //.format
        0,                  //.sequence
        (xcb_window_t)hTmp, //.window
        atoms.WM_DISCONN    //.type
    };
    xcb_send_event(connection, 0, hTmp, XCB_EVENT_MASK_NO_EVENT, (const char *)&client_msg_event);
    xcb_flush(connection);
    m_trdEvtReader.join();
    //    SLOG_STMI() << "event reader quited";

    xcb_destroy_window(connection, hTmp);
    xcb_flush(connection);
    xcb_disconnect(connection);

    for (auto it : m_msgQueue)
    {
        delete it;
    }
    m_msgQueue.clear();
    if (m_msgPeek && m_bMsgNeedFree)
    {
        delete m_msgPeek;
        m_msgPeek = nullptr;
        m_bMsgNeedFree = false;
    }
    for (auto it = m_msgStack.rbegin(); it != m_msgStack.rend(); it++)
    {
        delete *it;
    }
    m_msgStack.clear();

    CloseHandle(m_evtSync);
}

void SConnection::readXResources()
{
    int offset = 0;
    std::stringstream resources;
    while (1)
    {
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, xcb_get_property_unchecked(connection, false, screen->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, offset / 4, 8192), NULL);
        bool more = false;
        if (reply && reply->format == 8 && reply->type == XCB_ATOM_STRING)
        {
            int len = xcb_get_property_value_length(reply);
            resources << std::string((const char *)xcb_get_property_value(reply), len);
            offset += len;
            more = reply->bytes_after != 0;
        }

        if (reply)
            free(reply);

        if (!more)
            break;
    }

    std::string line;
    static const char kDpiDesc[] = "Xft.dpi:\t";
    while (std::getline(resources, line, '\n'))
    {
        if (line.length() > ARRAYSIZE(kDpiDesc) - 1 && strncmp(line.c_str(), kDpiDesc, ARRAYSIZE(kDpiDesc) - 1) == 0)
        {
            m_forceDpi = atoi(line.c_str() + ARRAYSIZE(kDpiDesc) - 1);
        }
    }
}

void SConnection::initializeXFixes()
{
    xcb_generic_error_t *error = 0;
    const xcb_query_extension_reply_t *reply = xcb_get_extension_data(connection, &xcb_xfixes_id);
    if (!reply || !reply->present)
        return;

    xfixes_first_event = reply->first_event;
    xcb_xfixes_query_version_cookie_t xfixes_query_cookie = xcb_xfixes_query_version(connection, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);
    xcb_xfixes_query_version_reply_t *xfixes_query = xcb_xfixes_query_version_reply(connection, xfixes_query_cookie, &error);
    if (!xfixes_query || error || xfixes_query->major_version < 2)
    {
        SLOG_STMW() << "SConnection: Failed to initialize XFixes";
        free(error);
        xfixes_first_event = 0;
    }
    free(xfixes_query);
    SLOG_STMI() << "hasXFixes()=" << hasXFixes();
}

bool SConnection::event2Msg(bool bTimeout, int elapse, uint64_t ts)
{
    if (m_bQuit)
        return false;
    bool bRet = false;
    if (!bTimeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex4Evt);
        for (auto it : m_evtQueue)
        {
            bool accepted = false;
            if (m_clipboard->processIncr())
                m_clipboard->incrTransactionPeeker(it, accepted);
            if (!accepted)
            {
                if (!xcb_xim_filter_event(m_xim, it))
                {
                    bool bForward = false;
                    if ((it->response_type & ~0x80) == XCB_KEY_PRESS || (it->response_type & ~0x80) == XCB_KEY_RELEASE)
                    {
                        HIMC hIMC = ImmGetContext(m_hFocus);
                        if (hIMC && hIMC->xic)
                        {
                            bForward = xcb_xim_forward_event(m_xim, hIMC->xic, (xcb_key_press_event_t *)it);
                        }
                    }
                    if (!bForward)
                        pushEvent(it);
                }
            }
            free(it);
        }
        bRet = !m_evtQueue.empty();
        m_evtQueue.clear();
    }
    if (!m_bBlockTimer)
    {
        static const int kMaxDalayMsg = 5; // max delay ms for a timer.
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        int msgQueueSize = (int)m_msgQueue.size();
        int elapse2 = elapse + std::min(msgQueueSize, kMaxDalayMsg);
        POINT pt;
        GetCursorPos(&pt);
        for (auto &it : m_lstTimer)
        {
            if (it.fireRemain <= elapse2)
            {
                // fire timer event
                Msg *pMsg = new Msg;
                pMsg->hwnd = it.hWnd;
                pMsg->message = WM_TIMER;
                pMsg->wParam = it.id;
                pMsg->lParam = (LPARAM)it.proc;
                pMsg->time = ts;
                pMsg->pt = pt;
                m_msgQueue.push_back(pMsg);
                it.fireRemain = it.elapse;
                bRet = true;
            }
            else
            {
                it.fireRemain -= elapse;
            }
        }
    }
    return bRet;
}

bool SConnection::waitMsg()
{
    UINT timeOut = -1;
    if (!m_bBlockTimer)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto &it : m_lstTimer)
        {
            timeOut = std::min(timeOut, it.fireRemain);
        }
    }
    bool bTimeout = WaitForSingleObject(m_evtSync, timeOut) == WAIT_TIMEOUT;
    uint64_t ts = GetTickCount64();
    UINT elapse = m_tsLastMsg == -1 ? 0 : (ts - m_tsLastMsg);
    m_tsLastMsg = ts;
    event2Msg(bTimeout, elapse, ts);
    return !m_msgQueue.empty();
}

DWORD SConnection::GetMsgPos() const
{ // todo:hjx
    if (m_msgPeek)
    {
        return MAKELONG(m_msgPeek->pt.x, m_msgPeek->pt.y);
    }
    return 0;
}

LONG SConnection::GetMsgTime() const
{
    if (m_msgPeek)
    {
        return m_msgPeek->time;
    }
    return GetTickCount();
}

DWORD SConnection::GetQueueStatus(UINT flags)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    DWORD ret = 0;
    for (auto it : m_msgQueue)
    {
        switch (it->message)
        {
        case WM_PAINT:
            if (flags & QS_PAINT)
            {
                ret = MAKELONG(0, it->message);
            }
            break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (flags & QS_KEY)
            {
                ret = MAKELONG(0, it->message);
            }
            break;
        case WM_MOUSEMOVE:
            if (flags & QS_MOUSEMOVE)
            {
                ret = MAKELONG(0, it->message);
            }
            break;
        case WM_TIMER:
            if (flags & QS_TIMER)
            {
                ret = MAKELONG(0, it->message);
            }
            break;
        default:
            if (it->message >= WM_LBUTTONDOWN && it->message <= WM_XBUTTONDBLCLK && (flags & QS_MOUSEBUTTON))
            {
                ret = MAKELONG(0, it->message);
            }
            if (it->msgReply)
            {
                if (it->msgReply->GetType() == MT_POST && flags & QS_POSTMESSAGE)
                {
                    ret = MAKELONG(0, it->message);
                }
                else if (it->msgReply->GetType() == MT_SEND && flags & QS_SENDMESSAGE)
                {
                    ret = MAKELONG(0, it->message);
                }
            }
            if (flags & QS_ALLPOSTMESSAGE)
            {
                ret = MAKELONG(0, it->message);
            }
            break;
        }
        if (ret != 0)
            break;
    }
    return ret;
}

class DefEvtChecker : public IEventChecker {
  public:
    int type;
    DefEvtChecker(int _type)
        : type(_type)
    {
    }

    bool checkEvent(xcb_generic_event_t *e) const
    {
        return e->response_type == type;
    }
};
xcb_generic_event_t *SConnection::checkEvent(int type)
{
    DefEvtChecker checker(type);
    return checkEvent(&checker);
}

xcb_generic_event_t *SConnection::checkEvent(IEventChecker *checker)
{
    std::unique_lock<std::mutex> lock(m_mutex4Evt);
    for (auto it = m_evtQueue.begin(); it != m_evtQueue.end(); it++)
    {
        if (checker->checkEvent(*it))
        {
            xcb_generic_event_t *ret = *it;
            m_evtQueue.erase(it);
            return ret;
        }
    }
    return nullptr;
}

int SConnection::_waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD to, DWORD dwWaitMask)
{
    for (;;)
    {
        UINT timeOut = to;
        if (!m_bBlockTimer)
        {
            std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
            for (auto &it : m_lstTimer)
            {
                timeOut = std::min(timeOut, it.fireRemain);
            }
        }

        uint64_t ts1 = GetTickCount64();
        int ret = WaitForMultipleObjects(nCount, handles, FALSE, timeOut);
        uint64_t ts2 = GetTickCount64();
        UINT elapse = ts2 - ts1;
        if (ret == WAIT_TIMEOUT || ret == WAIT_OBJECT_0 + nCount - 1)
        {
            // the last handle is m_evtSync
            if (m_bQuit)
                return WAIT_FAILED;
            event2Msg(ret == WAIT_TIMEOUT, elapse, ts2);
            if (dwWaitMask != 0)
            {
                if (GetQueueStatus(dwWaitMask))
                    return WAIT_OBJECT_0 + nCount - 1;
            }
            if (to != INFINITE)
            {
                if (to <= elapse)
                    return WAIT_TIMEOUT;
                to -= elapse;
            }
        }
        else
        {
            return ret;
        }
    }
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD to, BOOL fWaitAll, DWORD dwWaitMask)
{
    if (nCount >= MAXIMUM_WAIT_OBJECTS)
        return -1;
    HANDLE hs[MAXIMUM_WAIT_OBJECTS] = { 0 };
    for (int i = 0; i < nCount; i++)
    {
        hs[i] = AddHandleRef(handles[i]);
    }
    hs[nCount] = m_evtSync;
    int nSignals = 0;
    bool hasMsg = false;
    int ret = 0;
    for (;;)
    {
        ret = _waitMutliObjectAndMsg(hs, nCount + 1, to, dwWaitMask);
        if (!fWaitAll)
        {
            break;
        }
        if (ret == WAIT_TIMEOUT || ret == WAIT_FAILED)
            break;
        if (ret == WAIT_OBJECT_0 + nCount)
            hasMsg = true;
        else
        {
            CloseHandle(hs[ret - WAIT_OBJECT_0]);
            hs[ret - WAIT_OBJECT_0] = INVALID_HANDLE_VALUE;
            nSignals++;
        }
        if (nSignals == nCount && hasMsg)
        { // all handles were signaled.
            ret = WAIT_OBJECT_0;
            break;
        }
    }
    for (int i = 0; i < nCount; i++)
    {
        CloseHandle(hs[i]);
    }
    return ret;
}

SHORT SConnection::GetKeyState(int vk)
{
    return m_keyboard->getKeyState((BYTE)vk);
}

BOOL SConnection::GetKeyboardState(PBYTE lpKeyState)
{
    m_keyboard->getKeyboardState(lpKeyState);
    return TRUE;
}

SHORT SConnection::GetAsyncKeyState(int vk)
{
    return m_keyboard->getKeyState((BYTE)vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT uMapType) const
{
    return m_keyboard->mapVirtualKey(uCode, uMapType);
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
    {
        char c = m_keyboard->scanCodeToAscii(HIWORD(pMsg->lParam));
        if (c != 0 && !GetKeyState(VK_CONTROL) && !GetKeyState(VK_MENU))
        {
            std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
            Msg *msg = new Msg;
            msg->message = pMsg->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
            msg->hwnd = pMsg->hwnd;
            msg->wParam = c;
            msg->lParam = pMsg->lParam;
            GetCursorPos(&msg->pt);
            postMsg(msg);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL SConnection::peekMsg(THIS_ LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    bool bTimeout = WaitForSingleObject(m_evtSync, 0) != WAIT_OBJECT_0;
    uint64_t ts = GetTickCount64();
    UINT elapse = m_tsLastMsg == -1 ? 0 : (ts - m_tsLastMsg);
    m_tsLastMsg = ts;
    event2Msg(bTimeout, elapse, ts);

    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    { // test for callback task
        auto it = m_lstCallbackTask.begin();
        while (it != m_lstCallbackTask.end())
        {
            auto cur = it++;
            if ((*cur)->TestEvent())
            {
                (*cur)->Release();
                m_lstCallbackTask.erase(cur);
            }
        }
    }
    auto it = m_msgQueue.begin();
    for (; it != m_msgQueue.end(); it++)
    {
        BOOL bMatch = TRUE;
        Msg *msg = (*it);
        do
        {
            if (msg->message == WM_QUIT)
                break;
            if (msg->message == WM_TIMER && msg->lParam == 0)
                break;
            if (msg->hwnd != hWnd && hWnd != 0)
            {
                bMatch = FALSE;
                break;
            }
            if (wMsgFilterMin == 0 && wMsgFilterMax == 0)
                break;
            if (wMsgFilterMin <= msg->message && wMsgFilterMax >= msg->message)
                break;
            bMatch = FALSE;
        } while (false);
        if (bMatch)
            break;
    }
    if (it != m_msgQueue.end())
    {
        Msg *msg = (*it);
        if (m_msgPeek && m_bMsgNeedFree)
        {
            delete m_msgPeek;
            m_msgPeek = nullptr;
            m_bMsgNeedFree = false;
        }
        if (msg->message == WM_TIMER && msg->lParam)
        {
            // SetTimer with callback, call it now.
            TIMERPROC proc = (TIMERPROC)msg->lParam;
            m_msgQueue.erase(it);
            proc(msg->hwnd, WM_TIMER, msg->wParam, msg->time);
            delete msg;
            return PeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
        }
        m_msgPeek = msg;
        if (wRemoveMsg == PM_NOREMOVE)
        { // the peeked message should not be dispatch.
            m_bMsgNeedFree = false;
        }
        else
        {
            m_msgQueue.erase(it);
            if (m_msgPeek->message == WM_PAINT)
            {
                static const uint64_t kFrameInterval = 15; // 15 interval for 66 frame per second
                if (m_msgQueue.empty() || m_tsLastPaint == -1 || (GetTickCount64() - m_msgPeek->time) >= kFrameInterval)
                    m_tsLastPaint = m_msgPeek->time;
                else
                {
                    m_msgPeek = nullptr;
                    BOOL bRet = peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
                    postMsg(msg); // insert paint message back.
                    return bRet;
                }
            }
            m_bMsgNeedFree = true;
        }
        memcpy(pMsg, (MSG *)m_msgPeek, sizeof(MSG));
        if (!m_msgQueue.empty()) // wake up the next waitMsg.
            SetEvent(m_evtSync);
        return TRUE;
    }

    return FALSE;
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    BOOL bRet = FALSE;
    for (; !bRet && !m_bQuit;)
    {
        bRet = peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE);
        if (!bRet)
        {
            waitMsg();
        }
    }

    return bRet;
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp)
{
    Msg *pMsg = new Msg(new MsgReply);
    pMsg->hwnd = hWnd;
    pMsg->message = message;
    pMsg->wParam = wp;
    pMsg->lParam = lp;
    GetCursorPos(&pMsg->pt);
    postMsg(pMsg);
}

void SConnection::postMsg(Msg *pMsg)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    m_msgQueue.push_back(pMsg);
    SetEvent(m_evtSync);
}

void SConnection::postMsg2(BOOL bWideChar, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply)
{
    if (!bWideChar)
    {
        Msg *pMsg = new Msg(reply);
        pMsg->hwnd = hWnd;
        pMsg->message = message;
        pMsg->wParam = wp;
        pMsg->lParam = lp;
        GetCursorPos(&pMsg->pt);
        postMsg(pMsg);
    }
    else
    {
        MsgW2A *pMsg = new MsgW2A(reply);
        pMsg->orgMsg.message = message;
        pMsg->orgMsg.wParam = wp;
        pMsg->orgMsg.lParam = lp;
        pMsg->hwnd = hWnd;
        GetCursorPos(&pMsg->pt);
        postMsg(pMsg);
    }
}

void SConnection::postCallbackTask(CbTask *pTask)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    m_lstCallbackTask.push_back(pTask);
    pTask->AddRef();
}

BOOL SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    DestroyCaret();
    m_caretInfo.hOwner = hWnd;
    if (hBitmap == (HBITMAP)1) // windows api support hBitmap set to 1 to indicator a gray caret, ignore it.
        hBitmap = nullptr;
    m_caretInfo.hBmp = RefGdiObj(hBitmap);
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

BOOL SConnection::DestroyCaret()
{
    if (m_caretInfo.nVisible > 0)
    {
        KillTimer(m_caretInfo.hOwner, TM_CARET);
    }
    DeleteObject(m_caretInfo.hBmp);
    m_caretInfo.hBmp = NULL;
    m_caretInfo.nHeight = 0;
    m_caretInfo.nWidth = 0;
    m_caretInfo.hOwner = 0;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

BOOL SConnection::ShowCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible++;
    if (m_caretInfo.nVisible == 1)
    {
        SetTimer(hWnd, TM_CARET, m_caretBlinkTime, NULL);
    }
    return TRUE;
}

BOOL SConnection::HideCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible--;

    if (m_caretInfo.nVisible == 0)
    {
        KillTimer(hWnd, TM_CARET);
    }
    return TRUE;
}

BOOL SConnection::SetCaretPos(int X, int Y)
{
    if (!m_hFocus)
        return FALSE;
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    HIMC hIMC = ImmGetContext(m_hFocus);
    if (hIMC && hIMC->xic)
    {
        xcb_point_t spot = { (int16_t)X, (int16_t)(Y + m_caretInfo.nHeight) };
        xcb_xim_nested_list nested = xcb_xim_create_nested_list(m_xim, XCB_XIM_XNSpotLocation, &spot, NULL);
        xcb_xim_set_ic_values(m_xim, hIMC->xic, nullptr, nullptr, XCB_XIM_XNPreeditAttributes, &nested, nullptr);
        free(nested.data);
    }
    ImmReleaseContext(m_hFocus, hIMC);
    return TRUE;
}

BOOL SConnection::GetCaretPos(LPPOINT lpPoint)
{
    if (!lpPoint)
        return FALSE;
    lpPoint->x = m_caretInfo.x;
    lpPoint->y = m_caretInfo.y;
    return TRUE;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime)
{
    m_caretBlinkTime = blinkTime;
}

void SConnection::EnableDragDrop(HWND hWnd, BOOL enable)
{
    if (enable)
    {
        xcb_atom_t atm = SDragDrop::xdnd_version;
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms.XdndAware, XCB_ATOM_ATOM, 32, 1, &atm);
    }
    else
    {
        xcb_delete_property(connection, hWnd, atoms.XdndAware);
    }
}

void SConnection::SendXdndStatus(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect)
{
    xcb_client_message_event_t response;
    response.response_type = XCB_CLIENT_MESSAGE;
    response.sequence = 0;
    response.window = hSource;
    response.format = 32;
    response.type = atoms.XdndStatus;
    response.data.data32[0] = hTarget;
    response.data.data32[1] = accept ? 1 : 0;              // flags
    response.data.data32[2] = 0;                           // x, y
    response.data.data32[3] = 0;                           // w, h
    response.data.data32[4] = XdndEffect2Action(dwEffect); // action
    xcb_send_event(connection, false, hSource, XCB_EVENT_MASK_NO_EVENT, (const char *)&response);
}

void SConnection::SendXdndFinish(HWND hTarget, HWND hSource, BOOL accept, DWORD dwEffect)
{
    xcb_client_message_event_t response;
    response.response_type = XCB_CLIENT_MESSAGE;
    response.sequence = 0;
    response.window = hSource;
    response.format = 32;
    response.type = atoms.XdndFinished;
    response.data.data32[0] = hTarget;
    response.data.data32[1] = accept ? 1 : 0;              // flags
    response.data.data32[2] = XdndEffect2Action(dwEffect); // action
    xcb_send_event(connection, false, hSource, XCB_EVENT_MASK_NO_EVENT, (const char *)&response);
}

xcb_atom_t SConnection::clipFormat2Atom(UINT uFormat)
{
    switch (uFormat)
    {
    case CF_TEXT:
        return atoms.CLIPF_UTF8;
    case CF_UNICODETEXT:
        return atoms.CLIPF_UNICODETEXT;
    case CF_BITMAP:
        return atoms.CLIPF_BITMAP;
    case CF_WAVE:
        return atoms.CLIPF_WAVE;
    default:
        // for registered format
        if (uFormat > CF_MAX)
        {
            return uFormat - CF_MAX;
        }
    }
    return 0;
}

uint32_t SConnection::atom2ClipFormat(xcb_atom_t atom)
{
    if (atom == atoms.CLIPF_UTF8)
        return CF_TEXT;
    else if (atom == atoms.CLIPF_UNICODETEXT)
        return CF_UNICODETEXT;
    else if (atom == atoms.CLIPF_BITMAP)
        return CF_BITMAP;
    else if (atom == atoms.CLIPF_WAVE)
        return CF_WAVE;
    else
        return CF_MAX + atom; // for registed format
}

std::shared_ptr<std::vector<char>> SConnection::readSelection(bool bXdnd, uint32_t fmt)
{
    return m_clipboard->getDataInFormat(bXdnd ? atoms.XdndSelection : atoms.CLIPBOARD, clipFormat2Atom(fmt));
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

enum QX11EmbedInfoFlags
{
    XEMBED_VERSION = 0,
    XEMBED_MAPPED = (1 << 0),
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

HWND SConnection::OnWindowCreate(_Window *pWnd, CREATESTRUCT *cs, int depth)
{
    HWND hWnd = xcb_generate_id(connection);

    xcb_colormap_t cmap = xcb_generate_id(connection);
    xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, cmap, screen->root, pWnd->visualId);

    const uint32_t evt_mask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

    const uint32_t mask = XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_SAVE_UNDER | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

    const uint32_t values[] = {
        XCB_NONE,                                    // XCB_CW_BACK_PIXMAP
        0,                                           // XCB_CW_BORDER_PIXEL
        (cs->dwExStyle & WS_EX_TOOLWINDOW) ? 1u : 0, // XCB_CW_OVERRIDE_REDIRECT
        0,                                           // XCB_CW_SAVE_UNDER
        evt_mask,                                    // XCB_CW_EVENT_MASK
        cmap                                         // XCB_CW_COLORMAP
    };
    xcb_window_class_t wndCls = XCB_WINDOW_CLASS_INPUT_OUTPUT;
    HWND hParent = pWnd->parent;
    if (hParent == HWND_MESSAGE)
    {
        hParent = screen->root;
        wndCls = XCB_WINDOW_CLASS_INPUT_ONLY;
    }
    else if (!(cs->style & WS_CHILD) || !hParent)
        hParent = screen->root;
    xcb_void_cookie_t cookie = xcb_create_window_checked(connection, depth, hWnd, hParent, cs->x, cs->y, std::max(cs->cx, 1u), std::max(cs->cy, 1u), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, pWnd->visualId, mask, values);

    xcb_generic_error_t *err = xcb_request_check(connection, cookie);
    if (err)
    {
        SLOG_FMTE("xcb_create_window failed, errcode=%d", err->error_code);
        free(err);
        xcb_free_colormap(connection, cmap);
        return 0;
    }
    pWnd->cmap = cmap;
    xcb_change_window_attributes(connection, hWnd, mask, values);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms.WM_PROTOCOLS, XCB_ATOM_ATOM, 32, 1, &atoms.WM_DELETE_WINDOW);

    // set the PID to let the WM kill the application if unresponsive
    uint32_t pid = getpid();
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms._NET_WM_PID, XCB_ATOM_CARDINAL, 32, 1, &pid);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, pWnd->title.length(), pWnd->title.c_str());

    { // set window clas
        char szPath[MAX_PATH];
        GetModuleFileName(nullptr, szPath, MAX_PATH);
        char *szName = strrchr(szPath, '/') + 1;
        int nNameLen = strlen(szName);
        char szClassName[MAX_PATH + 1] = { 0 };
        int clsLen = GetAtomNameA(pWnd->clsAtom, szClassName, MAX_PATH);
        int nLen = nNameLen + 1 + clsLen + 1;
        char *pszCls = new char[nLen];
        strcpy(pszCls, szName);
        strcpy(pszCls + nNameLen + 1, szClassName);
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms.WM_CLASS, XCB_ATOM_STRING, 8, nLen, pszCls);
        delete[] pszCls;
    }

    setMotifWindowFlags(this, hWnd, pWnd->dwStyle, pWnd->dwExStyle);
    {
        /* Add XEMBED info; this operation doesn't initiate the embedding. */
        uint32_t data[] = { XEMBED_VERSION, XEMBED_MAPPED };
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms._XEMBED_INFO, atoms._XEMBED_INFO, 32, 2, (void *)data);
    }
    if (!(pWnd->dwStyle & WS_EX_TOOLWINDOW) && wndCls == XCB_WINDOW_CLASS_INPUT_OUTPUT)
    {
        pWnd->hIMC = ImmCreateContext();
        pWnd->hIMC->xim = m_xim;
    }

    xcb_flush(connection);
    return hWnd;
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *wnd)
{
    KillWindowTimer(hWnd);
    if (GetCapture() == hWnd)
    {
        ReleaseCapture();
    }
    if (GetCaretInfo()->hOwner == hWnd)
    {
        DestroyCaret();
    }
    if (hWnd == m_hFocus)
    {
        SetFocus(0);
    }
    if (m_hWndActive == hWnd)
    {
        m_hWndActive = 0;
    }
    if (wnd->hIMC)
    {
        ImmDestroyContext(wnd->hIMC);
        wnd->hIMC = nullptr;
    }
    m_wndCursor.erase(hWnd);
    xcb_destroy_window(connection, hWnd);
    if (wnd->cmap)
    {
        xcb_free_colormap(connection, wnd->cmap);
        wnd->cmap = 0;
    }

    xcb_flush(connection);
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, BOOL bVisible, int nCmdShow)
{
    if (bVisible)
    {
        if (0 == (wndObj->dwStyle & WS_CHILD))
        { // show a popup window, auto release capture.
            ReleaseCapture();
        }
        xcb_map_window(connection, hWnd);
        wndObj->dwStyle |= WS_VISIBLE;
        InvalidateRect(hWnd, nullptr, TRUE);
        if (nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA && !(wndObj->dwStyle & WS_CHILD) && wndObj->mConnection->GetActiveWnd() == 0)
            SetActiveWindow(hWnd);
        sync();
    }
    else
    {
        xcb_unmap_window(connection, hWnd);
        wndObj->dwStyle &= ~WS_VISIBLE;
        if (!(wndObj->dwStyle & WS_CHILD))
        {
            // send synthetic UnmapNotify event according to icccm 4.1.4
            xcb_unmap_notify_event_t event;
            event.response_type = XCB_UNMAP_NOTIFY;
            event.event = screen->root;
            event.window = hWnd;
            event.from_configure = false;
            xcb_send_event(connection, false, event.event, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);
            if (m_hWndActive == hWnd)
            {
                SetActiveWindow(0);
            }
        }
        if (hWnd == m_hFocus)
        {
            SetFocus(0); // kill focus.
        }
    }
    xcb_flush(connection);
}

void SConnection::SetParent(HWND hWnd, _Window *wndObj, HWND hParent)
{
    if (wndObj)
    {
        if (!hParent)
        {
            if (m_hWndActive)
                hWnd = m_hWndActive;
            else
                hParent = screen->root;
        }

        if (!(wndObj->dwStyle & WS_CHILD))
        {
            xcb_icccm_set_wm_transient_for(connection, hWnd, hParent);
        }
        else if (hParent)
        {
            xcb_reparent_window(connection, hWnd, hParent, 0, 0);
        }
    }
    else
    {
        // hwnd for other process.
        xcb_reparent_window(connection, hWnd, hParent ? hParent : screen->root, 0, 0);
    }
    xcb_flush(connection);
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc)
{
    xcb_expose_event_t expose_event;
    expose_event.response_type = XCB_EXPOSE;
    expose_event.window = hWnd;
    expose_event.x = 0;
    expose_event.y = 0;
    expose_event.width = 0;
    expose_event.height = 0;
    xcb_send_event(connection, false, hWnd, XCB_EVENT_MASK_EXPOSURE, (const char *)&expose_event);
    xcb_flush(connection);
}

void SConnection::SetWindowMsgTransparent(HWND hWnd, _Window *wndObj, BOOL bTransparent)
{
    BOOL transparent = (wndObj->dwExStyle & WS_EX_TRANSPARENT) != 0;
    if (!(transparent ^ bTransparent) || !wndObj->mConnection->hasXFixes())
        return;

    xcb_rectangle_t rectangle;

    xcb_rectangle_t *rect = nullptr;
    int nrect = 0;

    if (!bTransparent)
    {
        rectangle.x = 0;
        rectangle.y = 0;
        rectangle.width = wndObj->rc.right - wndObj->rc.left;
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

void SConnection::AssociateHIMC(HWND hWnd, _Window *wndObj, HIMC hIMC)
{
    wndObj->hIMC = hIMC;
    if (hIMC)
    {
        hIMC->xim = m_xim;
    }
    if (GetFocus() == hWnd)
    {
        xcb_xim_open(m_xim, xim_open_callback, true, (void *)hWnd);
    }
}

DWORD SConnection::GetWndProcessId(HWND hWnd)
{
    DWORD pid=0;
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms._NET_WM_PID, XCB_ATOM_CARDINAL, 0, 1);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply != NULL)
        {
            pid = *(DWORD *)xcb_get_property_value(reply);
            free(reply);
        }
    return pid;
}


static void XcbGeo2Rect(xcb_get_geometry_reply_t *geo, RECT *rc)
{
    rc->left = geo->x;
    rc->top = geo->y;
    rc->right = geo->x + geo->width;
    rc->bottom = geo->y + geo->height;
}

static HWND _WindowFromPoint(xcb_connection_t *connection, xcb_window_t parent, POINT pt)
{
    xcb_query_tree_reply_t *tree;
    xcb_query_tree_cookie_t tree_cookie;
    xcb_window_t *children;
    HWND ret = XCB_NONE;
    int children_num;

    tree_cookie = xcb_query_tree(connection, parent);
    tree = xcb_query_tree_reply(connection, tree_cookie, NULL);

    if (!tree)
    {
        return XCB_NONE;
    }
    xcb_get_geometry_reply_t *geoParent = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, parent), NULL);
    if (!geoParent)
        return XCB_NONE;
    RECT rcParent;
    XcbGeo2Rect(geoParent, &rcParent);
    free(geoParent);
    if (!PtInRect(&rcParent, pt))
        return XCB_NONE;
    children = xcb_query_tree_children(tree);
    children_num = xcb_query_tree_children_length(tree);

    for (int i = children_num - 1; i >= 0; i--)
    {
        if (!IsWindowVisible(children[i]))
            continue;
        xcb_get_geometry_reply_t *geo = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, children[i]), NULL);
        if (!geo)
            continue;
        RECT rc;
        XcbGeo2Rect(geo, &rc);
        free(geo);
        if (PtInRect(&rc, pt))
        {
            ret = children[i];
            break;
        }
    }
    free(tree);
    if (ret != XCB_NONE)
    {
        pt.x -= rcParent.left;
        pt.y -= rcParent.top;
        ret = _WindowFromPoint(connection, ret, pt);
    }
    else
    {
        ret = parent;
    }
    return ret;
}

HWND SConnection::WindowFromPoint(POINT pt)
{
    return _WindowFromPoint(connection, screen->root, pt);
}

BOOL SConnection::GetClientRect(HWND hWnd, RECT *pRc)
{
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(connection, hWnd);
    xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(connection, cookie, NULL);
    if (!reply)
        return FALSE;
    pRc->left = pRc->top = 0;
    pRc->right = reply->width;
    pRc->bottom = reply->height;
    free(reply);
    return TRUE;
}

void SConnection::BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (m_msgPeek && m_bMsgNeedFree && m_msgPeek->hwnd == hWnd && m_msgPeek->message == msg && m_msgPeek->wParam == wp && m_msgPeek->lParam == lp)
    {
        m_msgStack.push_back(m_msgPeek);
        m_msgPeek = nullptr;
        m_bMsgNeedFree = false;
    }
}

void SConnection::AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res)
{
    if (m_msgStack.empty() || m_bMsgNeedFree)
        return;
    Msg *lastMsg = m_msgStack.back();

    if (lastMsg->hwnd == hWnd && lastMsg->message == msg && lastMsg->wParam == wp && lastMsg->lParam == lp)
    {
        lastMsg->SetResult(res);
        m_msgStack.pop_back();
        delete lastMsg;
    }
}

class PropertyNotifyEvent : public IEventChecker {
  public:
    PropertyNotifyEvent(xcb_window_t win, xcb_atom_t property)
        : window(win)
        , type(XCB_PROPERTY_NOTIFY)
        , atom(property)
    {
    }
    xcb_window_t window;
    int type;
    xcb_atom_t atom;
    bool checkEvent(xcb_generic_event_t *event) const
    {
        if (!event)
            return false;
        if ((event->response_type & ~0x80) != type)
        {
            return false;
        }
        else
        {
            xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)event;
            if ((pn->window == window) && (pn->atom == atom))
                return true;
        }
        return false;
    }
};

xcb_timestamp_t SConnection::getSectionTs()
{
    return m_tsSelection;
}

UINT_PTR SConnection::SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    if (hWnd)
    {
        // find exist timer.
        for (auto &it : m_lstTimer)
        {
            if (it.hWnd != hWnd)
                continue;
            if (it.id == id)
            {
                it.fireRemain = uElapse;
                it.proc = proc;
                return id;
            }
        }
        TimerInfo timer;
        timer.id = id;
        timer.fireRemain = uElapse;
        timer.hWnd = hWnd;
        timer.proc = proc;
        timer.elapse = uElapse;
        m_lstTimer.push_back(timer);
        return id;
    }
    else
    {
        UINT_PTR newId = 0;
        for (auto &it : m_lstTimer)
        {
            if (it.hWnd)
                continue;
            newId = std::max(it.id, newId);
        }
        TimerInfo timer;
        timer.id = newId + 1;
        timer.fireRemain = uElapse;
        timer.hWnd = 0;
        timer.proc = proc;
        timer.elapse = uElapse;
        m_lstTimer.push_back(timer);
        return timer.id;
    }
}

BOOL SConnection::KillTimer(HWND hWnd, UINT_PTR id)
{
    BOOL bRet = FALSE;
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); it++)
    {
        if (it->hWnd == hWnd && it->id == id)
        {
            m_lstTimer.erase(it);
            bRet = TRUE;
            break;
        }
    }

    if (bRet)
    {
        // remove timer from message queue.
        auto it = m_msgQueue.begin();
        while (it != m_msgQueue.end())
        {
            auto it2 = it++;
            Msg *msg = *it2;
            if (msg->hwnd == hWnd && msg->message == WM_TIMER && msg->wParam == id)
            {
                m_msgQueue.erase(it2);
                delete msg;
            }
        }
    }
    return bRet;
}

HDC SConnection::GetDC()
{
    return m_deskDC;
}

BOOL SConnection::ReleaseDC(HDC hdc)
{
    // todo:hjx
    return TRUE;
}

HWND SConnection::SetCapture(HWND hCapture)
{
    HWND ret = m_hWndCapture;
    if (hCapture == m_hWndCapture)
        return ret;
    xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(connection,
                                                        1, // 这个标志位表示不使用对子窗口的事件
                                                        hCapture, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
                                                        XCB_GRAB_MODE_ASYNC, // 异步捕获
                                                        XCB_GRAB_MODE_ASYNC,
                                                        XCB_NONE,        // 捕获事件的窗口
                                                        XCB_NONE,        // 使用默认光标
                                                        XCB_CURRENT_TIME // 立即开始捕获
    );
    xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(connection, cookie, NULL);
    bool result = false;
    if (reply)
    {
        result = reply->status == XCB_GRAB_STATUS_SUCCESS;
        if (!result)
            SLOG_STMI() << "set capture: reply->status=" << reply->status;
        free(reply);
    }
    if (result)
    {
        m_hWndCapture = hCapture;
    }
    xcb_flush(connection);
    // SLOG_STMI() << "set capture:" << hCapture << " result="<<result;
    return ret;
}

BOOL SConnection::ReleaseCapture()
{
    if (!m_hWndCapture)
        return FALSE;
    // SLOG_STMI() << "release capture, oldCapture=" << m_hWndCapture;
    m_hWndCapture = 0;
    xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
    xcb_flush(connection);
    return TRUE;
}

HWND SConnection::GetCapture() const
{
    return m_hWndCapture;
}

xcb_cursor_t SConnection::createXcbCursor(HCURSOR cursor)
{
    xcb_generic_error_t *error = 0;
    xcb_render_query_pict_formats_cookie_t formatsCookie = xcb_render_query_pict_formats(connection);
    xcb_render_query_pict_formats_reply_t *formatsReply = xcb_render_query_pict_formats_reply(connection, formatsCookie, &error);
    if (!formatsReply || error)
    {
        // qWarning("qt_xcb_createCursorXRender: query_pict_formats failed");
        free(formatsReply);
        free(error);
        return XCB_NONE;
    }
    xcb_render_pictforminfo_t *fmt = xcb_render_util_find_standard_format(formatsReply, XCB_PICT_STANDARD_ARGB_32);
    if (!fmt)
    {
        // qWarning("qt_xcb_createCursorXRender: Failed to find format PICT_STANDARD_ARGB_32");
        free(formatsReply);
        return XCB_NONE;
    }
    xcb_cursor_t xcb_cursor = XCB_NONE;
    BITMAP bm;
    ICONINFO info = { 0 };
    GetIconInfo(cursor, &info);
    assert(info.fIcon == 0);
    if (!info.hbmColor)
        goto end;
    GetObject(info.hbmColor, sizeof(bm), &bm);
    if (bm.bmBitsPixel != 32)
    {
        goto end;
    }

    do
    {
        xcb_pixmap_t pix = xcb_generate_id(connection);
        xcb_create_pixmap(connection, 32, pix, screen->root, bm.bmWidth, bm.bmHeight);

        xcb_render_picture_t pic = xcb_generate_id(connection);
        xcb_render_create_picture(connection, pic, pix, fmt->id, 0, 0);

        xcb_gcontext_t gc = xcb_generate_id(connection);

        xcb_image_t *xi = xcb_image_create(bm.bmWidth, bm.bmHeight, XCB_IMAGE_FORMAT_Z_PIXMAP, 32, 32, 32, 32,
                                           XCB_IMAGE_ORDER_LSB_FIRST, // todo:hjx
                                           XCB_IMAGE_ORDER_MSB_FIRST, 0, 0, 0);
        if (!xi)
        {
            // qWarning("qt_xcb_createCursorXRender: xcb_image_create failed");
            break;
        }
        xi->data = (uint8_t *)malloc(xi->stride * bm.bmHeight);
        if (!xi->data)
        {
            // qWarning("qt_xcb_createCursorXRender: Failed to malloc() image data");
            xcb_image_destroy(xi);
            break;
        }
        memcpy(xi->data, bm.bmBits, bm.bmWidth * 4 * bm.bmHeight);

        xcb_create_gc(connection, gc, pix, 0, 0);
        xcb_image_put(connection, pix, gc, xi, 0, 0, 0);
        xcb_free_gc(connection, gc);

        xcb_cursor = xcb_generate_id(connection);
        xcb_render_create_cursor(connection, xcb_cursor, pic, info.xHotspot, info.yHotspot);

        free(xi->data);
        xcb_image_destroy(xi);
        xcb_render_free_picture(connection, pic);
        xcb_free_pixmap(connection, pix);

    } while (false);

    free(formatsReply);
end:
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);

    return xcb_cursor;
}

HCURSOR SConnection::GetCursor()
{
    HWND hWnd = GetActiveWnd();
    auto it = m_wndCursor.find(hWnd);
    if (it == m_wndCursor.end())
        return 0;
    return it->second;
}

HCURSOR SConnection::SetCursor(HWND hWnd,HCURSOR cursor)
{
    HCURSOR ret = cursor;
    if (!hWnd){
        hWnd = m_hWndActive;
    }
    if(!hWnd)
        return ret;
    auto it = m_wndCursor.find(hWnd);
    if (it != m_wndCursor.end())
    {
        ret = it->second;
    }
    xcb_cursor_t xcbCursor = getXcbCursor(cursor);
    if (xcbCursor){
        uint32_t val[] = { xcbCursor };
        xcb_change_window_attributes(connection, hWnd, XCB_CW_CURSOR, val);
        m_wndCursor[hWnd] = cursor; // update window cursor    
    }
    // SLOG_STMI()<<"SetCursor, hWnd="<<hWnd<<" cursor="<<cursor<<" xcb cursor="<<xcbCursor;
    return ret;
}

xcb_cursor_t SConnection::getXcbCursor(HCURSOR cursor)
{
    if (!cursor)
        cursor = ::LoadCursor(nullptr, IDC_ARROW);
    assert(cursor);
    xcb_cursor_t xcbCursor = 0;
    auto it = m_sysCursor.find(cursor);
    if (it != m_sysCursor.end())
    {
        xcbCursor = it->second;
    }
    else
    {
        xcbCursor = createXcbCursor(cursor);
        if (!xcbCursor)
        {
            SLOG_STMW() << "create xcb cursor failed!";
            return 0;
        }
        m_sysCursor.insert(std::make_pair(cursor, xcbCursor));
    }
    return xcbCursor;
}

BOOL SConnection::DestroyCursor(HCURSOR cursor)
{
    for (auto &it : m_wndCursor)
    {
        if (it.second == cursor)
            return FALSE;
    }
    // look for sys cursor
    auto it = m_sysCursor.find(cursor);
    if (it == m_sysCursor.end())
        return FALSE;
    xcb_free_cursor(connection, it->second);
    m_sysCursor.erase(cursor);
    return TRUE;
}

static uint32_t TsSpan(uint32_t t1, uint32_t t2)
{
    if (t2 == -1u)
        return -1u;
    if (t1 > t2)
    {
        return t1 - t2;
    }
    else
    {
        return t1 + (-1u - t2);
    }
}

static WPARAM ButtonState2Mask(uint16_t state)
{
    WPARAM wp = 0;
    if (state & XCB_KEY_BUT_MASK_SHIFT)
        wp |= MK_SHIFT;
    if (state & XCB_KEY_BUT_MASK_CONTROL)
        wp |= MK_CONTROL;
    if (state & XCB_BUTTON_MASK_1)
        wp |= MK_LBUTTON;
    if (state & XCB_BUTTON_MASK_2)
        wp |= MK_MBUTTON;
    if (state & XCB_BUTTON_MASK_3)
        wp |= MK_RBUTTON;
    return wp;
}

HWND SConnection::GetActiveWnd() const
{
    return m_hWndActive;
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    if (m_hWndActive == hWnd)
        return FALSE;
    if (hWnd)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
            return FALSE;
        if (wndObj->dwStyle & (WS_CHILD | WS_DISABLED))
            return FALSE;
        if (!(wndObj->dwStyle & WS_VISIBLE))
            return FALSE;
        if (wndObj->dwExStyle & (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW))
            return FALSE;
        m_hWndActive = hWnd;
    }
    else
    {
        if (m_hFocus && (IsChild(m_hWndActive, m_hFocus) || m_hFocus == m_hWndActive))
        {
            SetFocus(0);
        }
        m_hWndActive = 0;
    }
        SLOG_STMI()<<"SetActiveWindow hwnd="<<hWnd;
    return TRUE;
}

BOOL SConnection::IsWindow(HWND hWnd) const
{
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(connection, hWnd);
    xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(connection, cookie, NULL);
    if (!reply)
        return FALSE;
    free(reply);
    return TRUE;
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const
{
    uint32_t coords[] = { static_cast<uint32_t>(x), static_cast<uint32_t>(y) };
    xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, coords);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_size_hints_set_position(&hints, true, x, y);
    xcb_size_hints_set_win_gravity(&hints, XCB_GRAVITY_STATIC);
    xcb_set_wm_normal_hints(connection, hWnd, &hints);
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const
{
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    uint32_t coords[] = { static_cast<uint32_t>(cx), static_cast<uint32_t>(cy) };
    xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, coords);

    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_size_hints_set_size(&hints, true, cx, cy);
    xcb_set_wm_normal_hints(connection, hWnd, &hints);
}

BOOL SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const
{
    if (!IsWindow(hWnd))
        return FALSE;
    SetWindowPos(hWnd, x, y);
    SetWindowSize(hWnd, cx, cy);
    xcb_flush(connection);
    return TRUE;
}

HWND SConnection::WindowFromPoint(POINT pt, HWND hWnd) const
{
    if (!hWnd)
        hWnd = screen->root;
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(connection, xcb_query_tree(connection, hWnd), 0);

    xcb_window_t *children = xcb_query_tree_children(reply);
    int num_children = xcb_query_tree_children_length(reply);
    xcb_window_t result = XCB_WINDOW_NONE;

    for (int i = 0; i < num_children; i++)
    {
        xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(connection, xcb_get_geometry(connection, children[i]), NULL);
        if (!geometry)
            continue;

        if (pt.x >= geometry->x && pt.x < (geometry->x + geometry->width) && pt.y >= geometry->y && pt.y < (geometry->y + geometry->height))
        {
            result = children[i];
        }
        free(geometry);
        if (result != XCB_WINDOW_NONE)
            break;
    }
    free(reply);
    if (!result)
        return hWnd;
    else
        return WindowFromPoint(pt, result);
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    // 获取当前鼠标位置的请求
    xcb_query_pointer_cookie_t pointer_cookie = xcb_query_pointer(connection, screen->root);
    xcb_query_pointer_reply_t *pointer_reply = xcb_query_pointer_reply(connection, pointer_cookie, NULL);
    if (!pointer_reply)
    {
        fprintf(stderr, "Failed to get mouse position\n");
        return FALSE;
    }
    ppt->x = pointer_reply->root_x;
    ppt->y = pointer_reply->root_y;
    // 释放资源
    free(pointer_reply);
    return TRUE;
}

int SConnection::GetDpi(BOOL bx) const
{
    if (!screen)
    {
        SLOG_STME() << "screen is null! swinx will crash!!!";
    }
    if (m_forceDpi != -1)
    {
        return m_forceDpi;
    }
    if (bx)
    {
        return floor(25.4 * screen->width_in_pixels / screen->width_in_millimeters + 0.5f);
    }
    else
    {
        return floor(25.4 * screen->height_in_pixels / screen->height_in_millimeters + 0.5f);
    }
}

void SConnection::KillWindowTimer(HWND hWnd)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    auto it = m_lstTimer.begin();
    while (it != m_lstTimer.end())
    {
        auto cur = it++;
        if (cur->hWnd == hWnd)
        {
            m_lstTimer.erase(cur);
        }
    }
}

HWND SConnection::GetForegroundWindow()
{
    xcb_get_input_focus_reply_t *focusReply = nullptr;
    xcb_query_tree_cookie_t treeCookie;

    focusReply = xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr);

    xcb_window_t window = screen->root;
    if (focusReply)
    {
        window = focusReply->focus;
        free(focusReply);
    }

    return window;
}

BOOL SConnection::SetForegroundWindow(HWND hWnd)
{
    uint32_t values[] = { XCB_STACK_MODE_TOP_IF };
    xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_STACK_MODE, values);
    xcb_flush(connection);
    return TRUE;
}

BOOL SConnection::SetWindowOpacity(HWND hWnd, BYTE byAlpha)
{
    uint32_t opacity = (0xffffffff / 0xff) * byAlpha;

    if (opacity == 0xffffffff)
        xcb_delete_property(connection, hWnd, atoms._NET_WM_WINDOW_OPACITY);
    else
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms._NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 32, 1, &opacity);
    xcb_flush(connection);
    return TRUE;
}

BOOL SConnection::SetWindowRgn(HWND hWnd, HRGN hRgn)
{
    if (hRgn)
    {
        std::vector<xcb_rectangle_t> rects;
        DWORD len = GetRegionData(hRgn, 0, nullptr);
        if (!len)
            return FALSE;
        RGNDATA *pData = (RGNDATA *)malloc(len);
        GetRegionData(hRgn, len, pData);
        rects.resize(pData->rdh.nCount);
        xcb_rectangle_t *dst = rects.data();
        RECT *src = (RECT *)pData->Buffer;
        for (DWORD i = 0; i < pData->rdh.nCount; i++)
        {
            dst->x = src->left;
            dst->y = src->top;
            dst->width = src->right - src->left;
            dst->height = src->bottom - src->top;
            src++;
            dst++;
        }
        free(pData);
        xcb_shape_rectangles(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, XCB_CLIP_ORDERING_UNSORTED, hWnd, 0, 0, rects.size(), &rects[0]);
    }
    else
    {
        xcb_shape_mask(connection, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, hWnd, 0, 0, XCB_NONE);
    }
    return TRUE;
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl)
{
    HKL ret = m_hkl;
    m_hkl = hKl;
    return ret;
}

void SConnection::OnFocusChanged(HWND hFocus)
{
    if (hFocus == m_hFocus)
        return;
    if (m_hFocus)
    {
        HIMC hIMC = ImmGetContext(m_hFocus);
        if (hIMC)
        {
            if (hIMC->xic)
            {
                xcb_xim_close(m_xim);
            }
            ImmReleaseContext(m_hFocus, hIMC);
        }
        Msg *pMsg = new Msg;
        pMsg->hwnd = m_hFocus;
        pMsg->message = WM_KILLFOCUS;
        pMsg->wParam = (WPARAM)hFocus;
        pMsg->lParam = 0;
        postMsg(pMsg);
    }
    HWND hOldFocus = m_hFocus;
    m_hFocus = hFocus;
    if (hFocus)
    {
        HIMC hIMC = ImmGetContext(hFocus);
        if (hIMC)
        {
            xcb_xim_open(m_xim, xim_open_callback, true, (void *)hFocus);
            ImmReleaseContext(hFocus, hIMC);
        }
        Msg *pMsg = new Msg;
        pMsg->hwnd = m_hFocus;
        pMsg->message = WM_SETFOCUS;
        pMsg->wParam = (WPARAM)hOldFocus;
        pMsg->lParam = 0;
        postMsg(pMsg);
    }
}

BOOL SConnection::SetFocus(HWND hWnd)
{
    if (hWnd == m_hFocus)
    {
        return TRUE;
    }

    HWND hRet = m_hFocus;
    xcb_void_cookie_t cookie = xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_POINTER_ROOT, hWnd ? hWnd : screen->root, XCB_CURRENT_TIME);
    xcb_generic_error_t *error = xcb_request_check(connection, cookie);
    xcb_flush(connection);
    if (error)
    {
        SLOG_STMI() << "SetFocus error, code=" << (int)error->error_code << " hWnd=" << hWnd;
        free(error);
        return FALSE;
    }
    else
    {
        SLOG_STMI() << "SetFocus, oldFocus=" << hRet << " newFocus=" << hWnd;
        OnFocusChanged(hWnd);
        return TRUE;
    }
}

#ifdef ENABLE_PRINTATOMNAME
static void printAtomName(xcb_connection_t *connection, xcb_atom_t atom)
{
    xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(connection, atom);
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(connection, cookie, nullptr);
    if (reply)
    {
        char *atom_name = xcb_get_atom_name_name(reply);
        if (atom_name)
        {
            printf("Atom %d name: %s\n", atom, atom_name);
        }
        free(reply);
    }
}
#endif // ENABLE_PRINTATOMNAME

uint32_t SConnection::netWmStates(HWND hWnd)
{
    uint32_t result(0);

    xcb_get_property_cookie_t get_cookie = xcb_get_property_unchecked(connection, 0, hWnd, atoms._NET_WM_STATE, XCB_ATOM_ATOM, 0, 1024);

    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, get_cookie, NULL);

    if (reply && reply->format == 32 && reply->type == XCB_ATOM_ATOM)
    {
        const xcb_atom_t *states = static_cast<const xcb_atom_t *>(xcb_get_property_value(reply));
        for (int i = 0; i < reply->length; i++)
        {
            if (states[i] == atoms._NET_WM_STATE_ABOVE)
                result |= NetWmStateAbove;
            if (states[i] == atoms._NET_WM_STATE_BELOW)
                result |= NetWmStateBelow;
            if (states[i] == atoms._NET_WM_STATE_FULLSCREEN)
                result |= NetWmStateFullScreen;
            if (states[i] == atoms._NET_WM_STATE_MAXIMIZED_HORZ)
                result |= NetWmStateMaximizedHorz;
            if (states[i] == atoms._NET_WM_STATE_MAXIMIZED_VERT)
                result |= NetWmStateMaximizedVert;
            if (states[i] == atoms._NET_WM_STATE_STAYS_ON_TOP)
                result |= NetWmStateStaysOnTop;
            if (states[i] == atoms._NET_WM_STATE_DEMANDS_ATTENTION)
                result |= NetWmStateDemandsAttention;
            if (states[i] == atoms._NET_WM_STATE_FOCUSED)
                result |= NetWMStateFocus;
        }
        free(reply);
    }
    else
    {
#ifdef NET_WM_STATE_DEBUG
        SLOG_FMTI("getting net wm state (%x), empty", m_window);
#endif
    }

    return result;
}

DWORD SConnection::XdndAction2Effect(xcb_atom_t action)
{
    if (action == atoms.XdndActionMove)
    {
        return DROPEFFECT_MOVE;
    }
    else if (action == atoms.XdndActionCopy)
    {
        return DROPEFFECT_COPY;
    }
    else if (action == atoms.XdndActionLink)
    {
        return DROPEFFECT_LINK;
    }
    else
    {
        return DROPEFFECT_NONE;
    }
}

xcb_atom_t SConnection::XdndEffect2Action(DWORD dwEffect)
{
    xcb_atom_t action = XCB_NONE;
    if (dwEffect & DROPEFFECT_MOVE)
        action = atoms.XdndActionMove;
    else if (dwEffect & DROPEFFECT_LINK)
        action = atoms.XdndActionCopy;
    else if (dwEffect & DROPEFFECT_COPY)
        action = atoms.XdndActionCopy;
    return action;
}

bool SConnection::pushEvent(xcb_generic_event_t *event)
{
    uint8_t event_code = event->response_type & 0x7f;
    Msg *pMsg = nullptr;
    bool ret = false;
    switch (event_code)
    {
    case XCB_SELECTION_REQUEST:
    {
        xcb_selection_request_event_t *e2 = (xcb_selection_request_event_t *)event;
        m_tsSelection = e2->time;
        m_clipboard->handleSelectionRequest(e2);
        return false;
    }
    case XCB_SELECTION_CLEAR:
    {
        xcb_selection_clear_event_t *e2 = (xcb_selection_clear_event_t *)event;
        m_tsSelection = e2->time;
        m_clipboard->handleSelectionClear(e2);
        return false;
    }
    case XCB_SELECTION_NOTIFY:
    {
        // todo:hjx
        xcb_selection_notify_event_t *e2 = (xcb_selection_notify_event_t *)event;
        m_tsSelection = e2->time;
        return false;
    }
    case XCB_LEAVE_NOTIFY:
    {
        xcb_leave_notify_event_t *e2 = (xcb_leave_notify_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = WM_MOUSELEAVE;
        pMsg->wParam = pMsg->lParam = 0;
        break;
    }
    case XCB_ENTER_NOTIFY:
    {
        xcb_enter_notify_event_t *e2 = (xcb_enter_notify_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = WM_MOUSEHOVER;
        pMsg->wParam = 0;
        pMsg->lParam = MAKELPARAM(e2->event_x, e2->event_y);
        break;
    }
    case XCB_KEY_PRESS:
    {
        xcb_key_press_event_t *e2 = (xcb_key_press_event_t *)event;
        m_tsSelection = e2->time;
        pMsg = new Msg;
        pMsg->hwnd = m_hFocus;

        UINT vk = m_keyboard->onKeyEvent(true, e2->detail, e2->state, e2->time);
        pMsg->message = (vk < VK_NUMLOCK || (vk >= VK_OEM_1 && vk <= VK_OEM_8)) ? WM_KEYDOWN : WM_SYSKEYDOWN;
        pMsg->wParam = vk;
        BYTE scanCode = (BYTE)e2->detail;
        uint32_t tst = (scanCode << 16);
        int cn = m_keyboard->getRepeatCount();
        pMsg->lParam = (scanCode << 16) | m_keyboard->getRepeatCount();
        // SLOG_FMTI("onkeydown, detail=%d,vk=%d, repeat=%d", e2->detail, vk, (int)m_keyboard->getRepeatCount());
        break;
    }
    case XCB_KEY_RELEASE:
    {
        xcb_key_release_event_t *e2 = (xcb_key_release_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = m_hFocus;

        UINT vk = m_keyboard->onKeyEvent(false, e2->detail, e2->state, e2->time);
        pMsg->message = vk < VK_NUMLOCK ? WM_KEYUP : WM_SYSKEYUP;
        pMsg->wParam = vk;
        BYTE scanCode = (BYTE)e2->detail;
        pMsg->lParam = scanCode << 16;
        // SLOG_FMTI("onkeyup, detail=%d,vk=%d", e2->detail, vk);
        break;
    }
    case XCB_EXPOSE:
    {
        xcb_expose_event_t *expose = (xcb_expose_event_t *)event;
        RECT rc = { expose->x, expose->y, expose->x + expose->width, expose->y + expose->height };
        HRGN hrgn = CreateRectRgnIndirect(&rc);
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); it++)
        {
            if ((*it)->message == WM_PAINT && (*it)->hwnd == expose->window)
            {
                MsgPaint *oldMsg = (MsgPaint *)(*it);
                CombineRgn(oldMsg->rgn, oldMsg->rgn, hrgn, RGN_OR);
                DeleteObject(hrgn);
                return true;
            }
        }
        MsgPaint *pMsgPaint = new MsgPaint(hrgn);
        pMsgPaint->hwnd = expose->window;
        pMsgPaint->message = WM_PAINT;
        pMsgPaint->wParam = 0;
        pMsgPaint->lParam = (LPARAM)pMsgPaint->rgn;
        pMsgPaint->time = GetTickCount64();
        pMsg = pMsgPaint;
        break;
    }
    case XCB_PROPERTY_NOTIFY:
    {
        xcb_property_notify_event_t *e2 = (xcb_property_notify_event_t *)event;
        if (e2->atom == atoms._NET_WORKAREA)
        {
            updateWorkArea();
        }
        else if (e2->atom == atoms._NET_WM_STATE || e2->atom == atoms.WM_STATE)
        {
            uint32_t newState = -1;
            if (e2->atom == atoms.WM_STATE)
            {
                const xcb_get_property_cookie_t get_cookie = xcb_get_property(connection, 0, e2->window, atoms.WM_STATE, XCB_ATOM_ANY, 0, 1024);
                xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, get_cookie, nullptr);
                if (reply && reply->format == 32 && reply->type == atoms.WM_STATE && reply->length != 0)
                {
                    const uint32_t *data = (const uint32_t *)xcb_get_property_value(reply);
                    if (data[0] == XCB_ICCCM_WM_STATE_ICONIC /* || data[0]==XCB_ICCCM_WM_STATE_WITHDRAWN*/)
                    {
                        newState = SIZE_MINIMIZED;
                    }
                    else if (data[0] == XCB_ICCCM_WM_STATE_NORMAL)
                    {
                        newState = SIZE_RESTORED;
                    }
                }
                free(reply);
            }
            if (newState != SIZE_MINIMIZED)
            {
                uint32_t state = netWmStates(e2->window);
                if ((state & (NetWmStateMaximizedHorz | NetWmStateMaximizedVert)) == (NetWmStateMaximizedHorz | NetWmStateMaximizedVert))
                {
                    newState = SIZE_MAXIMIZED;
                }
                else if ((state & (NetWmStateMaximizedHorz | NetWmStateMaximizedVert)) == 0)
                {
                    newState = SIZE_RESTORED;
                }
                if (state & NetWMStateFocus && m_hWndActive != e2->window)
                {
                    OnActiveChange(e2->window, TRUE);
                }
                if ((state & NetWMStateFocus) == 0 && m_hWndActive == e2->window)
                {
                    OnActiveChange(e2->window, FALSE);
                }
            }
            if (newState != -1)
            {
                pMsg = new Msg;
                pMsg->hwnd = e2->window;
                pMsg->message = UM_STATE;
                pMsg->wParam = newState;
            }
        }
        break;
    }
    case XCB_CONFIGURE_NOTIFY:
    {
        xcb_configure_notify_event_t *e2 = (xcb_configure_notify_event_t *)event;
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); it++)
        {
            if ((*it)->message == WM_MOVE && (*it)->hwnd == e2->window)
            {
                delete *it;
                m_msgQueue.erase(it);
                break;
            }
        }
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); it++)
        {
            if ((*it)->message == WM_SIZE && (*it)->hwnd == e2->window)
            {
                delete *it;
                m_msgQueue.erase(it);
                break;
            }
        }
        bool fromSendEvent = (event->response_type & 0x80);
        POINT pos = { e2->x, e2->y };
        if (!GetParent(e2->window) && !fromSendEvent)
        {
            // Do not trust the position, query it instead.
            xcb_translate_coordinates_cookie_t cookie = xcb_translate_coordinates(connection, e2->window, screen->root, 0, 0);
            xcb_translate_coordinates_reply_t *reply = xcb_translate_coordinates_reply(connection, cookie, NULL);
            if (reply)
            {
                pos.x = reply->dst_x;
                pos.y = reply->dst_y;
                free(reply);
            }
        }
        RECT rc;
        GetWindowRect(e2->window, &rc);
        if (rc.left != pos.x || rc.top != pos.y)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->window;
            pMsg->message = WM_MOVE;
            pMsg->wParam = 0;
            pMsg->lParam = MAKELPARAM(pos.x, pos.y);
            GetCursorPos(&pMsg->pt);
            m_msgQueue.push_back(pMsg);
        }
        if (rc.right - rc.left != e2->width || rc.bottom - rc.top != e2->height)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->window;
            pMsg->message = WM_SIZE;
            pMsg->wParam = 0;
            pMsg->lParam = MAKELPARAM(e2->width, e2->height);
            GetCursorPos(&pMsg->pt);
            m_msgQueue.push_back(pMsg);
        }
        pMsg = nullptr;
        break;
    }
    case XCB_CLIENT_MESSAGE:
    {
        xcb_client_message_event_t *e2 = (xcb_client_message_event_t *)event;
        if (e2->type == atoms.WM_WIN4XCB_IPC)
        {
            // ipc message
            pMsg = new IpcMsg(e2->window, e2->data.data32);
        }
        else if (e2->type == atoms.WM_PROTOCOLS)
        {
            if (e2->data.data32[0] == atoms.WM_DELETE_WINDOW)
            {
                pMsg = new Msg;
                pMsg->message = WM_CLOSE;
                pMsg->hwnd = e2->window;
                pMsg->wParam = pMsg->lParam = 0;
            }
        }
        else if (e2->type == atoms._NET_WM_STATE_HIDDEN)
        {
            pMsg = new Msg;
            pMsg->message = WM_SHOWWINDOW;
            pMsg->hwnd = e2->window;
            pMsg->wParam = e2->data.data32[0];
            pMsg->lParam = 0;
        }
        else if (e2->type == atoms.XdndEnter)
        {
            WndObj wndObj = WndMgr::fromHwnd(e2->window);
            if (!wndObj)
                break;
            int version = (int)(e2->data.data32[1] >> 24);
            if (version > SDragDrop::xdnd_version)
                break;
            SLOG_STMI() << "####drag enter, init dragData";
            XDndDataObjectProxy *pDataObject = new XDndDataObjectProxy(this, e2->data.data32[0], e2->data.data32);
            DragEnterMsg *pMsg2 = new DragEnterMsg(pDataObject);
            pDataObject->Release();
            pMsg2->hwnd = e2->window;
            pMsg2->message = UM_XDND_DRAG_ENTER;
            pMsg2->hFrom = e2->data.data32[0];

            POINT pt;
            GetCursorPos(&pt);
            pMsg2->DragEnterData::pt.x = pt.x;
            pMsg2->DragEnterData::pt.y = pt.y;

            pMsg = pMsg2;
        }
        else if (e2->type == atoms.XdndPosition)
        {
            // remove old position
            std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
            for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); it++)
            {
                if ((*it)->message == UM_XDND_DRAG_OVER && (*it)->hwnd == e2->window)
                {
                    m_msgQueue.erase(it);
                    break;
                }
            }
            WndObj wndObj = WndMgr::fromHwnd(e2->window);
            if (!wndObj || !wndObj->dragData)
                break;
            XDndDataObjectProxy *pData = (XDndDataObjectProxy *)wndObj->dragData;
            if (pData->getSource() != e2->data.data32[0])
                break;
            if (e2->data.data32[3] != XCB_NONE)
                pData->m_targetTime = e2->data.data32[3];
            DragOverMsg *pMsg2 = new DragOverMsg;
            pMsg2->hwnd = e2->window;
            pMsg2->message = UM_XDND_DRAG_OVER;
            pMsg2->hFrom = e2->data.data32[0];
            pMsg2->dwKeyState = e2->data.data32[1];
            pMsg2->supported_actions = XdndAction2Effect(e2->data.data32[4]) | DROPEFFECT_COPY; // only one action is transfered from source, combine copy operation.
            pMsg2->DragOverData::pt.x = HIWORD(e2->data.data32[2]);
            pMsg2->DragOverData::pt.y = LOWORD(e2->data.data32[2]);
            pMsg = pMsg2;
        }
        else if (e2->type == atoms.XdndLeave)
        {
            Msg *pMsg2 = new Msg;
            pMsg2->hwnd = e2->window;
            pMsg2->message = UM_XDND_DRAG_LEAVE;
            pMsg2->wParam = e2->data.data32[0];
            pMsg = pMsg2;
        }
        else if (e2->type == atoms.XdndDrop)
        {
            DragDropMsg *pMsg2 = new DragDropMsg;
            pMsg2->hwnd = e2->window;
            pMsg2->message = UM_XDND_DRAG_DROP;
            pMsg2->hFrom = e2->data.data32[0];
            pMsg2->wParam = e2->data.data32[4];
            pMsg = pMsg2;
        }
        else if (e2->type == atoms.XdndFinished)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->window;
            pMsg->message = UM_XDND_FINISH;
            pMsg->wParam = e2->data.data32[1]; // accept flag
            pMsg->lParam = XdndAction2Effect(e2->data.data32[2]);
        }
        else if (e2->type == atoms.XdndStatus)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->window;
            pMsg->message = UM_XDND_STATUS;
            pMsg->wParam = e2->data.data32[1]; // accept flag
            pMsg->lParam = XdndAction2Effect(e2->data.data32[4]);
        }
    }
    break;
    case XCB_BUTTON_PRESS:
    {
        xcb_button_press_event_t *e2 = (xcb_button_press_event_t *)event;
        m_tsSelection = e2->time;
        if (e2->detail >= XCB_BUTTON_INDEX_1 && e2->detail <= XCB_BUTTON_INDEX_3)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->event;
            pMsg->pt.x = e2->event_x;
            pMsg->pt.y = e2->event_y;
            if (m_hWndCapture != 0 && e2->event != m_hWndCapture)
            {
                MapWindowPoints(e2->event, m_hWndCapture, &pMsg->pt, 1);
                pMsg->hwnd = m_hWndCapture;
            }
            pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);
            switch (e2->detail)
            {
            case XCB_BUTTON_INDEX_1: // left button
                pMsg->message = TsSpan(e2->time, m_tsPrevPress[0]) > m_tsDoubleSpan ? WM_LBUTTONDOWN : WM_LBUTTONDBLCLK;
                m_tsPrevPress[0] = e2->time;
                break;
            case XCB_BUTTON_INDEX_2:
                pMsg->message = TsSpan(e2->time, m_tsPrevPress[1]) > m_tsDoubleSpan ? WM_MBUTTONDOWN : WM_MBUTTONDBLCLK;
                m_tsPrevPress[1] = e2->time;
                break;
            case XCB_BUTTON_INDEX_3:
                pMsg->message = TsSpan(e2->time, m_tsPrevPress[2]) > m_tsDoubleSpan ? WM_RBUTTONDOWN : WM_RBUTTONDBLCLK;
                m_tsPrevPress[2] = e2->time;
                break;
            }
            if (e2->detail <= XCB_BUTTON_INDEX_3)
            {
                uint8_t vk = 0;
                switch (e2->detail)
                {
                case XCB_BUTTON_INDEX_1:
                    vk = VK_LBUTTON;
                    break;
                case XCB_BUTTON_INDEX_2:
                    vk = VK_MBUTTON;
                    break;
                case XCB_BUTTON_INDEX_3:
                    vk = VK_RBUTTON;
                    break;
                }
                m_keyboard->setKeyState(vk, 0x80);
            }
            pMsg->wParam = ButtonState2Mask(e2->state);
        }
        else if (e2->detail == XCB_BUTTON_INDEX_4 || e2->detail == XCB_BUTTON_INDEX_5)
        {
            // mouse wheel event
            // SLOG_STMI() << "mouse wheel, dir = " << (e2->detail == XCB_BUTTON_INDEX_4 ? "up" : "down");
            pMsg = new Msg;
            pMsg->hwnd = e2->event;
            pMsg->message = WM_MOUSEWHEEL;

            pMsg->pt.x = e2->event_x;
            pMsg->pt.y = e2->event_y;
            ClientToScreen(pMsg->hwnd, &pMsg->pt);
            pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);

            WORD vkFlag = 0;
            vkFlag |= GetKeyState(VK_CONTROL) ? MK_CONTROL : 0;
            vkFlag |= GetKeyState(VK_LBUTTON) ? MK_LBUTTON : 0;
            vkFlag |= GetKeyState(VK_MBUTTON) ? MK_MBUTTON : 0;
            vkFlag |= GetKeyState(VK_RBUTTON) ? MK_RBUTTON : 0;
            vkFlag |= GetKeyState(VK_SHIFT) ? MK_SHIFT : 0;
            int delta = e2->detail == XCB_BUTTON_INDEX_4 ? WHEEL_DELTA : -WHEEL_DELTA;
            pMsg->wParam = MAKEWPARAM(vkFlag, delta);
        }
        if (pMsg && !IsWindowEnabled(pMsg->hwnd))
        {
            delete pMsg;
            pMsg = nullptr;
        }
        break;
    }
    case XCB_BUTTON_RELEASE:
    {
        xcb_button_release_event_t *e2 = (xcb_button_release_event_t *)event;
        if (e2->detail >= XCB_BUTTON_INDEX_1 && e2->detail <= XCB_BUTTON_INDEX_3)
        {
            pMsg = new Msg;
            pMsg->hwnd = e2->event;
            pMsg->pt.x = e2->event_x;
            pMsg->pt.y = e2->event_y;
            if (m_hWndCapture != 0 && e2->event != m_hWndCapture)
            {
                MapWindowPoints(e2->event, m_hWndCapture, &pMsg->pt, 1);
                pMsg->hwnd = m_hWndCapture;
            }
            pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);
            uint8_t vk = 0;
            switch (e2->detail)
            {
            case XCB_BUTTON_INDEX_1: // left button
                pMsg->message = WM_LBUTTONUP;
                vk = VK_LBUTTON;
                break;
            case XCB_BUTTON_INDEX_2:
                pMsg->message = WM_MBUTTONUP;
                vk = VK_MBUTTON;
                break;
            case XCB_BUTTON_INDEX_3:
                pMsg->message = WM_RBUTTONUP;
                vk = VK_RBUTTON;
                break;
            }
            m_keyboard->setKeyState(vk, 0);
            pMsg->wParam = ButtonState2Mask(e2->state);
        }
        if (pMsg && !IsWindowEnabled(pMsg->hwnd))
        {
            delete pMsg;
            pMsg = nullptr;
        }
        break;
    }
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t *e2 = (xcb_motion_notify_event_t *)event;
        // remove old mouse move
        static const int16_t kMinPosDiff = 3;
        WPARAM wp = ButtonState2Mask(e2->state);
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); it++)
        {
            if ((*it)->message == WM_MOUSEMOVE && (*it)->hwnd == e2->event && (*it)->wParam == wp)
            {
                int16_t diff = abs((*it)->pt.x - e2->event_x) + abs((*it)->pt.y - e2->event_y);
                if (diff <= kMinPosDiff)
                {
                    m_msgQueue.erase(it);
                    break;
                }
            }
        }
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = WM_MOUSEMOVE;
        POINT pt = { e2->event_x, e2->event_y };
        if (m_hWndCapture != 0 && e2->event != m_hWndCapture)
        {
            // SLOG_STMI()<<"remap mousemove to capture: capture="<<m_hWndCapture<<" event window="<<e2->event;
            MapWindowPoints(e2->event, m_hWndCapture, &pt, 1);
            pMsg->hwnd = m_hWndCapture;
        }
        pMsg->lParam = MAKELPARAM(pt.x, pt.y);
        pMsg->wParam = wp;
        pMsg->time = e2->time;
        // different from other mouse message, dispatch mousemove dispite whether the target window is disable or not. we need it to generate WM_SETCURSOR
        break;
    }
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
    {
        xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
        xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(connection, cookie, nullptr);
        if (reply)
        {
            OnFocusChanged(reply->focus);
            free(reply);
        }
        break;
    }
    case XCB_MAP_NOTIFY:
    {
        xcb_map_notify_event_t *e2 = (xcb_map_notify_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = UM_MAPNOTIFY;
        pMsg->wParam = 1;
        break;
    }
    case XCB_UNMAP_NOTIFY:
    {
        xcb_unmap_notify_event_t *e2 = (xcb_unmap_notify_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = UM_MAPNOTIFY;
        pMsg->wParam = 0;
        break;
    }
    case XCB_MAPPING_NOTIFY:
    {
        xcb_mapping_notify_event_t *e2 = (xcb_mapping_notify_event_t *)event;
        m_keyboard->onMappingNotifyEvent(e2);
        break;
    }
    default:
        // SLOG_STMI()<<"unknown event code:"<<event_code;
        break;
    }
    if (pMsg)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        GetCursorPos(&pMsg->pt);
        m_msgQueue.push_back(pMsg);
    }
    return ret;
}

void *SConnection::readProc(void *p)
{
    SConnection *_this = static_cast<SConnection *>(p);
    _this->_readProc();
    return p;
}

void SConnection::_readProc()
{
    while (!m_bQuit)
    {
        xcb_generic_event_t *event = xcb_wait_for_event(connection);
        if (!event)
        {
            continue;
        }
        if ((event->response_type & 0x7f) == XCB_CLIENT_MESSAGE && ((xcb_client_message_event_t *)event)->type == atoms.WM_DISCONN)
        {
            m_bQuit = true;
            SetEvent(m_evtSync);
            //            SLOG_STMI() << "recv WM_DISCONN, quit event reading thread";
            break;
        }
        {
            std::unique_lock<std::mutex> lock(m_mutex4Evt);
            m_evtQueue.push_back(event);
            SetEvent(m_evtSync);
        }
    }

    m_mutex4Evt.lock();
    for (auto it : m_evtQueue)
    {
        free(it);
    }
    m_evtQueue.clear();
    m_mutex4Evt.unlock();

    // SLOG_STMI() << "event reader done";
}

void SConnection::updateWorkArea()
{
    memset(&m_rcWorkArea, 0, sizeof(RECT));
    xcb_get_property_reply_t *workArea = xcb_get_property_reply(connection, xcb_get_property_unchecked(connection, false, screen->root, atoms._NET_WORKAREA, XCB_ATOM_CARDINAL, 0, 1024), NULL);
    if (workArea && workArea->type == XCB_ATOM_CARDINAL && workArea->format == 32 && workArea->value_len >= 4)
    {
        // If workArea->value_len > 4, the remaining ones seem to be for WM's virtual desktops
        // (don't mess with QXcbVirtualDesktop which represents an X screen).
        // But QScreen doesn't know about that concept.  In reality there could be a
        // "docked" panel (with _NET_WM_STRUT_PARTIAL atom set) on just one desktop.
        // But for now just assume the first 4 values give us the geometry of the
        // "work area", AKA "available geometry"
        uint32_t *geom = (uint32_t *)xcb_get_property_value(workArea);
        m_rcWorkArea.left = geom[0];
        m_rcWorkArea.top = geom[1];
        m_rcWorkArea.right = m_rcWorkArea.left + geom[2];
        m_rcWorkArea.bottom = m_rcWorkArea.top + geom[3];
    }
    free(workArea);
    SLOG_STMI() << "updateWorkArea, rc=" << m_rcWorkArea.left << "," << m_rcWorkArea.top << "," << m_rcWorkArea.right << "," << m_rcWorkArea.bottom;
}

void SConnection::GetWorkArea(HMONITOR hMonitor, RECT *prc)
{
    memcpy(prc, &m_rcWorkArea, sizeof(RECT));
}

//--------------------------------------------------------------------------
// clipboard api

BOOL SConnection::EmptyClipboard()
{
    return m_clipboard->emptyClipboard();
}

BOOL SConnection::IsClipboardFormatAvailable(_In_ UINT format)
{
    if (!GetClipboardOwner())
        return FALSE;
    return m_clipboard->hasFormat(format);
}

BOOL SConnection::OpenClipboard(HWND hWndNewOwner)
{
    return m_clipboard->openClipboard(hWndNewOwner);
}

BOOL SConnection::CloseClipboard()
{
    return m_clipboard->closeClipboard();
}

HWND SConnection::GetClipboardOwner()
{
    return m_clipboard->getClipboardOwner();
}

HANDLE
SConnection::GetClipboardData(UINT uFormat)
{
    HANDLE ret = m_clipboard->getClipboardData(uFormat);
    if (uFormat == CF_UNICODETEXT)
    {
        const char *src = (const char *)GlobalLock(ret);
        size_t len = GlobalSize(ret);
        std::wstring wstr;
        towstring(src, len, wstr);
        GlobalUnlock(ret);
        ret = GlobalReAlloc(ret, wstr.length() * sizeof(wchar_t), 0);
        void *buf = GlobalLock(ret);
        memcpy(buf, wstr.c_str(), wstr.length() * sizeof(wchar_t));
        GlobalUnlock(ret);
    }
    return ret;
}

HANDLE SConnection::SetClipboardData(UINT uFormat, HANDLE hMem)
{
    return m_clipboard->setClipboardData(uFormat, hMem);
}

BOOL SConnection::IsDropTarget(HWND hWnd)
{
    xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms.XdndAware, XCB_GET_PROPERTY_TYPE_ANY, 0, 1024);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
    int ret = 0;
    if (reply)
    {
        if (reply->type != XCB_NONE)
        {
            char *data = (char *)xcb_get_property_value(reply);
            if (data[0] <= SDragDrop::xdnd_version)
            {
                ret = data[0];
            }
        }
        free(reply);
    }
    //    SLOG_STMD() << "IsDropTarget for " << hWnd << " ret=" << ret;
    return ret;
}

VOID CALLBACK OnFlashWindowTimeout(HWND hWnd, UINT, UINT_PTR, DWORD)
{
}

BOOL SConnection::FlashWindowEx(PFLASHWINFO info)
{
    if (info->dwFlags == FLASHW_STOP)
    { // stop flash for the window
        changeNetWmState(info->hwnd, false, atoms._NET_WM_STATE_DEMANDS_ATTENTION, 0);
    }
    else
    { // start flash
        changeNetWmState(info->hwnd, true, atoms._NET_WM_STATE_DEMANDS_ATTENTION, 0);
        if (info->dwFlags != FLASHW_TIMER)
        { //
            SetTimer(info->hwnd, TM_FLASH, info->dwTimeout * info->uCount, OnFlashWindowTimeout);
        }
    }
    return 0;
}

void SConnection::changeNetWmState(HWND hWnd, bool set, xcb_atom_t one, xcb_atom_t two)
{
    xcb_client_message_event_t event;
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = hWnd;
    event.type = atoms._NET_WM_STATE;
    event.data.data32[0] = set ? 1 : 0;
    event.data.data32[1] = one;
    event.data.data32[2] = two;
    event.data.data32[3] = 0;
    event.data.data32[4] = 0;

    xcb_send_event(connection, 0, screen->root, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);
}

int SConnection::OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        return GetAtomNameA(wndObj->clsAtom, lpClassName, nMaxCount);
    }
    else
    {
        int ret = 0;
        xcb_get_property_cookie_t cookie = xcb_icccm_get_wm_class(connection, hWnd);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply)
        {
            xcb_icccm_get_wm_class_reply_t clsReply = { 0 };
            xcb_icccm_get_wm_class_from_reply(&clsReply, reply);
            if (clsReply.class_name)
            {
                int len = strlen(clsReply.class_name);
                if (len <= nMaxCount)
                {
                    ret = len;
                    memcpy(lpClassName, clsReply.class_name, len);
                    if (len < nMaxCount)
                    {
                        lpClassName[len] = 0;
                        ret++;
                    }
                }
                else
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                }
            }
            free(reply);
        }
        return ret;
    }
}

BOOL SConnection::OnSetWindowText(HWND hWnd, _Window *wndObj, LPCSTR lpszString)
{
    wndObj->title = lpszString ? lpszString : "";
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms.WM_NAME, atoms.UTF8_STRING, 8, wndObj->title.length(), wndObj->title.c_str());
    xcb_flush(connection);
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        return wndObj->title.length();
    }
    else
    {
        int ret = 0;
        xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms.WM_NAME, atoms.UTF8_STRING, 0, UINT_MAX);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply)
        {
            ret = xcb_get_property_value_length(reply);
            free(reply);
        }
        return ret;
    }
}

int SConnection::OnGetWindowTextLengthW(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
    }
    else
    {
        int ret = 0;
        xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms.WM_NAME, atoms.UTF8_STRING, 0, UINT_MAX);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply)
        {
            int len = xcb_get_property_value_length(reply);
            const char *text = (const char *)xcb_get_property_value(reply);
            ret = MultiByteToWideChar(CP_UTF8, 0, text, len, nullptr, 0);
            free(reply);
        }
        return ret;
    }
}

int SConnection::OnGetWindowTextA(HWND hWnd, char *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        if (bufLen < wndObj->title.length())
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        strcpy(buf, wndObj->title.c_str());
        return wndObj->title.length();
    }
    else
    {
        int ret = 0;
        xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms.WM_NAME, atoms.UTF8_STRING, 0, UINT_MAX);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply)
        {
            int len = xcb_get_property_value_length(reply);
            if (len <= bufLen)
            {
                const char *text = (const char *)xcb_get_property_value(reply);
                memcpy(buf, text, len);
                ret = len;
                if (len < bufLen)
                {
                    buf[len] = 0;
                }
            }
            free(reply);
        }
        return ret;
    }
}

int SConnection::OnGetWindowTextW(HWND hWnd, wchar_t *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), buf, bufLen);
    }
    else
    {
        int ret = 0;
        xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, hWnd, atoms.WM_NAME, atoms.UTF8_STRING, 0, UINT_MAX);
        xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
        if (reply)
        {
            int len = xcb_get_property_value_length(reply);
            if (len <= bufLen)
            {
                const char *text = (const char *)xcb_get_property_value(reply);
                ret = MultiByteToWideChar(CP_UTF8, 0, text, len, buf, bufLen);
            }
            free(reply);
        }
        return ret;
    }
}

struct StFindWindow
{
    SConnection *conn;
    LPCSTR lpClassName;
    LPCSTR lpWindowName;
    HWND hRet;
};

static BOOL CALLBACK CbFindWindow(HWND hWnd, LPARAM lp)
{
    StFindWindow *param = (StFindWindow *)lp;
    BOOL bMatch = TRUE;
    if (param->lpWindowName)
    {
        char szTxt[1000];
        if (param->conn->OnGetWindowTextA(hWnd, szTxt, 1000))
        {
            bMatch = strcmp(param->lpWindowName, szTxt) == 0;
        }
        else
        {
            bMatch = FALSE;
        }
        //        SLOG_STMI()<<"OnGetWindowText for "<<hWnd<<" got "<<szTxt;
    }
    if (bMatch && param->lpClassName)
    {
        char szCls[1000];
        if (param->conn->OnGetClassName(hWnd, szCls, 1000))
        {
            bMatch = strcmp(szCls, param->lpClassName) == 0;
        }
        else
        {
            bMatch = FALSE;
        }
    }
    if (bMatch)
    {
        param->hRet = hWnd;
    }
    return !bMatch;
}

HWND SConnection::OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName)
{
    char szTstCls[1000] = { 0 };
    if (lpClassName && IS_INTRESOURCE(lpClassName))
    {
        GetAtomNameA((int)(intptr_t)lpClassName, szTstCls, 1000);
        lpClassName = szTstCls;
    }
    StFindWindow param = { this, lpClassName, lpWindowName, 0 };
    OnEnumWindows(hParent, hChildAfter, CbFindWindow, (LPARAM)&param);
    return param.hRet;
}

BOOL SConnection::OnEnumWindows(HWND hParent, HWND hChildAfter, WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
    if (!hParent)
        hParent = screen->root;
    xcb_query_tree_cookie_t tree_cookie = xcb_query_tree(connection, hParent);
    xcb_query_tree_reply_t *tree_reply = xcb_query_tree_reply(connection, tree_cookie, NULL);
    if (!tree_reply)
        return FALSE;
    xcb_window_t *children = xcb_query_tree_children(tree_reply);
    int i = 0;
    if (hChildAfter)
    {
        while (i < tree_reply->children_len)
        {
            if (children[i] == hChildAfter)
                break;
            i++;
        }
    }
    BOOL bContinue = TRUE;
    for (; bContinue && i < tree_reply->children_len; i++)
    {
        bContinue = lpEnumFunc(children[i], lParam);
    }
    free(tree_reply);
    return TRUE;
}

void SConnection::OnActiveChange(HWND hWnd, BOOL bActivate)
{
    if (bActivate)
    {
        HWND oldActive = m_hWndActive;
        m_hWndActive = hWnd;
        if (hWnd)
            SendMessageA(hWnd, WM_ACTIVATE, WA_ACTIVE, m_hWndActive);
        if (oldActive)
            SendMessageA(oldActive, WM_ACTIVATE, WA_INACTIVE, hWnd);
    }
    else
    {
        HWND oldActive = m_hWndActive;
        m_hWndActive = 0;
        SendMessageA(oldActive, WM_ACTIVATE, WA_INACTIVE, 0);
    }
}

static xcb_window_t _GetParent(xcb_connection_t *connection, xcb_window_t hwnd)
{
    xcb_window_t ret = XCB_NONE;
    xcb_query_tree_cookie_t cookie = xcb_query_tree(connection, hwnd);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(connection, cookie, NULL);
    if (!reply)
    {
        return ret;
    }
    if (reply->parent != reply->root)
    {
        ret = reply->parent;
    }
    free(reply);
    return ret;
}

static xcb_window_t _GetRoot(xcb_connection_t *connection, xcb_window_t hwnd)
{
    xcb_window_t ret = hwnd;
    for (;;)
    {
        xcb_window_t hParent = _GetParent(connection, ret);
        if (!hParent)
            break;
        ret = hParent;
    }
    return ret;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags)
{
    switch (gaFlags)
    {
    case GA_PARENT:
        return _GetParent(connection, hwnd);
    case GA_ROOT:
        _GetRoot(connection, hwnd);
    case GA_ROOTOWNER:
    {
        HWND ret = _GetRoot(connection, hwnd);
        HWND hOwner = GetParent(ret);
        if (hOwner)
            ret = hOwner;
        return ret;
    }
    default:
        return 0;
    }
}

cairo_surface_t *SConnection::CreateWindowSurface(HWND hWnd, uint32_t visualId, int cx, int cy)
{
    return cairo_xcb_surface_create(connection, hWnd, xcb_aux_find_visual_by_id(screen, visualId), std::max(cx, 1), std::max(cy, 1));
}

cairo_surface_t * SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId,int cx, int cy)
{
    cairo_xcb_surface_set_size(surface, std::max(cx, 1), std::max(cy, 1));
    return surface;
}

static void _ChangeNetWmState(SConnection *conn, xcb_window_t wnd, bool bSet, xcb_atom_t one, xcb_atom_t two)
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

static void _SendSysCommand(SConnection *conn, xcb_window_t wnd, uint32_t cmd)
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

static void _SendSysRestore(SConnection *conn, xcb_window_t wnd)
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


void SConnection::SendSysCommand(HWND hWnd, int nCmd)
{
    switch(nCmd){
        case SC_MINIMIZE:
            _SendSysCommand(this, hWnd, XCB_ICCCM_WM_STATE_ICONIC);
            break;
        case SC_MAXIMIZE:
            _ChangeNetWmState(this, hWnd, true, atoms._NET_WM_STATE_MAXIMIZED_HORZ, atoms._NET_WM_STATE_MAXIMIZED_VERT);
            break;
        case SC_RESTORE:
            _SendSysRestore(this, hWnd);
            break;
    }
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

void SConnection::UpdateWindowIcon(HWND hWnd, _Window * wndObj)
    {
        if (wndObj)
        {
            std::vector<uint32_t> buf;
            AppendIconData(buf, wndObj->iconSmall);
            AppendIconData(buf, wndObj->iconBig);
            if (!buf.empty())
            {
                xcb_change_property(connection, XCB_PROP_MODE_REPLACE, hWnd, atoms._NET_WM_ICON, XCB_ATOM_CARDINAL, 32, buf.size(), buf.data());
            }
            else
            {
                xcb_delete_property(connection, hWnd, atoms._NET_WM_ICON);
            }
            xcb_flush(connection);
        }
    }
    
    int SConnection::GetScreenWidth(HMONITOR hMonitor) const
    {
        return ((xcb_screen_t *)hMonitor)->width_in_pixels;
    }

    int SConnection::GetScreenHeight(HMONITOR hMonitor) const
    {
        return ((xcb_screen_t *)hMonitor)->height_in_pixels;
    }

    HWND SConnection::GetScreenWindow() const
    {
        return screen->root;
    }

    uint32_t SConnection::GetVisualID(BOOL bScreen) const
    {
        if (bScreen)
        {
            return screen->root_visual;
        }
        else
        {
            return rgba_visual->visual_id;
        }
    }

    void SConnection::SetZOrder(HWND hWnd, _Window * wndObj, HWND hwndInsertAfter)
    {
        if (hwndInsertAfter == HWND_TOPMOST || hwndInsertAfter == HWND_TOP)
            {
                if (hwndInsertAfter == HWND_TOPMOST)
                {
                    uint32_t val[] = { XCB_STACK_MODE_ABOVE };
                    xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_STACK_MODE, val);
                    wndObj->dwExStyle |= WS_EX_TOPMOST;
                }
                else
                {
                    uint32_t val[] = { XCB_STACK_MODE_TOP_IF };
                    xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_STACK_MODE, val);
                    wndObj->dwExStyle &= ~WS_EX_TOPMOST;
                }
            }
            else
            {
                uint32_t val[] = { (uint32_t)hwndInsertAfter, XCB_STACK_MODE_ABOVE };
                xcb_configure_window(connection, hWnd, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, val);
                wndObj->dwExStyle &= ~WS_EX_TOPMOST;
            }
            xcb_flush(wndObj->mConnection->connection);
    }

    void SConnection::SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len)
    {
        xcb_client_message_event_t client_msg_event = {
            XCB_CLIENT_MESSAGE,           //.response_type
            32,                           //.format
            0,                            //.sequence
            (xcb_window_t)hWnd,           //.window
            type //.type
        };
        assert(len<=5);
        memcpy(client_msg_event.data.data32, data, len * sizeof(uint32_t));
        // Send the client message event
        xcb_send_event(connection, 0, hWnd, XCB_EVENT_MASK_NO_EVENT, (const char *)&client_msg_event);
        // Flush the request to the X server
        xcb_flush(connection);
    }


BOOL SConnection::IsWindowVisible(HWND hWnd)
{
    xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(connection, hWnd);
    xcb_get_window_attributes_reply_t *reply = xcb_get_window_attributes_reply(connection, cookie, NULL);
    if (!reply)
        return FALSE;
    uint8_t mapState = reply->map_state;
    free(reply);
    return mapState == XCB_MAP_STATE_VIEWABLE;
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

HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd)
{
    if (uCmd == GW_OWNER)
    {
        if (!wndObj)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        return wndObj->owner;
    }

    xcb_query_tree_cookie_t cookie = xcb_query_tree(connection, hWnd);
    xcb_query_tree_reply_t *reply = xcb_query_tree_reply(connection, cookie, NULL);
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
        hRet = GetWindow(hParent, wndObj,GW_CHILDFIRST);
        break;
    case GW_HWNDLAST:
        hRet = GetWindow(hParent, wndObj, GW_CHILDLAST);
        break;
    case GW_HWNDPREV:
        hRet = GetWndSibling(connection, hParent, hWnd, FALSE);
        break;
    case GW_HWNDNEXT:
        hRet = GetWndSibling(connection, hParent, hWnd, TRUE);
        break;
    }
    free(reply);
    return hRet;
}

UINT SConnection::RegisterMessage(LPCSTR lpString)
{
    return WM_REG_FIRST + SAtoms::internAtom(connection, 0, lpString);;
}

UINT SConnection::RegisterClipboardFormatA(LPCSTR lpString)
{
    return CF_MAX+SAtoms::internAtom(connection, 0, lpString);
}

BOOL SConnection::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData){
    return GetTrayIconMgr()->NotifyIcon(dwMessage, lpData);
}


HMONITOR
SConnection::GetScreen(DWORD dwFlags) const
{
    return (HMONITOR)screen;
}

void SConnection::updateWindow(HWND hWnd, const RECT &rc){
    SendMessageA(hWnd, WM_PAINT, 0, 0);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc){
    flush();
}