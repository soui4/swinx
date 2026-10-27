#!/usr/bin/env python3
"""从 api_scan.json + 人工覆盖表生成 swinx API 实现清单 markdown 文档"""
import json, os, re
from collections import defaultdict, Counter

HERE = os.path.dirname(os.path.abspath(__file__))
d = json.load(open(os.path.join(HERE, "api_scan.json"), encoding="utf-8"))
funcs = d["funcs"]
no_def = d["_meta"]["no_def"]

# ---------------- 人工覆盖表：name -> (级别, 备注) ----------------
# 级别: impl=完整实现 simple=简化实现 partial=部分实现 stub=空实现/语义桩
OVERRIDES = {
    # ---- 原判 stub，实为有意义的简化行为 ----
    "GetACP":            ("simple", "恒返回 CP_UTF8，swinx 全线采用 UTF-8 口径"),
    "IsValidCodePage":   ("simple", "恒返回 TRUE（配合 UTF-8 口径）"),
    "GetNearestColor":   ("simple", "无调色板概念，原样返回请求颜色"),
    # ---- 纯分支逻辑，扫描器按"无外部调用"误判成 simple ----
    "ScriptItemize":     ("impl", "把整串当成一个 run（eScript = SCRIPT_UNDEFINED），不做脚本分段；空串 S_OK + 0 项、cMaxItems<2 报 E_OUTOFMEMORY 并给出需要项数（WebKit 靠这个码扩容重试），这两点与系统 usp10 的 E_INVALIDARG 口径不同，fun_test 按平台分开断言；字符串参数是本地 WCHAR，调用方必须逐 UTF-16 码元转换以保持下标对齐"),
    "ScriptShape":       ("impl", "一个 UTF-16 码元对一个字形、clusters 恒等，不做整形；psc 被忽略（真 Uniscribe 把它当必需的 in/out 参数，传 NULL 报 E_INVALIDARG）；没有 DC 时报 E_PENDING 让调用方选好字体再试"),
    "PathRemoveFileSpecW": ("impl", "整串只有文件名、没有目录部分时返回 FALSE 且不修改缓冲；系统 shlwapi 在这里把文件名本身当 file spec 去掉（返回非 0、缓冲清空），fun_test 按平台分别断言两种口径"),
    "PathRemoveFileSpecA": ("impl", "同 PathRemoveFileSpecW"),
    "ScriptXtoCP":       ("impl", "按 Advance 累加定位字形，再按「落在字形后半段」给出 trailing 标志"),
    "ScriptStringAnalyse": ("impl", "pString 在 SDK 里是 const void*，类型拦不住调用方，靠 iCharset 区分：-1 = 本地 WCHAR（cString 数 WCHAR），其它 = ANSI（cString 数字节，swinx 按 UTF-8 解码）；空串返回 E_INVALIDARG，与 SDK 一致"),
    "CancelIPChangeNotify": ("impl", "从挂起表里摘掉指定的 OVERLAPPED，不存在时返回 FALSE"),
    "__WSAFDIsSet":      ("impl", "在 fd_array 里线性查表找句柄（Win32 里这个导出名就是给 FD_ISSET 宏调用的）"),
    # ---- 语义上的显式取舍，扫描器只看函数体分不出来 ----
    "WSAAsyncSelect":    ("partial", "显式拒绝：返回 WSAEOPNOTSUPP。它要把套接字事件翻译成窗口消息，需要 Winsock 与消息循环的耦合，swinx 没有那条线；吞掉事件比报错更糟"),
    "WSARecv":           ("impl", "同步语义（多缓冲按序收全）；带 OVERLAPPED 时返回 WSAEOPNOTSUPP —— 没有完成端口，不假装异步"),
    "WSARecvFrom":       ("impl", "同步语义；带 OVERLAPPED 时返回 WSAEOPNOTSUPP"),
    "WSASend":           ("impl", "同步语义（多缓冲按序发出）；带 OVERLAPPED 时返回 WSAEOPNOTSUPP"),
    "WSASendTo":         ("impl", "同步语义，多缓冲拼成一条数据报；带 OVERLAPPED 时返回 WSAEOPNOTSUPP"),
    "select":            ("impl", "用 poll 实现：Win32 的 fd_set 本来就是句柄数组 + 计数，正好对上 pollfd 数组；三组都为空时按 select 语义纯睡眠等待"),
    "WSAEventSelect":    ("impl", "后台线程 20ms 轮询就绪位并 SetEvent；同时把套接字切成非阻塞（这是接口契约，不是副作用）"),
    "getaddrinfo":       ("impl", "结果是一条 malloc 块（每节 ADDRINFOA + SOCKADDR_STORAGE + canonname），freeaddrinfo 因此就是一次 free，与 Win32 的所有权约定一致"),
    "WSAGetLastError":   ("simple", "返回线程局部的 WSA 错误码"),
    "WSASetLastError":   ("simple", "写入线程局部的 WSA 错误码"),
    "ExitThread":        ("simple", "记录线程退出码供 GetExitCodeThread 查询；不支持在任意位置强制终止线程"),
    "DbgBreakPoint":     ("simple", "实现为 assert(0) 调试断言"),
    "SetLastError":      ("simple", "以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致"),
    "GetLastError":      ("simple", "以线程局部 errno 模拟 last-error，错误码语义与 Win32 不完全一致"),
    # ---- COM 初始化协议占位 ----
    "CoInitializeEx":    ("stub", "恒返回 S_OK，为 COM 初始化协议占位（swinx COM 子系统无需真正初始化）"),
    "CoUninitialize":    ("stub", "空操作，与 CoInitializeEx 配对"),
    "OleInitialize":     ("stub", "恒返回 S_OK（OLE 初始化占位）"),
    "OleUninitialize":   ("stub", "空操作，与 OleInitialize 配对"),
    # ---- 显式拒绝 ----
    "BeginUpdateResourceA": ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED) 并返回 NULL；资源层为只读实现"),
    "BeginUpdateResourceW": ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED) 并返回 NULL；资源层为只读实现"),
    "EndUpdateResourceA":   ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现"),
    "EndUpdateResourceW":   ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现"),
    "UpdateResourceA":      ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现"),
    "UpdateResourceW":      ("stub", "显式拒绝：SetLastError(ERROR_CALL_NOT_IMPLEMENTED)；资源层为只读实现"),
    "FreeResource":         ("stub", "恒返回 TRUE。Win32 32 位模式下该 API 本身即为无操作，语义兼容"),
    # ---- 其余语义桩（附影响说明） ----
    "ActivateKeyboardLayout":  ("impl", "经 HKL 抽象暴露各平台键盘布局：Linux 走 XKB group、macOS 走 TIS、iOS/移动端只有唯一布局（OS 不允许 App 切换）。句柄 = 布局索引 + SWINX_HKL_BASE，与魔法值 HKL_PREV(0)/HKL_NEXT(1) 隔离"),
    "AdjustWindowRectEx":      ("impl", "由客户区矩形反推窗口矩形：只有 WS_BORDER（swinx 自绘的那圈边框）会让矩形四周各外扩 SM_CXEDGE/SM_CYEDGE；标题栏与调整边框由原生窗口管理器画在窗口矩形之外、菜单栏不自绘，故这三项贡献为 0（详见 src/wnd.cpp 的说明）"),
    "DragFinish":              ("impl", "释放 WM_DROPFILES 交付给宿主的拖放数据；宿主未调用时由 CDropFileTarget::Drop 兜底释放，重复调用幂等"),
    "EnumDisplayDevicesW":     ("stub", "恒返回 FALSE，枚举显示器请使用 EnumDisplayMonitors"),
    "GetCurrentProcess_Priv":  ("stub", "内部辅助符号，恒返回 INVALID_HANDLE_VALUE（伪句柄方案不用进程句柄）"),
    "GetKeyboardLayout":       ("impl", "返回当前 HKL；尚未同步时按平台当前布局惰性初始化"),
    "GetKeyboardLayoutList":   ("impl", "枚举各平台键盘布局列表，返回 HKL 数组"),
    "IsWindowUnicode":         ("stub", "恒返回 FALSE（swinx 窗口内部统一 UTF-8 存储，非 Win32 的 Unicode/ANSI 双轨制）"),
    "MessageBeep":             ("impl", "转发平台实现 swinx_messageBeep：Linux 走 X11 Bell 请求、macOS 走 NSBeep()、iOS 走 AudioServicesPlayAlertSound，Android / OHOS 经 g_platformAPI.audio.messageBeep 交宿主应用发声（契约见 src/SwinxUtils.h）"),
    "RealizePalette":          ("stub", "恒返回 0（无调色板概念）"),
    "ScrollWindowEx":          ("stub", "恒返回 0，不执行窗口滚动（SOUI 滚动走自己的失效/重绘路径）"),
    "SelectPalette":           ("stub", "恒返回 NULL（无调色板概念）"),
    "SendNotifyMessageA":      ("stub", "恒返回 FALSE，不投递消息"),
    "SendNotifyMessageW":      ("stub", "恒返回 FALSE，不投递消息"),
    "SetGraphicsMode":         ("simple", "记录并返回旧的 graphicsMode，GM_ADVANCED / GM_COMPATIBLE 下绘制结果一致（world transform 始终直接合成进后端 CTM）。NULL HDC 必须按 Win32 语义置 ERROR_INVALID_HANDLE 并返回 0 而不解引用——WebKit 的 syncContext() 对 ImageBuffer 就是这么调的；也必须返回成功，否则 cairo 的 _get_global_font_dc() 认定 DC 不可用而丢弃它，留下悬空静态句柄"),
    "GetGraphicsMode":         ("simple", "读取 DC 记录的 graphicsMode（纯状态存取，两种模式行为相同）；NULL HDC 返回 0"),
    "SetMenuItemBitmaps":      ("stub", "恒返回 FALSE，不支持菜单项位图"),
    "SetStretchBltMode":       ("stub", "两个后端都只返回 COLORONCOLOR：拉伸采样由 cairo / CoreGraphics 决定，没有可切换的拉伸算法；调用方设置的黑白/颜色模式不会被记住"),
    "SetWindowExtEx":          ("stub", "恒返回 FALSE，不支持窗口坐标 extents 变换"),
    "TerminateThread":         ("stub", "恒返回 0，不强制终止线程（Win32 本身也强烈不建议使用）"),
    # ---- 备注补充（级别不变） ----
    "CopyRect":        ("simple", "memcpy 结构拷贝"),
    "EqualRect":       ("simple", "逐成员比较"),
    "IsRectEmpty":     ("simple", "逐成员比较"),
    "SetRectEmpty":    ("simple", "逐成员置零"),
    "InitializeCriticalSection": ("impl", "内部为 std::recursive_mutex（swinx 的 CRITICAL_SECTION 支持递归语义）"),
    "GetGdiObjPtr":    ("impl", "GDI 句柄表内部辅助"),
    "SetGdiObjPtr":    ("impl", "GDI 句柄表内部辅助"),
    "PlatformAPI_Deinit": ("impl", "平台注入接口内部辅助"),
    # ---- 路径与 Shell 组（2025 补充） ----
    # 扫描器按「函数体里没有外部调用」把它判成简单实现，实际是逐位分类的完整实现
    "PathGetCharTypeW": ("impl", "按 GCT_* 位组合对单个字符分类：分隔符 / 通配符 / 非法字符 / 长文件名 / 短文件名"),
    # swinx 没有注册表后端，这两个是「如实报告没有」的语义桩
    "SHGetValueA": ("stub", "恒返回 ERROR_FILE_NOT_FOUND，pdwType/pcbData 置 0：swinx 无注册表后端（见 ADVAPI32 说明），调用方会走各自的回退分支"),
    "SHGetValueW": ("stub", "同上"),
    # 属性 / 显示名 / 类型名如实提供，图标受限于「无系统图像列表、不解析 PE 资源段」
    "SHGetFileInfoA": ("partial", "填 dwAttributes / szDisplayName / szTypeName；SHGFI_ICON 仅当路径本身是 .ico / .cur 时成功，目录与 PE 无图标可给（返回 0）"),
    "SHGetFileInfoW": ("partial", "同上"),
    "ExtractIconExA": ("partial", "只能取出下标 0、且文件本身是 .ico / .cur 的图标；无资源序号枚举能力。返回值仍按 Win32 语义给出文件内的图标个数（非图像文件为 0）"),
    "ExtractIconExW": ("partial", "同上"),
    # ---- WKE / WebKit 适配补充（2026-10 扫描核定） ----
    # 内存字体只在 CoreText 后端有定义：两个后端的同名符号是「分目录实现、链接期解析」，
    # 缺一个后端定义就是链接错误而不是运行期降级，必须显式登记。
    "AddFontMemResourceEx":    ("impl", "仅 CoreText 后端（src/gdi/apple/gdi.cpp）有定义：CGFont 从内存缓冲建出后经 CTFontManagerRegisterGraphicsFont 注册，*pNumFonts 返回 1，句柄是 FontMemResource*。**cairo 后端没有同名实现，Linux 上引用即链接失败**"),
    "RemoveFontMemResourceEx": ("impl", "仅 CoreText 后端有定义：CTFontManagerUnregisterGraphicsFont + CGFontRelease + delete，句柄为 AddFontMemResourceEx 的返回值"),
    "ExtEscape":               ("stub", "恒返回 0（含 QUERYESCSUPPORT 探测）：本层没有打印机转义通道，如实报「不支持」比伪造成功安全"),
    "FormatMessageA":          ("partial", "无 Windows 系统消息表：FORMAT_MESSAGE_FROM_SYSTEM 把 dwMessageId 当 errno 交给 strerror，其余来源统一返回 \"unknown error <id>\"；忽略 FROM_STRING / FROM_HMODULE / ARGUMENT_LIST，FORMAT_MESSAGE_ALLOCATE_BUFFER 用 LocalAlloc 兑现「调用方 LocalFree」的所有权约定"),
    "FormatMessageW":          ("partial", "同上，输出侧按 WCHAR 宽度转换（macOS 的 wchar_t 是 4 字节）"),
    "GdiFlush":                ("simple", "恒返回 TRUE：cairo/xcb 与 CoreGraphics 的绘制都是同步下发，没有待冲刷的 GDI 队列。WebKit 只把它当成功标志用"),
    "GetCharWidth32A":         ("simple", "转发 GetCharWidthA：Win32 里 32 位版本与旧版语义相同，只是宽度按 32 位整数给出"),
    "GetCharWidth32W":         ("simple", "转发 GetCharWidthW"),
    "GetCharacterPlacementA":  ("partial", "ANSI 入参按 UTF-8 转宽字符后复用 W 版实现"),
    "GetCharacterPlacementW":  ("partial", "逐字符 charmap + horiAdvance 累加：不做连字 / 字距 / bidi / kinsoku，dwFlags 全部忽略；按 nMaxExtent 截断并回填 nMaxFit，lpDx/lpGlyphs 缓冲不足时按 Win32 约定把 lpGlyphs 置 NULL 并回传所需数量；返回值按 Win32 打包——低 16 位是总宽度、高 16 位是拟合字数"),
    "GetFontData":             ("impl", "只读 sfnt 表（非 sfnt 字体与 dwTable=0 的「整文件」请求返回 GDI_ERROR，因此 Type1 判定恒为 false）。GDI 的 dwTable 是 MAKEFOURCC 口径（首字符在最低字节），FreeType 的 FT_MAKE_TAG 相反，实现先按 Win32 口径翻转查一次、失败再用原值查一次，两种写法都接受"),
    "GetGuiResources":         ("impl", "GR_GDIOBJECTS 返回 swinx 自己登记的 GDI 句柄计数（供 WebCore 的 GDIObjectCounter 做 Debug 泄漏观察）；GR_USEROBJECTS 未统计，恒返回 0。hProcess 只可能来自 GetCurrentProcess()，不校验"),
    "GetMapMode":              ("simple", "恒返回 MM_TEXT（唯一支持的映射模式），NULL HDC 返回 0"),
    "SetMapMode":              ("simple", "只接受 MM_TEXT 并返回它，其它模式返回 0 且不改变状态——逻辑单位 = 设备像素是本层所有绘制的前提"),
    "GetThreadContext":        ("partial", "仅 x86_64 给出寄存器：Linux 读 SuspendThread 用 SIGPARK 挂起时快照的 ucontext（只在挂起期间有意义，未挂起返回 FALSE），macOS x86_64 走 thread_get_state；其它架构如实返回 FALSE，绝不给伪造的 Rsp（WebCore 的 MachineStackMarker 会从这个指针开始扫栈）"),
    "QueryInterface":          ("impl", "COM 方法而非 Win32 导出 API：这里统计的是 IMalloc（src/malloc_impl.cpp，CoGetMalloc 的可替换分配器）与 macOS 可访问性对象的实现"),
    "WidenPath":               ("stub", "恒返回 FALSE：路径转轮廓只在 Win32 打印路径上被用到，本层没有打印机 DC"),
    "_chsize_s":               ("impl", "ftruncate；失败时把 errno 作为 errno_t 返回，0 表示成功"),
    "_ftime":                  ("simple", "gettimeofday 填 time/millitm，timezone 与 dstflag 恒 0（不做本地时区换算）"),
    "_vsnprintf":              ("impl", "转发 vsnprintf，并按 MSVC 约定「截断时返回 -1」；判据是应写长度 > size，因此「应写长度恰好等于 size」（少一个结尾 NUL）这一边界仍返回该长度而非 -1"),
}

# ---------------- 模块归类（按主导头文件） ----------------
HEADER_MODULE = [
    (["winuser.h", "wnd.h", "multimon.h", "hook.h", "menu.h", "windowsx.h", "winerror.h", "ctrl_types.h"],
     "user32", "窗口、消息与用户界面（USER32 等价）"),
    (["gdi.h", "region.h"], "gdi32", "图形设备接口（GDI32 等价）"),
    (["sysapi.h", "fileapi.h", "process.h", "winnls.h", "ctypes.h", "tchar.h", "basetyps.h"],
     "kernel32", "系统、文件、进程与线程（KERNEL32 等价）"),
    (["objbase.h", "oleauto.h", "ole2.h", "objidl.h", "oaidl.h", "unknwn.h", "comcat.h",
      "oleidl.h", "shlobj.h", "guiddef.h", "initguid.h"],
     "ole", "COM / OLE（OLE32·OLEAUT32 等价）"),
    (["commctrl.h", "richedit.h", "textserv.h", "commdlg.h", "shellapi.h", "shlwapi.h", "imm.h"],
     "shell", "通用控件、Shell 与公共对话框（COMCTL32·SHELL32·SHLWAPI 等价）"),
    (["mmsystem.h", "resapi.h", "strapi.h", "class.h", "ios_entry.h", "logdef.h",
      "platform_api.h", "misc", "其它"],
     "misc", "多媒体、资源及其它（WINMM·杂项）"),
]

def module_of(headers):
    best = "misc"
    best_rank = len(HEADER_MODULE)
    for rank, (hdrs, mod, _) in enumerate(HEADER_MODULE):
        for h in headers:
            if h in hdrs and rank < best_rank:
                best, best_rank = mod, rank
    return best

# no_def 中排除的宏 / 头内 inline
MACROS = {"LOWORD", "HIWORD", "DEFINE_GUID", "IsEqualGUID", "SNDMSG", "wcstok"}

# ---------------- 分级 ----------------
def level_of(name, v):
    if name in OVERRIDES:
        return OVERRIDES[name]
    agg = v["agg"]
    if agg == "impl":
        marks = [m for _, _, m in v["site_classes"] if m]
        if any("简单getter" in m for m in marks):
            return "simple", ""
        return "impl", ""
    if agg == "partial":
        return "partial", ""
    return "stub", ""

data = {}  # name -> dict(level, note, headers, files, module)
for name, v in funcs.items():
    lv, note = level_of(name, v)
    data[name] = {
        "level": lv, "note": note,
        "headers": v["headers"],
        "files": v["files"],
        "module": module_of(v["headers"]),
    }
no_def_list = sorted(set(n for n in no_def if n not in MACROS and n not in data))

LV_ORDER = {"impl": 0, "simple": 1, "partial": 2, "stub": 3}
def file_disp(files):
    srcs = [f for f in files if not f.startswith("platform/")]
    plats = [f for f in files if f.startswith("platform/")]
    if srcs:
        s = "src/" + srcs[0]
        if len(srcs) > 1:
            s += " 等 %d 处" % len(srcs)
        if plats:
            s += "（另有 %d 个平台分支）" % len(plats)
        return s
    plats2 = sorted({re.sub(r"platform/[^/]+/", "platform/*/", p) for p in plats})
    return "src/" + plats2[0] + (" 等 %d 处" % len(plats) if len(plats) > 1 else "")

LV_NAME = {"impl": "实现", "simple": "简单实现", "partial": "部分实现", "stub": "空实现"}
lines = []
A = lines.append

A("# swinx API 实现清单")
A("")
A("> **代码基线**：swinx @ 2026-10-26（soui4 无头门禁 504 用例全部通过；主进程 valgrind `definitely lost: 0`）  ")
A("> **统计口径**：以 `swinx/include/*.h` 对外声明的 Win32 兼容函数为准，在 `swinx/src/**` 中定位定义并按函数体核定实现程度。宏与头内 inline（`LOWORD`、`HIWORD`、`SNDMSG`、`IsEqualGUID`、`DEFINE_GUID` 等）不计入。  ")
A("> **本文由扫描工具生成后人工核定**：工具见 `doc/tools/api_scan.py`（扫描）、`doc/tools/api_verify.py`（明细核对）。")
A("")
A("## 1. 实现程度分级")
A("")
A("| 级别 | 含义 |")
A("|---|---|")
A("| 实现 | 有完整逻辑，语义对应 Win32 行为 |")
A("| 简单实现 | 提供了有意义的行为，但比 Win32 简化（如结构拷贝、状态存取、errno 模拟错误码） |")
A("| 部分实现 | 主流程可用，个别分支/参数组合未实现并记录日志 |")
A("| 空实现 / 语义桩 | 返回固定值或空操作；或为初始化协议占位（详见第 5 节） |")
A("| 未提供 | 仅在头文件中保留声明以维持 Windows 头兼容，swinx 无符号定义，SOUI 当前未引用 |")
A("")
A("## 2. 总览")
A("")
cnt = Counter(v["level"] for v in data.values())
A("| 分级 | 数量 |")
A("|---|---|")
A("| 实现 | %d |" % cnt["impl"])
A("| 简单实现 | %d |" % cnt["simple"])
A("| 部分实现 | %d |" % cnt["partial"])
A("| 空实现 / 语义桩 | %d |" % cnt["stub"])
A("| 未提供（仅声明） | %d |" % len(no_def_list))
A("| **合计（有定义）** | **%d** |" % len(data))
A("")
A("## 3. 模块分布")
A("")
A("| 模块 | 实现 | 简单实现 | 部分实现 | 空实现 |")
A("|---|---|---|---|---|")
for hdrs, mod, title in HEADER_MODULE:
    sub = [v for v in data.values() if v["module"] == mod]
    c = Counter(v["level"] for v in sub)
    A("| %s | %d | %d | %d | %d |" % (title, c["impl"], c["simple"], c["partial"], c["stub"]))
A("")

# 模块引言
MODULE_INTRO = {
    "user32": "覆盖窗口类注册、窗口创建/销毁、窗口属性与关系查询、消息队列与消息循环、消息投递/发送、定时器、菜单、滚动条、剪切板钩子入口、鼠标键盘状态、显示器枚举（EnumDisplayMonitors/GetMonitorInfo 等）与系统参数。窗口子系统核心在 `wnd.cpp`/`wndobj.cpp`，平台差异收敛在 `src/platform/*`。",
    "gdi32": "覆盖 DC 创建与属性、画笔/画刷/字体/位图等 GDI 对象、区域（region.cpp 独立实现）、文本绘制（cairo/Apple CoreText 两条后端）、位块与拉伸传输、GetDIBits/SetDIBits 等。后端位于 `src/gdi/cairo` 与 `src/gdi/apple`。",
    "kernel32": "覆盖文件读写与查找、内存映射与共享内存、进程启动（含 CreateProcessAsUserA 修复版）、线程与 TLS、同步对象（CRITICAL_SECTION/信号量/事件/互斥体）、命令行与环境变量、堆内存、代码页转换与 INI/Profile（profile.cpp）。",
    "ole": "覆盖 COM 引用计数底座（SUnkImpl）、BSTR 族（含内部 bstr 实现）、VARIANT 族（variant.cpp，2026-09-09 重写覆盖 DISPATCH/UNKNOWN/ARRAY/BYREF）、SafeArray、类对象与 CoCreateInstance（objbase.cpp）、拖放数据对象与 DragDropHelper（sdragsourcehelper.cpp、enumformatetc.cpp）以及 GUID 注册表（winguids.cpp）。",
    "shell": "覆盖 SHBrowseForFolder/SHGetFileInfo、拖放文件（shellapi.cpp）、通用控件消息入口、菜单/状态栏/工具栏实现（cmnctl32/）、公共对话框（cmmmdlg.cpp）、SHLWAPI 字符串辅助与 IMM 输入法接口入口。",
    "misc": "覆盖多媒体计时（mmsystem.cpp，timeBegin/EndPeriod 为简化实现）、PE 资源读取（resapi.cpp，只读实现，支持 .exe/.dll 资源段解析与 ZIP 资源包）、CRT 风格字符串（strapi.cpp）、窗口类与原子表（class.cpp、clsmgr.cpp）等。",
}

A("## 4. 模块 API 明细")
A("")
A("> 「简单实现」指语义成立但比 Win32 简化（结构拷贝、状态存取、固定口径返回等）；表中未加备注的行其行为与 Win32 文档语义一致。")
A("")
for hdrs, mod, title in HEADER_MODULE:
    sub = {n: v for n, v in data.items() if v["module"] == mod}
    if not sub:
        continue
    A("### 4.%d %s" % (list(m for _, m, _ in HEADER_MODULE).index(mod) + 1, title))
    A("")
    A(MODULE_INTRO[mod])
    A("")
    A("| API | 级别 | 实现位置 | 备注 |")
    A("|---|---|---|---|")
    for name in sorted(sub, key=lambda n: (LV_ORDER[sub[n]["level"]], n)):
        v = sub[name]
        note = v["note"]
        A("| `%s` | %s | %s | %s |" % (name, LV_NAME[v["level"]], file_disp(v["files"]), note))
    A("")

# ---- 空实现详解 ----
A("## 5. 空实现 / 语义桩 API 详解")
A("")
A("以下 %d 个 API 在 swinx 中提供符号但无实质逻辑。分两类：**协议占位**（为满足初始化/配对协议，返回成功值）与**能力缺失**（直接返回失败或空操作）。使用前请确认调用方不依赖其真实效果。" % cnt["stub"])
A("")
A("### 5.1 协议占位（返回成功，可正常配对调用）")
A("")
A("| API | 行为 | 说明 |")
A("|---|---|---|")
FIX51 = {
    "CoInitializeEx": ("恒返回 S_OK", "COM 初始化协议占位，swinx COM 子系统无需真正初始化"),
    "CoUninitialize": ("空操作", "与 CoInitializeEx 配对"),
    "OleInitialize": ("恒返回 S_OK", "OLE 初始化占位，与 OleUninitialize 配对"),
    "OleUninitialize": ("空操作", "与 OleInitialize 配对"),
}
for n in ["CoInitializeEx", "CoUninitialize", "OleInitialize", "OleUninitialize"]:
    beh, desc = FIX51[n]
    A("| `%s` | %s | %s |" % (n, beh, desc))
A("")
A("### 5.2 显式拒绝（返回失败并设置错误码）")
A("")
A("| API | 行为 | 说明 |")
A("|---|---|---|")
for n in ["BeginUpdateResourceA", "BeginUpdateResourceW", "EndUpdateResourceA", "EndUpdateResourceW", "UpdateResourceA", "UpdateResourceW"]:
    v = data[n]
    A("| `%s` | 返回失败，`GetLastError()`=ERROR_CALL_NOT_IMPLEMENTED | 资源层为只读实现，不支持资源更新 |" % n)
A("")
A("### 5.3 其余空实现")
A("")
A("| API | 行为与影响 |")
A("|---|---|")
for n in sorted(data):
    v = data[n]
    if v["level"] != "stub" or n in ("CoInitializeEx", "CoUninitialize", "OleInitialize", "OleUninitialize",
                                     "BeginUpdateResourceA", "BeginUpdateResourceW", "EndUpdateResourceA",
                                     "EndUpdateResourceW", "UpdateResourceA", "UpdateResourceW"):
        continue
    A("| `%s` | %s |" % (n, v["note"]))
A("")
A("### 5.4 语义等效但口径不同的简化实现")
A("")
A("以下 API 有真实行为，但口径与 Win32 存在差异，移植时需留意：")
A("")
A("| API | swinx 口径 |")
A("|---|---|")
for n in ["GetACP", "IsValidCodePage", "SetLastError", "GetLastError", "ExitThread", "GetNearestColor", "DbgBreakPoint"]:
    A("| `%s` | %s |" % (n, data[n]["note"]))
A("| `CRITICAL_SECTION` 族 | 内部为 `std::recursive_mutex`，支持递归加锁（同 Win32） |")
A("| `InitializeSRWLock`/SRWLock 族 | 内部为自研 `SRwLock`（见 `src/SRwLock.hpp`） |")
A("| `LresultFromObject`/`ObjectFromLresult` | Win32 的句柄由 COM 跨进程管理，swinx 是进程内兼容层，改用内部**弱登记**句柄表实现同样的往返语义（不 AddRef，不持有对象，无撤销 API）；行为与真实 oleacc 对齐（实测）：失败以 HRESULT 作 LRESULT 返回（null=E_INVALIDARG、QI 失败=E_NOINTERFACE，并非文档描述的 0）、每次调用新句柄（表设容量上限）、句柄绑定 WM_GETOBJECT 的 wParam（服务端须原样回传，ObjectFromLresult 不匹配返回 E_FAIL） |")
A("| `NotifyWinEvent`/`SetWinEventHook`/`UnhookWinEvent` | 语义对齐 user32：NotifyWinEvent 只把事件入队、立即返回，OUTOFCONTEXT 钩子回调由消息泵（GetMessage/PeekMessage）异步派发；支持多钩子并发、事件区间、idProcess/idThread 过滤，SKIPOWNPROCESS/SKIPOWNTHREAD 钩子注册成功但收不到本进程/注册线程事件；无任何可接收钩子时事件直接丢弃（与 user32 一致：钩子注册前发出的事件永不投递，不滞留队列）；平台桥（Linux AT-SPI / macOS NSAccessibility）经标准 SetWinEventHook 接收 |")
A("| `AccessibleObjectFromEvent` | 与 Win32 同路径：经 AccessibleObjectFromWindow（SendMessage WM_GETOBJECT）解析 (hwnd, idObject) 处对象，pvarChild 回带事件携带的 child id |")
A("| `OBJID_*` 常量 | 数值与 WinSDK 一致，但定义写法不同：WinSDK 的 `0xFFFFFFFCL` 在 LP64（Linux/macOS）上是 64 位正数，swinx 用 `((LONG)0xFFFFFFFC)` 显式收窄，保证 `(LONG)lParam == OBJID_CLIENT` 这类服务端判断在所有平台成立；`AccessibleObjectFromWindow` 发 WM_GETOBJECT 时按真实客户端行为对 OBJID 做符号扩展 |")
A("")

# ---- 未提供 ----
A("## 6. 仅声明、未提供实现的 API（%d 个）" % len(no_def_list))
A("")
A("以下 API 为维持 Windows 头文件（主要是 `commctrl.h` 的 DPA/DSA、ImageList、FlatSB、TaskDialog 系列）兼容而声明，swinx **没有符号定义**。SOUI 当前源码未引用它们；若第三方代码引用将产生链接错误。")
A("")
A("```")
for i in range(0, len(no_def_list), 4):
    A("  ".join("%-32s" % n for n in no_def_list[i:i + 4]))
A("```")
A("")

# ---- 平台注入接口 ----
A("## 7. 平台注入接口（platform_api.h）")
A("")
A("swinx 将与宿主平台强相关的能力抽象为 C 结构体函数指针表（`PLATFORM_API_VERSION 4`），由各平台后端（`src/platform/{cocoa,linux,ios,mobile}`）在启动时填充，Android / OHOS 由宿主应用经 `PlatformAPI_Init` 注入：")
A("")
A("| 接口组 | 职责 |")
A("|---|---|")
A("| `PlatformClipboardAPI` | 剪切板开关/读写/格式注册 |")
A("| `PlatformWindowAPI` | 原生窗口创建销毁、位置尺寸、可见性、焦点、捕获、屏幕/DPI/光标、RawInput、软键盘 |")
A("| `PlatformIMEAPI` | 输入法上下文与组合字符串 |")
A("| `PlatformAudioAPI` | `PlaySound` / `MessageBeep` 的宿主发声通道（Android / OHOS 无本地音频栈） |")
A("| `PlatformPathAPI` | `GetTempPathA` / `SHGetSpecialFolderPathA` 的路径来源（沙箱内没有 Win32 目录概念，路径按 UTF-8 返回） |")
A("| `PlatformShellAPI` | `ShellExecute` 的平台回调：swinx 侧没有 JNI / N-API 通道，打开 URL 或文件必须交宿主应用（Android Intent、OHOS Want） |")
A("")
A("这些函数指针不是对外 Win32 API，而是 swinx 内核的平台后端契约；宿主不填充时对应能力降级为失败返回（不是崩溃，也不假装成功）。`version` 字段用于结构体演进时的兼容校验。")
A("")
A("## 8. 维护说明")
A("")
A("- 重新生成统计数据：`python doc/tools/api_scan.py`（产出 `api_scan.json`）")
A("- CI 门禁（只读源码比对、不写盘，清单与源码不同步即失败）：`python doc/tools/api_scan.py --check`")
A("- 核对个别函数实现：`python doc/tools/api_verify.py`（打印 stub/partial 函数体）")
A("- 人工覆盖分级或备注：编辑本脚本内的 `OVERRIDES` 表后重新运行 `python doc/tools/gen_doc.py`")
A("- **后端对称性需人工核对**：`src/gdi/{cairo,apple}`、`src/platform/*` 是同名符号分目录实现、链接期解析，扫描器只报告定义出现在哪些文件。新增 GDI/平台 API 时要在**两个后端都给出定义**（没有能力就给显式拒绝的桩），否则该平台是链接错误而不是运行期降级；当前已知不对称项：`AddFontMemResourceEx` / `RemoveFontMemResourceEx` 只有 CoreText 后端")
A("- 本文统计不含 `thirdparty/`（expat、zlib 等第三方库自身的 API）")

out_path = os.path.join(os.path.dirname(HERE), "swinx-API实现清单.md")
open(out_path, "w", encoding="utf-8").write("\n".join(lines))
print("written:", out_path, len(lines), "lines")
