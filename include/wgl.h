/**
 * @file       wgl.h
 * @brief      非 Windows 平台的 WGL 兼容层：把 OpenGL 上下文管理映射到平台原生 API。
 *
 * @details    Windows 上 WGL 由系统 wingdi/opengl32 提供，本头文件不参与编译；
 *             在 macOS/Linux 上按 Win32 语义提供同一组入口，让按 WGL 写的
 *             代码（真实窗口 + 像素格式 + 上下文 + SwapBuffers）无需改动即可编译：
 *
 *             - macOS   : NSOpenGLContext 绑定到 HWND（即 SNsWindow NSView）。
 *             - Linux   : GLX 绑定到 HWND（即 X11 窗口）。
 *
 *             语义映射约定：
 *             - ChoosePixelFormat/SetPixelFormat 恒成功（返回 1/TRUE）：
 *               这两个 API 只在 Windows 上参与像素格式选择，macOS/Linux 的
 *               上下文属性（颜色/深度/模板位数、双缓冲）固定按 32/24/8 + 双缓冲
 *               创建，DescribePixelFormat 返回同样的值供调用方回显。
 *             - wglCreateContext 用的 HDC 必须来自 GetDC(hwnd)，即 swinx 窗口
 *               自己的 DC（_SDC::hwnd 指回窗口）；上下文与该窗口绑定。
 *             - 默认上下文为 legacy profile：桌面 OpenGL 2.1（macOS 上限），
 *               覆盖固定管线与 GLSL+VBO 两类用法；需要 3.x core 时走
 *               wglCreateContextAttribsARB（见下）。
 *
 *             能力矩阵：
 *             - 完整实现：核心上下文管理、wglShareLists、像素格式、SwapBuffers，
 *               以及 WGL_ARB_create_context(_profile)、WGL_EXT_swap_control、
 *               WGL_ARB/EXT_extensions_string（入口经 wglGetProcAddress 取）。
 *             - 受限实现：wglMakeContextCurrentARB/wglGetCurrentReadDCARB——两个
 *               平台的上下文只有一个 drawable，读写同一窗口时可用；传入不同的
 *               读/写 DC 时明确失败，而不是静默把写窗口当读窗口。
 *             - 明确降级：分层平面（wgl*LayerPlane/Palette/Overlay）与
 *               wglUseFontBitmaps/Outlines——这几组依赖 GDI 字形与图层平面，
 *               macOS/Linux 无对应能力，恒返回失败而不是伪造成功。
 */

#ifndef __SWINX_WGL_H__
#define __SWINX_WGL_H__

#include <windows.h>

#ifndef APIENTRY
#define APIENTRY
#endif

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#if defined(__has_include)
#if __has_include(<OpenGL/glext.h>)
#include <OpenGL/glext.h> // 常量与 PFN 类型；Legacy profile 下 2.1 全量可用
#endif
#endif
#elif defined(__ANDROID__)
/* Android NDK 只提供 GLES（<GLES/gl.h>、<GLES2/gl2.h>），没有桌面 GL 头，
 * 也没有可以绑定 GL 上下文的窗口表面 API。所以这里**不引入**系统 GL 头：
 * WGL 是桌面 GL 的窗口表面协议，在 Android 上恒为"能力缺失"——
 * wgl* 入口仍然存在（见 src/wgl.cpp 的 dlopen 分支，取不到 libGL 即返回
 * 失败），但调用方不该指望它能用。保留本头可编译，是为了让
 * "只包含 <wgl.h> 但不碰 GL"的跨平台代码不在移动端直接爆掉。 */
#else
// 取系统 GL 头（GL 1.1 声明与 GLenum/GLuint 等基础类型）。用 include_next
// 跳过本目录的 GL/gl.h 兼容头，避免自包含（系统 gl.h 末尾自带 glext.h）。
#include_next <GL/gl.h>
#endif

#ifndef __SWINX_WGL_TYPES__
#define __SWINX_WGL_TYPES__

typedef void *HGLRC;

/* 系统头不提供的 PIXELFORMATDESCRIPTOR：字段取调用方真正读写的子集 */
#ifndef PFD_TYPE_RGBA
#define PFD_TYPE_RGBA       0
#define PFD_TYPE_COLORINDEX 1
#endif

#ifndef PFD_MAIN_PLANE
#define PFD_MAIN_PLANE     0
#define PFD_OVERLAY_PLANE  1
#define PFD_UNDERLAY_PLANE 2
#endif

#ifndef PFD_DOUBLEBUFFER
#define PFD_DOUBLEBUFFER          0x00000001
#define PFD_STEREO                0x00000002
#define PFD_DRAW_TO_WINDOW        0x00000004
#define PFD_DRAW_TO_BITMAP        0x00000008
#define PFD_SUPPORT_GDI           0x00000010
#define PFD_SUPPORT_OPENGL        0x00000020
#define PFD_GENERIC_FORMAT        0x00000040
#define PFD_NEED_PALETTE          0x00000080
#define PFD_NEED_SYSTEM_PALETTE   0x00000100
#define PFD_SWAP_EXCHANGE         0x00000200
#define PFD_SWAP_COPY             0x00000400
#define PFD_SWAP_LAYER_BUFFERS    0x00000800
#define PFD_GENERIC_ACCELERATED   0x00001000
#define PFD_SUPPORT_DIRECTDRAW    0x00002000
#define PFD_DIRECT3D_AVAILABLE    0x00004000
#define PFD_SUPPORT_COMPOSITION   0x00008000
#define PFD_DEPTH_DONTCARE        0x20000000
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000
#define PFD_STEREO_DONTCARE       0x80000000
#endif

/* 图层平面与 GDI 字形相关的常量：同样由 Windows 的 wingdi.h 提供，这里补齐是为了让
 * 按 Windows 写的调用方（wglSwapLayerBuffers / wglUseFontOutlines*）能直接编译；
 * 本层对这两组能力一律返回失败（见文件头能力矩阵）。 */
#ifndef WGL_SWAP_MAIN_PLANE
#define WGL_SWAP_MAIN_PLANE 0x00000001
#define WGL_SWAP_OVERLAY1   0x00000002
#define WGL_SWAP_UNDERLAY1  0x00010000
#endif

#ifndef WGL_FONT_LINES
#define WGL_FONT_LINES    0
#define WGL_FONT_POLYGONS 1
#endif

typedef struct tagPIXELFORMATDESCRIPTOR
{
    WORD nSize;
    WORD nVersion;
    DWORD dwFlags;
    BYTE iPixelType;
    BYTE cColorBits;
    BYTE cRedBits;
    BYTE cRedShift;
    BYTE cGreenBits;
    BYTE cGreenShift;
    BYTE cBlueBits;
    BYTE cBlueShift;
    BYTE cAlphaBits;
    BYTE cAlphaShift;
    BYTE cAccumBits;
    BYTE cAccumRedBits;
    BYTE cAccumGreenBits;
    BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits;
    BYTE cDepthBits;
    BYTE cStencilBits;
    BYTE cAuxBuffers;
    BYTE iLayerType;
    BYTE bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR, FAR *LPPIXELFORMATDESCRIPTOR;

/* 图层平面与 GDI 字形的描述结构：swinx 不做图层/GDI 字形，仅为源码兼容而定义，
   布局与 wingdi.h 一致（对应入口恒返回失败，调用方不会读到有意义的内容）。 */
typedef struct tagLAYERPLANEDESCRIPTOR
{
    WORD nSize;
    WORD nVersion;
    DWORD dwFlags;
    BYTE iPixelType;
    BYTE cColorBits;
    BYTE cRedBits;
    BYTE cRedShift;
    BYTE cGreenBits;
    BYTE cGreenShift;
    BYTE cBlueBits;
    BYTE cBlueShift;
    BYTE cAlphaBits;
    BYTE cAlphaShift;
    BYTE cAccumBits;
    BYTE cAccumRedBits;
    BYTE cAccumGreenBits;
    BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits;
    BYTE cDepthBits;
    BYTE cStencilBits;
    BYTE cAuxBuffers;
    BYTE iLayerPlane;
    BYTE bReserved;
    COLORREF crTransparent;
} LAYERPLANEDESCRIPTOR, *PLAYERPLANEDESCRIPTOR, FAR *LPLAYERPLANEDESCRIPTOR;

typedef struct _POINTFLOAT
{
    FLOAT x;
    FLOAT y;
} POINTFLOAT, *PPOINTFLOAT, FAR *LPPOINTFLOAT;

typedef struct _GLYPHMETRICSFLOAT
{
    FLOAT gmfBlackBoxX;
    FLOAT gmfBlackBoxY;
    POINTFLOAT gmfptGlyphOrigin;
    FLOAT gmfCellIncX;
    FLOAT gmfCellIncY;
} GLYPHMETRICSFLOAT, *PGLYPHMETRICSFLOAT, FAR *LPGLYPHMETRICSFLOAT;

/** wglSwapMultipleBuffers 的每一项（同 wingdi.h）。 */
typedef struct _WGLSWAP
{
    HDC hdc;
    UINT uiFlags;
} WGLSWAP, *PWGLSWAP, FAR *LPWGLSWAP;

#endif // __SWINX_WGL_TYPES__

#include <GL/wglext.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int WINAPI ChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd);
    int WINAPI DescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, PIXELFORMATDESCRIPTOR *ppfd);
    BOOL WINAPI SetPixelFormat(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd);

    HGLRC WINAPI wglCreateContext(HDC hdc);
    BOOL WINAPI wglDeleteContext(HGLRC hglrc);
    BOOL WINAPI wglMakeCurrent(HDC hdc, HGLRC hglrc);
    HDC WINAPI wglGetCurrentDC(void);
    HGLRC WINAPI wglGetCurrentContext(void);
    PROC WINAPI wglGetProcAddress(LPCSTR lpszProc);
    PROC WINAPI wglGetDefaultProcAddress(LPCSTR lpszProc);

    /** 使 hglrc2 共享 hglrc1 的资源；hglrc2 必须在首次 wglMakeCurrent 之前调用。 */
    BOOL WINAPI wglShareLists(HGLRC hglrc1, HGLRC hglrc2);
    /** 本层无上下文拷贝能力，恒失败。 */
    BOOL WINAPI wglCopyContext(HGLRC hglrcSrc, HGLRC hglrcDst, UINT mask);
    /** 图层平面上下文是 Windows 专有，本层恒返回 NULL。 */
    HGLRC WINAPI wglCreateLayerContext(HDC hdc, int iLayerPlane);

    BOOL WINAPI SwapBuffers(HDC hdc);
    /** 批量交换：返回成功交换的缓冲区个数（逐项等价 SwapBuffers）。 */
    DWORD WINAPI wglSwapMultipleBuffers(UINT nBuffers, const WGLSWAP *pswaps);

    /* 图层平面与 GDI 字形：macOS/Linux 没有对应能力，恒返回失败（见文件头能力矩阵）。 */
    BOOL WINAPI wglDescribeLayerPlane(HDC hdc, int iPixelFormat, int iLayerPlane, UINT nBytes, LPLAYERPLANEDESCRIPTOR plpd);
    int WINAPI wglSetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, const COLORREF *pcr);
    int WINAPI wglGetLayerPaletteEntries(HDC hdc, int iLayerPlane, int iStart, int cEntries, COLORREF *pcr);
    BOOL WINAPI wglRealizeLayerPalette(HDC hdc, int iLayerPlane, BOOL bRealize);
    BOOL WINAPI wglSwapLayerBuffers(HDC hdc, UINT fuPlanes);

    BOOL WINAPI wglUseFontBitmapsA(HDC hdc, DWORD first, DWORD count, DWORD listBase);
    BOOL WINAPI wglUseFontBitmapsW(HDC hdc, DWORD first, DWORD count, DWORD listBase);
    BOOL WINAPI wglUseFontOutlinesA(HDC hdc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT lpgmf);
    BOOL WINAPI wglUseFontOutlinesW(HDC hdc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT lpgmf);

#ifdef UNICODE
#define wglUseFontBitmaps  wglUseFontBitmapsW
#define wglUseFontOutlines wglUseFontOutlinesW
#else
#define wglUseFontBitmaps  wglUseFontBitmapsA
#define wglUseFontOutlines wglUseFontOutlinesA
#endif

#ifdef __cplusplus
}
#endif

#endif // __SWINX_WGL_H__
