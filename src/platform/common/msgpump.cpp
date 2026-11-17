/*
 * msgpump.cpp -- USER32 消息泵：GetMessage / PeekMessage 及其配套输入查询
 *
 * 为什么单独成文件（2026-11-16 平台层结构调整）：
 * ---------------------------------------------------------------------------
 * 消息泵的 Win32 语义与窗口系统无关：取一条可派发的消息、必要时阻塞等待、
 * 取走后过一遍 WH_GETMESSAGE 钩子、顺手维护 GetLastInputInfo 的输入时间戳，
 * 以及 WinEvent（无障碍 OUTOFCONTEXT 钩子）在泵里派发。各平台真正的差异只有
 * 一处 —— 「队列从哪来」，也就是 src/platform/<平台>/SConnection.cpp 的
 * getMsg/peekMsg/waitMsg。所以泵写在 common 里一份，平台目录只留 SConnection：
 *
 *     posix 家族（linux / macOS / iOS / Android / OHOS / wasm）
 *         SConnection 收到的是 xcb / CoreFoundation / 宿主回调投递的消息
 *     freertos
 *         SConnection 收到的是输入驱动与 app 任务 postMsg 的消息，队列由 FreeRTOS
 *         事件唤醒
 *
 * 这条分层与 mobile（Android/OHOS）复用 src/platform/posix/ 的方式一致：平台
 * 目录回答「差在哪」，common 回答「Win32 是什么语义」。改造前 freertos 有一份
 * 几乎逐字拷贝的泵（旧 src/platform/freertos/sysbridge.cpp:687-763），从而多出
 * 三个行为差异：没有 hWnd 归属线程校验、没有 GetLastInputInfo、drain WinEvent
 * 的位置也各写一套。抽到这里之后两个平台共用同一份，差异只剩 SConnection。
 *
 * WinEvent 的排空点：
 * ---------------------------------------------------------------------------
 * SwinxDispatchPendingWinEvents() 只在取消息**前后**各调一次，不额外插入其它
 * 位置。这与 user32 的时机一致：OUTOFCONTEXT 钩子的回调由消息泵投递，而泵只有
 * 这一个入口。调用方（SOUI 的 SMessageLoop::HandleMsg / AndroidPlatformAPI 的
 * executePendingTask / esp32s3 的 soui_ui_task）不需要知道这件事 —— freertos 的
 * 旧实现在 GetMessage/PeekMessage 里各调两次，同一个语义散在四处，读泵的人无法
 * 判断哪个才是权威时机。
 *
 * 不在这里面实现的东西：
 *   PostThreadMessageA/W      线程 -> 连接的映射属于平台拓扑，留在各平台目录
 *   SetTimer + TIMERPROC      Win32 要求由 peekMsg 内部回调，规则写在 SConnection
 *   GetSystemMetrics          屏幕尺寸来自 SConnection，但取值表与平台无关；目前
 *                             posix 与 freertos 各有一份，取值必须一致（迁移前
 *                             先确认两边的 SM_* 表没有分歧），属下一步可下沉项
 */
#include <windows.h>
#include <atomic>

#include "SConnection.h"
#include "sysapi.h"
#include "hook.h"
#include "wndobj.h"

/* WinEvent 异步派发（实现位于 src/oleacc.cpp，所有平台变体都在编译）：
 * OUTOFCONTEXT 钩子的回调与 user32 一致，在消息泵里投递。 */
extern "C" void WINAPI SwinxDispatchPendingWinEvents(void);

/* GetLastInputInfo 的输入时间戳。
 *
 * Win32 在输入事件到达时就刷新它；swinx 选择在消息泵取出输入类消息时刷新：
 * 对「用户空闲了多久」这个用途二者等价，而且不必给每个平台的 SConnection 各加
 * 一份 idle 追踪。 */
static std::atomic<DWORD> s_lastInputTick(0);

static bool IsInputMessage(UINT msg)
{
    if (msg >= 0x0100 && msg <= 0x0109) // WM_KEYFIRST..WM_KEYLAST
        return true;
    if (msg >= 0x00A0 && msg <= 0x00AD) // WM_NCMOUSEMOVE..WM_NCXBUTTONDBLCLK
        return true;
    if (msg >= 0x0200 && msg <= 0x020E) // WM_MOUSEFIRST..WM_MOUSELAST
        return true;
    if (msg == 0x00FF) // WM_INPUT（原始输入）
        return true;
    return false;
}

static void RefreshLastInputTick(UINT msg)
{
    if (IsInputMessage(msg))
        s_lastInputTick.store(GetTickCount());
}

BOOL WINAPI GetLastInputInfo(PLASTINPUTINFO plii)
{
    if (!plii || plii->cbSize != sizeof(LASTINPUTINFO))
        return FALSE;
    DWORD tick = s_lastInputTick.load();
    if (tick == 0)
        tick = GetTickCount(); // 还没有收到过输入：按「刚有输入」处理
    plii->dwTime = tick;
    return TRUE;
}

BOOL WaitMessage()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMsg();
}

DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMutliObjectAndMsg(pHandles, nCount, dwMilliseconds, fWaitAll, dwWakeMask);
}

DWORD WINAPI GetQueueStatus(UINT flags)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return 0;
    return conn->GetQueueStatus(flags);
}

BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    SwinxDispatchPendingWinEvents();
    if (hWnd)
    {
        /* Win32: hWnd 必须属于调用线程，否则消息拿不到。swinx 的窗口对象记录
         * 创建线程（WndObj::tid），这里照同样口径拒绝 —— 少了这一步，跨线程
         * GetMessage(某个前台窗口) 会偷走不属于本线程的消息。 */
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
        {
            return FALSE;
        }
        if (wndObj->tid != GetCurrentThreadId())
        {
            return FALSE;
        }
    }
    SConnection *conn = SConnMgr::instance()->getConnection();
    BOOL bRet = conn->getMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    SwinxDispatchPendingWinEvents();
    if (bRet)
    {
        if (lpMsg)
            RefreshLastInputTick(lpMsg->message);
        if (CallHook(WH_GETMESSAGE, HC_ACTION, 1, (LPARAM)lpMsg))
            return GetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }
    return bRet;
}

BOOL PeekMessage(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    SwinxDispatchPendingWinEvents();
    if (hWnd)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (!wndObj)
        {
            return FALSE;
        }
        if (wndObj->tid != GetCurrentThreadId())
        {
            return FALSE;
        }
    }
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    BOOL bRet = conn->peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    SwinxDispatchPendingWinEvents();
    if (bRet)
    {
        // 只有真正把消息取走（PM_REMOVE）才算「消费了这次输入」
        if (pMsg && (wRemoveMsg & PM_REMOVE))
            RefreshLastInputTick(pMsg->message);
        if (CallHook(WH_GETMESSAGE, HC_ACTION, wRemoveMsg & PM_REMOVE, (LPARAM)pMsg))
            return PeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }
    return bRet;
}

BOOL CallMsgFilter(LPMSG lpMsg, int nCode)
{
    if (CallHook(WH_SYSMSGFILTER, nCode, 0, (LPARAM)lpMsg))
        return TRUE;
    return CallHook(WH_MSGFILTER, nCode, 0, (LPARAM)lpMsg);
}

/* ---- 键盘状态查询：全部转发到 SConnection（各平台的键盘模型在那边） ------- */

BOOL GetKeyboardState(PBYTE lpKeyState)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyboardState(lpKeyState);
}

SHORT GetKeyState(int nVirtKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyState(nVirtKey);
}

SHORT WINAPI GetAsyncKeyState(int vKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetAsyncKeyState(vKey);
}

UINT MapVirtualKey(UINT uCode, UINT uMapType)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->MapVirtualKey(uCode, uMapType);
}

UINT MapVirtualKeyEx(UINT uCode, UINT uMapType, HKL dwhkl __attribute__((unused)))
{
    // todo:hjx
    return MapVirtualKey(uCode, uMapType);
}
