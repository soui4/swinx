# swinx 非客户区绘制：`WM_PRINT` 重投递导致滚动条永不绘制

> 本文记录一个**只在非 Windows 平台显形**的窗口系统缺陷：带 `WS_HSCROLL`/`WS_VSCROLL` 的窗口
> 自带的滚动条**一次都画不出来**。它与"子窗口客户区裁剪"（见 `swinx-子窗口客户区裁剪.md`）
> 是**两回事**：那个规则只影响"子窗口被父窗口客户区挡住的部分"，而本缺陷在**没有任何子窗口**
> 时同样成立，只由平台层自己的非客户区绘制通路决定。

## 1. 现象

- Linux（Ubuntu）与 macOS 上，任何带 `WS_HSCROLL` / `WS_VSCROLL` 的 **SOUI 宿主窗口**
  自带的滚动条都不显示；窗口客户区内容一切正常。
- 与"该窗口下面还有没有子窗口"**完全无关**（用户原话：*A 的滚动条在没有 B 的情况下都没有正常显示*）。
- Windows 上从未出现——因为 swinx **在 Windows 上根本不参与编译**，滚动条由系统绘制。

## 2. 正常通路（swinx 侧该怎么画）

swinx 的滚动条不是独立控件，而是**窗口的非客户区**的一部分，唯一绘制实现是
`src/wnd.cpp::handlePrint()` 的 `PRF_NONCLIENT` 分支：

```
WM_PAINT 收尾
  └─ CallWindowObjProc(..., WM_NCPAINT, (LPARAM)wndObj->invalid.hRgn, 0)   // wnd.cpp:1444 附近
       └─ app 的消息链（app 没自行处理 WM_NCPAINT）
            └─ ::DefWindowProc → swinx DefWindowProc → case WM_NCPAINT → OnNcPaint()
                 ├─ GetDCEx(hWnd, hrgn, DCX_WINDOW | DCX_INTERSECTRGN)
                 │    · 首次 SaveDC 时套"窗口减客户区"= 右侧 16px + 底部 16px 两条带
                 │    · 再与失效区求交（RGN_AND）
                 ├─ SendMessageA(hWnd, WM_PRINT, hdc, PRF_NONCLIENT)      ← 缺陷就在这一步
                 └─ ReleaseDC()
                      └─ DefWindowProc → case WM_PRINT → handlePrint(hWnd, hdc, PRF_NONCLIENT)
                           ├─ GetClipRgn(hdc, …) 取 DC 裁剪区
                           ├─ Gate：GetScrollBarRect(SB_VERT/SB_HORZ) && RectInRegion(clip, rcSb)
                           └─ 画 rail/thumb/箭头（BuiltinImage::drawScrollbarState → BitBlt 回 hdc）
```

`OnNcPaint` 用"给自己发 `WM_PRINT`"来触发标准非客户区绘制，本意是：**app 可以先自绘非客户区
（SOUI 的 `SNcPainter` 就是干这个），没处理时才落到 `DefWindowProc` 把标准非客户区（边框 + 滚动条）补上。**

## 3. 根因：`WM_PRINT` 不是 Win32 的非客户区绘制协议

`WM_PRINT` 的语义是"把窗口（的某个部分）画到给定 DC"——**应用完全可以为自己的用途注册
`WM_PRINT` 处理器**（窗口快照、离屏打印、导出图片），并且把它标记成"已处理"。这在 Win32 下
完全合法，跟"我要不要自绘非客户区"没有任何关系。

SOUI 的宿主窗口正是这样：

```cpp
// SOUI/include/core/SHostWnd.h
BEGIN_MSG_MAP_EX(SHostWnd)
    MSG_WM_SIZE(OnSize)
    MSG_WM_PRINT(OnPrint)        // ← 命中这里
    MSG_WM_PAINT(OnPaint)
    ...
    CHAIN_MSG_MAP_MEMBER(*m_pNcPainter)   // SNcPainter 在 system=1 时整体 return FALSE
END_MSG_MAP()
```

```cpp
// utilities/include/wtl.mini/msgcrack.h
#define MSG_WM_PRINT(func)      \
    if (uMsg == WM_PRINT) {     \
        SetMsgHandled(TRUE);    \
        func((HDC)wParam, (UINT)lParam); \
        lResult = 0;            \
        if(IsMsgHandled()) return TRUE;  /* ← 消息被吃掉 */
    }
```

于是链条断在 `OnNcPaint` 的 `SendMessageA(WM_PRINT, PRF_NONCLIENT)`：

1. `WM_PAINT` 收尾照常发出 `WM_NCPAINT`；`SHostWnd` 的消息映射里**没有** `WM_NCPAINT` 处理器，
   串到 `SNcPainter` 时 `if (m_bSysNcPainter) return FALSE;` 直接放行（`system="1"` 的含义就是
   **"让系统画非客户区（含滚动条）"**，`demos/uieditor/designer/DesignWnd.cpp` 就这么设），
   于是正确落到 `DefWindowProc` → `OnNcPaint`。
2. `OnNcPaint` 取到 DC、算好裁剪区，然后 `SendMessage(WM_PRINT, PRF_NONCLIENT)`。
3. `SHostWnd` 的 `MSG_WM_PRINT(OnPrint)` 命中：`OnPrint` **不看 `uFlags`**，把内存 RT 当内容
   重画一遍就 `return TRUE`；紧接着 `return TRUE` 把消息标记为已处理。
4. `DefWindowProc` 的 `case WM_PRINT: return handlePrint(...)` **永远不会被执行**
   → `PRF_NONCLIENT` 分支（唯一的滚动条绘制代码）**一次都没跑过**。

结论：**非客户区滚动条的绘制被 app 的一个合法 `WM_PRINT` 处理器整条吃掉。**

## 4. 为什么只在非 Windows 平台显形、且与子窗口无关

| | Windows | Linux / macOS |
| --- | --- | --- |
| 谁画 `WS_VSCROLL` 的非客户区滚动条 | **系统**（`DefWindowProc` 由 user32 提供） | swinx（本文件 §2 的通路） |
| swinx 是否参与 | 不参与编译 | 全部参与 |
| 本缺陷是否可见 | 不可见 | **可见**（滚动条完全不存在） |

而"有没有子窗口 B"影响的是**裁剪区域**（`swinx-子窗口客户区裁剪.md`），与本通路无关，
所以本缺陷在"完全没有子窗口"的窗口上照样成立——这正是用户用来排除裁剪嫌疑的判据。

## 5. 修法

`OnNcPaint` **直接调用** swinx 自己的非客户区绘制实现，不再绕一次消息：

```cpp
static LRESULT handlePrint(HWND hWnd, WPARAM wp, LPARAM lp);   // 前向声明（实现在本文件后面）

...
        HDC hdc = GetDCEx(hWnd, hrgn, DCX_WINDOW | DCX_INTERSECTRGN);
        /* 非客户区（滚动条、边框）由 swinx 直接画，绝不能再绕 SendMessage(WM_PRINT, PRF_NONCLIENT) */
        handlePrint(hWnd, (WPARAM)hdc, PRF_NONCLIENT);
        ReleaseDC(hWnd, hdc);
```

为什么这是正确的一侧、而不是改 SOUI：

- **语义匹配**：能走到 `OnNcPaint` 说明 app **没有**自行处理 `WM_NCPAINT`（app 处理了它，
  `DefWindowProc` 这条链根本到不了这里）。此时"标准非客户区"本来就该由平台补上——这正是
  Windows 上 `DefWindowProc(WM_NCPAINT)` 的行为。用 `WM_PRINT` 去问，问错了协议。
- **职责边界**：非客户区（含滚动条）是窗口系统的职责，不该依赖某个 app 是否恰好转发 `WM_PRINT`。
  改 swinx 一处对所有 app 都成立；改 SOUI 只救 SOUI。
- **不需要保留旧行为**：旧写法唯一"额外的"效果是——当 app 恰好有 `WM_PRINT` 处理器时，能借它
  把客户区内容再画一遍到非客户区 DC 上。那是**副作用**（DC 已被裁成非客户区两条带），不是需求。
- **幂等**：`handlePrint` 的 `PRF_NONCLIENT` 分支只做 `SRCCOPY` 覆盖，重复调用不产生差异。
- `system="0"` 的 SOUI 宿主（`SNcPainter` 自己处理 `WM_NCPAINT`、自己画边框/标题）**不受影响**：
  它的消息处理器会标记"已处理"，`OnNcPaint` 根本不会被调到。

## 6. 验证

**日志特征（临时诊断，`SWINX_CHILD_CLIP_TRACE=1`）**：

| 修复前 | 修复后 |
| --- | --- |
| 有 `[swinx-onncpaint]`，**完全没有** `[swinx-print]` | `[swinx-onncpaint]` 与 `[swinx-print]` 成对出现，`in=1` |

即"`OnNcPaint` 进了、`handlePrint` 没进"就是本缺陷的指纹。

**静态门禁**——`tools/check_ncpaint_direct.py`（无 BOM + CRLF，与其它门禁同风格）断言：

1. `handlePrint` 在 `OnNcPaint` 之前有前向声明；
2. `handlePrint` 的**定义**与前向声明签名逐字一致；
3. `OnNcPaint` 体内直接调用 `handlePrint(hWnd, (WPARAM)hdc, PRF_NONCLIENT)`；
4. `OnNcPaint` 体内不再把 `WM_PRINT` 投给自己；
5. `DefWindowProc` 仍然响应 `WM_PRINT`（显式快照/打印 API 不能被顺手删掉）。

变异验证（改的是临时副本，仓库文件不动；脚本支持传路径）：

| 变异 | 期望 | 实测 |
| --- | --- | --- |
| 原样 | PASS | 退出码 0 |
| 还原成 `SendMessageA(hWnd, WM_PRINT, hdc, PRF_NONCLIENT)` | FAIL | "OnNcPaint 没有直接调用 handlePrint…" |
| 删掉 `handlePrint` 前向声明 | FAIL | "缺少 handlePrint 的前向声明…" |
| 删掉 `DefWindowProc` 的 `case WM_PRINT` | FAIL | "DefWindowProc 必须继续响应 WM_PRINT…" |
| 把定义签名改成 `WPARAM` → `UINT` | FAIL | "handlePrint 的定义缺失，或定义签名与前向声明不一致…" |

## 7. 遗留

- `system="0"`（SOUI 自绘非客户区）**且**要求系统滚动条的宿主：`SNcPainter` 会独占 `WM_NCPAINT`，
  swinx 的滚动条通路照样到不了。当前没有这种组合（`system="1"` 才是"要系统滚动条"），如需支持
  应由 SOUI 侧在自绘后串一次 `DefWindowProc`。
- `WM_SIZE` 的 `lParam` 应传**客户区尺寸**而后当前传的是窗口尺寸，属独立契约问题，见
  `swinx-子窗口客户区裁剪.md` §8。
- 非 Windows 平台上 `handlePrint` 的 `PRF_NONCLIENT` 分支此前**从未被真正执行过**，其内部用到的
  `CreateCompatibleDC` / `BitBlt` / `SetViewportOrgEx` 在 cairo 与 apple 两个 GDI 后端的实际表现
  需要在真机上过一遍（本机 Windows 无法编译 swinx）。

## 附：关键源码索引

| 文件 | 内容 |
| --- | --- |
| `swinx/src/wnd.cpp` | `OnNcPaint`（改为直接调 `handlePrint`）、`handlePrint`（`PRF_NONCLIENT` 画滚动条/边框）、`GetScrollBarRect`、`DefWindowProc` 的 `case WM_PRINT/WM_NCPAINT` |
| `swinx/src/wndobj.h` | `showSbFlags`（SB_VERT/SB_HORZ）、`sbVert`/`sbHorz`、`invalid.hRgn` |
| `swinx/src/gdi/{cairo,apple}/builtin_image.cpp` | 内置滚动条位图（`sys_scrollbar_png`） |
| `SOUI/include/core/SHostWnd.h` | `MSG_WM_PRINT(OnPrint)`——吃掉重投递的那一条 |
| `SOUI/src/core/SNcPainter.cpp` / `.h` | `BEGIN_MSG_MAP_EX(SNcPainter)` 顶部 `if (m_bSysNcPainter) return FALSE;`（`system="1"` 的语义） |
| `utilities/include/wtl.mini/msgcrack.h` | `MSG_WM_PRINT` 宏：`SetMsgHandled(TRUE)` + `return TRUE` |
| `tools/check_ncpaint_direct.py` | 本条修复的静态门禁（5 条断言 + 4 项变异） |
