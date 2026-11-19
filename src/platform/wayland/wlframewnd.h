/*
 * wlframewnd.h -- Wayland 客户端装饰窗（CSD）
 *
 * xdg-shell 没有服务端装饰（xdg-decoration 的 SERVER_SIDE 在 GNOME/mutter 上
 * 会被强制回 CLIENT_SIDE），所以带 WS_CAPTION 的窗口不会像 X11 那样由窗口
 * 管理器给出标题栏。X11 的做法是 WM 把应用窗 reparent 进一个装饰窗；这里用
 * 同样的模型在客户端模拟：
 *
 *   装饰窗（frame，本文件的类，有独立画布，画标题栏 + 关闭按钮）
 *     └─ 内容窗（应用自己的 HWND，wl_surface 是 frame surface 的 subsurface）
 *
 * 内容窗的 Win32 语义完全不改（dwStyle / parent / rc 都不动），所以 SOUI 的
 * NC 绘制、命中测试、CenterWindow 等都保持原样；只有 Wayland 的 surface 拓扑
 * 由平台层接管：frame 是唯一的 xdg_toplevel（进任务列表的就是它），内容窗是
 * 它的 subsurface，定位于 (0, titleH)。
 *
 */
#ifndef _SWINX_WL_FRAMEWND_H_
#define _SWINX_WL_FRAMEWND_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 标题栏高度（逻辑像素）。内容窗的 y 偏移、frame 的尺寸都按它算。 */
int SwinxWlFrameTitleHeight(void);
/* 按内容窗 DPI 缩放后的标题栏高度（96dpi 下等于 SwinxWlFrameTitleHeight）。 */
int SwinxWlFrameTitleHeightFor(HWND hContent);

/* 是否需要为这个窗口补装饰窗。dwStyle 必须是 CreateWindow 传进来的**原始**
 * 样式：wnd.cpp 在调用 OnWindowCreate 之前已经把 WS_CAPTION 里的 WS_BORDER
 * 位清掉了，用 _Window::dwStyle 判断会漏。 */
BOOL SwinxWlFrameShouldDecorate(DWORD dwStyle, DWORD dwExStyle, HWND hParent);

/* 创建装饰窗。x/y/w/h 是**内容窗**的初始矩形（屏幕坐标），frame 自己的矩形
 * 是 (x, y-titleH, x+w, y+h)。成功后 frame 的 _Window::plat.frameContent 指向
 * hContent，内容窗自己把 _Window::plat.frame 填上。 */
HWND SwinxWlFrameCreate(HWND hContent, LPCSTR lpszTitle,
                        int x, int y, int w, int h);

/* 销毁装饰窗（只拆装饰，不动内容窗）。 */
void SwinxWlFrameDestroy(HWND hFrame);

/* 同步 frame 矩形（x/y/w/h = 内容窗矩形，w/h 不含标题栏）。 */
void SwinxWlFrameSetRect(HWND hFrame, int x, int y, int w, int h);

/* 只改位置 / 只改尺寸。
 * wnd.cpp 的 WM_WINDOWPOSCHANGED 把"移动"和"改尺寸"拆成两次平台回调
 * （SConnection::SetWindowPos / SetWindowSize），每一次只带一半几何；若两边都
 * 走 SetRect，后一次就会拿 rc 里还没更新的另一半把前一次的结果覆盖掉
 * （移动后又被写回旧坐标）。所以这两个入口各自只动自己那一边。 */
void SwinxWlFrameMove(HWND hFrame, int x, int y);
void SwinxWlFrameResize(HWND hFrame, int w, int h);

/* 标题变化：重画标题栏 + 同步合成器标题（任务列表/概览里显示的就是它）。 */
void SwinxWlFrameSetTitle(HWND hFrame, LPCSTR lpszTitle);

/* frame 被装饰的内容窗（没有则 0）。 */
HWND SwinxWlFrameContent(HWND hFrame);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_WL_FRAMEWND_H_
