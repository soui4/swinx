# swinx — Linux 无障碍（AT-SPI2 / D-Bus）桥接

> 适用平台：Linux 桌面（X11，swinx 的 xcb 后端）
> 代码：`swinx/src/platform/linux/SAtSpi.h` / `SAtSpi.cpp`
> 关联：`swinx/include/oleacc.h`、`swinx/src/oleacc.cpp`（MSAA 接口面）
> macOS 对应实现：`swinx/src/platform/cocoa/SNsAccessibility.mm`

## 1. 它解决什么问题

swinx 在 Linux 上早就有了完整的 MSAA 接口面：`AccessibleObjectFromWindow()` 向窗口
发 `WM_GETOBJECT`、SOUI 的 `SHostWnd::OnGetObject` 用 `LresultFromObject()` 应答，
进程内的 MSAA 查询是通的。

但 Linux 的屏幕阅读器（Orca）、检查工具（accerciser）**不认 MSAA**，它们只走
AT-SPI2 —— 一套跑在**无障碍总线（a11y bus）**上的 D-Bus 协议。所以在有这层桥之前，
Linux 上的无障碍对象树对外等于不存在：`NotifyWinEvent` 没有任何消费者。

本桥把进程内的 MSAA 树映射成 a11y bus 上的 AT-SPI2 对象树。

```
   Orca / accerciser                        swinx 进程
   (libatspi)                    ┌────────────────────────────────┐
        │                        │  SAtSpi.cpp                    │
        │  D-Bus 方法调用         │   ├─ 对象路径 <-> MSAA 节点     │
        ├───────────────────────>│   ├─ role/state/几何 映射       │
        │  a11y bus              │   └─ ResolveNode(): 按路径导航  │
        │                        │            │                   │
        │                        │            ▼                   │
        │                        │  AccessibleObjectFromWindow()  │
        │                        │       -> SendMessage(WM_GETOBJECT)
        │                        │            │                   │
        │                        │            ▼                   │
        │                        │  SOUI SwndAccessible (IAccessible)
        │  D-Bus 信号（事件）      │            ▲                   │
        <───────────────────────┤            │                   │
                                 │  NotifyWinEvent -> 事件钩子     │
                                 └────────────────────────────────┘
```

## 2. 协议要点（与 at-spi2-core 对齐）

实现前核对过 at-spi2-core 的 `xml/*.xml` 与 `atk-adaptor/` 源码：

| 环节 | 做法 |
| --- | --- |
| 取总线地址 | 先读环境变量 `AT_SPI_BUS`；否则向 session bus 的 `org.a11y.Bus` `/org/a11y/bus` 调 `GetAddress()` |
| 连接 | `dbus_connection_open(addr)` + `dbus_bus_register()`（p2p 总线必须 register 才有 unique name） |
| 注册应用 | 向 `org.a11y.atspi.Registry` 的 `/org/a11y/atspi/accessible/root` 调 `org.a11y.atspi.Socket.Embed(plug=(so))`，返回 registry 根对象引用 |
| 对象路径 | 应用根固定为 `/org/a11y/atspi/accessible/root`；其余挂在 `/org/a11y/atspi/accessible/` 下 |
| 事件 | D-Bus **信号**，`interface = org.a11y.atspi.Event.<Class>`、`member = 事件名(CamelCase)`，签名固定 `"siiva{sv}"`（detail, detail1, detail2, any_data, properties） |

实现的接口：

- `org.a11y.atspi.Accessible` — GetRole / GetRoleName / GetState / GetChildAtIndex /
  GetChildren / GetIndexInParent / GetRelationSet / GetAttributes / GetApplication /
  GetInterfaces，以及经 `org.freedesktop.DBus.Properties` 暴露的 Name / Description /
  Parent / ChildCount / Locale / AccessibleId / HelpText
- `org.a11y.atspi.Component` — GetExtents / GetPosition / GetSize / Contains /
  GetAccessibleAtPoint / GetLayer / GetMDIZOrder / GrabFocus / GetAlpha
  （`Set*` / `ScrollTo*` 不支持，返回 false）
- `org.a11y.atspi.Action` — GetActions / DoAction / GetName 等（映射到
  `get_accDefaultAction` / `accDoDefaultAction`）
- `org.a11y.atspi.Application`（仅根对象）— ToolkitName / AtspiVersion / Id 等
- `org.a11y.atspi.Cache` — `GetItems`（Orca 主要靠它一次性拉全树）
- `org.freedesktop.DBus.Introspectable` — 静态 introspection XML

## 3. 对象路径编码：为什么不缓存 IAccessible*

SOUI 的 `SwndAccessible` 生命周期绑在 `SWindow` 上，控件销毁后指针即失效；AT-SPI 侧
又拿不到销毁通知（见第 5 节的事件限制）。**在桥里长期保存 `IAccessible*` 必然悬垂。**

所以路径做成**确定性编码**，不依赖任何缓存表：

```
/org/a11y/atspi/accessible/root                 应用根（role=APPLICATION）
/org/a11y/atspi/accessible/w<hwnd>              顶层窗口（role=FRAME）
/org/a11y/atspi/accessible/w<hwnd>c             窗口客户区根（MSAA OBJID_CLIENT）
/org/a11y/atspi/accessible/w<hwnd>c_1           客户区的第 1 个子（MSAA child id，1-based）
/org/a11y/atspi/accessible/w<hwnd>c_1_3         再下一层
```

`<hwnd>` 是十六进制（`HWND` 在 swinx 里是 `UINT_PTR`），层级用 `_` 分隔（D-Bus object
path 只允许 `[A-Za-z0-9_]`，所以不能用 `.` 或 `-`）。

每次处理请求时 `ResolveNode()` 都从窗口根重新导航一遍：代价是 O(深度) 次
`get_accChild`，换来的是**永远不会访问野指针**；控件被销毁重建后，只是路径解析失败、
该节点退化成空信息，不会崩。

> 导航内核已下沉为 swinx 内部共享助手 `SwinxAccResolvePath()`（`src/SwinxAccGlue.h`，
> 属实现细节，不进公共 API 面），macOS 的 NSAccessibility 桥（`SNsAccessibility.mm`）
> 用同一套 (hwnd, child 链) 身份模型 + 按需解析，两个平台的 acc 语义保持一致。
> 同时 swinx 的 `LresultFromObject` 句柄表为**弱登记**（不 AddRef），`WM_GETOBJECT`
> 的 LRESULT 只在同一次同步往返内有效——swinx 全链路都不持有 `IAccessible`，
> 接口的所有者始终是 SOUI。事件接收用标准 `SetWinEventHook`/`UnhookWinEvent`
> （winuser.h，与 user32 语义一致）。

MSAA 的两种子元素形态都处理了：`get_accChild` 返回 `IDispatch` 的是"完整对象"
（用 `childId = CHILDID_SELF` 表示），返回失败的是"简单元素"（保留父对象 +
`childId` 表示，且它没有子节点）。

## 4. 线程模型

AT-SPI 的查询最终要走到 `AccessibleObjectFromWindow` → `SendMessage(WM_GETOBJECT)`。
swinx 的跨线程 `SendMessage` 会把消息 post 给窗口所属线程并等待，从 D-Bus 线程发起
既绕又容易踩到调用栈深度限制。

所以桥**不额外开线程**：

- `SwinxAtSpiInit()` 在 UI 线程被调用。**主初始化点是 `SConnMgr::instance()`
  首次创建进程单例完成后**（`SConnection.cpp`，`SOUI_ENABLE_ACC` 编译闸门内）：
  SConnMgr 是进程单例、由第一个创建窗口的（UI）线程拉起，此时初始化早于任何
  业务事件——读屏器（Orca/accerciser）常在应用发出任何事件之前就开始枚举
  对象树。注意初始化**不能放在 SConnMgr 构造函数里**：init 里
  `SetTimer(NULL, 0, 50, proc)` 会经 wnd.cpp 重入 `instance()`，与 `static
  SConnMgr inst` 的初始化守卫（`__cxa_guard`，不可重入）死锁（`SwinxAtSpiInit`
  内另有 thread_local 重入防御，覆盖惰性链先于 instance() 到达的场景——例如
  `--gtest_filter=swinx_oleacc.*` 只跑 ACC 用例时，首个 `LresultFromObject`
  就触发惰性初始化）。`oleacc.cpp` 在 `LresultFromObject`/`NotifyWinEvent`
  里保留惰性兜底，覆盖不经 SConnMgr 的路径（如单元测试）。`SetTimer(NULL, 0,
  50, proc)` 把 D-Bus 分发挂进该线程的消息循环（swinx 的
  `SetTimer(NULL, ...)` 支持纯回调，不需要隐藏窗口）。
- 定时器回调里 `dbus_connection_read_write_dispatch(..., 0)` + 按
  `DBUS_DISPATCH_DATA_REMAINS` 循环。
- 因此处理 D-Bus 请求时可以直接调 MSAA，无需跨线程 marshal。
- 锁用 `std::recursive_mutex`：`Pump` 处理请求 → 调 MSAA → `WM_GETOBJECT` →
  `LresultFromObject` → 可能再次进入 `EnsureInit`，同一线程二次加锁必须安全。

初始化失败的兜底：拿不到 a11y bus、注册失败、`Embed` 失败都只是"桥不存在"，
应用照常运行。可用环境变量 `SWINX_DISABLE_ATSPI=1` 主动关闭。

## 5. 事件桥

`SAtSpi.cpp` 通过标准 `SetWinEventHook()`（winuser.h）注册回调，`NotifyWinEvent`
的转发映射：

| MSAA 事件 | AT-SPI 信号 |
| --- | --- |
| `EVENT_OBJECT_CREATE/DESTROY` | `Event.Object.ChildrenChanged` (add/remove) |
| `EVENT_OBJECT_SHOW/HIDE` | `Event.Object.StateChanged` (visible / showing) |
| `EVENT_OBJECT_FOCUS` | `Event.Object.StateChanged`(focused) + `Event.Focus.Focus` |
| `EVENT_OBJECT_NAMECHANGE` | `Event.Object.PropertyChange`(accessible-name) |
| `EVENT_OBJECT_VALUECHANGE` | `Event.Object.PropertyChange`(accessible-value) |
| `EVENT_OBJECT_DESCRIPTIONCHANGE` | `Event.Object.PropertyChange`(accessible-description) |
| `EVENT_OBJECT_LOCATIONCHANGE` | `Event.Object.BoundsChanged` |
| `EVENT_OBJECT_SELECTION` | `Event.Object.StateChanged`(selected) |
| `EVENT_SYSTEM_FOREGROUND` | `Event.Window.Activate` |
| `EVENT_SYSTEM_DIALOGSTART/END` | `Event.Window.Create/Destroy` |

**归因粒度是已知限制**：SOUI 的 `SWindow::accNotifyEvent()` 用 `idObject` 传 **SWND**
（不是 `OBJID_*`），桥无法从 SWND 反查到具体的 AT-SPI 节点，因此这类事件统一归因到
该窗口的**客户区根节点**。AT 能收到通知并朗读变化，但定位不到精确控件。
要做得更精确，需要 swinx/SOUI 额外提供 SWND → 节点 的反查通道。

## 6. 启用与验证

桥接层随 swinx 一起编译（Linux 走 `swinx/linux.cmake`，`src/platform/linux/*.cpp`
会被 glob 到），无需额外开关。想让**控件级**对象树完整，SOUI 侧还要开：

```bash
cmake -DSOUI_ENABLE_ACC=ON ...
```

（该开关控制 SOUI 的 `SwndAccessible`；不开时桥依然工作，只是只能暴露窗口框架。）

手动验证：

```bash
# 1. 确认无障碍总线在跑（会话通常由桌面环境启动）
echo $AT_SPI_BUS

# 2. 用 accerciser 看应用树
accerciser &

# 3. 或用 dbus-send 直接问（把 <bus> 换成 $AT_SPI_BUS，<name> 换成 swinx 应用的 unique name）
dbus-send --bus="$AT_SPI_BUS" --print-reply \
  --dest=<name> /org/a11y/atspi/accessible/root \
  org.a11y.atspi.Accessible.GetChildCount

# 4. 批量拉整棵树
dbus-send --bus="$AT_SPI_BUS" --print-reply \
  --dest=<name> /org/a11y/atspi/accessible/root \
  org.a11y.atspi.Cache.GetItems

# 5. Orca
orca &
```

没有自动化测试的原因：需要真实的 a11y bus 与桌面会话，CI 环境通常不具备；
`demos/fun_test/test_oleacc.cpp` 覆盖的是 MSAA 接口面本身（与桥无关）。

## 7. 已知限制

- 事件经标准 WinEvent 钩子接收；`NotifyWinEvent` 只入队，回调在消息泵里
  异步派发（与 user32 的 OUTOFCONTEXT 钩子一致），AT-SPI 桥随 UI 线程的
  timer 分发一并收到事件。
- 事件归因到窗口客户区根节点，粒度偏粗（见第 5 节）。
- 未实现 Text / Value / Selection / Table / Document 等扩展接口，文本控件对 AT
  只暴露 name/value，不支持逐字符朗读与选区。
- `Component` 的 `SetExtents/SetPosition/SetSize/ScrollTo*`、`GetAlpha` 为固定值/不支持。
- `Cache.GetItems` 限制为最多 2000 个节点、深度 20，超出的部分会被截断。
- 顶层窗口列表有 100ms 缓存（`EnumWindows` 开销），极端情况下新建窗口可能延迟可见。
