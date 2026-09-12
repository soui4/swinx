#ifndef _SWINX_ACC_GLUE_H_
#define _SWINX_ACC_GLUE_H_

/* swinx 无障碍胶水层内部共享助手
 *
 * 注意：这是 **实现细节，不是公共 API**——公共头文件
 * swinx/include/oleacc.h 保持与 Windows SDK oleacc.h 相同的 API 面，不放
 * 任何 swinx 扩展。本助手只被平台胶水模块（cocoa/SNsAccessibility.mm、
 * linux/SAtSpi.cpp）与 swinx 自身的实现（src/oleacc.cpp）包含。
 */

#include <windows.h>
#include <oleacc.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* 从 hwnd 的可访问根（OBJID_CLIENT，回退 OBJID_WINDOW）出发，沿 MSAA
     * child id 链逐级下钻，按需解析目标元素。每次调用都重新向窗口发
     * WM_GETOBJECT 查询（SHostWnd 按 SOUI_ENABLE_ACC 决定返回 NULL 还是
     * IAccessible），swinx 不缓存接口指针。
     *
     * 输出约定（MSAA 语义）：
     *   - *pChildId == CHILDID_SELF：*ppAcc 即目标对象；
     *   - *pChildId != CHILDID_SELF：目标是简单元素，*ppAcc 为其父对象，
     *     查询属性时须带 childId。
     * 调用方负责 Release(*ppAcc)。返回 S_OK 或 E_FAIL（窗口无对象/链失效）。 */
    HRESULT WINAPI SwinxAccResolvePath(HWND hwnd, const LONG *pChain, LONG cChain,
                                       IAccessible **ppAcc, LONG *pChildId);

    /* WinEvent 异步派发泵：NotifyWinEvent 只把事件入队，钩子回调（与 user32
     * 的 OUTOFCONTEXT 钩子一致）在消息泵里执行。src/sysapi.cpp 的
     * GetMessage/PeekMessage 在每次取消息前后调用本函数；实现位于
     * src/oleacc.cpp。仅 swinx 内部使用，不是公共 API。 */
    void WINAPI SwinxDispatchPendingWinEvents(void);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_ACC_GLUE_H_
