/*
 * wndplatform.h -- X11 后端专用的窗口私有数据
 *
 * _Window 只按后端选型内嵌一个 WndPlatformData，公共代码（src/wnd.cpp 等）
 * 不再直接持有 visual/cmap 这类只在 X11 有意义的字段。字段由平台层在
 * SConnection::OnWindowCreate 里填充、在 OnWindowDestroy 里释放。
 *
 */
#ifndef _SWINX_X11_WNDPLATFORM_H_
#define _SWINX_X11_WNDPLATFORM_H_

#include <stdint.h>

struct WndPlatformData
{
    uint32_t visualId; /* 本窗创建时选中的 X visual；建 cairo/xcb surface、选 GLX FBConfig 用 */
    uint32_t cmap;     /* 本窗的 X colormap，窗口销毁时释放 */
};

#endif // _SWINX_X11_WNDPLATFORM_H_
