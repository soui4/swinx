/*
 * wndplatform.h -- Wayland 后端专用的窗口私有数据
 *
 * _Window 只按后端选型内嵌一个 WndPlatformData，公共代码（src/wnd.cpp 等）
 * 不再直接持有 CSD 装饰窗句柄。字段由平台层填充：
 *   - OnWindowCreate 填内容窗的 plat.frame（见 SConnection.cpp）
 *   - wlframewnd.cpp 填装饰窗的 plat.frameContent
 * 销毁时由 OnWindowDestroy 清零。
 *
 */
#ifndef _SWINX_WL_WNDPLATFORM_H_
#define _SWINX_WL_WNDPLATFORM_H_

#include <windows.h>

struct WndPlatformData
{
    HWND frame;        /* 本窗的 CSD 装饰窗；非 0 表示 surface 拓扑由平台接管 */
    HWND frameContent; /* 装饰窗包裹的内容窗（仅装饰窗自己非 0） */
};

#endif // _SWINX_WL_WNDPLATFORM_H_
