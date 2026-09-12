# swinx 无障碍（Accessibility）胶水架构

> 适用范围：swinx 的 MSAA（IAccessible/oleacc）接口面，以及 macOS（NSAccessibility）、
> Linux（AT-SPI2/D-Bus）两个平台的系统无障碍桥接。

## 1. 设计原则

**swinx 是胶水层，不持有 IAccessible。**

系统的每次无障碍访问，都按下面的链路"现查现用现放"：

```
系统 AT（VoiceOver / Orca / accerciser）
   │  调平台相关的 acc 接口
   ▼
swinx 平台胶水模块
   ├─ macOS：SNsAccessibility.mm（NSAccessibility 元素）
   └─ Linux ：SAtSpi.cpp（AT-SPI2 D-Bus 对象）
   │  每次属性/动作访问都重新发查询
   ▼
SendMessage(hwnd, WM_GETOBJECT, 0, OBJID_xxx)     ← swinx 的 HWND
   ▼
SHostWnd::OnGetObject()（SOUI/src/core/shostwnd.cpp）
   ├─ 未开 SOUI_ENABLE_ACC：SetMsgHandled(FALSE) 并返回 0（= NULL）
   └─ 已开：GetRoot()/SWindow::GetAccessible() → LresultFromObject(...)
   ▼
胶水层拿到 IAccessible → 直接调用 get_accXxx / accDoDefaultAction → Release
```

要点：

1. **接口的所有者始终是 SOUI**（`SWindow::m_pAcc`，随 SWindow 生命周期）。
   swinx 里没有任何长期持有 `IAccessible*` 的地方——控件销毁后，下一次查询要么
   返回 NULL，要么解析失败退化成空信息，绝不会访问悬垂 COM 指针。
2. **编译开关集中在 SHostWnd**：`SOUI_ENABLE_ACC` 关闭时 `WM_GETOBJECT` 返回 0，
   胶水层各入口对此自然退化为"没有可访问对象"，无需各自再加开关。该宏由根
   CMakeLists 的 `option(SOUI_ENABLE_ACC)` 经 `add_definitions` 全局定义，SOUI
   与 swinx 共用同一个宏。
3. **平台桥的初始化时机**：Linux 的 AT-SPI 桥在 `SConnMgr::instance()` 首次
   创建进程单例**完成后**主动初始化（`SOUI_ENABLE_ACC` 闸门内；不能放在
   SConnMgr 构造函数里——桥内 `SetTimer(NULL,...)` 会重入 `instance()`，与
   静态局部变量的初始化守卫（`__cxa_guard`，不可重入）冲突，曾致启动死锁；
   `SwinxAtSpiInit` 另有同线程重入防御，覆盖 lazy 链先于 instance() 的场景），
   `oleacc.cpp` 的 `LresultFromObject`/`NotifyWinEvent` 保留惰性兜底；macOS
   桥在首次 `WM_GETOBJECT`/事件时惰性初始化。开关关闭时桥完全不启动
   （不连 D-Bus、不占 timer）。
4. **元素身份 = (hwnd, MSAA child id 链)**，两个平台桥共用同一模型：
   - macOS：`SwinxAccElement` 只存 `(hwnd, chain)`，不存接口指针；
   - Linux：D-Bus object path 确定性编码 `w<hwnd>c_1_3`，等价于 (hwnd, chain)。

## 2. swinx 侧的 MSAA 内核（src/oleacc.cpp + include/oleacc.h）

`include/oleacc.h` 保持与 Windows SDK oleacc.h **完全相同的 API 面，无 swinx 扩展**：

- `AccessibleObjectFromWindow()` — 向窗口 `SendMessage(WM_GETOBJECT)`，再
  `ObjectFromLresult` 解析，与 Win32 语义一致。
- `LresultFromObject()` — **弱登记**句柄表：只记 (句柄 → {对象指针, wParam}) 映射，
  不 AddRef。行为与真实 oleacc 对齐（Windows 本机实测）：每次调用产生新句柄
  （表设容量上限，超限淘汰最旧句柄）；失败以 HRESULT 作 LRESULT 返回
  （null → E_INVALIDARG、QI 失败 → E_NOINTERFACE，并非文档描述的 0）；
  句柄绑定 WM_GETOBJECT 的 wParam，服务端必须原样回传（SHostWnd::OnGetObject
  即如此），ObjectFromLresult 时 wParam 不匹配 → E_FAIL。弱表语义使陈旧句柄
  自然失效（无引用可释放，也没有"撤销 LRESULT"的 API）。
- `NotifyWinEvent()` + 标准 `SetWinEventHook()` / `UnhookWinEvent()`（声明在
  winuser.h，语义对齐 user32：支持多钩子并发、事件区间过滤、idProcess/idThread
  限定、WINEVENT_SKIPOWNPROCESS/SKIPOWNTHREAD）— 事件先入队，钩子回调由消息泵
  （sysapi.cpp 的 GetMessage/PeekMessage 调 `SwinxDispatchPendingWinEvents`）
  异步派发，与 user32 的 OUTOFCONTEXT 钩子投递时机一致；SKIP 钩子注册成功但
  收不到本进程/注册线程的事件。接收钩子在**事件发出那一刻**确定（入队时快照
  匹配的钩子）：零匹配事件立即丢弃、不滞留队列，之后注册的钩子永远收不到
  此前发出的事件（user32 语义；曾因把匹配推迟到派发时、叠加 AT-SPI 桥的全
  区间钩子导致事件泄漏给晚注册钩子，leak.log 捕获后修正）。
- `OBJID_*` 常量（LP64 兼容）：oleacc.h 用 `((LONG)0xFFFFFFFC)` 定义而非
  WinSDK 的 `0xFFFFFFFCL`——后者在 64 位 Linux/macOS 上 long 为 64 位，
  常量变成正数 4294967292，服务端 `(LONG)lParam == OBJID_CLIENT` 永远
  不成立（真实 Windows 上该值是 -4）。`AccessibleObjectFromWindow` 发
  WM_GETOBJECT 时对 OBJID 做符号扩展，与真实 oleacc 客户端上线路径一致。
- `AccessibleObjectFromEvent()` — 与 Win32 同路径：经
  `AccessibleObjectFromWindow`（WM_GETOBJECT）解析 (hwnd, idObject) 处对象，
  `pvarChild` 回带事件携带的 child id。
- 平台胶水共享助手 `SwinxAccResolvePath(hwnd, chain, cChain, &acc, &childId)` —
  **内部实现细节，不进公共 API 面**，声明在 `src/SwinxAccGlue.h`：从
  OBJID_CLIENT（回退 OBJID_WINDOW）根出发沿 child 链逐级下钻，输出
  `(acc, childId)`（CHILDID_SELF = 完整对象；否则 acc 为父对象 + 简单元素 id）。
  调用方负责 Release。

## 3. macOS 桥（src/platform/cocoa/SNsAccessibility.mm）

**编译约束（macOS 特有，改动 .mm/.h 时必须遵守）**：

- **Cocoa 必须先于 swinx 头导入**。swinx 的 gdi.h 有 `#define Polygon
  Polygon_Priv`（避开 Quickdraw 的 `typedef MacPolygon Polygon`），若它在
  Cocoa 之前定义，Quickdraw.h 会被宏破坏而编译失败。SNsWindow.mm 与
  SNsAccessibility.h 都遵守"Cocoa 第一"的顺序（Cocoa 自带 include 守卫，
  重复 `#import` 是空操作）。
- swinx 的 COM 头（basetyps.h）把 `interface` 宏定义为 `struct`，而 macOS
  SDK 头文件用到 `interface`——进入 ObjC 世界前必须 `#undef interface`
  （SNsWindow.mm / SNsAccessibility.h 均已处理）。


- `SwinxAccElement : NSAccessibilityElement` 是"身份壳"：属性 getter 全部经
  `SwinxAccResolvePath` 实时查询（role/title/value/help/frame/enabled/focused/
  hidden/PerformPress），ARC 与 COM 互不掺和——壳里没有 COM 引用。
- 壳按需构建并按 (hwnd, chain) 复用，维持 VoiceOver 需要的元素对象身份稳定；
  结构变化事件只丢弃壳缓存，零成本。
- `SNsWindow`（contentView）的 `-accessibilityChildren` 返回根壳，事件经标准
  `SetWinEventHook` → dispatch 主队列 → `NSAccessibilityPostNotification`。
- 坐标：`accLocation` 的 swinx 像素矩形经 `SNsCoord.h` 换算为 Cocoa 屏幕 rect。
- 文本：swinx 的 `WCHAR = wchar_t`（4 字节 UTF-32），BSTR 转 NSString 用
  `NSUTF32LittleEndianStringEncoding`。

## 4. Linux 桥（src/platform/linux/SAtSpi.cpp）

- 对象树经 a11y bus（`AT_SPI_BUS` 或 org.a11y.Bus）以 AT-SPI2 协议暴露：
  `Socket.Embed` 注册 + Accessible/Component/Action/Application/Cache/Properties
  接口，事件为 `org.a11y.atspi.Event.*` 信号。
- 不开线程：`SetTimer(NULL, 0, 50, proc)` 把 D-Bus 分发挂进 swinx 消息循环，
  处理请求时可直接走 MSAA 同步查询。
- 细节见《swinx-Linux无障碍(AT-SPI)桥接.md》。

## 5. 已知限制 / 待办

- SOUI 的 `accNotifyEvent` 用 `idObject` 传 SWND，胶水层无法反查到具体元素，
  事件统一归因到窗口根节点（属性本身实时查询，只影响通知粒度）。
- AT-SPI 侧 Text/Value/Selection/Table 扩展接口未实现（文本控件不能逐字朗读）。
- 非当前开发机无法编译 Cocoa/Linux 代码，两桥均需在目标平台
  `-DSOUI_ENABLE_ACC=ON` 下实测（macOS: VoiceOver；Linux: accerciser/Orca）。
