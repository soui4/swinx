/*
 * SConnection.cpp -- FreeRTOS platform connection implementation.
 *
 * Message-queue semantics are ported from src/platform/mobile/SConnection.cpp
 * (WM_PAINT / WM_MOUSEMOVE / WM_TIMER coalescing, msg stack, callback tasks).
 * Differences from mobile:
 *   - no g_platformAPI injection: the queue is filled via postMsg() and the
 *     timers are driven inside updateMsgQueue() from the FreeRTOS tick;
 *   - waitMsg() blocks on an auto-reset Win32-compat event (winobjs); the cv
 *     shim only accepts std::mutex locks, so a recursive CountMutex cannot
 *     wait on it;
 *   - rendering: window canvases are cairo image surfaces (software), and
 *     commitCanvas() blits dirty regions into the framebuffer (framebuffer.h).
 */
#include "SConnection.h"
#include "wndobj.h"
#include "framebuffer.h"
#include <gdi.h>
#include <cairo.h>
#include <log.h>

#define kLogTag "SConnection"

static UINT s_nextRegisteredMessage = WM_USER + 100000;

SClipboard::FmtTable &SClipboard::fmtTable()
{
    // leak-on-purpose: format names live for the process lifetime
    static FmtTable *s_table = new FmtTable();
    return *s_table;
}

//=============================================================================
// SClipboard (in-memory)
//=============================================================================

SClipboard::SClipboard()
    : m_hOwner(0)
    , m_bOpen(false)
{
}

SClipboard::~SClipboard()
{
}

UINT SClipboard::RegisterClipboardFormatA(LPCSTR pszName)
{
    if (!pszName || !*pszName)
        return 0;
    FmtTable &table = fmtTable();
    std::lock_guard<std::recursive_mutex> lock(table.mutex);
    swinx_stl::string name(pszName);
    auto it = table.names.find(name);
    if (it != table.names.end())
        return it->second;
    UINT fmt = table.nextFmt++;
    table.names[name] = fmt;
    return fmt;
}

BOOL SClipboard::emptyClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return TRUE;
}

BOOL SClipboard::hasFormat(UINT format)
{
    (void)format;
    return FALSE;
}

BOOL SClipboard::openClipboard(HWND hWndNewOwner)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_bOpen)
        return FALSE;
    m_bOpen = true;
    m_hOwner = hWndNewOwner;
    return TRUE;
}

BOOL SClipboard::closeClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_bOpen)
        return FALSE;
    m_bOpen = false;
    return TRUE;
}

HWND SClipboard::getClipboardOwner()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_hOwner;
}

HANDLE SClipboard::getClipboardData(UINT uFormat)
{
    (void)uFormat;
    return NULL;
}

HANDLE SClipboard::setClipboardData(UINT uFormat, HANDLE hMem)
{
    // take ownership of the caller's HGLOBAL and drop it (no OS clipboard).
    (void)uFormat;
    if (hMem)
        GlobalFree(hMem);
    return hMem;
}

//=============================================================================
// SConnection Implementation
//=============================================================================

SConnection::SConnection(int screenNum)
    : m_msgPeek(nullptr)
    , m_bMsgNeedFree(false)
    , m_bBlockTimer(false)
    , m_nextTimerId(1)
    , m_tsLastMsg(0)
    , m_bQuit(false)
    , m_tid(GetCurrentThreadId())
    , m_deskDC(new _SDC(0))
    , m_deskBmp(nullptr)
    , m_screenNum(screenNum)
    , m_hWndCapture(0)
    , m_hFocus(0)
    , m_hActive(0)
    , m_hForeground(0)
    , m_nextRegMsg(WM_USER + 100000)
    , m_cursorCount(1)
    , m_screenW(320)
    , m_screenH(240)
    , m_caretBlinkTime(TS_CARET)
    , m_clipboard(new SClipboard())
{
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    memset(m_keyboardState, 0, sizeof(m_keyboardState));
    m_hQueueEvt = CreateEventA(nullptr, FALSE /*auto reset*/, FALSE, nullptr);
    SLOG_FMTI("SConnection created for thread %d", m_tid);
}

SConnection::~SConnection()
{
    std::unique_lock<CountMutex> lock(m_mutex);
    CloseHandle(m_hQueueEvt);
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

void SConnection::BeforeProcMsg(HWND, UINT, WPARAM, LPARAM)
{
}

void SConnection::AfterProcMsg(HWND, UINT, WPARAM, LPARAM, LRESULT)
{
}

SHORT SConnection::GetKeyState(int vk)
{
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
    for (int i = 0; i < 256; ++i)
        lpKeyState[i] = m_keyboardState[i];
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
        return MAKELONG((DWORD)m_msgQueue.size(), 0);
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

// fire due timers into the message queue (tick based, connection-internal)
void SConnection::updateMsgQueue(DWORD dwTimeout)
{
    (void)dwTimeout;
    if (m_bBlockTimer)
        return;
    uint64_t now = GetTickCount();

    std::unique_lock<CountMutex> lock(m_mutex);
    bool bPosted = false;
    for (auto &ti : m_lstTimer)
    {
        if (now - (uint64_t)ti.fireRemain >= ti.elapse || (uint64_t)ti.fireRemain > now)
        {
            ti.fireRemain = (UINT)now;
            Msg *msg = new Msg;
            msg->hwnd = ti.hWnd;
            msg->message = WM_TIMER;
            msg->wParam = (WPARAM)ti.id;
            msg->lParam = (LPARAM)ti.proc;
            msg->time = (DWORD)now;
            bPosted = true;
            m_msgQueue.push_back(msg);
        }
    }
    if (bPosted)
        SetEvent(m_hQueueEvt);
}

bool SConnection::waitMsg(UINT timeOut)
{
    updateMsgQueue(0);   // fire due timers before computing the wait bound

    // wait bound: user timeout, or until the next timer is due
    uint64_t waitMs = timeOut;
    {
        std::unique_lock<CountMutex> lock(m_mutex);
        if (!m_msgQueue.empty())
            return true;
    }
    if (!m_bBlockTimer && !m_lstTimer.empty())
    {
        uint64_t now = GetTickCount();
        for (auto &ti : m_lstTimer)
        {
            uint64_t due = (uint64_t)ti.fireRemain + ti.elapse;
            uint64_t delta = due > now ? due - now : 0;
            if (delta < waitMs)
                waitMs = delta;
        }
    }
    if (waitMs > 0x7fffffff)
        waitMs = 0x7fffffff;

    uint64_t start = GetTickCount();
    for (;;)
    {
        DWORD wait = WaitForSingleObject(m_hQueueEvt, (DWORD)waitMs);
        // timers are driven inside waitMsg: a due timer enqueues WM_TIMER
        // and sets the queue event, waking this very loop
        updateMsgQueue(0);
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return true;
        }
        // spurious wake or timeout: recompute the remainder
        uint64_t elapsed = GetTickCount() - start;
        if (elapsed >= (uint64_t)timeOut)
            return false;
        waitMs = (uint64_t)timeOut - elapsed;
    }
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL fWaitAll, DWORD dwWaitMask)
{
    // FreeRTOS port: poll objects then the queue; objects are Win32-compat
    // events/mutexes/semaphores from winobjs, safe to poll non-blocking.
    uint64_t start = GetTickCount();
    for (;;)
    {
        // 1. any object ready?
        for (int i = 0; i < nCount; i++)
        {
            DWORD wait = WaitForSingleObject(handles[i], 0);
            if (wait == WAIT_OBJECT_0)
            {
                if (fWaitAll)
                    continue;
                return WAIT_OBJECT_0 + i;
            }
            else if (wait == WAIT_ABANDONED)
            {
                if (fWaitAll)
                    continue;
                return WAIT_ABANDONED + i;
            }
        }
        if (fWaitAll && nCount > 0)
        {
            bool bAll = true;
            for (int i = 0; i < nCount; i++)
            {
                if (WaitForSingleObject(handles[i], 0) != WAIT_OBJECT_0)
                {
                    bAll = false;
                    break;
                }
            }
            if (bAll)
                return WAIT_OBJECT_0;
        }
        // 2. any message?
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return (int)(WAIT_OBJECT_0 + nCount);
        }
        // 3. timeout?
        if (timeout != INFINITE)
        {
            uint64_t now = GetTickCount();
            if (now - start >= (uint64_t)timeout)
                return WAIT_TIMEOUT;
        }
        updateMsgQueue(1);
        taskYIELD();
    }
    // not reached
    (void)dwWaitMask;
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    // FreeRTOS port: no IME; key-down -> WM_CHAR passthrough for ASCII.
    if (pMsg && (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN))
    {
        UINT vk = (UINT)pMsg->wParam;
        if (vk >= 0x20 && vk <= 0x7e)
        {
            MSG charMsg;
            charMsg.hwnd = pMsg->hwnd;
            charMsg.message = WM_CHAR;
            charMsg.wParam = vk;
            charMsg.lParam = pMsg->lParam;
            postMsg(charMsg.hwnd, WM_CHAR, vk, pMsg->lParam);
            return TRUE;
        }
    }
    return FALSE;
}

static bool isSameTimerMsg(const Msg *msg, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    return msg && msg->message == WM_TIMER && msg->hwnd == hWnd && msg->wParam == (WPARAM)id && msg->lParam == (LPARAM)proc;
}

static bool hasQueuedTimerMsg(const swinx_stl::list<Msg *> &queue, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    for (auto msg : queue)
    {
        if (isSameTimerMsg(msg, hWnd, id, proc))
            return true;
    }
    return false;
}

static bool hasQueuedPaintMsg(const swinx_stl::list<Msg *> &queue, HWND hWnd)
{
    for (auto msg : queue)
    {
        if (msg && msg->message == WM_PAINT && msg->hwnd == hWnd)
            return true;
    }
    return false;
}

BOOL SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    // Win32 semantics: (0,0) means "no filter"
    bool bNoFilter = (wMsgFilterMin == 0 && wMsgFilterMax == 0);
    for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); ++it)
    {
        Msg *msg = *it;
        if (hWnd && hWnd != msg->hwnd && msg->hwnd != NULL)
            continue;
        UINT id = msg->message;
        if (!bNoFilter)
        {
            if (wMsgFilterMin <= wMsgFilterMax && (id < wMsgFilterMin || id > wMsgFilterMax))
                continue;
            if (wMsgFilterMin > wMsgFilterMax && (id > wMsgFilterMax && id < wMsgFilterMin))
                continue;   // wrapped range (Win32): only messages outside (max, min)
        }
        memcpy((void *)pMsg, (MSG *)msg, sizeof(MSG));
        if (wRemoveMsg & PM_REMOVE)
        {
            m_msgQueue.erase(it);
            delete msg;
        }
        return TRUE;
    }
    return FALSE;
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    updateMsgQueue(0);
    std::unique_lock<CountMutex> lock(m_mutex);
    // Win32 semantics: (0,0) means "no filter"
    bool bNoFilter = (wMsgFilterMin == 0 && wMsgFilterMax == 0);
    for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); ++it)
    {
        Msg *msg = *it;
        if (hWnd && hWnd != msg->hwnd && msg->hwnd != NULL)
            continue;
        UINT id = msg->message;
        if (!bNoFilter)
        {
            if (wMsgFilterMin <= wMsgFilterMax && (id < wMsgFilterMin || id > wMsgFilterMax))
                continue;
            if (wMsgFilterMin > wMsgFilterMax && (id > wMsgFilterMax && id < wMsgFilterMin))
                continue;   // wrapped range (Win32)
        }
        m_msgQueue.erase(it);
        memcpy((void *)lpMsg, (MSG *)msg, sizeof(MSG));
        delete msg;
        return TRUE;
    }
    return FALSE;
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp)
{
    Msg *msg = new Msg;
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    msg->pt.x = 0;
    msg->pt.y = 0;
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
    SetEvent(m_hQueueEvt);
}

void SConnection::postMsg2(BOOL, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply)
{
    Msg *msg = new Msg(reply);
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    msg->pt.x = 0;
    msg->pt.y = 0;
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
    // reuse: replace same (hWnd,id,proc)
    for (auto &ti : m_lstTimer)
    {
        if (ti.hWnd == hWnd && ti.id == id && ti.proc == proc)
        {
            ti.elapse = uElapse;
            ti.fireRemain = (UINT)GetTickCount();
            return id;
        }
    }
    if (id == 0)
        id = m_nextTimerId++;
    TimerInfo ti;
    ti.hWnd = hWnd;
    ti.id = id;
    ti.elapse = uElapse;
    ti.fireRemain = (UINT)GetTickCount();
    ti.proc = proc;
    m_lstTimer.push_back(ti);
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
    return old;
}

BOOL SConnection::ReleaseCapture()
{
    m_hWndCapture = 0;
    return TRUE;
}

HWND SConnection::GetCapture() const
{
    return m_hWndCapture;
}

HCURSOR SConnection::SetCursor(HWND hWnd, HCURSOR cursor)
{
    HCURSOR old = 0;
    auto it = m_wndCursor.find(hWnd);
    if (it != m_wndCursor.end())
        old = it->second;
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
    std::unique_lock<CountMutex> lock(m_mutex);
    m_bBlockTimer = bBlock;
}

HWND SConnection::GetActiveWnd() const
{
    return m_hActive;
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    HWND old = m_hActive;
    m_hActive = hWnd;
    return old != 0;
}

HWND SConnection::WindowFromPoint(POINT, HWND hWnd) const
{
    // single-window traversal depth: FreeRTOS windows are virtual objects
    // owned by _Window; hit-test refinement lands with the renderer (TODO).
    return hWnd;
}

BOOL SConnection::IsWindow(HWND) const
{
    return TRUE;
}

void SConnection::SetWindowPos(HWND, int, int) const
{
    // virtual windows live in _Window state; nothing OS-side to move (TODO fb)
}

void SConnection::SetWindowSize(HWND, int, int) const
{
}

BOOL SConnection::MoveWindow(HWND, int, int, int, int) const
{
    return TRUE;
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    if (ppt)
    {
        ppt->x = 0;
        ppt->y = 0;
    }
    return TRUE;
}

int SConnection::GetDpi(BOOL) const
{
    return 96;
}

HWND SConnection::GetForegroundWindow()
{
    return m_hForeground;
}

BOOL SConnection::SetForegroundWindow(HWND hWnd)
{
    m_hForeground = hWnd;
    return TRUE;
}

BOOL SConnection::BringWindowToTop(HWND hWnd)
{
    (void)hWnd;
    return TRUE;
}

BOOL SConnection::SetWindowOpacity(HWND, BYTE)
{
    return TRUE;
}

BOOL SConnection::SetWindowRgn(HWND, HRGN)
{
    return TRUE;
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl)
{
    HKL old = m_hkl;
    m_hkl = hKl;
    return old;
}

HKL SConnection::GetKeyboardLayout(DWORD)
{
    return m_hkl;
}

UINT SConnection::GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    if (lpList && nBuff > 0)
        lpList[0] = m_hkl;
    return 1;
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
    HWND old = m_hFocus;
    m_hFocus = hWnd;
    return old != 0;
}

BOOL SConnection::IsDropTarget(HWND)
{
    return FALSE;
}

BOOL SConnection::FlashWindowEx(PFLASHWINFO info)
{
    (void)info;
    return TRUE;
}

int SConnection::OnGetClassName(HWND, LPSTR lpClassName, int nMaxCount)
{
    if (!lpClassName || nMaxCount <= 0)
        return 0;
    strncpy(lpClassName, "SWINX", nMaxCount - 1);
    lpClassName[nMaxCount - 1] = 0;
    return (int)strlen(lpClassName);
}

BOOL SConnection::OnSetWindowText(HWND, _Window *, LPCSTR lpszString)
{
    (void)lpszString;
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND)
{
    return 0;
}

int SConnection::OnGetWindowTextLengthW(HWND)
{
    return 0;
}

int SConnection::OnGetWindowTextA(HWND, char *buf, int bufLen)
{
    if (buf && bufLen > 0)
        buf[0] = 0;
    return 0;
}

int SConnection::OnGetWindowTextW(HWND, wchar_t *buf, int bufLen)
{
    if (buf && bufLen > 0)
        buf[0] = 0;
    return 0;
}

HWND SConnection::OnFindWindowEx(HWND, HWND, LPCSTR, LPCSTR)
{
    return NULL;
}

BOOL SConnection::OnEnumWindows(HWND, HWND, WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
    (void)lpEnumFunc;
    (void)lParam;
    return TRUE;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags)
{
    if (gaFlags == GA_ROOT || gaFlags == GA_ROOTOWNER)
        return hwnd;
    return NULL;
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

int SConnection::GetMonitorCount() const
{
    return 1;
}

HMONITOR SConnection::GetMonitor(int index) const
{
    return index == 0 ? (HMONITOR)1 : NULL;
}

HMONITOR SConnection::GetPrimaryMonitor() const
{
    return (HMONITOR)1;
}

bool SConnection::GetMonitorRect(HMONITOR, RECT *prc) const
{
    if (prc)
    {
        prc->left = 0;
        prc->top = 0;
        prc->right = m_screenW;
        prc->bottom = m_screenH;
    }
    return true;
}

bool SConnection::GetMonitorWorkRect(HMONITOR hMonitor, RECT *prc) const
{
    return GetMonitorRect(hMonitor, prc);
}

bool SConnection::IsPrimaryMonitor(HMONITOR hMonitor) const
{
    return hMonitor == (HMONITOR)1;
}

int SConnection::GetScreenWidth(HMONITOR) const
{
    return m_screenW;
}

int SConnection::GetScreenHeight(HMONITOR) const
{
    return m_screenH;
}

HWND SConnection::GetScreenWindow() const
{
    return NULL;
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

cairo_surface_t *SConnection::CreateWindowSurface(HWND, uint32_t, int cx, int cy)
{
    // software renderer: every window paints into its own ARGB32 image
    // surface; commitCanvas() blits dirty regions into the framebuffer.
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cx, cy);
}

cairo_surface_t *SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId, int cx, int cy)
{
    if (surface)
        cairo_surface_destroy(surface);
    return CreateWindowSurface(hWnd, visualId, cx, cy);
}

DWORD SConnection::GetWndProcessId(HWND)
{
    return 1;
}

HWND SConnection::WindowFromPoint(POINT)
{
    return NULL;
}

BOOL SConnection::GetClientRect(HWND, RECT *pRc)
{
    if (pRc)
    {
        pRc->left = 0;
        pRc->top = 0;
        pRc->right = m_screenW;
        pRc->bottom = m_screenH;
    }
    return TRUE;
}

void SConnection::SendSysCommand(HWND, int)
{
}

BOOL SConnection::IsWindowVisible(HWND)
{
    return TRUE;
}

HWND SConnection::GetWindow(HWND hWnd, _Window *, UINT)
{
    return hWnd;
}

UINT SConnection::RegisterMessage(LPCSTR lpString)
{
    if (!lpString || !*lpString)
        return 0;
    std::lock_guard<CountMutex> lock(m_mutex);
    swinx_stl::string name(lpString);
    for (auto &pair : m_regMessages)
    {
        if (pair.first == name)
            return pair.second;
    }
    UINT id = m_nextRegMsg++;
    m_regMessages[name] = id;
    return id;
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
    return (HMONITOR)1;
}

void SConnection::updateWindow(HWND hWnd, const RECT &)
{
    // synchronous paint of the current invalid region (same as the X11 port)
    SendMessageA(hWnd, WM_PAINT, 0, 0);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc)
{
    // blit the window canvas region (rc is window-local) into the software
    // framebuffer at the window's screen position, then notify the presenter.
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return;
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return;
    cairo_surface_flush(surface);

    int sw = cairo_image_surface_get_width(surface);
    int sh = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    uint32_t *srcBase = (uint32_t *)cairo_image_surface_get_data(surface);
    if (!srcBase)
        return;

    // clip rc to the window canvas
    RECT rcSrc = rc;
    if (rcSrc.left < 0)
        rcSrc.left = 0;
    if (rcSrc.top < 0)
        rcSrc.top = 0;
    if (rcSrc.right > sw)
        rcSrc.right = sw;
    if (rcSrc.bottom > sh)
        rcSrc.bottom = sh;
    if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
        return;

    // screen-space dirty rect, clipped to the framebuffer
    int ox = wndObj->rc.left, oy = wndObj->rc.top;
    RECT rcFb = { ox + rcSrc.left, oy + rcSrc.top, ox + rcSrc.right, oy + rcSrc.bottom };
    if (rcFb.left < 0)
    {
        rcSrc.left -= rcFb.left;
        rcFb.left = 0;
    }
    if (rcFb.top < 0)
    {
        rcSrc.top -= rcFb.top;
        rcFb.top = 0;
    }
    if (rcFb.right > SWINX_FB_WIDTH)
        rcFb.right = SWINX_FB_WIDTH;
    if (rcFb.bottom > SWINX_FB_HEIGHT)
        rcFb.bottom = SWINX_FB_HEIGHT;
    if (rcFb.left >= rcFb.right || rcFb.top >= rcFb.bottom)
        return;

    int w = rcFb.right - rcFb.left;
    for (int y = 0; y < rcFb.bottom - rcFb.top; ++y)
    {
        memcpy(SwinxFbBits() + (size_t)(rcFb.top + y) * SwinxFbStride() + rcFb.left,
               srcBase + (size_t)(rcSrc.top + y) * stride + rcSrc.left,
               w * sizeof(uint32_t));
    }
    RECT dirty = rcFb;
    swinxFbPresent(&dirty);
}

void SConnection::EnableWindow(HWND, BOOL)
{
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
    if (bShow)
        m_cursorCount++;
    else
        m_cursorCount--;
    return m_cursorCount;
}

UINT SConnection::GetRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize)
{
    if (!puiNumDevices)
        return (UINT)-1;
    if (pRawInputDeviceList && *puiNumDevices < 1)
        return (UINT)-1;
    *puiNumDevices = 0;
    (void)pRawInputDeviceList;
    (void)cbSize;
    return 0;
}

UINT SConnection::GetRawInputDeviceInfoA(HRAWINPUT, UINT, LPVOID, PUINT)
{
    return (UINT)-1;
}

UINT SConnection::GetRawInputDeviceInfoW(HRAWINPUT, UINT, LPVOID, PUINT)
{
    return (UINT)-1;
}

BOOL SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    m_caretInfo.hOwner = hWnd;
    m_caretInfo.hBmp = hBitmap;
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    return TRUE;
}

BOOL SConnection::DestroyCaret()
{
    std::lock_guard<CountMutex> lock(m_mutex);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    return TRUE;
}

BOOL SConnection::ShowCaret(HWND hWnd)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (m_caretInfo.hOwner != hWnd)
        return FALSE;
    m_caretInfo.nVisible++;
    return TRUE;
}

BOOL SConnection::HideCaret(HWND hWnd)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (m_caretInfo.hOwner != hWnd)
        return FALSE;
    if (m_caretInfo.nVisible > 0)
        m_caretInfo.nVisible--;
    return TRUE;
}

BOOL SConnection::SetCaretPos(int X, int Y)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (!m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    return TRUE;
}

BOOL SConnection::GetCaretPos(LPPOINT lpPoint)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (lpPoint)
    {
        lpPoint->x = m_caretInfo.x;
        lpPoint->y = m_caretInfo.y;
    }
    return m_caretInfo.hOwner != 0;
}

const SConnection::CaretInfo *SConnection::GetCaretInfo() const
{
    return &m_caretInfo;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    m_caretBlinkTime = blinkTime;
}

UINT SConnection::GetCaretBlinkTime() const
{
    return m_caretBlinkTime;
}

void SConnection::GetWorkArea(HMONITOR hMonitor, RECT *prc) const
{
    GetMonitorWorkRect(hMonitor, prc);
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

HRESULT SConnection::DoDragDrop(IDataObject *, IDropSource *, DWORD dwOKEffect, DWORD *pdwEffect)
{
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    (void)dwOKEffect;
    return DRAGDROP_S_CANCEL;
}

HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth)
{
    // virtual windows: swinx creates the _Window object itself; the
    // connection only tracks it.  See mobile's ENABLE_VIRTUAL_HWND path.
    (void)cs;
    (void)depth;
    return (HWND)wnd;
}

void SConnection::OnWindowDestroy(HWND, _Window *)
{
}

void SConnection::SetWindowVisible(HWND, _Window *, BOOL, int)
{
}

void SConnection::SetParent(HWND, _Window *, HWND)
{
}

void SConnection::SendExposeEvent(HWND, LPCRECT, BOOL)
{
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

_Window *SConnection::CreateVirtualWindowObject()
{
    // TODO: virtual _Window factory lands with the window glue
    return NULL;
}

//=============================================================================
// SConnMgr
//=============================================================================

SConnMgr::SConnMgr()
    : m_hHeap(NULL)
{
}

SConnMgr::~SConnMgr()
{
    for (auto &pair : m_conns)
        delete pair.second;
}

SConnMgr *SConnMgr::instance()
{
    static SConnMgr *s_mgr = new SConnMgr(); // leak-on-purpose, never destructed
    return s_mgr;
}

SConnection *SConnMgr::getConnection(tid_t tid, int screenNum)
{
    if (tid == 0)
        tid = GetCurrentThreadId();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_conns.find(tid);
    if (it != m_conns.end())
        return it->second;
    SConnection *pConn = new SConnection(screenNum);
    m_conns[tid] = pConn;
    return pConn;
}

HANDLE SConnMgr::getProcessHeap()
{
    return m_hHeap ? m_hHeap : (m_hHeap = GetProcessHeap());
}
