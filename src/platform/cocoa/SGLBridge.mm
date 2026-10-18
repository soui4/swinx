/**
 * @file       SGLBridge.mm
 * @brief      swinx/src/wgl.cpp 的 macOS 落地：NSOpenGLContext 桥接。
 *
 * @details    swinx 在 macOS 上 HWND 即 SNsWindow（NSView 派生）指针，因此
 *             “WGL 上下文绑窗口”直接映射为 NSOpenGLContext setView:。
 *             - 像素格式固定：32bit RGBA / 24bit 深度 / 8bit 模板 / 双缓冲，
 *               与 wgl.cpp 的 DescribePixelFormat 回显一致。
 *             - profile 由调用方指定：0=legacy(2.1) / 0x3200=3.2 core /
 *               0x4100=4.1 core（对应 NSOpenGLProfileVersion* 的取值）。
 *             - 窗口几何变化后需 [NSOpenGLContext update] 才能正确重投影，
 *               这里在每次 makeCurrent 时对比视图尺寸，变了就 update。
 *             - AppKit 里一张视图只有"一张当前 GL 表面"，第二个上下文对同一视图
 *               setView: 会把它顶掉，而前者的 ctx.view 仍指回该视图，看不出异常。
 *               所以视图表面的归属要显式记账（见 SwinxBindView / SwinxUnbindView）：
 *               创建上下文时不绑视图，makeCurrent 时抢占，销毁时把新的栈顶绑回去。
 *             - wglGetProcAddress 用 dlsym 从 OpenGL framework 取入口：
 *               桌面 GL 2.1 的全部入口（含 GLSL/VBO）都是该 framework 直连
 *               导出的符号，无需扩展加载机制。
 */

#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/OpenGL.h>
#import <dlfcn.h>

#include <objc/objc.h>
#include <windows.h>
#include "SNsWindow.h"
#include "log.h"

#include <string.h>

#define kLogTag "SGLBridge"

struct SwinxGLBridge
{
    NSOpenGLContext *ctx;
    int nViewW;
    int nViewH;
    BOOL bNeedsRebind; /**< 表面被别的上下文顶掉/释放过，下一次 makeCurrent 要重新 setView: */
};

///////////////////////////////////////////////////////////////////////////////
// 视图表面的归属记账
//
// AppKit 的 GL 画面挂在 NSView 上，一张视图只有"一张当前表面"。第二个
// NSOpenGLContext 对同一视图 setView: 会把前一个的表面顶掉，但前一个的
// ctx.view 仍然指回该视图，看起来"还绑着"——只比较 ctx.view 是发现不了的。
// 如果此时把临时上下文直接 clearDrawable 掉，视图的表面被释放，原上下文之后
// 的绘制全部落空（画面停留在窗口服务器的旧像素上，一旦窗口需要重画就变空白）。
//
// 因此按视图维护一个绑定栈，栈顶 = 当前拥有表面的上下文：
//   - makeCurrent：把本上下文压到栈顶；不在栈顶就重新 setView: 夺回表面；
//   - 销毁：先摘出栈，clearDrawable 之后把新的栈顶重新绑上去。
// 这样临时建/删上下文的代码（例如 WGL 入口测试）不会打掉主上下文的表面。
// 栈以 NSView 为弱键存放：视图销毁时条目自动消失，不会留下悬空指针。
///////////////////////////////////////////////////////////////////////////////
static NSMapTable *SwinxViewStacks(void)
{
    static NSMapTable *s_stacks = nil;
    if (!s_stacks)
        s_stacks = [NSMapTable weakToStrongObjectsMapTable];
    return s_stacks;
}

/** 取某视图的绑定栈；bCreate 为假时不存在就返回 nil。 */
static NSMutableArray *SwinxStackOf(NSView *view, BOOL bCreate)
{
    if (!view)
        return nil;
    NSMapTable *stacks = SwinxViewStacks();
    NSMutableArray *arr = [stacks objectForKey:view];
    if (!arr && bCreate)
    {
        arr = [NSMutableArray array];
        [stacks setObject:arr forKey:view];
    }
    return arr;
}

/** 上下文在栈里的位置；不在栈里返回 -1。 */
static NSInteger SwinxStackIndex(NSMutableArray *arr, SwinxGLBridge *pBridge)
{
    NSUInteger n = arr ? arr.count : 0;
    for (NSUInteger i = 0; i < n; ++i)
    {
        if ([[arr objectAtIndex:i] pointerValue] == (void *)pBridge)
            return (NSInteger)i;
    }
    return -1;
}

/** 把 view 的 GL 表面交给本上下文（必要时重新 setView: 夺回来）。 */
static void SwinxBindView(NSView *view, SwinxGLBridge *pBridge)
{
    if (!view || !pBridge || !pBridge->ctx)
        return;

    // 常见路径（渲染循环每帧都会 makeCurrent）：本上下文已经是该视图的表面拥有者，
    // 什么都不用做，避免每帧遍历视图表。
    NSMutableArray *pCur = SwinxStackOf(view, NO);
    if (pCur && [[pCur lastObject] pointerValue] == (void *)pBridge && pBridge->ctx.view == view &&
        !pBridge->bNeedsRebind)
        return;

    // 一个上下文只属于一张视图：换视图时先从旧视图的栈里摘掉
    NSMapTable *stacks = SwinxViewStacks();
    for (NSView *key in stacks)
    {
        if (key == view)
            continue;
        NSMutableArray *pOld = [stacks objectForKey:key];
        NSInteger nOld = SwinxStackIndex(pOld, pBridge);
        if (nOld >= 0)
            [pOld removeObjectAtIndex:(NSUInteger)nOld];
    }

    NSMutableArray *arr = SwinxStackOf(view, YES);
    BOOL bWasTop = (arr.count > 0 && [[arr lastObject] pointerValue] == (void *)pBridge);
    NSInteger nIdx = SwinxStackIndex(arr, pBridge);
    if (nIdx >= 0)
        [arr removeObjectAtIndex:(NSUInteger)nIdx];
    [arr addObject:[NSValue valueWithPointer:pBridge]];

    if (!bWasTop || pBridge->bNeedsRebind || pBridge->ctx.view != view)
    {
        if (!bWasTop && arr.count > 1)
            SLOG_STMD() << "SGLBridge: GL surface taken over by another context, rebind";
        [pBridge->ctx setView:view];
        pBridge->bNeedsRebind = FALSE;
        pBridge->nViewW = -1; // 表面是新的，下一次 makeCurrent 必须 update 重投影
        pBridge->nViewH = -1;
    }
}

/** 销毁前解绑：摘出栈、释放自己的 drawable，再把新的栈顶重新绑回该视图。 */
static void SwinxUnbindView(SwinxGLBridge *pBridge)
{
    if (!pBridge || !pBridge->ctx)
        return;

    // 不依赖 ctx.view 找栈：表面被其它上下文顶掉后它可能已经不可信
    NSMapTable *stacks = SwinxViewStacks();
    NSView *view = nil;
    NSMutableArray *arr = nil;
    for (NSView *key in stacks)
    {
        NSMutableArray *pArr = [stacks objectForKey:key];
        NSInteger nIdx = SwinxStackIndex(pArr, pBridge);
        if (nIdx >= 0)
        {
            view = key;
            arr = pArr;
            [arr removeObjectAtIndex:(NSUInteger)nIdx];
            break;
        }
    }

    [pBridge->ctx clearDrawable];

    if (!view || !arr)
        return;
    if (arr.count > 0)
    {
        // clearDrawable 释放的是这张视图上的 GL 表面，同视图的其它上下文会跟着失效
        // （即使它们的 ctx.view 指针没变），所以栈顶必须重新 setView: + update。
        SwinxGLBridge *pTop = (SwinxGLBridge *)[[arr lastObject] pointerValue];
        if (pTop && pTop->ctx)
        {
            SLOG_STMD() << "SGLBridge: context destroyed, rebind remaining GL surface";
            pTop->nViewW = -1;
            pTop->nViewH = -1;
            if ([NSOpenGLContext currentContext] == pTop->ctx)
            {
                // 它正是本线程的当前上下文：不在 current 期间改 drawable，
                // 标记出来由下一次 makeCurrent 重新 setView: 并 update
                pTop->bNeedsRebind = TRUE;
            }
            else
            {
                [pTop->ctx setView:view];
                pTop->bNeedsRebind = FALSE;
            }
        }
    }
    else
    {
        [stacks removeObjectForKey:view];
    }
}

extern "C" {

void *swinxGLCreate(HWND hWnd, int nProfile, void *pShareCtx)
{
    @autoreleasepool {
        if (!IsNsWindow(hWnd))
            return NULL;
        NSView *view = (__bridge NSView *)(void *)hWnd;
        if (!view)
            return NULL;

        NSOpenGLPixelFormatAttribute profile = NSOpenGLProfileVersionLegacy;
        if (nProfile == 0x3200)
            profile = NSOpenGLProfileVersion3_2Core;
        else if (nProfile == 0x4100)
            profile = NSOpenGLProfileVersion4_1Core;

        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, profile,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAColorSize, 32,
            // 显式申请 alpha 通道：view 默认 isOpaque=NO，AppKit 按 per-pixel
            // alpha 合成；不带 alpha 的 surface 写入的 alpha=1 会被丢弃，
            // 呈现时 alpha 取 surface 初值 0，整个 GL 区域合成后全透明。
            NSOpenGLPFAAlphaSize, 8,
            NSOpenGLPFADepthSize, 24,
            NSOpenGLPFAStencilSize, 8,
            0
        };
        NSOpenGLPixelFormat *fmt = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!fmt)
            return NULL;
        NSOpenGLContext *shareCtx = nil;
        if (pShareCtx)
            shareCtx = ((SwinxGLBridge *)pShareCtx)->ctx;
        NSOpenGLContext *ctx = [[NSOpenGLContext alloc] initWithFormat:fmt shareContext:shareCtx];
        if (!ctx)
            return NULL;

        // 这里既不绑视图也不设为当前上下文：swinx/wgl.cpp 把"创建"与"绑定"拆开，
        // 一是让 wglShareLists 能在两者之间生效（share group 只能在初始化时指定），
        // 二是避免临时上下文一建出来就抢走本视图的 GL 表面（见 SwinxBindView）。
        // 视图与几何在首次 makeCurrent 时绑定并 update。

        // 标记该 view 为 GL 直绘窗口：SNsWindow 的 isOpaque 据此返回 YES，
        // 阻止 AppKit 在窗口失效时把 GL 区域擦成透明背景（见 setNsWindowGLDirect）。
        setNsWindowGLDirect(hWnd, TRUE);

        SwinxGLBridge *pBridge = new SwinxGLBridge();
        pBridge->ctx = ctx;
        pBridge->nViewW = -1; // -1 保证首次 makeCurrent 一定 update 一次
        pBridge->nViewH = -1;
        pBridge->bNeedsRebind = FALSE;
        return pBridge;
    }
}

void swinxGLDestroy(void *pHandle)
{
    if (!pHandle)
        return;
    SwinxGLBridge *pBridge = (SwinxGLBridge *)pHandle;
    @autoreleasepool {
        // 先解绑（可能要把表面交还给仍在使用的其它上下文），再释放上下文本身
        SwinxUnbindView(pBridge);
        pBridge->ctx = nil;
    }
    delete pBridge;
}

BOOL swinxGLMakeCurrent(void *pHandle, HWND hWnd)
{
    SwinxGLBridge *pBridge = (SwinxGLBridge *)pHandle;
    if (!pBridge || !pBridge->ctx)
        return FALSE;
    @autoreleasepool {
        NSView *view = hWnd ? (__bridge NSView *)(void *)hWnd : pBridge->ctx.view;
        if (!view)
            return FALSE;
        // 把该视图的表面交给本上下文；被别的上下文顶掉过就重新 setView: 夺回来
        SwinxBindView(view, pBridge);
        int nW = (int)view.bounds.size.width;
        int nH = (int)view.bounds.size.height;
        if (nW != pBridge->nViewW || nH != pBridge->nViewH)
        {
            // 视图还没上窗口时 drawable 还不存在，update 无意义；保留 -1 让下一次补上
            if (view.window)
            {
                pBridge->nViewW = nW;
                pBridge->nViewH = nH;
                [pBridge->ctx update]; // 视图几何变化或表面重绑后必须 update 重投影
            }
        }
        [pBridge->ctx makeCurrentContext];
        return TRUE;
    }
}

void swinxGLClearCurrent(void *pHandle)
{
    SwinxGLBridge *pBridge = (SwinxGLBridge *)pHandle;
    if (!pBridge || !pBridge->ctx)
        return;
    @autoreleasepool {
        [NSOpenGLContext clearCurrentContext];
    }
}

BOOL swinxGLSwap(void *pHandle)
{
    SwinxGLBridge *pBridge = (SwinxGLBridge *)pHandle;
    if (!pBridge || !pBridge->ctx)
        return FALSE;
    @autoreleasepool {
        [pBridge->ctx flushBuffer];
        return TRUE;
    }
}

BOOL swinxGLSetSwapInterval(void *pHandle, int nInterval)
{
    SwinxGLBridge *pBridge = (SwinxGLBridge *)pHandle;
    if (!pBridge || !pBridge->ctx)
        return FALSE;
    @autoreleasepool {
        GLint nValue = (GLint)nInterval;
        [pBridge->ctx setValues:&nValue forParameter:NSOpenGLCPSwapInterval];
        return TRUE;
    }
}

void *swinxGLGetProcAddress(const char *pszName)
{
    if (!pszName)
        return NULL;
    static void *s_hGL = NULL;
    if (!s_hGL)
    {
        s_hGL = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",
                       RTLD_LAZY | RTLD_GLOBAL);
        if (!s_hGL)
            s_hGL = RTLD_DEFAULT;
    }
    return dlsym(s_hGL, pszName);
}

} // extern "C"
