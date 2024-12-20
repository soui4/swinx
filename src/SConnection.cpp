#include "SConnection.h"
#include <assert.h>
#include <functional>
#include <xcb/xcb_icccm.h>
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>
#include <xcb/xcb_image.h>
#include <xcb/shape.h>
#include <xcb/xcb_aux.h>
#include <xcb/xfixes.h>
#include <algorithm>
#include <sstream>
#include <string>

#include "uimsg.h"
#include "cursors.hpp"
#include "keyboard.h"
#include "tostring.hpp"
#include "log.h"
#define kLogTag "SConnection"

static SConnMgr *s_connMgr = NULL;

static std::recursive_mutex s_cs;
typedef std::lock_guard<std::recursive_mutex> SAutoLock;

using namespace swinx;


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
    pthread_t tid = pthread_self();
    auto it = m_conns.find(tid);
    if (it == m_conns.end())
    {
        return;
    }
    assert(it->second == pObj);
    delete it->second;
    m_conns.erase(it);
}

SConnection *SConnMgr::getConnection(pthread_t tid_, int screenNum)
{
    pthread_t tid = tid_ != 0 ? tid_ : pthread_self();
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

void SConnection::readXResources()
{
    int offset = 0;
    std::stringstream resources;
    while(1) {
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(connection,
                xcb_get_property_unchecked(connection, false, screen->root,
                                 XCB_ATOM_RESOURCE_MANAGER,
                                 XCB_ATOM_STRING, offset/4, 8192), NULL);
        bool more = false;
        if (reply && reply->format == 8 && reply->type == XCB_ATOM_STRING) {
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
    while (std::getline(resources, line, '\n')) {
        if(line.length() > ARRAYSIZE(kDpiDesc)-1 && strncmp(line.c_str(),kDpiDesc,ARRAYSIZE(kDpiDesc)-1)==0){
            m_forceDpi = atoi(line.c_str()+ARRAYSIZE(kDpiDesc)-1);
        }
    }
}

SConnection::SConnection(int screenNum)
    : m_keyboard(nullptr)
    , m_hook_table(nullptr)
    , m_forceDpi(-1)
{
    connection = xcb_connect(nullptr, &screenNum);
    if (xcb_connection_has_error(connection) > 0)
    {
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
        for (; depth_iter.rem && rgba_visual == 0; xcb_depth_next(&depth_iter)) {
            if (depth_iter.data->depth == 32) {
                xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
                for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
                    xcb_visualtype_t* visual_type = visual_iter.data;
                    if (visual_type->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                        visual_type->red_mask == 0xFF0000 &&
                        visual_type->green_mask == 0x00FF00 &&
                        visual_type->blue_mask == 0x0000FF) {
                        rgba_visual = visual_type;
                        break;
                    }
                }
            }
        }
        if(rgba_visual)
            SLOG_STMI() << "32 bit visual id=" << rgba_visual->visual_id;
    }
    readXResources();
    initializeXFixes();
    if (rgba_visual) {//get composited for screen
        char szAtom[50];
        sprintf(szAtom, "_XSETTINGS_S%d", screenNum);
        xcb_atom_t atom = SAtoms::internAtom(connection, FALSE, szAtom);        
        xcb_get_selection_owner_cookie_t owner_cookie = xcb_get_selection_owner(connection, atom);
        xcb_get_selection_owner_reply_t* owner_reply = xcb_get_selection_owner_reply(connection, owner_cookie, NULL);
        m_bComposited =  owner_reply->owner!=0;
        free(owner_reply);
    }
    m_tid = GetCurrentThreadId();

    atoms.Init(connection,screenNum);

    m_tsDoubleSpan = GetDoubleClickSpan(connection, screen);

    m_bQuit = false;
    m_msgPeek = nullptr;
    m_bMsgNeedFree = false;
    m_hWndCapture = 0;
    m_hWndActive = 0;
    m_hFocus = 0;
    m_hCursor = 0;
    m_bBlockTimer = false;

    m_deskDC = new _SDC(screen->root);
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);

    memset(&m_caretInfo,0, sizeof(m_caretInfo));

    m_rcWorkArea.left = m_rcWorkArea.top = 0;
    m_rcWorkArea.right = screen->width_in_pixels;
    m_rcWorkArea.bottom = screen->height_in_pixels;

    m_evtSync = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    m_trdEvtReader = std::move(std::thread(std::bind(&readProc, this)));

    m_keyboard = new SKeyboard(this);
    m_clipboard = new SClipboard(this);
    m_trayIconMgr = new STrayIconMgr(this);
}


SConnection::~SConnection()
{
    if (!connection)
    {
        return;
    }

    delete m_keyboard;
    delete m_clipboard;
    delete m_trayIconMgr;
    for (auto it : m_sysCursor)
    {
        xcb_free_cursor(connection, it.second);
    }
    m_sysCursor.clear();
    for (auto it : m_stdCursor)
    {
        DestroyIcon((HICON)it.second);
    }
    m_stdCursor.clear();

    delete m_deskDC;
    DeleteObject(m_deskBmp);
    DestroyCaret();

    SLOG_STMI()<<"quit sconnection";
    //m_bQuit = true;

    xcb_window_t hTmp = xcb_generate_id(connection);
    xcb_create_window(connection,
                                     XCB_COPY_FROM_PARENT,            // depth -- same as root
                                     hTmp,                        // window id
                                     screen->root,                   // parent window id
                                     0,0,3,3,
                                     0,                               // border width
                                     XCB_WINDOW_CLASS_INPUT_ONLY,   // window class
                                     screen->root_visual, // visual
                                     0,                               // value mask
                                     0);                             // value list

    // the hWnd is valid window id.
    xcb_client_message_event_t client_msg_event = {
        XCB_CLIENT_MESSAGE,          //.response_type
        32,                          //.format
        0,                           //.sequence
        (xcb_window_t)hTmp,          //.window
        atoms.WM_DISCONN //.type
    };
    xcb_send_event(connection, 0, hTmp, XCB_EVENT_MASK_NO_EVENT, (const char *)&client_msg_event);
    xcb_flush(connection);
    m_trdEvtReader.join();
    SLOG_STMI()<<"event reader quited";

    xcb_destroy_window(connection,hTmp);
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

void SConnection::initializeXFixes()
{
    xcb_generic_error_t* error = 0;
    const xcb_query_extension_reply_t* reply = xcb_get_extension_data(connection, &xcb_xfixes_id);
    if (!reply || !reply->present)
        return;

    xfixes_first_event = reply->first_event;
    xcb_xfixes_query_version_cookie_t xfixes_query_cookie = xcb_xfixes_query_version(connection,
        XCB_XFIXES_MAJOR_VERSION,
        XCB_XFIXES_MINOR_VERSION);
    xcb_xfixes_query_version_reply_t* xfixes_query = xcb_xfixes_query_version_reply(connection,
        xfixes_query_cookie, &error);
    if (!xfixes_query || error || xfixes_query->major_version < 2) {
        SLOG_STMW()<<"SConnection: Failed to initialize XFixes";
        free(error);
        xfixes_first_event = 0;
    }
    free(xfixes_query);
    SLOG_STMI() << "hasXFixes()=" << hasXFixes();
}

bool SConnection::event2Msg(bool bTimeout, UINT elapse, uint64_t ts)
{
    if (m_bQuit)
        return false;
    bool bRet = false;
    if (!bTimeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex4Evt);
        for (auto it : m_evtQueue)
        {
            pushEvent(it);
            free(it);
        }
        bRet = !m_evtQueue.empty();
        m_evtQueue.clear();
    }
    else if (!m_bBlockTimer)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto &it : m_lstTimer)
        {
            if (it.fireRemain <= elapse)
            {
                // fire timer event
                Msg *pMsg = new Msg;
                pMsg->hwnd = it.hWnd;
                pMsg->message = WM_TIMER;
                pMsg->wParam = it.id;
                pMsg->lParam = (LPARAM)it.proc;
                pMsg->time = ts;
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
    uint64_t ts1 = GetTickCount64();
    bool bTimeout = WaitForSingleObject(m_evtSync, timeOut) == WAIT_TIMEOUT;
    uint64_t ts2 = GetTickCount64();
    UINT elapse = ts2 - ts1;
    return event2Msg(bTimeout, elapse, ts2);
}

DWORD SConnection::GetMsgPos() const
{//todo:hjx
    if (m_msgPeek) {
        return MAKELONG(m_msgPeek->pt.x, m_msgPeek->pt.y);
    }
    return 0;
}

LONG SConnection::GetMsgTime() const
{
    if (m_msgPeek) {
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
    DefEvtChecker(int _type) :type(_type) {}

    bool checkEvent(xcb_generic_event_t* e) const {
        return e->response_type == type;
    }
};
xcb_generic_event_t* SConnection::checkEvent(int type)
{
    DefEvtChecker checker(type);
    return checkEvent(&checker);
}

xcb_generic_event_t* SConnection::checkEvent(IEventChecker* checker)
{
    std::unique_lock<std::mutex> lock(m_mutex4Evt);
    for (auto it = m_evtQueue.begin(); it != m_evtQueue.end(); it++) {
        if (checker->checkEvent(*it)) {
            xcb_generic_event_t* ret = *it;
            m_evtQueue.erase(it);
            return ret;
        }
    }
    return nullptr;
}

int SConnection::_waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD to, DWORD dwWaitMask)
{
    UINT timeOut = INFINITE;
    if (!m_bBlockTimer)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        for (auto &it : m_lstTimer)
        {
            timeOut = std::min(timeOut, it.fireRemain);
        }
    }
    if (timeOut != INFINITE && timeOut > to)
        timeOut = to;

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
        return _waitMutliObjectAndMsg(handles, nCount, to, dwWaitMask);
    }
    else
    {
        return ret;
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
            break;
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
    if (vk == VK_LBUTTON || vk == VK_MBUTTON || vk == VK_RBUTTON) {
        return m_mouseKeyState[vk - VK_LBUTTON];
    }
    return m_keyboard->getKeyState((BYTE)vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT uMapType) const
{
    return m_keyboard->mapVirtualKey(uCode, uMapType);
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    if (pMsg->message == WM_KEYDOWN && isprint(pMsg->wParam))
    {
        if (pMsg->wParam >= VK_PRIOR && pMsg->wParam <= VK_HELP)
            return FALSE;
        //handle shortcut key
        SHORT ctrlState = GetKeyState(VK_CONTROL);
        if ((ctrlState & 0x8000) && (
            pMsg->wParam=='A' 
            || pMsg->wParam == 'S' 
            || pMsg->wParam == 'D'
            || pMsg->wParam == 'Z'
            || pMsg->wParam == 'X'
            || pMsg->wParam == 'C'
            || pMsg->wParam == 'V')) {
            return FALSE;
        }
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
        Msg *msg = new Msg;
        msg->message = WM_CHAR;
        msg->hwnd = pMsg->hwnd;
        msg->wParam = pMsg->wParam;
        msg->lParam = pMsg->lParam;

        SHORT shiftState = GetKeyState(VK_SHIFT);
        if (!(shiftState & 0x8000) && pMsg->wParam >= 'A' && pMsg->wParam <= 'Z')
        {
            msg->wParam += 0x20; // tolower
        }
        m_msgQueue.push_back(msg);
        return TRUE;
    }
    return FALSE;
}

BOOL SConnection::peekMsg(THIS_ LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
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
            proc(msg->hwnd, WM_TIMER, msg->wParam, msg->time);
            m_msgQueue.erase(it);
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
            m_bMsgNeedFree = true;
        }
        memcpy(pMsg, (MSG *)m_msgPeek, sizeof(MSG));

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
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    Msg *pMsg = new Msg(new MsgReply);
    pMsg->hwnd = hWnd;
    pMsg->message = message;
    pMsg->wParam = wp;
    pMsg->lParam = lp;
    m_msgQueue.push_back(pMsg);
}

void SConnection::postMsg2(BOOL bWideChar,HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    if(!bWideChar){
        Msg *pMsg = new Msg(reply);
        pMsg->hwnd = hWnd;
        pMsg->message = message;
        pMsg->wParam = wp;
        pMsg->lParam = lp;
        m_msgQueue.push_back(pMsg);
    }else{
        MsgW2A *pMsg = new MsgW2A(reply);
        pMsg->orgMsg.message = message;
        pMsg->orgMsg.wParam = wp;
        pMsg->orgMsg.lParam = lp;
        pMsg->hwnd = hWnd;
        m_msgQueue.push_back(pMsg);
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
    if (hBitmap == (HBITMAP)1) //windows api support hBitmap set to 1 to indicator a gray caret, ignore it.
        hBitmap = nullptr;
    m_caretInfo.hBmp = RefGdiObj(hBitmap);
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

BOOL SConnection::DestroyCaret()
{
    if (m_caretInfo.nVisible>0) {
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

BOOL SConnection::ShowCaret(HWND hWnd) {
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible++;
    if (m_caretInfo.nVisible == 1) {
        SetTimer(hWnd, TM_CARET, m_caretBlinkTime, NULL);
    }
    return TRUE;
}

BOOL SConnection::HideCaret(HWND hWnd) {
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible--;

    if (m_caretInfo.nVisible ==0) {
        KillTimer(hWnd, TM_CARET);
    }
    return TRUE;
}

BOOL SConnection::SetCaretPos(int X, int Y) {
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    return TRUE;
}

BOOL SConnection::GetCaretPos(LPPOINT lpPoint) {
    if (!lpPoint)
        return FALSE;
    lpPoint->x = m_caretInfo.x;
    lpPoint->y = m_caretInfo.y;
    return TRUE;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime) {
    m_caretBlinkTime = blinkTime;
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

class PropertyNotifyEvent : public IEventChecker{
public:
    PropertyNotifyEvent(xcb_window_t win, xcb_atom_t property)
        : window(win), type(XCB_PROPERTY_NOTIFY), atom(property) {}
    xcb_window_t window;
    int type;
    xcb_atom_t atom;
    bool checkEvent(xcb_generic_event_t* event) const {
        if (!event)
            return false;
        if ((event->response_type & ~0x80) != type) {
            return false;
        }
        else {
            xcb_property_notify_event_t* pn = (xcb_property_notify_event_t*)event;
            if ((pn->window == window) && (pn->atom == atom))
                return true;
        }
        return false;
    }
};

xcb_timestamp_t SConnection::getSectionTs() {
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
        timer.hWnd = hWnd;
        timer.proc = proc;
        timer.elapse = uElapse;
        m_lstTimer.push_back(timer);
        return newId;
    }
}

BOOL SConnection::KillTimer(HWND hWnd, UINT_PTR id)
{

    std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); it++)
    {
        if (it->hWnd == hWnd && it->id == id)
        {
            m_lstTimer.erase(it);
            return TRUE;
        }
    }
    return FALSE;
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
    m_hWndCapture = hCapture;
    xcb_grab_pointer(connection,
                     1, // 这个标志位表示不使用对子窗口的事件
                     hCapture, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION,
                     XCB_GRAB_MODE_ASYNC, // 异步捕获
                     XCB_GRAB_MODE_ASYNC,
                     XCB_NONE,        // 捕获事件的窗口
                     XCB_NONE,        // 使用默认光标
                     XCB_CURRENT_TIME // 立即开始捕获
    );
    xcb_flush(connection);
    return ret;
}

BOOL SConnection::ReleaseCapture()
{
    if (!m_hWndCapture)
        return FALSE;
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
    ICONINFO info = { 0 };
    GetIconInfo(cursor, &info);
    assert(info.fIcon == 0);
    assert(info.hbmColor);
    BITMAP bm;
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

HCURSOR SConnection::GetCursor() {
    return m_hCursor;
}

HCURSOR SConnection::SetCursor(HCURSOR cursor)
{
    if (!cursor)
        cursor = LoadCursor(IDC_ARROW);
    assert(cursor);
    if (cursor == m_hCursor)
        return cursor;
    xcb_cursor_t xcbCursor = 0;
    if (m_sysCursor.find(cursor) != m_sysCursor.end())
    {
        xcbCursor = m_sysCursor[cursor];
    }
    else
    {
        xcbCursor = createXcbCursor(cursor);
        if (!xcbCursor)
        {
            // todo: print error
            return 0;
        }
    }
    HCURSOR ret = m_hCursor;
    uint32_t val[] = { xcbCursor };
    xcb_change_window_attributes(connection, m_hWndActive ? m_hWndActive : screen->root, XCB_CW_CURSOR, val);
    m_hCursor = cursor;
    return ret;
}

enum
{
    CIDC_NODROP = 1,
    CIDC_MOVE   = 2,
    CIDC_COPY   = 3,
    CIDC_LINK   = 4,

    CIDC_ARROW = 32512,
    CIDC_IBEAM = 32513,
    CIDC_WAIT = 32514,
    CIDC_CROSS = 32515,
    CIDC_UPARROW = 32516,
    CIDC_SIZE = 32640,
    CIDC_ICON = 32641,
    CIDC_SIZENWSE = 32642,
    CIDC_SIZENESW = 32643,
    CIDC_SIZEWE = 32644,
    CIDC_SIZENS = 32645,
    CIDC_SIZEALL = 32646,
    CIDC_NO = 32648,
    CIDC_HAND = 32649,
    CIDC_APPSTARTING = 32650,
    CIDC_HELP = 32651,
};

struct CData
{
    const unsigned char *buf;
    UINT length;
};

static bool getStdCursorCData(WORD wId, CData &data)
{
    bool ret = true;
    switch (wId)
    {
    case CIDC_MOVE:
        data.buf = drag_move_cur;
        data.length = sizeof(drag_move_cur);
        break;
    case CIDC_COPY:
        data.buf = drag_copy_cur;
        data.length = sizeof(drag_copy_cur);
        break;
    case CIDC_LINK:
        data.buf = drag_link_cur;
        data.length = sizeof(drag_link_cur);
        break;
    case CIDC_NODROP:
        data.buf = drag_nodrop_cur;
        data.length = sizeof(drag_nodrop_cur);
        break;
    case CIDC_ARROW:
        data.buf = ocr_normal_cur;
        data.length = sizeof(ocr_normal_cur);
        break;
        ;
    case CIDC_IBEAM:
        data.buf = ocr_ibeam_cur;
        data.length = sizeof(ocr_ibeam_cur);
        break;
        ;
    case CIDC_WAIT:
        data.buf = ocr_wait_cur;
        data.length = sizeof(ocr_wait_cur);
        break;
    case CIDC_CROSS:
        data.buf = ocr_cross_cur;
        data.length = sizeof(ocr_cross_cur);
        break;
    case CIDC_UPARROW:
        data.buf = ocr_up_cur;
        data.length = sizeof(ocr_up_cur);
        break;
    case CIDC_SIZE:
        data.buf = ocr_size_cur;
        data.length = sizeof(ocr_size_cur);
        break;
    case CIDC_SIZEALL:
        data.buf = ocr_sizeall_cur;
        data.length = sizeof(ocr_sizeall_cur);
        break;
    case CIDC_ICON:
        data.buf = ocr_icon_cur;
        data.length = sizeof(ocr_icon_cur);
        break;
    case CIDC_SIZENWSE:
        data.buf = ocr_sizenwse_cur;
        data.length = sizeof(ocr_sizenwse_cur);
        break;
    case CIDC_SIZENESW:
        data.buf = ocr_sizenesw_cur;
        data.length = sizeof(ocr_sizenesw_cur);
        break;
    case CIDC_SIZEWE:
        data.buf = ocr_sizewe_cur;
        data.length = sizeof(ocr_sizewe_cur);
        break;
    case CIDC_SIZENS:
        data.buf = ocr_sizens_cur;
        data.length = sizeof(ocr_sizens_cur);
        break;
    case CIDC_HAND:
        data.buf = ocr_hand_cur;
        data.length = sizeof(ocr_hand_cur);
        break;
    case CIDC_HELP:
        data.buf = ocr_help_cur;
        data.length = sizeof(ocr_help_cur);
        break;
    case CIDC_APPSTARTING:
        data.buf = ocr_appstarting_cur;
        data.length = sizeof(ocr_appstarting_cur);
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}

HCURSOR SConnection::LoadCursor(LPCSTR lpCursorName)
{
    HCURSOR ret = 0;
    if (IS_INTRESOURCE(lpCursorName))
    {
        WORD wId = (WORD)(ULONG_PTR)lpCursorName;
        if (m_stdCursor.find(wId) != m_stdCursor.end())
            return m_stdCursor[wId];
        CData data;
        if (!getStdCursorCData(wId, data))
        {
            getStdCursorCData(CIDC_ARROW, data);
        }
        ret = (HCURSOR)LoadImageBuf((PBYTE)data.buf, data.length, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
        assert(ret);
        m_stdCursor.insert(std::make_pair(wId, ret));
    }
    else
    {
        ret = (HCURSOR)LoadImage(0, lpCursorName, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
    }
    return ret;
}

BOOL SConnection::DestroyCursor(HCURSOR cursor)
{
    // look for sys cursor
    auto it = m_sysCursor.find(cursor);
    if (it == m_sysCursor.end())
        return FALSE;
    for (auto it : m_stdCursor)
    {
        if (cursor == it.second)
            return TRUE;
    }
    m_sysCursor.erase(cursor);
    DestroyIcon((HICON)cursor);
    return TRUE;
}

static uint32_t TsSpan(uint32_t t1, uint32_t t2)
{
    if (t1 > t2)
    {
        return t1 - t2;
    }
    else
    {
        return t1 + (0xffffffff - t2);
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
        wp |= MK_RBUTTON;
    if (state & XCB_BUTTON_MASK_3)
        wp |= MK_RBUTTON;
    return wp;
}

HWND SConnection::GetActiveWnd() const
{
    if (m_hWndActive)
        return m_hWndActive;
    xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(connection);
    xcb_get_input_focus_reply_t *reply = xcb_get_input_focus_reply(connection, cookie, nullptr);
    if (reply)
    {
        HWND ret = reply->focus;
        free(reply);
        return ret;
    }
    return screen->root;
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    if (hWnd == 0)
        return false;
    HWND ret = m_hWndActive;
    SetFocus(hWnd);
    m_hWndActive = hWnd;
    return TRUE;
}

HWND SConnection::GetParentWnd(HWND hWnd) const
{
    return GetWindow(hWnd, GW_PARENT);
}

HWND SConnection::GetWindow(HWND hWnd, int code) const
{
    xcb_query_tree_cookie_t tree_cookie = xcb_query_tree(connection, hWnd);
    xcb_query_tree_reply_t *tree_reply = xcb_query_tree_reply(connection, tree_cookie, NULL);
    if (!tree_reply)
        return 0;
    HWND ret = 0;
    switch (code)
    {
    case GW_CHILDFIRST:
        if (tree_reply->children_len > 0)
        {
            xcb_window_t *children = xcb_query_tree_children(tree_reply);
            ret = children[0];
        }
        break;
    case GW_CHILDLAST:
        if (tree_reply->children_len > 0)
        {
            xcb_window_t *children = xcb_query_tree_children(tree_reply);
            ret = children[tree_reply->children_len - 1];
        }
        break;
    case GW_HWNDFIRST:
        if (tree_reply->parent)
        {
            ret = GetWindow(tree_reply->parent, GW_CHILDFIRST);
        }
        break;
    case GW_HWNDLAST:
        if (tree_reply->parent)
        {
            ret = GetWindow(tree_reply->parent, GW_CHILDLAST);
        }
        break;
    case GW_PARENT:
        ret = tree_reply->parent;
        break;
    }
    free(tree_reply);
    return ret;
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
    if (!screen) {
        SLOG_STME() << "screen is null! swinx will crash!!!";
    }
    if(m_forceDpi != -1){
        return m_forceDpi;
    }
    if (bx)
    {
        return screen->width_in_pixels * 25.4 / screen->width_in_millimeters;
    }
    else
    {
        return screen->height_in_pixels * 25.4 / screen->height_in_millimeters;
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
    // todo:hjx
    return SetActiveWindow(hWnd);
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

HWND SConnection::SetFocus(HWND hWnd)
{
    //TRACE("setfocus hwnd=%d, curfocus=%d\n",(int)hWnd,(int)m_hFocus);
    if(hWnd == m_hFocus){
        return hWnd;
    }
    HWND ret = m_hFocus;
    xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, hWnd, XCB_CURRENT_TIME);
    xcb_flush(connection);
    return ret;
}

uint32_t SConnection::netWmStates(HWND hWnd)
{
    uint32_t result(0);

    xcb_get_property_cookie_t get_cookie = xcb_get_property_unchecked(connection, 0, hWnd, atoms._NET_WM_STATE, XCB_ATOM_ATOM, 0, 1024);

    xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, get_cookie, NULL);

    if (reply && reply->format == 32 && reply->type == XCB_ATOM_ATOM)
    {
        const xcb_atom_t *states = static_cast<const xcb_atom_t *>(xcb_get_property_value(reply));
        const xcb_atom_t *statesEnd = states + reply->length;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_ABOVE))
            result |= NetWmStateAbove;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_BELOW))
            result |= NetWmStateBelow;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_FULLSCREEN))
            result |= NetWmStateFullScreen;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_MAXIMIZED_HORZ))
            result |= NetWmStateMaximizedHorz;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_MAXIMIZED_VERT))
            result |= NetWmStateMaximizedVert;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_STAYS_ON_TOP))
            result |= NetWmStateStaysOnTop;
        if (statesEnd != std::find(states, statesEnd, atoms._NET_WM_STATE_DEMANDS_ATTENTION))
            result |= NetWmStateDemandsAttention;
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

bool SConnection::pushEvent(xcb_generic_event_t *event)
{
    uint8_t event_code = event->response_type & 0x7f;
    Msg *pMsg = nullptr;
    bool ret = false;
    switch (event_code)
    {
    case XCB_SELECTION_REQUEST:
    {
        xcb_selection_request_event_t* e2 = (xcb_selection_request_event_t*)event;
        m_tsSelection = e2->time;
        m_clipboard->handleSelectionRequest(e2);
        return false;
    }
    case XCB_SELECTION_CLEAR:
    {
        xcb_selection_clear_event_t* e2 = (xcb_selection_clear_event_t*)event;
        m_tsSelection = e2->time;
        m_clipboard->handleSelectionClearRequest(e2);
        return false;
    }
    case XCB_SELECTION_NOTIFY:
    {
        xcb_selection_notify_event_t* e2 = (xcb_selection_notify_event_t*)event;
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
        pMsg->hwnd = e2->child == XCB_NONE ? e2->event : e2->child;
        UINT vk = m_keyboard->onKeyEvent(true, e2->detail, e2->state, e2->time);
        pMsg->message = vk < VK_NUMLOCK ? WM_KEYDOWN : WM_SYSKEYDOWN;
        pMsg->wParam = vk;
        BYTE scanCode = (BYTE)e2->detail;
        pMsg->lParam = scanCode << 16 | m_keyboard->getRepeatCount();
        //SLOG_FMTI("onkeydown, detail=%d,vk=%d, repeat=%d", e2->detail, vk, (int)m_keyboard->getRepeatCount());
        break;
    }
    case XCB_KEY_RELEASE:
    {
        xcb_key_release_event_t *e2 = (xcb_key_release_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->child == XCB_NONE ? e2->event : e2->child;
        UINT vk = m_keyboard->onKeyEvent(false, e2->detail, e2->state, e2->time);
        pMsg->message = vk < VK_NUMLOCK ? WM_KEYUP : WM_SYSKEYUP;
        pMsg->wParam = vk;
        BYTE scanCode = (BYTE)e2->detail;
        pMsg->lParam = scanCode << 16;
        //SLOG_FMTI("onkeyup, detail=%d,vk=%d", e2->detail, vk);
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
        pMsg = pMsgPaint;
        break;
    }
    case XCB_PROPERTY_NOTIFY:
    {
        xcb_property_notify_event_t *e2 = (xcb_property_notify_event_t *)event;
        if (e2->atom == atoms._NET_WORKAREA) {
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
        pMsg = new Msg;
        pMsg->hwnd = e2->window;
        pMsg->message = WM_MOVE;
        pMsg->wParam = 0;
        pMsg->lParam = MAKELPARAM(pos.x, pos.y);
        m_msgQueue.push_back(pMsg);
        pMsg = new Msg;
        pMsg->hwnd = e2->window;
        pMsg->message = WM_SIZE;
        pMsg->wParam = 0;
        pMsg->lParam = MAKELPARAM(e2->width, e2->height);
        m_msgQueue.push_back(pMsg);
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
        else if (e2->data.data32[0] == atoms.WM_DELETE_WINDOW)
        {
            pMsg = new Msg;
            pMsg->message = WM_CLOSE;
            pMsg->hwnd = e2->window;
            pMsg->wParam = pMsg->lParam = 0;
        }
        else if (e2->type == atoms._NET_WM_STATE_HIDDEN)
        {
            pMsg = new Msg;
            pMsg->message = WM_SHOWWINDOW;
            pMsg->hwnd = e2->window;
            pMsg->wParam = e2->data.data32[0];
            pMsg->lParam = 0;
        }
        else if (e2->type == atoms._NET_WM_STATE_DEMANDS_ATTENTION)
        {
            pMsg = new Msg;
            pMsg->message = WM_ENABLE;
            pMsg->hwnd = e2->window;
            pMsg->wParam = e2->data.data32[0];
            pMsg->lParam = 0;
        }
        break;
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
            pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);
            switch (e2->detail)
            {
            case XCB_BUTTON_INDEX_1: // left button
                pMsg->message = TsSpan(e2->time, m_tsPrevPress) > m_tsDoubleSpan ? WM_LBUTTONDOWN : WM_LBUTTONDBLCLK;
                break;
            case XCB_BUTTON_INDEX_2:
                pMsg->message = TsSpan(e2->time, m_tsPrevPress) > m_tsDoubleSpan ? WM_MBUTTONDOWN : WM_MBUTTONDBLCLK;
                break;
            case XCB_BUTTON_INDEX_3:
                pMsg->message = TsSpan(e2->time, m_tsPrevPress) > m_tsDoubleSpan ? WM_RBUTTONDOWN : WM_RBUTTONDBLCLK;
                break;
            }
            if (e2->detail <= XCB_BUTTON_INDEX_3)
                m_mouseKeyState[e2->detail - XCB_BUTTON_INDEX_1] = 0x8000;
            pMsg->wParam = ButtonState2Mask(e2->state);
            m_tsPrevPress = e2->time;
        }
        else if (e2->detail == XCB_BUTTON_INDEX_4 || e2->detail == XCB_BUTTON_INDEX_5) {
            //mouse wheel event
            SLOG_STMI() << "mouse wheel, dir = " << (e2->detail == XCB_BUTTON_INDEX_4 ? "up" : "down");
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
            pMsg->wParam = MAKEWPARAM(vkFlag,delta);
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
            pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);
            switch (e2->detail)
            {
            case XCB_BUTTON_INDEX_1: // left button
                pMsg->message = WM_LBUTTONUP;
                break;
            case XCB_BUTTON_INDEX_2:
                pMsg->message = WM_MBUTTONUP;
                break;
            case XCB_BUTTON_INDEX_3:
                pMsg->message = WM_RBUTTONUP;
                break;
            }
            if(e2->detail <= XCB_BUTTON_INDEX_3)
                m_mouseKeyState[e2->detail - XCB_BUTTON_INDEX_1] = 0;
            pMsg->wParam = ButtonState2Mask(e2->state);
        }
        break;
    }
    case XCB_MOTION_NOTIFY:
    {
        xcb_motion_notify_event_t *e2 = (xcb_motion_notify_event_t *)event;
        pMsg = new Msg;
        pMsg->hwnd = e2->event;
        pMsg->message = WM_MOUSEMOVE;
        pMsg->pt.x = e2->event_x;
        pMsg->pt.y = e2->event_y;
        pMsg->lParam = MAKELPARAM(pMsg->pt.x, pMsg->pt.y);
        pMsg->wParam = ButtonState2Mask(e2->state);
        pMsg->time = e2->time;
        break;
    }
    case XCB_FOCUS_IN:
	{
		xcb_focus_in_event_t* e2 = (xcb_focus_in_event_t*)event;
		pMsg = new Msg;
		pMsg->hwnd = e2->event;
		pMsg->message = WM_SETFOCUS;
		if (m_hFocus != pMsg->hwnd)
		{
			pMsg->wParam = (WPARAM)m_hFocus;
			m_hFocus = e2->event;
		}
		pMsg->lParam = 0;
		break;
	}
    case XCB_FOCUS_OUT:
    {
		xcb_focus_out_event_t* e2 = (xcb_focus_out_event_t*)event;
		pMsg = new Msg;
		pMsg->hwnd = e2->event;
		pMsg->message = WM_KILLFOCUS;
		pMsg->wParam = (WPARAM)m_hFocus;
		pMsg->lParam = 0;
		if (e2->event == m_hFocus)
		{
			m_hFocus = 0;
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
        xcb_mapping_notify_event_t* e2 = (xcb_mapping_notify_event_t*)event;
        m_keyboard->onMappingNotifyEvent(e2);
        break;
    }
    default:
        break;
    }
    if (pMsg)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex4Msg);
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
        if((event->response_type& 0x7f)==XCB_CLIENT_MESSAGE && ((xcb_client_message_event_t *)event)->type == atoms.WM_DISCONN){
            m_bQuit = true;
            SetEvent(m_evtSync);
            SLOG_STMI()<<"recv WM_DISCONN, quit event reading thread";
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

    SLOG_STMI()<<"event reader done";
}

void SConnection::updateWorkArea()
{
    memset(&m_rcWorkArea, 0, sizeof(RECT));
    xcb_get_property_reply_t* workArea =
        xcb_get_property_reply(connection,
            xcb_get_property_unchecked(connection, false, screen->root,
                atoms._NET_WORKAREA,
                XCB_ATOM_CARDINAL, 0, 1024), NULL);
    if (workArea && workArea->type == XCB_ATOM_CARDINAL && workArea->format == 32 && workArea->value_len >= 4) {
        // If workArea->value_len > 4, the remaining ones seem to be for WM's virtual desktops
        // (don't mess with QXcbVirtualDesktop which represents an X screen).
        // But QScreen doesn't know about that concept.  In reality there could be a
        // "docked" panel (with _NET_WM_STRUT_PARTIAL atom set) on just one desktop.
        // But for now just assume the first 4 values give us the geometry of the
        // "work area", AKA "available geometry"
        uint32_t* geom = (uint32_t*)xcb_get_property_value(workArea);
        m_rcWorkArea.left = geom[0];
        m_rcWorkArea.top = geom[1];
        m_rcWorkArea.right = m_rcWorkArea.left + geom[2];
        m_rcWorkArea.bottom = m_rcWorkArea.top + geom[3];
    }
    free(workArea);
    SLOG_STMI() << "updateWorkArea, rc="<<m_rcWorkArea.left<<","<<m_rcWorkArea.top<<","<<m_rcWorkArea.right<<","<<m_rcWorkArea.bottom;
}

void SConnection::GetWorkArea(RECT* prc)
{
    memcpy(prc, &m_rcWorkArea, sizeof(RECT));
}


//--------------------------------------------------------------------------
// clipboard api

BOOL
SConnection::EmptyClipboard() {
    return m_clipboard->emptyClipboard();
}

BOOL
SConnection::IsClipboardFormatAvailable(_In_ UINT format) {
    if(!GetClipboardOwner())
        return FALSE;
    return m_clipboard->hasFormat(format);
}

BOOL SConnection::OpenClipboard(HWND hWndNewOwner) {
    return m_clipboard->openClipboard(hWndNewOwner);
}

BOOL
SConnection::CloseClipboard() {
    return m_clipboard->closeClipboard();
}

HWND
SConnection::GetClipboardOwner() {
    return m_clipboard->getClipboardOwner();
}

HANDLE
SConnection::GetClipboardData(UINT uFormat) {
    HANDLE ret = m_clipboard->getClipboardData(uFormat);
    if (uFormat == CF_UNICODETEXT) {
        const char* src = (const char*)GlobalLock(ret);
        size_t len = GlobalSize(ret);
        std::wstring wstr;
        towstring(src, len, wstr);
        GlobalUnlock(ret);
        ret = GlobalReAlloc(ret, wstr.length() * sizeof(wchar_t), 0);
        void* buf = GlobalLock(ret);
        memcpy(buf, wstr.c_str(), wstr.length() * sizeof(wchar_t));
        GlobalUnlock(ret);
    }
    return ret;
}

HANDLE SConnection::SetClipboardData( UINT uFormat, HANDLE hMem) {
    return m_clipboard->setClipboardData(uFormat, hMem);
}

UINT SConnection::RegisterClipboardFormatA(LPCSTR pszName)
{
    return CF_MAX +  SAtoms::internAtom(connection, 0, pszName);
}
