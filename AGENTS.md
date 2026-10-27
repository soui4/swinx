# swinx 模块协作指引

本文件固定 swinx **当前**的设计原则：分层与登记点、平台差异的收敛形态、句柄与退出期生存期约束、编码与错误码口径、API 面门禁与测试边界。改 swinx 前先读这里；与 soui4 根目录 `AGENTS.md` 冲突时，涉及 swinx 内部设计的以本文件为准，涉及仓库级流程（PR、CI、提交规范）的以根文件为准。

swinx 是非 Windows 平台上的 **Win32 兼容层**：向 SOUI 4 与 WKE 浏览器内核提供 USER32 / GDI32 / KERNEL32 / OLE32 / OLEAUT32 / SHELL32 等等价符号。Windows 上直接用系统 Win32 API，不编译本模块。

## 1. 分层与「新增一个 API」的登记点

分四层，其中「OS 维度」与「绘图维度」是**两个正交维度**（窗口系统/进程模型 × 绘图后端），不要混为一谈：

| 层 | 位置 | 职责 |
|---|---|---|
| 声明面 | `include/*.h`（57 个头，`windows.h` 是聚合入口） | 镜像 Win32 SDK 的类型、原型、宏。**不放实现** |
| 语义核 | `src/*.cpp`（35 个顶层文件）+ `src/cmnctl32/` | 与平台无关的 Win32 语义（句柄表、窗口/消息、区域、资源读取等） |
| OS 维度 | `src/platform/{posix,linux,cocoa,ios,mobile,freertos}` | 线程、进程、窗口系统、剪贴板、输入法、无障碍桥 |
| 绘图维度 | `src/gdi/{cairo,apple}`（+ `src/gdi/freertos/`） | DC、位图、文本、路径、图像编解码 |
| 依赖 | `thirdparty/`（cairo、pixman、fontconfig、freetype、libpng、zlib、brotli、expat、libxcb、xcb-util 家族、xcb-imdkit、xkbcommon、dbus、sysinclude） | 全部 vendor 化、内置构建的第三方栈，**不在其中做功能开发** |

按平台选目录的机制是 `file(GLOB)`：见 `linux.cmake` / `macos.cmake` / `ios.cmake` / `mobile.cmake` / `freeRTOS.cmake`，由 `CMakeLists.txt` 按 `CMAKE_SYSTEM_NAME` 或 `SOUI_PLATFORM` 分派。**新增源文件必须落在某个 GLOB 覆盖的目录里**，否则不会被编译（新目录要同时改所有相关 `.cmake`）。

一个 API 的完整链路，缺一即视为未完成：

1. `include/` 对应头里声明，保持与 Win32 SDK 同名同参同返回值。
2. 定义放在正确的层：平台无关 → `src/`；OS 相关 → `src/platform/<平台>/`；绘制相关 → `src/gdi/` 的**两个后端**（见 §8）。
3. `python3 doc/tools/api_scan.py` 刷新 `doc/tools/api_scan.json`。
4. 语义上与 Win32 有任何取舍，就在 `doc/tools/gen_doc.py` 的 `OVERRIDES` 里补人工分级与备注（扫描器只看函数体，看不出「故意拒绝」「只支持一种模式」这类决策）。
5. `python3 doc/tools/gen_doc.py` 重生成 `doc/swinx-API实现清单.md`。
6. `python3 doc/tools/api_scan.py --check` 返回 0 —— CI `.github/workflows/swinx-api-gate.yml` 跑的就是这条命令。
7. 行为可测的补 `demos/fun_test` 用例（见 §9）。

这道门禁是**两级**的：`api_scan.json` 是同步表（动了源码就得刷新），`api_baseline.json` 是能力下限（任何函数从「有实现」退化成「桩」都会失败）。两个 JSON 都受版本管理，不要手改；只有确认是**有意**收敛能力时才用 `python3 doc/tools/api_scan.py --bless`，它会同时改写两个文件、等于放宽门禁，必须在 PR 里说明原因。

## 2. 四种平台差异收敛形态

按优先级选其中一种，不要新造第五种，也不要退回散落的 `#ifdef __APPLE__`。

1. **实现下沉**：平台相关实现放 `src/platform/<平台>/`，`src/*.cpp` 只写 Win32 语义、不含平台分支。这是首选形态。
2. **同一符号、分目录实现、链接期解析**：公共助手契约集中在 `src/SwinxUtils.h`（当前是 `swinx_messageBeep` 等），各平台目录给出**同名同签名**实现，`file(GLOB)` 只把当前平台的源文件放进构建，由链接器决定哪份进二进制 —— 调用方零 `#ifdef`。暂不实现的平台也要**保留占位定义**（如 `platform/mobile/utils.cpp` 返回 `FALSE`），维持这一形态而不是退回条件编译。移动端需要宿主能力时转发 `g_platformAPI`（`include/platform_api.h`，现 v4）。跨 TU 边界的返回类型用 `int` 而不是 `BOOL` —— swinx 的 `BOOL` 在 Objective-C++ 单元里是 `bool`/`signed char`（clang 预定义 `__OBJC_BOOL_IS_BOOL`），在 `.cpp` 里是 `int`，两边不一致（见 `include/ctypes.h`）。
3. **把「平台能力的差异」抽象成句柄族**：顶层入口保持 Win32 的句柄语义，差异压到各平台 `SConnection` 的同名方法里，句柄取值约定收在一个内部头供四平台共用。范例 `src/hkl.h`：`HKL = 布局索引 + SWINX_HKL_BASE`，必须避开 Win32 魔法值 `HKL_PREV(0)` / `HKL_NEXT(1)`，且 0 另作「尚未与真实布局同步」用（历史上 cocoa 用过 4、其余用裸索引，分叉本身就是缺陷来源）。
4. **能力门控 + 延迟创建**（OpenGL 专用）：依赖不能落地时不引入链接期依赖，改运行期解析并让缺库如实失败。范例 `src/platform/posix/wgl.cpp`：公共层对平台只要求 8 个入口 —— 7 个 `swinxGL*`（Create / Destroy / MakeCurrent / ClearCurrent / Swap / GetProcAddress / SetSwapInterval，macOS 侧由 `cocoa/SGLBridge.mm` 以 `extern "C"` 给出，Linux 侧在同文件内联 GLX 实现）加每分支一个 `static PlatformSupportsContextAttribs()`；两个 `#ifdef __APPLE__` 分支只出现在文件首尾，Linux 全量走 `dlopen` + `dlsym`，缺 libGL 时 GL 入口返回失败而不影响其它功能；上下文延迟到首次 `wglMakeCurrent` 才真正创建，这样 `wglShareLists` 才能在「创建之后、绑定之前」生效（GLX/NSOpenGL 都只允许创建时指定 share group）。

`src/` 下的头是实现细节、不随 `include/` 安装出去 —— 公共头只提供 Win32 兼容面，内部约定（句柄编码、平台契约、单例）留在 `src/`。

## 3. C++ 标准与裸金属限制

- swinx 自身用 C++11（`CMakeLists.txt:4` `set(CMAKE_CXX_STANDARD 11)`）；**SOUI 4 主体不用 C++11**。别把 swinx 里的写法（range-for、`auto`、lambda、初始化列表）带回 `SOUI/`、`components/`。
- **进程级状态放函数局部 `static`，不放命名空间全局对象**。FreeRTOS 端 `startup.c` 不跑 `__libc_init_array`，带构造函数的命名空间全局对象根本不会被初始化，含锁成员的后果是首次使用即死锁。证据见 `src/cursormgr.cpp:5-11` 的注释；FreeRTOS 另配 `src/platform/freertos/stl/` 兼容层，只补缺的并发原语，不引入 uSTL。
- 编译器警告在 swinx 目标是 `PRIVATE` 作用域的 `-Wall -Wextra`（`CMakeLists.txt` 末段），不影响 `thirdparty/`。新增代码不得靠 `(void)var` 之外的手段消警。

## 4. 句柄与对象模型

- Win32 内核对象句柄统一包成 `_Handle`（`src/handle.h:8`）：`std::recursive_mutex + type + nRef + ptr + cbFree`。`AddHandleRef` / `CloseHandle` 按引用计数回收；创建入口是 GDI 侧的 `InitGdiObj`（`src/gdi/cairo/gdi.cpp:327` 与 `src/gdi/apple/gdi.cpp:559` 各一份）和 `src/region.cpp:458` 的 `alloc_gdi_handle`。
- 同步对象族走 `_SynHandle` + `NewSynHandle` / `GetSynHandle`（`src/synhandle.h:102/107`）；线程对象是 `ThreadObj`（`src/platform/posix/sysapi.cpp:4841`），其析构里 `pthread_join` —— 这是退出期 valgrind 报告形态的来源之一（见 §7）。
- GDI 对象存活计数是 `GetGuiResources(GR_GDIOBJECTS)` 的唯一数据源（`src/handle.cpp:11-30`）：`AddSwinxGdiObject` / `ReleaseSwinxGdiObject`。`HDC` 不走 `_Handle`（`src/sdc.cpp` 自己 new/delete，单独计数）。**新增对象族时必须同时接上计数**，否则 WebCore 的 GDIObjectCounter 观察不到它。
- 被 DC / 图标引用的位图要 `RefGdiObj` 而不是裸持有，否则 `DeleteObject` 之后留下悬空引用。

## 5. 退出期生存期约束（本模块最容易踩的区）

- **析构顺序契约**：`SConnMgr::~SConnMgr`（`src/platform/linux/SConnection.cpp:115-139`）必须是「连接 → `CloseHandle(m_hHeap)` → `SwinXFontFallbackShutdown()`（join 字体枚举线程并释放其 FcPattern）→ `cairo_debug_reset_static_data()` → `FcFini()`」。任何一步提前都会 use-after-free 或让字体缓存碰到已关闭的 X 连接。
- **leak-on-purpose 是规约，不是疏漏**：会被静态析构路径访问、且跨 DSO 存在弱定义的单例，一律堆上分配且**永不析构**。原因写在 `src/sharedmem.h:94-108`：inline 函数的静态局部量由「首次初始化它的 TU」注册到该 DSO 的 `__dso_handle`，而可执行文件的 `__cxa_finalize` 恒早于动态库的 `fini_array`，「exe 先销毁表、库再析构对象」的顺序**不可靠调整构造顺序修复**。同族还有 `src/hook.cpp:86`、`src/resapi.cpp:46`、`src/wndobj.cpp:145`、`src/platform/freertos/SConnection.cpp:1550`。新增这类单例必须写明「为什么不能析构」，运行期行为保持原样，代价只是退出时多一份 still-reachable。
- **守卫式析构会自锁**：单例析构里凡是「用自己的容器判定能否释放」的资源，必须先把容器整体摘走（`swap` 到局部变量）再逐项释放；否则守卫恒真、一项都释放不掉，紧随的 `clear()` 反而把所有对象孤儿化。已发生的事故：`CursorMgr::~CursorMgr`（`src/cursormgr.cpp:115`）里 `DestroyIcon` 被 `isStdCursor` 守卫短路，7 个标准光标（280 B 直接 + 27 552 B 间接）泄漏。

## 6. 语义口径（三条硬约定）

- **A/W 与编码**：`char*`（A 版）一律按 **UTF-8** 处理，不再走 ANSI/GBK 双轨；`wchar_t`（W 版）的宽度由构建注入 `-DWCHAR_SIZE=__SIZEOF_WCHAR_T__`（MinGW/Windows 为 2，POSIX/ARM 为 4），转换实现只读这个宏（`src/uniconv.h:6-14`、`include/tchar.h:6-17`）。**代码里不要假设 `sizeof(wchar_t)`**，也不要把 `WCHAR` 当 `uint16_t` 用。
- **错误码**：`SetLastError` / `GetLastError` 是线程 `errno` 代理（`src/platform/posix/sysapi.cpp:592-601`），与 Win32 错误码表**不等价**。依赖具体 `ERROR_*` 数值的代码要在注释里说明，或改判成功/失败。
- **显式拒绝优于静默降级**：能力不存在就返回失败并 `SetLastError(ERROR_CALL_NOT_IMPLEMENTED)`（资源更新族 `BeginUpdateResource*` / `UpdateResource*` / `EndUpdateResource*`），或按 Win32 语义给出固定失败值；**吞掉请求、假装成功、或返回伪造值都比失败更糟** —— `GetThreadContext` 在非 x86_64 架构直接返回 FALSE 而不给伪造的 `Rsp`，正是因为 WebCore 的 `MachineStackMarker` 会拿这个指针开始扫栈。
- **与 Win32 的偏差就地写清**：注释要说明「为什么这样取舍」以及「哪个调用方依赖它」（WKE/SOUI 的具体路径），例：`SetGraphicsMode` 必须对 NULL HDC 返回 0 并置 `ERROR_INVALID_HANDLE`（WebKit `syncContext()` 对 ImageBuffer 就是这么调的），同时又必须对有效 HDC 返回成功，否则 cairo 的 `_get_global_font_dc()` 会丢弃 DC 并留下悬空静态句柄。
- 所有空实现/语义桩都要在 `doc/swinx-API实现清单.md` 第 5 节可见（由 `OVERRIDES` 生成），不能只留在代码注释里。

## 7. 内存与泄漏判读纪律

判读细则与实测基线在 `doc/swinx-总体评估报告-2026-10-18.md` §6.3 和 `demos/fun_test/readme.md`，命令在 `build.md`。执行时要遵守：

- 跑 `fun_test` 必须加 `--child-silent-after-fork=yes`：Memcheck **总是跟随 `fork()`**（`--trace-children` 只管 `exec`），子进程继承父进程堆快照后 `_exit()`，会逐字节重复报出假 `definitely lost`。先分清「fork 快照」与「本进程真实泄漏」再动手。
- X11 请求走**共享输出队列**，「未初始化字节」的栈顶只是最后调 `flush` 的人（常是 cairo），不能照栈顶归因。
- 抑制文件 `valgrind-linux.supp` 的条目必须附成因说明（如 glibc AVX2 `wmemcmp` 的 ifunc 分派绕过拦截器导致的假阳性），不允许静默关闭。
- 主进程基线：`definitely lost: 0`、`indirectly lost: 0`，`possibly lost: 336` 是 glibc dtv（`_dl_allocate_tls`）的线程栈缓存，非泄漏。父进程一出现非零 `definitely lost`，先查 §5 的两条（leak-on-purpose 是否被误加析构、守卫式析构是否自锁）。

## 8. 后端对称性

`src/gdi/{cairo,apple}` 与 `src/platform/*` 是**同名符号、链接期解析**，`api_scan.py` 只报告定义出现在哪些文件、不会替你检查对称性。

新增 GDI / 平台 API 时，**两个后端都要给出定义**（没有对应能力就给显式拒绝的桩）。只在一个后端定义 = 另一个平台是**链接错误**，不是运行期降级。当前已知不对称项：`AddFontMemResourceEx` / `RemoveFontMemResourceEx` 仅有 `src/gdi/apple/gdi.cpp`（CoreText 路径），Linux 引用即链接失败 —— 已在清单第 8 节登记，补齐前不要假设跨平台可用。

## 9. 测试

- 用例落在 soui4 的 `demos/fun_test/`，按**文件名约定**自动分层注册：`test_*.cpp` → `soui-unit`、`test_integration_*` → 集成、`test_e2e_*` → 无窗口 E2E（三层都带门禁标签 `soui-headless`，注册 507 条＝496 单元 + 1 集成 + 10 E2E，实跑 506 条，`test_misc.cpp:191` 的 `DISABLED_is_dbcs_lead_byte_codepage_dependent` 跳过），`test_gui_*` → `soui-gui`，`test_gdi.cpp` / `test_soui.cpp` → `soui-interactive`（默认不注册、不进门禁）。
- 面向 WKE 的 swinx API 用例（`test_swinx_wke_*.cpp`）**不得使用 C++11 语法，要能在 VS2008 编译**，Linux / Windows / macOS 都得跑：用 `memset` + 逐字段赋值代替聚合初始化，`NULL` 代替 `nullptr`，不用 range-for / `auto` / lambda。
- 平台专属断言用 `#ifndef _WIN32` 包住。注意 `gtest_add_tests` 扫的是**源码文本**而非预处理结果：被排除的分支仍会注册成条目，`--gtest_filter` 匹配 0 条时返回 0、CTest 报 Passed —— 这类用例的通过证据只能来自它真正编译到的平台。注释掉的 `//TEST(...)` 同样会被注册，删除用例要整行删。
- **新增的空实现/语义桩不补用例**（断言「返回固定值」没有回归价值），但必须在清单里登记。要写的是「行为在回归时会失败」的用例，例如标准光标无条件销毁后仍存活、调用方自有的图标确实可销毁。
- GUI 行为（像素、真实输入、窗口事件派发）当前无头测试覆盖不到，需按风险补 `soui-gui` 或人工步骤，并如实标注未覆盖范围。

## 10. 文件编码与格式

实测现状（2026-10-26，`include/` + `src/` 顶层 127 个文件）：仅 14 个带 UTF-8 BOM、5 个 CRLF；`core.autocrlf` 未设置、仓库无 `.gitattributes`。

- **新文件：UTF-8 无 BOM + LF**，与最近新增的头保持一致。
- 例外：生成文件 `src/cptable.h` 带 UTF-8 BOM（`tools/gen_cptable.py` 以 CRLF 写出，但已提交的版本是 LF —— 重跑该脚本会带来整文件的行尾 diff，只在码表本身变化时才跑）。手写源文件不要以它为风格参照。
- **不做批量重排**：格式调整只限本次实际改动的文件，避免形成无关大 diff。
- 注释允许中文，但要说成因与约束（不是复述代码在做什么）；代码标识符、Win32 原型名、常量名保持原文。

## 11. 模块边界与提交

- `swinx/` 是 soui4 的 Git **子模块**。改动在子模块内提交，主仓库只更新指针，并说明原因与兼容性；不要把子模块里的修改误当主仓库普通文件提交。
- 面向 SOUI 的公共面（窗口、消息循环、资源加载、渲染路径）改动要同时检查 `SOUI/`、`components/`、`controls.extend/` 的调用点；面向 WKE 的改动以 wke 的实际引用面为契约边界，**不为未被引用的 Win32 API 主动补实现**。
- commit / PR / Review 结论用中文，代码标识、命令、路径保留原文。
- 提交前自检顺序：`api_scan.py --check` → `gen_doc.py` 重生成且 diff 只含预期条目 → 构建 `fun_test` 并跑 `-L '^soui-headless$'` → 涉及内存/GDI 生命周期时按 §7 跑一轮 valgrind。
