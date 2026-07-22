#include "SConnection.h"
#include "wndobj.h"
#include "sdc.h"
#include "platform_api.h"
#include <gdi.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <log.h>
#define kLogTag "SConnection"

static UINT s_nextRegisteredMessage = WM_USER + 100000;
static std::recursive_mutex s_registeredMessageMutex;
static std::map<std::string, UINT> s_registeredMessages;

static bool isSameTimerMsg(const Msg *msg, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    return msg && msg->message == WM_TIMER && msg->hwnd == hWnd && msg->wParam == id && msg->lParam == (LPARAM)proc;
}

static bool hasQueuedTimerMsg(const std::list<Msg *> &queue, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    for (auto msg : queue)
    {
        if (isSameTimerMsg(msg, hWnd, id, proc))
            return true;
    }
    return false;
}

static bool hasQueuedPaintMsg(const std::list<Msg *> &queue, HWND hWnd)
{
    for (auto msg : queue)
    {
        if (msg && msg->message == WM_PAINT && msg->hwnd == hWnd)
            return true;
    }
    return false;
}

static unsigned int mouseButtonBitFromMsg(UINT message)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        return MK_LBUTTON;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        return MK_RBUTTON;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        return MK_MBUTTON;
    default:
        return 0;
    }
}

static bool isMouseButtonDownMsg(UINT message)
{
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK;
}

static bool isMouseButtonUpMsg(UINT message)
{
    return message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP;
}

static UINT mouseButtonUpMsgFromButtons(unsigned int buttons)
{
    if (buttons & MK_LBUTTON)
        return WM_LBUTTONUP;
    if (buttons & MK_RBUTTON)
        return WM_RBUTTONUP;
    if (buttons & MK_MBUTTON)
        return WM_MBUTTONUP;
    return 0;
}

static bool shouldSendToCapture(UINT message)
{
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL;
}

static bool isKeyDownMsg(UINT message)
{
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

static bool isKeyUpMsg(UINT message)
{
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

static void updateKeyboardStateForMsg(BYTE *keyboardState, UINT msg, WPARAM wp)
{
    if (!keyboardState || ((!isKeyDownMsg(msg) && !isKeyUpMsg(msg)) || wp >= 256))
        return;

    BYTE downFlag = isKeyDownMsg(msg) ? 0x80 : 0x00;
    keyboardState[wp] = (keyboardState[wp] & 0x01) | downFlag;
    if (wp == VK_SHIFT)
    {
        keyboardState[VK_LSHIFT] = (keyboardState[VK_LSHIFT] & 0x01) | downFlag;
        keyboardState[VK_RSHIFT] = (keyboardState[VK_RSHIFT] & 0x01) | downFlag;
    }
    else if (wp == VK_CONTROL)
    {
        keyboardState[VK_LCONTROL] = (keyboardState[VK_LCONTROL] & 0x01) | downFlag;
        keyboardState[VK_RCONTROL] = (keyboardState[VK_RCONTROL] & 0x01) | downFlag;
    }
    else if (wp == VK_MENU)
    {
        keyboardState[VK_LMENU] = (keyboardState[VK_LMENU] & 0x01) | downFlag;
        keyboardState[VK_RMENU] = (keyboardState[VK_RMENU] & 0x01) | downFlag;
    }
    else if (wp == VK_CAPITAL && isKeyDownMsg(msg))
    {
        keyboardState[VK_CAPITAL] |= 0x80;
    }
}

_Window *SConnection::CreateVirtualWindowObject()
{
    _Window *pWnd = new _Window(0);
    pWnd->tid = m_tid;
    pWnd->mConnection = this;
    pWnd->state = WS_Normal;
    pWnd->dwStyle = 0;
    pWnd->dwExStyle = 0;
    pWnd->hInstance = 0;
    pWnd->clsAtom = 0;
    pWnd->bAutoDblClick = FALSE;
    pWnd->iconSmall = pWnd->iconBig = nullptr;
    pWnd->parent = NULL;
    pWnd->winproc = DefWindowProc;
    pWnd->rc = {0, 0, 0, 0};
    pWnd->showSbFlags = 0;
    pWnd->visualId = GetVisualID(TRUE);
    return pWnd;
}


BOOL
SConnection::RegisterVirtualHWND(UINT_PTR externalId, HWND hParent, DWORD dwStyle, DWORD dwExStyle,
                                 const RECT *prc, int ctrlId)
{
    HWND hWnd = reinterpret_cast<HWND>(externalId);
    if (WndMgr::fromHwnd(hWnd))
    {
        SLOG_STME()<<"HWND already registered"<<externalId;
        return FALSE;
    }    
    SConnection *pConn = SConnMgr::instance()->getConnection();
    _Window *pWnd = pConn->CreateVirtualWindowObject();
    pWnd->parent = hParent;
    pWnd->dwStyle = dwStyle;
    pWnd->dwExStyle = dwExStyle;
    pWnd->rc = *prc;
    BOOL ok = WndMgr::insertWindow(hWnd, pWnd);
    assert(ok);
    int cx =  prc->right-prc->left;
    int cy = prc->bottom-prc->top;
    SetWindowLongPtrA(hWnd, GWLP_ID, ctrlId);
    cairo_surface_t *surface = pConn->CreateWindowSurface(hWnd, 0, cx, cy);
    pWnd->bmp = InitGdiObj(OBJ_BITMAP, surface);
    pWnd->hdc = new _SDC(hWnd);
    SelectObject(pWnd->hdc, pWnd->bmp);
    SLOG_STMI()<<"Registered virtual HWND:"<<externalId;
    return TRUE;
}

BOOL SConnection::UnregisterVirtualHWND(UINT_PTR externalId)
{
    HWND hWnd = reinterpret_cast<HWND>(externalId);
    WndObj wnd = WndMgr::fromHwnd(hWnd);
    if (!wnd)
    {
        SLOG_STMW()<<"Unregistered virtual HWND:"<<externalId<<" not found";
        return FALSE;
    }
    WndMgr::freeWindow(hWnd);
    SLOG_STMI()<<"Unregistered virtual HWND:"<<externalId;
    return TRUE;
}

//=============================================================================
// SConnection Implementation
//=============================================================================

SConnection::SConnection(int)
    : m_msgPeek(nullptr)
    , m_bMsgNeedFree(false)
    , m_tsLastMsg(0)
    , m_bQuit(false)
    , m_tid(GetCurrentThreadId())
    , m_deskDC(new _SDC(0))
    , m_deskBmp(nullptr)
    , m_screenNum(0)
    , m_hWndCapture(0)
    , m_hFocus(0)
    , m_hActive(0)
    , m_hForeground(0)
    , m_mouseButtons(0)
    , m_cursorCount(1)
    , m_caretBlinkTime(TS_CARET)
    , m_clipboard(new SClipboard())
{
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    memset(m_keyboardState, 0, sizeof(m_keyboardState));
    SLOG_FMTI("SConnection created for thread %d", m_tid);
}

SConnection::~SConnection()
{
    std::unique_lock<CountMutex> lock(m_mutex);
    delete m_clipboard;
    for (auto msg : m_msgStack)
        delete msg;
    for (auto msg : m_msgQueue)
        delete msg;
    for (auto task : m_lstCallbackTask)
        task->Release();
    if (m_msgPeek && m_bMsgNeedFree)
        delete m_msgPeek;
    DeleteDC(m_deskDC);
    DeleteObject(m_deskBmp);
    SLOG_FMTI("SConnection destroyed");
}

SHORT SConnection::GetKeyState(int vk)
{
    unsigned int buttons = m_mouseButtons.load();
    if ((vk == VK_LBUTTON && (buttons & MK_LBUTTON)) || (vk == VK_RBUTTON && (buttons & MK_RBUTTON)) || (vk == VK_MBUTTON && (buttons & MK_MBUTTON)))
        return (SHORT)0x8000;
    if (vk >= 0 && vk < 256)
    {
        SHORT state = 0;
        if (m_keyboardState[vk] & 0x80)
            state |= (SHORT)0x8000;
        if (m_keyboardState[vk] & 0x01)
            state |= 0x0001;
        return state;
    }
    return 0;
}

BOOL SConnection::GetKeyboardState(PBYTE lpKeyState)
{
    if (!lpKeyState)
        return FALSE;
    memset(lpKeyState, 0, 256);
    unsigned int buttons = m_mouseButtons.load();
    if (buttons & MK_LBUTTON)
        lpKeyState[VK_LBUTTON] = 0x80;
    if (buttons & MK_RBUTTON)
        lpKeyState[VK_RBUTTON] = 0x80;
    if (buttons & MK_MBUTTON)
        lpKeyState[VK_MBUTTON] = 0x80;
    for (int i = 0; i < 256; ++i)
        lpKeyState[i] |= m_keyboardState[i];
    return TRUE;
}

SHORT SConnection::GetAsyncKeyState(int vk)
{
    return GetKeyState(vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT uMapType) const
{
    switch (uMapType)
    {
    case MAPVK_VK_TO_VSC:
    case MAPVK_VK_TO_VSC_EX:
        return uCode;
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        return uCode;
    case MAPVK_VK_TO_CHAR:
        if (uCode >= 'A' && uCode <= 'Z')
            return uCode;
        if (uCode >= '0' && uCode <= '9')
            return uCode;
        return 0;
    default:
        return 0;
    }
}

UINT SConnection::GetDoubleClickTime() const
{
    return 400;
}

LONG SConnection::GetMsgTime() const
{
    return (LONG)m_tsLastMsg;
}

DWORD SConnection::GetMsgPos() const
{
    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    return MAKELONG(pt.x, pt.y);
}

DWORD SConnection::GetQueueStatus(UINT flags)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    if (flags == QS_ALLINPUT)
        return MAKELONG(m_msgQueue.size(), 0);
    for (auto msg : m_msgQueue)
    {
        if ((flags & QS_PAINT) && msg->message == WM_PAINT)
            return MAKELONG(0, msg->message);
        if ((flags & QS_TIMER) && msg->message == WM_TIMER)
            return MAKELONG(0, msg->message);
        if ((flags & QS_KEY) && (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP))
            return MAKELONG(0, msg->message);
        if ((flags & QS_MOUSEMOVE) && msg->message == WM_MOUSEMOVE)
            return MAKELONG(0, msg->message);
        if ((flags & QS_MOUSEBUTTON) && msg->message >= WM_LBUTTONDOWN && msg->message <= WM_XBUTTONDBLCLK)
            return MAKELONG(0, msg->message);
        if (flags & QS_ALLPOSTMESSAGE)
            return MAKELONG(0, msg->message);
    }
    return 0;
}

void SConnection::updateMsgQueue(DWORD dwTimeout)
{
    return;
}

bool SConnection::waitMsg(UINT timeOut)
{
    return false;
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL, DWORD)
{
    return WAIT_TIMEOUT;
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    return FALSE;
}

BOOL SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    return FALSE;
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    if(m_msgQueue.empty())
        return FALSE;
    Msg *pMsg = m_msgQueue.front();
    m_msgQueue.pop_front();
    memcpy(lpMsg,(MSG*)pMsg,sizeof(MSG));
    delete pMsg;
    return TRUE;
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
    {
        unsigned int buttons = m_mouseButtons.load();
        unsigned int msgButton = mouseButtonBitFromMsg(message);
        if (isMouseButtonDownMsg(message))
        {
            buttons |= msgButton;
        }
        else if (isMouseButtonUpMsg(message))
        {
            buttons &= ~msgButton;
        }
        else if (message == WM_MOUSEMOVE)
        {
            unsigned int postedButtons = (unsigned int)wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON);
            if (postedButtons == 0 && buttons != 0)
            {
                message = mouseButtonUpMsgFromButtons(buttons);
                msgButton = mouseButtonBitFromMsg(message);
                buttons &= ~msgButton;
            }
            else if (postedButtons != 0)
            {
                buttons = postedButtons;
            }
        }
        m_mouseButtons.store(buttons);
        wp = (wp & ~(MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) | buttons;

        if (m_hWndCapture && shouldSendToCapture(message))
        {
            hWnd = m_hWndCapture;
        }
    }
    Msg *msg = new Msg;
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    GetCursorPos(&msg->pt);
    postMsg(msg);
}

void SConnection::postMsg(Msg *pMsg)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    if (pMsg->message == WM_PAINT && hasQueuedPaintMsg(m_msgQueue, pMsg->hwnd))
    {
        delete pMsg;
        return;
    }
    if (pMsg->message == WM_MOUSEMOVE)
    {
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end();)
        {
            Msg *queued = *it;
            if (queued && queued->message == WM_MOUSEMOVE && queued->hwnd == pMsg->hwnd)
            {
                it = m_msgQueue.erase(it);
                delete queued;
            }
            else
            {
                ++it;
            }
        }
    }
    if (pMsg->message == WM_TIMER && hasQueuedTimerMsg(m_msgQueue, pMsg->hwnd, pMsg->wParam, (TIMERPROC)pMsg->lParam))
    {
        delete pMsg;
        return;
    }
    m_msgQueue.push_back(pMsg);
    if (g_platformAPI.window.postMessage && m_msgQueue.size()==1)
    {
        g_platformAPI.window.postMessage();
    }
}

void SConnection::postMsg2(BOOL, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply)
{
    Msg *msg = new Msg(reply);
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    GetCursorPos(&msg->pt);
    postMsg(msg);
}

void SConnection::postCallbackTask(CbTask *pTask)
{
    if (!pTask)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    pTask->AddRef();
    m_lstCallbackTask.push_back(pTask);
}

UINT_PTR SConnection::SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc)
{
    // If platform provides timer API, delegate to platform layer
    if (g_platformAPI.window.setTimer)
    {
        return g_platformAPI.window.setTimer(reinterpret_cast<UINT_PTR>(hWnd), id, uElapse, proc);
    }
    return 0;
}

BOOL SConnection::KillTimer(HWND hWnd, UINT_PTR id)
{
    // If platform provides timer kill API, delegate
    if (g_platformAPI.window.killTimer)
    {
        return g_platformAPI.window.killTimer(reinterpret_cast<UINT_PTR>(hWnd), id);
    }
    return FALSE;
}

HDC SConnection::GetDC()
{
    return m_deskDC;
}

BOOL SConnection::ReleaseDC(HDC)
{
    return TRUE;
}

HWND SConnection::SetCapture(HWND hCapture)
{
    HWND old = m_hWndCapture;
    m_hWndCapture = hCapture;
    if(g_platformAPI.window.setCapture){
        return g_platformAPI.window.setCapture(hCapture);
    }
    return old;
}

BOOL SConnection::ReleaseCapture()
{
    if(g_platformAPI.window.releaseCapture){
        return g_platformAPI.window.releaseCapture();
    }
    m_hWndCapture = 0;
    return TRUE;
}

HWND SConnection::GetCapture() const
{
    return m_hWndCapture;
}

HCURSOR SConnection::SetCursor(HWND hWnd, HCURSOR cursor)
{
    HCURSOR old = m_wndCursor[hWnd];
    m_wndCursor[hWnd] = cursor;
    return old;
}

HCURSOR SConnection::GetCursor()
{
    auto it = m_wndCursor.find(m_hFocus);
    return it == m_wndCursor.end() ? 0 : it->second;
}

BOOL SConnection::DestroyCursor(HCURSOR)
{
    return TRUE;
}

void SConnection::SetTimerBlock(bool bBlock)
{

}

HWND SConnection::GetActiveWnd() const
{
    return m_hActive;
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    m_hActive = hWnd;
    m_hForeground = hWnd;
    return TRUE;
}

HWND SConnection::WindowFromPoint(POINT pt, HWND) const
{
    return m_hActive;
}

BOOL SConnection::IsWindow(HWND hWnd) const
{
    return (bool)WndMgr::fromHwnd(hWnd);
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const
{
    // If platform API provides setWindowPos and this is a virtual HWND, delegate
    if (g_platformAPI.window.setWindowPos && IsWindow(hWnd))
    {
        g_platformAPI.window.setWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return;
    if (g_platformAPI.window.setWindowSize)
    {
        g_platformAPI.window.setWindowSize(hWnd, cx, cy);
    }
}

BOOL SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    if (g_platformAPI.window.moveWindow)
    {
        g_platformAPI.window.moveWindow(hWnd, x, y, cx, cy, TRUE);
    }
    return TRUE;
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    if(g_platformAPI.window.getCursorPos){
        return g_platformAPI.window.getCursorPos(ppt);
    }
    return FALSE;
}

int SConnection::GetDpi(BOOL) const
{
    if (g_platformAPI.window.getDpi) {
        return g_platformAPI.window.getDpi();
    }
    return 96;
}

void SConnection::KillWindowTimer(HWND hWnd)
{
    // If platform provides a way to cancel all timers for a window, use it
    if (g_platformAPI.window.killWindowTimers)
    {
        g_platformAPI.window.killWindowTimers(reinterpret_cast<UINT_PTR>(hWnd));
        return;
    }
}

HWND SConnection::GetForegroundWindow()
{
    return m_hForeground;
}

BOOL SConnection::SetForegroundWindow(HWND hWnd)
{
    m_hForeground = hWnd;
    return SetActiveWindow(hWnd);
}

BOOL SConnection::BringWindowToTop(HWND hWnd)
{
    return SetForegroundWindow(hWnd);
}

BOOL SConnection::SetWindowOpacity(HWND hWnd, BYTE)
{
    return TRUE;
}

BOOL SConnection::SetWindowRgn(HWND hWnd, HRGN)
{
    return TRUE;
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl)
{
    return hKl;
}

HBITMAP SConnection::GetDesktopBitmap()
{
    return m_deskBmp;
}

HWND SConnection::GetFocus() const
{
    return m_hFocus;
}

BOOL SConnection::SetFocus(HWND hWnd)
{
    if (g_platformAPI.window.setFocus) {
        g_platformAPI.window.setFocus(hWnd);
    }
    HWND old = m_hFocus;
    m_hFocus = hWnd;
    if (old && old != hWnd)
        SendMessageA(old, WM_KILLFOCUS, (WPARAM)hWnd, 0);
    if (hWnd && old != hWnd)
        SendMessageA(hWnd, WM_SETFOCUS, (WPARAM)old, 0);
    return TRUE;
}

BOOL SConnection::IsDropTarget(HWND)
{
    return FALSE;
}

BOOL SConnection::FlashWindowEx(PFLASHWINFO)
{
    return FALSE;
}

int SConnection::OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !lpClassName || nMaxCount <= 0)
        return 0;
    return GetAtomNameA(wndObj->clsAtom, lpClassName, nMaxCount);
}

BOOL SConnection::OnSetWindowText(HWND, _Window *wndObj, LPCSTR lpszString)
{
    if (!wndObj)
        return FALSE;
    wndObj->title = lpszString ? lpszString : "";
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    return wndObj ? (int)wndObj->title.length() : 0;
}

int SConnection::OnGetWindowTextLengthW(HWND hWnd)
{
    return OnGetWindowTextLengthA(hWnd);
}

int SConnection::OnGetWindowTextA(HWND hWnd, char *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !buf || bufLen <= 0)
        return 0;
    int len = std::min<int>((int)wndObj->title.length(), bufLen - 1);
    memcpy(buf, wndObj->title.c_str(), len);
    buf[len] = 0;
    return len;
}

int SConnection::OnGetWindowTextW(HWND hWnd, wchar_t *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !buf || bufLen <= 0)
        return 0;
    int len = std::min<int>((int)wndObj->title.length(), bufLen - 1);
    for (int i = 0; i < len; ++i)
        buf[i] = (unsigned char)wndObj->title[i];
    buf[len] = 0;
    return len;
}

HWND SConnection::OnFindWindowEx(HWND, HWND, LPCSTR, LPCSTR)
{
    return 0;
}

BOOL SConnection::OnEnumWindows(HWND, HWND, WNDENUMPROC, LPARAM)
{
    return FALSE;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags)
{
    switch (gaFlags)
    {
        case GA_PARENT:
            return GetParent(hwnd);
        case GA_ROOT:
        {
            HWND ret = hwnd;
            while(ret){
                if(0==(GetWindowLongPtr(ret,GWL_STYLE)&WS_CHILD))
                    break;
                ret = GetParent(ret);
            }
            return ret;
        }
        case GA_ROOTOWNER:
        {
            HWND ret = OnGetAncestor(hwnd,GA_ROOT);
            HWND hOwner = GetParent(ret);
            if (hOwner)
                ret = hOwner;
            return ret;
        }
        default:
            return 0;
    }
}

HMONITOR SConnection::MonitorFromWindow(HWND, DWORD)
{
    return GetScreen(0);
}

HMONITOR SConnection::MonitorFromPoint(POINT, DWORD)
{
    return GetScreen(0);
}

HMONITOR SConnection::MonitorFromRect(LPCRECT, DWORD)
{
    return GetScreen(0);
}

int SConnection::GetScreenWidth(HMONITOR) const
{
    if(g_platformAPI.window.getScreenWidth){
        return g_platformAPI.window.getScreenWidth(0);
    }
    return 1280;
}

int SConnection::GetScreenHeight(HMONITOR) const
{
    if(g_platformAPI.window.getScreenHeight){
        return g_platformAPI.window.getScreenHeight(0);
    }
    return 720;
}

HWND SConnection::GetScreenWindow() const
{
    if(g_platformAPI.window.getScreen){
        return g_platformAPI.window.getScreen();
    }
    return 0;
}

void SConnection::UpdateWindowIcon(HWND, _Window *)
{
}

uint32_t SConnection::GetVisualID(BOOL) const
{
    return 0;
}

uint32_t SConnection::GetCmap() const
{
    return 0;
}

void SConnection::SetZOrder(HWND, _Window *, HWND)
{
}

void SConnection::OnStyleChanged(HWND, _Window *, DWORD, DWORD)
{
}

void SConnection::OnExStyleChanged(HWND, _Window *, DWORD, DWORD)
{
}

void SConnection::SendClientMessage(HWND, uint32_t, uint32_t *, int)
{
}

uint32_t SConnection::GetIpcAtom() const
{
    return 0;
}

cairo_surface_t *SConnection::CreateWindowSurface(HWND hWnd, uint32_t visualId, int cx, int cy)
{
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,cx,cy);
}

cairo_surface_t * SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId,int cx, int cy)
{
    if(surface) cairo_surface_destroy(surface);
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,cx,cy);
}


DWORD SConnection::GetWndProcessId(HWND)
{
    return (DWORD)getpid();
}

HWND SConnection::WindowFromPoint(POINT pt)
{
    return m_hActive;
}

BOOL SConnection::GetClientRect(HWND hWnd, RECT *pRc)
{
    if (!pRc)
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
    {
        return FALSE;
    }
    *pRc = wndObj->rc;
    OffsetRect(pRc, -pRc->left, -pRc->top);
    return TRUE;
}

void SConnection::SendSysCommand(HWND hWnd, int cmd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(cmd == SC_MAXIMIZE){

    }
}

BOOL SConnection::IsWindowVisible(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if(!wndObj)
        return FALSE;
    return (wndObj->dwStyle & WS_VISIBLE);
}

HWND SConnection::GetWindow(HWND hWnd, _Window *, UINT code)
{
    if (g_platformAPI.window.getWindow)
    {
        return g_platformAPI.window.getWindow(hWnd, code);
    }
}

UINT SConnection::RegisterMessage(LPCSTR lpString)
{
    if (!lpString)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(s_registeredMessageMutex);
    std::string key(lpString);
    auto it = s_registeredMessages.find(key);
    if (it != s_registeredMessages.end())
        return it->second;
    UINT msg = s_nextRegisteredMessage++;
    s_registeredMessages[key] = msg;
    return msg;
}

UINT SConnection::RegisterClipboardFormatA(LPCSTR pszName)
{
    return SClipboard::RegisterClipboardFormatA(pszName);
}

BOOL SConnection::NotifyIcon(DWORD, PNOTIFYICONDATAA)
{
    return FALSE;
}

HMONITOR SConnection::GetScreen(DWORD) const
{
    return reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(m_screenNum + 1));
}

void SConnection::updateWindow(HWND, const RECT &)
{
}

void SConnection::commitCanvas(HWND, const RECT &)
{
}

void SConnection::EnableWindow(HWND hWnd, BOOL bEnable)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    if (g_platformAPI.window.enableWindow)
    {
        g_platformAPI.window.enableWindow(hWnd, bEnable);
    }
    if (bEnable)
        wndObj->dwStyle &= ~WS_DISABLED;
    else
        wndObj->dwStyle |= WS_DISABLED;
}

BOOL SConnection::IsIconic(HWND) const
{
    return FALSE;
}

BOOL SConnection::IsZoomed(HWND) const
{
    return FALSE;
}

int SConnection::ShowCursor(BOOL bShow)
{
    m_cursorCount += bShow ? 1 : -1;
    return m_cursorCount;
}

BOOL SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    DestroyCaret();
    m_caretInfo.hOwner = hWnd;
    m_caretInfo.hBmp = RefGdiObj(hBitmap == (HBITMAP)1 ? nullptr : hBitmap);
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    return TRUE;
}

BOOL SConnection::DestroyCaret()
{
    if (m_caretInfo.nVisible > 0)
        KillTimer(m_caretInfo.hOwner, TM_CARET);
    DeleteObject(m_caretInfo.hBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    return TRUE;
}

BOOL SConnection::ShowCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    if (++m_caretInfo.nVisible == 1)
        SetTimer(m_caretInfo.hOwner, TM_CARET, m_caretBlinkTime, NULL);
    return TRUE;
}

BOOL SConnection::HideCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    if (m_caretInfo.nVisible > 0 && --m_caretInfo.nVisible == 0)
        KillTimer(m_caretInfo.hOwner, TM_CARET);
    return TRUE;
}

BOOL SConnection::SetCaretPos(int X, int Y)
{
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
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

const SConnection::CaretInfo *SConnection::GetCaretInfo() const
{
    return &m_caretInfo;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime)
{
    m_caretBlinkTime = blinkTime;
}

UINT SConnection::GetCaretBlinkTime() const
{
    return m_caretBlinkTime;
}

void SConnection::GetWorkArea(HMONITOR, RECT *prc)
{
    if (prc)
        *prc = {0, 0, GetScreenWidth(0), GetScreenHeight(0)};
}

SClipboard *SConnection::getClipboard()
{
    return m_clipboard;
}

BOOL SConnection::EmptyClipboard()
{
    return m_clipboard->emptyClipboard();
}

BOOL SConnection::IsClipboardFormatAvailable(UINT format)
{
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

HANDLE SConnection::GetClipboardData(UINT uFormat)
{
    return m_clipboard->getClipboardData(uFormat);
}

HANDLE SConnection::SetClipboardData(UINT uFormat, HANDLE hMem)
{
    return m_clipboard->setClipboardData(uFormat, hMem);
}

void SConnection::EnableDragDrop(HWND, BOOL)
{
}

HRESULT SConnection::DoDragDrop(IDataObject *, IDropSource *, DWORD, DWORD *)
{
    return E_NOTIMPL;
}

HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int)
{
    HWND hWnd = 0;
    if (g_platformAPI.window.createWindow)
    {
        // 12 参与 Win32 CreateWindowEx / platform_api.h PlatformWindowAPI::createWindow 完全对齐。
        // 关键：HMENU hMenu 在 WS_CHILD 子窗口语义 == 子窗口控件 ID（GetDlgCtrlID/事件映射
        // 按 ID 匹配依赖此字段），必须原封下传到各平台实现。
        hWnd = (HWND)g_platformAPI.window.createWindow(reinterpret_cast<UINT_PTR>(cs->hwndParent),
                                                        cs->lpszClass,
                                                        cs->lpszName,
                                                        cs->style,
                                                        cs->dwExStyle,
                                                        cs->x, cs->y, cs->cx, cs->cy,
                                                        reinterpret_cast<UINT_PTR>(cs->hMenu),
                                                        reinterpret_cast<UINT_PTR>(cs->hInstance),
                                                        cs->lpCreateParams);
        if (!hWnd)
        {
            SLOG_FMTD("Android native window created via platform API failed: parent=%p style=0x%x cls=%s id(menu)=%p",
                    (void*)cs->hwndParent, (unsigned)cs->style,
                      cs->lpszClass ? cs->lpszClass : "(null)",
                      (void*)cs->hMenu);
        }
    }
    return hWnd;
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *)
{
    if (g_platformAPI.window.destroyWindow)
    {
        g_platformAPI.window.destroyWindow(reinterpret_cast<UINT_PTR>(hWnd));
    }

    if (m_hWndCapture == hWnd)
        m_hWndCapture = 0;
    if (m_hFocus == hWnd)
        m_hFocus = 0;
    if (m_hActive == hWnd)
        m_hActive = 0;
    if (m_hForeground == hWnd)
        m_hForeground = 0;

    // Ensure virtual HWND unregister frees the window object
    //UnregisterVirtualHWND(reinterpret_cast<UINT_PTR>(hWnd));
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wnd, BOOL bVisible, int nCmdShow)
{
    if (!wnd)
        return;
    if (g_platformAPI.window.showWindow)
    {
        g_platformAPI.window.showWindow(hWnd, bVisible ? nCmdShow : SW_HIDE);
    }
    if (bVisible)
    {
        wnd->dwStyle |= WS_VISIBLE;
    }
    else
    {
        wnd->dwStyle &= ~WS_VISIBLE;
    }
}

void SConnection::SetParent(HWND hWnd, _Window *wnd, HWND parent)
{
    //not impl
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc, BOOL)
{
    if (g_platformAPI.window.invalidRect)
    {
        g_platformAPI.window.invalidRect(hWnd, rc);
    }
}

void SConnection::SetWindowMsgTransparent(HWND, _Window *, BOOL)
{
}

void SConnection::AssociateHIMC(HWND, _Window *, HIMC)
{
}

void SConnection::flush()
{
}

void SConnection::sync()
{
}


BOOL SConnection::IsScreenComposited() const
{
    return TRUE;
}

UINT SConnection::GetRawInputDeviceList(
        _Out_writes_opt_(*puiNumDevices) PRAWINPUTDEVICELIST pRawInputDeviceList,
        _Inout_ PUINT puiNumDevices,
        _In_ UINT cbSize)
{
    if(g_platformAPI.window.getRawInputDeviceList)
        return g_platformAPI.window.getRawInputDeviceList(pRawInputDeviceList, puiNumDevices, cbSize);
    return 0;
}

UINT SConnection::GetRawInputDeviceInfoA(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize)
{
    if(g_platformAPI.window.getRawInputDeviceInfoA)
        return g_platformAPI.window.getRawInputDeviceInfoA(hDevice, uiCommand, pData,pcbSize);
    return 0;
}

UINT SConnection::GetRawInputDeviceInfoW(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize)
{
    if(g_platformAPI.window.getRawInputDeviceInfoW)
        return g_platformAPI.window.getRawInputDeviceInfoW(hDevice, uiCommand, pData,pcbSize);
    return 0;
}

BOOL SConnection::ShowSoftKeyboard(HWND hWnd, BOOL bShow){
    if(g_platformAPI.window.showSoftKeyboard)
        return g_platformAPI.window.showSoftKeyboard(hWnd,bShow);
    return FALSE;
}

void SConnection::BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    updateKeyboardStateForMsg(m_keyboardState, msg, wp);

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

//=============================================================================
// SConnMgr Implementation
//=============================================================================

SConnMgr *SConnMgr::instance()
{
    static SConnMgr inst;
    return &inst;
}

SConnMgr::SConnMgr()
    : m_hHeap(HeapCreate(0, 0, 0))
{
}

SConnMgr::~SConnMgr()
{
    for (auto &item : m_conns)
        delete item.second;
    m_conns.clear();
    CloseHandle(m_hHeap);
}

SConnection *SConnMgr::getConnection(tid_t tid, int screenNum)
{
    tid_t currentTid = GetCurrentThreadId();
    tid_t targetTid = tid != 0 ? tid : currentTid;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_conns.find(targetTid);
        if (it != m_conns.end())
            return it->second;
    }
    if (tid != 0 && targetTid != currentTid)
        return nullptr;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_conns.find(targetTid);
    if (it != m_conns.end())
        return it->second;

    SConnection *conn = new SConnection(screenNum);
    m_conns[targetTid] = conn;
    return conn;
}

HANDLE SConnMgr::getProcessHeap()
{
    return m_hHeap;
}