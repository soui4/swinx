# swinx API 实现清单

> **代码基线**：swinx @ 2026-10-26（soui4 无头门禁 504 用例全部通过；主进程 valgrind `definitely lost: 0`）  
> **统计口径**：以 `swinx/include/*.h` 对外声明的 Win32 兼容函数为准，在 `swinx/src/**` 中定位定义并按函数体核定实现程度。宏与头内 inline（`LOWORD`、`HIWORD`、`SNDMSG`、`IsEqualGUID`、`DEFINE_GUID` 等）不计入。  
> **本文由扫描工具生成后人工核定**：工具见 `doc/tools/api_scan.py`（扫描）、`doc/tools/api_verify.py`（明细核对）。

## 1. 实现程度分级

| 级别 | 含义 |
|---|---|
| 实现 | 有完整逻辑，语义对应 Win32 行为 |
| 简单实现 | 提供了有意义的行为，但比 Win32 简化（如结构拷贝、状态存取、errno 模拟错误码） |
| 部分实现 | 主流程可用，个别分支/参数组合未实现并记录日志 |
| 空实现 / 语义桩 | 返回固定值或空操作；或为初始化协议占位（详见第 5 节） |
| 未提供 | 仅在头文件中保留声明以维持 Windows 头兼容，swinx 无符号定义，SOUI 当前未引用 |

## 2. 总览

| 分级 | 数量 |
|---|---|
| 实现 | 1003 |
| 简单实现 | 86 |
| 部分实现 | 12 |
| 空实现 / 语义桩 | 41 |
| 未提供（仅声明） | 126 |
| **合计（有定义）** | **1142** |

## 3. 模块分布

| 模块 | 实现 | 简单实现 | 部分实现 | 空实现 |
|---|---|---|---|---|
| 窗口、消息与用户界面（USER32 等价） | 197 | 8 | 0 | 6 |
| 图形设备接口（GDI32 等价） | 173 | 26 | 2 | 6 |
| 系统、文件、进程与线程（KERNEL32 等价） | 304 | 17 | 5 | 2 |
| COM / OLE（OLE32·OLEAUT32 等价） | 56 | 8 | 0 | 4 |
| 通用控件、Shell 与公共对话框（COMCTL32·SHELL32·SHLWAPI 等价） | 77 | 18 | 4 | 2 |
| 多媒体、资源及其它（WINMM·杂项） | 196 | 9 | 1 | 21 |

## 4. 模块 API 明细

> 「简单实现」指语义成立但比 Win32 简化（结构拷贝、状态存取、固定口径返回等）；表中未加备注的行其行为与 Win32 文档语义一致。

### 4.1 窗口、消息与用户界面（USER32 等价）

覆盖窗口类注册、窗口创建/销毁、窗口属性与关系查询、消息队列与消息循环、消息投递/发送、定时器、菜单、滚动条、剪切板钩子入口、鼠标键盘状态、显示器枚举（EnumDisplayMonitors/GetMonitorInfo 等）与系统参数。窗口子系统核心在 `wnd.cpp`/`wndobj.cpp`，平台差异收敛在 `src/platform/*`。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `AdjustWindowRectEx` | 实现 | src/wnd.cpp | 由客户区矩形反推窗口矩形：只有 WS_BORDER（swinx 自绘的那圈边框）会让矩形四周各外扩 SM_CXEDGE/SM_CYEDGE；标题栏与调整边框由原生窗口管理器画在窗口矩形之外、菜单栏不自绘，故这三项贡献为 0（详见 src/wnd.cpp 的说明） |
| `AnimateWindow` | 实现 | src/wnd.cpp |  |
| `AppendMenuA` | 实现 | src/cmnctl32/menu.cpp |  |
| `AppendMenuW` | 实现 | src/cmnctl32/menu.cpp |  |
| `BeginPaint` | 实现 | src/wnd.cpp |  |
| `BringWindowToTop` | 实现 | src/wnd.cpp |  |
| `CallHook` | 实现 | src/hook.cpp |  |
| `CallNextHookEx` | 实现 | src/hook.cpp |  |
| `CallWindowProc` | 实现 | src/wnd.cpp |  |
| `CheckMenuItem` | 实现 | src/cmnctl32/menu.cpp |  |
| `CheckMenuRadioItem` | 实现 | src/cmnctl32/menu.cpp |  |
| `ClientToScreen` | 实现 | src/wnd.cpp |  |
| `CreateCaret` | 实现 | src/wnd.cpp |  |
| `CreatePopupMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `CreateWindowA` | 实现 | src/wnd.cpp |  |
| `CreateWindowExA` | 实现 | src/wnd.cpp |  |
| `CreateWindowExW` | 实现 | src/wnd.cpp |  |
| `CreateWindowW` | 实现 | src/wnd.cpp |  |
| `DefWindowProc` | 实现 | src/wnd.cpp |  |
| `DeleteMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `DestroyCaret` | 实现 | src/wnd.cpp |  |
| `DestroyMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `DestroyWindow` | 实现 | src/wnd.cpp |  |
| `EnableMenuItem` | 实现 | src/cmnctl32/menu.cpp |  |
| `EnableScrollBar` | 实现 | src/wnd.cpp |  |
| `EnableWindow` | 实现 | src/wnd.cpp |  |
| `EndMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `EndPaint` | 实现 | src/wnd.cpp |  |
| `EnumChildWindows` | 实现 | src/wnd.cpp |  |
| `EnumDisplayDevicesA` | 实现 | src/multimon.cpp |  |
| `EnumDisplayMonitors` | 实现 | src/multimon.cpp |  |
| `EnumDisplaySettingsA` | 实现 | src/multimon.cpp |  |
| `EnumDisplaySettingsW` | 实现 | src/multimon.cpp |  |
| `EnumWindows` | 实现 | src/wnd.cpp |  |
| `FindWindowA` | 实现 | src/wnd.cpp |  |
| `FindWindowExA` | 实现 | src/wnd.cpp |  |
| `FindWindowExW` | 实现 | src/wnd.cpp |  |
| `FindWindowW` | 实现 | src/wnd.cpp |  |
| `FlashWindow` | 实现 | src/wnd.cpp |  |
| `FlashWindowEx` | 实现 | src/wnd.cpp |  |
| `GetActiveWindow` | 实现 | src/wnd.cpp |  |
| `GetAncestor` | 实现 | src/wnd.cpp |  |
| `GetCapture` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `GetCaretBlinkTime` | 实现 | src/wnd.cpp |  |
| `GetCaretPos` | 实现 | src/wnd.cpp |  |
| `GetClassLongA` | 实现 | src/wnd.cpp |  |
| `GetClassLongPtrA` | 实现 | src/wnd.cpp |  |
| `GetClassLongPtrW` | 实现 | src/wnd.cpp |  |
| `GetClassLongW` | 实现 | src/wnd.cpp |  |
| `GetClassNameA` | 实现 | src/wnd.cpp |  |
| `GetClassNameW` | 实现 | src/wnd.cpp |  |
| `GetClientRect` | 实现 | src/wnd.cpp |  |
| `GetCursorPos` | 实现 | src/wnd.cpp |  |
| `GetDC` | 实现 | src/wnd.cpp |  |
| `GetDCEx` | 实现 | src/wnd.cpp |  |
| `GetDesktopWindow` | 实现 | src/wnd.cpp |  |
| `GetDlgCtrlID` | 实现 | src/wnd.cpp |  |
| `GetDlgItem` | 实现 | src/wnd.cpp |  |
| `GetDpiForWindow` | 实现 | src/wnd.cpp |  |
| `GetFocus` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `GetForegroundWindow` | 实现 | src/wnd.cpp |  |
| `GetGuiResources` | 实现 | src/handle.cpp | GR_GDIOBJECTS 返回 swinx 自己登记的 GDI 句柄计数（供 WebCore 的 GDIObjectCounter 做 Debug 泄漏观察）；GR_USEROBJECTS 未统计，恒返回 0。hProcess 只可能来自 GetCurrentProcess()，不校验 |
| `GetMenuContextHelpId` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuInfo` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuItemCount` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuItemID` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuItemInfoA` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuItemInfoW` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuStringA` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMenuStringW` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetMessagePos` | 实现 | src/wnd.cpp |  |
| `GetMessageTime` | 实现 | src/wnd.cpp |  |
| `GetMonitorInfoA` | 实现 | src/multimon.cpp |  |
| `GetMonitorInfoW` | 实现 | src/multimon.cpp |  |
| `GetParent` | 实现 | src/wnd.cpp |  |
| `GetPropA` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `GetPropW` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `GetRawInputDeviceInfoA` | 实现 | src/winuser.cpp |  |
| `GetRawInputDeviceInfoW` | 实现 | src/winuser.cpp |  |
| `GetRawInputDeviceList` | 实现 | src/winuser.cpp |  |
| `GetScrollInfo` | 实现 | src/wnd.cpp |  |
| `GetScrollPos` | 实现 | src/wnd.cpp |  |
| `GetScrollRange` | 实现 | src/wnd.cpp |  |
| `GetSubMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `GetSystemMenu` | 实现 | src/wnd.cpp |  |
| `GetTickCount` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetTickCount64` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetUpdateRect` | 实现 | src/wnd.cpp |  |
| `GetUpdateRgn` | 实现 | src/wnd.cpp |  |
| `GetWindow` | 实现 | src/wnd.cpp |  |
| `GetWindowDC` | 实现 | src/wnd.cpp |  |
| `GetWindowLongA` | 实现 | src/wnd.cpp |  |
| `GetWindowLongPtrA` | 实现 | src/wnd.cpp |  |
| `GetWindowLongPtrW` | 实现 | src/wnd.cpp |  |
| `GetWindowLongW` | 实现 | src/wnd.cpp |  |
| `GetWindowPlacement` | 实现 | src/wnd.cpp |  |
| `GetWindowRect` | 实现 | src/wnd.cpp |  |
| `GetWindowTextA` | 实现 | src/wnd.cpp |  |
| `GetWindowTextLengthA` | 实现 | src/wnd.cpp |  |
| `GetWindowTextLengthW` | 实现 | src/wnd.cpp |  |
| `GetWindowTextW` | 实现 | src/wnd.cpp |  |
| `GetWindowThreadProcessId` | 实现 | src/wnd.cpp |  |
| `HideCaret` | 实现 | src/wnd.cpp |  |
| `InsertMenuA` | 实现 | src/cmnctl32/menu.cpp |  |
| `InsertMenuItemA` | 实现 | src/cmnctl32/menu.cpp |  |
| `InsertMenuItemW` | 实现 | src/cmnctl32/menu.cpp |  |
| `InsertMenuW` | 实现 | src/cmnctl32/menu.cpp |  |
| `IntersectRect` | 实现 | src/winuser.cpp |  |
| `InvalidateRect` | 实现 | src/wnd.cpp |  |
| `IsChild` | 实现 | src/wnd.cpp |  |
| `IsIconic` | 实现 | src/wnd.cpp |  |
| `IsMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `IsWindow` | 实现 | src/wnd.cpp |  |
| `IsWindowEnabled` | 实现 | src/wnd.cpp |  |
| `IsWindowVisible` | 实现 | src/wnd.cpp |  |
| `IsZoomed` | 实现 | src/wnd.cpp |  |
| `KillTimer` | 实现 | src/wnd.cpp |  |
| `MapWindowPoints` | 实现 | src/wnd.cpp |  |
| `MessageBeep` | 实现 | src/platform/*/sysapi.cpp | 转发平台实现 swinx_messageBeep：Linux 走 X11 Bell 请求、macOS 走 NSBeep()、iOS 走 AudioServicesPlayAlertSound，Android / OHOS 经 g_platformAPI.audio.messageBeep 交宿主应用发声（契约见 src/SwinxUtils.h） |
| `MessageBoxA` | 实现 | src/cmnctl32/msgbox.cpp |  |
| `MessageBoxW` | 实现 | src/cmnctl32/msgbox.cpp |  |
| `ModifyMenuA` | 实现 | src/cmnctl32/menu.cpp |  |
| `ModifyMenuW` | 实现 | src/cmnctl32/menu.cpp |  |
| `MonitorFromPoint` | 实现 | src/multimon.cpp |  |
| `MonitorFromRect` | 实现 | src/multimon.cpp |  |
| `MonitorFromWindow` | 实现 | src/multimon.cpp |  |
| `MoveWindow` | 实现 | src/wnd.cpp |  |
| `PostMessageA` | 实现 | src/wnd.cpp |  |
| `PostMessageW` | 实现 | src/wnd.cpp |  |
| `PostQuitMessage` | 实现 | src/wnd.cpp |  |
| `RegisterVirtualHWND` | 实现 | src/wnd.cpp |  |
| `RegisterWindowMessageA` | 实现 | src/wnd.cpp |  |
| `RegisterWindowMessageW` | 实现 | src/wnd.cpp |  |
| `ReleaseCapture` | 实现 | src/wnd.cpp |  |
| `ReleaseDC` | 实现 | src/wnd.cpp |  |
| `RemoveMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `RemovePropA` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `RemovePropW` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `ScreenToClient` | 实现 | src/wnd.cpp |  |
| `SendMessageA` | 实现 | src/wnd.cpp |  |
| `SendMessageCallbackA` | 实现 | src/wnd.cpp |  |
| `SendMessageCallbackW` | 实现 | src/wnd.cpp |  |
| `SendMessageTimeoutA` | 实现 | src/wnd.cpp |  |
| `SendMessageTimeoutW` | 实现 | src/wnd.cpp |  |
| `SendMessageW` | 实现 | src/wnd.cpp |  |
| `SetActiveWindow` | 实现 | src/wnd.cpp |  |
| `SetCapture` | 实现 | src/wnd.cpp |  |
| `SetCaretBlinkTime` | 实现 | src/wnd.cpp |  |
| `SetCaretPos` | 实现 | src/wnd.cpp |  |
| `SetClassLongA` | 实现 | src/wnd.cpp |  |
| `SetClassLongPtrA` | 实现 | src/wnd.cpp |  |
| `SetClassLongPtrW` | 实现 | src/wnd.cpp |  |
| `SetClassLongW` | 实现 | src/wnd.cpp |  |
| `SetFocus` | 实现 | src/wnd.cpp |  |
| `SetForegroundWindow` | 实现 | src/wnd.cpp |  |
| `SetLayeredWindowAttributes` | 实现 | src/wnd.cpp |  |
| `SetMenuContextHelpId` | 实现 | src/cmnctl32/menu.cpp |  |
| `SetMenuInfo` | 实现 | src/cmnctl32/menu.cpp |  |
| `SetMenuItemInfoA` | 实现 | src/cmnctl32/menu.cpp |  |
| `SetMenuItemInfoW` | 实现 | src/cmnctl32/menu.cpp |  |
| `SetParent` | 实现 | src/cmnctl32/menu.cpp 等 2 处 |  |
| `SetPropA` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `SetPropW` | 实现 | src/wnd.cpp（另有 1 个平台分支） |  |
| `SetScrollInfo` | 实现 | src/wnd.cpp |  |
| `SetScrollPos` | 实现 | src/wnd.cpp |  |
| `SetScrollRange` | 实现 | src/wnd.cpp |  |
| `SetTimer` | 实现 | src/wnd.cpp |  |
| `SetWinEventHook` | 实现 | src/oleacc.cpp |  |
| `SetWindowLongA` | 实现 | src/wnd.cpp |  |
| `SetWindowLongPtrA` | 实现 | src/wnd.cpp |  |
| `SetWindowLongPtrW` | 实现 | src/wnd.cpp |  |
| `SetWindowLongW` | 实现 | src/wnd.cpp |  |
| `SetWindowPlacement` | 实现 | src/wnd.cpp |  |
| `SetWindowPos` | 实现 | src/wnd.cpp |  |
| `SetWindowRgn` | 实现 | src/wnd.cpp |  |
| `SetWindowTextA` | 实现 | src/wnd.cpp |  |
| `SetWindowTextW` | 实现 | src/wnd.cpp |  |
| `SetWindowsHookA` | 实现 | src/hook.cpp |  |
| `SetWindowsHookExA` | 实现 | src/hook.cpp |  |
| `SetWindowsHookExW` | 实现 | src/hook.cpp |  |
| `SetWindowsHookW` | 实现 | src/hook.cpp |  |
| `ShowCaret` | 实现 | src/wnd.cpp |  |
| `ShowCursor` | 实现 | src/winuser.cpp |  |
| `ShowScrollBar` | 实现 | src/wnd.cpp |  |
| `ShowSoftKeyboard` | 实现 | src/winuser.cpp |  |
| `ShowWindow` | 实现 | src/wnd.cpp |  |
| `SubtractRect` | 实现 | src/winuser.cpp |  |
| `TrackMouseEvent` | 实现 | src/wnd.cpp |  |
| `TrackPopupMenu` | 实现 | src/cmnctl32/menu.cpp |  |
| `TrackPopupMenuEx` | 实现 | src/cmnctl32/menu.cpp |  |
| `UnhookWinEvent` | 实现 | src/oleacc.cpp |  |
| `UnhookWindowsHookEx` | 实现 | src/hook.cpp |  |
| `UnionRect` | 实现 | src/winuser.cpp |  |
| `UnregisterVirtualHWND` | 实现 | src/wnd.cpp |  |
| `UpdateWindow` | 实现 | src/wnd.cpp |  |
| `WindowFromPoint` | 实现 | src/wnd.cpp |  |
| `getAppleHostWindow` | 实现 | src/platform/*/SNsWindow.mm 等 2 处 |  |
| `CopyRect` | 简单实现 | src/winuser.cpp | memcpy 结构拷贝 |
| `EqualRect` | 简单实现 | src/winuser.cpp | 逐成员比较 |
| `InflateRect` | 简单实现 | src/winuser.cpp |  |
| `IsRectEmpty` | 简单实现 | src/winuser.cpp | 逐成员比较 |
| `OffsetRect` | 简单实现 | src/winuser.cpp |  |
| `PtInRect` | 简单实现 | src/winuser.cpp |  |
| `SetRect` | 简单实现 | src/winuser.cpp |  |
| `SetRectEmpty` | 简单实现 | src/winuser.cpp | 逐成员置零 |
| `EnumDisplayDevicesW` | 空实现 | src/multimon.cpp | 恒返回 FALSE，枚举显示器请使用 EnumDisplayMonitors |
| `IsWindowUnicode` | 空实现 | src/wnd.cpp | 恒返回 FALSE（swinx 窗口内部统一 UTF-8 存储，非 Win32 的 Unicode/ANSI 双轨制） |
| `ScrollWindowEx` | 空实现 | src/wnd.cpp | 恒返回 0，不执行窗口滚动（SOUI 滚动走自己的失效/重绘路径） |
| `SendNotifyMessageA` | 空实现 | src/wnd.cpp | 恒返回 FALSE，不投递消息 |
| `SendNotifyMessageW` | 空实现 | src/wnd.cpp | 恒返回 FALSE，不投递消息 |
| `SetMenuItemBitmaps` | 空实现 | src/cmnctl32/menu.cpp | 恒返回 FALSE，不支持菜单项位图 |

### 4.2 图形设备接口（GDI32 等价）

覆盖 DC 创建与属性、画笔/画刷/字体/位图等 GDI 对象、区域（region.cpp 独立实现）、文本绘制（cairo/Apple CoreText 两条后端）、位块与拉伸传输、GetDIBits/SetDIBits 等。后端位于 `src/gdi/cairo` 与 `src/gdi/apple`。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `AbortPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `AddFontMemResourceEx` | 实现 | src/gdi/apple/gdi.cpp | 仅 CoreText 后端（src/gdi/apple/gdi.cpp）有定义：CGFont 从内存缓冲建出后经 CTFontManagerRegisterGraphicsFont 注册，*pNumFonts 返回 1，句柄是 FontMemResource*。**cairo 后端没有同名实现，Linux 上引用即链接失败** |
| `AddFontResourceA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `AddFontResourceExA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `AddFontResourceExW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `AddFontResourceW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `AlphaBlend` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Arc` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `BeginPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `BitBlt` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Chord` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ClearRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CloseEnhMetaFile` | 实现 | src/metafile.cpp |  |
| `CloseFigure` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CombineRgn` | 实现 | src/region.cpp |  |
| `CreateBitmap` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateCompatibleBitmap` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateCompatibleDC` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateDIBPatternBrush` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateDIBPatternBrushPt` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateDIBSection` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateDIBSectionEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateDIBitmap` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateEllipticRgn` | 实现 | src/region.cpp |  |
| `CreateEllipticRgnIndirect` | 实现 | src/region.cpp |  |
| `CreateEnhMetaFileA` | 实现 | src/metafile.cpp |  |
| `CreateEnhMetaFileW` | 实现 | src/metafile.cpp |  |
| `CreateFontA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateFontIndirectA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateFontIndirectW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateFontW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateGradientBrush` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateICA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateICW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreateIconFromResource` | 实现 | src/platform/*/cursoricon.cpp 等 2 处 |  |
| `CreateIconFromResourceEx` | 实现 | src/platform/*/cursoricon.cpp |  |
| `CreateIconIndirect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreatePatternBrush` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreatePatternBrush2` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreatePen` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreatePenIndirect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `CreatePolygonRgn` | 实现 | src/region.cpp |  |
| `CreateRectRgn` | 实现 | src/region.cpp |  |
| `CreateRectRgnIndirect` | 实现 | src/region.cpp |  |
| `CreateRoundRectRgn` | 实现 | src/region.cpp |  |
| `CreateSolidBrush` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DPtoLP` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DeleteEnhMetaFile` | 实现 | src/metafile.cpp |  |
| `DeleteObject` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DestroyIcon` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawBitmap9Patch` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawBitmapEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawEdge` | 实现 | src/uitools.cpp |  |
| `DrawFocusRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawFrameControl` | 实现 | src/uitools.cpp |  |
| `DrawIcon` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawIconEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawTextA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `DrawTextW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Ellipse` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `EndPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `EnumEnhMetaFile` | 实现 | src/metafile.cpp |  |
| `EnumFontFamiliesExA` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `EnumFontFamiliesExW` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `EqualRgn` | 实现 | src/region.cpp |  |
| `ExcludeClipRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ExtCreatePen` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ExtCreateRegion` | 实现 | src/region.cpp |  |
| `ExtSelectClipRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ExtTextOutA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ExtTextOutW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `FillPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `FillRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `FillRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `FrameRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `FrameRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCharWidthA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCharWidthI` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetCharWidthW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetClipBox` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetClipRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCurrentObject` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCurrentPositionEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetDIBits` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetDeviceCaps` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
<<<<<<< HEAD
| `GetFontUnicodeRanges` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
=======
| `GetFontData` | 实现 | src/fontapi.cpp | 只读 sfnt 表（非 sfnt 字体与 dwTable=0 的「整文件」请求返回 GDI_ERROR，因此 Type1 判定恒为 false）。GDI 的 dwTable 是 MAKEFOURCC 口径（首字符在最低字节），FreeType 的 FT_MAKE_TAG 相反，实现先按 Win32 口径翻转查一次、失败再用原值查一次，两种写法都接受 |
| `GetFontUnicodeRanges` | 实现 | src/fontapi.cpp |  |
>>>>>>> d2c9a09 (add agents.md)
| `GetGdiObjPtr` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 | GDI 句柄表内部辅助 |
| `GetGlyphIndicesA` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetGlyphIndicesW` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetGlyphOutlineA` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetGlyphOutlineW` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetIconInfo` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetObjectA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetObjectType` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetObjectW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetOutlineTextMetricsA` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetOutlineTextMetricsW` | 实现 | src/gdi/apple/fontapi.cpp 等 2 处 |  |
| `GetPixel` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetRegionData` | 实现 | src/region.cpp |  |
| `GetRgnBox` | 实现 | src/region.cpp |  |
| `GetStockObject` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetSysColor` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetSysColorBrush` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetSysColorPen` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTabbedTextExtentA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTabbedTextExtentW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextExtentExPointA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextExtentExPointW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextExtentPoint32A` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextExtentPoint32W` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextFaceA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextFaceW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextMetricsA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextMetricsW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GradientFill` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `InitGdiObj` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `IntersectClipRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `InvertRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `InvertRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `LPtoDP` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `LineTo` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `LoadBitmapA` | 实现 | src/platform/*/cursoricon.cpp 等 2 处 |  |
| `LoadBitmapW` | 实现 | src/platform/*/cursoricon.cpp |  |
| `LoadImageA` | 实现 | src/platform/*/cursoricon.cpp 等 2 处 |  |
| `LoadImageBuf` | 实现 | src/platform/*/cursoricon.cpp 等 2 处 |  |
| `LoadImageW` | 实现 | src/platform/*/cursoricon.cpp |  |
| `LookupIconIdFromDirectoryEx` | 实现 | src/platform/*/cursoricon.cpp |  |
| `MarkPixmapDirty` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `ModifyWorldTransform` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `MoveToEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `OffsetRgn` | 实现 | src/region.cpp |  |
| `OffsetViewportOrgEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PaintRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PatBlt` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PathToRegion` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Pie` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PolyBezier` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PolyBezierTo` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PolyDraw` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Polygon_Priv` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `Polyline` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `PtInRegion` | 实现 | src/region.cpp |  |
| `RectInRegion` | 实现 | src/region.cpp |  |
| `Rectangle` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `RefGdiObj` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `RemoveFontMemResourceEx` | 实现 | src/gdi/apple/gdi.cpp | 仅 CoreText 后端有定义：CTFontManagerUnregisterGraphicsFont + CGFontRelease + delete，句柄为 AddFontMemResourceEx 的返回值 |
| `RestoreDC` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `RgnComplexity` | 实现 | src/region.cpp |  |
| `RoundRect` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SaveDC` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SelectClipPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SelectClipRgn` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SelectObject` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetAntialiasMode` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetGdiObjPtr` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 | GDI 句柄表内部辅助 |
| `SetMiterLimit` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetPixel` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetRectRgn` | 实现 | src/region.cpp |  |
| `SetViewportOrgEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetWindowOrgEx` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetWorldTransform` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `StretchBlt` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `StretchDIBits` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `StrokeAndFillPath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `StrokePath` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `TabbedTextOutA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `TabbedTextOutW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `TextOutA` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `TextOutW` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `TransparentBlt` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `UpdateDIBPixmap` | 实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `UpdateLayeredWindow` | 实现 | src/platform/*/sysbridge.cpp |  |
| `DeleteDC` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GdiFlush` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 TRUE：cairo/xcb 与 CoreGraphics 的绘制都是同步下发，没有待冲刷的 GDI 队列。WebKit 只把它当成功标志用 |
| `GetAntialiasMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetBkColor` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetBkMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetBrushOrgEx` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCharWidth32A` | 简单实现 | src/fontapi.cpp | 转发 GetCharWidthA：Win32 里 32 位版本与旧版语义相同，只是宽度按 32 位整数给出 |
| `GetCharWidth32W` | 简单实现 | src/fontapi.cpp | 转发 GetCharWidthW |
| `GetGraphicsMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 读取 DC 记录的 graphicsMode（纯状态存取，两种模式行为相同）；NULL HDC 返回 0 |
| `GetMapMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 MM_TEXT（唯一支持的映射模式），NULL HDC 返回 0 |
| `GetMiterLimit` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetNearestColor` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 无调色板概念，原样返回请求颜色 |
| `GetPath` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextAlign` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetTextColor` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetViewportOrgEx` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetWorldTransform` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetBkColor` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetBkMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetBrushOrgEx` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetGraphicsMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 记录并返回旧的 graphicsMode，GM_ADVANCED / GM_COMPATIBLE 下绘制结果一致（world transform 始终直接合成进后端 CTM）。NULL HDC 必须按 Win32 语义置 ERROR_INVALID_HANDLE 并返回 0 而不解引用——WebKit 的 syncContext() 对 ImageBuffer 就是这么调的；也必须返回成功，否则 cairo 的 _get_global_font_dc() 认定 DC 不可用而丢弃它，留下悬空静态句柄 |
| `SetMapMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 | 只接受 MM_TEXT 并返回它，其它模式返回 0 且不改变状态——逻辑单位 = 设备像素是本层所有绘制的前提 |
| `SetPolyFillMode` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetROP2` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetTextAlign` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `SetTextColor` | 简单实现 | src/gdi/apple/gdi.cpp 等 2 处 |  |
| `GetCharacterPlacementA` | 部分实现 | src/fontapi.cpp | ANSI 入参按 UTF-8 转宽字符后复用 W 版实现 |
| `GetCharacterPlacementW` | 部分实现 | src/fontapi.cpp | 逐字符 charmap + horiAdvance 累加：不做连字 / 字距 / bidi / kinsoku，dwFlags 全部忽略；按 nMaxExtent 截断并回填 nMaxFit，lpDx/lpGlyphs 缓冲不足时按 Win32 约定把 lpGlyphs 置 NULL 并回传所需数量；返回值按 Win32 打包——低 16 位是总宽度、高 16 位是拟合字数 |
| `ExtEscape` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 0（含 QUERYESCSUPPORT 探测）：本层没有打印机转义通道，如实报「不支持」比伪造成功安全 |
| `RealizePalette` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 0（无调色板概念） |
| `SelectPalette` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 NULL（无调色板概念） |
| `SetStretchBltMode` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 两个后端都只返回 COLORONCOLOR：拉伸采样由 cairo / CoreGraphics 决定，没有可切换的拉伸算法；调用方设置的黑白/颜色模式不会被记住 |
| `SetWindowExtEx` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 FALSE，不支持窗口坐标 extents 变换 |
| `WidenPath` | 空实现 | src/gdi/apple/gdi.cpp 等 2 处 | 恒返回 FALSE：路径转轮廓只在 Win32 打印路径上被用到，本层没有打印机 DC |

### 4.3 系统、文件、进程与线程（KERNEL32 等价）

覆盖文件读写与查找、内存映射与共享内存、进程启动（含 CreateProcessAsUserA 修复版）、线程与 TLS、同步对象（CRITICAL_SECTION/信号量/事件/互斥体）、命令行与环境变量、堆内存、代码页转换与 INI/Profile（profile.cpp）。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `AcquireSRWLockExclusive` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `AcquireSRWLockShared` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `ActivateKeyboardLayout` | 实现 | src/platform/*/sysapi.cpp | 经 HKL 抽象暴露各平台键盘布局：Linux 走 XKB group、macOS 走 TIS、iOS/移动端只有唯一布局（OS 不允许 App 切换）。句柄 = 布局索引 + SWINX_HKL_BASE，与魔法值 HKL_PREV(0)/HKL_NEXT(1) 隔离 |
| `CallMsgFilter` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `CancelWaitableTimer` | 实现 | src/platform/*/sysobjs.cpp |  |
| `ChangeTimerQueueTimer` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CloseClipboard` | 实现 | src/platform/*/sysapi.cpp |  |
| `CloseHandle` | 实现 | src/handle.cpp |  |
| `CompareStringA` | 实现 | src/winnsl.cpp |  |
| `CompareStringW` | 实现 | src/winnsl.cpp |  |
| `CopyDirA` | 实现 | src/platform/*/fileapi.cpp |  |
| `CopyDirW` | 实现 | src/platform/*/fileapi.cpp |  |
| `CopyFileA` | 实现 | src/platform/*/fileapi.cpp |  |
| `CopyFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `CreateDirectoryA` | 实现 | src/platform/*/fileapi.cpp |  |
| `CreateDirectoryW` | 实现 | src/platform/*/fileapi.cpp |  |
| `CreateEventA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `CreateEventW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateFileA` | 实现 | src/platform/*/fileapi.cpp |  |
| `CreateFileMappingA` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateFileMappingW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `CreateMutexA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `CreateMutexW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateProcessA` | 实现 | src/platform/*/sysapi.cpp |  |
| `CreateProcessAsUserA` | 实现 | src/platform/*/sysapi.cpp |  |
| `CreateProcessAsUserW` | 实现 | src/platform/*/sysapi.cpp |  |
| `CreateProcessW` | 实现 | src/platform/*/sysapi.cpp |  |
| `CreateSemaphoreA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `CreateSemaphoreW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateThread` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `CreateTimerQueue` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateTimerQueueTimer` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateWaitableTimerA` | 实现 | src/platform/*/sysobjs.cpp |  |
| `CreateWaitableTimerW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `DebugBreak` | 实现 | src/platform/*/sysapi.cpp |  |
| `DelDirA` | 实现 | src/platform/*/fileapi.cpp |  |
| `DelDirW` | 实现 | src/platform/*/fileapi.cpp |  |
| `DeleteFileA` | 实现 | src/platform/*/fileapi.cpp |  |
| `DeleteFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `DeleteTimerQueue` | 实现 | src/platform/*/sysobjs.cpp |  |
| `DeleteTimerQueueEx` | 实现 | src/platform/*/sysobjs.cpp |  |
| `DeleteTimerQueueTimer` | 实现 | src/platform/*/sysobjs.cpp |  |
| `DestroyCursor` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `DispatchMessage` | 实现 | src/wnd.cpp |  |
| `DosDateTimeToFileTime` | 实现 | src/platform/*/fileapi.cpp |  |
| `EmptyClipboard` | 实现 | src/platform/*/sysapi.cpp |  |
| `EnterCriticalSection` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `ExpandEnvironmentStringsA` | 实现 | src/platform/*/sysapi.cpp |  |
| `ExpandEnvironmentStringsW` | 实现 | src/platform/*/sysapi.cpp |  |
| `FileTimeToDosDateTime` | 实现 | src/platform/*/fileapi.cpp |  |
| `FileTimeToLocalFileTime` | 实现 | src/platform/*/sysapi.cpp |  |
| `FindClose` | 实现 | src/platform/*/fileapi.cpp 等 2 处 |  |
| `FindCloseChangeNotification` | 实现 | src/platform/*/sysapi.cpp |  |
| `FindFirstChangeNotificationA` | 实现 | src/platform/*/sysapi.cpp |  |
| `FindFirstChangeNotificationW` | 实现 | src/platform/*/sysapi.cpp |  |
| `FindFirstFileA` | 实现 | src/platform/*/fileapi.cpp 等 2 处 |  |
| `FindFirstFileExA` | 实现 | src/platform/*/fileapi.cpp |  |
| `FindFirstFileExW` | 实现 | src/platform/*/fileapi.cpp |  |
| `FindFirstFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `FindNextChangeNotification` | 实现 | src/platform/*/sysapi.cpp |  |
| `FindNextFileA` | 实现 | src/platform/*/fileapi.cpp 等 2 处 |  |
| `FindNextFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `FlushInstructionCache` | 实现 | src/platform/*/memory.cpp |  |
| `FreeLibrary` | 实现 | src/platform/*/dlfreertos.cpp 等 2 处 |  |
| `GetAppleBundlePath` | 实现 | src/platform/*/SConnection.mm 等 2 处 |  |
| `GetAsyncKeyState` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetClipboardData` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetClipboardOwner` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetCommandLineA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetCommandLineW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetComputerNameA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetComputerNameW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetCurrentDirectoryA` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetCurrentDirectoryW` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetCurrentProcessId` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetCurrentThreadId` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetCursor` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetDllDirectoryA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetDllDirectoryW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetDoubleClickTime` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetEnvironmentVariableA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetEnvironmentVariableW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetExitCodeProcess` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetFileAttributesA` | 实现 | src/platform/*/fileapi.cpp 等 2 处 |  |
| `GetFileAttributesExA` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetFileAttributesExW` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetFileAttributesW` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetFileSize` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetFileSizeEx` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetFileTime` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetHandleName` | 实现 | src/platform/*/sysobjs.cpp |  |
| `GetKeyState` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetKeyboardLayout` | 实现 | src/winnsl.cpp | 返回当前 HKL；尚未同步时按平台当前布局惰性初始化 |
| `GetKeyboardLayoutList` | 实现 | src/platform/*/sysapi.cpp | 枚举各平台键盘布局列表，返回 HKL 数组 |
| `GetKeyboardState` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetLastInputInfo` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetLocalTime` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetLocaleInfoA` | 实现 | src/winnsl.cpp |  |
| `GetLocaleInfoW` | 实现 | src/winnsl.cpp |  |
| `GetMessage` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetModuleFileNameA` | 实现 | src/platform/*/dlfreertos.cpp 等 2 处 |  |
| `GetModuleFileNameW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetModuleHandleExA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetModuleHandleExW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetModuleHandleW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetPrivateProfileIntA` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileIntW` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileSectionA` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileSectionNamesA` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileSectionNamesW` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileSectionW` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileStringA` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileStringW` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileStructA` | 实现 | src/platform/*/profile.cpp |  |
| `GetPrivateProfileStructW` | 实现 | src/platform/*/profile.cpp |  |
| `GetProcAddress` | 实现 | src/platform/*/dlfreertos.cpp 等 2 处 |  |
| `GetProcessHeap` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `GetProcessId` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetProfileIntA` | 实现 | src/platform/*/profile.cpp |  |
| `GetProfileIntW` | 实现 | src/platform/*/profile.cpp |  |
| `GetProfileSectionA` | 实现 | src/platform/*/profile.cpp |  |
| `GetProfileSectionW` | 实现 | src/platform/*/profile.cpp |  |
| `GetProfileStringA` | 实现 | src/platform/*/profile.cpp |  |
| `GetProfileStringW` | 实现 | src/platform/*/profile.cpp |  |
| `GetQueueStatus` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetStdHandle` | 实现 | src/platform/*/fileapi.cpp |  |
| `GetSystemDefaultLCID` | 实现 | src/winnsl.cpp |  |
| `GetSystemDefaultLangID` | 实现 | src/winnsl.cpp |  |
| `GetSystemDirectoryA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetSystemDirectoryW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetSystemInfo` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetSystemMetrics` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetSystemScale` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetSystemTime` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `GetTempFileNameA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetTempFileNameW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetTempPathA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetTempPathW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetThreadTimes` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetUserDefaultLCID` | 实现 | src/winnsl.cpp |  |
| `GetUserDefaultLangID` | 实现 | src/winnsl.cpp |  |
| `GetUserDefaultUILanguage` | 实现 | src/winnsl.cpp |  |
| `GetUserNameA` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetUserNameW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GetVersionExW` | 实现 | src/platform/*/sysapi.cpp |  |
| `GlobalAlloc` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `GlobalFlags` | 实现 | src/platform/*/memory.cpp |  |
| `GlobalFree` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `GlobalHandle` | 实现 | src/platform/*/memory.cpp |  |
| `GlobalLock` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `GlobalReAlloc` | 实现 | src/platform/*/memory.cpp |  |
| `GlobalSize` | 实现 | src/platform/*/memory.cpp |  |
| `GlobalUnlock` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapAlloc` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapCreate` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapDestroy` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapFree` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapLock` | 实现 | src/platform/*/memory.cpp |  |
| `HeapReAlloc` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapSize` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `HeapUnlock` | 实现 | src/platform/*/memory.cpp |  |
| `HeapValidate` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `IIDFromString` | 实现 | src/platform/*/sysapi.cpp |  |
| `InitOnceBeginInitialize` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `InitOnceComplete` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `InitOnceExecuteOnce` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `InitializeCriticalSection` | 实现 | src/platform/*/syncapi.cpp 等 2 处 | 内部为 std::recursive_mutex（swinx 的 CRITICAL_SECTION 支持递归语义） |
| `InitializeSRWLock` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `InterlockedCompareExchange` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `InterlockedCompareExchange64` | 实现 | src/platform/*/sysapi.cpp |  |
| `InterlockedDecrement` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `InterlockedDecrement64` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `InterlockedExchange` | 实现 | src/platform/*/sysapi.cpp |  |
| `InterlockedExchangeAdd` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `InterlockedExchangeAdd64` | 实现 | src/platform/*/sysapi.cpp |  |
| `InterlockedIncrement` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `InterlockedIncrement64` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `IsBadStringPtrA` | 实现 | src/platform/*/sysapi.cpp |  |
| `IsBadStringPtrW` | 实现 | src/platform/*/sysapi.cpp |  |
| `IsBadWritePtr` | 实现 | src/platform/*/sysapi.cpp |  |
| `IsClipboardFormatAvailable` | 实现 | src/platform/*/sysapi.cpp |  |
| `IsDBCSLeadByte` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `IsDebuggerPresent` | 实现 | src/platform/*/sysapi.cpp |  |
| `LeaveCriticalSection` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `LoadCursorA` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `LoadCursorW` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `LoadLibraryA` | 实现 | src/platform/*/dlfreertos.cpp 等 2 处 |  |
| `LoadLibraryExW` | 实现 | src/platform/*/sysapi.cpp |  |
| `LoadLibraryW` | 实现 | src/platform/*/sysapi.cpp |  |
| `LocalAlloc` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `LocalFileTimeToFileTime` | 实现 | src/platform/*/sysapi.cpp |  |
| `LocalFlags` | 实现 | src/platform/*/memory.cpp |  |
| `LocalFree` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `LocalHandle` | 实现 | src/platform/*/memory.cpp |  |
| `LocalLock` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `LocalReAlloc` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `LocalSize` | 实现 | src/platform/*/memory.cpp |  |
| `LocalUnlock` | 实现 | src/platform/*/memory.cpp 等 2 处 |  |
| `MapViewOfFile` | 实现 | src/platform/*/sysobjs.cpp |  |
| `MapVirtualKey` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `MapVirtualKeyEx` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `MoveFileA` | 实现 | src/platform/*/fileapi.cpp |  |
| `MoveFileExW` | 实现 | src/platform/*/fileapi.cpp |  |
| `MoveFileW` | 实现 | src/platform/*/fileapi.cpp |  |
| `MsgWaitForMultipleObjects` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `MulDiv` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `MultiByteToWideChar` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `OpenClipboard` | 实现 | src/platform/*/sysapi.cpp |  |
| `OpenEventA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `OpenEventW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenFileMappingA` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenFileMappingW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenMutexA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `OpenMutexW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenSemaphoreA` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `OpenSemaphoreW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenWaitableTimerA` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OpenWaitableTimerW` | 实现 | src/platform/*/sysobjs.cpp |  |
| `OutputDebugStringA` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `OutputDebugStringW` | 实现 | src/platform/*/sysapi.cpp |  |
| `PeekMessage` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `PostThreadMessageA` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `PostThreadMessageW` | 实现 | src/platform/*/sysapi.cpp |  |
| `QueryPerformanceCounter` | 实现 | src/platform/*/sysapi.cpp |  |
| `RaiseException` | 实现 | src/platform/*/sysapi.cpp |  |
| `ReadFile` | 实现 | src/platform/*/fileapi.cpp |  |
| `RegisterClipboardFormatA` | 实现 | src/platform/*/sysapi.cpp |  |
| `RegisterClipboardFormatW` | 实现 | src/platform/*/sysapi.cpp |  |
| `RegisterWaitForSingleObject` | 实现 | src/platform/*/sysobjs.cpp |  |
| `ReleaseMutex` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `ReleaseSRWLockExclusive` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `ReleaseSRWLockShared` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `ReleaseSemaphore` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `RemoveDirectoryA` | 实现 | src/platform/*/fileapi.cpp |  |
| `RemoveDirectoryW` | 实现 | src/platform/*/fileapi.cpp |  |
| `ResetEvent` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `ResumeThread` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `SetClipboardData` | 实现 | src/platform/*/sysapi.cpp |  |
| `SetCurrentDirectoryA` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetCurrentDirectoryW` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetCursor` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `SetDllDirectoryA` | 实现 | src/platform/*/sysapi.cpp |  |
| `SetDllDirectoryW` | 实现 | src/platform/*/sysapi.cpp |  |
| `SetEndOfFile` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetEnvironmentVariableA` | 实现 | src/platform/*/sysapi.cpp |  |
| `SetEnvironmentVariableW` | 实现 | src/platform/*/sysapi.cpp |  |
| `SetEvent` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `SetFileAttributesA` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetFileAttributesW` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetFilePointer` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetFilePointerEx` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetFileTime` | 实现 | src/platform/*/fileapi.cpp |  |
| `SetWaitableTimer` | 实现 | src/platform/*/sysobjs.cpp |  |
| `Sleep` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `StrToInt64ExA` | 实现 | src/platform/*/sysapi.cpp |  |
| `StrToInt64ExW` | 实现 | src/platform/*/sysapi.cpp |  |
| `StrToIntExA` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `StrToIntExW` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `SuspendThread` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `SystemParametersInfoW` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `TlsAlloc` | 实现 | src/platform/*/sysapi.cpp |  |
| `TlsFree` | 实现 | src/platform/*/sysapi.cpp |  |
| `TlsGetValue` | 实现 | src/platform/*/sysapi.cpp |  |
| `TlsSetValue` | 实现 | src/platform/*/sysapi.cpp |  |
| `TranslateMessage` | 实现 | src/wnd.cpp |  |
| `TryAcquireSRWLockExclusive` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `TryAcquireSRWLockShared` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `TryEnterCriticalSection` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `UninitializeSRWLock` | 实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `UnmapViewOfFile` | 实现 | src/platform/*/sysobjs.cpp |  |
| `UnregisterWait` | 实现 | src/platform/*/sysobjs.cpp |  |
| `UnregisterWaitEx` | 实现 | src/platform/*/sysobjs.cpp |  |
| `VirtualAlloc` | 实现 | src/platform/*/sysapi.cpp |  |
| `VirtualFree` | 实现 | src/platform/*/sysapi.cpp |  |
| `VirtualProtect` | 实现 | src/platform/*/sysapi.cpp |  |
| `VirtualQuery` | 实现 | src/platform/*/sysapi.cpp |  |
| `WaitForMultipleObjects` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `WaitForSingleObject` | 实现 | src/platform/*/sysobjs.cpp 等 2 处 |  |
| `WaitForSingleObjectEx` | 实现 | src/platform/*/sysobjs.cpp |  |
| `WaitMessage` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `WideCharToMultiByte` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `WriteFile` | 实现 | src/platform/*/fileapi.cpp |  |
| `WritePrivateProfileSectionA` | 实现 | src/platform/*/profile.cpp |  |
| `WritePrivateProfileSectionW` | 实现 | src/platform/*/profile.cpp |  |
| `WritePrivateProfileStringA` | 实现 | src/platform/*/profile.cpp |  |
| `WritePrivateProfileStringW` | 实现 | src/platform/*/profile.cpp |  |
| `WritePrivateProfileStructA` | 实现 | src/platform/*/profile.cpp |  |
| `WritePrivateProfileStructW` | 实现 | src/platform/*/profile.cpp |  |
| `WriteProfileStringA` | 实现 | src/platform/*/profile.cpp |  |
| `WriteProfileStringW` | 实现 | src/platform/*/profile.cpp |  |
| `_beginthread` | 实现 | src/process.cpp |  |
| `_beginthreadex` | 实现 | src/process.cpp |  |
| `_endthread` | 实现 | src/process.cpp |  |
| `_endthreadex` | 实现 | src/process.cpp |  |
| `_get_osfhandle` | 实现 | src/platform/*/sysapi.cpp |  |
| `_localtime64_s` | 实现 | src/platform/*/sysapi.cpp |  |
| `_mkgmtime` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `_mktime64` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `_open_osfhandle` | 实现 | src/platform/*/fileapi.cpp |  |
| `_time64` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `_wfopen` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `get_process_uid` | 实现 | src/platform/*/sysapi.cpp |  |
| `qsort_s` | 实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `CompareFileTime` | 简单实现 | src/platform/*/sysapi.cpp |  |
| `DbgBreakPoint` | 简单实现 | src/platform/*/sysapi.cpp | 实现为 assert(0) 调试断言 |
| `DeleteCriticalSection` | 简单实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `ExitThread` | 简单实现 | src/platform/*/sysapi.cpp | 记录线程退出码供 GetExitCodeThread 查询；不支持在任意位置强制终止线程 |
| `GetACP` | 简单实现 | src/platform/*/sysapi.cpp | 恒返回 CP_UTF8，swinx 全线采用 UTF-8 口径 |
| `GetLastError` | 简单实现 | src/platform/*/sysapi.cpp 等 2 处 | 以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致 |
| `GetModuleHandleA` | 简单实现 | src/platform/*/dlfreertos.cpp 等 2 处 |  |
| `GetVersionExA` | 简单实现 | src/platform/*/sysapi.cpp |  |
| `InitOnceInitialize` | 简单实现 | src/platform/*/syncapi.cpp 等 2 处 |  |
| `IsBadReadPtr` | 简单实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `IsValidCodePage` | 简单实现 | src/platform/*/sysapi.cpp | 恒返回 TRUE（配合 UTF-8 口径） |
| `LCMapStringA` | 简单实现 | src/winnsl.cpp |  |
| `LCMapStringW` | 简单实现 | src/winnsl.cpp |  |
| `QueryPerformanceFrequency` | 简单实现 | src/platform/*/sysapi.cpp |  |
| `SetLastError` | 简单实现 | src/platform/*/sysapi.cpp 等 2 处 | 以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致 |
| `SystemParametersInfoA` | 简单实现 | src/platform/*/sysapi.cpp 等 2 处 |  |
| `set_error` | 简单实现 | src/platform/*/sysapi.cpp |  |
| `FormatMessageA` | 部分实现 | src/platform/*/sysapi.cpp | 无 Windows 系统消息表：FORMAT_MESSAGE_FROM_SYSTEM 把 dwMessageId 当 errno 交给 strerror，其余来源统一返回 "unknown error <id>"；忽略 FROM_STRING / FROM_HMODULE / ARGUMENT_LIST，FORMAT_MESSAGE_ALLOCATE_BUFFER 用 LocalAlloc 兑现「调用方 LocalFree」的所有权约定 |
| `FormatMessageW` | 部分实现 | src/platform/*/sysapi.cpp | 同上，输出侧按 WCHAR 宽度转换（macOS 的 wchar_t 是 4 字节） |
| `GetThreadContext` | 部分实现 | src/platform/*/sysapi.cpp | 仅 x86_64 给出寄存器：Linux 读 SuspendThread 用 SIGPARK 挂起时快照的 ucontext（只在挂起期间有意义，未挂起返回 FALSE），macOS x86_64 走 thread_get_state；其它架构如实返回 FALSE，绝不给伪造的 Rsp（WebCore 的 MachineStackMarker 会从这个指针开始扫栈） |
| `LoadLibraryExA` | 部分实现 | src/platform/*/sysapi.cpp |  |
| `MoveFileExA` | 部分实现 | src/platform/*/fileapi.cpp |  |
| `GetCurrentProcess_Priv` | 空实现 | src/platform/*/sysapi.cpp | 内部辅助符号，恒返回 INVALID_HANDLE_VALUE（伪句柄方案不用进程句柄） |
| `TerminateThread` | 空实现 | src/platform/*/sysapi.cpp | 恒返回 0，不强制终止线程（Win32 本身也强烈不建议使用） |

### 4.4 COM / OLE（OLE32·OLEAUT32 等价）

覆盖 COM 引用计数底座（SUnkImpl）、BSTR 族（含内部 bstr 实现）、VARIANT 族（variant.cpp，2026-09-09 重写覆盖 DISPATCH/UNKNOWN/ARRAY/BYREF）、SafeArray、类对象与 CoCreateInstance（objbase.cpp）、拖放数据对象与 DragDropHelper（sdragsourcehelper.cpp、enumformatetc.cpp）以及 GUID 注册表（winguids.cpp）。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `CLSIDFromProgID` | 实现 | src/objbase.cpp |  |
| `CoCreateGuid` | 实现 | src/objbase.cpp |  |
| `CoCreateInstance` | 实现 | src/objbase.cpp |  |
| `CoGetMalloc` | 实现 | src/malloc_impl.cpp |  |
| `CoInitialize` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `CoTaskMemAlloc` | 实现 | src/malloc_impl.cpp |  |
| `CoTaskMemFree` | 实现 | src/malloc_impl.cpp |  |
| `CoTaskMemRealloc` | 实现 | src/malloc_impl.cpp |  |
| `DoDragDrop` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `OleDuplicateData` | 实现 | src/platform/*/ole2.cpp 等 3 处 |  |
| `OleFlushClipboard` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `OleGetClipboard` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `OleIsCurrentClipboard` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `OleSetClipboard` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `RegisterDragDrop` | 实现 | src/platform/*/dlghelper.cpp 等 5 处 |  |
| `ReleaseStgMedium` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `RevokeDragDrop` | 实现 | src/platform/*/ole2.cpp 等 4 处 |  |
| `SHCreateDirectoryExA` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHCreateDirectoryExW` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHCreateStdEnumFmtEtc` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHGetFolderPathA` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHGetFolderPathW` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHGetSpecialFolderPathA` | 实现 | src/platform/*/shellobj.cpp |  |
| `SHGetSpecialFolderPathW` | 实现 | src/platform/*/shellobj.cpp |  |
| `SafeArrayAccessData` | 实现 | src/safearray.cpp |  |
| `SafeArrayAllocData` | 实现 | src/safearray.cpp |  |
| `SafeArrayAllocDescriptor` | 实现 | src/safearray.cpp |  |
| `SafeArrayAllocDescriptorEx` | 实现 | src/safearray.cpp |  |
| `SafeArrayCopy` | 实现 | src/safearray.cpp |  |
| `SafeArrayCopyData` | 实现 | src/safearray.cpp |  |
| `SafeArrayCreate` | 实现 | src/safearray.cpp |  |
| `SafeArrayCreateEx` | 实现 | src/safearray.cpp |  |
| `SafeArrayCreateVector` | 实现 | src/safearray.cpp |  |
| `SafeArrayCreateVectorEx` | 实现 | src/safearray.cpp |  |
| `SafeArrayDestroy` | 实现 | src/safearray.cpp |  |
| `SafeArrayDestroyData` | 实现 | src/safearray.cpp |  |
| `SafeArrayDestroyDescriptor` | 实现 | src/safearray.cpp |  |
| `SafeArrayGetElement` | 实现 | src/safearray.cpp |  |
| `SafeArrayGetRecordInfo` | 实现 | src/safearray.cpp |  |
| `SafeArrayGetVartype` | 实现 | src/safearray.cpp |  |
| `SafeArrayLock` | 实现 | src/safearray.cpp |  |
| `SafeArrayPutElement` | 实现 | src/safearray.cpp |  |
| `SafeArrayRedim` | 实现 | src/safearray.cpp |  |
| `SafeArraySetRecordInfo` | 实现 | src/safearray.cpp |  |
| `SafeArrayUnaccessData` | 实现 | src/safearray.cpp |  |
| `SafeArrayUnlock` | 实现 | src/safearray.cpp |  |
| `SysAllocString` | 实现 | src/objbase.cpp |  |
| `SysAllocStringByteLen` | 实现 | src/objbase.cpp |  |
| `SysAllocStringLen` | 实现 | src/objbase.cpp |  |
| `SysFreeString` | 实现 | src/objbase.cpp |  |
| `SysReAllocString` | 实现 | src/objbase.cpp |  |
| `SysReAllocStringLen` | 实现 | src/objbase.cpp |  |
| `SysStringByteLen` | 实现 | src/objbase.cpp |  |
| `SysStringLen` | 实现 | src/objbase.cpp |  |
| `VariantClear` | 实现 | src/variant.cpp |  |
| `VariantCopy` | 实现 | src/variant.cpp |  |
| `SafeArrayGetDim` | 简单实现 | src/safearray.cpp |  |
| `SafeArrayGetElemsize` | 简单实现 | src/safearray.cpp |  |
| `SafeArrayGetIID` | 简单实现 | src/safearray.cpp |  |
| `SafeArrayGetLBound` | 简单实现 | src/safearray.cpp |  |
| `SafeArrayGetUBound` | 简单实现 | src/safearray.cpp |  |
| `SafeArrayPtrOfIndex` | 简单实现 | src/safearray.cpp |  |
| `SafeArraySetIID` | 简单实现 | src/safearray.cpp |  |
| `VariantInit` | 简单实现 | src/variant.cpp |  |
| `CoInitializeEx` | 空实现 | src/platform/*/ole2.cpp 等 4 处 | 恒返回 S_OK，为 COM 初始化协议占位（swinx COM 子系统无需真正初始化） |
| `CoUninitialize` | 空实现 | src/platform/*/ole2.cpp 等 4 处 | 空操作，与 CoInitializeEx 配对 |
| `OleInitialize` | 空实现 | src/objbase.cpp | 恒返回 S_OK（OLE 初始化占位） |
| `OleUninitialize` | 空实现 | src/objbase.cpp | 空操作，与 OleInitialize 配对 |

### 4.5 通用控件、Shell 与公共对话框（COMCTL32·SHELL32·SHLWAPI 等价）

覆盖 SHBrowseForFolder/SHGetFileInfo、拖放文件（shellapi.cpp）、通用控件消息入口、菜单/状态栏/工具栏实现（cmnctl32/）、公共对话框（cmmmdlg.cpp）、SHLWAPI 字符串辅助与 IMM 输入法接口入口。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `ChooseColorA` | 实现 | src/cmmmdlg.cpp |  |
| `ChooseColorW` | 实现 | src/cmmmdlg.cpp |  |
| `ChooseFontA` | 实现 | src/platform/*/dlghelper.cpp 等 4 处 |  |
| `ChooseFontW` | 实现 | src/platform/*/dlghelper.cpp 等 4 处 |  |
| `DragAcceptFiles` | 实现 | src/platform/*/shellapi.cpp |  |
| `DragFinish` | 实现 | src/platform/*/shellapi.cpp | 释放 WM_DROPFILES 交付给宿主的拖放数据；宿主未调用时由 CDropFileTarget::Drop 兜底释放，重复调用幂等 |
| `DragQueryFileA` | 实现 | src/platform/*/shellapi.cpp |  |
| `DragQueryFileW` | 实现 | src/platform/*/shellapi.cpp |  |
| `GetFullPathNameA` | 实现 | src/platform/*/shellapi.cpp 等 2 处 |  |
| `GetFullPathNameW` | 实现 | src/platform/*/shellapi.cpp |  |
| `GetOpenFileNameA` | 实现 | src/cmmmdlg.cpp |  |
| `GetOpenFileNameW` | 实现 | src/cmmmdlg.cpp |  |
| `GetSaveFileNameA` | 实现 | src/cmmmdlg.cpp |  |
| `GetSaveFileNameW` | 实现 | src/cmmmdlg.cpp |  |
| `ImmAssociateContext` | 实现 | src/platform/*/imm.cpp 等 5 处 |  |
| `ImmCreateContext` | 实现 | src/platform/*/imm.cpp 等 5 处 |  |
| `ImmDestroyContext` | 实现 | src/platform/*/imm.cpp 等 5 处 |  |
| `ImmEscapeA` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmEscapeW` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetCompositionStringA` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetCompositionStringW` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetContext` | 实现 | src/platform/*/imm.cpp 等 5 处 |  |
| `ImmGetDefaultIMEWnd` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetProperty` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetVirtualKey` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmIsIME` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmNotifyIME` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmReleaseContext` | 实现 | src/platform/*/imm.cpp 等 5 处 |  |
| `ImmSetCompositionFontA` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetCompositionFontW` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetCompositionStringA` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetCompositionStringW` | 实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `PathAppendA` | 实现 | src/shlwapi.cpp |  |
| `PathAppendW` | 实现 | src/shlwapi.cpp |  |
| `PathCanonicalizeA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathCanonicalizeW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathCombineA` | 实现 | src/shlwapi.cpp |  |
| `PathCombineW` | 实现 | src/shlwapi.cpp |  |
| `PathCommonPrefixA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathCommonPrefixW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathFileExistsA` | 实现 | src/shlwapi.cpp |  |
| `PathFileExistsW` | 实现 | src/shlwapi.cpp |  |
| `PathFindExtensionA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathFindFileNameA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathGetCharTypeA` | 实现 | src/shlwapi.cpp |  |
| `PathGetCharTypeW` | 实现 | src/shlwapi.cpp | 按 GCT_* 位组合对单个字符分类：分隔符 / 通配符 / 非法字符 / 长文件名 / 短文件名 |
| `PathIsDirectoryA` | 实现 | src/shlwapi.cpp |  |
| `PathIsDirectoryW` | 实现 | src/shlwapi.cpp |  |
| `PathIsPrefixA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathIsPrefixW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathMatchSpecA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathMatchSpecExA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathMatchSpecExW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathMatchSpecW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathQuoteSpacesA` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathQuoteSpacesW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PathRemoveExtensionA` | 实现 | src/shlwapi.cpp |  |
| `PathRemoveExtensionW` | 实现 | src/shlwapi.cpp |  |
| `PathRemoveFileSpecA` | 实现 | src/shlwapi.cpp |  |
| `PathRemoveFileSpecW` | 实现 | src/shlwapi.cpp |  |
| `PathRenameExtensionA` | 实现 | src/shlwapi.cpp |  |
| `PathRenameExtensionW` | 实现 | src/shlwapi.cpp |  |
| `PathUnquoteSpacesW` | 实现 | src/platform/*/shellapi.cpp |  |
| `PickFolderA` | 实现 | src/cmmmdlg.cpp |  |
| `PickFolderW` | 实现 | src/cmmmdlg.cpp |  |
| `SHCreateStreamOnFileA` | 实现 | src/platform/*/shellapi.cpp |  |
| `SHCreateStreamOnFileExA` | 实现 | src/platform/*/shellapi.cpp |  |
| `SHCreateStreamOnFileExW` | 实现 | src/platform/*/shellapi.cpp |  |
| `SHCreateStreamOnFileW` | 实现 | src/platform/*/shellapi.cpp |  |
| `SHFileOperationA` | 实现 | src/platform/*/shellapi.cpp |  |
| `SHFileOperationW` | 实现 | src/platform/*/shellapi.cpp |  |
| `ShellExecuteA` | 实现 | src/platform/*/dlghelper.cpp 等 2 处 |  |
| `ShellExecuteExA` | 实现 | src/platform/*/shellapi.cpp |  |
| `ShellExecuteExW` | 实现 | src/platform/*/shellapi.cpp |  |
| `ShellExecuteW` | 实现 | src/platform/*/shellapi.cpp |  |
| `Shell_NotifyIconA` | 实现 | src/platform/*/shellapi.cpp |  |
| `Shell_NotifyIconW` | 实现 | src/platform/*/shellapi.cpp |  |
| `DragQueryPoint` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `ImmGetCandidateWindow` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetCompositionWindow` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetConversionStatus` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetOpenStatus` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmGetStatusWindowPos` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetCandidateWindow` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetCompositionWindow` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetConversionStatus` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetOpenStatus` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `ImmSetStatusWindowPos` | 简单实现 | src/platform/*/imm.cpp 等 4 处 |  |
| `PathFindExtensionW` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `PathFindFileNameW` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `PathIsRelativeA` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `PathIsRelativeW` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `PathIsUNCA` | 简单实现 | src/shlwapi.cpp |  |
| `PathIsUNCW` | 简单实现 | src/shlwapi.cpp |  |
| `PathUnquoteSpacesA` | 简单实现 | src/platform/*/shellapi.cpp |  |
| `ExtractIconExA` | 部分实现 | src/platform/*/shellapi.cpp | 只能取出下标 0、且文件本身是 .ico / .cur 的图标；无资源序号枚举能力。返回值仍按 Win32 语义给出文件内的图标个数（非图像文件为 0） |
| `ExtractIconExW` | 部分实现 | src/platform/*/shellapi.cpp | 同上 |
| `SHGetFileInfoA` | 部分实现 | src/platform/*/shellapi.cpp | 填 dwAttributes / szDisplayName / szTypeName；SHGFI_ICON 仅当路径本身是 .ico / .cur 时成功，目录与 PE 无图标可给（返回 0） |
| `SHGetFileInfoW` | 部分实现 | src/platform/*/shellapi.cpp | 同上 |
| `SHGetValueA` | 空实现 | src/shlwapi.cpp | 恒返回 ERROR_FILE_NOT_FOUND，pdwType/pcbData 置 0：swinx 无注册表后端（见 ADVAPI32 说明），调用方会走各自的回退分支 |
| `SHGetValueW` | 空实现 | src/shlwapi.cpp | 同上 |

### 4.6 多媒体、资源及其它（WINMM·杂项）

覆盖多媒体计时（mmsystem.cpp，timeBegin/EndPeriod 为简化实现）、PE 资源读取（resapi.cpp，只读实现，支持 .exe/.dll 资源段解析与 ZIP 资源包）、CRT 风格字符串（strapi.cpp）、窗口类与原子表（class.cpp、clsmgr.cpp）等。

| API | 级别 | 实现位置 | 备注 |
|---|---|---|---|
| `AccessibleChildren` | 实现 | src/oleacc.cpp |  |
| `AccessibleObjectFromEvent` | 实现 | src/oleacc.cpp |  |
| `AccessibleObjectFromPoint` | 实现 | src/oleacc.cpp |  |
| `AccessibleObjectFromWindow` | 实现 | src/oleacc.cpp |  |
| `CancelIPChangeNotify` | 实现 | src/platform/*/iphlpapi.cpp | 从挂起表里摘掉指定的 OVERLAPPED，不存在时返回 FALSE |
| `CertDuplicateCertificateContext` | 实现 | src/platform/*/cryptapi.cpp |  |
| `CertFreeCertificateContext` | 实现 | src/platform/*/cryptapi.cpp |  |
| `CharLowerA` | 实现 | src/strapi.cpp |  |
| `CharLowerBuffA` | 实现 | src/strapi.cpp |  |
| `CharLowerBuffW` | 实现 | src/strapi.cpp |  |
| `CharLowerW` | 实现 | src/strapi.cpp |  |
| `CharNextA` | 实现 | src/strapi.cpp |  |
| `CharNextW` | 实现 | src/strapi.cpp |  |
| `CharToOemA` | 实现 | src/strapi.cpp |  |
| `CharToOemBuffA` | 实现 | src/strapi.cpp |  |
| `CharToOemBuffW` | 实现 | src/strapi.cpp |  |
| `CharToOemW` | 实现 | src/strapi.cpp |  |
| `CharUpperBuffA` | 实现 | src/strapi.cpp |  |
| `CharUpperBuffW` | 实现 | src/strapi.cpp |  |
| `CryptAcquireContextA` | 实现 | src/platform/*/cryptapi.cpp |  |
| `CryptAcquireContextW` | 实现 | src/platform/*/cryptapi.cpp |  |
| `CryptGenRandom` | 实现 | src/platform/*/cryptapi.cpp |  |
| `CryptReleaseContext` | 实现 | src/platform/*/cryptapi.cpp |  |
| `EnumResourceLanguagesA` | 实现 | src/resapi.cpp |  |
| `EnumResourceLanguagesW` | 实现 | src/resapi.cpp |  |
| `EnumResourceNamesA` | 实现 | src/resapi.cpp |  |
| `EnumResourceNamesW` | 实现 | src/resapi.cpp |  |
| `EnumResourceTypesA` | 实现 | src/resapi.cpp |  |
| `EnumResourceTypesW` | 实现 | src/resapi.cpp |  |
| `FindAtomA` | 实现 | src/class.cpp |  |
| `FindAtomW` | 实现 | src/class.cpp |  |
| `FindResourceA` | 实现 | src/resapi.cpp |  |
| `FindResourceExA` | 实现 | src/resapi.cpp |  |
| `FindResourceExW` | 实现 | src/resapi.cpp |  |
| `FindResourceW` | 实现 | src/resapi.cpp |  |
| `FreeAddrInfoA` | 实现 | src/platform/*/winsock2.cpp |  |
| `FreeAddrInfoW` | 实现 | src/platform/*/winsock2.cpp |  |
| `GetAdaptersAddresses` | 实现 | src/platform/*/iphlpapi.cpp |  |
| `GetAddrInfoA` | 实现 | src/platform/*/winsock2.cpp |  |
| `GetAddrInfoW` | 实现 | src/platform/*/winsock2.cpp |  |
| `GetAtomNameA` | 实现 | src/class.cpp |  |
| `GetAtomNameW` | 实现 | src/class.cpp |  |
| `GetClassInfoExA` | 实现 | src/class.cpp |  |
| `GetClassInfoExW` | 实现 | src/class.cpp |  |
| `GetFileVersionInfoA` | 实现 | src/platform/*/version.cpp |  |
| `GetFileVersionInfoSizeA` | 实现 | src/platform/*/version.cpp |  |
| `GetFileVersionInfoSizeW` | 实现 | src/platform/*/version.cpp |  |
| `GetFileVersionInfoW` | 实现 | src/platform/*/version.cpp |  |
| `GetNameInfoA` | 实现 | src/platform/*/winsock2.cpp |  |
| `GetNameInfoW` | 实现 | src/platform/*/winsock2.cpp |  |
| `GetRoleTextA` | 实现 | src/oleacc.cpp |  |
| `GetRoleTextW` | 实现 | src/oleacc.cpp |  |
| `GetStateTextA` | 实现 | src/oleacc.cpp |  |
| `GetStateTextW` | 实现 | src/oleacc.cpp |  |
| `GetStringTypeExA` | 实现 | src/strapi.cpp |  |
| `GetStringTypeExW` | 实现 | src/strapi.cpp |  |
| `InetNtopA` | 实现 | src/platform/*/winsock2.cpp |  |
| `InetNtopW` | 实现 | src/platform/*/winsock2.cpp |  |
| `InetPtonA` | 实现 | src/platform/*/winsock2.cpp |  |
| `InetPtonW` | 实现 | src/platform/*/winsock2.cpp |  |
| `LoadResource` | 实现 | src/resapi.cpp |  |
| `LoadStringA` | 实现 | src/resapi.cpp |  |
| `LoadStringW` | 实现 | src/resapi.cpp |  |
| `LockResource` | 实现 | src/resapi.cpp |  |
| `LresultFromObject` | 实现 | src/oleacc.cpp |  |
| `NotifyAddrChange` | 实现 | src/platform/*/iphlpapi.cpp |  |
| `NotifyWinEvent` | 实现 | src/oleacc.cpp |  |
| `ObjectFromLresult` | 实现 | src/oleacc.cpp |  |
| `OemToCharA` | 实现 | src/strapi.cpp |  |
| `OemToCharBuffA` | 实现 | src/strapi.cpp |  |
| `OemToCharBuffW` | 实现 | src/strapi.cpp |  |
| `OemToCharW` | 实现 | src/strapi.cpp |  |
| `PlatformAPI_Deinit` | 实现 | src/platform_api.cpp | 平台注入接口内部辅助 |
| `PlaySound` | 实现 | src/platform/*/mmsystem.cpp |  |
| `QueryInterface` | 实现 | src/malloc_impl.cpp（另有 1 个平台分支） | COM 方法而非 Win32 导出 API：这里统计的是 IMalloc（src/malloc_impl.cpp，CoGetMalloc 的可替换分配器）与 macOS 可访问性对象的实现 |
| `RegCloseKey` | 实现 | src/platform/*/registry.cpp |  |
| `RegCreateKeyExA` | 实现 | src/platform/*/registry.cpp |  |
| `RegCreateKeyExW` | 实现 | src/platform/*/registry.cpp |  |
| `RegDeleteKeyA` | 实现 | src/platform/*/registry.cpp |  |
| `RegDeleteKeyW` | 实现 | src/platform/*/registry.cpp |  |
| `RegDeleteValueA` | 实现 | src/platform/*/registry.cpp |  |
| `RegDeleteValueW` | 实现 | src/platform/*/registry.cpp |  |
| `RegEnumKeyExA` | 实现 | src/platform/*/registry.cpp |  |
| `RegEnumKeyExW` | 实现 | src/platform/*/registry.cpp |  |
| `RegFlushKey` | 实现 | src/platform/*/registry.cpp |  |
| `RegMountDatabase` | 实现 | src/platform/*/registry.cpp |  |
| `RegOpenKeyExA` | 实现 | src/platform/*/registry.cpp |  |
| `RegOpenKeyExW` | 实现 | src/platform/*/registry.cpp |  |
| `RegQueryInfoKeyA` | 实现 | src/platform/*/registry.cpp |  |
| `RegQueryInfoKeyW` | 实现 | src/platform/*/registry.cpp |  |
| `RegQueryValueExA` | 实现 | src/platform/*/registry.cpp |  |
| `RegQueryValueExW` | 实现 | src/platform/*/registry.cpp |  |
| `RegSetValueExA` | 实现 | src/platform/*/registry.cpp |  |
| `RegSetValueExW` | 实现 | src/platform/*/registry.cpp |  |
| `RegUnmountDatabase` | 实现 | src/platform/*/registry.cpp |  |
| `RegisterClassA` | 实现 | src/class.cpp |  |
| `RegisterClassExA` | 实现 | src/class.cpp |  |
| `RegisterClassExW` | 实现 | src/class.cpp |  |
| `RegisterClassW` | 实现 | src/class.cpp |  |
| `ScriptFreeCache` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptGetFontProperties` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptItemize` | 实现 | src/platform/*/usp10.cpp | 把整串当成一个 run（sScript = SCRIPT_UNDEFINED），不做脚本分段；E_OUTOFMEMORY 表示 run 数组不足 2 项 |
| `ScriptPlace` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptShape` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptStringAnalyse` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptStringFree` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptStringOut` | 实现 | src/platform/*/usp10.cpp |  |
| `ScriptXtoCP` | 实现 | src/platform/*/usp10.cpp | 按 Advance 累加定位字形，再按「落在字形后半段」给出 trailing 标志 |
| `SetSwinxLogCallback` | 实现 | src/log.cpp |  |
| `SizeofResource` | 实现 | src/resapi.cpp |  |
| `SwapBuffers` | 实现 | src/platform/*/wgl.cpp |  |
| `UnregisterClassA` | 实现 | src/class.cpp |  |
| `UnregisterClassW` | 实现 | src/class.cpp |  |
| `VerQueryValueA` | 实现 | src/platform/*/version.cpp |  |
| `VerQueryValueW` | 实现 | src/platform/*/version.cpp |  |
| `WSAAccept` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAAddressToStringA` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAAddressToStringW` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSACleanup` | 实现 | src/platform/*/winsock.cpp |  |
| `WSACloseEvent` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSACreateEvent` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAEnumNetworkEvents` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAEventSelect` | 实现 | src/platform/*/winsock2.cpp | 后台线程 20ms 轮询就绪位并 SetEvent；同时把套接字切成非阻塞（这是接口契约，不是副作用） |
| `WSAPoll` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSARecv` | 实现 | src/platform/*/winsock2.cpp | 同步语义（多缓冲按序收全）；带 OVERLAPPED 时返回 WSAEOPNOTSUPP —— 没有完成端口，不假装异步 |
| `WSARecvFrom` | 实现 | src/platform/*/winsock2.cpp | 同步语义；带 OVERLAPPED 时返回 WSAEOPNOTSUPP |
| `WSAResetEvent` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSASend` | 实现 | src/platform/*/winsock2.cpp | 同步语义（多缓冲按序发出）；带 OVERLAPPED 时返回 WSAEOPNOTSUPP |
| `WSASendTo` | 实现 | src/platform/*/winsock2.cpp | 同步语义，多缓冲拼成一条数据报；带 OVERLAPPED 时返回 WSAEOPNOTSUPP |
| `WSASetEvent` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSASocketA` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSASocketW` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAStartup` | 实现 | src/platform/*/winsock.cpp |  |
| `WSAStringToAddressA` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAStringToAddressW` | 实现 | src/platform/*/winsock2.cpp |  |
| `WSAWaitForMultipleEvents` | 实现 | src/platform/*/winsock2.cpp |  |
| `_InitResourceSystem` | 实现 | src/resapi.cpp |  |
| `__WSAFDIsSet` | 实现 | src/platform/*/winsock2.cpp | 在 fd_array 里线性查表找句柄（Win32 里这个导出名就是给 FD_ISSET 宏调用的） |
| `_chsize_s` | 实现 | src/strapi.cpp | ftruncate；失败时把 errno 作为 errno_t 返回，0 表示成功 |
| `_mbscvt` | 实现 | src/strapi.cpp |  |
| `_mbsinc` | 实现 | src/strapi.cpp |  |
| `_snprintf` | 实现 | src/strapi.cpp |  |
| `_snwprintf` | 实现 | src/strapi.cpp |  |
| `_splitpath` | 实现 | src/strapi.cpp |  |
| `_vsnprintf` | 实现 | src/strapi.cpp | 转发 vsnprintf，并按 MSVC 约定「截断时返回 -1」；判据是应写长度 > size，因此「应写长度恰好等于 size」（少一个结尾 NUL）这一边界仍返回该长度而非 -1 |
| `_wcslwr` | 实现 | src/strapi.cpp |  |
| `_wcsupr` | 实现 | src/strapi.cpp |  |
| `_wrename` | 实现 | src/strapi.cpp |  |
| `_wsplitpath` | 实现 | src/strapi.cpp |  |
| `_wtof` | 实现 | src/strapi.cpp |  |
| `closesocket` | 实现 | src/platform/*/winsock2.cpp |  |
| `ioctlsocket` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_accept` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_bind` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_connect` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_freeaddrinfo` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getaddrinfo` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_gethostbyaddr` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_gethostbyname` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_gethostname` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getnameinfo` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getpeername` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getprotobyname` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getprotobynumber` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getservbyname` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getservbyport` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getsockname` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_getsockopt` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_htonl` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_htons` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_inet_addr` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_inet_ntoa` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_inet_ntop` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_inet_pton` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_ios_entry` | 实现 | src/platform/*/ios_main.mm |  |
| `swinx_listen` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_ntohl` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_ntohs` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_recv` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_recvfrom` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_select` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_send` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_sendto` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_setsockopt` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_shutdown` | 实现 | src/platform/*/winsock2.cpp |  |
| `swinx_socket` | 实现 | src/platform/*/winsock2.cpp |  |
| `timeBeginPeriod` | 实现 | src/platform/*/mmsystem.cpp |  |
| `timeEndPeriod` | 实现 | src/platform/*/mmsystem.cpp |  |
| `timeGetTime` | 实现 | src/platform/*/mmsystem.cpp |  |
| `wcscpy_s` | 实现 | src/strapi.cpp |  |
| `wglCreateContext` | 实现 | src/platform/*/wgl.cpp |  |
| `wglDeleteContext` | 实现 | src/platform/*/wgl.cpp |  |
| `wglGetDefaultProcAddress` | 实现 | src/platform/*/wgl.cpp |  |
| `wglGetProcAddress` | 实现 | src/platform/*/wgl.cpp |  |
| `wglMakeCurrent` | 实现 | src/platform/*/wgl.cpp |  |
| `wglSwapMultipleBuffers` | 实现 | src/platform/*/wgl.cpp |  |
| `DescribePixelFormat` | 简单实现 | src/platform/*/wgl.cpp |  |
| `PlatformAPI_Init` | 简单实现 | src/platform_api.cpp |  |
| `WSAGetLastError` | 简单实现 | src/platform/*/winsock.cpp | 返回线程局部的 WSA 错误码 |
| `WSASetLastError` | 简单实现 | src/platform/*/winsock.cpp | 写入线程局部的 WSA 错误码 |
| `_ftime` | 简单实现 | src/strapi.cpp | gettimeofday 填 time/millitm，timezone 与 dstflag 恒 0（不做本地时区换算） |
| `strcpy_s` | 简单实现 | src/strapi.cpp |  |
| `timeGetDevCaps` | 简单实现 | src/platform/*/mmsystem.cpp |  |
| `wglGetCurrentDC` | 简单实现 | src/platform/*/wgl.cpp |  |
| `wglShareLists` | 简单实现 | src/platform/*/wgl.cpp |  |
| `WSAAsyncSelect` | 部分实现 | src/platform/*/winsock2.cpp | 显式拒绝：返回 WSAEOPNOTSUPP。它要把套接字事件翻译成窗口消息，需要 Winsock 与消息循环的耦合，swinx 没有那条线；吞掉事件比报错更糟 |
| `BeginUpdateResourceA` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED) 并返回 NULL；资源层为只读实现 |
| `BeginUpdateResourceW` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED) 并返回 NULL；资源层为只读实现 |
| `ChoosePixelFormat` | 空实现 | src/platform/*/wgl.cpp |  |
| `EndUpdateResourceA` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现 |
| `EndUpdateResourceW` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现 |
| `FreeResource` | 空实现 | src/resapi.cpp | 恒返回 TRUE。Win32 32 位模式下该 API 本身即为无操作，语义兼容 |
| `SetPixelFormat` | 空实现 | src/platform/*/wgl.cpp |  |
| `UpdateResourceA` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现 |
| `UpdateResourceW` | 空实现 | src/resapi.cpp | 显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现 |
| `wglCopyContext` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglCreateLayerContext` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglDescribeLayerPlane` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglGetCurrentContext` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglGetLayerPaletteEntries` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglRealizeLayerPalette` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglSetLayerPaletteEntries` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglSwapLayerBuffers` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglUseFontBitmapsA` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglUseFontBitmapsW` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglUseFontOutlinesA` | 空实现 | src/platform/*/wgl.cpp |  |
| `wglUseFontOutlinesW` | 空实现 | src/platform/*/wgl.cpp |  |

## 5. 空实现 / 语义桩 API 详解

以下 41 个 API 在 swinx 中提供符号但无实质逻辑。分两类：**协议占位**（为满足初始化/配对协议，返回成功值）与**能力缺失**（直接返回失败或空操作）。使用前请确认调用方不依赖其真实效果。

### 5.1 协议占位（返回成功，可正常配对调用）

| API | 行为 | 说明 |
|---|---|---|
| `CoInitializeEx` | 恒返回 S_OK | COM 初始化协议占位，swinx COM 子系统无需真正初始化 |
| `CoUninitialize` | 空操作 | 与 CoInitializeEx 配对 |
| `OleInitialize` | 恒返回 S_OK | OLE 初始化占位，与 OleUninitialize 配对 |
| `OleUninitialize` | 空操作 | 与 OleInitialize 配对 |

### 5.2 显式拒绝（返回失败并设置错误码）

| API | 行为 | 说明 |
|---|---|---|
| `BeginUpdateResourceA` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |
| `BeginUpdateResourceW` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |
| `EndUpdateResourceA` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |
| `EndUpdateResourceW` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |
| `UpdateResourceA` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |
| `UpdateResourceW` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |

### 5.3 其余空实现

| API | 行为与影响 |
|---|---|
| `ChoosePixelFormat` |  |
| `EnumDisplayDevicesW` | 恒返回 FALSE，枚举显示器请使用 EnumDisplayMonitors |
| `ExtEscape` | 恒返回 0（含 QUERYESCSUPPORT 探测）：本层没有打印机转义通道，如实报「不支持」比伪造成功安全 |
| `FreeResource` | 恒返回 TRUE。Win32 32 位模式下该 API 本身即为无操作，语义兼容 |
| `GetCurrentProcess_Priv` | 内部辅助符号，恒返回 INVALID_HANDLE_VALUE（伪句柄方案不用进程句柄） |
| `IsWindowUnicode` | 恒返回 FALSE（swinx 窗口内部统一 UTF-8 存储，非 Win32 的 Unicode/ANSI 双轨制） |
| `RealizePalette` | 恒返回 0（无调色板概念） |
| `SHGetValueA` | 恒返回 ERROR_FILE_NOT_FOUND，pdwType/pcbData 置 0：swinx 无注册表后端（见 ADVAPI32 说明），调用方会走各自的回退分支 |
| `SHGetValueW` | 同上 |
| `ScrollWindowEx` | 恒返回 0，不执行窗口滚动（SOUI 滚动走自己的失效/重绘路径） |
| `SelectPalette` | 恒返回 NULL（无调色板概念） |
| `SendNotifyMessageA` | 恒返回 FALSE，不投递消息 |
| `SendNotifyMessageW` | 恒返回 FALSE，不投递消息 |
| `SetMenuItemBitmaps` | 恒返回 FALSE，不支持菜单项位图 |
| `SetPixelFormat` |  |
| `SetStretchBltMode` | 两个后端都只返回 COLORONCOLOR：拉伸采样由 cairo / CoreGraphics 决定，没有可切换的拉伸算法；调用方设置的黑白/颜色模式不会被记住 |
| `SetWindowExtEx` | 恒返回 FALSE，不支持窗口坐标 extents 变换 |
| `TerminateThread` | 恒返回 0，不强制终止线程（Win32 本身也强烈不建议使用） |
| `WidenPath` | 恒返回 FALSE：路径转轮廓只在 Win32 打印路径上被用到，本层没有打印机 DC |
| `wglCopyContext` |  |
| `wglCreateLayerContext` |  |
| `wglDescribeLayerPlane` |  |
| `wglGetCurrentContext` |  |
| `wglGetLayerPaletteEntries` |  |
| `wglRealizeLayerPalette` |  |
| `wglSetLayerPaletteEntries` |  |
| `wglSwapLayerBuffers` |  |
| `wglUseFontBitmapsA` |  |
| `wglUseFontBitmapsW` |  |
| `wglUseFontOutlinesA` |  |
| `wglUseFontOutlinesW` |  |

### 5.4 语义等效但口径不同的简化实现

以下 API 有真实行为，但口径与 Win32 存在差异，移植时需留意：

| API | swinx 口径 |
|---|---|
| `GetACP` | 恒返回 CP_UTF8，swinx 全线采用 UTF-8 口径 |
| `IsValidCodePage` | 恒返回 TRUE（配合 UTF-8 口径） |
| `SetLastError` | 以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致 |
| `GetLastError` | 以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致 |
| `ExitThread` | 记录线程退出码供 GetExitCodeThread 查询；不支持在任意位置强制终止线程 |
| `GetNearestColor` | 无调色板概念，原样返回请求颜色 |
| `DbgBreakPoint` | 实现为 assert(0) 调试断言 |
| `CRITICAL_SECTION` 族 | 内部为 `std::recursive_mutex`，支持递归加锁（同 Win32） |
| `InitializeSRWLock`/SRWLock 族 | 内部为自研 `SRwLock`（见 `src/SRwLock.hpp`） |
| `LresultFromObject`/`ObjectFromLresult` | Win32 的句柄由 COM 跨进程管理，swinx 是进程内兼容层，改用内部**弱登记**句柄表实现同样的往返语义（不 AddRef，不持有对象，无撤销 API）；行为与真实 oleacc 对齐（实测）：失败以 HRESULT 作 LRESULT 返回（null=E_INVALIDARG、QI 失败=E_NOINTERFACE，并非文档描述的 0）、每次调用新句柄（表设容量上限）、句柄绑定 WM_GETOBJECT 的 wParam（服务端须原样回传，ObjectFromLresult 不匹配返回 E_FAIL） |
| `NotifyWinEvent`/`SetWinEventHook`/`UnhookWinEvent` | 语义对齐 user32：NotifyWinEvent 只把事件入队、立即返回，OUTOFCONTEXT 钩子回调由消息泵（GetMessage/PeekMessage）异步派发；支持多钩子并发、事件区间、idProcess/idThread 过滤，SKIPOWNPROCESS/SKIPOWNTHREAD 钩子注册成功但收不到本进程/注册线程事件；无任何可接收钩子时事件直接丢弃（与 user32 一致：钩子注册前发出的事件永不投递，不滞留队列）；平台桥（Linux AT-SPI / macOS NSAccessibility）经标准 SetWinEventHook 接收 |
| `AccessibleObjectFromEvent` | 与 Win32 同路径：经 AccessibleObjectFromWindow（SendMessage WM_GETOBJECT）解析 (hwnd, idObject) 处对象，pvarChild 回带事件携带的 child id |
| `OBJID_*` 常量 | 数值与 WinSDK 一致，但定义写法不同：WinSDK 的 `0xFFFFFFFCL` 在 LP64（Linux/macOS）上是 64 位正数，swinx 用 `((LONG)0xFFFFFFFC)` 显式收窄，保证 `(LONG)lParam == OBJID_CLIENT` 这类服务端判断在所有平台成立；`AccessibleObjectFromWindow` 发 WM_GETOBJECT 时按真实客户端行为对 OBJID 做符号扩展 |

## 6. 仅声明、未提供实现的 API（126 个）

以下 API 为维持 Windows 头文件（主要是 `commctrl.h` 的 DPA/DSA、ImageList、FlatSB、TaskDialog 系列）兼容而声明，swinx **没有符号定义**。SOUI 当前源码未引用它们；若第三方代码引用将产生链接错误。

```
ChangeClipboardChain              CountClipboardFormats             CreateMappedBitmap                CreateStatusWindowA             
CreateStatusWindowW               CreateTextServices                CreateToolbarEx                   CreateUpDownControl             
DPA_Clone                         DPA_Create                        DPA_CreateEx                      DPA_DeleteAllPtrs               
DPA_DeletePtr                     DPA_Destroy                       DPA_DestroyCallback               DPA_EnumCallback                
DPA_GetPtr                        DPA_GetPtrIndex                   DPA_GetSize                       DPA_Grow                        
DPA_InsertPtr                     DPA_LoadStream                    DPA_Merge                         DPA_SaveStream                  
DPA_Search                        DPA_SetPtr                        DPA_Sort                          DSA_Clone                       
DSA_Create                        DSA_DeleteAllItems                DSA_DeleteItem                    DSA_Destroy                     
DSA_DestroyCallback               DSA_EnumCallback                  DSA_GetItem                       DSA_GetItemPtr                  
DSA_GetSize                       DSA_InsertItem                    DSA_SetItem                       DSA_Sort                        
DefSubclassProc                   DrawInsert                        DrawShadowText                    DrawStatusTextA                 
DrawStatusTextW                   EnumClipboardFormats              FlatSB_EnableScrollBar            FlatSB_GetScrollInfo            
FlatSB_GetScrollPos               FlatSB_GetScrollProp              FlatSB_GetScrollPropPtr           FlatSB_GetScrollRange           
FlatSB_SetScrollInfo              FlatSB_SetScrollPos               FlatSB_SetScrollProp              FlatSB_SetScrollRange           
FlatSB_ShowScrollBar              GetClipboardFormatNameA           GetClipboardFormatNameW           GetClipboardSequenceNumber      
GetClipboardViewer                GetEffectiveClientRect            GetMUILanguage                    GetMenuCheckMarkDimensions      
GetOpenClipboardWindow            GetPriorityClipboardFormat        GetWindowSubclass                 HIMAGELIST_QueryInterface       
HyphenateProc                     ImageList_Add                     ImageList_AddMasked               ImageList_BeginDrag             
ImageList_Copy                    ImageList_Create                  ImageList_Destroy                 ImageList_DragEnter             
ImageList_DragLeave               ImageList_DragMove                ImageList_DragShowNolock          ImageList_Draw                  
ImageList_DrawEx                  ImageList_DrawIndirect            ImageList_Duplicate               ImageList_EndDrag               
ImageList_GetBkColor              ImageList_GetDragImage            ImageList_GetIcon                 ImageList_GetIconSize           
ImageList_GetImageCount           ImageList_GetImageInfo            ImageList_LoadImageA              ImageList_LoadImageW            
ImageList_Merge                   ImageList_Read                    ImageList_ReadEx                  ImageList_Remove                
ImageList_Replace                 ImageList_ReplaceIcon             ImageList_SetBkColor              ImageList_SetDragCursorImage    
ImageList_SetIconSize             ImageList_SetImageCount           ImageList_SetOverlayImage         ImageList_Write                 
ImageList_WriteEx                 InitCommonControls                InitCommonControlsEx              InitMUILanguage                 
InitializeFlatSB                  LBItemFromPt                      LoadIconMetric                    LoadIconWithScaleDown           
MakeDragList                      MenuHelp                          RemoveWindowSubclass              SetClipboardViewer              
SetWindowSubclass                 ShowHideMenuCtl                   Str_SetPtrW                       TaskDialog                      
TaskDialogIndirect                UninitializeFlatSB                _TrackMouseEvent                  _aligned_free                   
free                              memcpy                          
```

## 7. 平台注入接口（platform_api.h）

swinx 将与宿主平台强相关的能力抽象为 C 结构体函数指针表（`PLATFORM_API_VERSION 4`），由各平台后端（`src/platform/{cocoa,linux,ios,mobile}`）在启动时填充，Android / OHOS 由宿主应用经 `PlatformAPI_Init` 注入：

| 接口组 | 职责 |
|---|---|
| `PlatformClipboardAPI` | 剪切板开关/读写/格式注册 |
| `PlatformWindowAPI` | 原生窗口创建销毁、位置尺寸、可见性、焦点、捕获、屏幕/DPI/光标、RawInput、软键盘 |
| `PlatformIMEAPI` | 输入法上下文与组合字符串 |
| `PlatformAudioAPI` | `PlaySound` / `MessageBeep` 的宿主发声通道（Android / OHOS 无本地音频栈） |
| `PlatformPathAPI` | `GetTempPathA` / `SHGetSpecialFolderPathA` 的路径来源（沙箱内没有 Win32 目录概念，路径按 UTF-8 返回） |
| `PlatformShellAPI` | `ShellExecute` 的平台回调：swinx 侧没有 JNI / N-API 通道，打开 URL 或文件必须交宿主应用（Android Intent、OHOS Want） |

这些函数指针不是对外 Win32 API，而是 swinx 内核的平台后端契约；宿主不填充时对应能力降级为失败返回（不是崩溃，也不假装成功）。`version` 字段用于结构体演进时的兼容校验。

## 8. 维护说明

- 重新生成统计数据：`python doc/tools/api_scan.py`（产出 `api_scan.json`）
- CI 门禁（只读源码比对、不写盘，清单与源码不同步即失败）：`python doc/tools/api_scan.py --check`
- 核对个别函数实现：`python doc/tools/api_verify.py`（打印 stub/partial 函数体）
- 人工覆盖分级或备注：编辑本脚本内的 `OVERRIDES` 表后重新运行 `python doc/tools/gen_doc.py`
- **后端对称性需人工核对**：`src/gdi/{cairo,apple}`、`src/platform/*` 是同名符号分目录实现、链接期解析，扫描器只报告定义出现在哪些文件。新增 GDI/平台 API 时要在**两个后端都给出定义**（没有能力就给显式拒绝的桩），否则该平台是链接错误而不是运行期降级；当前已知不对称项：`AddFontMemResourceEx` / `RemoveFontMemResourceEx` 只有 CoreText 后端
- 本文统计不含 `thirdparty/`（expat、zlib 等第三方库自身的 API）