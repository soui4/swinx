/**
 * @file       GL/gl.h
 * @brief      swinx 的 <GL/gl.h> 兼容头：转发平台 GL 头，并带出 WGL 兼容声明。
 *
 * @details    按 WGL 写的代码通常只包含 <windows.h> + <GL/gl.h>，WGL 入口由
 *             windows.h 里的 wingdi.h 带出；macOS/Linux 没有这套东西，所以这里
 *             转发平台 GL 头之后拉入 <wgl.h>——HGLRC、PIXELFORMATDESCRIPTOR、
 *             ChoosePixelFormat、wgl*、SwapBuffers 的唯一声明处都在那里，
 *             本文件不再保留第二份，避免两处不一致。
 */

#ifndef __SWINX_GL_H__
#define __SWINX_GL_H__

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(__ANDROID__)
/* Android 是 GLES-only：桌面 GL 开发头（GL/gl.h、GL/glext.h）在 NDK 里根本
 * 不存在。这里转发到 <GLES/gl.h>，让按桌面 GL 类型/常量写的声明式代码仍能
 * 编译；但 GLES1 不含立即模式（glBegin/glEnd），真调用的地方会在编译期
 * 报"未声明"，这比假装能跑更诚实。 */
#if defined(__has_include)
#if __has_include(<GLES/gl.h>)
#include <GLES/gl.h>
#endif
#endif
#else
// include_next 跳过本文件自己，取系统 GL 头（GL 1.1 声明与基础类型）；
// 缺 GL 开发头（libgl1-mesa-dev）时这里会报找不到文件。
#include_next <GL/gl.h>
#endif

#include <wgl.h>

#endif // __SWINX_GL_H__
