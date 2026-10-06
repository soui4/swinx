# swinx 子窗口客户区裁剪（父窗口滚动条被子窗口压住）

> 本文记录 swinx 补上 Win32「子窗口不得出现在父窗口客户区之外」这条裁剪规则的背景、方案取舍、实现与验证方法。规则本身是纯几何、跨平台无关的（`src/wndclip.h`），落地复用各平台已有的 `SetWindowRgn` 通路：Linux 走 XShape 的 bounding shape，macOS / iOS 走 `CAShapeLayer` 蒙版 + 重写 `hitTest:`。**不引入任何"滚动条区域透明窗口"之类的新窗口**。

## 1. 问题定义

swinx 的滚动条不是独立控件，而是父窗口的**非客户区**：数据存 `WndObj::sbVert`/`sbHorz`，几何由 `GetScrollBarRect()` 算，绘制由父窗口自己在窗口边缘描。于是当"父窗口 A 需要滚动条"且"子窗口 B 的矩形压在 A 的滚动条那一条上"时，两个方向同时坏掉：

1. **绘制**：B 是一扇真实原生窗口，压在 A 的滚动条上把它盖住。Win32 语义下 A 的滚动条应当可见（B 被裁掉）。
2. **输入**：落在滚动条那一条上的鼠标消息只会命中 B，A 收不到 `WM_VSCROLL` / `SBN_*`。Win32 语义下应当命中 A 的滚动条。

判据（Win32 文档原文）：

> The system clips a child window so that it cannot appear outside the client area of its parent.

## 2. 根因

swinx 的每扇 HWND 都是**真实原生窗口**（X11 子窗口 / Cocoa `NSView` 子视图），不是伪窗口。原生窗口系统只把子窗口裁到**父窗口的窗口矩形**（含边框），并不裁到**父窗口的客户区**——因为"客户区"是 Win32 的概念，X11 / AppKit 里没有对应物。所以这条铁律在 swinx 上是**缺失**的，而不是实现错了。

坐标上要留意：swinx 的客户区原点与窗口原点重合，只在右/下边减去 `SM_CXEDGE`/`SM_CYEDGE` 与滚动条宽度；`GetWindowRect` 返回的是**根坐标系**矩形（已逐层累加父窗口原点）。

## 3. 方案取舍：为什么不用"滚动条区域透明窗口"

原设想是在滚动条区域建两个透明窗口专门收鼠标消息、绘制仍交给 A。它确实能保住 `GetWindowDC` 语义，但代价是把一个**违规**从短命 rollout 变成**永久的公开 API 面**：

- `EnumChildWindows` / `GetWindow(GW_CHILD)` / `GetWindow(GW_HWNDNEXT)` 等所有枚举路径都要特判并跳过这两个窗口，漏一处就多出两个"幽灵子窗口"；
- z-order 每次重排都要把它们重新提到最前；
- 鼠标消息要再转发回 A，坐标要再换算一次；
- 每个需要滚动条的窗口都要两扇额外原生窗口，窗口数直接翻三倍；
- 问题只被"遮住"，其他"子窗口越出父客户区"的场合（边框、菜单栏、工具栏区域压住子窗口）依然错。

顺带发现一个独立缺陷：`xcb_create_window_checked` 里窗口类型被硬编码成 `INPUT_OUTPUT`，`wndClass` 的 `INPUT_ONLY` 分支是死代码——这条思路连"透明窗口不收绘制"都还得先把这处补齐。

**结论：采用补规则本身**——让子窗口的可见区与输入区恒等于「子窗口矩形 ∩ 祖先客户区」。它一次性修正绘制与输入两侧，且不扩大 API 面。

## 4. 裁剪规则与实现

### 4.1 几何：`src/wndclip.h`（新增，平台无关）

两个 `inline` 函数，只依赖 `windows.h` 里的 `RECT`/区域运算，不含任何平台 `#ifdef`：

| 函数                                                 | 作用                                                                                                    |
| -------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `SbClipChildToClient(prcChild, prcClient, prcOut)` | 把「父坐标系下的子窗口矩形」裁到「同一坐标系下的父客户区」，再把结果换算到**子窗口自身坐标系**（左上角归零）。返回 FALSE 表示裁完为空（`prcOut` 置空矩形）。              |
| `SbGetWindowClientClip(hWnd, prcClip)`             | 沿 `GetParent` 往上逐层求交，遇到非 `WS_CHILD` 窗口即停（顶层窗口的"父"是 owner，owner 不裁剪被拥有的窗口——这正是 Win32 行为），最后换算到窗口自身坐标系。 |

要点：

- **带原点偏移**换算，不能求交完就当成子窗口坐标（子窗口原点可为负，例如被滚到父窗口外面）。
- 半开区间：上边界刚好贴合客户区边缘**不算**越界，否则每个子窗口都会被多裁一行/一列。
- 祖先链上限 64 层，防环。

### 4.2 下发：`src/wnd.cpp`

- `SbRefreshWindowClip(hWnd, bForce)`：算本窗口的可行区域 → 与 app 自己 `SetWindowRgn` 的区域 `CombineRgn(..., RGN_AND)` → 交给平台已有的 `SConnection::SetWindowRgn` 落地。
  - 「整窗可见」的判据取**平台真实矩形**（`GetWindowRect`），不取缓存的 `wndObj->rc`：`wndObj->rc` 只在 `WM_MOVE`/`WM_SIZE` 里更新，且部分路径上未必已同步。
  - 是否下发由 §4.4 的 `SbCalcClipWant` / `SbNeedPushClipRgn` 判定：**比的是"平台侧现在挂着什么"，不是"期望状态是什么"**。整窗可见且平台侧没有残留区域时才不下发；若平台侧还挂着上一次下发的区域，必须补一次下发把它撤掉。反过来，期望与平台侧现状**一致**时一次都不下发——下发要动平台的渲染表面（见 §4.4 与 §7 第 4 条）。
  - 完全落在祖先客户区之外时下发一个**零宽矩形区域**。注意"空区域"与"无区域"在各平台表达不同，零宽矩形没有这个歧义。
- `SbRefreshChildClips(hWnd)`：用 `GetWindow(GW_CHILD)` / `GW_HWNDNEXT` 遍历**直接**子窗口重算（更深的层级由各自的父窗口触发，避免 O(n²)）。

### 4.3 与 app 区域共存

`SetWindowRgn` 被改写为：把 app 传入的区域 `RGN_COPY` 存进 `WndObj::hAppRgn`（`hRgn == NULL` 时释放），再调 `SbRefreshWindowClip(hWnd, TRUE)`。原来 app 的区域是**直接**转发给平台的，一旦我们再下发裁剪就会互相覆盖（后调用的那个把先调用的抹掉）；现在两者始终取交集。

`hAppRgn` 在 `_Window` 析构里 `DeleteObject`。

### 4.4 下发是"状态"，不是一次性动作

平台**不会**自己清理区域：一旦下发过，就必须由 swinx 显式撤销；反过来，**期望与平台侧现状一致时一次都不该下发**。判定因此拆成两段纯函数（都在 `wndclip.h`，可单测）：

```c
/* 期望什么形态：FALSE = 不需要区域；TRUE = 需要 *prcWant 这块矩形 */
inline BOOL SbCalcClipWant(const RECT *prcWnd, BOOL bClipped, const RECT *prcClip, RECT *prcWant)
{
    if (!prcWnd || IsRectEmpty(prcWnd)) return FALSE;               /* 0 尺寸窗口本来就不可见：别动平台 */
    if (!bClipped) { SetRect(prcWant, 0, 0, 0, 1); return TRUE; }    /* 完全在外：空区域（零宽矩形） */
    if (覆盖整窗) return FALSE;                                       /* 整窗可见：不需要区域 */
    *prcWant = *prcClip; return TRUE;                                /* 部分重叠：给可行区 */
}

/* 要不要真的下发：只比"期望"与"平台侧现状" */
inline BOOL SbNeedPushClipRgn(const RECT *prcWant /* NULL = 期望无区域 */,
                              BOOL bPushed, const RECT *prcPushed, BOOL bForce)
{
    if (bForce)   return TRUE;                /* app 改了 SetWindowRgn 的区域：强制下发一次 */
    if (!prcWant) return bPushed;             /* 期望无区域：只有平台侧还挂着才需要撤销 */
    if (!bPushed) return TRUE;                /* 期望有区域、平台侧还没有 */
    return !EqualRect(prcWant, prcPushed);    /* 两边都有区域：只有变了才值得下发 */
}
```

`_Window` 上因此记两份状态：`bClipPushed`（平台侧是否挂着本逻辑下发的裁剪区域）与 `rcClipPushed`（挂着的那块矩形）。

**只看期望状态会漏掉"撤销"这一半**，后果是把一个原本正确的功能反向弄坏：窗口先以 **0 尺寸**创建（那时裁剪自然算成空区域并下发），随后被摆到真实几何、变成整窗可见；若这一步被当成"无需动作"跳过去，平台侧就永远挂着建窗时那块空区域，窗口整片不显示。`demos/uieditor` 的真实子窗口正是这样建出来的（`SouiRealWndHandler.cpp` 用空矩形 `Create`、`0,0,0,0` `CreateEx`）——于是它的**所有**子窗口都消失了。

**只问"要不要裁剪"会漏掉"不该下发"这一半**，后果是把平台渲染表面当成"可以随便动"的对象：下发要切 `wantsLayer`、走一次 Shape 请求，而 `SetWindowRgn` 恰恰可能正被 app 在自己的绘制过程中调用（`SHostPresenter::OnHostPresent` 对 `translucent + autoShape` 的宿主**每帧**都会调）。空转的下发既浪费，又会扰动正在排队的绘制与失效安排。`_Window` 上多出来的 `rcClipPushed` 就是为这条"期望 = 现状则不下发"的比较服务的。

（曾经把这条推论延伸成"macOS 的同步重绘把子窗口自己的滚动条打掉了"——那是**错的**，已撤回；滚动条不显示的真正原因与本节无关，见 §8 最后一条。）

> ⚠️ 上面这条推导里的"同步重绘"只有 macOS 有（Linux 的 `SetWindowRgn` 只是排一个 XShape 请求，不触发重绘）。Linux 实测**未因此改善**，见 §8 最后一条：子窗口自带滚动条不显示在 Linux 上另有原因，尚未定位。

修复后，`demos/uieditor` 子窗口（`0` 尺寸建窗 → 定位到真实几何）的时序变成**全程不下发**：

| 时刻 | 窗口有没有面积 | 期望区域 | 平台侧现状 | 动作 |
| --- | --- | --- | --- | --- |
| 建窗 | 没有（0 尺寸） | 不需要 | 无 | **不下发**（旧版这里下发了一块空区域） |
| 定位到真实几何 | 有 | 整窗可见 → 不需要 | 无 | **不下发**（旧版这里会补一次撤销下发，纯属空转） |
| 之后稳定态 | 有 | 整窗可见 → 不需要 | 无 | 不下发 |

真正需要下发的只有另外两档，且各自都只在状态变化时发生一次：子窗口被滚到父客户区之外（下发空区域）、滚回来（下发撤销）。

## 5. 三平台落地

| 平台          | 可见区                                 | 输入区                                    |
| ----------- | ----------------------------------- | -------------------------------------- |
| Linux (X11) | `xcb_shape_rectangles(SK_BOUNDING)` | **无需单独处理**——见下                         |
| macOS       | `CAShapeLayer` 蒙版（`setNsWindowRgn`，只 `setNeedsDisplay`、不强制同步重绘） | 重写 `NSView::hitTest:`，落点不在可行区域返回 `nil` |
| iOS         | `CAShapeLayer` 蒙版（`setUiWindowRgn`） | 重写 `UIView::hitTest:withEvent:`，同上     |

X11 侧的关键依据（X Shape 扩展协议原文）：

> **effective input region**: … When a window has a client input region **or a client bounding region**, the effective input region is the intersection of the default input region, the client input region (if any) and **the client bounding region (if any)**.

即**只设 bounding shape 就会同时收窄输入区**，所以 Linux 不需要额外设 `SK_INPUT`。反过来，若某窗口已经显式设过输入区，两者求交仍然正确——`WS_EX_TRANSPARENT` 的窗口输入区为空，交集仍为空，行为不变。

macOS / iOS 的 `hitTest:` 换算：`point` 在**父视图**坐标系，先 `convertPoint:fromView:self.superview` 到自身视图坐标，再乘 scale 换成物理像素（swinx 的 `RECT` 一律是物理像素，视图坐标是逻辑点）。scale 分别取 `backingScaleFactor` / `screenScale`，与既有 `drawRect:` 的约定一致。

**形状下发（`setNsWindowRgn` / `setUiWindowRgn`）的 scale 换算只在桥接层**：RECT（物理像素）除以本地 scale 变成点再喂给 CAShapeLayer；纵轴方向 macOS 靠 `isFlipped=YES` + AppKit 自动翻转的 backing layer（勿手动取反、勿设 `geometryFlipped`），iOS 的 UIKit 本来就是左上原点（不存在翻转概念）。蒙版要显式 `frame = bounds` + `contentsScale = scale`，撤销只摘 `layer.mask`。两平台各有门禁 `tools/check_macos_shape.py` / `tools/check_ios_shape.py` 锁定这套口径。

## 6. 触发点（客户区或窗口几何一变就要重算）

| 位置                                         | 为什么                  |
| ------------------------------------------ | -------------------- |
| `onStyleChange` 末尾                         | 边框与滚动条都占客户区之外的空间     |
| `WM_WINDOWPOSCHANGED`（`SWP_NOREDRAW` 判断之前） | 位置/尺寸变了，自身与直接子窗口都要重算 |
| `SetParent`                                | 拔插父窗口，裁剪基准跟着换        |
| `UM_SHOWWINDOW`                            | 隐藏期间不必管，重新显示要把裁剪补上   |
| `SetScrollInfo`（仅 `bSwitch`）               | 滚动条出现/消失改变客户区大小      |
| `ShowScrollBar` 末尾                         | 同上                   |
| `SetWindowRgn`（`bForce = TRUE`）            | app 撤销/更换自己的区域       |

## 7. 本次一并修掉的四个缺陷

四个缺陷都属于"让功能在边界上失效"这一类。前两个是平台层的「空区域 ≠ 无区域」，第三个是下发侧的「只下发、不撤销」，第四个是「不该下发的时候也在下发」：

1. **`cocoa/SNsWindow.mm` 与 `ios/SUIWindow.mm`**：`if(prc && nCount)` 把 `nCount == 0` 判成"无区域"而摘掉蒙版。于"子窗口完全滚出客户区"这一档，子窗口会**照常显示**。改为 `if(prc)`——`nCount == 0` 是"空区域"（完全不可见），真正要取消区域时走 `prc == NULL` 分支。这也正是 Win32 语义（`SetWindowRgn` 传空区域 = 窗口整体不可见）。
2. **`linux/SConnection.cpp`**：空区域时 `GetRegionData` 返回 32（`sizeof(RGNDATAHEADER)`）而不是 0，于是 `rects` 被 `resize(0)`，接着 `&rects[0]` 就是**空 vector 取首元素（未定义行为）**。改为长度 0 时显式给一个静态空矩形表。注意这与 `else` 分支的 `xcb_shape_mask(NONE)` 含义不同：后者是"取消形状"，即恢复成普通矩形窗口，不是把窗口裁空。
   （协议上 `ShapeRectangles` 的请求长度定义为 `4+2n`，`n = 0` 即长度 4，是合法请求，服务端据此生成空区域；退一步说，往列表里放一个零面积矩形也同样是空区域。）
3. **`src/wnd.cpp`**：下发后没有撤销通路，详见 §4.4。这条不是平台坑，而是把「下发」当成了幂等的一次性动作——平台侧的区域是**状态**，必须能回到"无区域"。
4. **`src/wnd.cpp`（+ `src/platform/cocoa/SNsWindow.mm`）**：**不该下发的时候也在下发**（§4.4，纯硬化；**不是**"滚动条不显示"的根因）。

   - `SbRefreshWindowClip` 原先只看"期望状态"，于是建窗那一刻（0 尺寸）也下发一次空区域，定位到真实几何后再下发一次撤销——**每个子窗口一生至少白下发两次**；此外 `hAppRgn` 存在时每档都会下发（SOUI 的 autoShape 宿主则是每帧都调 `SetWindowRgn`）。
   - 修法：`SbCalcClipWant` / `SbNeedPushClipRgn` 让"0 尺寸窗口"和"期望 = 现状"两档都不下发（§4.4）；`setNsWindowRgn` 顺手去掉结尾的 `displayIfNeeded`——蒙版是 layer 属性，`setNeedsDisplay` 就够了，同步重绘没有必要（iOS 侧本来就只 `setNeedsDisplay`）。
   - ⚠️ **曾把这一条当成"子窗口自带滚动条不显示"的根因，已撤回**：链条里唯一能"打断上层排队绘制"的动作是 macOS 的 `displayIfNeeded`，而 Linux 的 `SetWindowRgn` 只排一个 XShape 请求、根本不触发重绘，解释不了 Ubuntu 上"现象和上一版本一样"。真正的根因与本节无关，见 §8 最后一条。

## 8. 已知缺口与约束

- **Android / OHOS**：`mobile/SConnection.cpp` 的 `SetWindowRgn` 是空实现（`return TRUE;`），裁剪无法落地。平台无关的规则仍然会算，但平台侧没有接收方。属既有缺口（该平台的 `SetWindowRgn` 本来就不生效）。
- **macOS / iOS 的 `hitTest:` 每帧每次命中测试都算一次裁剪**：`SbGetWindowClientClip` 会沿祖先链调 `GetWindowRect`/`GetClientRect`。窗口层级深且鼠标频繁移动时有额外开销；实测未发现明显问题，若日后有感可加缓存（按窗口几何版本号失效）。
- **X11 下已显式设过 `SK_INPUT` 的窗口**：输入区是"默认输入区 ∩ 客户端输入区 ∩ 客户端 bounding"三者交集，我们的裁剪照样生效；但若某处代码把输入区显式设成了整窗矩形，那部分会"不跟随"裁剪。当前仓库内只有 `SetWindowMsgTransparent` 会设输入区，且它只在**创建期**按窗口类的 `WS_EX_TRANSPARENT` 调一次（设为空区域），不构成冲突。
- ✅ **已定位并已修复：Linux/macOS 上"滚动条（含子窗口自带的）不显示"**。与本节裁剪规则**无关**——裁剪规则只裁"被父客户区挡掉的那部分"，而本缺陷在**没有任何子窗口**的窗口上照样成立。
  - 根因：swinx 画滚动条的唯一实现是 `handlePrint()` 的 `PRF_NONCLIENT` 分支，而 `OnNcPaint` 是用 `SendMessage(hWnd, WM_PRINT, hdc, PRF_NONCLIENT)` 去"请 app 画非客户区"、指望这次重投递落到 `DefWindowProc→handlePrint`。`WM_PRINT` 不是 Win32 的非客户区绘制协议，app 为自己的用途（快照/离屏打印）注册 `WM_PRINT` 处理器并标记"已处理"完全合法——SOUI 的 `SHostWnd` 正是 `MSG_WM_PRINT(OnPrint)`，于是那次重投递被 app 吃掉，`handlePrint` **一次都没执行过**。Windows 上由系统画、swinx 不参与编译，所以这个缺陷一直没暴露。
  - 修法：`OnNcPaint` 直接调用 `handlePrint(hWnd, (WPARAM)hdc, PRF_NONCLIENT)`，不再绕消息。完整推导、边界、为什么不改 SOUI、以及验证方法见 **`swinx-非客户区绘制与WM_PRINT分发缺陷.md`**；门禁 `tools/check_ncpaint_direct.py`。
  - 定位过程中逐条读码排除的嫌疑（均无罪，留作记录）：`showSbFlags` 与创建样式的同步（`wnd.cpp` 建窗时 `showSbFlags |= (cs->style & WS_HSCROLL/WS_VSCROLL)`，`onStyleChange` 也会重算）；`GetScrollBarRect` 的几何（与 `GetClientRect` 的内缩口径一致，`SM_CXVSCROLL == SM_CYHSCROLL == 16`）；`GetDCEx` 的 `DCX_WINDOW` 分支（`ExcludeClipRect(rcClient)` 之后窗口减客户区 = 右侧与底部两条带，滚动条落在里面）；cairo 侧 `GetClipRgn` 导出的坐标空间（`OnNcPaint` 的 DC 在 `SetViewportOrgEx(0,0)` 之后 user == device，与 `rcSb` 同系）。
- **待改（与本次现象无关，独立的 Win32 契约问题）**：`wnd.cpp` 在 `WM_WINDOWPOSCHANGED` 里转发给 app 的 `WM_SIZE` 用了**窗口尺寸**（`MAKELPARAM(wndPos.cx, wndPos.cy)`），而 Win32 契约里 `WM_SIZE` 的 `lParam` 是**客户区尺寸**（`SOUI/Core/SHostWnd.cpp::OnSize` 正是拿它直接 `OnRelayout(CRect(0,0,cx,cy))`）。窗口无边框无滚动条时两者相等，所以一直没暴露；带 `WS_VSCROLL`（如 `demos/uieditor` 的 `designer_wnd`）时差 16px，SOUI 的布局会比真实客户区大一档。
  注意**这不是**本次"滚动条不显示"的原因：SOUI 落屏走的是 `SHostPresenter::OnHostPresent` 的 `GetDC()`（客户区 DC，`GetDCEx` 已 `IntersectClipRect(rcClient)`），画不到非客户区那两条带，覆盖不掉滚动条。修法是把转发用的 `lp2` 换成 `GetClientRect` 的尺寸（**注意内部 `WM_SIZE` 处理仍要用窗口尺寸去设 `wndObj->rc` 和重建 surface**），改前需评估对既有 demo 的影响。

## 9. 验证方法与证据

### 9.1 本机（Windows）能跑的

swinx **在 Windows 上不参与编译**（顶层 `CMakeLists.txt` 只在非 Windows 分支 `add_subdirectory(swinx)`），所以平台文件在本机没有任何编译器会看到。为此提供三道本地门禁：

**回归测试**——`demos/fun_test/test_win_clip.cpp`（新增，19 个用例，归入 `soui-unit` 层，随 `soui-headless` 门禁一起跑）：

- 纯几何 5 条：`child_inside_client_is_untouched`、`child_over_scrollbar_strip_is_clipped`、`child_with_negative_origin_keeps_offset`、`child_fully_outside_client_is_empty`、`child_touching_client_edge_is_not_clipped`
- 真实窗口取数 3 条：`child_over_parent_scrollbar_is_clipped`、`top_level_window_uses_whole_rect`、`clip_follows_child_position`
- 下发判定 11 条（§4.4 的期望/现状比较）：`zero_sized_window_needs_no_clip`、`fully_visible_window_needs_no_clip`、`partially_covered_window_wants_the_clip_rect`、`fully_outside_window_wants_empty_rect`、`no_push_when_nothing_to_clip_and_no_stale_region`、`push_when_stale_region_must_be_cleared`、`push_when_window_needs_clipping`、`no_push_when_region_unchanged`、`push_when_region_changed`、`forced_push_always_pushes`、`zero_sized_child_never_touches_platform`

后 3 条建真窗口取数，在真 Windows 上同样成立（真 Windows 本来就会裁子窗口），所以本机就能跑；只有"裁掉多少"依赖 swinx 的客户区约定，用 `#ifndef _WIN32` 隔离。

```
cmake --build build --config Debug --target fun_test --parallel
./build/bin/Debug/fun_test.exe --gtest_filter=swinx_wnd_clip.*
```

实测：19/19 通过。完整门禁：

```
ctest --test-dir build -C Debug -L "^soui-headless$" --output-on-failure
```

实测：414 条注册、413 条执行、**413 条全通过**、1 条既存 Disabled（`swinx_misc.is_dbcs_lead_byte_codepage_dependent`）。注意多配置生成器下 ctest **必须带 `-C Debug`**，否则所有用例都注册成 `NOT_AVAILABLE`，标签过滤会返回 0。

**测试有效性（变异验证）**——新增用例必须真的能拦住它对应的规则，否则只是"恰好都通过"。这里不用重建工程，而是把 `wndclip.h` 复制到临时目录做变异（仓库文件一个字节都不动）、用 mingw g++ 编译一个只调纯判定的探针程序来跑断言（`tools/check_clip_decisions_mutation.py`）：

| 变异 | 期望结果 | 实测 |
| --- | --- | --- |
| 原始版本 | 断言全过 | 退出码 0，全过 |
| A. 去掉"0 尺寸窗口不要求区域"这一档（回到旧行为） | 断言失败 | `zero-sized window must not want a clip` 失败 |
| B. `SbNeedPushClipRgn` 无条件下发（回到"空转下发"） | 断言失败 | `unchanged region must not be pushed` 失败 |

也就是说 §7 第 4 条的两条新规则各有对应用例守着。同一探针里还反向断言"该下发的两档仍要下发"（撤销残留区域、下发新裁剪），避免把功能一起变异没了。

> 注意：凡是脚本里跑 `cmake --build` 的验证，必须先把 `https_proxy`/`http_proxy` 从**子进程环境**里摘掉，否则 MSBuild 会静默失败、跑到的其实是上一次的旧 exe（看起来"变异也全绿"）。

**编译门禁**——`tools/check_swinx_clip_block.py`：从 `wnd.cpp` 里按锚点抽出裁剪函数块的真实源码，配最小 `WndObj`/`WndMgr` 桩，用 mingw g++ `-fsyntax-only -Wall -Wextra` 编译（含真实 `windows.h` 与真实 `wndclip.h`）。这条曾经抓到一个真缺陷：新函数一开始被**误插进 `ScrollBarHitTest` 函数体内部**（`for` 循环之后、`return -1;` 之前），C++ 不允许嵌套函数定义——因为 Windows 不编译 swinx，这个语法错误在整棵构建树里都是静默的。

**结构门禁**——`tools/check_nested_funcs.py`：剥掉注释与字符串后跟踪花括号深度，报告任何"行首出现函数定义样式但深度不为 0"的行。对反例自测可报错，对当前全部改动文件报全绿。

### 9.2 本机跑不了、需要在对应平台验证的

| 平台    | 怎么验                                                                                                     |
| ----- | ------------------------------------------------------------------------------------------------------- |
| Linux | 构建后运行带滚动条 + 覆盖子窗口的 demo。形状可用 `XShapeGetRectangles(dpy, wid, ShapeBounding, ...)` 查询（探针程序方式）；行为按 §10 清单。 |
| macOS | 运行同一 demo；可见区可查 `view.layer.mask` 是否非空；输入按 §10 清单本地点击验证。                                                |
| iOS   | 同 macOS，另需确认触摸点在可行区域外时 `hitTest:` 返回 `nil`（事件落到下层视图）。                                                   |

### 9.3 临时诊断开关（裁剪规则的 A/B 与现场数据）

`src/wnd.cpp` 顶部（裁剪函数块之前）有一段**临时诊断代码**：一对环境变量开关 + 4 处打印。**问题定位后整块删除**（删的时候注意 `tools/check_swinx_clip_block.py` 的抽取起点——它现在会把这段一并以"位于裁剪块之前"的横幅一起抽进来，删掉后那段特殊处理也要一起删）。

| 环境变量                  | 作用                                                       |
| --------------------- | -------------------------------------------------------- |
| `SWINX_CHILD_CLIP=0`  | 关掉"子窗口客户区裁剪"：本逻辑一律不下发区域，`SetWindowRgn` 恢复"app 区域直通平台"的旧行为。**A/B 判定用**。 |
| `SWINX_CHILD_CLIP_TRACE=1` | 打开 4 处打印（见下），日志前缀分别是 `[swinx-clip]`、`[swinx-ncpaint-send]`、`[swinx-onncpaint]`、`[swinx-print]`，每条都带 `hwnd` + `cls`，父/子窗口一眼可分。 |

```bash
# ① A/B：滚动条是不是被"子窗口裁剪"这事弄没的
SWINX_CHILD_CLIP=0  ./demos/uieditor/uieditor        # 期望：若滚动条回来了 ⇒ 罪魁是本规则；若照旧 ⇒ 与本规则无关
# ② 取现场：把四段日志发回来
SWINX_CHILD_CLIP_TRACE=1 ./demos/uieditor/uieditor 2>&1 | tee /tmp/swinx-trace.log
```

四段日志各回答一个问题：

| 日志                    | 回答了                                                                                                    |
| --------------------- | ------------------------------------------------------------------------------------------------------ |
| `[swinx-clip]`        | 每个子窗口的裁剪判定：是否"整窗可见"、`want` 是哪块矩形、要不要下发（`-> push=1`）。**若某子窗口的 `push=1` 且 `want` 的右边/下边比 `rcWnd` 小 16 或更多，基本就是 (a)。**                 |
| `[swinx-ncpaint-send]` | `WM_PAINT` 收尾发出 `WM_NCPAINT` 时失效区的包围盒，以及**它是不是空的**（`空=1` ⇒ 分支 (b)）。                                         |
| `[swinx-onncpaint]`   | `OnNcPaint` 拿到的 DC 的真实裁剪盒（`dcClipBox`）与 `showSbFlags`。**`dcClipBox` 若等于客户区（右下少了那 16），就是分支 (c)。**               |
| `[swinx-print]`       | 滚动条自身的 `vert/horz` 矩形、是否落在裁剪区里（`in=`）、以及 swinx 收到的 `nMin..nMax/page/pos`。**`in=0` ⇒ 门禁挡掉。** |

⚠️ 还有一种特征与裁剪无关：**有 `[swinx-onncpaint]` 却完全没有 `[swinx-print]`** —— 说明 `OnNcPaint` 进去了、`handlePrint` 却没执行（非客户区绘制被 app 的 `WM_PRINT` 处理器吃掉）。这条曾经就是"Linux/macOS 上滚动条全都不显示"的真正根因，现已在 `OnNcPaint` 里改为直接调用 `handlePrint`（修复后这两条日志会成对出现、`in=1`）。详见 `swinx-非客户区绘制与WM_PRINT分发缺陷.md`。

## 10. 人工验收清单

1. 父窗口滚动条被压在子窗口下的那一段**仍然可见**，不被子窗口盖住。
2. 在该段上按下并拖动：**父窗口**收到滚动条消息（`WM_VSCROLL` + `SBN_THUMBTRACK` 等），子窗口收不到。
3. 子窗口落在父客户区内的部分：显示与命中**均无变化**（无回归）。
4. 子窗口被滚到完全离开父客户区：不再出现在父窗口的边框/滚动条上。
5. 滚动条动态出现/消失（`SetScrollInfo` 切换）：裁剪跟着更新，不残留旧的一档。
6. app 自己调 `SetWindowRgn` 的窗口：app 区域与裁剪区域**取交集**，互不覆盖。
7. `SetParent` 换父窗口后：裁剪基准随之改变。
8. **以 0 尺寸创建、随后定位**的子窗口（`demos/uieditor` 的 real window 路径）：定位后必须正常显示，且**全程不该有任何区域下发**（建窗时的那次下发会被自己的滚动条问题反噬）；`demos/uieditor` 整体可正常打开、编辑器与 designer 子窗口都可见。
9. **自带滚动条**（`WS_VSCROLL`/`WS_HSCROLL`，swinx 画在非客户区）：窗口显示正常的同时，它自己的滚动条也必须正常显示、能拖动——**无论该窗口内部还有没有子窗口**。
   这条与裁剪规则无关（§8 最后一条）：`WS_VSCROLL`/`WS_HSCROLL` 的非客户区绘制曾被 app 的 `WM_PRINT` 处理器整条吃掉，已在 `OnNcPaint` 改为直接调用 `handlePrint`（见 `swinx-非客户区绘制与WM_PRINT分发缺陷.md`）。验收时用 `SWINX_CHILD_CLIP_TRACE=1` 确认 `[swinx-onncpaint]` 与 `[swinx-print]` 成对出现且 `in=1`。

## 附：关键源码索引

| 文件 | 内容 |
| --- | --- |
| `swinx/src/wndclip.h` | 裁剪规则（纯几何 + 两条下发判定，平台无关） |
| `swinx/src/wnd.cpp` | `SbRefreshWindowClip` / `SbRefreshChildClips`、7 个触发点、`SetWindowRgn` 改写；文件顶部另有**临时诊断块**（§9.3，定位完即删） |
| `swinx/src/wndobj.h` / `wndobj.cpp` | `_Window::hAppRgn`（app 区域）、`bClipPushed` / `rcClipPushed`（平台侧现状）及其构造/析构 |
| `swinx/src/platform/linux/SConnection.cpp` | `SetWindowRgn`（XShape bounding）+ 空区域修正 |
| `swinx/src/platform/cocoa/SNsWindow.mm` | `hitTest:`、`setNsWindowRgn` 空区域修正 + 去掉同步重绘 |
| `swinx/src/platform/ios/SUIWindow.mm` | `hitTest:withEvent:`、`setUiWindowRgn` 空区域修正 + 除 `UIScreen.scale` 换点（参照 macOS 定稿口径） |
| `demos/fun_test/test_win_clip.cpp` | 19 条回归用例（5 条几何 + 3 条真实窗口 + 11 条下发判定） |
| `tools/check_swinx_clip_block.py`、`tools/check_nested_funcs.py`、`tools/check_clip_decisions_mutation.py` | Windows 上无法编译 swinx 时的三道本地门禁 |
| `tools/check_ncpaint_direct.py` | 非客户区绘制必须直连 `handlePrint` 的门禁（5 条断言 + 4 项变异） |
| `tools/check_macos_shape.py` | macOS `setNsWindowRgn` 落地约定的门禁（13 条断言 + 9 项变异：除 backingScaleFactor 换点 / 纵轴不取反 / 无 geometryFlipped / 撤销只摘蒙版） |
| `tools/check_ios_shape.py` | iOS `setUiWindowRgn` 落地约定的门禁（11 条断言 + 10 项变异：除 UIScreen.scale 换点 / 纵轴不取反 / 无翻转概念 / 显式 frame + contentsScale / 撤销摘 mask / 不同步重绘） |
| `swinx/doc/swinx-非客户区绘制与WM_PRINT分发缺陷.md` | "滚动条不显示"的真正根因与修法（与本文的裁剪规则无关） |
