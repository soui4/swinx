# swinx 无障碍桥实现流程详解（macOS / Linux）

> 目标读者：需要了解或维护 swinx 无障碍（ACC）链路的开发者
> 覆盖范围：`src/platform/cocoa/SNsAccessibility.mm`（macOS）、`src/platform/linux/SAtSpi.cpp`（Linux）
> 关联文档：《swinx-无障碍(acc)架构.md》（设计原则）、《swinx-跨平台ACC实现总结.md》（排障经验）、《swinx-Linux无障碍(AT-SPI)桥接.md》（协议细节）

本文回答一个问题：**平台的无障碍接口（NSAccessibility / AT-SPI2）是如何经由 `IAccessible` 与 SOUI 交互的？**
按"一次查询的生命周期"为主线，逐段拆解两套实现的完整流程。

---

## 1. 全景：三段式链路

两套实现共用同一个三段式结构，差异只体现在首尾两段：

```
┌─ 第一段：系统 AT 客户端 ────────────────────────────────────────┐
│  macOS：VoiceOver                                              │
│  Linux：Orca / accerciser（libatspi）                          │
└───────────────────────┬────────────────────────────────────────┘
                        │ 平台协议（XPC-MIG / D-Bus）
                        ▼
┌─ 第二段：平台桥（swinx）────────────────────────────────────────┐
│  macOS：SwinxAccElement（NSAccessibilityElement 身份壳）        │
│  Linux：AT-SPI2 D-Bus 对象（确定性路径 w<hex>_c_<i>_<j>）       │
│                                                                 │
│  共同内核：SwinxAccResolvePath(hwnd, chain) → (IAccessible*,    │
│            childId)，现查现用现放，绝不缓存 COM 指针            │
└───────────────────────┬────────────────────────────────────────┘
                        │ SendMessage(WM_GETOBJECT, OBJID_CLIENT)
                        │ ← LresultFromObject(IID_IAccessible, ...)
                        ▼
┌─ 第三段：swinx 进程内 MSAA 内核 + SOUI ─────────────────────────┐
│  swinx/src/oleacc.cpp：WM_GETOBJECT 协议 + 弱句柄表              │
│  SOUI/src/core/shostwnd.cpp：SHostWnd::OnGetObject              │
│  SOUI SAccessible（include/core/SWndAccessible.h，              │
│  实现 IAccessible）：以 SWindow 树回答 accXxx                   │
└─────────────────────────────────────────────────────────────────┘
```

**核心不变量**：`IAccessible*` 的所有者自始至终是 SOUI（`SWindow::m_pAcc`，随控件生命周期）。
两个平台桥都不持有 COM 引用，因此控件销毁后不可能访问悬垂指针——最坏情况是路径解析失败、
该节点退化成空属性。

### 1.1 为什么必须"现查现用"而不是缓存

| 若缓存 `IAccessible*` | 实际做法 |
| :--- | :--- |
| SOUI 控件销毁时 `m_pAcc` 析构，桥里指针立刻悬垂 | 桥只记 `(hwnd, child 链)`，每次访问重新解析 |
| AT-SPI 侧收不到可靠的控件级销毁通知（见 §5.3） | 路径解析失败即"节点不存在"，安全退化 |
| 跨线程持有 COM 引用带来生命周期竞态 | 解析与使用都在同一线程、同一调用栈内完成 |

代价是每次访问 O(深度) 次 `get_accChild`；收益是**整条链路零悬垂风险**。

---

## 2. 汇合点：swinx 进程内 MSAA 内核（oleacc.cpp）

两个平台桥最终都调到同一组函数。理解这段是理解全链路的前提。

### 2.1 `AccessibleObjectFromWindow`：WM_GETOBJECT 往返

```
AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, IID_IAccessible, &ppAcc)
  ├─ SendMessageA(hwnd, WM_GETOBJECT, (WPARAM)-1, (LPARAM)(LONG)OBJID_CLIENT)
  │     └─▶ SHostWnd::OnGetObject（SOUI，UI 线程）
  │           ├─ 未开 SOUI_ENABLE_ACC → SetMsgHandled(FALSE); return 0
  │           └─ 已开 → GetRoot()->GetAccessible()（走 SWindow 树的根）
  │                     → LresultFromObject(IID_IAccessible, wParam, pAcc)
  │                        → 登记 (句柄 → {对象指针, wParam})，返回 LRESULT
  └─ ObjectFromLresult(lResult, riid, (WPARAM)-1, ppv) → 查表 + QueryInterface
```

两个易错点（都已在实现中处理，理解后可避免误改）：

1. **wParam 必须原样回传**。swinx 的 `LresultFromObject` 把句柄与 `WM_GETOBJECT`
   的 `wParam` 绑定；`ObjectFromLresult` 时 `wParam` 不匹配即 `E_FAIL`。
   客户端固定发 `(WPARAM)-1`，服务端（SOUI）原样传回。
2. **OBJID 的符号扩展**。真实 oleacc 把 OBJID 当 32 位有符号数放进 LPARAM，
   `OBJID_CLIENT`（-4）在 64 位上是 `0xFFFFFFFFFFFFFFFC`。swinx 用
   `(LPARAM)(LONG)dwObjectID` 复刻该行为——否则服务端 `(LONG)lParam == OBJID_CLIENT`
   在 LP64 上永不成立（真实 Windows 上该值是 -4）。

### 2.2 弱登记句柄表

`LresultFromObject` 的句柄表**只记映射、不 AddRef**：

- 每次调用产生新句柄，表有容量上限，超限淘汰最旧句柄；
- 句柄只在同一次同步往返内有效（客户端拿到接口后立即使用并 Release）；
- 失败以 HRESULT 直接作 LRESULT 返回（`null → E_INVALIDARG`、QI 失败 → `E_NOINTERFACE`），
  与文档描述不同但与真实 oleacc 实测一致。

陈旧句柄自然失效——没有"撤销 LRESULT"的 API，也没有引用需要释放。

### 2.3 `SwinxAccResolvePath`：两个平台桥的公共下钻内核

声明在 `src/SwinxAccGlue.h`（内部头，**不进公共 API 面**），实现于 oleacc.cpp。
这是 macOS 与 Linux 两条链路**唯一的汇合点**。

```
SwinxAccResolvePath(hwnd, chain[], cChain, &ppAcc, &pChildId)
  ├─ AccGetRootAccessible(hwnd, &cur)
  │     ├─ AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, ...)   ← 首选：控件树根
  │     └─ 失败则 OBJID_WINDOW                                    ← 回退：整个窗口
  └─ for i in 0..cChain-1:
        get_accChild(chain[i], &pdisp)
        ├─ 成功且 QI 到 IAccessible → cur = next，继续下钻
        └─ 失败（简单元素）→ 仅当它是最后一级才返回
              { *ppAcc = cur(父对象), *pChildId = chain[i] }
              ↑ 非末级失败 = 路径已失效 → E_FAIL

  返回约定（MSAA 语义）：
    pChildId == CHILDID_SELF → *ppAcc 自身即目标对象
    pChildId != CHILDID_SELF → 目标是简单元素，*ppAcc 是父对象，查询须带 childId
  调用方负责 Release(*ppAcc)。
```

**MSAA 的两种子元素形态**（贯穿两套实现，必须区分）：

| 形态 | `get_accChild(i)` 返回 | 表示法 | 能否有子节点 |
| :--- | :--- | :--- | :--- |
| 完整对象 | `IDispatch*`（可 QI 到 IAccessible） | `(acc, CHILDID_SELF)` | 能 |
| 简单元素（虚拟项） | 失败 | `(父 acc, childId=i)` | 不能 |

---

## 3. macOS 流程（NSAccessibility 桥）

代码：`src/platform/cocoa/SNsAccessibility.mm` / `.h`，接入点 `SNsWindow.mm`。

### 3.1 元素模型：身份壳而非代理

SOUI 的窗口内容整体绘制在一个 `NSView`（`SNsWindow`）里，AppKit 默认只能看到
一个不透明大视图。桥把 `IAccessible` 树映射成 `NSAccessibilityElement` 树挂到该视图下：

```
NSWindow
  └─ contentView (SNsWindow)          ← -accessibilityChildren 返回根壳
       └─ SwinxAccElement (chain = [])          ← OBJID_CLIENT 根
            └─ SwinxAccElement (chain = [1])    ← 第 1 个子的壳
                 └─ SwinxAccElement (chain = [1, 3])
```

`SwinxAccElement` 是**身份壳**，成员只有三样：

```objc
HWND _hwnd;                  // 宿主窗口
NSArray<NSNumber*> *_chain;  // MSAA child id 链（1-based）
__weak id _parent;           // AX 父节点，弱引用
```

**壳里没有任何 COM 引用**。每次属性访问都经 `swinxResolve:` 重新解析：

```objc
- (BOOL)swinxResolve:(IAccessible **)pAcc childId:(LONG *)pChildId
{
    // _chain 转 std::vector<LONG>
    return SwinxAccResolvePath(_hwnd, chain, cChain, pAcc, pChildId) == S_OK;
}
```

所有 getter 都包在同一个模板里，保证解析/Release 成对、异常路径不漏引用：

```objc
template <typename T>
static T SwinxAccQuery(SwinxAccElement *self,
                       T (^block)(IAccessible *acc, LONG childId), T fallback)
{
    IAccessible *acc = NULL; LONG childId = CHILDID_SELF;
    if (![self swinxResolve:&acc childId:&childId] || !acc) return fallback;
    T ret = block(acc, childId);
    acc->Release();          // ← 无论 block 内部怎么走，都必释放
    return ret;
}
```

### 3.2 一次属性查询的完整流程（以 VoiceOver 朗读按钮名为例）

```
VoiceOver 读 accessibilityTitle（用户正悬停某按钮壳，chain = [2, 1]）
  ▼
[SwinxAccElement accessibilityTitle]
  ▼
SwinxAccQuery(self, ^(acc, childId){ acc->get_accName(child, &name); }, nil)
  ▼
swinxResolve: → SwinxAccResolvePath(hwnd, [2,1], ...)
  ▼
AccessibleObjectFromWindow(hwnd, OBJID_CLIENT)     ← 每次都重新发 WM_GETOBJECT
  ▼  SendMessage
SHostWnd::OnGetObject → GetRoot()->GetAccessible() → LresultFromObject
  ▼
沿 chain 下钻：get_accChild(2) → get_accChild(1)
  ▼
SOUI 的 SAccessible::get_accName(child, &bstr)      ← 真正读 SWindow 的文本
  ▼
BSTR(UTF-32) → NSString(NSUTF32LittleEndianStringEncoding) → VoiceOver 朗读
  ▼
acc->Release()（SwinxAccQuery 收尾）
```

### 3.3 属性映射表

| NSAccessibility | IAccessible 调用 | 备注 |
| :--- | :--- | :--- |
| `accessibilityRole` | `get_accRole` → `SwinxAccRoleFromMsaA` | ROLE_SYSTEM_* → NSAccessibility*Role |
| `accessibilityTitle` | `get_accName` | |
| `accessibilityValue` | `get_accValue` | |
| `accessibilityHelp` | `get_accDescription` | |
| `accessibilityFrame` | `accLocation` + `SNsCoord` | swinx 像素（y 向下）→ Cocoa 屏幕（y 向上） |
| `accessibilityEnabled` | `get_accState` & `STATE_SYSTEM_UNAVAILABLE` | 查不到状态时按"可用"处理 |
| `accessibilityFocused` | `get_accState` & `STATE_SYSTEM_FOCUSED` | |
| `accessibilityHidden` | `get_accState` & (INVISIBLE\|OFFSCREEN) | |
| `accessibilityPerformPress` | `accDoDefaultAction` | |
| `accessibilityChildren` | `get_accChildCount` + 逐个建子壳 | 见下 |

文本编码：swinx 的 `WCHAR` 在 macOS 是 `wchar_t`（4 字节），BSTR 载荷即 UTF-32，
故用 `NSUTF32LittleEndianStringEncoding` 转换。

### 3.4 子元素枚举与身份稳定

```objc
- (NSArray *)accessibilityChildren
{
    // 1. 解析自身（简单元素没有子节点，直接返回空）
    // 2. get_accChildCount 取数量
    // 3. for i in 1..count：childChain = _chain + [i]
    //       → swinxChildShellWithChain: 在旧缓存里找链相同的壳复用
    _children = kids;   // 每次实时重建
    return _children;
}
```

**壳按需构建、按链复用**：VoiceOver 依赖"同一元素对应同一对象"来追踪焦点，
复用旧壳维持了这个身份；而壳不持 COM 引用，所以结构变化时**丢弃缓存零成本**
（`swinxInvalidateChildren` 只是把 `_children = nil`）。

### 3.5 命中测试（鼠标跟随朗读）

这是链路中最复杂的一环，涉及四级转发：

```
VoiceOver 鼠标跟随
  ▼  App 级 → Window 级 → View 级逐级 accessibilityHitTest:（Cocoa 屏幕坐标）
[SNsWindowHost/SNsPanelHost -accessibilityHitTest:]   ← SwinxNsHostAccHitTest
  │   内容区内的查询显式转交 contentView，不依赖 AppKit 默认转发
  ▼
[SNsWindow -accessibilityHitTest:]
  │   ① SwinxNsAccAttachRootParent(m_hWnd, self)  ← 先把 self 挂为根壳 AXParent（防§3.7）
  │   ② SwinxNsAccHitTest(m_hWnd, point)
  ▼
SwinxNsAccHitTest
  ├─ swinxNsWinPointFromCocoa：Cocoa 屏幕点 → swinx 像素点
  ├─ SwinxAccHitTestChain(hwnd, pt, @[])：从根开始逐级下钻
  │     loop（最多 64 层）:
  │       解析当前链 → accHitTest(pt.x, pt.y, &v)
  │       ├─ v 为 VT_I4：childIndex = v.lVal（SELF=0 或直接子编号）
  │       ├─ v 为 VT_DISPATCH（完整对象）：按 IUnknown 身份在直接子中定位编号
  │       └─ childIndex <= 0 → 命中自身/失败 → 终止
  │       追加到链，继续
  │     简单元素（childId != CHILDID_SELF）即终点——它没有子节点
  ├─ SwinxAccShellForChain：沿链逐级取/建子壳（复用缓存维持身份与 parent 链）
  └─ frame 契约兜底：结果壳的 frame 外扩 2pt 不含查询点 → 回退根元素
```

**为什么必须逆序 / 为什么要 frame 兜底**：SOUI 的 `accHitTest` 子遍历必须按 z 序
（最上层优先），否则垫底的背景容器会抢答；而 frame 兜底是因为"语音光标朝错误目标
游走不收敛"在 AX 里表现为死循环——AppKit 契约要求返回的元素必须包含查询点。

**重入防护**：`s_accHitTestDepth` 计数器。AX 命中查询可能经
`SendMessage(WM_GETOBJECT)` 重入窗口过程，再次触发命中查询；检测到重入直接浅层应答。

### 3.6 事件桥（WinEvent → NSAccessibility 通知）

```
SOUI SWindow 状态变化 → accNotifyEvent(dwEvt) → NotifyWinEvent(...)
  ▼  （swinx 事件先入队，消息泵异步派发——与 user32 OUTOFCONTEXT 钩子一致）
SwinxAccEventHook（任意线程）
  ├─ SwinxAccNotificationForEvent(event, &invalidate)   ← MSAA 事件 → AX 通知名
  ├─ 通知节流：同一窗口 50ms 内只投递一次通知（壳缓存失效不受影响）
  └─ dispatch_async(主队列):
       ├─ SwinxAccCachedRoot(hwnd)（只查缓存，不为无 ACC 对象的窗口凭空造壳）
       ├─ invalidate → [root swinxInvalidateChildren]
       └─ NSAccessibilityPostNotification(root, notification)
```

| MSAA 事件 | NSAccessibility 通知 | 需失效壳缓存 |
| :--- | :--- | :--- |
| `EVENT_OBJECT_FOCUS` | `FocusedUIElementChanged` | |
| `EVENT_OBJECT_NAMECHANGE` | `TitleChanged` | |
| `EVENT_OBJECT_VALUECHANGE` / `STATECHANGE` | `ValueChanged` | |
| `EVENT_OBJECT_SELECTION*` | `SelectedChildrenChanged` | |
| `EVENT_OBJECT_LOCATIONCHANGE` | `Moved` | |
| `EVENT_OBJECT_CREATE` | `Created` | ✓ |
| `EVENT_OBJECT_DESTROY` | `UIElementDestroyed` | ✓ |
| `EVENT_OBJECT_SHOW` / `HIDE` | （无通知） | ✓ |

**事件归因粒度是已知限制**：SOUI 的 `accNotifyEvent` 用 `idObject` 传 **SWND**（不是
`OBJID_*`），桥无法从 SWND 反查到具体元素壳，因此事件统一归因到窗口根节点。
属性本身是实时查询的，所以通知只起"让 AT 再看一眼"的作用，不影响内容正确性。

### 3.7 两个必须遵守的 macOS 契约

**（a）AXParent 锚点**。AppKit 解析元素属性时会沿 `accessibilityParent` 上溯寻找
原生锚点（NSView / NSWindow / NSApp）。若祖先链中途遇到 `nil` 非锚点元素 → 解析失败 →
VoiceOver **无限重试**同一批属性查询 → 主线程被 AX 查询洪水钉死（表现为假死）。
两道防线：

- `SwinxNsAccAttachRootParent(hwnd, parent)`：幂等地把 contentView 挂为根壳父节点。
  hit test 可能先于 `-accessibilityChildren` 发生，所以在 `-accessibilityHitTest:`
  **入口**也必须调用（不能只依赖 children 路径）。
- `accessibilityParent` 的 nil 兜底：孤儿壳（窗口已销毁但 VO 仍持有旧壳）直接返回 `NSApp`，
  保证链必达终点（带限流日志，便于诊断孤儿来源）。

**（b）编译顺序**。`.mm` / `.h` 中 Cocoa 必须先于 swinx 头导入：

- swinx 的 `gdi.h` 有 `#define Polygon Polygon_Priv`（避开 Quickdraw 的
  `typedef MacPolygon Polygon`）；若它先于 Cocoa 定义，Quickdraw.h 会被宏破坏而编译失败。
- swinx 的 COM 头（basetyps.h）把 `interface` 宏定义为 `struct`，而 macOS SDK 头文件
  用到 `interface` 这个词——进入 ObjC 世界前必须 `#undef interface`。

---

## 4. Linux 流程（AT-SPI2 / D-Bus 桥）

代码：`src/platform/linux/SAtSpi.cpp` / `.h`。

### 4.1 为什么需要这层桥

swinx 在 Linux 上早就有完整的 MSAA 接口面，但 **Orca / accerciser 不认 MSAA**，
它们只走 AT-SPI2——一套跑在**无障碍总线（a11y bus）**上的 D-Bus 协议。
桥的职责就是把进程内的 MSAA 对象树映射成 a11y bus 上的 AT-SPI2 对象树。

### 4.2 节点模型与路径编码

```cpp
struct AccNode {
    bool bRoot;               // /org/a11y/atspi/accessible/root
    bool bWindow;             // 顶层窗口节点（OBJID_WINDOW）
    HWND hwnd;
    std::vector<LONG> chain;  // client 之后的 MSAA child id 链（1-based）
};
```

路径是**确定性编码**，不依赖任何缓存表：

```
/org/a11y/atspi/accessible/root              应用根（role=APPLICATION）
/org/a11y/atspi/accessible/w<hwnd-hex>       顶层窗口（role=FRAME）
/org/a11y/atspi/accessible/w<hex>_c          窗口客户区根（MSAA OBJID_CLIENT）
/org/a11y/atspi/accessible/w<hex>_c_1        客户区第 1 个子
/org/a11y/atspi/accessible/w<hex>_c_1_3      再下一层
```

**分隔符必须用 `_`**（不能省、不能换成字母）：路径解析用 `strtoull(base 16)` 还原 hwnd，
而 `c` 是合法十六进制字符——若拼成 `w1800002c`，`c` 会被吞进句柄（`0x1800002c`），
该路径随即被解析成一个"顶层窗口"，窗口节点的 ChildCount 恒为 1，于是
`w1800002c → w1800002cc → w1800002ccc` 无限展开成假树。
`_` 不是十六进制字符，`strtoull` 在其处自然停住，编码自此无歧义。

### 4.3 节点解析：ResolveNode

```cpp
struct Resolved {                // RAII：析构自动 Release
    IAccessible *acc = NULL;
    LONG childId = CHILDID_SELF;
    bool ok = false;
    ~Resolved() { if (acc) acc->Release(); }
};

bool ResolveNode(const AccNode &node, Resolved &out)
{
    LONG childId = CHILDID_SELF; IAccessible *acc = NULL;
    HRESULT hr = SwinxAccResolvePath(node.hwnd, chain.data(), chain.size(), &acc, &childId);
    if (hr != S_OK || !acc) return false;   // 路径失效（控件销毁/重建）
    out.acc = acc; out.childId = childId; out.ok = true;
    return true;
}
```

与 macOS 完全同构，只是把"每次访问重新解析"封装成了 RAII 结构——C++ 侧靠析构
保证 Release，ObjC 侧靠 `SwinxAccQuery` 模板保证，语义一致。

### 4.4 节点属性：全部由 MSAA 派生

每个 AT-SPI 属性都对应一次（或多次）MSAA 调用：

| AT-SPI 属性 | 实现 | 说明 |
| :--- | :--- | :--- |
| `GetRole` | `get_accRole` → `RoleFromMsaA` | ROLE_SYSTEM_* → ATSPI_ROLE_* |
| `GetRoleName` | `RoleNameFromAtSpi` | 本进程查表，不调 MSAA |
| `GetState` | `get_accState` → `AppendMsaAStates` | 位映射 + 反向推导（见下） |
| `Name` | `get_accName`（BSTR→UTF-8） | 窗口节点用 `GetWindowTextA` |
| `Description` | `get_accDescription` | |
| `ChildCount` | `get_accChildCount` | 简单元素恒为 0 |
| `GetChildren` / `GetChildAtIndex` | `get_accChildCount` + `get_accChild` | 见 §4.5 |
| `GetExtents` | `accLocation` | 含 X 原点校正（见 §4.7） |
| `DoAction` / `GetActions` | `accDoDefaultAction` / `get_accDefaultAction` | |

**状态映射的反向推导**（MSAA 位 → AT-SPI 状态的补充）：

```cpp
if (!(msaa & STATE_SYSTEM_UNAVAILABLE)) → 补 ENABLED + SENSITIVE
if (!(msaa & STATE_SYSTEM_INVISIBLE))   → 补 VISIBLE
if (!(msaa & STATE_SYSTEM_OFFSCREEN))   → 补 SHOWING
```

AT-SPI 侧的 `ENABLED`/`VISIBLE` 是"肯定态"，而 MSAA 的 `UNAVAILABLE`/`INVISIBLE`
是"否定态"，必须这样反向补齐。root / window 节点不走 MSAA，直接给固定状态集。

### 4.5 子节点导航

```
NodeChildCount(node):
  bRoot   → TopLevelWindows().size()
  bWindow → 1（固定只有客户区根节点）
  否则    → get_accChildCount（简单元素返回 0）

NodeChildAt(node, index):
  bRoot   → AccNode{ bWindow=true, hwnd=TopLevelWindows()[index] }
  bWindow → AccNode{ hwnd, chain={} }            ← 客户区根
  否则    → AccNode{ hwnd, chain + (index+1) }   ← AT-SPI 0-based → MSAA 1-based
```

**唯一的 0-based / 1-based 转换点**就在这一行 `index + 1`，其余全程保持 MSAA 的
1-based 语义。

`NodeIndexInParent` 是逆运算：root 返回 -1，window 在顶层窗口列表中定位，
客户区根恒为 0，其余取 `chain.back() - 1`。

### 4.6 一次查询的完整流程（以 accerciser 展开三层控件为例）

```
accerciser 点开节点 w<hex>_c_1
  ▼  D-Bus 方法调用（a11y bus）
      org.a11y.atspi.Accessible.GetChildren @ /org/a11y/atspi/accessible/w<hex>_c_1
  ▼
AtSpiMessageFunction（UI 线程，timer 驱动的 Pump 内）
  ├─ ParseNodePath(msg.path) → AccNode{ hwnd, chain={1} }
  ├─ ResolveNode → SwinxAccResolvePath(hwnd, [1], ...)
  │     └─ AccessibleObjectFromWindow → SendMessage(WM_GETOBJECT) → SOUI
  │        → get_accChild(1) → IAccessible*
  ├─ get_accChildCount → N
  ├─ for i in 0..N-1: NodeChildAt(node, i) → NodePath(child)
  └─ 回复 D-Bus 数组 [(busName, path), ...]
  ▼
accerciser 收到子对象引用列表，按需继续下钻
```

### 4.7 坐标：以 X 服务器为可信源

Windows 上 `accLocation` 直接可信，但 Linux 下 swinx 维护的窗口矩形（`wndObj->rc`）
可能与 X 服务器实际位置不同步（实测确有该 bug）。因此桥做了两层校正：

- **窗口节点**：`QueryXWindowOrigin` 用 `xcb_translate_coordinates`（source=窗口，
  dest=root）直查真实原点（200ms 缓存）。`GetWindowRect` 与之不一致时按 X 校正，
  并节流 WARN `window origin out of sync`。
- **控件节点**：`accLocation` 结果若不在"X 修正后的窗口矩形"内、却落在
  "客户区尺寸 @ 原点"内 → 按窗口真实原点校正，节流 WARN
  `accLocation missing window origin`。

结构上这是 AT-SPI 相对 MSAA 多出来的一段适配，不是纯协议转换。

### 4.8 线程模型：不额外开线程

AT-SPI 查询最终要走到 `SendMessage(WM_GETOBJECT)`；swinx 的跨线程 `SendMessage`
会把消息 post 给窗口所属线程并等待，从 D-Bus 线程发起既绕又容易踩到栈深限制。

所以桥把 D-Bus 分发**挂进 UI 线程的消息循环**：

```
SwinxAtSpiInit()（必须在跑消息循环的线程，通常 UI 线程）
  └─ SetTimer(NULL, 0, 50, AtSpiTimerProc)      ← 纯回调定时器，无需隐藏窗口
       └─ AtSpiTimerProc → Pump()
            ├─ 派发期间不持 m_mutex（处理器会重入 EnsureInit，需能再拿锁）
            ├─ 循环泵空 socket + 每条派发后 flush
            ├─ socket 暂时无请求时 poll(5ms) 再确认
            └─ 单轮 20ms 时间预算，用尽即交还主线程
```

由此**处理 D-Bus 请求时可以直接调 MSAA**，无需任何跨线程 marshal。

初始化时机的坑（曾有启动死锁）：主初始化点必须在 `SConnMgr::instance()` 首次创建
进程单例**完成之后**，不能放在 SConnMgr 构造函数里——init 里 `SetTimer` 会重入
`instance()`，与 `static SConnMgr inst` 的初始化守卫（`__cxa_guard`，不可重入）死锁。
`oleacc.cpp` 的 `LresultFromObject`/`NotifyWinEvent` 另有惰性兜底（覆盖不经 SConnMgr 的
路径，如只跑 ACC 单测）。

### 4.9 事件桥（NotifyWinEvent → AT-SPI 信号）

```
SOUI accNotifyEvent(dwEvt) → NotifyWinEvent
  ▼  （swinx 事件先入队，消息泵异步派发；接收钩子在事件发出那一刻确定）
AtSpiWinEventHook
  ├─ 按进程 id 过滤（全局钩子会收到其他进程的事件）
  ├─ MSAA 事件 → (class, member, detail, any)
  │    ├─ 主线程发信号只入队（m_sigQueue）——避免 AB-BA 死锁
  │    └─ DrainSignals 在泵的派发间隙统一构造/发送/flush
  └─ EmitSignalFor → org.a11y.atspi.Event.<Class> 信号，签名 "siiva{sv}"
```

| MSAA 事件 | AT-SPI 信号 |
| :--- | :--- |
| `EVENT_OBJECT_CREATE/DESTROY` | `Event.Object.ChildrenChanged`（add/remove） |
| `EVENT_OBJECT_SHOW/HIDE` | `Event.Object.StateChanged`（visible / showing） |
| `EVENT_OBJECT_FOCUS` | `Event.Object.StateChanged`(focused) + `Event.Focus.Focus` |
| `EVENT_OBJECT_NAMECHANGE` | `Event.Object.PropertyChange`(accessible-name) |
| `EVENT_OBJECT_VALUECHANGE` | `Event.Object.PropertyChange`(accessible-value) |
| `EVENT_OBJECT_LOCATIONCHANGE` | `Event.Object.BoundsChanged` |
| `EVENT_SYSTEM_FOREGROUND` | `Event.Window.Activate` |

**归因粒度是同一处已知限制**：`accNotifyEvent` 用 `idObject` 传 SWND，桥无法反查到
具体的 AT-SPI 节点，因此事件统一归因到该窗口的**客户区根节点**。

`ChildrenChanged` 只对**顶层窗口**的增删广播（信号发到 root 路径，载荷必须带被增删
子对象的 `(so)` 引用与索引）——载荷残缺会被 libatspi 丢弃或据其建出坏树。
内部控件增删由客户端展开时 `GetChildren` 实时枚举获得，逐节点广播既无必要也过量。

### 4.10 D-Bus 两个致命细节

**（a）字符串变体必须传指针的指针**。libdbus 对 `DBUS_TYPE_STRING` / object path 的
`append_basic` 要求传 `const char **`；误传字符串指针本身，会把字面量前 8 字节
解引用成 `char*` 再 `strlen`——段错误（曾是进程退出时崩溃的根因）。

**（b）fallback 注册必须覆盖 `/org/a11y/atspi` 全子树**。`Cache.GetItems` 的标准
路径是 `/org/a11y/atspi/cache`，只注册 `/accessible` 时该请求落不到任何处理器，
而 libdbus 对未处理的方法调用**不保证自动回错**——客户端永久等应答，我方日志零记录
（盲区级卡死）。解析失败的路径与未知接口一律显式 `ReplyError`。

### 4.11 顶层窗口枚举：本地候选表

窗口映射是 `wndobj.cpp` 的文件私有数据，为单一消费者在 WndMgr 上加公开接口不划算，
桥自维护候选表：

```
候选集 = EnsureInit 时的 EnumWindows 种子
         （覆盖 AT 钩子安装前已存在的窗口——钩子只收注册之后的事件）
       ∪ 此后 WinEvent(CREATE/SHOW) 携带的宿主窗口（去重）

入口按进程过滤：GetWindowThreadProcessId(hwnd, NULL) != 0
  （EnumWindows 走 X 根窗口枚举，会收到桌面上所有进程的窗口；
    swinx 的 IsWindow / IsWindowVisible / GetParent 对外来窗口全部放行，
    不过滤则 root 子列表被整个桌面污染。判据用纯 map 查找，零 X 往返。）

读取时逐项实时校验：IsWindow + IsWindowVisible + GetParent == 0
  （销毁/隐藏/层级变化按调用瞬间判定，不依赖事件不丢失）

空表自愈：候选表为空 → 重新播种（节流 1s）
  （首次 NotifyWinEvent 可能早于宿主窗口登记）
```

副作用是正确行为：tooltip 平时隐藏（`WS_VISIBLE` 关闭）天然被过滤，
符合 AT-SPI"不暴露不可见窗口"的语义。

---

## 5. 两套实现对照

### 5.1 结构对照

| 维度 | macOS | Linux |
| :--- | :--- | :--- |
| 平台 ACC 协议 | NSAccessibility（XPC/MIG 通道） | AT-SPI2 over D-Bus |
| 元素身份 | `SwinxAccElement`，只存 `(hwnd, chain)` | `AccNode{hwnd, chain}` ↔ 确定性路径 |
| 子对象表示 | `NSArray<NSNumber*>` 链 | 路径字符串 `w<hex>_c_<i>_<j>` |
| 身份稳定手段 | 壳按链缓存复用 | 路径天然确定，无需缓存 |
| 解析内核 | `SwinxAccResolvePath` | 同左（共用） |
| 引用管理 | `SwinxAccQuery` 模板保证 Release | `Resolved` RAII 析构 |
| 线程 | 属性访问在 AX 查询线程；通知 dispatch 到主队列 | 全部在 UI 线程（timer 驱动 Pump） |
| 命中测试 | AppKit 四级转发 + 逐级 `accHitTest` | 客户端逐层 `GetChildAtIndex` 下钻 |
| 坐标 | swinx 像素 → Cocoa 屏幕（`SNsCoord`） | X 服务器 `translate_coordinates` 校正 |
| 事件通道 | WinEvent 钩子 → 主队列 → `NSAccessibilityPostNotification` | WinEvent 钩子 → 入队 → D-Bus 信号 |
| 事件节流 | 同窗口 50ms | ChildrenChanged 按 hwnd 200ms 去抖 |

### 5.2 共同的语义转换点

两套实现都要面对同一组"概念语义 ≠ MSAA 语义"的转换：

1. **两种子元素形态**：完整对象 vs 简单元素。macOS 侧体现为
   `accessibilityChildren` 里"壳可能是虚拟项"；Linux 侧体现为路径解析的
   末级特例。`SwinxAccResolvePath` 的 `(acc, childId)` 输出约定是统一抽象。
2. **角色映射**：`ROLE_SYSTEM_*` → 平台角色（`SwinxAccRoleFromMsaA` / `RoleFromMsaA`），
   未知角色退化为平台的中性角色（UnknownRole / ATSPI_ROLE_UNKNOWN）。
3. **状态映射**：MSAA 用位掩码，两边都要展开成平台枚举/布尔属性。
   AT-SPI 还需反向推导肯定态（§4.4）。
4. **事件归因**：两套都受同一个限制——SOUI 事件只带 SWND，无法精确到元素。
5. **坐标空间**：MSAA 是"swinx 全局像素、y 向下"，两个平台的目标空间都不同。

### 5.3 共同的已知限制

- **事件归因只能到窗口客户区根节点**。要精确到控件，需要 swinx/SOUI 提供
  SWND → 节点 的反查通道。
- **扩展接口未实现**。AT-SPI 的 Text / Value / Selection / Table 扩展接口缺失，
  文本控件对 AT 只暴露 name/value，不支持逐字朗读与选区；macOS 侧同理
  （只实现了 NSAccessibilityElement 基础属性集）。
- **`GetClipBox` 之类的既有坑不影响 ACC**，但 ACC 侧的 `accLocation` 依赖 swinx
  窗口矩形（`wndObj->rc`）的正确性——Linux 侧因此才有了 X 原点校正。
- **无自动化测试**。ACC 需要真实 a11y bus / VoiceOver 会话，CI 不具备；
  `demos/fun_test/test_oleacc.cpp` 覆盖的只是 MSAA 接口面本身（与桥无关）。

---

## 6. 启用与验证

### 6.1 编译开关

```bash
cmake -DSOUI_ENABLE_ACC=ON ...
```

该宏由根 CMakeLists 经 `add_definitions` 全局定义，**SOUI 与 swinx 共用同一个宏**。
关闭时 `SHostWnd::OnGetObject` 对 `WM_GETOBJECT` 返回 0 → 桥各入口自然退化为
"没有可访问对象"（macOS 不建壳、Linux 不连 D-Bus、不占 timer），无需各自加开关。

### 6.2 快速自检清单

| 现象 | 排查方向 |
| :--- | :--- |
| AT 里完全看不到应用 | `SOUI_ENABLE_ACC` 是否开启；桥是否初始化成功 |
| 只看到窗口框架、没有控件 | SOUI 侧 `SOUI_ENABLE_ACC` 未开（只有 SWindow 根可用） |
| Linux：反复 `ConnectBus failed` | 会话无障碍总线未起（`toolkit-accessibility` / at-spi2 状态） |
| Linux：客户端卡死无日志 | fallback 注册是否覆盖 `/org/a11y/atspi` 全子树（§4.10b） |
| Linux：`window origin out of sync` | swinx 核心窗口矩形同步失真，按 WARN 里的差值定位 |
| macOS：应用假死、AX 查询洪水 | AXParent 锚点链是否中断（§3.7a） |
| macOS：VO 光标游走不收敛 | hit test 返回的 frame 是否包含查询点（§3.5） |

### 6.3 手工验证

```bash
# Linux：确认总线在跑
echo $AT_SPI_BUS
accerciser &        # 左侧树应出现 "swinx application" 根节点
orca &

# 直接问单条属性（<bus> = $AT_SPI_BUS，<name> = 应用 unique name）
dbus-send --bus="$AT_SPI_BUS" --print-reply --dest=<name> \
  /org/a11y/atspi/accessible/root \
  org.a11y.atspi.Accessible.GetChildCount

# 批量拉整棵树（Orca 主要靠它）
dbus-send --bus="$AT_SPI_BUS" --print-reply --dest=<name> \
  /org/a11y/atspi/accessible/root \
  org.a11y.atspi.Cache.GetItems

# macOS：开启 VoiceOver 后鼠标跟随朗读即可验证命中链路
#   注意：VO 默认鼠标跟踪是 "Follows VO cursor"，
#   需在 VO+F8 → Navigation 开 "Moves VoiceOver cursor"；
#   开启后默认的"键盘焦点与 VO 光标同步"会把鼠标扫过的后台窗口拉前台
#   （失活振荡，非 bug，需取消同步）
```

### 6.4 诊断日志约定

正常运行只保留异常路径 WARN（`ConnectBus`/`Embed` 失败、注册失败、不可解析路径、
窗口原点失同步、accLocation 校正触发、AXParent nil 兜底、hit test 重入）。
联调期的逐调用日志（`dbus in/out,#seq`、`NodeScreenRect,ctrl` 等）已移除，
需要时按上述关键字模式临时回加。

---

## 7. 关键代码索引

| 功能 | 位置 |
| :--- | :--- |
| 平台桥公共下钻内核 | `src/oleacc.cpp` → `SwinxAccResolvePath`；声明 `src/SwinxAccGlue.h` |
| WM_GETOBJECT 协议服务端 | `src/oleacc.cpp` → `LresultFromObject` / `ObjectFromLresult` |
| WM_GETOBJECT 协议客户端 | `src/oleacc.cpp` → `AccessibleObjectFromWindow` |
| 事件泵 | `src/oleacc.cpp` → `SwinxDispatchPendingWinEvents`（由 `src/sysapi.cpp` 的 GetMessage/PeekMessage 调用） |
| macOS 身份壳与属性 | `src/platform/cocoa/SNsAccessibility.mm` → `SwinxAccElement` 实现段 |
| macOS 命中测试 | 同上 → `SwinxAccHitTestChain` / `SwinxNsAccHitTest` |
| macOS 事件桥 | 同上 → `SwinxAccEventHook` |
| macOS 视图接入点 | `src/platform/cocoa/SNsWindow.mm` → `-accessibilityChildren` / `-accessibilityHitTest:` |
| macOS 坐标换算 | `src/platform/cocoa/SNsCoord.h` |
| Linux 路径编码 | `src/platform/linux/SAtSpi.cpp` → `NodePath` / `ParseNodePath` |
| Linux 节点解析 | 同上 → `ResolveNode` / `Resolved` |
| Linux 属性派生 | 同上 → `NodeRole` / `NodeStates` / `NodeName` / `NodeChildCount` |
| Linux D-Bus 分派 | 同上 → `AtSpiMessageFunction` |
| Linux 线程泵 | 同上 → `AtSpi::Pump` / `AtSpiTimerProc` |
| Linux 事件桥 | 同上 → `AtSpiWinEventHook` / `Emit*` |
| SOUI 服务端入口 | `SOUI/src/core/shostwnd.cpp` → `SHostWnd::OnGetObject` |
| SOUI 接口所有者 | `SOUI/src/core/Swnd.cpp` → `SWindow::GetAccessible` / `accNotifyEvent` |

---

*文档版本：2026-09-13*
