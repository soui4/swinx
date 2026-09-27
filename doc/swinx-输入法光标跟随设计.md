## swinx 输入法（IME）候选窗光标跟随设计

> 本文记录 swinx 跨平台 IME 候选窗/组合窗光标跟随的方案设计与数据流。覆盖 macOS 与 Linux 两套实现：macOS 走系统级 `NSTextInputClient` 回调 + 顶层 HIMC 状态；Linux 走 XIM `XNSpotLocation` preedit 定位。坐标约定：macOS 涉及视图 `isFlipped` 与 Retina scale 换算且已踩坑，Linux 窗口原点天然在左上角、无翻转无缩放。

### A.1 目标与背景

soUI4 的 swinx 层通过兼容 Win32 Imm* 接口提供跨平台输入法上下文（HIMC）。此前输入法的候选窗口/组合窗口不会跟随编辑器的光标；输入法始终在固定位置弹出候选窗，遮挡视野。需求：输入法通过 IME 接口从编辑器取光标位置，编辑器主动把光标位置写入 IME 上下文（candidate form），各平台据此把候选窗钉在光标下方。

"光标下方"的精确语义：候选窗应显示在光标所在行的**下一行**，不应覆盖当前输入位置——这要求向上层提交的参考位置是光标矩形的**底部**，而非光标顶部。

### A.2 总体结构与两条数据流

三条正交的写入源 + 各平台消费端：

```
                    ┌─────────────────────────────────────────────┐
  编辑器光标移动      │  swinx 兼容层（全部平台共用的"脱水"接口）        │
                    │                                               │
  SWindow::SetCaretPos ─► SCaret::SetPosition ─► ::SetCaretPos     │
        (SOUI)              (SOUI, FrameToHost)     │               │
                                                    ▼               │
                                  全局系统插入符 m_caretInfo(x,y,    │
                                  nHeight, hOwner)                  │
                                                    │               │
  ricedit 组合窗移动 ─► TxSetCaretPos ─► UpdateCandidateWindowGlue  │
   (富文本 Level-3 IME)             ─► ImmSetCandidateWindow        │
                                      （写入 HIMC candForm[]）      │
                                                    │               │
                                          ┌─────────┴─────────┐     │
                                          ▼                   ▼     │
                                 macOS firstRectCallback  XIM XNSpot│
                                 读 candForm|GetCaretPos    读 spot  │
                                 换算→屏幕坐标              (左上原点)│
                                          │                   │     │
                                          ▼                   ▼     │
                                 系统输入法候选窗偏移       输入法候选窗偏移│
                                 └──────────────────────────┴─────────┘
```

两条活跃数据流：

1. **系统插入符流（通用，本文核心）**：编辑器每移动光标，最终落到 swinx 全局 `SConnection::SetCaretPos(X, Y)`，更新 `m_caretInfo`。该流是 macOS `firstRectForCharacterRange:` 和 Linux `SetCaretPos` 里 XIM spot 推送的公共数据源。
2. **富文本 IME 流（ricedit Level-3）**：ricedit 在组合窗打开/候选窗打开时，把当前光标位置经 `ImmSetCandidateWindow` 写入 HIMC 的 `candForm[]`。仅当 Windows 式 Level-3 IME 流程激活时生效；macOS/Linux 的系统 IME 通常不走该流程（见 A.5）。

### A.3 HIMC 状态模型（全部平台统一）

每窗口持有一个 HIMC（`IMContext`），其字段对应 Win32 `ImmGet/Set*` 系列读取的状态：

```cpp
typedef struct _IMContext : SUnkImpl<IUnknown> {
    BOOL  fOpen;                  // IME 开关
    DWORD fdwConversion, fdwSentence;
    POINT ptStatus;               // 状态窗口位置
    COMPOSITIONFORM  compForm;                       // 组合窗（光标跟随）
    CANDIDATEFORM    candForm[IMC_MAXCANDIDATEWINDOW]; // 候选窗（=4，Win32 约定）
    // Linux 特有：
    xcb_xim_t *xim;  xcb_xic_t xic;   // XIM 连接与输入上下文
    // iOS 特有（随移动端便携层）：
    // ...（mobile 通过 platform_api.ime 回调转发）
} IMContext;
```

生命周期与引用计数（重点，防泄漏）：

- `SConnection::OnWindowCreate`：`wnd->hIMC = ImmCreateContext()`（refcnt=1）。
- `SConnection::OnWindowDestroy`：`ImmDestroyContext(wnd->hIMC)` → `Release()` → 置空。二者严格配对，主引用永不泄漏。
- 临时获取用 `ImmGetContext(hWnd)`（`AddRef`）→ 使用完 `ImmReleaseContext(hWnd, hIMC)`（`Release`），所有调用点左右对称。
- `ImmAssociateContext(hWnd, hIMC)`：交换窗口关联的 IMC。返回旧 HIMC（本函数已 `Release` 旧引用、`AddRef` 新引用并接管 `wndObj->hIMC`）。

### A.4 macOS 实现：`firstRectForCharacterRange:`

macOS 的输入法通过 `NSTextInputClient` 协议询问插入点矩形，系统把所有 IME 的候选/组合窗定位为"该矩形下方"。实现位于 `swinx/src/platform/cocoa/SNsWindow.mm` 的视图（`SNsWindow : NSView`, `isFlipped a=YES`）。

#### 数据源优先级

```cpp
POINT pt = {-1, -1};
HIMC hIMC = ImmGetContext(m_hWnd);
if (hIMC) {
    CANDIDATEFORM cf;
    if (ImmGetCandidateWindow(hIMC, 0, &cf) && cf.dwStyle != CFS_DEFAULT)
        pt = cf.ptCurrentPos;                     // 1. RICEdit 主动写入的候选窗位置
    ImmReleaseContext(m_hWnd, hIMC);
}
if (pt.x < 0 || pt.y < 0) {
    GetCaretPos(&pt);                             // 2. 系统插入符位置（通用）
}
```

1. 优先取 HIMC 候选窗表单（`ImmSetCandidateWindow` 写入的 `cf.ptCurrentPos`）——供 ricedit 等主动报位。
2. 缺省回退到全局系统插入符 `GetCaretPos()`——覆盖所有编辑器的无头光标。

#### 坐标换算（本方案踩坑重点）

数据源 `pt` 是**宿主窗口客户区的物理像素坐标（y 向下）**；`convertRectToScreen:` 期望**视图的逻辑点坐标**。二者存在两个差异，必须逐一消除，否则产生"偏移大、无规律"的症状：

```cpp
float scale = [window backingScaleFactor];
NSRect winFrame = [window frame];
NSRect contentRect = [window contentRectForFrameRect:winFrame];
// (a) 视图原点相对窗口 frame 左上角的偏移（标题栏等；borderless 为 0）
CGFloat offsetX = contentRect.origin.x - winFrame.origin.x;
CGFloat offsetY = (winFrame.origin.y + winFrame.size.height) -
                  (contentRect.origin.y + contentRect.size.height);
rect.origin.x = pt.x / scale - offsetX;
rect.origin.y = pt.y / scale - offsetY;                 // 逻辑点坐标
rect.size.width = 1;
// (b) 高度用系统插入符高度：IME 把候选窗摆在下方，
//     若只返回 1 点高会紧贴光标顶部、覆盖输入位置
int caretH = conn ? conn->GetCaretInfo()->nHeight : 0;
rect.size.height = (caretH > 0 ? caretH : 16) / scale;
// (c) y 翻转：视图是 isFlipped=YES（左上原点），必须先经
//     视图自身 convertRect:toView:nil 转为窗口 base 坐标，
//     再由窗口 convertRectToScreen: 转屏幕；若直接把视图坐标
//     交给 NSWindow 的 convertRectToScreen: 会 y 镜像。
rect = [self convertRect:rect toView:nil];
rect = [window convertRectToScreen:rect];
```

各步的作用与踩过的坑：

- **(a) 逻辑点 vs 物理像素**：swinx 全局（鼠标事件、窗口布局、`m_caretInfo`）统一用物理像素；Retina 屏 `scale=2` 时把像素当点用会整体放大 2 倍，且误差随光标远离窗口左上角线性增大——即最初"偏移大、看不出规律"的根因。故先 `pt / scale` 再扣除窗口偏移。
- **(b) 高度 1 点 vs 光标高度**：候选窗本应钉在光标**下方**；高度仅 1 点时系统把候选窗顶到光标下一行顶部，覆盖当前输入位置且可见一像素闪烁间隙。改回插入符真实高度后候选窗落到整行下方。
- **(c) 用窗口还是视图的 convert**：最初用 `[window convertRectToScreen:]`，它期望**窗口 base 坐标**（左下原点、y 向上），而 rect 是**视图坐标**（`isFlipped=YES`，y 向下）→ 垂直方向上下镜像。必须先用 `[self convertRect:rect toView:nil]` 让视图处理 y 翻转，再交给窗口。

#### 候选项闪烁的那条缝

"候选窗上方总能看到光标的一像素闪烁"——本质是返回矩形高度不足，IME 把候选窗顶到了返回矩形（≈光标顶部）的下一行顶部，那一像素即候选窗与光标的间隙。修复 (b) 后消除。

### A.5 Linux 实现：XIM `XNSpotLocation`

Linux 走 X11 的 XIM（Input Method）协议，候选窗跟随直接用 `XNSpotLocation` preedit 定位——**不需要** macOS 那套"IME 回调查矩形再换算"机制，窗口坐标本来就是左上原点。实现集中在 `swinx/src/platform/linux/SConnection.cpp`。

#### 光标移动即推送

```cpp
BOOL SConnection::SetCaretPos(int X, int Y) {
    if (!m_hFocus) return FALSE;
    m_caretInfo.x = X;  m_caretInfo.y = Y;
    HIMC hIMC = ImmGetContext(m_hFocus);
    if (hIMC && hIMC->xic) {
        // spot 定位在光标行下方（Y + 插入符高度）
        xcb_point_t spot = { (int16_t)X, (int16_t)(Y + m_caretInfo.nHeight) };
        xcb_xim_nested_list nested =
            xcb_xim_create_nested_list(m_xim, XCB_XIM_XNSpotLocation, &spot, NULL);
        xcb_xim_set_ic_values(m_xim, hIMC->xic, nullptr, nullptr,
                              XCB_XIM_XNPreeditAttributes, &nested, nullptr);
        free(nested.data);     // XIM 分配的 nested 列表需释放
    }
    ImmReleaseContext(m_hFocus, hIMC);
    return TRUE;
}
```

data flow：编辑器光标移动 → `SCaret::SetPosition`（`FrameToHost`）→ swinx 全局 `::SetCaretPos` → 本函数 → 把 `(X, Y+caretHeight)` 作为 spot 经 XIM `XNSpotLocation` 推给输入法。输入法据此定位候选/组合窗。同一 `m_caretInfo` 数据源同时喂给 macOS 回调和 Linux spot，两平台行为一致。

#### XIM 接入点（既有，未改）

- `xcb_xim_create` 建连接，`xim_open_callback` 统一用 `XCB_IM_PreeditPosition | XCB_IM_StatusArea` 建输入上下文（IC），并把 `XNSpotLocation` 挂到 `XNPreeditAttributes`。
- `xim_create_ic_callback` 把新建 IC 存到 `hIMC->xic`。
- 键盘事件先经 `xcb_xim_filter_event`/`xcb_xim_forward_event` 转发给输入法。
- 每窗口 `hIMC` 在 `OnWindowCreate` 创建；`ImmDestroyContext` 里 `if (hIMC->xic) xcb_xim_destroy_ic(...)` 释放 IC。

### A.6 富文本（ricedit）主动报位

`third-part/richedit41` 新增 Level-3 候选窗跟随（操作系统无关，经 swinx Imm 接口生效）：

- `CTxtEdit::TxSetCaretPos` 末尾调 `UpdateCandidateWindowGlue`（判空保护）。光标移动后若候选窗已开（`_fCandidateOpen`），重新 `SetCandidateWindow` 重定位。
- `SetCandidateWindow` 计算候选窗位置（优先 `GetPoint(tomClientCoord)` 取光标底部、失败则用客户端矩形底部），写入 `CANDIDATEFORM{ dwStyle=CFS_CANDIDATEPOS, ptCurrentPos } ` 并经 `ImmSetCandidateWindow` 落到 HIMC 的 `candForm[]`。
- `CIme_Lev3` 构造初始 `_fCandidateOpen=FALSE, _indexCandidate=0`；`IMENotify` 的 OPEN/CLOSE 分支维护该状态，CLOSE 时复位回 `CFS_DEFAULT`。

注意：macOS/Linux 的系统 IME 通常不走 Windows 式 Level-3 组合流程（动态直接回灌 `WM_IME_CHAR`），此时该流处于休眠，firstRect 走 `GetCaretPos()` 兜底；`candForm[]` 路径主要为需要主动报位的宿主保留。

### A.7 三平台涵盖

| 平台 | 机制 | 坐标换算 | 状态载体 |
|---|---|---|---|
| macOS | `NSTextInputClient firstRectForCharacterRange:` | 需分量 scale + 扣窗口偏移 + y 翻转（见 A.4） | HIMC `candForm[]` / 全局 `m_caretInfo` |
| Linux | XIM `XNSpotLocation` preedit | 左上原点，无需换算 | `m_caretInfo`（Y+caretHeight） |
| iOS | 移动端便携层 `platform_api.ime.*` 回调（`Imm*` 转发） | 移动端上下文处理 | 同上 |

### A.8 关键文件索引

- 通用 HIMC 状态：`swinx/src/platform/{cocoa,ios,linux}/SImContext.h`、`imm.{mm,cpp}`
- HIMC 生命周期与光标：`swinx/src/platform/{cocoa,ios,linux}/SConnection.{mm,cpp}`
- macOS 回调：`swinx/src/platform/cocoa/SNsWindow.mm`（`firstRectForCharacterRange:`）
- Linux XIM spot：`swinx/src/platform/linux/SConnection.cpp`（`SetCaretPos`、`xim_open_callback`、`imm.cpp:ImmDestroyContext`）
- 富文本报位：`third-part/richedit41/re41/ime.cpp`（`CIme_Lev3::SetCandidateWindow`/`UpdateCandidateWindow`）、`edit.cpp:TxSetCaretPos`
- 光标源头：`SOUI/src/core/SCaret.cpp`（`SetPosition`→`::SetCaretPos`）

### A.9 验证方式

- **行为验证（人工，GUI）**：在 Retina 屏 macOS 上运行 `demos/sci_demo`，打开输入法，移动光标到窗口顶部/中部/底部及多行文本处，分别核对候选窗跟随、始终位于光标行下方、不覆盖输入位置。Linux 用 fcitx/ibus 在 GUI 环境做同验证。
- **坐标自检**：`firstRectForCharacterRange:` 返回矩形应覆盖整个光标字符（高度≈插入符高度）；光标在窗口中部时返回的屏幕 y 应等于窗口客户区顶部 + 光标偏移（经 scale）。
- **回归关注点**：Retina/非 Retina 屏幕 scale 差异、带标题栏（offsetX/Y 非 0）与 borderless 窗口、组合窗和候选窗同时打开、光标紧贴窗口左/下边缘。
- **内存**：窗口开合循环 + 反复触发 IME，确认 HIMC refcnt 不涨（戳 `ImmAssociateContext/ImmGetContext` 配对）。