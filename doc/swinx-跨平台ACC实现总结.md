# swinx 跨平台无障碍（ACC）实现总结

> 更新日期：2026-09-12  
> 适用版本：swinx + SOUI（SOUI_ENABLE_ACC）  
> 覆盖平台：Windows（原生）、macOS（NSAccessibility 桥）、Linux（AT-SPI2 桥）

---

## 1. 总体架构：三层设计

```
┌─────────────────────────────────────────────────────────┐
│ SOUI 应用层（MSAA 语义）                                  │
│   SAccessible / SAccProxy —— IAccessible COM 接口，       │
│   以 SWindow 树回答 accNavigate/accHitTest/accLocation 等 │
├─────────────────────────────────────────────────────────┤
│ swinx 进程内 MSAA 兼容层（oleacc.cpp）                     │
│   AccessibleObjectFromWindow / LresultFromObject /        │
│   ObjectFromLresult —— WM_GETOBJECT 协议的进程内实现       │
│   NotifyWinEvent / SetWinEventHook —— 事件系统            │
│   SwinxAccResolvePath —— (hwnd, childPath) 解析与弱登记    │
├─────────────────────────────────────────────────────────┤
│ 平台桥                                                    │
│   Windows：系统原生 MSAA/UIA，swinx 只需实现 Win32 语义    │
│   macOS  ：SNsAccessibility.mm（NSAccessibility 身份壳桥） │
│   Linux  ：SAtSpi.cpp（MSAA → AT-SPI2 over D-Bus 桥）      │
└─────────────────────────────────────────────────────────┘
```

设计原则：

1. **平台桥只做协议转换**，不持有业务状态：桥把平台 ACC 查询翻译成对
   IAccessible 的调用（经由兼容层拿到窗口根对象），语义判断全部留在
   SOUI 的 SAccessible。
2. **元素身份不长期持有 COM 指针**：macOS 用 (hwnd, chain) 身份壳、
   Linux 用确定性对象路径，每次请求从窗口根重新导航（代价 O(深度)，
   换来绝不访问已销毁控件的野指针）。
3. **确定性 + 弱引用**：兼容层只维护 hwnd → 根对象的弱登记，控件链
   解析失败即返回空，绝不缓存 IAccessible* 跨消息使用。

## 2. 对象模型与身份表示

| 维度 | Windows | macOS | Linux (AT-SPI2) |
| :--- | :--- | :--- | :--- |
| ACC API | MSAA / UIA（系统原生） | NSAccessibility | AT-SPI2 over D-Bus |
| 元素身份 | COM 对象本身 | 身份壳 (hwnd, chain)，按链缓存 | 确定性对象路径，见下 |
| 树来源 | SOUI SWindow 树 | 同左（经 IAccessible） | 同左（经 IAccessible） |
| 命中测试 | `accHitTest` | `accessibilityHitTest:` 四级转发 | 客户端逐层 `GetChildAtIndex` 下钻 |
| 事件通道 | WinEvent（系统） | WinEvent → NSAccessibility 通知 | NotifyWinEvent → AT-SPI D-Bus 信号 |

Linux 对象路径编码（SAtSpi）：

- 应用根：`/org/a11y/atspi/accessible/root`
- 顶层窗口：`…/w<hwnd-16进制>`
- 窗口客户区根：`…/w<hex>_c`
- 控件链节点：`…/w<hex>_c_<i>_<j>…`

**分隔符必须用 `_`**：路径解析用 `strtoull(base 16)` 还原 hwnd，
若用字母后缀（如早期版本的 `c`）会被当十六进制数字吞进句柄，
产生 `w1800002c → w1800002cc → …` 的"无限单子链"假树（见 §5.9）。

## 3. SOUI 语义层要点

- `SAccessible::accHitTest` 的子遍历必须**逆序**（`GSW_LASTCHILD →
  PREVSIBLING`，最上层优先），与 `SwndFromPoint` 的 z 序一致。
  正序遍历时垫底的全窗口背景容器会抢答，导致 VoiceOver 悬停
  只能命中最顶层 caption（macOS 联调时的实症）。
- `accLocation` 依赖 `ClientToScreen(GetHostHwnd())` 折算屏幕坐标，
  即依赖 swinx 维护的窗口矩形（`wndObj->rc`）。

## 4. macOS 桥（SNsAccessibility.mm）

- **命中链路**：VoiceOver 鼠标跟随沿 app → window(Host) →
  view(SNsWindow) → 壳元素四级调用 `accessibilityHitTest:`
  （屏幕坐标，Cocoa point，原点左下）。AppKit 按 frame 预筛选后才调用。
- **AXParent 锚点契约**：AppKit 解析元素属性时沿
  `accessibilityParent` 上溯找原生锚点（NSView/NSWindow/NSApp）。
  祖先链中途断在 nil 非锚点元素 → 解析失败 → VoiceOver 无限重试
  属性查询 → 主线程被 MIG 查询洪水钉死（假死）。对策：
  `SwinxNsAccAttachRootParent` 幂等挂接 + `accessibilityParent`
  nil 时返回 NSApp 兜底。
- **重入防护**：hit test 是进程首个 AX 查询时的常见入口，需
  `s_accHitTestDepth` 防护；frame 契约兜底（结果 frame 外扩 2pt
  不含查询点则回退根元素）。
- **事件节流**：WinEvent 钩子 50ms 节流（只丢通知不丢缓存失效）。
- **与消息循环无关**：AX 查询走独立 XPC/MIG 通道，不经过
  NSEvent 队列/sendEvent——自定义消息循环和鼠标事件特殊派发
  均与其无关（排障时走过的弯路）。
- VoiceOver 行为坑：默认鼠标跟踪是 "Follows VO cursor"，需在
  VO+F8 → Navigation 开 "Moves VoiceOver cursor"；开启后默认
  "键盘焦点与 VO 光标同步" 会把鼠标扫过的后台窗口拉前台
  （失活振荡，非 bug，需取消同步）。

## 5. Linux 桥（SAtSpi.cpp）

### 5.1 线程模型

- 桥的 `Pump` 由 `SetTimer(0, 50ms)` 驱动，经 `SConnection::peekMsg`
  的 WM_TIMER 派发——**Pump 与所有 D-Bus 处理器都跑在 UI 线程**。
- **主线程发信号只入队**（`m_sigQueue`），由 `DrainSignals` 在泵的
  派发间隙统一构造/发送/flush。此前主线程持窗口锁时直接
  `dbus_connection_send`，与泵线程"持 libdbus 连接锁派发 → 取窗口锁"
  形成 AB-BA 死锁（tooltip 枚举卡死的根因）。
- Pump **派发期间不持有 `m_mutex`**（注册检查时短暂持锁）：
  处理器经 MSAA → `SendMessage(WM_GETOBJECT)` → `LresultFromObject`
  → `EnsureInit` 的重入路径需要能再拿该锁。
- **重入防御**：处理器栈内任何路径再进 `peekMsg` 触发同一 WM_TIMER
  会对同一 DBusConnection 嵌套 `read_write_dispatch`（libdbus 连接
  锁自锁），`thread_local s_inPump` 直接跳过。

### 5.2 泵吞吐与时间预算

- `read_write_dispatch` 每次调用只派发一条消息，且回复要等下一次
  调用才写出 socket——只靠定时器驱动时同步客户端每个请求-应答跨
  两个 tick（~10-20 请求/秒），枚举大子树表现为"卡死"。
  Pump 必须**循环泵空 socket 并对每条派发 flush**；socket 上暂时
  无后续请求时 `poll(5ms)` 再确认（同步客户端收到应答后过一小会儿
  才发下一个请求，`timeout=0` 会错过它）。
- **单轮 20ms 时间预算**：accerciser 启动会自动枚举整棵桌面树，
  请求流持续不断；没有预算时泵循环一路跑满上限，UI 线程被连续
  霸占，应用界面整个冻结（"双进程卡死"的直接机制）。预算用尽即
  交还主线程处理输入/绘制，50ms 定时器下一轮再续。

### 5.3 对象路径与 fallback 注册

- fallback 注册必须覆盖 **`/org/a11y/atspi` 全子树**：`Cache.GetItems`
  的标准路径是 `/org/a11y/atspi/cache`，只注册 `/accessible` 时
  cache 请求落不到任何处理器，而 libdbus 对未处理的方法调用不保证
  自动回错——客户端永久等应答，且我方日志零记录（盲区级卡死）。
- 解析失败的路径、未知接口一律**显式 `ReplyError`**
  （UnknownObject/UnknownMethod），保证任何到达桥的请求必有应答。

### 5.4 顶层窗口枚举（本地候选表）

窗口映射是 wndobj.cpp 的文件私有数据，不为单一消费者在 WndMgr 上
加公开枚举接口；桥在 SAtSpi.cpp 内自维护候选表：

- **候选集 = EnsureInit 时的 `EnumWindows` 种子**（覆盖 AT 钩子
  安装前已存在的窗口——钩子只接收注册之后发出的事件）**∪ 此后
  WinEvent（CREATE/SHOW）携带的宿主窗口**（去重）。
- **入口必须按进程过滤**：`EnumWindows` 走 X 根窗口枚举，回调会
  收到桌面上所有进程的顶层窗口；而 swinx 的 `IsWindow`（对外来
  窗口做 `xcb_get_geometry`，存在即真）、`IsWindowVisible`（返回
  X 的 VIEWABLE）、`GetParent`（fromHwnd 落空返回 0）对它们全部
  放行——不过滤则 root 的子列表被整个桌面污染，枚举实际不可用。
  判据用 `GetWindowThreadProcessId(hwnd, NULL) != 0`：窗口不在本
  进程映射中时返回 0，纯 map 查找、零 X 往返。
- **读取时逐项实时校验**（`IsWindow` + `IsWindowVisible` +
  `GetParent==0`）并剔除失效候选。销毁/隐藏/层级变化都按调用瞬间
  的实时状态判定，不依赖事件不丢失。
- **空表自愈**：候选表为空时重新播种（节流 1s）——种子可能早于
  首个窗口登记（首次 NotifyWinEvent 的时机早于宿主窗口可见）。
- 效果：tooltip 平时隐藏（`WS_VISIBLE` 关闭）天然被过滤，符合
  AT-SPI 不暴露不可见窗口的语义；窗口显示期间正常出现在树中。
- 弃用的旧路径：`EnumWindows` 原实现对 X 根窗口做
  `xcb_query_tree` 全系统枚举 + `_findAppChild` 嵌套往返 +
  逐窗口属性查询，root 的 ChildCount 高频触发时是卡顿源。
  （注：`EnumWindows` 的 X 树后端仍被 FindWindowEx/窗口销毁链
  复用，本次未动。）

### 5.5 事件桥（NotifyWinEvent → AT-SPI 信号）

- 钩子是全局钩子，**必须按进程 id 过滤只桥接本进程窗口**。
- `ChildrenChanged` 只对**顶层窗口**的增删广播（信号发到 root 路径，
  载荷必须带被增删子对象的 `(so)` 引用与索引；libatspi 收到残缺
  载荷会丢弃或据其建出坏树），按 hwnd 200ms 防抖。内部控件增删由
  客户端展开时 `GetChildren` 实时枚举获得。
- `LocationChange` 等高频/未知事件不广播（噪声），位置/名称变化由
  客户端按需拉取。
- 事件签名 `siiva{sv}`：`StateChanged` any 载荷为 `i`；
  `PropertyChange`/`Focus`/`Window` 为 `s`；`ChildrenChanged` 为
  `(so)`。**libdbus 对 `DBUS_TYPE_STRING`/object path 的
  `append_basic` 要求传 `const char **`（指向指针的指针）**——误传
  字符串指针本身会把字面量前 8 字节解引用成 `char*`，`strlen` 段
  错误（进程退出时崩溃的根因）。

### 5.6 坐标：以 X 服务器为可信源

- `QueryXWindowOrigin` 用 `xcb_translate_coordinates`（src=窗口，
  dst=root）直查窗口真实原点（200ms 缓存）。
- 窗口节点：`GetWindowRect` 结果与 X 原点不一致时按 X 校正，并
  节流 WARN（`window origin out of sync`）——该 WARN 是 swinx 核心
  窗口矩形同步 bug 的直接证据。
- 控件节点：`accLocation` 结果不在"X 修正后的窗口矩形"内、却落在
  "客户区尺寸@原点"内时，按窗口真实原点校正，节流 WARN
  （`accLocation missing window origin`）。

### 5.7 总线连接与晚就绪

- 地址获取：`AT_SPI_BUS` env → `org.a11y.Bus.GetAddress`。
- `ConnectBus`/`Socket.Embed` 失败（bus/registry 晚于应用就绪）时
  以 500ms 慢速定时器重试，成功后换 50ms 正常泵；Embed 重试节流 3s。

### 5.8 诊断日志约定

正常运行只保留异常路径 WARN（ConnectBus/Embed 失败、注册失败、
不可解析路径、窗口原点失同步、accLocation 校正触发）；联调期的
逐调用日志（`dbus in/out,#seq`、`NodeScreenRect,ctrl` 等）已移除，
需要时按上述关键字模式临时回加。

### 5.9 客户端行为备忘（libatspi 源码核实）

- 路径一律逐字使用消息中的 `(so)`，客户端**不合成路径**——
  "无限展开"必是服务端返回了可解析的坏路径。
- `children-changed` 事件 any 载荷需 `(so)`，否则缓存不更新。
- 事件源会经 `_atspi_ref_accessible(sender, signal_path)` 在客户端
  建对象。
- accerciser 高亮直接取 `GetExtents(ATSPI_COORD_TYPE_SCREEN)`；
  其对 GTK4 应用走窗口相对坐标回退——高亮位置对多进程元素恒定
  不动是 accerciser 自身的显示 bug（已用 vscode 等对照确认）。

## 6. 排障备忘（经验教训索引）

1. **accHitTest z 序**：子遍历逆序，与 `SwndFromPoint` 一致（§3）。
2. **macOS AXParent 锚点**：祖先链断在非锚点元素 → AX 查询洪水假死；
   AttachRootParent + NSApp 兜底（§4）。
3. **AX 通道独立性**：不走 NSEvent/sendEvent，别往消息循环找（§4）。
4. **D-Bus 字符串变体契约**：`append_basic` 传 `const char **`（§5.5）。
5. **ChildrenChanged 载荷**：`(so)` + 索引，只广播顶层增删（§5.5）。
6. **泵吞吐与时间预算**：循环泵空 + flush + 20ms 预算（§5.2）。
7. **线程模型与死锁**：信号入队 + Pump 派发期间放锁（§5.1）。
8. **对象路径编码歧义**：分隔符用非十六进制字符 `_`（§2）。
9. **cache 路径黑洞**：fallback 覆盖全子树 + 显式回错（§5.3）。
10. **顶层枚举语义**：本地候选表 + 读取时实时校验；隐藏窗口不暴露（§5.4）。
11. **坐标可信源**：X `translate_coordinates` 直查校正（§5.6）。

## 7. 验证方法速查

- **Linux 环境**：`sudo apt install at-spi2-core orca accerciser`；
  `gsettings set org.gnome.desktop.interface toolkit-accessibility true`；
  以 `-DSOUI_ENABLE_ACC=ON` 构建；桌面会话内运行 fun_test。
- **accerciser**：左侧树应列出 SOUI 窗口 → client → 控件树，
  选中元素有位置高亮（大小正确；位置恒定问题见 §5.9）。
- **快速验证桥是否注册**：`dbus-send` 向 a11y bus 查询，或观察
  accerciser 是否出现 "swinx application" 根节点。
- **日志判读**：出现 `EnsureInit,ConnectBus failed` 反复重试 →
  会话无障碍总线未起（检查 toolkit-accessibility / at-spi2 状态）；
  出现 `window origin out of sync` → swinx 核心窗口矩形同步失真，
  按 WARN 中的 `wndMgrOrigin` vs `xOrigin` 差值定位。
