/**
 * @file       GL/wglext.h
 * @brief      WGL 扩展的常量与入口指针类型（Khronos wglext.h 的兼容子集）。
 *
 * @details    Windows SDK 不提供 wglext.h，跨平台代码通常自带一份；非 Windows
 *             上由 swinx 提供同名头，让 `#include <GL/wglext.h>` 三平台都能解析。
 *             这里只列 swinx 真正实现（或明确降级）的扩展，避免调用方按扩展
 *             字符串误判能力。
 *
 *             扩展入口不导出成普通函数，一律用 wglGetProcAddress 取地址——
 *             与 Windows 上的用法完全一致，源码无需平台分支。
 *
 *             约定：属性/位掩码的数值取自 Khronos 官方头文件，与 GLX 侧同名
 *             常量一致（WGL_CONTEXT_MAJOR_VERSION_ARB == GLX_CONTEXT_MAJOR_VERSION_ARB），
 *             因此实现里可以直接透传给 GLX。
 */

#ifndef __SWINX_WGLEXT_H__
#define __SWINX_WGLEXT_H__

#include <windows.h>

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef WINAPI
#define WINAPI
#endif

/*--------- WGL_ARB_create_context ---------*/
#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#endif
#ifndef WGL_CONTEXT_MINOR_VERSION_ARB
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#endif
#ifndef WGL_CONTEXT_LAYER_PLANE_ARB
#define WGL_CONTEXT_LAYER_PLANE_ARB 0x2093
#endif
#ifndef WGL_CONTEXT_FLAGS_ARB
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#endif
#ifndef WGL_CONTEXT_PROFILE_MASK_ARB
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#endif
#ifndef WGL_CONTEXT_DEBUG_BIT_ARB
#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
#endif
#ifndef WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#endif
#ifndef WGL_CONTEXT_CORE_PROFILE_BIT_ARB
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif
#ifndef WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

/*--------- WGL_ARB_create_context_robustness ---------*/
#ifndef WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB
#define WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB 0x00000004
#endif
#ifndef WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB
#define WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256
#endif
#ifndef WGL_LOSE_CONTEXT_ON_RESET_ARB
#define WGL_LOSE_CONTEXT_ON_RESET_ARB 0x8252
#endif
#ifndef WGL_NO_RESET_NOTIFICATION_ARB
#define WGL_NO_RESET_NOTIFICATION_ARB 0x8261
#endif

#ifndef __SWINX_WGL_TYPES__
/* 单独包含本头（未经由 wgl.h）时补齐句柄类型 */
typedef void *HGLRC;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef const char *(WINAPI *PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hdc);
typedef const char *(WINAPI *PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
typedef BOOL(WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);
typedef int(WINAPI *PFNWGLGETSWAPINTERVALEXTPROC)(void);
typedef HGLRC(WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC hDC, HGLRC hShareContext,
                                                         const int *attribList);
typedef BOOL(WINAPI *PFNWGLMAKECONTEXTCURRENTARBPROC)(HDC hDrawDC, HDC hReadDC, HGLRC hglrc);
typedef HDC(WINAPI *PFNWGLGETCURRENTREADDCARBPROC)(void);

#ifdef __cplusplus
}
#endif

#endif // __SWINX_WGLEXT_H__
