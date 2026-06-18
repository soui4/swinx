#include "SConnection.h"
#include "SImContext.h"
#include "keyboard.h"
#include "napi_bridge.h"
#include "wndobj.h"
#include "sdc.h"
#include <gdi.h>
#include <ohos_ime_bridge.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <unistd.h>

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

SConnection::SConnection(int)
    : m_msgPeek(nullptr)
    , m_bMsgNeedFree(false)
    , m_bBlockTimer(false)
    , m_tsLastMsg(0)
    , m_bQuit(false)
    , m_tid(GetCurrentThreadId())
    , m_deskDC(new _SDC(0))
    , m_deskBmp(nullptr)
    , m_hWndCapture(0)
    , m_hFocus(0)
    , m_hActive(0)
    , m_hForeground(0)
    , m_mouseButtons(0)
    , m_cursorCount(1)
    , m_caretBlinkTime(TS_CARET)
    , m_clipboard(new SClipboard())
    , m_trayIconMgr(new STrayIconMgr())
{
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    memset(m_keyboardState, 0, sizeof(m_keyboardState));
}

SConnection::~SConnection()
{
    std::unique_lock<CountMutex> lock(m_mutex);
    delete m_clipboard;
    delete m_trayIconMgr;
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
}

void SConnection::onTerminate()
{
    m_bQuit = true;
    postMsg(0, WM_QUIT, 0, 0);
}

void SConnection::OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    postMsg(hWnd, message, wParam, lParam);
}

void SConnection::OnDrawRect(HWND, const RECT &, cairo_t *)
{
}

void SConnection::OnNsActive(HWND hWnd, BOOL bActive)
{
    if (bActive)
        SetActiveWindow(hWnd);
    else if (m_hActive == hWnd)
        m_hActive = 0;
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
        return OhosVKToKeyCode(uCode);
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        return OhosKeyCodeToVK(uCode);
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
    if (dwTimeout != 0 && dwTimeout != INFINITE)
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<DWORD>(dwTimeout, 10)));
    else if (dwTimeout == INFINITE)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::unique_lock<CountMutex> lock(m_mutex);
    if (m_bBlockTimer)
        return;

    UINT now = GetTickCount();
    for (auto &timer : m_lstTimer)
    {
        if ((INT)(now - timer.fireRemain) >= 0)
        {
            if (!hasQueuedTimerMsg(m_msgQueue, timer.hWnd, timer.id, timer.proc))
            {
                Msg *msg = new Msg;
                msg->hwnd = timer.hWnd;
                msg->message = WM_TIMER;
                msg->wParam = timer.id;
                msg->lParam = (LPARAM)timer.proc;
                msg->time = now;
                m_msgQueue.push_back(msg);
            }
            timer.fireRemain = now + timer.elapse;
        }
    }
}

bool SConnection::waitMsg(UINT timeOut)
{
    updateMsgQueue(timeOut);
    std::unique_lock<CountMutex> lock(m_mutex);
    return !m_msgQueue.empty();
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL, DWORD)
{
    DWORD start = GetTickCount();
    do
    {
        for (int i = 0; i < nCount; ++i)
        {
            if (WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
                return WAIT_OBJECT_0 + i;
        }
        if (waitMsg(1))
            return WAIT_OBJECT_0 + nCount;
    } while (timeout == INFINITE || GetTickCount() - start < timeout);
    return WAIT_TIMEOUT;
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    if (!pMsg)
        return FALSE;
    if (pMsg->message == WM_KEYDOWN)
    {
        if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000))
            return TRUE;
        UINT ch = MapVirtualKey((UINT)pMsg->wParam, MAPVK_VK_TO_CHAR);
        if (ch)
            postMsg(pMsg->hwnd, WM_CHAR, ch, pMsg->lParam);
    }
    return TRUE;
}

BOOL SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    updateMsgQueue(0);
    std::unique_lock<CountMutex> lock(m_mutex);

    for (auto itTask = m_lstCallbackTask.begin(); itTask != m_lstCallbackTask.end();)
    {
        CbTask *task = *itTask;
        if (task->TestEvent())
        {
            itTask = m_lstCallbackTask.erase(itTask);
            task->Release();
        }
        else
        {
            ++itTask;
        }
    }

    auto it = m_msgQueue.begin();
    for (; it != m_msgQueue.end(); ++it)
    {
        Msg *msg = *it;
        bool match = true;
        if (msg->hwnd != hWnd && hWnd != 0)
            match = false;
        if (match && (wMsgFilterMin != 0 || wMsgFilterMax != 0) && (msg->message < wMsgFilterMin || msg->message > wMsgFilterMax))
            match = false;
        if (match)
            break;
    }

    if (it == m_msgQueue.end())
        return FALSE;

    if (m_msgPeek && m_bMsgNeedFree)
    {
        delete m_msgPeek;
        m_msgPeek = nullptr;
        m_bMsgNeedFree = false;
    }

    Msg *msg = *it;
    if (msg->message == WM_TIMER && msg->lParam)
    {
        TIMERPROC proc = (TIMERPROC)msg->lParam;
        HWND timerWnd = msg->hwnd;
        UINT_PTR timerId = msg->wParam;
        DWORD timerTime = msg->time;
        m_msgQueue.erase(it);
        LONG lockCount = m_mutex.FreeLock();
        proc(timerWnd, WM_TIMER, timerId, timerTime);
        m_mutex.RestoreLock(lockCount);
        delete msg;
        return peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }

    m_msgPeek = msg;
    if (wRemoveMsg == PM_NOREMOVE)
    {
        m_bMsgNeedFree = false;
    }
    else
    {
        m_msgQueue.erase(it);
        m_bMsgNeedFree = true;
    }
    memcpy(pMsg, (MSG *)m_msgPeek, sizeof(MSG));
    if (wRemoveMsg != PM_NOREMOVE)
        updateKeyboardStateForMsg(m_keyboardState, pMsg->message, pMsg->wParam);
    m_tsLastMsg = pMsg->time;
    return TRUE;
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    while (!m_bQuit)
    {
        if (peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE))
            return lpMsg->message != WM_QUIT;
        waitMsg();
    }
    return FALSE;
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        POINT screenPt = pt;
        mapOhosPointToWindow(hWnd, 0, &screenPt);
        setOhosCursorPos(screenPt);

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
            pt = screenPt;
            mapOhosPointToWindow(0, hWnd, &pt);
            lp = MAKELPARAM(pt.x, pt.y);
        }
        else if (shouldSendToCapture(message))
        {
            HWND hit = ohosHwndFromPoint(0, screenPt);
            if (hit)
            {
                hWnd = hit;
                pt = screenPt;
                mapOhosPointToWindow(0, hWnd, &pt);
                lp = MAKELPARAM(pt.x, pt.y);
            }
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
    std::unique_lock<CountMutex> lock(m_mutex);
    if (id == 0)
        id = (UINT_PTR)(&m_lstTimer) ^ (UINT_PTR)GetTickCount();
    KillTimer(hWnd, id);
    TimerInfo timer = { id, hWnd, uElapse ? uElapse : 1, GetTickCount() + (uElapse ? uElapse : 1), proc };
    m_lstTimer.push_back(timer);
    return id;
}

BOOL SConnection::KillTimer(HWND hWnd, UINT_PTR id)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); ++it)
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

BOOL SConnection::ReleaseDC(HDC)
{
    return TRUE;
}

HWND SConnection::SetCapture(HWND hCapture)
{
    HWND old = m_hWndCapture;
    if (setOhosWindowCapture(hCapture))
        m_hWndCapture = hCapture;
    return old;
}

BOOL SConnection::ReleaseCapture()
{
    releaseOhosWindowCapture(m_hWndCapture);
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
    setOhosWindowCursor(hWnd, cursor);
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
    m_bBlockTimer = bBlock;
}

HWND SConnection::GetActiveWnd() const
{
    return m_hActive ? m_hActive : getOhosActiveWindow();
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    if (!setOhosActiveWindow(hWnd))
        return FALSE;
    m_hActive = hWnd;
    m_hForeground = hWnd;
    return TRUE;
}

HWND SConnection::WindowFromPoint(POINT pt, HWND) const
{
    return ohosHwndFromPoint(0, pt);
}

BOOL SConnection::IsWindow(HWND hWnd) const
{
    RECT rc;
    return getOhosWindowRect(hWnd, &rc);
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const
{
    setOhosWindowPos(hWnd, x, y);
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const
{
    setOhosWindowSize(hWnd, cx, cy);
}

BOOL SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const
{
    return setOhosWindowPos(hWnd, x, y) && setOhosWindowSize(hWnd, cx, cy);
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    return getOhosCursorPos(ppt);
}

int SConnection::GetDpi(BOOL bx) const
{
    return getOhosDpi(bx);
}

void SConnection::KillWindowTimer(HWND hWnd)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end();)
    {
        if (it->hWnd == hWnd)
            it = m_lstTimer.erase(it);
        else
            ++it;
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
    setOhosWindowZorder(hWnd, HWND_TOP);
    return SetForegroundWindow(hWnd);
}

BOOL SConnection::SetWindowOpacity(HWND hWnd, BYTE byAlpha)
{
    return setOhosWindowAlpha(hWnd, byAlpha);
}

BOOL SConnection::SetWindowRgn(HWND hWnd, HRGN hRgn)
{
    RECT rc;
    if (!GetRgnBox(hRgn, &rc))
        return FALSE;
    return setOhosWindowRgn(hWnd, &rc, 1);
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
    return m_hFocus ? m_hFocus : getOhosFocusWindow();
}

BOOL SConnection::SetFocus(HWND hWnd)
{
    HWND old = m_hFocus;
    if (!hWnd && old && swinx::ohos::IsImeProxyActive())
        return TRUE;
    if (!setOhosFocusWindow(hWnd))
        return FALSE;
    m_hFocus = hWnd;
    if (old && old != hWnd)
        postMsg(old, WM_KILLFOCUS, (WPARAM)hWnd, 0);
    if (hWnd && old != hWnd)
        postMsg(hWnd, WM_SETFOCUS, (WPARAM)old, 0);
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
    if (gaFlags == GA_PARENT)
        return GetWindow(hwnd, nullptr, GW_OWNER);
    return hwnd;
}

HMONITOR SConnection::MonitorFromWindow(HWND, DWORD)
{
    return (HMONITOR)1;
}

HMONITOR SConnection::MonitorFromPoint(POINT, DWORD)
{
    return (HMONITOR)1;
}

HMONITOR SConnection::MonitorFromRect(LPCRECT, DWORD)
{
    return (HMONITOR)1;
}

int SConnection::GetScreenWidth(HMONITOR) const
{
    swinx::ohos::XComponentState state = swinx::ohos::GetXComponentState();
    return state.width > 0 ? state.width : 1280;
}

int SConnection::GetScreenHeight(HMONITOR) const
{
    swinx::ohos::XComponentState state = swinx::ohos::GetXComponentState();
    return state.height > 0 ? state.height : 720;
}

HWND SConnection::GetScreenWindow() const
{
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

void SConnection::SetZOrder(HWND hWnd, _Window *, HWND hWndInsertAfter)
{
    setOhosWindowZorder(hWnd, hWndInsertAfter);
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

cairo_surface_t *SConnection::CreateWindowSurface(HWND, uint32_t, int cx, int cy)
{
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(cx, 1), std::max(cy, 1));
}

cairo_surface_t *SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId, int width, int height)
{
    if (surface)
        cairo_surface_destroy(surface);
    return CreateWindowSurface(hWnd, visualId, width, height);
}

DWORD SConnection::GetWndProcessId(HWND)
{
    return (DWORD)getpid();
}

HWND SConnection::WindowFromPoint(POINT pt)
{
    return ohosHwndFromPoint(0, pt);
}

BOOL SConnection::GetClientRect(HWND hWnd, RECT *pRc)
{
    if (!getOhosWindowRect(hWnd, pRc))
        return FALSE;
    OffsetRect(pRc, -pRc->left, -pRc->top);
    return TRUE;
}

void SConnection::SendSysCommand(HWND hWnd, int nCmd)
{
    sendOhosSysCommand(hWnd, nCmd);
}

BOOL SConnection::IsWindowVisible(HWND hWnd)
{
    return isOhosWindowVisible(hWnd);
}

HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd)
{
    (void)wndObj;
    return getOhosWindow(hWnd, uCmd);
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

BOOL SConnection::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    return m_trayIconMgr->NotifyIcon(dwMessage, lpData);
}

HMONITOR SConnection::GetScreen(DWORD) const
{
    return (HMONITOR)1;
}

void SConnection::updateWindow(HWND hWnd, const RECT &rc)
{
    updateOhosWindow(hWnd, rc);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wndObj->bmp)
        return;
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    commitOhosWindow(hWnd, surface, rc);
}

void SConnection::EnableWindow(HWND hWnd, BOOL bEnable)
{
    enableOhosWindow(hWnd, bEnable);
}

BOOL SConnection::IsIconic(HWND hWnd) const
{
    return isOhosWindowMinimized(hWnd);
}

BOOL SConnection::IsZoomed(HWND hWnd) const
{
    return isOhosWindowMaximized(hWnd);
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
        *prc = { 0, 0, GetScreenWidth(0), GetScreenHeight(0) };
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

STrayIconMgr *SConnection::GetTrayIconMgr()
{
    return m_trayIconMgr;
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
    return createOhosWindow(cs->hwndParent, cs->style, cs->dwExStyle, wnd->bAutoDblClick, cs->lpszName, cs->x, cs->y, cs->cx, cs->cy, this);
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *)
{
    closeOhosWindow(hWnd);
    if (m_hWndCapture == hWnd)
        m_hWndCapture = 0;
    if (m_hFocus == hWnd)
        m_hFocus = 0;
    if (m_hActive == hWnd)
        m_hActive = 0;
    if (m_hForeground == hWnd)
        m_hForeground = 0;
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wnd, BOOL bVisible, int nCmdShow)
{
    if (!wnd)
        return;
    showOhosWindow(hWnd, bVisible ? nCmdShow : SW_HIDE);
    if (bVisible)
    {
        wnd->dwStyle |= WS_VISIBLE;
        InvalidateRect(hWnd, nullptr, TRUE);
    }
    else
    {
        wnd->dwStyle &= ~WS_VISIBLE;
    }
}

void SConnection::SetParent(HWND hWnd, _Window *, HWND parent)
{
    setOhosParent(hWnd, parent);
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc, BOOL)
{
    invalidateOhosWindow(hWnd, rc);
}

void SConnection::SetWindowMsgTransparent(HWND hWnd, _Window *, BOOL bTransparent)
{
    setOhosMsgTransparent(hWnd, bTransparent);
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
