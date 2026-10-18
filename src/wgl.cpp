/**
 * @file       wgl.cpp
 * @brief      wgl.h 声明的 WGL 兼容层实现（仅非 Windows 平台编译）。
 *
 * @details    结构分两层：
 *             - 平台层（文件内 static）：上下文创建/绑定/交换/入口解析。
 *                 __APPLE__ : 转调 cocoa/SGLBridge.mm 的 NSOpenGLContext 桥接。
 *                 Linux     : 用 dlopen 加载 libGL/libX11 走 GLX，不引入 X11/GLX
 *                             开发头与链接期依赖；缺库时入口直接返回失败。
 *             - 公共层（两平台共用）：WGL 上下文对象、核心入口、WGL 扩展分派。
 *               上下文对象在首次 wglMakeCurrent 时才真正创建，这样
 *               wglShareLists 能在"创建之后、绑定之前"生效（GLX/NSOpenGL 都只允许
 *               在创建时指定 share group，无法后设）。
 *
 *             GLX 上下文必须与窗口的 Visual 一致：用建窗时记下的
 *             _Window::visualId 选同 Visual 的 FBConfig（见 PickFBConfig），
 *             选不到（该 Visual 不支持 GLX）则返回失败。
 */

#include <wgl.h>
#include <string.h>
#include "sdc.h"
#include "wndobj.h"
#include "log.h"
#define kLogTag "wgl"

///////////////////////////////////////////////////////////////////////////////
// 平台层
///////////////////////////////////////////////////////////////////////////////

#ifdef __APPLE__

extern "C" {
/** 创建绑定到 HWND(=SNsWindow NSView) 的 GL 上下文；nProfile 取 0/0x3200/0x4100。 */
void *swinxGLCreate(HWND hWnd, int nProfile, void *pShareCtx);
/** 销毁 swinxGLCreate 返回的句柄。 */
void swinxGLDestroy(void *pCtx);
/** 把上下文置为当前；窗口几何变化时顺带 [NSOpenGLContext update]。 */
BOOL swinxGLMakeCurrent(void *pCtx, HWND hWnd);
/** 解除当前上下文。 */
void swinxGLClearCurrent(void *pCtx);
/** 双缓冲交换（flushBuffer）。 */
BOOL swinxGLSwap(void *pCtx);
/** 按 dlsym 取 GL 入口（OpenGL framework 直连导出 GL 1.1~2.1 全部符号）。 */
void *swinxGLGetProcAddress(const char *pszName);
/** 设置垂直同步间隔（NSOpenGLCPSwapInterval）；返回 FALSE 表示上下文未就绪。 */
BOOL swinxGLSetSwapInterval(void *pCtx, int nInterval);
}

static void *PlatformCreateCtx(HWND hWnd, int nMajor, int nMinor, int nProfileMask, int nFlags,
                               void *pShareCtx)
{
    // macOS 只提供 legacy(2.1) / 3.2 core / 4.1 core 三档，没有兼容 profile，
    // 也没有独立的 3.0/3.1/3.3/4.0 版本。请求能用更高一档满足就映射过去，
    // 满足不了返回 NULL（按 WGL_ARB_create_context 的约定，让调用方降级）。
    int nProfile = 0;
    if (nMajor >= 3)
    {
        if (nProfileMask & WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB)
            return NULL; // macOS 的 3.x 只有 core
        if (nMajor > 4 || (nMajor == 4 && nMinor > 1) || (nMajor == 3 && nMinor > 2))
            return NULL; // 超出 4.1 上限
        nProfile = (nMajor >= 4) ? 0x4100 : 0x3200;
    }
    (void)nFlags; // debug / forward-compatible 在 macOS 无对应开关
    return swinxGLCreate(hWnd, nProfile, pShareCtx);
}

static void PlatformDestroyCtx(void *pCtx)
{
    swinxGLDestroy(pCtx);
}

static BOOL PlatformMakeCurrent(void *pCtx, HWND hWnd)
{
    return swinxGLMakeCurrent(pCtx, hWnd);
}

static void PlatformClearCurrent(void *pCtx)
{
    swinxGLClearCurrent(pCtx);
}

static BOOL PlatformSwap(void *pCtx)
{
    return swinxGLSwap(pCtx);
}

static void *PlatformGetProcAddress(const char *pszName)
{
    return swinxGLGetProcAddress(pszName);
}

static BOOL PlatformSetSwapInterval(void *pCtx, int nInterval)
{
    return swinxGLSetSwapInterval(pCtx, nInterval);
}

static BOOL PlatformSupportsContextAttribs(void)
{
    return TRUE; // 3.2/4.1 core 由 NSOpenGLPFAOpenGLProfile 提供
}

#else // !__APPLE__ -> Linux/X11

#include <dlfcn.h>

typedef struct _XDisplay Display;
typedef unsigned long XID;
typedef XID Window;
typedef XID VisualID;
typedef int Bool_LX;
typedef unsigned long GLXDrawable_LX;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef struct __GLXContextRec *GLXContext;

/* GLX 常量（glext.h 的子集，避免依赖 GLX 开发头）。数值与同名 WGL_* 一致。 */
#define LX_GLX_ATTR_END 0 /* attribList 以 None(0) 结尾；0x8000(GLX_NONE) 是属性值不是终止符 */
#define LX_GLX_DOUBLEBUFFER 5
#define LX_GLX_SCREEN 0x800C
#define LX_GLX_VISUAL_ID 0x800B
#define LX_GLX_RGBA_TYPE 0x8014
#define LX_GLX_DEPTH_SIZE 12
#define LX_GLX_STENCIL_SIZE 13
#define LX_GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define LX_GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define LX_GLX_CONTEXT_FLAGS_ARB 0x2094
#define LX_GLX_CONTEXT_PROFILE_MASK_ARB 0x9126

/* XVisualInfo 的 C++ 布局（不引入 Xutil.h）：只用 visualid 做 Visual 比对 */
typedef struct
{
    void *visual;
    unsigned long visualid;
    int screen;
    int depth;
    int c_class; /* C++ 下 XVisualInfo 的 class 字段名是 c_class */
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    int colormap_size;
    int bits_per_rgb;
} SwinxXVisualInfo;

/** XErrorEvent 的字段布局（同 X11/Xlib.h，不引系统头）。 */
typedef struct
{
    int type;
    Display *display;
    unsigned long resourceid;
    unsigned long serial;
    unsigned char error_code;
    unsigned char request_code;
    unsigned char minor_code;
} SwinxXErrorEvent;
typedef int (*PFN_XErrorHandler)(Display *, SwinxXErrorEvent *);

typedef Display *(*PFN_XOpenDisplay)(const char *);
typedef int (*PFN_XCloseDisplay)(Display *);
typedef void (*PFN_XFree)(void *);
typedef int (*PFN_XDefaultScreen)(Display *);
typedef PFN_XErrorHandler (*PFN_XSetErrorHandler)(PFN_XErrorHandler);
typedef GLXFBConfig *(*PFN_glXChooseFBConfig)(Display *, int, const int *, int *);
typedef GLXContext (*PFN_glXCreateNewContext)(Display *, GLXFBConfig, int, GLXContext, Bool_LX);
typedef GLXContext (*PFN_glXCreateContextAttribsARB)(Display *, GLXFBConfig, GLXContext, Bool_LX,
                                                     const int *);
typedef Bool_LX (*PFN_glXMakeCurrent)(Display *, GLXDrawable_LX, GLXContext);
typedef void (*PFN_glXSwapBuffers)(Display *, GLXDrawable_LX);
typedef void (*PFN_glXDestroyContext)(Display *, GLXContext);
typedef void *(*PFN_glXGetProcAddressARB)(const unsigned char *);
typedef SwinxXVisualInfo *(*PFN_glXGetVisualFromFBConfig)(Display *, GLXFBConfig);
typedef void (*PFN_glXSwapIntervalEXT)(Display *, GLXDrawable_LX, int);
typedef int (*PFN_glXSwapIntervalMESA)(unsigned int);
typedef int (*PFN_glXSwapIntervalSGI)(int);

/** Linux 侧的平台上下文（公共层的 SwinxGLCtx 持有它的指针）。 */
struct SwinxGLPlatform
{
    Display *dpy;
    GLXContext ctx;
    GLXDrawable_LX draw;
    bool bCurrent; /**< glXMakeCurrent 是否真的成功（窗口未 map 时可能失败） */
};

static PFN_XOpenDisplay s_XOpenDisplay = NULL;
static PFN_XCloseDisplay s_XCloseDisplay = NULL;
static PFN_XFree s_XFree = NULL;
static PFN_XDefaultScreen s_XDefaultScreen = NULL;
static PFN_XSetErrorHandler s_XSetErrorHandler = NULL;
static PFN_glXChooseFBConfig s_glXChooseFBConfig = NULL;
static PFN_glXCreateNewContext s_glXCreateNewContext = NULL;
static PFN_glXCreateContextAttribsARB s_glXCreateContextAttribsARB = NULL;
static PFN_glXMakeCurrent s_glXMakeCurrent = NULL;
static PFN_glXSwapBuffers s_glXSwapBuffers = NULL;
static PFN_glXDestroyContext s_glXDestroyContext = NULL;
static PFN_glXGetProcAddressARB s_glXGetProcAddressARB = NULL;
static PFN_glXGetVisualFromFBConfig s_glXGetVisualFromFBConfig = NULL; /* 可选入口 */
static PFN_glXSwapIntervalEXT s_glXSwapIntervalEXT = NULL;             /* 可选入口 */
static PFN_glXSwapIntervalMESA s_glXSwapIntervalMESA = NULL;           /* 可选入口 */
static PFN_glXSwapIntervalSGI s_glXSwapIntervalSGI = NULL;             /* 可选入口 */

static Display *s_pDisplay = NULL; /**< 进程内共享的 Xlib 连接（GLX 需要 Xlib Display） */
static void *s_hGL = NULL;         /**< libGL 句柄，进程级缓存 */
static void *s_hX11 = NULL;        /**< libX11 句柄，进程级缓存 */
static bool s_bGLXChecked = false; /**< 入口表已解析过（成功失败都只解析一次） */
static bool s_bGLXReady = false;

/** dlopen 并解析 GLX 所需入口；缺库或缺任一必需入口都判定 GLX 不可用。 */
static bool EnsureGLX()
{
    if (s_bGLXChecked)
        return s_bGLXReady;
    s_bGLXChecked = true;

    // RTLD_GLOBAL：后面 dlsym(RTLD_DEFAULT) 兜底取 GL 1.1 入口时要求符号已进全局作用域
    const char *szGLNames[] = {"libGL.so.1", "libGL.so", NULL};
    for (int i = 0; szGLNames[i] && !s_hGL; i++)
        s_hGL = dlopen(szGLNames[i], RTLD_LAZY | RTLD_GLOBAL);
    const char *szX11Names[] = {"libX11.so.6", "libX11.so", NULL};
    for (int i = 0; szX11Names[i] && !s_hX11; i++)
        s_hX11 = dlopen(szX11Names[i], RTLD_LAZY | RTLD_GLOBAL);
    if (!s_hGL || !s_hX11)
        return false;

    s_XOpenDisplay = (PFN_XOpenDisplay)dlsym(s_hX11, "XOpenDisplay");
    s_XCloseDisplay = (PFN_XCloseDisplay)dlsym(s_hX11, "XCloseDisplay");
    s_XFree = (PFN_XFree)dlsym(s_hX11, "XFree");
    s_XDefaultScreen = (PFN_XDefaultScreen)dlsym(s_hX11, "XDefaultScreen");
    s_XSetErrorHandler = (PFN_XSetErrorHandler)dlsym(s_hX11, "XSetErrorHandler");
    s_glXChooseFBConfig = (PFN_glXChooseFBConfig)dlsym(s_hGL, "glXChooseFBConfig");
    s_glXCreateNewContext = (PFN_glXCreateNewContext)dlsym(s_hGL, "glXCreateNewContext");
    s_glXCreateContextAttribsARB =
        (PFN_glXCreateContextAttribsARB)dlsym(s_hGL, "glXCreateContextAttribsARB");
    s_glXMakeCurrent = (PFN_glXMakeCurrent)dlsym(s_hGL, "glXMakeCurrent");
    s_glXSwapBuffers = (PFN_glXSwapBuffers)dlsym(s_hGL, "glXSwapBuffers");
    s_glXDestroyContext = (PFN_glXDestroyContext)dlsym(s_hGL, "glXDestroyContext");
    s_glXGetProcAddressARB = (PFN_glXGetProcAddressARB)dlsym(s_hGL, "glXGetProcAddressARB");
    s_glXGetVisualFromFBConfig =
        (PFN_glXGetVisualFromFBConfig)dlsym(s_hGL, "glXGetVisualFromFBConfig");
    s_glXSwapIntervalEXT = (PFN_glXSwapIntervalEXT)dlsym(s_hGL, "glXSwapIntervalEXT");
    s_glXSwapIntervalMESA = (PFN_glXSwapIntervalMESA)dlsym(s_hGL, "glXSwapIntervalMESA");
    s_glXSwapIntervalSGI = (PFN_glXSwapIntervalSGI)dlsym(s_hGL, "glXSwapIntervalSGI");

    // 旧版 libGL 可能只通过 glXGetProcAddress 暴露 ARB 入口，再补一次
    if (!s_glXCreateContextAttribsARB && s_glXGetProcAddressARB)
        s_glXCreateContextAttribsARB =
            (PFN_glXCreateContextAttribsARB)s_glXGetProcAddressARB(
                (const unsigned char *)"glXCreateContextAttribsARB");

    s_bGLXReady = s_XOpenDisplay && s_XCloseDisplay && s_XFree && s_glXChooseFBConfig &&
                  s_glXCreateNewContext && s_glXMakeCurrent && s_glXSwapBuffers && s_glXDestroyContext;
    return s_bGLXReady;
}

/**
 * @brief Xlib 错误回调：记录后返回，绝不终止进程。
 *
 * @details Xlib 的默认处理是打印后 exit()。GLX 的 BadMatch/BadWindow 属于可恢复
 *          错误（窗口未映射就被 MakeCurrent、窗口刚销毁仍在 SwapBuffers 等），
 *          让进程退出会拖垮整个 UI，因此换成"记录 + 返回 0"。
 */
static int SwinxXErrorHandler(Display *dpy __attribute__((unused)), SwinxXErrorEvent *ev)
{
    SLOG_FMTW("X error: code=%d request=%d minor=%d resource=0x%lx", (int)ev->error_code,
              (int)ev->request_code, (int)ev->minor_code, (unsigned long)ev->resourceid);
    return 0;
}

static Display *EnsureXDisplay()
{
    if (!s_pDisplay)
    {
        if (s_XSetErrorHandler)
            s_XSetErrorHandler(SwinxXErrorHandler);
        s_pDisplay = s_XOpenDisplay(NULL);
    }
    return s_pDisplay;
}

/**
 * @brief 按给定属性取 FBConfig 列表（调用方负责 XFree 返回的数组）。
 * @param bWithVisual 是否把 GLX_VISUAL_ID 作为过滤属性（false 时只按 screen/缓冲/位深过滤）
 */
static GLXFBConfig *ChooseFBConfigs(Display *dpy, int screen, VisualID visualId, bool bWithVisual,
                                    int bDoubleBuffer, int nDepthBits, int nStencilBits, int *pnCount)
{
    int attribs[12]; // screen + visual + 双缓冲 + 深度 + 模板 + 终止符，最多 11 项
    int n = 0;
    attribs[n++] = LX_GLX_SCREEN;
    attribs[n++] = screen;
    if (bWithVisual)
    {
        attribs[n++] = LX_GLX_VISUAL_ID;
        attribs[n++] = (int)visualId;
    }
    if (bDoubleBuffer)
    {
        attribs[n++] = LX_GLX_DOUBLEBUFFER;
        attribs[n++] = 1;
    }
    if (nDepthBits > 0)
    {
        attribs[n++] = LX_GLX_DEPTH_SIZE;
        attribs[n++] = nDepthBits;
    }
    if (nStencilBits > 0)
    {
        attribs[n++] = LX_GLX_STENCIL_SIZE;
        attribs[n++] = nStencilBits;
    }
    attribs[n++] = LX_GLX_ATTR_END;

    *pnCount = 0;
    return s_glXChooseFBConfig(dpy, screen, attribs, pnCount);
}

/**
 * @brief 按 Visual + 双缓冲 + 深度/模板位选一次 FBConfig。
 *
 * @details 先让 glXChooseFBConfig 按 GLX_VISUAL_ID 过滤；个别实现不支持该属性时
 *          （直接返回空），再放宽过滤取回候选，用 glXGetVisualFromFBConfig 逐个比对
 *          XVisualInfo.visualid。两条路都按 Visual 精确匹配，避免选出无法和窗口
 *          绑定的配置。
 */
static GLXFBConfig PickFBConfigOnce(Display *dpy, int screen, VisualID visualId, int bDoubleBuffer,
                                    int nDepthBits, int nStencilBits)
{
    int nCfg = 0;
    GLXFBConfig *pCfgs =
        ChooseFBConfigs(dpy, screen, visualId, true, bDoubleBuffer, nDepthBits, nStencilBits, &nCfg);
    if (pCfgs)
    {
        GLXFBConfig cfg = (nCfg > 0) ? pCfgs[0] : NULL;
        s_XFree(pCfgs);
        if (cfg)
            return cfg;
    }

    if (!s_glXGetVisualFromFBConfig)
        return NULL;
    pCfgs = ChooseFBConfigs(dpy, screen, visualId, false, bDoubleBuffer, nDepthBits, nStencilBits, &nCfg);
    if (!pCfgs)
        return NULL;
    GLXFBConfig cfgResult = NULL;
    for (int i = 0; i < nCfg && !cfgResult; i++)
    {
        SwinxXVisualInfo *pVi = s_glXGetVisualFromFBConfig(dpy, pCfgs[i]);
        if (pVi)
        {
            if (pVi->visualid == visualId)
                cfgResult = pCfgs[i];
            s_XFree(pVi);
        }
    }
    s_XFree(pCfgs);
    return cfgResult;
}

/**
 * @brief 挑选与窗口 Visual 同源的 FBConfig。
 * @param bDoubleBuffer 非 0 时要求双缓冲配置
 *
 * @details 深度/模板位是"和 Windows 效果一致"的关键：glXChooseFBConfig 对没有显式
 *          指定的 GLX_DEPTH_SIZE 是按"至少 0 位"处理的，最可能匹配到无深度缓冲的
 *          配置，于是 glEnable(GL_DEPTH_TEST) 形同虚设、立方体面片按绘制顺序互相
 *          覆盖（Windows 的 PFD 请求 24 位深度 + 8 位模板）。这里按
 *          24+8 -> 24 -> 16 -> 不要求 逐级放宽，兼顾新老驱动。
 */
static GLXFBConfig PickFBConfig(Display *dpy, int screen, VisualID visualId, int bDoubleBuffer)
{
    static const int kDepthLadder[4][2] = {{24, 8}, {24, 0}, {16, 0}, {0, 0}};
    for (int i = 0; i < 4; i++)
    {
        GLXFBConfig cfg = PickFBConfigOnce(dpy, screen, visualId, bDoubleBuffer, kDepthLadder[i][0],
                                          kDepthLadder[i][1]);
        if (cfg)
            return cfg;
    }
    return NULL;
}

static void *PlatformCreateCtx(HWND hWnd, int nMajor, int nMinor, int nProfileMask, int nFlags,
                               void *pShareCtx)
{
    if (!EnsureGLX())
    {
        SLOG_FMTE("libGL/libX11 missing or GLX entry points incomplete");
        return NULL;
    }
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wndObj->visualId)
        return NULL;

    // 窗口建立在 xcb 连接上，GLX 走的是另一条 Xlib 连接：先把建窗请求刷到 server，
    // 保证这个 XID 在 GLX 侧已经存在（否则后续 MakeCurrent/SwapBuffers 会 BadWindow）。
    if (wndObj->mConnection)
        wndObj->mConnection->flush();

    Display *dpy = EnsureXDisplay();
    if (!dpy)
        return NULL;

    // 屏幕号取 Xlib 连接的默认屏幕（swinx 与 GLX 连接通常都在 screen 0；多屏时以 GLX
    // 侧为准，拿不到 XDefaultScreen 就退回 0）。FBConfig 必须与该窗口的 Visual 严格匹配，
    // 否则 glXMakeCurrent 必然失败，所以这里只回退双缓冲 -> 单缓冲，不放宽 Visual。
    int screen = s_XDefaultScreen ? s_XDefaultScreen(dpy) : 0;
    GLXFBConfig cfg = PickFBConfig(dpy, screen, (VisualID)wndObj->visualId, 1);
    if (!cfg)
        cfg = PickFBConfig(dpy, screen, (VisualID)wndObj->visualId, 0);
    if (!cfg)
    {
        SLOG_FMTE("no FBConfig for visual 0x%lx (screen %d)", (unsigned long)wndObj->visualId, screen);
        return NULL; // 该窗口 Visual 不支持 GLX
    }

    GLXContext share = pShareCtx ? ((SwinxGLPlatform *)pShareCtx)->ctx : NULL;
    // 只有 2.1 以上的版本、profile 或 flags 才必须走 ARB 入口；legacy 请求保持
    // glXCreateNewContext，与驱动默认行为完全一致。
    bool bRequireAttribs =
        (nMajor > 2) || (nMajor == 2 && nMinor > 1) || nProfileMask != 0 || nFlags != 0;
    GLXContext ctx = NULL;
    if (bRequireAttribs)
    {
        if (!s_glXCreateContextAttribsARB)
        {
            SLOG_FMTE("glXCreateContextAttribsARB unavailable, cannot create %d.%d context", nMajor,
                      nMinor);
            return NULL; // 不悄悄降级到 2.1：调用方要靠失败决定回退策略
        }
        int attribs[10];
        int n = 0;
        attribs[n++] = LX_GLX_CONTEXT_MAJOR_VERSION_ARB;
        attribs[n++] = nMajor;
        attribs[n++] = LX_GLX_CONTEXT_MINOR_VERSION_ARB;
        attribs[n++] = nMinor;
        if (nProfileMask)
        {
            attribs[n++] = LX_GLX_CONTEXT_PROFILE_MASK_ARB;
            attribs[n++] = nProfileMask;
        }
        if (nFlags)
        {
            attribs[n++] = LX_GLX_CONTEXT_FLAGS_ARB;
            attribs[n++] = nFlags;
        }
        attribs[n++] = LX_GLX_ATTR_END;
        ctx = s_glXCreateContextAttribsARB(dpy, cfg, share, 1, attribs);
        if (!ctx)
        {
            SLOG_FMTW("glXCreateContextAttribsARB failed (%d.%d, profile 0x%x)", nMajor, nMinor,
                      nProfileMask);
            return NULL;
        }
    }
    else
    {
        ctx = s_glXCreateNewContext(dpy, cfg, LX_GLX_RGBA_TYPE, share, 1);
        if (!ctx)
        {
            SLOG_FMTE("glXCreateNewContext failed (visual 0x%lx)", (unsigned long)wndObj->visualId);
            return NULL;
        }
    }

    SwinxGLPlatform *pGL = new SwinxGLPlatform();
    pGL->dpy = dpy;
    pGL->ctx = ctx;
    pGL->draw = (GLXDrawable_LX)hWnd;
    pGL->bCurrent = false;
    return pGL;
}

static void PlatformDestroyCtx(void *pCtx)
{
    SwinxGLPlatform *pGL = (SwinxGLPlatform *)pCtx;
    if (!pGL)
        return;
    if (pGL->bCurrent)
        s_glXMakeCurrent(pGL->dpy, 0, NULL);
    s_glXDestroyContext(pGL->dpy, pGL->ctx);
    delete pGL;
}

static BOOL PlatformMakeCurrent(void *pCtx, HWND hWnd)
{
    SwinxGLPlatform *pGL = (SwinxGLPlatform *)pCtx;
    if (!pGL)
        return FALSE;
    GLXDrawable_LX draw = (GLXDrawable_LX)hWnd;
    if (!draw)
        draw = pGL->draw;
    if (!draw)
        return FALSE;
    pGL->draw = draw;
    pGL->bCurrent = (s_glXMakeCurrent(pGL->dpy, draw, pGL->ctx) != 0);
    if (!pGL->bCurrent)
        SLOG_FMTD("glXMakeCurrent failed (drawable 0x%lx), retry on SwapBuffers", (unsigned long)draw);
    return pGL->bCurrent ? TRUE : FALSE;
}

static void PlatformClearCurrent(void *pCtx)
{
    SwinxGLPlatform *pGL = (SwinxGLPlatform *)pCtx;
    if (!pGL || !pGL->bCurrent)
        return;
    s_glXMakeCurrent(pGL->dpy, 0, NULL);
    pGL->bCurrent = false;
}

static BOOL PlatformSwap(void *pCtx)
{
    SwinxGLPlatform *pGL = (SwinxGLPlatform *)pCtx;
    if (!pGL)
        return FALSE;
    if (!pGL->bCurrent)
    {
        // 上一次 MakeCurrent 时窗口可能还没 map，这里重试；成功后本帧才真正上屏
        if (!s_glXMakeCurrent(pGL->dpy, pGL->draw, pGL->ctx))
            return FALSE;
        pGL->bCurrent = true;
    }
    s_glXSwapBuffers(pGL->dpy, pGL->draw);
    return TRUE;
}

static void *PlatformGetProcAddress(const char *pszName)
{
    if (!EnsureGLX())
        return NULL;
    void *p = NULL;
    if (s_glXGetProcAddressARB)
        p = s_glXGetProcAddressARB((const unsigned char *)pszName);
    if (!p)
        p = dlsym(RTLD_DEFAULT, pszName); // GL 1.1 入口由 libGL 正常导出
    return p;
}

static BOOL PlatformSetSwapInterval(void *pCtx, int nInterval)
{
    SwinxGLPlatform *pGL = (SwinxGLPlatform *)pCtx;
    if (!pGL)
        return FALSE;
    if (s_glXSwapIntervalEXT)
    {
        s_glXSwapIntervalEXT(pGL->dpy, pGL->draw, nInterval);
        return TRUE;
    }
    if (s_glXSwapIntervalMESA)
        return s_glXSwapIntervalMESA((unsigned int)nInterval) == 0;
    if (s_glXSwapIntervalSGI)
        return s_glXSwapIntervalSGI(nInterval) == 0;
    return FALSE;
}

static BOOL PlatformSupportsContextAttribs(void)
{
    return (EnsureGLX() && s_glXCreateContextAttribsARB) ? TRUE : FALSE;
}

#endif // __APPLE__

///////////////////////////////////////////////////////////////////////////////
// 公共层：上下文对象
///////////////////////////////////////////////////////////////////////////////

/**
 * @struct SwinxGLCtx
 * @brief  WGL 上下文句柄（HGLRC 即该结构指针）。
 *
 * @details 平台资源延迟到首次 wglMakeCurrent 才创建，目的是让 wglShareLists 生效：
 *          GLX 的 glXCreateNewContext / NSOpenGLContext 都只能在创建时指定
 *          share group，之后无法追加。
 */
struct SwinxGLCtx
{
    void *pPlatform; /**< 平台句柄（macOS: SwinxGLBridge*，Linux: SwinxGLPlatform*） */
    HDC hdc;         /**< 创建该上下文用的 DC */
    HWND hWnd;       /**< 创建该上下文用的窗口 */
    HGLRC hShare;    /**< wglShareLists 记录的共享源 */
    int nMajor;      /**< 请求的主版本（legacy 为 1.0） */
    int nMinor;
    int nProfileMask;
    int nFlags;
    BOOL bCreating; /**< 防共享关系成环时递归 */
    BOOL bCreated;  /**< 平台资源已尝试创建（成功与否都置位，失败不重试） */
};

static SwinxGLCtx *s_pCurCtx = NULL;
static int s_nSwapInterval = 1; /**< 与驱动默认一致：跟随垂直同步 */

/** 确保平台上下文已创建；共享源未创建时先建它。 */
static BOOL EnsureCreated(SwinxGLCtx *pCtx)
{
    if (pCtx->bCreated)
        return pCtx->pPlatform != NULL;
    if (pCtx->bCreating)
        return FALSE; // 共享关系成环
    pCtx->bCreating = TRUE;

    void *pSharePlat = NULL;
    if (pCtx->hShare)
    {
        SwinxGLCtx *pShare = (SwinxGLCtx *)pCtx->hShare;
        if (!EnsureCreated(pShare))
        {
            pCtx->bCreating = FALSE;
            return FALSE;
        }
        pSharePlat = pShare->pPlatform;
    }
    pCtx->pPlatform =
        PlatformCreateCtx(pCtx->hWnd, pCtx->nMajor, pCtx->nMinor, pCtx->nProfileMask, pCtx->nFlags,
                          pSharePlat);
    pCtx->bCreated = TRUE;
    pCtx->bCreating = FALSE;
    return pCtx->pPlatform != NULL;
}

/** 分配上下文对象并记录请求参数（不建平台资源）。 */
static HGLRC CreateCtxObject(HDC hdc, HGLRC hShare, int nMajor, int nMinor, int nProfileMask,
                             int nFlags)
{
    if (!hdc)
        return NULL;
    _SDC *pDC = (_SDC *)hdc;
    HWND hWnd = pDC->hwnd;
    if (!hWnd || !WndMgr::fromHwnd(hWnd))
        return NULL;

    SwinxGLCtx *pCtx = new SwinxGLCtx();
    pCtx->pPlatform = NULL;
    pCtx->hdc = hdc;
    pCtx->hWnd = hWnd;
    pCtx->hShare = hShare;
    pCtx->nMajor = nMajor;
    pCtx->nMinor = nMinor;
    pCtx->nProfileMask = nProfileMask;
    pCtx->nFlags = nFlags;
    pCtx->bCreating = FALSE;
    pCtx->bCreated = FALSE;
    return (HGLRC)pCtx;
}

///////////////////////////////////////////////////////////////////////////////
// 核心入口
///////////////////////////////////////////////////////////////////////////////

HGLRC WINAPI wglCreateContext(HDC hdc)
{
    return CreateCtxObject(hdc, NULL, 1, 0, 0, 0);
}

BOOL WINAPI wglDeleteContext(HGLRC hglrc)
{
    SwinxGLCtx *pCtx = (SwinxGLCtx *)hglrc;
    if (!pCtx)
        return FALSE;
    if (s_pCurCtx == pCtx)
    {
        if (pCtx->pPlatform)
            PlatformClearCurrent(pCtx->pPlatform);
        s_pCurCtx = NULL;
    }
    if (pCtx->pPlatform)
        PlatformDestroyCtx(pCtx->pPlatform);
    delete pCtx;
    return TRUE;
}

BOOL WINAPI wglMakeCurrent(HDC hdc, HGLRC hglrc)
{
    SwinxGLCtx *pCtx = (SwinxGLCtx *)hglrc;
    if (!pCtx)
    {
        // wglMakeCurrent(NULL,NULL) 语义：解除当前上下文
        if (!hdc && s_pCurCtx)
        {
            if (s_pCurCtx->pPlatform)
                PlatformClearCurrent(s_pCurCtx->pPlatform);
            s_pCurCtx = NULL;
            return TRUE;
        }
        return FALSE;
    }
    HWND hWnd = NULL;
    if (hdc)
        hWnd = ((_SDC *)hdc)->hwnd;
    else if (pCtx->hdc)
        hWnd = ((_SDC *)pCtx->hdc)->hwnd;
    if (!hWnd)
        return FALSE;
    if (!EnsureCreated(pCtx))
        return FALSE;
    BOOL bOK = PlatformMakeCurrent(pCtx->pPlatform, hWnd);
    // 即使失败也登记为"当前上下文"：GLX 要求 drawable 已经映射到 server，而 swinx
    // 的窗口要到 ShowWindow 才 map，调用方往往在建窗后立刻建上下文。留到首次
    // SwapBuffers 再补一次，避免整个 GL 渲染因为创建时序而永久失效。
    s_pCurCtx = pCtx;
    return bOK;
}

HDC WINAPI wglGetCurrentDC(void)
{
    return s_pCurCtx ? s_pCurCtx->hdc : NULL;
}

HGLRC WINAPI wglGetCurrentContext(void)
{
    return s_pCurCtx;
}

BOOL WINAPI wglShareLists(HGLRC hglrc1, HGLRC hglrc2)
{
    SwinxGLCtx *pSrc = (SwinxGLCtx *)hglrc1;
    SwinxGLCtx *pDst = (SwinxGLCtx *)hglrc2;
    if (!pSrc || !pDst || pSrc == pDst)
        return FALSE;
    // 平台资源一旦创建就无法再加入 share group（GLX/NSOpenGL 都只支持创建时指定），
    // 此时按 Win32 的失败语义返回 FALSE，让调用方自己决定是否重建上下文。
    if (pDst->bCreated)
        return FALSE;
    pDst->hShare = hglrc1;
    return TRUE;
}

BOOL WINAPI wglCopyContext(HGLRC hglrcSrc __attribute__((unused)),
                           HGLRC hglrcDst __attribute__((unused)), UINT mask __attribute__((unused)))
{
    return FALSE; // 无对应能力
}

HGLRC WINAPI wglCreateLayerContext(HDC hdc __attribute__((unused)),
                                   int iLayerPlane __attribute__((unused)))
{
    return NULL; // 图层平面是 Windows 专有
}

PROC WINAPI wglGetDefaultProcAddress(LPCSTR lpszProc)
{
    return wglGetProcAddress(lpszProc);
}

BOOL WINAPI SwapBuffers(HDC hdc)
{
    // 语义与 WGL 一致：对"当前上下文绑定的 DC"做交换
    if (!s_pCurCtx || (hdc && s_pCurCtx->hdc != hdc))
        return FALSE;
    if (!s_pCurCtx->pPlatform)
        return FALSE;
    return PlatformSwap(s_pCurCtx->pPlatform);
}

DWORD WINAPI wglSwapMultipleBuffers(UINT nBuffers, const WGLSWAP *pswaps)
{
    if (!pswaps)
        return 0;
    DWORD nSwapped = 0;
    for (UINT i = 0; i < nBuffers; i++)
    {
        if (SwapBuffers(pswaps[i].hdc))
            nSwapped++;
    }
    return nSwapped;
}

///////////////////////////////////////////////////////////////////////////////
// WGL 扩展：入口一律经 wglGetProcAddress 取（与 Windows 上用法一致）
///////////////////////////////////////////////////////////////////////////////

static const char *s_szWGLExtBase =
    "WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_EXT_swap_control";
static const char *s_szWGLExtCtxAttribs =
    " WGL_ARB_create_context WGL_ARB_create_context_profile";

/** 扩展字符串按平台实际能力拼装，调用方据此判断是否走 3.x core 路径。 */
static const char *GetWGLExtensions(void)
{
    static char s_szBuf[256];
    static BOOL s_bInit = FALSE;
    if (!s_bInit)
    {
        s_bInit = TRUE;
        strcpy(s_szBuf, s_szWGLExtBase);
        if (PlatformSupportsContextAttribs())
            strcat(s_szBuf, s_szWGLExtCtxAttribs);
    }
    return s_szBuf;
}

static const char *WINAPI wglGetExtensionsStringARB(HDC hdc __attribute__((unused)))
{
    return GetWGLExtensions();
}

static const char *WINAPI wglGetExtensionsStringEXT(void)
{
    return GetWGLExtensions();
}

static BOOL WINAPI wglSwapIntervalEXT(int interval)
{
    if (interval < 0 || !s_pCurCtx || !s_pCurCtx->pPlatform)
        return FALSE;
    if (!PlatformSetSwapInterval(s_pCurCtx->pPlatform, interval))
        return FALSE;
    s_nSwapInterval = interval;
    return TRUE;
}

static int WINAPI wglGetSwapIntervalEXT(void)
{
    return s_nSwapInterval;
}

/**
 * @brief 创建指定版本/profile 的上下文，语义对应 WGL_ARB_create_context。
 *
 * @details 与 wglCreateContext 不同，这里立即创建平台资源并把失败原样返回——调用方
 *          通常靠这个返回值决定"降到 2.1"还是"提示显卡不支持"。属性表以 0 结尾，
 *          WGL_CONTEXT_LAYER_PLANE_ARB 与 robustness 策略在 macOS/Linux 上无对应能力，
 *          直接忽略（不影响版本/profile 的语义）。
 */
static HGLRC WINAPI wglCreateContextAttribsARB(HDC hDC, HGLRC hShareContext, const int *attribList)
{
    int nMajor = 1, nMinor = 0, nProfileMask = 0, nFlags = 0;
    if (attribList)
    {
        for (int i = 0; attribList[i] != 0; i += 2)
        {
            switch (attribList[i])
            {
            case WGL_CONTEXT_MAJOR_VERSION_ARB:
                nMajor = attribList[i + 1];
                break;
            case WGL_CONTEXT_MINOR_VERSION_ARB:
                nMinor = attribList[i + 1];
                break;
            case WGL_CONTEXT_PROFILE_MASK_ARB:
                nProfileMask = attribList[i + 1];
                break;
            case WGL_CONTEXT_FLAGS_ARB:
                nFlags = attribList[i + 1];
                break;
            default:
                break; // 图层平面 / robustness 等：忽略
            }
        }
    }
    if (nMajor < 1 || nMinor < 0)
        return NULL;
    // core 与 compatibility 同时置位是非法组合
    if ((nProfileMask & WGL_CONTEXT_CORE_PROFILE_BIT_ARB) &&
        (nProfileMask & WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB))
        return NULL;

    HGLRC hCtx = CreateCtxObject(hDC, hShareContext, nMajor, nMinor, nProfileMask, nFlags);
    if (!hCtx)
        return NULL;
    if (!EnsureCreated((SwinxGLCtx *)hCtx))
    {
        delete (SwinxGLCtx *)hCtx; // 平台资源没建起来，直接回收
        return NULL;
    }
    return hCtx;
}

static BOOL WINAPI wglMakeContextCurrentARB(HDC hDrawDC, HDC hReadDC, HGLRC hglrc)
{
    // 两个平台的上下文都只有一个 drawable：允许"读写同一窗口"或只给其中一个，
    // 传入不同的读/写 DC 时明确失败——静默拿写窗口当读窗口会读错像素。
    if (hDrawDC && hReadDC && hDrawDC != hReadDC)
        return FALSE;
    return wglMakeCurrent(hDrawDC ? hDrawDC : hReadDC, hglrc);
}

static HDC WINAPI wglGetCurrentReadDCARB(void)
{
    return wglGetCurrentDC();
}

/** WGL 扩展名到实现的映射表；表外的名字交给平台层（glXGetProcAddress/dlsym）。 */
struct SWGLProcEntry
{
    const char *pszName;
    PROC pfn;
};

static const SWGLProcEntry s_wglProcTable[] = {
    {"wglGetExtensionsStringARB", (PROC)wglGetExtensionsStringARB},
    {"wglGetExtensionsStringEXT", (PROC)wglGetExtensionsStringEXT},
    {"wglSwapIntervalEXT", (PROC)wglSwapIntervalEXT},
    {"wglGetSwapIntervalEXT", (PROC)wglGetSwapIntervalEXT},
    {"wglCreateContextAttribsARB", (PROC)wglCreateContextAttribsARB},
    {"wglMakeContextCurrentARB", (PROC)wglMakeContextCurrentARB},
    {"wglGetCurrentReadDCARB", (PROC)wglGetCurrentReadDCARB},
    {NULL, NULL}};

PROC WINAPI wglGetProcAddress(LPCSTR lpszProc)
{
    if (!lpszProc)
        return NULL;
    for (int i = 0; s_wglProcTable[i].pszName; i++)
    {
        if (strcmp(s_wglProcTable[i].pszName, lpszProc) == 0)
            return s_wglProcTable[i].pfn;
    }
    return (PROC)PlatformGetProcAddress(lpszProc);
}

///////////////////////////////////////////////////////////////////////////////
// 像素格式 API（两平台共用）：上下文属性在建上下文时固定按 32/24/8 + 双缓冲申请，
// 这里只需回显同一组值，让按 Win32 写的调用方读到符合实际的结果。
///////////////////////////////////////////////////////////////////////////////

int WINAPI ChoosePixelFormat(HDC hdc __attribute__((unused)),
                             const PIXELFORMATDESCRIPTOR *ppfd __attribute__((unused)))
{
    return 1; // 单一固定格式
}

int WINAPI DescribePixelFormat(HDC hdc __attribute__((unused)), int iPixelFormat __attribute__((unused)),
                               UINT nBytes, PIXELFORMATDESCRIPTOR *ppfd)
{
    if (!ppfd || nBytes < sizeof(PIXELFORMATDESCRIPTOR))
        return 0;
    memset(ppfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
    ppfd->nSize = sizeof(PIXELFORMATDESCRIPTOR);
    ppfd->nVersion = 1;
    ppfd->dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    ppfd->iPixelType = PFD_TYPE_RGBA;
    ppfd->cColorBits = 32;
    ppfd->cDepthBits = 24;
    ppfd->cStencilBits = 8;
    ppfd->iLayerType = PFD_MAIN_PLANE;
    return 1;
}

BOOL WINAPI SetPixelFormat(HDC hdc __attribute__((unused)), int iPixelFormat __attribute__((unused)),
                           const PIXELFORMATDESCRIPTOR *ppfd __attribute__((unused)))
{
    return TRUE; // 像素格式已在建上下文时确定，这里无需动作
}

///////////////////////////////////////////////////////////////////////////////
// 无对应能力的入口：语义上"明确失败"，不伪造成功
///////////////////////////////////////////////////////////////////////////////

BOOL WINAPI wglDescribeLayerPlane(HDC hdc __attribute__((unused)), int iPixelFormat __attribute__((unused)),
                                  int iLayerPlane __attribute__((unused)), UINT nBytes __attribute__((unused)),
                                  LPLAYERPLANEDESCRIPTOR plpd __attribute__((unused)))
{
    return FALSE;
}

int WINAPI wglSetLayerPaletteEntries(HDC hdc __attribute__((unused)), int iLayerPlane __attribute__((unused)),
                                     int iStart __attribute__((unused)), int cEntries __attribute__((unused)),
                                     const COLORREF *pcr __attribute__((unused)))
{
    return 0;
}

int WINAPI wglGetLayerPaletteEntries(HDC hdc __attribute__((unused)), int iLayerPlane __attribute__((unused)),
                                     int iStart __attribute__((unused)), int cEntries __attribute__((unused)),
                                     COLORREF *pcr __attribute__((unused)))
{
    return 0;
}

BOOL WINAPI wglRealizeLayerPalette(HDC hdc __attribute__((unused)), int iLayerPlane __attribute__((unused)),
                                   BOOL bRealize __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI wglSwapLayerBuffers(HDC hdc __attribute__((unused)), UINT fuPlanes __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI wglUseFontBitmapsA(HDC hdc __attribute__((unused)), DWORD first __attribute__((unused)),
                               DWORD count __attribute__((unused)), DWORD listBase __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI wglUseFontBitmapsW(HDC hdc __attribute__((unused)), DWORD first __attribute__((unused)),
                               DWORD count __attribute__((unused)), DWORD listBase __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI wglUseFontOutlinesA(HDC hdc __attribute__((unused)), DWORD first __attribute__((unused)),
                                DWORD count __attribute__((unused)), DWORD listBase __attribute__((unused)),
                                FLOAT deviation __attribute__((unused)), FLOAT extrusion __attribute__((unused)),
                                int format __attribute__((unused)),
                                LPGLYPHMETRICSFLOAT lpgmf __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI wglUseFontOutlinesW(HDC hdc __attribute__((unused)), DWORD first __attribute__((unused)),
                                DWORD count __attribute__((unused)), DWORD listBase __attribute__((unused)),
                                FLOAT deviation __attribute__((unused)), FLOAT extrusion __attribute__((unused)),
                                int format __attribute__((unused)),
                                LPGLYPHMETRICSFLOAT lpgmf __attribute__((unused)))
{
    return FALSE;
}
