# swinx macOS 基于 HWND 的跨进程 IPC —— 可行性调研

> 调研对象：`D:\work\soui4\swinx`
> 结论一句话：**照搬"HWND 即跨进程窗口句柄"的现有设计在 macOS 上不成立**（HWND 是进程内对象指针），
> 但**基于 HWND 语义（窗口寻址 + 消息路由）实现 IPC 是可行的**，且 swinx 现有的共享内存 / 具名对象 /
> fd 消息泵三层设施已具备，只需补齐"全局窗口标识 + 投递通道 + 接收端"三处。

---

## 1. 问题定义

"基于 HWND 实现 IPC" 在当前代码里是一条**已经跑通的完整链路**，其判定入口在
`src/wnd.cpp:1442-1452`：

```cpp
SConnection *connCur = SConnMgr::instance()->getConnection();
WndObj pWnd = WndMgr::fromHwnd(hWnd);
if (!pWnd)
{ // not the same process. send ipc message
    ...
    SharedMemory *shareMem = PostIpcMessage(connCur, hWnd, msg, wp, lp, hEvt);
```

语义是：**HWND 在本地窗口表里查不到 → 认定它属于别的进程 → 走 IPC 通路**。整个上层协议
（消息打包、`WM_COPYDATA` 载荷、返回值回传、`SendMessageTimeout` 超时）都已实现，与平台无关。

因此"macOS 上的可行性"本质是问三件事：

1. 能不能拿到并识别一个**属于其它进程**的 HWND；
2. 能不能把消息**投递**到那个进程；
3. 那个进程能不能**接收并路由**到对应窗口。

---

## 2. 源码事实：HWND 在各平台的真实语义

`include/ctypes.h:223` 统一定义了 `typedef UINT_PTR HWND;`，但各平台往里装的东西完全不同：

| 平台 | HWND 实际承载 | 生成处 | 跨进程可见 |
|---|---|---|---|
| Windows | 系统窗口句柄 | 系统 SDK | ✅ 系统级 |
| **Linux** | **X11 窗口 ID（`xcb_window_t`）** | `linux/SConnection.cpp:1413` `HWND hWnd = xcb_generate_id(connection);` | ✅ **X 服务器端资源 ID** |
| **macOS** | **`SNsWindow` 对象指针** | `cocoa/SNsWindow.mm:435` `m_hWnd = (HWND)(__bridge_retained void *)self;` | ❌ **进程内地址** |
| Android/OHOS | 由外部 ID 注册得来 | `mobile/SConnection.cpp:151` `RegisterVirtualHWND` | ❌ 映射表在本进程 |

本地查找一律依赖进程内静态表（`src/wndobj.cpp:133`）：

```cpp
static std::map<HWND, _Window *> s_wndMap;
static std::recursive_mutex s_wndMapMutex;
```

**Linux 之所以能跨进程，是因为它的 HWND 恰好是 X11 窗口 ID —— 一个由 X 服务器分配、
全局唯一的资源编号。** 这不是 swinx 设计的功劳，而是 X11 协议的天然属性。

macOS 上 `m_hWnd` 是 `__bridge_retained` 过的 Objective-C 对象指针，生命周期与窗口绑定
（`SNsWindow.mm:466-467` dealloc 时 `s_hWndMgr.remove` + `CFBridgingRelease`）。
**指针在另一个进程的地址空间里没有任何意义。**

---

## 3. 现有 IPC 通路：Linux 侧完整走查（作为对照基准）

| 环节 | 实现 | 位置 |
|---|---|---|
| 生成全局唯一消息 ID | `IpcMsg::gen_suid` → 8 字节 uuid | `src/uimsg.h:266-273` |
| 载荷落地 | `SharedMemory::init(shm_name, bufLen)`（`shm_open`） | `src/wnd.cpp:1354-1389` |
| 打包布局 | `struct MsgLayout { ret, wp, lp }` + `WM_COPYDATA` 时追加 `cbData`/`lpData` | `src/uimsg.h:17-21`、`wnd.cpp:1357-1376` |
| 跨进程信号 | `CreateEventA(nullptr, FALSE, FALSE, evtName)` 具名事件 | `src/wnd.cpp:1391-1394` |
| **投递** | `SendClientMessage(hWnd, GetIpcAtom(), data, 5)` → `xcb_send_event` 发 `WM_WIN4XCB_IPC` | `linux/SConnection.cpp:4002-4017`、`linux/atoms.h:93,188` |
| **接收** | 事件循环收到 `XCB_CLIENT_MESSAGE` 且 type 匹配 → `new IpcMsg(e2->window, e2->data.data32)` | `linux/SConnection.cpp:2792-2799` |
| 打开复用 | `CreateEventA(同名)` + `SharedMemory::init(name, 0)`（打开既有对象） | `src/uimsg.cpp:59-70` |
| 结果回传 | 写入 `MsgLayout::ret` + 置位具名事件 → 发送方 `waitMutliObjectAndMsg` 唤醒 | `src/wnd.cpp:1490-1499` |

**关键点：跨进程那一跳（投递 + 接收）完全依赖 X 服务器**。共享内存与具名事件只是为了
传载荷和回结果，它们本身已经是跨进程可用的。

---

## 4. macOS 缺失的环节（逐项确认）

| # | 环节 | 状态 | 证据 |
|---|---|---|---|
| 1 | 投递：`SendClientMessage` | **空实现** | `cocoa/SConnection.mm:1540-1542` `// Empty implementation` |
| 2 | IPC 消息类型 atom | **返回 0** | `cocoa/SConnection.mm:1544-1547` |
| 3 | 接收：事件循环 IPC 分支 | **不存在** | cocoa 消息泵无 `IpcMsg` 构造路径 |
| 4 | 跨进程窗口枚举 | **不存在**，仅遍历本进程 | `cocoa/SConnection.mm:1185-1207` `OnEnumWindows` → `[NSApp windows]` |
| 5 | 跨进程窗口标识 | **不存在** | HWND = 本进程 `SNsWindow*` |

对比 mobile 层（`mobile/SConnection.cpp:864-871`）也是同样的空桩，且 iOS 亦然 ——
**目前只有 Linux 一条腿是通的。**

---

## 5. 三个根本障碍 + 一个安全隐患

### 障碍 1：寻址 —— macOS 没有跨进程窗口 ID 命名空间

- Windows：`FindWindow` 能在**别的进程**里找到窗口（窗口管理器是系统级的）。
- Linux：X11 窗口 ID 全局唯一，`EnumWindows` 可跨进程。
- macOS：`[NSApp windows]` 严格等于"本 App 自己的窗口"。要看到别的 App 的窗口，只能走
  - `CGWindowListCopyWindowInfo`（系统级，但**只读**窗口信息，不能发消息）；
  - Accessibility API `AXUIElement`（可操作，但需要用户授予**辅助功能权限** TCC）。

**结论：macOS 上不存在"只凭一个编号就能向任意进程窗口投递消息"的系统能力。**

### 障碍 2：投递 —— 缺少与 X 服务器对等的路由器

macOS 没有"窗口服务器代发消息"的机制。可选项全部是**显式的进程间通道**：

| 原语 | 有 fd | 可同步可异步 | 沙箱/公证 | 与现有泵集成 |
|---|---|---|---|---|
| UNIX domain socket | ✅ | ✅ | 良好 | **最顺**（现有 `setupFDMonitoring`） |
| Mach port / `CFMessagePort` | 需转换 | ✅ | App Group 受限 | 需 `DISPATCH_SOURCE_TYPE_MACH_RECV` |
| `NSDistributedNotificationCenter` | ❌ | 仅异步广播 | 受限 | 需另开泵，语义弱 |
| `XPC` | — | 仅同团队/沙箱 | 面向服务 | 重，不适合窗口消息 |
| 复用现有具名 FIFO + 共享内存 | ✅ | ✅ | 良好 | **零新增原语** |

### 障碍 3：接收 —— cocoa 事件循环里没有注入点

需要在 `cocoa/SConnection.mm` 的消息泵里增加一条 IPC 分支，把外部消息转成 `IpcMsg`
并入 `m_msgQueue`。好消息是**泵本身已经支持 fd 等待**（见第 6 节），接入成本不高。

### 安全隐患：指针别名（必须优先消除）

因为 HWND 是**裸指针**，跨进程传递后存在 ABA/别名风险：

> A 进程把窗口指针 `0x7f...` 发给 B；若 B 进程恰好在同一地址分配了 `SNsWindow`，
> 那么 B 里 `WndMgr::fromHwnd(hWnd)` **查找成功**，于是 `if (!pWnd)` 判定为"本地窗口"，
> 消息被**错误地投给本地窗口**，而不是走 IPC。

也就是说，当前"查不到就说明是别的进程"这个判定，在 macOS 上**既不充分也不可靠**。
实现前必须先引入与指针解耦的全局窗口标识。

---

## 6. 可复用资产（这是可行性偏乐观的原因）

| 层 | 资产 | 位置 | macOS 现成可用 |
|---|---|---|---|
| 协议层 | `MsgLayout` / `IpcMsg` / `PostIpcMessage` / `WM_COPYDATA` 打包 / 超时回传 | `uimsg.h`、`uimsg.cpp`、`wnd.cpp` | ✅ 纯逻辑，平台无关 |
| 共享内存 | `SharedMemory`（`shm_open` + ftruncate + 信号量 + fcntl 锁） | `sharedmem.h`、`sharedmem.cpp:317-325` | ✅ 非 Android 分支即 `shm_open`，macOS 原生支持 |
| 具名对象 | `NamedEventObj` / `NamedWaitbleObj`（全局句柄表 + 具名 FIFO） | `sysobjs.cpp:508+`、`1770-1798` | ✅ 全是 POSIX，且 `src/*.cpp` 已在 `macos.cmake` 编译 |
| 消息泵 | `waitMutliObjectAndMsg` 用 `dispatch_source` 监听 **fd**，就绪后合成 `kFDReadyEventType` 注入 NSApp 队列 | `cocoa/SConnection.mm:475-556` | ✅ **接新通道的天然挂点** |
| fd 抽象 | `_SynHandle::getReadFd()` | `synhandle.h:41` | ✅ |
| 外部 ID→HWND 先例 | `RegisterVirtualHWND(externalId, ...)` | `mobile/SConnection.cpp:151`、`wnd.cpp:3982` | ✅ 可借鉴为全局 ID 注册 |

**`macos.cmake:15-20` 的 `file(GLOB SRCS src/*.cpp ...)` 已经把 `sysobjs.cpp` / `sharedmem.cpp` /
`uimsg.cpp` / `wnd.cpp` 全部编入 macOS 构建** —— 意味着这套跨进程基础设施在 macOS 上是
**"能编译、且大概率能跑"** 的，缺的只是最外层那一跳。

---

## 7. 可行性判定（分场景）

| 场景 | 可行性 | 说明 |
|---|---|---|
| **同族进程互发消息**（两个 swinx/SOUI 应用，一方已知对方的全局窗口 ID） | ✅ **可行** | 复用现有 shm + 具名事件协议，新增投递通道即可 |
| **跨进程 `FindWindow` 按类名/标题查找** | ⚠️ **可行但需新增注册表** | 需维护跨进程窗口注册表（shm 全局表 / 各进程自注册） |
| **向任意第三方 macOS 应用窗口发消息** | ❌ **不可行（且不合规）** | 无系统路由；只能靠 Accessibility + TCC 授权，且第三方不会实现 swinx 的 WndProc |
| **发送方仅在 macOS，接收方在 Linux** | ❌ 不做 | 协议不含跨 OS 传输，需另立标准 |

**综合结论：可行性 = 中高，但前提是把"HWND 即跨进程句柄"改为"全局窗口 ID ↔ 进程内 HWND"的映射。**

---

## 8. 推荐方案

### 8.1 分层设计

```
应用层   SendMessage/PostMessage/FindWindow/EnumWindows   ← 接口不变
   │
协议层   MsgLayout / IpcMsg / WM_COPYDATA / 超时回传        ← 已有，几乎不改
   │
寻址层   【新增】全局窗口 ID 注册表
   │     · GlobalWndId = { pid, 进程内序号 } 或 uuid
   │     · 本地: hwnd → GlobalWndId ；全局表: GlobalWndId → {pid, 类名, 标题}
   │     · 载体: 复用 SharedMemory 全局表(参照 GLobalHandleTable)
   │     · 判定改为"先查全局表，再决定本地派发 or IPC"，消除指针别名
   │
传输层   【新增】投递通道（三选一，见 8.2）
   │
同步层   SharedMemory + 具名事件(FIFO)                      ← 已有，直接复用
```

### 8.2 传输通道选型

| 方案 | 新增代码量 | 优点 | 缺点 | 建议 |
|---|---|---|---|---|
| **A. UNIX domain socket** | 中 | 有 fd，直接挂进现有 `dispatch_source` 泵；可靠、可控流；权限可收敛（`0600` + 目录） | 需管理监听 socket 与连接映射 | ✅ **首选** |
| B. 复用具名 FIFO + 共享内存 | 小 | 零新增原语，与命名对象体系同构 | FIFO 语义弱、需自行处理多发送者并发 | 可用于最小验证 |
| C. `CFMessagePort` | 中 | Apple 原生，run-loop 集成 | 集成需转 dispatch source；沙箱下命名受限 | 次选 |

### 8.3 建议的落地顺序

1. **先消除指针别名**：引入 `GlobalWndId`，把 `wnd.cpp:1443` 的判定从"查不到就 IPC"
   改为"查不到 → 查全局注册表 → 按 pid 决定"。**这一步不管做不做 IPC 都应该做**，
   因为现有判定在 macOS 上存在误判风险。
2. **做同族进程最小闭环**：单机两进程、已知窗口 ID，用 UNIX socket 投递一条
   `WM_COPYDATA`，跑通 `SendMessage` 同步往返（复用现有 shm + 具名事件）。
3. **补跨进程 `FindWindow`/`EnumWindows`**：接入全局注册表。
4. 视需要再评估 Accessibility 增强（要知道第三方窗口存在感时才需要，且必须处理 TCC 授权）。

---

## 9. 风险与约束

| 风险 | 影响 | 应对 |
|---|---|---|
| App Sandbox 开启 | socket/shm 路径受限，Mach 名需 App Group | 明确是否上架 Mac App Store；定义"同族进程"为同 Team 签名 |
| Hardened Runtime / 公证 | 影响 Mach port 与部分 IPC 权限 | 优先 UNIX socket（依赖文件系统权限，最易通过） |
| 辅助功能权限（TCC） | 无法枚举/操作第三方 App 窗口 | 仅在实际需要时申请，并明确降级行为 |
| 指针别名 | 消息误投本地窗口 | 第 1 步先解决 |
| 安全面扩大 | 监听 socket 成为攻击面 | `0600` 权限 + 校验对端 pid/签名 + 消息长度上限 |
| iOS 同样空缺 | iOS 上跨进程 IPC 基本不可用 | iOS 保持不支持，明确文档化 |

---

## 10. 与既有路线图的衔接

`swinx/doc/swinx-总体评估报告-2026-09-10.md:79,96`、`2026-09-12.md:96`、`2026-09-13.md:460-461`
已把 **「macOS 跨进程 HWND IPC 空缺」** 记为已知项（低优先级），并提到可用
"D-Bus / DistributedObjects 等价实现"补齐。本次调研的补充是：

- 明确它是**架构性空缺**（寻址层缺失），不是"补一个函数"能解决的；
- 明确指出可直接复用 `SharedMemory` / `NamedWaitbleObj` / fd 消息泵三层，**无需重造基础设施**；
- 建议最低成本的等价实现是 **UNIX domain socket**，而非 Distributed Objects。

---

## 附：关键源码索引

| 主题 | 位置 |
|---|---|
| HWND 类型定义 | `include/ctypes.h:223` |
| 进程内窗口表 | `src/wndobj.cpp:133-190` |
| IPC 分支判定 | `src/wnd.cpp:1442-1452` |
| IPC 消息打包 | `src/wnd.cpp:1346-1401` |
| `SendMessage` 超时/等待 | `src/wnd.cpp:1424-1607` |
| 载荷布局 | `src/uimsg.h:17-21`；`IpcMsg` 282 行起 |
| 接收端打开共用对象 | `src/uimsg.cpp:53-80` |
| 共享内存实现 | `src/sharedmem.h:415+`；`src/sharedmem.cpp:317-325` |
| 具名事件/等待对象 | `src/sysobjs.cpp:508+`、`744`、`1770-1798` |
| fd 抽象 | `src/synhandle.h:41` |
| **macOS HWND 生成** | `src/platform/cocoa/SNsWindow.mm:435-436`、`466-467` |
| **macOS 投递/atom 空桩** | `src/platform/cocoa/SConnection.mm:1540-1547` |
| **macOS 跨进程枚举缺失** | `src/platform/cocoa/SConnection.mm:1185-1207` |
| macOS fd 消息泵挂点 | `src/platform/cocoa/SConnection.mm:475-556` |
| Linux 投递（对照） | `src/platform/linux/SConnection.cpp:4002-4017` |
| Linux 接收（对照） | `src/platform/linux/SConnection.cpp:2792-2799` |
| mobile 空桩 | `src/platform/mobile/SConnection.cpp:864-871` |
| macOS 构建源清单 | `macos.cmake:15-20` |
