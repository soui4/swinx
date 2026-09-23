/*
 * Microsoft Active Accessibility (MSAA) — swinx 实现
 *
 * Windows 上 oleacc.dll 的 LresultFromObject 返回的是由 COM 管理的跨进程
 * 引用句柄，客户端进程用 ObjectFromLresult 解析回接口指针。swinx 是进程内
 * Win32 兼容层，这里用一张内部句柄表实现同样的往返语义：
 *
 *   WM_GETOBJECT  ->  服务端 LresultFromObject(IID_IAccessible, wParam, pAcc)
 *                 ->  句柄作为 LRESULT 返回
 *   AccessibleObjectFromWindow()
 *                 ->  SendMessage(hwnd, WM_GETOBJECT, 0, OBJID_xxx)
 *                 ->  ObjectFromLresult() 解析回 IAccessible
 *
 * 与 SOUI 的 SHostWnd::OnGetObject 正好闭环，因此进程内的 MSAA 查询
 * （屏幕阅读器、自动化测试脚本）可以正常工作。
 *
 * 所有权的约定：**swinx 不持有 IAccessible**。SHostWnd::OnGetObject 在响应
 * WM_GETOBJECT 时按需取对象（SOUI_ENABLE_ACC 关闭时直接返回 0），swinx 只把
 * 它"弱登记"进句柄表用于同一次同步往返的解析，不 AddRef、不延长生命周期。
 * 与 Win32 一致，每次 LresultFromObject 产生一个新句柄（不做对象级复用）；
 * 句柄表按容量上限淘汰最旧项，弱登记意味着死对象的陈旧句柄自然失效。
 * 平台胶水层（cocoa/SNsAccessibility.mm、linux/SAtSpi.cpp）每次属性访问都
 * 重新走 AccessibleObjectFromWindow 现查现用现放，绝不缓存接口指针。
 *
 * 事件转发走标准 SetWinEventHook/UnhookWinEvent（声明在 winuser.h，语义与
 * user32 一致）：NotifyWinEvent 只把事件投递到内部队列，钩子回调在消息泵
 * （GetMessage/PeekMessage）里异步派发——与 user32 的 OUTOFCONTEXT 钩子
 * 行为一致；SKIPOWNPROCESS/SKIPOWNTHREAD 钩子注册成功但收不到本进程/
 * 注册线程的事件。平台桥（Linux 的 AT-SPI / macOS 的 NSAccessibility 桥）注册后
 * 随 UI 线程的消息循环收到事件。共享的"对象路径按需解析"助手
 * SwinxAccResolvePath 声明在 src/SwinxAccGlue.h（内部头，非公共 API 面）。
 *
 * AccessibleObjectFromEvent 与 Win32 同路径实现：经
 * AccessibleObjectFromWindow（SendMessage WM_GETOBJECT）解析 (hwnd, idObject)
 * 处的对象，pvarChild 返回事件携带的 child id。
 */
#include <windows.h>
#include <oleacc.h>
#include "SwinxAccGlue.h"
#include <string.h>
#include <wchar.h>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__) && !defined(OHOS)
/* Linux/桌面 的 AT-SPI 桥接入口（swinx/src/platform/linux/SAtSpi.cpp）。
 * 主初始化点在 SConnMgr 构造函数（UI 线程、早于任何 WM_GETOBJECT）；这里是
 * 惰性兜底，覆盖不经 SConnMgr 的调用路径（如单元测试直接调 MSAA API）。
 * 用弱符号引用：某些同样定义 __linux__ 的平台（如 OpenHarmony）走 mobile.cmake、
 * 不会编译 SAtSpi.cpp，弱符号能保证链接仍然通过；SOUI_ENABLE_ACC 关闭时
 * 桥完全不启动（不开 D-Bus 连接、不占 timer）。 */
extern "C" void SwinxAtSpiInit(void) __attribute__((weak));
#define SWINX_HAVE_ATSPI 1
#endif

namespace
{
#if defined(SWINX_HAVE_ATSPI) && defined(SOUI_ENABLE_ACC)
/* 保证只初始化一次 */
std::once_flag &AtSpiOnceFlag()
{
    static std::once_flag flag;
    return flag;
}

void EnsureAtSpiInit()
{
    std::call_once(AtSpiOnceFlag(), []() {
        if (SwinxAtSpiInit != NULL) // 弱符号未定义时地址为 0
            SwinxAtSpiInit();
    });
}
#endif
/* 句柄表：WM_GETOBJECT 应答的"运载"结构。
 * 弱登记——只记 (句柄 → {对象指针, wParam}) 映射，不 AddRef：对象的真正
 * 所有者是 SOUI（SWindow::m_pAcc），swinx 不能替它续命，否则控件销毁后
 * 表里留下的是内部指针已悬垂的活尸体。与真实 oleacc 语义一致（实测）：
 *  - 每次调用产生新句柄；表设容量上限（OLEACC_HANDLE_CAP），超限淘汰
 *    最旧句柄；
 *  - 句柄绑定 WM_GETOBJECT 的 wParam，ObjectFromLresult 时 wParam 不
 *    匹配视为无效句柄（服务端必须原样回传 wParam，真实 oleacc 如此）。
 * 已知口径差异：真实 oleacc 中 wp=0 登记的句柄可脱离消息往返持久解析，
 * wp!=0 的句柄与应答往返绑定（往返结束后解析失败）；swinx 统一按
 * (句柄, wParam) 匹配解析，不追踪消息往返状态（进程内使用无影响）。
 * 弱表语义使死对象的陈旧句柄自然失效。 */
#define OLEACC_HANDLE_CAP 256

class SAccHandleTable {
  public:
    LONG Acquire(IUnknown *pUnk, WPARAM wParam)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        LONG handle = m_next++;
        if (m_next <= 0)
            m_next = 1;
        Entry e;
        e.pUnk = pUnk;
        e.wParam = wParam;
        m_byHandle[handle] = e;
        while (m_byHandle.size() > OLEACC_HANDLE_CAP)
            m_byHandle.erase(m_byHandle.begin()); /* 淘汰最旧句柄 */
        return handle;
    }

    IUnknown *Lookup(LONG handle, WPARAM wParam)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        swinx_stl::map<LONG, Entry>::iterator it = m_byHandle.find(handle);
        if (it == m_byHandle.end())
            return NULL;
        if (it->second.wParam != wParam)
            return NULL; /* wParam 不匹配：与真实 oleacc 一致按无效处理 */
        return it->second.pUnk;
    }

    /* 弱表没有撤销操作：与 Windows 一致，不存在"撤销 LRESULT"的 API，
     * 表不持有对象引用，也就没有需要回收的资源。 */

  private:
    struct Entry
    {
        IUnknown *pUnk;
        WPARAM wParam;
    };
    std::mutex m_mutex;
    LONG m_next = 1;
    swinx_stl::map<LONG, Entry> m_byHandle;
};

SAccHandleTable &AccTable()
{
    static SAccHandleTable tbl;
    return tbl;
}

/* WinEvent 钩子注册表（SetWinEventHook/UnhookWinEvent 的后端）。
 * 语义对齐 user32：支持多个并发钩子，按 [eventMin, eventMax] 区间和
 * idProcess/idThread 过滤。SKIPOWNPROCESS/SKIPOWNTHREAD 钩子注册成功
 * （与 Win32 一致返回有效句柄），分别收不到本进程/注册线程产生的事件
 * ——swinx 是进程内兼容层，所有事件都来自本进程，SKIPOWNPROCESS 钩子
 * 因此永远收不到回调。 */
struct WinEventHookEntry
{
    HWINEVENTHOOK hHook;
    WINEVENTPROC proc;
    DWORD eventMin;
    DWORD eventMax;
    DWORD idProcess;
    DWORD idThread;
    DWORD flags;
    DWORD idRegThread; /* 注册线程：WINEVENT_SKIPOWNTHREAD 过滤依据 */
};

std::mutex &EventHookMutex()
{
    static std::mutex mtx;
    return mtx;
}
swinx_stl::vector<WinEventHookEntry> g_winEventHooks;
HWINEVENTHOOK g_nextHook = (HWINEVENTHOOK)1;

/* 待派发事件队列：NotifyWinEvent 只入队（与 user32 一样立即返回），
 * 钩子回调在消息泵（sysapi.cpp 的 GetMessage/PeekMessage 调
 * SwinxDispatchPendingWinEvents）里异步执行——OUTOFCONTEXT 钩子的
 * 回调必须经消息循环投递，这是 Win32 的核心语义之一。 */
struct PendingWinEvent
{
    DWORD event;
    HWND hwnd;
    LONG idObject;
    LONG idChild;
    DWORD idThread;
    DWORD dwmsEventTime;
    /* 事件产生时刻的钩子匹配快照。与 user32 一致：接收者在事件发出那一
     * 刻确定，而不是派发时再匹配——之后注册的钩子绝不能收到此前发出的
     * 事件。（swinx 曾把匹配推迟到派发时，叠加 AT-SPI 桥的全区间钩子使
     * "无接收者则丢弃"检查恒为假，导致无测试钩子期间发出的事件滞留队
     * 列、泄漏给之后注册的钩子，leak.log 的 unhooked_is_safe 失败即此。）
     * 已入队的事件不受随后注销影响，照常投递（与测试固化的"已入队的事
     * 件不受影响"口径一致）。 */
    swinx_stl::vector<WinEventHookEntry> matched;
};

#define OLEACC_EVENT_QUEUE_CAP 512

std::mutex &PendingEventMutex()
{
    static std::mutex mtx;
    return mtx;
}
swinx_stl::vector<PendingWinEvent> g_pendingWinEvents;

/* 防重入：钩子回调里可能再调 PeekMessage（模态循环/测试泵），此时不再
 * 嵌套派发，剩余事件留给下一轮泵处理。 */
thread_local bool t_inDispatchWinEvents = false;

/* 计算事件产生时刻匹配的钩子快照（user32 语义：接收者在事件发出时确定，
 * 而非派发时再匹配）。 */
swinx_stl::vector<WinEventHookEntry> SnapshotWinEventMatches(const PendingWinEvent &ev)
{
    swinx_stl::vector<WinEventHookEntry> matched;
    std::lock_guard<std::mutex> lock(EventHookMutex());
    for (size_t i = 0; i < g_winEventHooks.size(); i++)
    {
        const WinEventHookEntry &e = g_winEventHooks[i];
        if (e.flags & WINEVENT_SKIPOWNPROCESS)
            continue; /* swinx 里所有事件都来自本进程 */
        if ((e.flags & WINEVENT_SKIPOWNTHREAD) && ev.idThread == e.idRegThread)
            continue;
        if (ev.event < e.eventMin || ev.event > e.eventMax)
            continue;
        if (e.idProcess != 0 && e.idProcess != (DWORD)::GetCurrentProcessId())
            continue;
        if (e.idThread != 0 && e.idThread != ev.idThread)
            continue;
        matched.push_back(e);
    }
    return matched;
}

/* 按事件产生时刻的钩子快照在锁外回调：钩子内可能再注册/注销钩子或发
 * 新事件，各自走各自的快照，互不影响。 */
void FireWinEvent(const PendingWinEvent &ev)
{
    for (size_t i = 0; i < ev.matched.size(); i++)
    {
        const WinEventHookEntry &e = ev.matched[i];
        e.proc(e.hHook, ev.event, ev.hwnd, ev.idObject, ev.idChild, ev.idThread, ev.dwmsEventTime);
    }
}
} // namespace

extern "C"
{
    /* 供 sysapi.cpp 的 GetMessage/PeekMessage 调用（声明在 src/SwinxAccGlue.h）：
     * 在消息泵里派发待投递的 WinEvent，与 user32 的回调时机一致。 */
    void WINAPI SwinxDispatchPendingWinEvents(void)
    {
        if (t_inDispatchWinEvents)
            return;
        t_inDispatchWinEvents = true;

        for (;;)
        {
            PendingWinEvent ev;
            {
                std::lock_guard<std::mutex> lock(PendingEventMutex());
                if (g_pendingWinEvents.empty())
                    break;
                ev = std::move(g_pendingWinEvents.front());
                g_pendingWinEvents.erase(g_pendingWinEvents.begin());
            }
            FireWinEvent(ev);
        }

        t_inDispatchWinEvents = false;
    }
} // extern "C"

extern "C"
{

    LRESULT WINAPI LresultFromObject(REFIID riid, WPARAM wParam, LPUNKNOWN pAcc)
    {
        /* 与真实 oleacc 一致（实测）：失败以 HRESULT 直接作为 LRESULT 返回
         * （文档写的"返回 0"与实际不符）。 */
        if (!pAcc)
            return (LRESULT)E_INVALIDARG;

#if defined(SWINX_HAVE_ATSPI) && defined(SOUI_ENABLE_ACC)
        /* 窗口首次响应 WM_GETOBJECT 说明进程里确实有可访问对象，此时（UI 线程）
         * 把 AT-SPI 桥挂起来。主初始化点在 SConnMgr 构造函数，这里是兜底。 */
        EnsureAtSpiInit();
#endif

        /* 对象不支持请求的接口：返回 E_NOINTERFACE（实测真实 oleacc 如此）。
         * wParam 必须是 WM_GETOBJECT 原样收到的值（服务端回传），客户端
         * ObjectFromLresult 时按 (句柄, wParam) 匹配。 */
        void *pv = NULL;
        HRESULT hr = pAcc->QueryInterface(riid, &pv);
        if (hr != S_OK)
            return (LRESULT)hr;
        static_cast<IUnknown *>(pv)->Release();

        return (LRESULT)AccTable().Acquire(pAcc, wParam);
    }

    HRESULT WINAPI ObjectFromLresult(LRESULT lResult, REFIID riid, WPARAM wParam, void **ppv)
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        if (lResult == 0)
            return E_INVALIDARG;

        IUnknown *pUnk = AccTable().Lookup((LONG)lResult, wParam);
        if (!pUnk)
            return E_FAIL; /* 未知句柄或 wParam 不匹配：实测真实 oleacc 返回 E_FAIL */
        return pUnk->QueryInterface(riid, ppv);
    }

    HRESULT WINAPI AccessibleObjectFromWindow(HWND hwnd, DWORD dwObjectID, REFIID riid, void **ppv)
    {
        if (!ppv)
            return E_POINTER;
        *ppv = NULL;
        if (!hwnd || !::IsWindow(hwnd))
            return E_INVALIDARG;

        /* 标准 MSAA 流程：OBJID 放在 lParam。wParam 实测真实 oleacc 客户端
         * 发 (WPARAM)-1，服务端原样回传给 LresultFromObject，两端 (句柄,
         * wParam) 匹配后解析成功。swinx 只提供 A/W 两个版本（没有 SendMessage
         * 泛型宏），WM_GETOBJECT 不含字符串参数，两者等价。
         * 注意符号扩展：真实 oleacc 客户端把 OBJID 当 long（32 位有符号）
         * 放进 LPARAM，OBJID_CLIENT 等负值在 64 位 LPARAM 上是符号扩展的
         * 0xFFFFFFFFFFFFFFFC；这里 (LONG) 强转复刻该行为，否则服务端的
         * "(LONG)lParam == OBJID_CLIENT" 判断在 LP64 上不成立。 */
        LRESULT lResult = ::SendMessageA(hwnd, WM_GETOBJECT, (WPARAM)-1, (LPARAM)(LONG)dwObjectID);
        if (lResult == 0)
            return E_FAIL; /* 窗口没有提供该 OBJID 的可访问对象 */
        return ObjectFromLresult(lResult, riid, (WPARAM)-1, ppv);
    }

    HRESULT WINAPI AccessibleObjectFromPoint(POINT ptScreen, IAccessible **ppAcc, VARIANT *pvarChild)
    {
        if (!ppAcc)
            return E_POINTER;
        *ppAcc = NULL;
        if (pvarChild)
        {
            VariantInit(pvarChild);
            pvarChild->vt = VT_I4;
            pvarChild->lVal = CHILDID_SELF;
        }
        HWND hwnd = ::WindowFromPoint(ptScreen);
        if (!hwnd)
            return E_FAIL;
        return AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, IID_IAccessible, (void **)ppAcc);
    }

    HRESULT WINAPI AccessibleObjectFromEvent(HWND hwnd, DWORD dwObjectID, DWORD dwChildID, IAccessible **ppAcc, VARIANT *pvarChild)
    {
        /* 与 Win32 同路径：经 AccessibleObjectFromWindow（SendMessage
         * WM_GETOBJECT）解析 (hwnd, idObject) 处的对象；pvarChild 返回事件
         * 携带的 child id，客户端按 (对象, childId) 对访问元素属性。 */
        if (!ppAcc || !pvarChild)
            return E_POINTER;
        *ppAcc = NULL;
        VariantInit(pvarChild);
        pvarChild->vt = VT_I4;
        pvarChild->lVal = (LONG)dwChildID;

        if (!hwnd || !::IsWindow(hwnd))
            return E_INVALIDARG;

        return AccessibleObjectFromWindow(hwnd, dwObjectID, IID_IAccessible, (void **)ppAcc);
    }

    HRESULT WINAPI AccessibleChildren(IAccessible *paccContainer, long iChildStart, long cChildren, VARIANT *rgvarChildren, long *pcObtained)
    {
        /* 边界语义与真实 oleacc 一致（实测）：
         *  - 空参一律 E_INVALIDARG（不是 E_POINTER）；
         *  - cChildren == 0：S_OK，*pcObtained = 0；
         *  - cChildren < 0：S_OK 且不写 *pcObtained（调用方出参保持原值）。 */
        if (!paccContainer || !rgvarChildren || !pcObtained)
            return E_INVALIDARG;
        if (cChildren < 0)
            return S_OK;
        *pcObtained = 0;
        if (cChildren == 0)
            return S_OK;

        long count = 0;
        HRESULT hr = paccContainer->get_accChildCount(&count);
        if (hr != S_OK)
            return hr;

        /* 与真实 oleacc 一致：第 i 个枚举项对应 child id (iChildStart + i + 1)，
         * 即枚举从 1 开始、永远不产生 CHILDID_SELF（Win32 语义，索引 0 不映射
         * 到容器自身）。get_accChild 失败时回退为 VT_I4 简单元素。 */
        for (long i = 0; i < cChildren; i++)
        {
            long idx = iChildStart + i + 1;
            if (idx > count)
                break;

            VARIANT varOut;
            VariantInit(&varOut);

            VARIANT varIdx;
            VariantInit(&varIdx);
            varIdx.vt = VT_I4;
            varIdx.lVal = idx;

            IDispatch *pdisp = NULL;
            hr = paccContainer->get_accChild(varIdx, &pdisp);
            if (hr == S_OK && pdisp)
            {
                varOut.vt = VT_DISPATCH;
                varOut.pdispVal = pdisp;
            }
            else
            {
                varOut.vt = VT_I4;
                varOut.lVal = idx;
            }
            VariantClear(&varIdx);

            rgvarChildren[i] = varOut;
            (*pcObtained)++;
        }

        return (*pcObtained == cChildren) ? S_OK : S_FALSE;
    }

    void WINAPI NotifyWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild)
    {
#if defined(SWINX_HAVE_ATSPI) && defined(SOUI_ENABLE_ACC)
        /* 事件也可能早于任何 WM_GETOBJECT 到达（例如焦点变化），同样触发初始化。 */
        EnsureAtSpiInit();
#endif
        /* 与 user32 一致：NotifyWinEvent 只投递事件、立即返回，钩子回调
         * （OUTOFCONTEXT）在消息泵里异步执行。接收钩子在事件发出那一刻
         * 确定（入队时快照匹配的钩子）：没有任何匹配钩子时直接丢弃，事件
         * 不滞留队列——之后注册的钩子永远收不到此前发出的事件（swinx 曾
         * 把匹配推迟到派发时，事件泄漏给之后注册的钩子，leak.log 捕获）。
         * AT-SPI 桥持有全区间钩子时事件照常入队并投递给桥，这正是桥接收
         * 事件的通道；测试钩子注册得更晚，自然不在快照里。 */
        PendingWinEvent ev;
        ev.event = event;
        ev.hwnd = hwnd;
        ev.idObject = idObject;
        ev.idChild = idChild;
        ev.idThread = (DWORD)::GetCurrentThreadId();
        ev.dwmsEventTime = ::GetTickCount();
        ev.matched = SnapshotWinEventMatches(ev);
        if (ev.matched.empty())
            return; /* 无人接收：立即丢弃，不占队列 */

        std::lock_guard<std::mutex> lock(PendingEventMutex());
        if (g_pendingWinEvents.size() >= OLEACC_EVENT_QUEUE_CAP)
            g_pendingWinEvents.erase(g_pendingWinEvents.begin()); /* 丢弃最旧事件 */
        g_pendingWinEvents.push_back(std::move(ev));
    }

    /* ---------------- 角色 / 状态文本 ---------------- */

    struct RoleTextEntry
    {
        DWORD role;
        LPCWSTR text;
    };

    static const RoleTextEntry kRoleTexts[] = {
        { ROLE_SYSTEM_TITLEBAR, L"title bar" },
        { ROLE_SYSTEM_MENUBAR, L"menu bar" },
        { ROLE_SYSTEM_SCROLLBAR, L"scroll bar" },
        { ROLE_SYSTEM_GRIP, L"grip" },
        { ROLE_SYSTEM_SOUND, L"sound" },
        { ROLE_SYSTEM_CURSOR, L"mouse pointer" },
        { ROLE_SYSTEM_CARET, L"text cursor" },
        { ROLE_SYSTEM_ALERT, L"alert" },
        { ROLE_SYSTEM_WINDOW, L"window" },
        { ROLE_SYSTEM_CLIENT, L"client" },
        { ROLE_SYSTEM_MENUPOPUP, L"menu" },
        { ROLE_SYSTEM_MENUITEM, L"menu item" },
        { ROLE_SYSTEM_TOOLTIP, L"tool tip" },
        { ROLE_SYSTEM_APPLICATION, L"application" },
        { ROLE_SYSTEM_DOCUMENT, L"document" },
        { ROLE_SYSTEM_PANE, L"pane" },
        { ROLE_SYSTEM_CHART, L"chart" },
        { ROLE_SYSTEM_DIALOG, L"dialog" },
        { ROLE_SYSTEM_BORDER, L"border" },
        { ROLE_SYSTEM_GROUPING, L"grouping" },
        { ROLE_SYSTEM_SEPARATOR, L"separator" },
        { ROLE_SYSTEM_TOOLBAR, L"tool bar" },
        { ROLE_SYSTEM_STATUSBAR, L"status bar" },
        { ROLE_SYSTEM_TABLE, L"table" },
        { ROLE_SYSTEM_COLUMNHEADER, L"column header" },
        { ROLE_SYSTEM_ROWHEADER, L"row header" },
        { ROLE_SYSTEM_COLUMN, L"column" },
        { ROLE_SYSTEM_ROW, L"row" },
        { ROLE_SYSTEM_CELL, L"cell" },
        { ROLE_SYSTEM_LINK, L"link" },
        { ROLE_SYSTEM_HELPBALLOON, L"help balloon" },
        { ROLE_SYSTEM_CHARACTER, L"character" },
        { ROLE_SYSTEM_LIST, L"list" },
        { ROLE_SYSTEM_LISTITEM, L"list item" },
        { ROLE_SYSTEM_OUTLINE, L"outline" },
        { ROLE_SYSTEM_OUTLINEITEM, L"outline item" },
        { ROLE_SYSTEM_PAGETAB, L"page tab" },
        { ROLE_SYSTEM_PROPERTYPAGE, L"property page" },
        { ROLE_SYSTEM_INDICATOR, L"indicator" },
        { ROLE_SYSTEM_GRAPHIC, L"graphic" },
        { ROLE_SYSTEM_STATICTEXT, L"static text" },
        { ROLE_SYSTEM_TEXT, L"text" },
        { ROLE_SYSTEM_PUSHBUTTON, L"push button" },
        { ROLE_SYSTEM_CHECKBUTTON, L"check button" },
        { ROLE_SYSTEM_RADIOBUTTON, L"radio button" },
        { ROLE_SYSTEM_COMBOBOX, L"combo box" },
        { ROLE_SYSTEM_DROPLIST, L"drop list" },
        { ROLE_SYSTEM_PROGRESSBAR, L"progress bar" },
        { ROLE_SYSTEM_DIAL, L"dial" },
        { ROLE_SYSTEM_HOTKEYFIELD, L"hot key field" },
        { ROLE_SYSTEM_SLIDER, L"slider" },
        { ROLE_SYSTEM_SPINBUTTON, L"spin button" },
        { ROLE_SYSTEM_DIAGRAM, L"diagram" },
        { ROLE_SYSTEM_ANIMATION, L"animation" },
        { ROLE_SYSTEM_EQUATION, L"equation" },
        { ROLE_SYSTEM_BUTTONDROPDOWN, L"drop down button" },
        { ROLE_SYSTEM_BUTTONMENU, L"menu button" },
        { ROLE_SYSTEM_PAGETABLIST, L"page tab list" },
        { ROLE_SYSTEM_CLOCK, L"clock" },
        { ROLE_SYSTEM_SPLITBUTTON, L"split button" },
        { ROLE_SYSTEM_IPADDRESS, L"IP address" },
    };

    struct StateTextEntry
    {
        DWORD bit;
        LPCWSTR text;
    };

    static const StateTextEntry kStateTexts[] = {
        { STATE_SYSTEM_UNAVAILABLE, L"unavailable" },
        { STATE_SYSTEM_SELECTED, L"selected" },
        { STATE_SYSTEM_FOCUSED, L"focused" },
        { STATE_SYSTEM_PRESSED, L"pressed" },
        { STATE_SYSTEM_CHECKED, L"checked" },
        { STATE_SYSTEM_MIXED, L"mixed" },
        { STATE_SYSTEM_READONLY, L"read only" },
        { STATE_SYSTEM_HOTTRACKED, L"hot tracked" },
        { STATE_SYSTEM_DEFAULT, L"default" },
        { STATE_SYSTEM_EXPANDED, L"expanded" },
        { STATE_SYSTEM_COLLAPSED, L"collapsed" },
        { STATE_SYSTEM_BUSY, L"busy" },
        { STATE_SYSTEM_FLOATING, L"floating" },
        { STATE_SYSTEM_MARQUEED, L"marqueed" },
        { STATE_SYSTEM_ANIMATED, L"animated" },
        { STATE_SYSTEM_INVISIBLE, L"invisible" },
        { STATE_SYSTEM_OFFSCREEN, L"offscreen" },
        { STATE_SYSTEM_SIZEABLE, L"sizeable" },
        { STATE_SYSTEM_MOVEABLE, L"moveable" },
        { STATE_SYSTEM_SELFVOICING, L"self voicing" },
        { STATE_SYSTEM_FOCUSABLE, L"focusable" },
        { STATE_SYSTEM_SELECTABLE, L"selectable" },
        { STATE_SYSTEM_LINKED, L"linked" },
        { STATE_SYSTEM_TRAVERSED, L"traversed" },
        { STATE_SYSTEM_MULTISELECTABLE, L"multiple selectable" },
        { STATE_SYSTEM_EXTSELECTABLE, L"extended selectable" },
        { STATE_SYSTEM_ALERT_LOW, L"alert low" },
        { STATE_SYSTEM_ALERT_MEDIUM, L"alert medium" },
        { STATE_SYSTEM_ALERT_HIGH, L"alert high" },
        { STATE_SYSTEM_PROTECTED, L"protected" },
        { STATE_SYSTEM_HASPOPUP, L"has pop-up" },
    };

    static UINT CopyAccTextW(LPCWSTR src, LPWSTR dst, UINT cchMax)
    {
        if (!src)
            return 0;
        if (!dst || cchMax == 0)
        {
            return (UINT)wcslen(src);
        }
        UINT len = (UINT)wcslen(src);
        UINT copy = len < (cchMax - 1) ? len : (cchMax - 1);
        memcpy(dst, src, copy * sizeof(WCHAR));
        dst[copy] = 0;
        return copy;
    }

    UINT WINAPI GetRoleTextW(DWORD lRole, LPWSTR lpszRole, UINT cchRoleMax)
    {
        for (size_t i = 0; i < sizeof(kRoleTexts) / sizeof(kRoleTexts[0]); i++)
        {
            if ((DWORD)kRoleTexts[i].role == lRole)
                return CopyAccTextW(kRoleTexts[i].text, lpszRole, cchRoleMax);
        }
        if (lpszRole && cchRoleMax > 0)
            lpszRole[0] = 0;
        return 0; /* Win32: 未知角色返回 0 */
    }

    UINT WINAPI GetStateTextW(DWORD lStateBit, LPWSTR lpszState, UINT cchStateMax)
    {
        for (size_t i = 0; i < sizeof(kStateTexts) / sizeof(kStateTexts[0]); i++)
        {
            if (kStateTexts[i].bit == lStateBit)
                return CopyAccTextW(kStateTexts[i].text, lpszState, cchStateMax);
        }
        if (lpszState && cchStateMax > 0)
            lpszState[0] = 0;
        return 0;
    }

    /* 查表 + 转码的公共路径：未知键返回 NULL（A/W 版本据此返回 0） */
    static LPCWSTR FindRoleText(DWORD lRole)
    {
        for (size_t i = 0; i < sizeof(kRoleTexts) / sizeof(kRoleTexts[0]); i++)
            if ((DWORD)kRoleTexts[i].role == lRole)
                return kRoleTexts[i].text;
        return NULL;
    }

    static LPCWSTR FindStateText(DWORD lStateBit)
    {
        for (size_t i = 0; i < sizeof(kStateTexts) / sizeof(kStateTexts[0]); i++)
            if (kStateTexts[i].bit == lStateBit)
                return kStateTexts[i].text;
        return NULL;
    }

    static UINT CopyAccTextA(LPCWSTR src, LPSTR dst, UINT cchMax)
    {
        if (!src || !*src)
            return 0;
        int need = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
        if (need <= 1)
            return 0;
        if (!dst || cchMax == 0)
            return (UINT)(need - 1); // 所需字符数（不含结尾 NUL）
        int ret = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)cchMax, NULL, NULL);
        return ret > 0 ? (UINT)(ret - 1) : 0;
    }

    UINT WINAPI GetRoleTextA(DWORD lRole, LPSTR lpszRole, UINT cchRoleMax)
    {
        LPCWSTR text = FindRoleText(lRole);
        if (!text)
        {
            if (lpszRole && cchRoleMax > 0)
                lpszRole[0] = 0;
            return 0;
        }
        return CopyAccTextA(text, lpszRole, cchRoleMax);
    }

    UINT WINAPI GetStateTextA(DWORD lStateBit, LPSTR lpszState, UINT cchStateMax)
    {
        LPCWSTR text = FindStateText(lStateBit);
        if (!text)
        {
            if (lpszState && cchStateMax > 0)
                lpszState[0] = 0;
            return 0;
        }
        return CopyAccTextA(text, lpszState, cchStateMax);
    }

    /* ---------------- WinEvent 钩子（标准 user32 API 面） ---------------- */

    HWINEVENTHOOK WINAPI SetWinEventHook(DWORD eventMin, DWORD eventMax, HMODULE hmodWinEventProc, WINEVENTPROC pfnWinEventProc, DWORD idProcess, DWORD idThread, DWORD dwFlags)
    {
        (void)hmodWinEventProc; /* WINEVENT_OUTOFCONTEXT 语义下不需要 DLL */
        if (!pfnWinEventProc || eventMin > eventMax)
            return NULL; /* 与 Win32 一致：非法参数返回 NULL */
        /* 与 Win32 一致：SKIPOWNPROCESS/SKIPOWNTHREAD 是过滤标志而非注册
         * 错误，钩子正常注册（返回有效句柄），只是收不到对应来源的事件
         * ——swinx 里所有事件都来自本进程，SKIPOWNPROCESS 钩子永远收不到
         * 回调。 */

        std::lock_guard<std::mutex> lock(EventHookMutex());
        WinEventHookEntry e;
        e.hHook = g_nextHook;
        g_nextHook = (HWINEVENTHOOK)((UINT_PTR)g_nextHook + 1);
        e.proc = pfnWinEventProc;
        e.eventMin = eventMin;
        e.eventMax = eventMax;
        e.idProcess = idProcess;
        e.idThread = idThread;
        e.flags = dwFlags;
        e.idRegThread = (DWORD)::GetCurrentThreadId();
        g_winEventHooks.push_back(e);
        return e.hHook;
    }

    BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK hWinEventHook)
    {
        if (!hWinEventHook)
            return FALSE;
        std::lock_guard<std::mutex> lock(EventHookMutex());
        for (size_t i = 0; i < g_winEventHooks.size(); i++)
        {
            if (g_winEventHooks[i].hHook == hWinEventHook)
            {
                g_winEventHooks.erase(g_winEventHooks.begin() + i);
                return TRUE;
            }
        }
        return FALSE; /* 与 Win32 一致：未知句柄返回 FALSE */
    }

    /* ---------------- 平台胶水共享助手 ----------------
     * cocoa / linux 两个平台桥的公共内核：从 hwnd 的可访问根出发，
     * 沿 MSAA child id 链按需解析目标元素。每次调用都重新走
     * WM_GETOBJECT 查询（SHostWnd 决定返回 NULL 还是 IAccessible），
     * swinx 不缓存任何接口指针。 */

    static HRESULT AccGetRootAccessible(HWND hwnd, IAccessible **ppAcc)
    {
        /* 优先取客户区对象（控件树根），回退到整个窗口 */
        HRESULT hr = AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, IID_IAccessible, (void **)ppAcc);
        if (hr != S_OK || !*ppAcc)
            hr = AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, IID_IAccessible, (void **)ppAcc);
        return hr;
    }

    HRESULT WINAPI SwinxAccResolvePath(HWND hwnd, const LONG *pChain, LONG cChain, IAccessible **ppAcc, LONG *pChildId)
    {
        if (!ppAcc || !pChildId)
            return E_POINTER;
        *ppAcc = NULL;
        *pChildId = CHILDID_SELF;
        if (!pChain && cChain > 0)
            return E_INVALIDARG;

        IAccessible *cur = NULL;
        if (!hwnd || !::IsWindow(hwnd) || AccGetRootAccessible(hwnd, &cur) != S_OK || !cur)
            return E_FAIL;

        for (LONG i = 0; i < cChain; i++)
        {
            LONG childId = pChain[i];
            BOOL bLast = (i + 1 == cChain);

            VARIANT varIdx;
            VariantInit(&varIdx);
            varIdx.vt = VT_I4;
            varIdx.lVal = childId;

            IDispatch *pdisp = NULL;
            HRESULT hr = cur->get_accChild(varIdx, &pdisp);
            VariantClear(&varIdx);

            IAccessible *next = NULL;
            if (hr == S_OK && pdisp)
            {
                if (pdisp->QueryInterface(IID_IAccessible, (void **)&next) != S_OK)
                    next = NULL;
                pdisp->Release();
            }

            if (!next)
            {
                /* 简单元素（get_accChild 未给出子对象）：只有末级才合法，
                 * 简单元素自身没有子节点。 */
                if (!bLast)
                {
                    cur->Release();
                    return E_FAIL; /* 路径已失效（控件销毁/重建） */
                }
                *ppAcc = cur; /* 调用方负责 Release；查询时须带 childId */
                *pChildId = childId;
                return S_OK;
            }

            cur->Release();
            cur = next;
        }

        *ppAcc = cur; /* 完整对象：acc 自身即目标 */
        *pChildId = CHILDID_SELF;
        return S_OK;
    }
} // extern "C"
