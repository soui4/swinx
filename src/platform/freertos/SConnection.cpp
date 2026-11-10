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
#include <algorithm>
#include <functional>
#include <sfr_config.h>
#if !SFR_KERNEL_REAL
#include <sched.h>  /* 主机 emu / passthrough 没有 FreeRTOS 内核，让出宿主时间片用 */
#endif

#define kLogTag "SConnection"

// Plain-value snapshot of the window registry (FreeRTOS-only: GetWindow needs
// to navigate parent / child / sibling without locking every other window).  See
// WndMgr::enumerate().
namespace {
struct WndBasicInfo
{
    HWND hwnd;
    HWND parent;
    HWND owner;
    BOOL bDestroyed;
};
} // namespace

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
    std::string name(pszName);
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

void SConnection::SetKeyDown(UINT vk, bool down)
{
    if (vk >= 256)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    if (down)
        m_keyboardState[vk] |= 0x80;
    else
        m_keyboardState[vk] &= (BYTE)~0x80;
}

// flip the toggle (low) bit of a lock key; the 0x80 transition bit belongs to
// SetKeyDown, so the two never fight over the same byte.  GetKeyState returns
// that bit as its low bit, which is what the WM_CHAR translator reads to decide
// the case of a letter.
void SConnection::ToggleKeyState(UINT vk)
{
    if (vk >= 256)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    m_keyboardState[vk] ^= 0x01;
}

void SConnection::SetCursorPos(int x, int y)
{
    // bare metal: the input driver is the single writer; 32-bit aligned
    // stores are atomic on ARM, no lock taken here
    m_curX = x;
    m_curY = y;
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
    {
        std::unique_lock<CountMutex> lock(m_mutex);
        if (!m_msgQueue.empty())
            return true;
    }

    uint64_t start = GetTickCount();
    for (;;)
    {
        // 上界每轮重算：取"剩余用户超时"和"下一个计时器到期"的较小者。原实现只在进
        // 循环前算一次，超时后把上界换成剩余用户超时，就再也看不到计时器了 ——
        // timeOut=INFINITE 时等于最长睡 0x7fffffff ms，getMsg 正是靠这一条把 50ms 的
        // 脚本节拍叫醒，睡过头就又回到"计时器不回调"的老症状。
        uint64_t elapsed = GetTickCount() - start;
        uint64_t waitMs = elapsed >= (uint64_t)timeOut ? 0 : (uint64_t)timeOut - elapsed;
        if (!m_bBlockTimer)
        {
            uint64_t now = GetTickCount();
            std::unique_lock<CountMutex> lock(m_mutex);
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

        // 放锁之后再 WaitForSingleObject：持锁睡等会让别的线程 postMsg 进不来。
        WaitForSingleObject(m_hQueueEvt, (DWORD)waitMs);
        // timers are driven inside waitMsg: a due timer enqueues WM_TIMER
        // and sets the queue event, waking this very loop
        updateMsgQueue(0);
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return true;
        }
        // spurious wake or timeout
        if (GetTickCount() - start >= (uint64_t)timeOut)
            return false;
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
#if SFR_KERNEL_REAL
        taskYIELD();
#else
        sched_yield();
#endif
    }
    // not reached
    (void)dwWaitMask;
}

// VK -> the character TranslateMessage should hand out as WM_CHAR, for a US
// layout (this port has no keyboard layout table: HKL is stored, never used).
// Mirrors platform/wasm/SConnection.cpp; without it the port posted the raw VK
// as the character, which breaks every non-trivial key:
//   * VK_A..VK_Z carry the *upper-case* code, so typing always produced capitals
//     and SHIFT did nothing (wm's own name lookups got worse, not better);
//   * VK_BACK/VK_RETURN/VK_TAB produced no WM_CHAR at all, so an edit control
//     could not delete or start a new line;
//   * VK_UP/DOWN (0x26/0x28) fell into the "printable" window 0x20..0x7e and
//     were posted as '&'/'(' characters.
static WORD vkToChar(UINT vk, bool bShift, bool bCapsLock)
{
    switch (vk)
    {
    case VK_BACK:     return 0x08;
    case VK_TAB:      return 0x09;
    case VK_RETURN:   return 0x0D;
    case VK_ESCAPE:   return 0x1B;
    case VK_SPACE:    return ' ';
    case VK_MULTIPLY: return '*';
    case VK_ADD:      return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL:  return '.';
    case VK_DIVIDE:   return '/';
    default: break;
    }

    // VK_A..VK_Z share the ASCII values of 'A'..'Z'
    if (vk >= 'A' && vk <= 'Z')
        return (WORD)((bShift != bCapsLock) ? vk : vk - ('A' - 'a'));

    // VK_0..VK_9 likewise; Shift gives the US-layout upper row
    if (vk >= '0' && vk <= '9')
    {
        static const char s_shiftDigits[] = ")!@#$%^&*(";
        return (WORD)(bShift ? s_shiftDigits[vk - '0'] : vk);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (WORD)('0' + (vk - VK_NUMPAD0));

    // US layout symbol keys: { vk, plain, shifted }
    struct SymPair { UINT vk; char plain; char shifted; };
    static const SymPair s_pairs[] = {
        { VK_OEM_1,     ';', ':'  },
        { VK_OEM_PLUS,  '=', '+'  },
        { VK_OEM_COMMA, ',', '<'  },
        { VK_OEM_MINUS, '-', '_'  },
        { VK_OEM_PERIOD,'.', '>'  },
        { VK_OEM_2,     '/', '?'  },
        { VK_OEM_3,     '`', '~'  },
        { VK_OEM_4,     '[', '{'  },
        { VK_OEM_5,     '\\', '|' },
        { VK_OEM_6,     ']', '}'  },
        { VK_OEM_7,     '\'', '"' },
    };
    for (unsigned i = 0; i < sizeof(s_pairs) / sizeof(s_pairs[0]); ++i)
    {
        if (vk == s_pairs[i].vk)
            return (WORD)(bShift ? s_pairs[i].shifted : s_pairs[i].plain);
    }
    return 0;
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    // FreeRTOS port: no IME, no dead keys -- one VK becomes at most one char.
    if (pMsg && (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN))
    {
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool bCapsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        WORD ch = vkToChar((UINT)pMsg->wParam, bShift, bCapsLock);
        if (ch)
        {
            if (bCtrl && !bAlt && ch >= ' ')
            {
                // Win32 turns Ctrl+<printable> into the matching control code
                // (vk & 0x1F) instead of the literal character, which is what
                // lets Ctrl+A/C/V reach the edit control's accelerators
                // instead of inserting 'a'/'c'/'v'.
                ch = (WORD)((UINT)pMsg->wParam & 0x1F);
            }
            // Alt produces WM_SYSCHAR, not WM_CHAR
            postMsg(pMsg->hwnd, bAlt ? WM_SYSCHAR : WM_CHAR, ch, pMsg->lParam);
            return TRUE;
        }
    }
    return FALSE;
}

static bool isSameTimerMsg(const Msg *msg, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    return msg && msg->message == WM_TIMER && msg->hwnd == hWnd && msg->wParam == (WPARAM)id && msg->lParam == (LPARAM)proc;
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

BOOL SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    // 外层循环只为了承载"带 TIMERPROC 的 WM_TIMER 就地回调，然后重新取一条"这一步。
    // 同一语义在 src/platform/wasm/SConnection.cpp 里写成 peekMsg 递归，这里用循环：
    // FreeRTOS 任务栈常只有几 KB，递归深度会跟着"同一拍里到期的计时器个数"涨，
    // 钉死在一层栈框更稳妥。
    for (;;)
    {
        // Win32 generates WM_TIMER for expired timers from inside PeekMessage too,
        // so a PeekMessage-only pump (the simulator) gets timer ticks.  Called
        // before taking m_mutex: updateMsgQueue() locks it as well.
        updateMsgQueue(0);

        std::unique_lock<CountMutex> lock(m_mutex);
        // Win32 semantics: (0,0) means "no filter"
        bool bNoFilter = (wMsgFilterMin == 0 && wMsgFilterMax == 0);
        bool bTimerFired = false;
        HWND hTimer = NULL;
        WPARAM wpTimer = 0;
        DWORD tTimer = 0;
        TIMERPROC procTimer = NULL;
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
            if (msg->message == WM_TIMER && msg->lParam)
            {
                // 带 TIMERPROC 的 SetTimer：Win32 由 PeekMessage 内部直接回调，不把
                // WM_TIMER 交给 DispatchMessage。这类消息的 hwnd 恒为 NULL（SOUI 的
                // STimerGenerator::SetTimer 就是 ::SetTimer(NULL, 0, elapse, _TimerProc)），
                // 而 DispatchMessage 见 pMsg->hwnd==NULL 直接返回 FALSE —— 队列抽干、
                // 计数照涨，回调却一次都不跑，脚本层的 ITimer（SOUI 的 STimer、
                // 动画与游戏逻辑的节拍）于是静默失效。语义与
                // src/platform/linux|cocoa|ios|wasm 的 peekMsg 同名分支一致。
                hTimer = msg->hwnd;
                wpTimer = msg->wParam;
                tTimer = msg->time;
                procTimer = (TIMERPROC)msg->lParam;
                m_msgQueue.erase(it);
                delete msg;
                bTimerFired = true;
                break;
            }
            memcpy((void *)pMsg, (MSG *)msg, sizeof(MSG));
            if (wRemoveMsg & PM_REMOVE)
            {
                m_msgQueue.erase(it);
                delete msg;
            }
            return TRUE;
        }
        if (!bTimerFired)
            return FALSE;
        // 放锁再回调：回调里会 SetTimer/KillTimer/postMsg（同一把 m_mutex，递归锁
        // 同线程不会自锁，但还可能 SendMessage 等别的线程回包），持锁调用等于自己
        // 堵住那条回包路径。
        lock.unlock();
        procTimer(hTimer, WM_TIMER, wpTimer, tTimer);
        lock.lock();
        // 回调可能又投递了新消息（包括重新排定的计时器），回到循环顶部重扫
    }
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    // GetMessage 与 PeekMessage 的差别就在"等"：拿不到一条可派发的消息就一直睡到
    // 有消息投递或下一个计时器到期，所以不能只委托 peekMsg 就返回（模态消息盒
    // src/cmnctl32/msgbox.cpp:217 的 while(GetMessage(...)) 靠的就是这个"睡"，
    // 不睡就是空转烧 CPU）。睡醒仍回 peekMsg：带 TIMERPROC 的 WM_TIMER 在 peek 内部
    // 就回调掉了，不占返回的那一次机会，所以计时器不会因为这里在睡而丢拍。
    // 队列规则与过滤条件仍然只有 peekMsg 一处实现（这里再扫一遍队列会让两条入口
    // 分叉，带回调的计时器就会从其中一条里静默消失）。
    for (;;)
    {
        if (peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE))
        {
            if (lpMsg->message == WM_QUIT)
            {
                // Win32 的 GetMessage 收到 WM_QUIT 返回 FALSE（退出码留在 wParam 里），
                // 而不是把它交给 DispatchMessage；调用方的 while(GetMessage(...)) 就靠
                // 这一次 FALSE 退出。与 src/platform/linux/SConnection.cpp:1077 同口径。
                return FALSE;
            }
            return TRUE;
        }
        bool bQueueEmpty;
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            bQueueEmpty = m_msgQueue.empty();
        }
        if (bQueueEmpty)
        {
            // 睡到有新投递（postMsg 置 m_hQueueEvt）或下一个计时器到期（waitMsg 内部
            // 按 next-due 收窄上界，见本文件 waitMsg）。
            waitMsg();
        }
        else
        {
            // 队列非空却没有一条落在过滤范围里：waitMsg 的"队列非空即返回 true"在这里
            // 会变成死转，所以让出一个短时间片再试。
            Sleep(10);
        }
    }
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

BOOL SConnection::IsWindow(HWND hWnd) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    return wndObj ? !wndObj->bDestroyed : FALSE;
}

void SConnection::SetWindowPos(HWND hWnd, int, int) const
{
    // 核心在这里之后才写 _Window::rc（WM_MOVE 里 OffsetRect），所以此刻 hWnd
    // 的 rc 还是旧矩形。旧矩形上的像素现在归下面的窗口：跳过 hWnd 重排一遍，
    // 把这块还回去，且不要把它的旧内容按旧位置又贴上来。它的新位置由随后的
    // WM_SIZE/InvalidateRect 触发的重绘负责。
    composeScreen(hWnd);
}

void SConnection::SetWindowSize(HWND, int, int) const
{
    // 同 SetWindowPos：rc 的 right/bottom 要等 WM_SIZE 才更新（画布也是那时才
    // resize），这里按旧矩形把右侧/下侧让出去。
    composeScreen();
}

BOOL SConnection::MoveWindow(HWND, int, int, int, int) const
{
    return TRUE;
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    if (ppt)
    {
        ppt->x = m_curX;
        ppt->y = m_curY;
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

void SConnection::SetZOrder(HWND hWnd, _Window *, HWND hWndInsertAfter)
{
    // m_zTop 是唯一记录"谁盖着谁"的地方（合成顺序），所以 z 序操作必须同步
    // 到这里。这个端口只有创建序，没有真正的窗口栈：HWND_TOP/HWND_TOPMOST 挪到
    // 末尾，HWND_BOTTOM 挪到开头，指定窗口则插到它后面。
    std::vector<HWND>::iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it == m_zTop.end())
        return;
    m_zTop.erase(it);
    if (hWndInsertAfter == HWND_BOTTOM)
    {
        m_zTop.insert(m_zTop.begin(), hWnd);
    }
    else if (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == NULL)
    {
        m_zTop.push_back(hWnd);
    }
    else
    {
        std::vector<HWND>::iterator pos = std::find(m_zTop.begin(), m_zTop.end(), hWndInsertAfter);
        if (pos == m_zTop.end())
            m_zTop.push_back(hWnd);
        else
            m_zTop.insert(pos + 1, hWnd);
    }
    // 遮挡关系变了：屏幕上已有的像素按旧 z 序叠的，重排一次
    composeScreen();
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

BOOL SConnection::IsWindowVisible(HWND hWnd) const
{
    // Win32 semantics: WS_VISIBLE on the window AND on every ancestor.
    // (This port used to answer TRUE unconditionally, which silently disabled
    // WM_SHOWWINDOW transitions and every "is it worth painting" check.)
    HWND h = hWnd;
    while (h)
    {
        WndObj wndObj = WndMgr::fromHwnd(h);
        if (!wndObj || (wndObj->dwStyle & WS_VISIBLE) == 0)
            return FALSE;
        HWND hParent = wndObj->parent;
        if (hParent == h)
            break;
        h = hParent;
    }
    return hWnd != NULL;
}

/* Window navigation.
 *
 * This port has no OS window manager: an HWND is a swinx _Window tracked in
 * WndMgr, so parent/owner relations only exist in _Window.  The query used to
 * return hWnd unconditionally, which is not "no relative window" -- the shared
 * WM_DESTROY path in wnd.cpp walks the child list with
 *
 *     hChild = GetWindow(hWnd, GW_CHILDLAST);
 *     while (hChild) { ... DestroyWindow(hChild); hChild = GetWindow(...); }
 *
 * so it kept getting the window being destroyed and re-entered DestroyWindow()
 * from inside that window's own WM_DESTROY, recursing until the window locks
 * dead-locked -- the app froze on close.  GW_HWNDNEXT has the same contract
 * (GetDlgItem(), the WM_PRINT children walk): it must return 0 at the end of
 * the list instead of repeating the last window forever.
 *
 * Sibling order is by HWND value.  There is no z-order on this port (windows
 * are virtual), and the callers that matter only need "iterate all of them
 * exactly once, then stop".
 */
HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd)
{
    if (!wndObj)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (uCmd == GW_OWNER)
        return wndObj->owner;
    if (uCmd == GW_PARENT)
        return wndObj->parent;

    const HWND hParent = wndObj->parent;
    std::vector<WndBasicInfo> all;
    WndMgr::enumerate([&](HWND hwnd, _Window *wnd) {
        all.push_back({hwnd, wnd->parent, wnd->owner, wnd->bDestroyed});
    });

    // windows sharing hWnd's parent (hWnd itself included, at its sorted place)
    std::vector<HWND> sibs;
    std::vector<HWND> children;
    sibs.reserve(all.size());
    for (size_t i = 0; i < all.size(); i++)
    {
        const WndBasicInfo &info = all[i];
        // a window already marked destroyed is on its way out of the registry:
        // skipping it here is what makes the destroy loops terminate
        if (info.hwnd != hWnd && info.bDestroyed)
            continue;
        if (info.parent == hParent)
            sibs.push_back(info.hwnd);
        if (info.parent == hWnd)
            children.push_back(info.hwnd);
    }
    std::sort(sibs.begin(), sibs.end());
    std::sort(children.begin(), children.end());

    switch (uCmd)
    {
    case GW_CHILD: // == GW_CHILDFIRST
        return children.empty() ? 0 : children.front();
    case GW_CHILDLAST:
        return children.empty() ? 0 : children.back();
    case GW_HWNDFIRST:
        return sibs.empty() ? 0 : sibs.front();
    case GW_HWNDLAST:
        return sibs.empty() ? 0 : sibs.back();
    case GW_HWNDNEXT:
    {
        std::vector<HWND>::const_iterator it = std::find(sibs.begin(), sibs.end(), hWnd);
        if (it == sibs.end())
            return 0;
        ++it;
        return it == sibs.end() ? 0 : *it;
    }
    case GW_HWNDPREV:
    {
        std::vector<HWND>::const_iterator it = std::find(sibs.begin(), sibs.end(), hWnd);
        if (it == sibs.end() || it == sibs.begin())
            return 0;
        --it;
        return *it;
    }
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
}

UINT SConnection::RegisterMessage(LPCSTR lpString)
{
    if (!lpString || !*lpString)
        return 0;
    std::lock_guard<CountMutex> lock(m_mutex);
    std::string name(lpString);
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

bool SConnection::blitCanvas(HWND hWnd, const RECT &rcLocal) const
{
    // blit the window canvas region (rcLocal is window-local) into the software
    // framebuffer at the window's screen position, then notify the presenter.
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed)
        return false;

    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return false;
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return false;
    cairo_surface_flush(surface);

    int sw = cairo_image_surface_get_width(surface);
    int sh = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    uint32_t *srcBase = (uint32_t *)cairo_image_surface_get_data(surface);
    if (!srcBase)
        return false;

    // clip rc to the window canvas
    RECT rcSrc = rcLocal;
    if (rcSrc.left < 0)
        rcSrc.left = 0;
    if (rcSrc.top < 0)
        rcSrc.top = 0;
    if (rcSrc.right > sw)
        rcSrc.right = sw;
    if (rcSrc.bottom > sh)
        rcSrc.bottom = sh;
    if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
        return false;

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
        return false;

    int w = rcFb.right - rcFb.left;
    for (int y = 0; y < rcFb.bottom - rcFb.top; ++y)
    {
        memcpy(SwinxFbBits() + (size_t)(rcFb.top + y) * SwinxFbStride() + rcFb.left,
               srcBase + (size_t)(rcSrc.top + y) * stride + rcSrc.left,
               w * sizeof(uint32_t));
    }
    RECT dirty = rcFb;
    swinxFbPresent(&dirty);
    return true;
}

void SConnection::composeAbove(HWND hWnd, const RECT &rcScreen) const
{
    // framebuffer 没有合成器：谁最后 blit 谁的地盘就归谁。于是"下层窗口重绘"
    // 会把上层窗口（下拉列表这类独立窗口）的像素擦掉，必须在同一帧里按 z 序
    // 把上层重新 blit 回来——注意源是上层窗口自己的画布，它保留着最近一帧，
    // 不需要（也不能）给它发 WM_PAINT。
    std::vector<HWND>::const_iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it == m_zTop.end())
        return; // 不在 z 序里：没有它盖着谁的问题
    for (++it; it != m_zTop.end(); ++it)
    {
        if (!IsWindowVisible(*it))
            continue;
        if (m_committed.find(*it) == m_committed.end())
            continue;
        WndObj above = WndMgr::fromHwnd(*it);
        if (!above || above->bDestroyed)
            continue;
        RECT rcInter = { 0, 0, 0, 0 };
        if (!IntersectRect(&rcInter, &rcScreen, &above->rc))
            continue;
        RECT rcLocal = { rcInter.left - above->rc.left, rcInter.top - above->rc.top,
                         rcInter.right - above->rc.left, rcInter.bottom - above->rc.top };
        blitCanvas(*it, rcLocal);
    }
}

void SConnection::composeScreen(HWND hSkip) const
{
    // 自底向顶把每个窗口的画布整幅 blit 一遍。上面某个窗口（弹出列表）被销毁/
    // 隐藏/挪走之后，它原来的位置属于下面那些窗口，而它们既没有收到 WM_PAINT、
    // 也不需要重画——画布里就是它最后的内容。
    RECT rcFull = { 0, 0, 0x7FFFFFFF, 0x7FFFFFFF };
    for (size_t i = 0; i < m_zTop.size(); ++i)
    {
        if (hSkip && m_zTop[i] == hSkip)
            continue;
        if (!IsWindowVisible(m_zTop[i]))
            continue;
        if (m_committed.find(m_zTop[i]) == m_committed.end())
            continue;
        blitCanvas(m_zTop[i], rcFull);
    }
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;

    // `rc` comes from the DC's clip box (ReleaseDC -> GetClipBox), and that box
    // is *not* the dirty rect: SOUI's paint path saves/restores DC state, so
    // ReleaseDC's RestoreDC(-1) has already reset the clip to the whole
    // surface by the time the box is read.  The accumulated invalid region is
    // the authoritative dirty rect -- pushing the whole canvas instead would
    // repaint every overlapping window with stale pixels (composeAbove() has to
    // undo that one window at a time below).
    RECT rcDirty = rc;
    if (wndObj->invalid.hRgn)
    {
        RECT rb;
        RECT rcClip;
        if (GetRgnBox(wndObj->invalid.hRgn, &rb) != NULLREGION && IntersectRect(&rcClip, &rc, &rb))
            rcDirty = rcClip;
    }

    if (!blitCanvas(hWnd, rcDirty))
        return;
    // 从这一帧起该窗口的画布可信，可以参与整屏合成
    m_committed.insert(hWnd);

    RECT rcScreen = { wndObj->rc.left + rcDirty.left, wndObj->rc.top + rcDirty.top,
                      wndObj->rc.left + rcDirty.right, wndObj->rc.top + rcDirty.bottom };
    composeAbove(hWnd, rcScreen);
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
    // z 序 = 创建序：宿主窗先建在底，弹出窗（SDropDownWnd 等）后建在顶。
    // 画布还没建、没画过，所以此刻不进 m_committed（见 composeScreen）。
    if (wnd)
        m_zTop.push_back((HWND)wnd);
    return (HWND)wnd;
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *)
{
    // Win32 语义：销毁 capture 窗口时自动释放 capture，否则鼠标路由永远指向
    // 已死的窗口（下拉窗是 capture 持有者，必须先清掉）。
    if (m_hWndCapture == hWnd)
        ReleaseCapture();
    std::vector<HWND>::iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it != m_zTop.end())
        m_zTop.erase(it);
    m_committed.erase(hWnd);
    // 弹出窗销毁会曝露它盖住的那块区域：下面的窗口收不到 WM_PAINT（它们的
    // 内容没变），只能按各自的画布整屏重排一遍，把这块补回来。
    composeScreen();
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, BOOL bVisible, int)
{
    if (!wndObj)
    {
        WndObj tmp = WndMgr::fromHwnd(hWnd);
        wndObj = tmp.data();
    }
    if (!wndObj)
        return;

    if (bVisible)
    {
        if (wndObj->dwStyle & WS_VISIBLE)
            return; // already visible
        wndObj->dwStyle |= WS_VISIBLE;
        // A window that just became visible needs one full repaint: it was
        // skipped by SendExposeEvent() while hidden.
        InvalidateRect(hWnd, NULL, TRUE);
        // 显示这一侧不需要重排：它的画布还没被画过（m_committed 里没有），
        // 重排也只会跳过它；等它自己的首帧 WM_PAINT 上屏即可。
    }
    else
    {
        if ((wndObj->dwStyle & WS_VISIBLE) == 0)
            return; // already hidden
        wndObj->dwStyle &= ~WS_VISIBLE;
        // 隐藏等价于"从画面上拿走一层"：它盖住的区域同样没有 WM_PAINT 的机会
        composeScreen();
    }
}

void SConnection::SetParent(HWND, _Window *, HWND)
{
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc, BOOL bForce)
{
    // Windows semantics: InvalidateRect() only *marks* the region and asks for
    // a later WM_PAINT; nothing paints synchronously here.
    //
    // Without this, SOUI's whole Invalidate()/ModifyState() path is a no-op on
    // this port: the first frame appears via the explicit UpdateWindow() at
    // startup and every later change (button hover/push state, animation,
    // caret, resize) never reaches the framebuffer.
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed)
        return;
    if (!IsWindowVisible(hWnd))
        return;

    // The invalid region is already accumulated in wndObj->invalid by
    // InvalidateRect(); rc is only a hint, so it is not needed to paint.
    if (bForce)
    { // synchronous paint, same path as UpdateWindow()
        if (wndObj->nPainting)
            return; // never re-enter the painter
        RECT rcBox;
        GetRgnBox(wndObj->invalid.hRgn, &rcBox);
        if (IsRectEmpty(&rcBox))
            return;
        updateWindow(hWnd, rcBox);
        return;
    }

    // Asynchronous: one pending WM_PAINT per window (postMsg coalesces), so a
    // burst of invalidations during one frame costs exactly one repaint.
    postMsg(hWnd, WM_PAINT, 0, 0);
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
