# swinx OpenGL（WGL）跨平台兼容层实现

> 本文说明 swinx 如何把 Win32 的 **WGL**（窗口 + 像素格式 + GL 上下文 + 双缓冲交换）在
> macOS 与 Linux 上补齐，使得"按 Windows 写"的 OpenGL 代码**不需要任何平台分支**即可编译运行。
> 内容包括：分层结构、公共层的关键时序设计、macOS（`NSOpenGLContext`）与 Linux（GLX）
> 两套落地方案、能力矩阵与降级约定、验证方式与已知限制。
>
> 配套示例与契约测试见 `demos/opengl_demo`（`SRealWnd` + `SNativeWnd` + OpenGL，三端同源）。

## 1. 目标与边界

### 1.1 目标

1. 让 `#include <windows.h>` + `#include <GL/gl.h>` + `wglCreateContext/MakeCurrent/SwapBuffers`
   这套写法在 macOS/Linux 上**原样编译、原样运行**，效果与 Windows 一致。
2. 覆盖面按"真实桌面程序会用到的那部分 WGL"来定：核心上下文管理、共享列表、像素格式、
   双缓冲、垂直同步，以及常用的 `WGL_ARB_create_context(_profile)` 扩展。
3. 语义诚实：做不到的能力**明确失败**，不伪造成功、不静默降级。调用方靠返回值决定回退策略，
   所以"能建/不能建"必须准确反映平台真实能力。

### 1.2 边界（明确不做）

| 不做的东西 | 原因 |
| --- | --- |
| `wglChoosePixelFormatARB`（`WGL_ARB_pixel_format`，MSAA 通常依赖它） | 两个平台的像素格式是创建上下文时一次性固定的，没有 Win32 那种"先枚举再挑"的模型；入口探测返回 NULL，调用方按"不支持"降级 |
| 图层平面（`wglCreateLayerContext`/`wglDescribeLayerPlane`/`wgl*LayerPaletteEntries`/`wglSwapLayerBuffers`） | 这是 Windows 的 overlay/underlay 平面机制，macOS/Linux 无对应概念 |
| GDI 字形（`wglUseFontBitmaps*`/`wglUseFontOutlines*`） | 依赖 GDI 取字形轮廓；跨平台文本应走纹理图集 |
| `wglCopyContext` | 两个平台都不支持上下文状态整体拷贝 |
| 多 drawable 的读/写分离（`wglMakeContextCurrentARB` 传不同 DC） | 一个上下文只能绑一张表面，读写不同窗口时明确失败，避免读错像素 |

## 2. 总体结构

```
调用方（SOUI components / 应用代码，全部按 Win32 WGL 写，无平台分支）
  <windows.h> + <GL/gl.h> + <GL/wglext.h>
        │
        ├─ 头文件层   swinx/include
        │    GL/gl.h       转发平台 GL 头 + 带出 wgl.h（WGL 声明的唯一源）
        │    wgl.h        HGLRC / PIXELFORMATDESCRIPTOR / WGLSWAP、核心与降级入口
        │    GL/wglext.h   扩展常量 + PFNWGL*PROC 函数指针类型
        │
        ├─ 公共层     swinx/src/wgl.cpp（两平台同一份）
        │    SwinxGLCtx、生命周期与 share 时序、核心入口、扩展分派、像素格式回显
        │
        └─ 平台层     PlatformXxx() 共 8 个
             ├─ __APPLE__  cocoa/SGLBridge.mm  → NSOpenGLContext → NSView（SNsWindow）
             └─ Linux      dlopen libGL/libX11  → GLX             → X11 窗口（xcb 建窗）
```

`wgl.cpp` 只在非 Windows 平台编入（主仓 `add_subdirectory(swinx)` 位于非 Windows 分支），
所以 Windows 上系统自带的 `opengl32` / `wingdi.h` 与这套兼容层互不干扰。

实现层只有 **8 个平台函数**，其余全部是公共逻辑：

| 平台函数 | 职责 |
| --- | --- |
| `PlatformCreateCtx` | 按 requested 版本/profile/share 建平台上下文 |
| `PlatformDestroyCtx` | 销毁平台上下文 |
| `PlatformMakeCurrent` | 把上下文绑到窗口 |
| `PlatformClearCurrent` | 解除当前绑定 |
| `PlatformSwap` | 双缓冲交换 |
| `PlatformGetProcAddress` | 解析 GL 入口 |
| `PlatformSetSwapInterval` + `PlatformSupportsContextAttribs` | 垂直同步 / 3.x 能力探测 |

### 2.1 三个关键设计决策

**(1) 上下文延迟创建**——`wglCreateContext` 只分配对象，平台资源留到首次 `wglMakeCurrent`
才真正创建。原因是 GLX 的 `glXCreateNewContext` 与 `NSOpenGLContext initWithFormat:shareContext:`
**都只能在创建时指定 share group，创建后无法追加**。若不延迟，最常见的调用顺序

```cpp
HGLRC a = wglCreateContext(dc);
HGLRC b = wglCreateContext(dc);
wglShareLists(a, b);          // 此时两个都还没建平台资源 ⇒ 能生效
wglMakeCurrent(dc, b);
```

就永远失败。延迟后 `wglShareLists` 只记录关系，等到 `EnsureCreated` 时把共享源传进去：

```
wglCreateContext(hdc)   → 分配 SwinxGLCtx{pPlatform=NULL, hShare=NULL}
wglCreateContext(hdc2)  → 同上
wglShareLists(h1, h2)   → h2->hShare = h1（仅记账）
wglMakeCurrent(dc, h2)  → EnsureCreated(h2)
                            ├─ 递归 EnsureCreated(h1)  → 先建共享源
                            └─ PlatformCreateCtx(..., share=h1->pPlatform)
                          → 平台层在"创建时"就把 h2 放进 h1 的 share group
```

`wglCreateContextAttribsARB` 是例外：它**立即创建**并把失败原样返回，因为调用方正是靠这个
返回值判断"要不要降到 2.1"。

**(2) WGL 扩展不导出成普通函数**——全部走 `wglGetProcAddress` 的静态分派表。
Windows 上扩展本来就只能这么取地址，保持一致后调用方代码零分支：

```cpp
PFNWGLSWAPINTERVALEXTPROC pfnSwapInterval =
    (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
if (pfnSwapInterval) pfnSwapInterval(1);
```

表外的名字再交给平台层（`glXGetProcAddressARB` / `dlsym`），所以 GL 1.1~2.1 的核心入口也
能从同一个函数取到。

**(3) 能力诚实降级**——扩展串只声明真正实现的扩展；做不到的入口返回文档写明的失败值
（`NULL`/`FALSE`/`0`）。详见 §8。

## 3. 头文件层

### 3.1 `GL/gl.h`：转发 + 带出 WGL 声明

Windows 上 `<windows.h>` 里的 `wingdi.h` 会顺带声明 WGL 入口，所以按 WGL 写的代码往往只
`#include <GL/gl.h>`（甚至只含 `<GL/gl.h>` + `<windows.h>`）。macOS/Linux 没有这套东西，于是：

```cpp
// swinx/include/GL/gl.h
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include_next <GL/gl.h>      // 跳过本文件自己，取系统 GL 头（GL 1.1 声明与基础类型）
#endif
#include <wgl.h>            // HGLRC / PFD / wgl* / SwapBuffers 的唯一声明处
```

要点：

- **声明只有一份**（在 `wgl.h`），`GL/gl.h` 不再复写一遍，避免两处漂移。
- 用 `include_next` 而非 `"GL/gl.h"`，否则会自包含成死循环。
- 缺 GL 开发头（Ubuntu 上 `libgl1-mesa-dev`）时这里会直接报找不到文件——这是有意的，
  比在链接期报一堆未定义符号更好定位。

### 3.2 `wgl.h`：类型与入口

在系统头之外补齐 Win32 的类型、常量与声明：

- 类型：`HGLRC`、`PIXELFORMATDESCRIPTOR`、`LAYERPLANEDESCRIPTOR`、`POINTFLOAT`、
  `GLYPHMETRICSFLOAT`、`WGLSWAP`。
- 常量：`PFD_*`（双缓冲/深度/模板/交换行为）、`WGL_SWAP_*`（图层平面掩码）、
  `WGL_FONT_LINES/POLYGONS`。
- 入口：像素格式 3 个；上下文/绘制 10 个（`wglCreateContext`、`wglDeleteContext`、
  `wglMakeCurrent`、`wglGetCurrentContext`、`wglGetCurrentDC`、`wglGetProcAddress`、
  `wglGetDefaultProcAddress`、`wglShareLists`、`SwapBuffers`、`wglSwapMultipleBuffers`）；
  降级 11 个（图层平面 6 + `wglCopyContext` + GDI 字形 4）。
- 末尾 `#include <GL/wglext.h>`，让"包含 wgl.h 就能用扩展常量/指针类型"。

`LAYERPLANEDESCRIPTOR` / `GLYPHMETRICSFLOAT` 虽然对应入口恒失败，仍按 `wingdi.h` 的布局定义：
调用方常把这两个结构当成栈上变量声明，缺定义会直接编译失败。

### 3.3 `GL/wglext.h`：扩展常量与指针类型

Windows SDK 不提供 `wglext.h`（跨平台项目一般自带一份），非 Windows 上由 swinx 提供同名头，
保证 `#include <GL/wglext.h>` 三平台都能解析。内容是 Khronos 官方头的**兼容子集**，只列本层
真正实现（或明确降级）的扩展：

| 分组 | 内容 |
| --- | --- |
| `WGL_ARB_create_context` | `WGL_CONTEXT_MAJOR/MINOR_VERSION_ARB`、`LAYER_PLANE_ARB`、`FLAGS_ARB`、`PROFILE_MASK_ARB`、`CORE/COMPATIBILITY_PROFILE_BIT_ARB`、`DEBUG/FORWARD_COMPATIBLE/ROBUST_ACCESS_BIT_ARB` |
| 指针类型 | `PFNWGLGETEXTENSIONSSTRINGARB/EXTPROC`、`PFNWGLSWAPINTERVALEXT/GETSWAPINTERVALEXTPROC`、`PFNWGLCREATECONTEXTATTRIBSARBPROC`、`PFNWGLMAKECONTEXTCURRENTARB/GETCURRENTREADDCARBPROC` |

数值取自 Khronos 官方头，与 GLX 侧同名常量**同值**（`WGL_CONTEXT_MAJOR_VERSION_ARB ==
GLX_CONTEXT_MAJOR_VERSION_ARB == 0x2091`），因此 Linux 实现可以把 WGL 属性表原样透传给
`glXCreateContextAttribsARB`，不需要逐项翻译。

### 3.4 接入条件

主仓 `CMakeLists.txt` 只在**非 Windows** 分支做 `include_directories(swinx/include)`，
且 `add_subdirectory(swinx)` 也只在该分支执行。所以：

- Windows 上这三个头**不参与编译**，不会和系统 `GL/gl.h`、`wingdi.h` 打架；
- macOS/Linux 上 `swinx/include` 在系统头之前，`<GL/gl.h>` 优先命中 swinx 的转发头；
- `swinx/*.cmake` 用 `src/*.cpp` glob 收源文件，`wgl.cpp` 两平台自动编入。

## 4. 公共层实现（两平台共用）

### 4.1 上下文对象

`HGLRC` 直接就是结构指针：

```cpp
struct SwinxGLCtx
{
    void *pPlatform;   // macOS: SwinxGLBridge*，Linux: SwinxGLPlatform*
    HDC   hdc;         // 创建用的 DC（wglGetCurrentDC 回读它）
    HWND  hWnd;        // 创建用的窗口
    HGLRC hShare;      // wglShareLists 记录的共享源
    int   nMajor, nMinor, nProfileMask, nFlags;   // 请求的版本/profile
    BOOL  bCreating;   // 防共享关系成环时递归
    BOOL  bCreated;    // 平台资源已尝试创建（成功与否都置位，失败不重试）
};
```

`bCreated` 同时承担"失败缓存"的作用：`EnsureCreated` 一旦尝试过就不再重试，避免每帧
`wglMakeCurrent` 都去撞一次必然失败的 `glXChooseFBConfig`。

### 4.2 `wglMakeCurrent` 的失败登记

```cpp
// 摘要：省略参数校验与 (NULL,NULL) 解除分支
if (!EnsureCreated(pCtx))
    return FALSE;
BOOL bOK = PlatformMakeCurrent(pCtx->pPlatform, hWnd);
s_pCurCtx = pCtx;      // 关键：平台层失败也登记为当前上下文
return bOK;
```

失败仍登记"当前上下文"，是因为 **Linux 上 drawable 必须已映射到 server**，而 swinx 的窗口要等
`ShowWindow` 才 `xcb_map_window`；调用方通常"建窗后立刻建上下文"。此时 `glXMakeCurrent` 会失败，
但把上下文判死会让整个 GL 渲染永久失效。处理方式是：Linux 平台层记住 `bCurrent=false`，
在首次 `SwapBuffers` 时补一次 `glXMakeCurrent`（§6.5）。

### 4.3 `SwapBuffers` 的严格语义

```cpp
BOOL WINAPI SwapBuffers(HDC hdc)
{
    if (!s_pCurCtx || (hdc && s_pCurCtx->hdc != hdc)) return FALSE;
    return PlatformSwap(s_pCurCtx->pPlatform);
}
```

与 WGL 一致：只对"当前上下文绑定的 DC"生效；传别的 DC 返回 FALSE，不是"随便挑一个交换"。
`wglSwapMultipleBuffers` 逐项等价调用它，返回**成功交换的个数**（Windows 实测也是这样，
MS 未文档化该返回值）。

### 4.4 像素格式 API 的回显

两个平台的上下文属性在创建时固定为 **32 位色 + 24 位深度 + 8 位模板 + 双缓冲**，所以：

- `ChoosePixelFormat` 恒返回 1（唯一格式）；
- `SetPixelFormat` 恒 TRUE（无"一次一格式"的限制，也不存在 Windows 那种"设了就不能改"）；
- `DescribePixelFormat` 回显同一组值，让按 Win32 写的"读回来判断"逻辑得到符合实际的结果。

这是"效果与 Windows 一致"的一部分：Windows 上 `PFD` 请求 24 位深度意味着 `GL_DEPTH_TEST`
真的有效；两个平台若因没申请深度缓冲而静默退化，画面会和 Windows 不同（见 §6.3 的深度阶梯）。

### 4.5 扩展分派与扩展串

```cpp
static const SWGLProcEntry s_wglProcTable[] = {
    {"wglGetExtensionsStringARB",  ...}, {"wglGetExtensionsStringEXT", ...},
    {"wglSwapIntervalEXT", ...},         {"wglGetSwapIntervalEXT", ...},
    {"wglCreateContextAttribsARB", ...}, {"wglMakeContextCurrentARB", ...},
    {"wglGetCurrentReadDCARB", ...},     {NULL, NULL}};
```

扩展串按平台实际能力拼装（首次调用时定稿）：

```
"WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_EXT_swap_control"
    + (支持 3.x 时)" WGL_ARB_create_context WGL_ARB_create_context_profile"
```

串里出现的名字一定能取到入口，反之亦然——这条自洽性由 `opengl_demo` 的契约测试断言（§10）。

## 5. macOS 方案：`NSOpenGLContext` 桥接

落地点：`swinx/src/platform/cocoa/SGLBridge.mm`（约 340 行，对外只暴露 7 个 `extern "C"` 函数）。
macOS 上 **`HWND` 就是 `SNsWindow`（`NSView` 派生）指针**，所以"上下文绑窗口"直接映射为
`[NSOpenGLContext setView:]`。

### 5.1 像素格式与 profile

```objc
NSOpenGLPixelFormatAttribute attrs[] = {
    NSOpenGLPFAOpenGLProfile, profile,   // Legacy / 3_2Core / 4_1Core
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 32,
    NSOpenGLPFAAlphaSize, 8,             // 必须显式申请，见下
    NSOpenGLPFADepthSize, 24,
    NSOpenGLPFAStencilSize, 8,
    0
};
```

两个细节：

- **`NSOpenGLPFAAlphaSize 8` 不能省**。视图默认 `isOpaque=NO`，AppKit 按 per-pixel alpha 合成；
  不带 alpha 的 surface 写入的 alpha=1 会被丢弃，呈现时 alpha 取 surface 初值 0
  ⇒ 整个 GL 区域合成后**全透明**（看起来像"没画"）。
- **profile 只有三档**：`Legacy`(2.1) / `3_2Core` / `4_1Core`。版本映射：

  | 请求 | 结果 |
  | --- | --- |
  | `major < 3` | legacy 2.1 |
  | `3.0/3.1/3.2` | `3_2Core`（用更高一档满足） |
  | `3.3` 或 `4.0/4.1` | `4_1Core` |
  | `major=3, minor>2` 或 `major>4` 或 `4.1` 以上 | **NULL**（超出平台上限） |
  | 带 `COMPATIBILITY_PROFILE_BIT` | **NULL**（macOS 的 3.x 只有 core） |

  返回 NULL 而不是"降级成 2.1"：`WGL_ARB_create_context` 的约定就是失败交由调用方决定回退，
  core-only 的代码拿到 2.1 反而会因为固定管线被移除而更难排查。

### 5.2 视图表面归属记账（本方案的核心难点）

AppKit 里**一张视图只有"一张当前 GL 表面"**。第二个 `NSOpenGLContext` 对同一视图 `setView:`，
会把它顶掉——但**被顶掉的那个 `ctx.view` 仍然指回该视图**，所以"比较指针"根本发现不了异常。
如果再把这个临时上下文 `clearDrawable`，视图的表面就彻底释放，原上下文之后的绘制全部落空：
画面上还留着窗口服务器的旧像素（看着正常），一旦窗口需要重画就整块变空白。

这不是理论问题：`opengl_demo` 的「API 测试」会临时建 3 个上下文（`wglShareLists` 的两个 +
`wglCreateContextAttribsARB` 的 3.2 core 一个），并逐个 `MakeCurrent` 再销毁，实测就是这个症状
（弹窗关掉后 GL 区域空白；而「API 探针报告」不动上下文，所以正常）。Linux 没有这个问题：
GLX 的 drawable 就是窗口本身，切换上下文不影响它。

修法是**按视图记一份"表面归属栈"，栈顶 = 当前拥有表面的上下文**：

```objc
// 栈：NSMapTable weakToStrongObjectsMapTable，视图弱键 ⇒ 视图销毁自动摘条目，不留悬空指针
static NSMapTable *SwinxViewStacks(void);

static void SwinxBindView(NSView *view, SwinxGLBridge *pBridge);    // makeCurrent 时调用
static void SwinxUnbindView(SwinxGLBridge *pBridge);                // destroy 时调用
```

- **`SwinxBindView`（makeCurrent）**：把自己压到栈顶；不在栈顶就重新 `setView:` 夺回表面。
  常见路径（渲染循环每帧都 makeCurrent）走一个短路判断——"已是栈顶且 `ctx.view == view`
  且不需要重绑"直接返回，不做任何遍历。
- **`SwinxUnbindView`（destroy）**：先从栈里摘掉自己（**不信任 `ctx.view`**，遍历视图表找），
  再 `clearDrawable`；若栈里还有别的上下文，把新的栈顶无条件重新 `setView:` 并置
  `nViewW = nViewH = -1`（下次 makeCurrent 必 `update`）。若栈顶恰好是本线程的当前上下文，
  则只置 `bNeedsRebind`，**不在 current 期间改 drawable**，留给下一次 makeCurrent 重绑。
- **`swinxGLCreate` 不再 `setView:`**：绑定挪到首次 makeCurrent，临时上下文一建出来就不会抢表面。

一句话总结这个契约：**创建不绑、makeCurrent 抢占、销毁后把栈顶交还**。

### 5.3 不透明与绘制派发

GL 直绘的视图不能在 AppKit 的绘制周期里被"擦成背景色"，否则每帧都被清掉：

- `swinxGLCreate` 调用 `setNsWindowGLDirect(hWnd, TRUE)`，`SNsWindow` 记 `m_bGLDirect`；
- `-isOpaque` 对 GL 直绘窗口返回 `YES`（内容每帧全幅更新，AppKit 无需也无力替它擦除）；
- `drawRect:` 在 `cgContext == NULL` 且 `m_bGLDirect` 时**仍然继续**派发绘制。
  GL 视图不经 CoreGraphics 绘制，显示周期里拿不到 `CGContext`；若因此直接 return，
  `drawRect → OnDrawRect → WM_PAINT` 的派发链就断了，动画定时器每帧 `Invalidate` 都换不来
  任何绘制，GL 区域的逻辑层更新全部停滞。

### 5.4 `update` 的时机

窗口几何变化或表面重绑后必须 `[ctx update]` 才能正确重投影。实现放在 `swinxGLMakeCurrent`：

```objc
if (nW != pBridge->nViewW || nH != pBridge->nViewH) {
    if (view.window) {                  // 还没上窗口时 drawable 不存在，update 无意义
        pBridge->nViewW = nW; pBridge->nViewH = nH;
        [pBridge->ctx update];
    }
}
[pBridge->ctx makeCurrentContext];
```

初始值取 `-1`，保证首次 makeCurrent 一定 update 一次。

### 5.5 入口解析与垂直同步

- `swinxGLGetProcAddress` 用 `dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL")`
  后 `dlsym`。桌面 GL 2.1 的全部入口（含 GLSL/VBO）都是该 framework 直连导出的符号，
  不需要扩展加载机制；`RTLD_DEFAULT` 兜底。
- 垂直同步走 `[ctx setValues:&v forParameter:NSOpenGLCPSwapInterval]`。

## 6. Linux 方案：GLX 动态加载

落地点：`swinx/src/wgl.cpp` 的 `#else // !__APPLE__` 分支。设计目标是
**不引入 X11/GLX 开发头，也不新增链接期依赖**——运行时用 `dlopen` 加载
`libGL.so.1` / `libX11.so.6`，缺库时入口直接返回失败，宿主程序不会因此起不来。

### 6.1 自建最小声明表

不 `#include <X11/Xlib.h>`、不 `#include <GL/glx.h>`，改为：

```cpp
typedef struct _XDisplay Display;      typedef unsigned long XID;
typedef XID Window;                    typedef XID VisualID;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef struct __GLXContextRec   *GLXContext;

#define LX_GLX_DOUBLEBUFFER  5
#define LX_GLX_SCREEN        0x800C
#define LX_GLX_VISUAL_ID     0x800B
#define LX_GLX_RGBA_TYPE     0x8014
#define LX_GLX_DEPTH_SIZE    12
#define LX_GLX_STENCIL_SIZE  13
#define LX_GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091   // 与 WGL_* 同值
...
struct SwinxXVisualInfo { void *visual; unsigned long visualid; int screen; int depth;
                          int c_class; /* C++ 下 XVisualInfo 的 class 字段名是 c_class */ ... };
```

之所以连 `XVisualInfo`/`XErrorEvent` 的布局都自己写：只用到 `visualid` 与几个错误码字段，
不值得为它引入整套 X11 头（也避免 `Window` 这种极常见的类型名污染调用方的编译单元）。

`EnsureGLX()` 一次性解析入口表并缓存结果（成功失败都只试一次，避免反复 dlopen 泄漏句柄）：

```
必需：XOpenDisplay / XCloseDisplay / XFree / glXChooseFBConfig / glXCreateNewContext /
      glXMakeCurrent / glXSwapBuffers / glXDestroyContext
可选：XDefaultScreen / XSetErrorHandler / glXGetVisualFromFBConfig /
      glXCreateContextAttribsARB / glXSwapInterval{EXT,MESA,SGI} / glXGetProcAddressARB
```

`dlopen` 一律带 `RTLD_GLOBAL`：后面用 `dlsym(RTLD_DEFAULT, ...)` 兜底取 GL 1.1 入口时，
要求这些符号已经进入全局作用域。

### 6.2 两条 X 连接：刷新顺序是关键

swinx 的窗口建在 **xcb** 连接上，GLX 走的是**另一条 Xlib 连接**（`EnsureXDisplay` 里
`XOpenDisplay(NULL)`）。同一进程两条连接，server 侧对同一个 XID 的可见性是有时序的：

```cpp
if (wndObj->mConnection)
    wndObj->mConnection->flush();   // 先把建窗请求刷到 server
Display *dpy = EnsureXDisplay();
```

少了这一步，后续 `MakeCurrent`/`SwapBuffers` 会拿到 `BadWindow`。

屏幕号也取自 Xlib 侧：`int screen = s_XDefaultScreen ? s_XDefaultScreen(dpy) : 0;`
（原实现硬编码 0，多屏环境下 FBConfig 会挑到别的屏幕上）。

### 6.3 FBConfig：Visual 必须精确匹配，深度必须显式请求

GLX 的上下文必须与窗口的 **Visual** 一致，否则 `glXMakeCurrent` 必失败。窗口的 Visual 在
建窗时记进 `_Window::visualId`，这里据此挑 FBConfig：

```cpp
// 挑一次：给定深度/模板位要求，按 Visual 精确匹配（见下面两条路）
static GLXFBConfig PickFBConfigOnce(Display *dpy, int screen, VisualID visualId,
                                   int bDoubleBuffer, int nDepthBits, int nStencilBits);
// 对外：按深度阶梯逐级放宽
static GLXFBConfig PickFBConfig(Display *dpy, int screen, VisualID visualId, int bDoubleBuffer);
```

`PickFBConfigOnce` 走两条路，都保证 Visual 精确匹配：

1. 首选：把 `GLX_VISUAL_ID = visualId` 作为过滤属性交给 `glXChooseFBConfig`；
2. 兜底：个别实现不支持该属性（直接返回空），则放宽拿回候选，再用
   `glXGetVisualFromFBConfig` 逐个比对 `XVisualInfo.visualid`。

**深度/模板位必须显式请求。** `glXChooseFBConfig` 对没写 `GLX_DEPTH_SIZE` 的属性是按
"至少 0 位"处理的，最可能匹配到**没有深度缓冲**的配置——于是 `glEnable(GL_DEPTH_TEST)`
形同虚设，立方体面片按绘制顺序互相覆盖。Windows 的 `PFD` 请求 24 位深度，这里按
`24+8 → 24 → 16 → 不要求` 阶梯逐级放宽，兼顾新老驱动：

```cpp
static const int kDepthLadder[4][2] = {{24, 8}, {24, 0}, {16, 0}, {0, 0}};
```

`PlatformCreateCtx` 再补一层"要求双缓冲 → 允许单缓冲"的回退（同样不放宽 Visual）。
两层回退都失败说明该窗口 Visual 确实不支持 GLX，返回 NULL 由调用方处理。

> 诊断出口：真机上 `glGetIntegerv(GL_DEPTH_BITS)` 应为 24。若为 0，就是这一环没对上。

### 6.4 Xlib 错误回调：只记录，不退出

**Xlib 的默认错误处理是打印后 `exit()`。** GLX 的 `BadMatch`/`BadWindow` 属于可恢复错误
（窗口未映射就 MakeCurrent、窗口刚销毁仍在 SwapBuffers），让进程退出会直接拖垮整个 UI。
因此在 `EnsureXDisplay` 里 `XOpenDisplay` **之前**装自己的回调：

```cpp
static int SwinxXErrorHandler(Display *dpy, SwinxXErrorEvent *ev)
{
    SLOG_FMTW("X error: code=%d request=%d minor=%d resource=0x%lx", ...);
    return 0;
}
...
if (s_XSetErrorHandler) s_XSetErrorHandler(SwinxXErrorHandler);
s_pDisplay = s_XOpenDisplay(NULL);
```

顺序不能反：**先装回调、再 `XOpenDisplay`**。若反过来（先开连接），连接建立阶段的错误
仍会落到 Xlib 默认处理上——也就是直接 `exit()`。

### 6.5 drawable 尚未 map：`bCurrent` + 推迟补绑

平台上下文带一个"是否真的成为当前"的标记：

```cpp
struct SwinxGLPlatform
{
    Display *dpy;  GLXContext ctx;  GLXDrawable_LX draw;
    bool bCurrent;   // glXMakeCurrent 是否真的成功
};
```

- `PlatformMakeCurrent`：失败时 `bCurrent=false` 并**返回 FALSE**（不谎报成功），
  但公共层仍登记当前上下文；
- `PlatformSwap`：若 `!bCurrent`，先重试 `glXMakeCurrent`，成功了本帧才真正上屏；
- `PlatformClearCurrent` / `PlatformDestroyCtx`：只在 `bCurrent` 为真时才清 GLX 状态，
  避免对从未绑定的上下文做 `glXMakeCurrent(dpy, 0, NULL)`。

### 6.6 垂直同步：三级回退

`wglSwapIntervalEXT` 在 Linux 上没有统一入口，按优先级依次尝试：

```cpp
if (s_glXSwapIntervalEXT) { s_glXSwapIntervalEXT(dpy, draw, n); return TRUE; }  // EXT，per-drawable
if (s_glXSwapIntervalMESA) return s_glXSwapIntervalMESA(n) == 0;               // MESA，全局
if (s_glXSwapIntervalSGI)  return s_glXSwapIntervalSGI(n) == 0;                // SGI，全局
return FALSE;                                                                  // 都没有 ⇒ 明确失败
```

公共层先拒绝负间隔（`WGL_EXT_swap_control` 规定 `interval < 0` 为非法），再调平台层。
注意有些驱动（实测 Intel）在 Windows 上会接受 `-1`，swinx 按规范拒绝——契约测试里
这一条在 Windows 上记为 SKIP、在 swinx 上强制断言。

### 6.7 版本/profile 透传，不静默降级

`WGL_CONTEXT_*_ARB` 与 `GLX_CONTEXT_*_ARB` **同值**，所以属性表可以直接透传：

```cpp
bool bRequireAttribs = (nMajor > 2) || (nMajor == 2 && nMinor > 1) || nProfileMask || nFlags;
if (bRequireAttribs) {
    if (!s_glXCreateContextAttribsARB) return NULL;   // 不悄悄降到 2.1

    int attribs[10], n = 0;
    attribs[n++] = LX_GLX_CONTEXT_MAJOR_VERSION_ARB; attribs[n++] = nMajor;
    attribs[n++] = LX_GLX_CONTEXT_MINOR_VERSION_ARB; attribs[n++] = nMinor;
    if (nProfileMask) { attribs[n++] = LX_GLX_CONTEXT_PROFILE_MASK_ARB; attribs[n++] = nProfileMask; }
    if (nFlags)       { attribs[n++] = LX_GLX_CONTEXT_FLAGS_ARB;        attribs[n++] = nFlags; }
    attribs[n++] = LX_GLX_ATTR_END;

    ctx = s_glXCreateContextAttribsARB(dpy, cfg, share, 1 /* True */, attribs);
} else {
    ctx = s_glXCreateNewContext(dpy, cfg, LX_GLX_RGBA_TYPE, share, 1);  // legacy 走默认行为
}
```

legacy 请求走 `glXCreateNewContext` 而不是 ARB 入口，是为了和驱动默认行为完全一致
（部分老驱动对 `glXCreateContextAttribsARB(2,1)` 的处理反而不如前者）。

> `LX_GLX_ATTR_END` 是 `0`。不要写成 `0x8000`（`GLX_NONE`）——它是**属性值**，不是终止符，
> 用作终止符会让属性表永不结束。

最后 `PlatformGetProcAddress` 先用 `glXGetProcAddressARB`，失败再
`dlsym(RTLD_DEFAULT, name)` 取 GL 1.1 入口。

## 7. 两平台对照

| 维度 | macOS | Linux |
| --- | --- | --- |
| 落地文件 | `platform/cocoa/SGLBridge.mm` | `src/wgl.cpp` 的 Linux 分支 |
| 实现技术 | `NSOpenGLContext` + `setView:` | GLX（`glXCreate*` / `glXMakeCurrent` / `glXSwapBuffers`） |
| 依赖引入方式 | 直接链接 Cocoa/OpenGL（系统 framework） | `dlopen("libGL.so.1")` / `dlopen("libX11.so.6")`，无编译/链接期依赖 |
| `HWND` 含义 | `SNsWindow`（`NSView` 派生） | X11 窗口（XID） |
| 上下文绑窗口 | `[ctx setView:view]` | `glXMakeCurrent(dpy, (GLXDrawable)hwnd, ctx)` |
| 像素格式选择 | `NSOpenGLPixelFormat` 属性表（创建时固定） | `glXChooseFBConfig` 按 Visual + 深度阶梯挑 |
| 深度/模板 | `NSOpenGLPFADepthSize 24` / `StencilSize 8` | `GLX_DEPTH_SIZE 24` + `GLX_STENCIL_SIZE 8`，阶梯放宽 |
| 版本/profile | 只有 legacy(2.1) / 3.2 core / 4.1 core，超范围返回 NULL | 透传 WGL 属性给 `glXCreateContextAttribsARB`，缺入口返回 NULL |
| 垂直同步 | `NSOpenGLCPSwapInterval` | `glXSwapIntervalEXT` → `MESA` → `SGI` |
| 交换 | `[ctx flushBuffer]` | `glXSwapBuffers` |
| 几何变化 | 尺寸变化或重绑后 `[ctx update]` | 无需处理（drawable 自适应） |
| 特有难点 | 一视图一表面 ⇒ 需"表面归属栈"（§5.2） | drawable 需先 map、双 X 连接时序（§6.2/6.5） |
| 特有风险 | AppKit 擦除 GL 区域 ⇒ `isOpaque`/`drawRect` 特殊处理（§5.3） | Xlib 默认错误处理是 `exit()`（§6.4） |
| 入口解析 | `dlsym` OpenGL.framework | `glXGetProcAddressARB` → `dlsym(RTLD_DEFAULT)` |
| 诊断标签 | `SLOG_*` + `kLogTag "SGLBridge"` | `SLOG_*` + `kLogTag "wgl"` |

## 8. 能力矩阵

| 分类 | 入口 | macOS | Linux |
| --- | --- | --- | --- |
| 核心 | `ChoosePixelFormat` / `DescribePixelFormat` / `SetPixelFormat` | 固定 32/24/8 + 双缓冲，恒选 1 号格式 | 同左 |
| 核心 | `wglCreateContext` / `wglMakeCurrent` / `wglDeleteContext` / `wglGetCurrentDC` / `wglGetCurrentContext` / `SwapBuffers` | 完整 | 完整（首次 Swap 补绑） |
| 核心 | `wglShareLists` | 支持"先创建、后共享、再 MakeCurrent"；平台资源已建后返回 FALSE | 同左 |
| 核心 | `wglSwapMultipleBuffers` | 逐项等价 `SwapBuffers`，返回成功个数 | 同左 |
| 核心 | `wglGetDefaultProcAddress` | 等价 `wglGetProcAddress` | 同左 |
| 扩展 | `wglCreateContextAttribsARB` | 3.2 core / 4.1 core；compatibility 与 >4.1 返回 NULL | 透传；入口缺失时返回 NULL |
| 扩展 | `wglSwapIntervalEXT` / `wglGetSwapIntervalEXT` | `NSOpenGLCPSwapInterval` | EXT → MESA → SGI，都无则 FALSE |
| 扩展 | `wglGetExtensionsStringARB/EXT` | 按实际能力拼装 | 同左 |
| 扩展 | `wglMakeContextCurrentARB` / `wglGetCurrentReadDCARB` | 读写同窗口可用；不同 DC 返回 FALSE | 同左 |
| 降级 | `wglCreateLayerContext`（返回 NULL）、`wglDescribeLayerPlane` / `wglSet/GetLayerPaletteEntries` / `wglRealizeLayerPalette` / `wglSwapLayerBuffers`（FALSE/0） | 恒失败 | 恒失败 |
| 降级 | `wglUseFontBitmapsA/W` / `wglUseFontOutlinesA/W` | 恒 FALSE | 恒 FALSE |
| 降级 | `wglCopyContext` | 恒 FALSE | 恒 FALSE |
| 未提供 | `wglChoosePixelFormatARB` | `wglGetProcAddress` 返回 NULL（个别驱动对未知名字不返回 NULL，取到也调不通，属驱动差异） | 同左 |

## 9. 调用方写法

因为两平台共用同一套 Win32 语义，应用代码不需要任何平台分支：

```cpp
#include <windows.h>
#include <GL/gl.h>        // swinx 上会自动带出 wgl.h
#include <GL/wglext.h>    // 扩展常量与 PFNWGL*PROC（macOS/Linux 由 swinx 提供）

// 1) 建上下文（HDC 必须来自 GetDC(hwnd)，即 swinx 窗口自己的 DC）
HDC hdc = ::GetDC(hWnd);
PIXELFORMATDESCRIPTOR pfd = {0};
pfd.nSize        = sizeof(pfd);
pfd.nVersion     = 1;
pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
pfd.iPixelType   = PFD_TYPE_RGBA;
pfd.cColorBits   = 32;
pfd.cDepthBits   = 24;
pfd.cStencilBits = 8;
::SetPixelFormat(hdc, ::ChoosePixelFormat(hdc, &pfd), &pfd);
HGLRC hRC = ::wglCreateContext(hdc);
::wglMakeCurrent(hdc, hRC);

// 2) 渲染循环
Render();
::SwapBuffers(hdc);

// 3) 需要 3.x 时（扩展入口按 Windows 写法取地址）
typedef HGLRC (WINAPI *PFN_CREATE_CTX)(HDC, HGLRC, const int *);
PFN_CREATE_CTX pfnCreate = (PFN_CREATE_CTX)::wglGetProcAddress("wglCreateContextAttribsARB");
if (pfnCreate) {
    int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 2,
                      WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    HGLRC hCore = pfnCreate(hdc, NULL, attribs);   // NULL ⇒ 该平台不支持，自己决定回退
}
```

调用方还需注意两件事：

1. **平台系统 GL 头必须在 `SNSBEGIN`（进入 `namespace SOUI`）之前包含。**
   Xlib 的 `Window`/`Display`/`Visual` 与 namespace 内同名符号（如 `sobject-i.h` 的枚举成员
   `Window`）会直接冲突，报 "typedef XID Window redeclared"；Windows 上 `<utils/SkWGL.h>`
   进了命名空间则会把 `SkCreateWGLContext` 变成 `SOUI::SkCreateWGLContext`，链接期找不到。
   `components/render-skia/GLContext-Skia.cpp` 就是这个坑的现成注脚，改动时别把包含链挪回去。
2. **`realwnd` 不要套在带 `padding` 的普通窗口里**（内缩后 `fill_parent` 多 2px 会级联出
   滚动条），直接挂在 root 下；`SetPixelFormat` 一旦设置就不必重复设。

## 10. 验证

### 10.1 端到端示例

`demos/opengl_demo`（`SRealWnd` + `SNativeWnd` + OpenGL）**三端同一份源码**：

- 两条渲染路径：固定管线（`glFrustum/glBegin`）与 GLSL+VBO（`#version 120`）；
- 交互：左键拖动旋转、滚轮缩放、双击暂停、切渲染路径、重建上下文、API 探针；
- `--selftest` 无人值守：三条截图（固定管线 / GLSL / 重建上下文后）+ 报告落盘。

### 10.2 契约测试（`GLWglTest.cpp`）

测的是**行为契约**而非"某驱动是否支持某扩展"，所以同一份断言三端通用：

| 分组 | 断言 |
| --- | --- |
| 入口取址 | 7 个扩展入口都能经 `wglGetProcAddress` 取到 |
| 扩展串自洽 | 串里有的名字必须取得到入口，反之亦然 |
| `wglSwapIntervalEXT` | `Set(1)` 回读 1；负间隔应被拒（不校验的驱动记 SKIP）；结束恢复原值 |
| `wglCreateContextAttribsARB` | 3.2 core 能建且 `GL_VERSION >= 3.2`；请求 9.9 必须 NULL；core+compatibility 同置必须 NULL |
| `wglShareLists` | 源上下文 `glGenTextures` 的名字在目标上下文 `glIsTexture` 可见 |
| `wglMakeContextCurrentARB` | 读写同窗口 TRUE，`wglGetCurrentReadDCARB` 回读窗口 DC |
| `wglSwapMultipleBuffers` | `(0,NULL)` 返回 0；真实交换后上下文与 `glGetError` 未被扰动 |
| 降级入口 | 11 个必须在 swinx 上返回文档写明的失败值 |
| 收尾 | 整套测试不得改变线程级 `wglGetCurrentContext()` / `wglGetCurrentDC()` |

### 10.3 观察点

| 现象 | 说明 |
| --- | --- |
| `GL_DEPTH_BITS == 24` | FBConfig 深度阶梯生效；为 0 说明选到了无深度缓冲的配置（§6.3） |
| GLSL 程序"编译链接 OK" | `#version 120` 在 legacy 2.1 下可用 |
| `wglSwapIntervalEXT` 可达 | 三平台都应能取到（Linux 取决于驱动是否提供 EXT/MESA/SGI） |
| 弹窗关闭后 GL 继续动画 | macOS 表面归属记账生效（§5.2），否则会空白 |

## 11. 已知限制

1. **当前上下文是进程级全局而非线程级。** Win32 的 current context 是 per-thread；
   swinx 的 `s_pCurCtx` 是单个静态变量。单线程渲染（SOUI 的用法）不受影响，但多线程各持
   上下文交叉 `MakeCurrent` 时会互相干扰。需要时改为 `thread_local`/TLS 即可。
2. **`wglChoosePixelFormatARB` 未实现**（无 `WGL_ARB_pixel_format`），因此拿不到 MSAA。
   入口探测返回 NULL，调用方按"不支持"降级。
3. **`wglShareLists` 只在平台资源创建前有效**。资源已建后再共享返回 FALSE（两个平台都只允许
   创建时指定 share group）；调用方需要共享时必须先 `wglShareLists` 再 `wglMakeCurrent`。
4. **macOS 只有 legacy / 3.2 core / 4.1 core**，无 compatibility profile，也没有独立的
   3.0/3.1/3.3/4.0。超出范围返回 NULL。
5. **macOS 的 `NSOpenGLContext` 自 10.14 起被 Apple 标记 deprecated**，但仍是可用的桌面 GL
   实现（部署目标 12.0，编译侧用 `-Wno-deprecated-declarations`）。
6. **Linux 需要 libGL 存在**；纯 Wayland 且无 XWayland 的环境下没有 X11 窗口，GLX 不可用。
7. **单 drawable**：一个上下文只绑一张表面，读写不同窗口返回 FALSE（§8）。

## 12. 关键文件索引

| 文件 | 作用 |
| --- | --- |
| `swinx/include/wgl.h` | WGL 类型/常量/声明（两平台唯一声明源）+ 能力矩阵注释 |
| `swinx/include/GL/gl.h` | 转发平台 GL 头并带出 `wgl.h` |
| `swinx/include/GL/wglext.h` | WGL 扩展常量与 `PFNWGL*PROC` |
| `swinx/src/wgl.cpp` | 公共层（上下文对象/生命周期/核心入口/扩展分派/像素格式）+ Linux GLX 实现 |
| `swinx/src/platform/cocoa/SGLBridge.mm` | macOS `NSOpenGLContext` 桥接 + 视图表面归属记账 |
| `swinx/src/platform/cocoa/SNsWindow.mm` | `m_bGLDirect` / `-isOpaque` / `drawRect` 的 GL 直绘处理 |
| `swinx/src/platform/linux/SConnection.cpp` | 建窗时记录 `visualId`（GLX 挑 FBConfig 的依据） |
| `demos/opengl_demo/` | 三端同源示例 + `--selftest` + WGL 入口契约测试 |
| `components/render-skia/GLContext-Skia.cpp` | 消费端：平台 GL 头必须在 `SNSBEGIN` 之前包含 |
