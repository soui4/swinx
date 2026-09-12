/*
 * swinx — Linux 无障碍桥接：把 MSAA(IAccessible) 对象树暴露为 AT-SPI2 (D-Bus) 树
 *
 * 背景
 * ----
 * swinx 在 Linux 上已经提供了完整的 MSAA 接口面（include/oleacc.h + src/oleacc.cpp），
 * SOUI 的 SwndAccessible 也照 Win32 的方式响应 WM_GETOBJECT。但 Linux 的屏幕阅读器
 * （Orca / accerciser）只认 AT-SPI2，它们通过无障碍总线（a11y bus）上的 D-Bus 接口
 * 拉取对象树。本文件就是这层桥：
 *
 *   MSAA 树 (进程内)  <-->  AT-SPI2 对象树 (a11y bus 上的 D-Bus 对象路径)
 *
 * 协议要点（与 at-spi2-core 一致）
 * ------------------------------
 * 1. 总线地址：先取环境变量 AT_SPI_BUS，否则向 session bus 的
 *    org.a11y.Bus (/org/a11y/bus) 调 GetAddress() 拿到 a11y bus 地址。
 * 2. 注册：连接 a11y bus 后，向 org.a11y.atspi.Registry 的
 *    /org/a11y/atspi/accessible/root 调 org.a11y.atspi.Socket.Embed(plug=(so))，
 *    返回 registry 根对象的 (bus name, object path)。
 * 3. 对象树：应用必须有唯一根 /org/a11y/atspi/accessible/root（实现
 *    org.a11y.atspi.Application），其余对象挂在该路径下。
 * 4. 事件：D-Bus 信号，interface = org.a11y.atspi.Event.<Class>，
 *    member = 事件名（CamelCase），签名固定为 "siiva{sv}"
 *    (detail, detail1, detail2, any_data, properties)。
 *
 * 设计取舍
 * --------
 * - 对象路径是**确定性编码**的："/org/a11y/atspi/accessible/w<hwnd>c_<i>_<j>"。
 *   这样不需要在桥里长期保存 IAccessible*（SOUI 的控件销毁后指针会失效，缓存它
 *   必然悬垂）；每次请求时按路径从窗口根重新导航一遍，代价是 O(深度) 次调用，
 *   换来的是绝不访问野指针。
 * - 所有 D-Bus 收发都在**创建连接的那个线程**（通常是 UI 线程）完成：桥用
 *   SetTimer(NULL, 0, 50, proc) 挂进 swinx 的消息循环，定时器回调里驱动
 *   dbus_connection_read_write_dispatch。因此处理 D-Bus 请求时可以直接调用
 *   MSAA（含 SendMessage(WM_GETOBJECT)），不需要跨线程 marshal。
 * - NotifyWinEvent 通过标准 SetWinEventHook（winuser.h，与 user32 语义一致）
 *   转发到这里，转成 AT-SPI 信号。
 *   事件当前只按 hwnd 归因到窗口节点，钩子不使用 idObject（见 SAtSpi.cpp 的
 *   AtSpi::OnWinEvent）。SOUI 的 SWindow::accNotifyEvent 在 idObject 里传的是
 *   SWND，这不是 OBJID_*，但**完全合法**：MSDN（winuser.h / NotifyWinEvent）对
 *   idObject 的定义是 "either one of the predefined object identifiers or a
 *   custom object ID value"，SWND 属于 custom object ID。服务端
 *   SHostWnd::OnGetObject 正是按此约定反查：非 OBJID_CLIENT 的 lParam 走
 *   SWindowMgr::GetWindow(lParam) 取回 SWindow。
 *   因此该值是可解析的——桥若要更细的粒度，调
 *   AccessibleObjectFromEvent(hwnd, idObject, idChild, &pAcc, &varChild) 即可
 *   经 AccessibleObjectFromWindow + WM_GETOBJECT 拿回精确节点。
 */
#ifndef SWINX_SATSPI_H_
#define SWINX_SATSPI_H_

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* 建立到 a11y bus 的连接并注册对象树。可重复调用；真正的初始化只做一次。
     * 必须在会跑 swinx 消息循环的线程（通常是 UI 线程）里首次调用。 */
    void SwinxAtSpiInit(void);

    /* 断开连接、注销定时器。进程退出时不需要显式调用。 */
    void SwinxAtSpiShutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SWINX_SATSPI_H_
