/*
 * wlframewnd.cpp -- Wayland 客户端装饰窗（CSD）实现
 *
 * 见 wlframewnd.h 的模型说明。这里只做三件事：
 *   1. 一个普通 swinx 窗口（有自己的 cairo 画布）当装饰窗，画标题栏 + 关闭按钮；
 *   2. NC 命中测试把标题区判成 HTCAPTION，于是 wnd.cpp 的 HandleNcTestCode 会
 *      走 beginWlDrag() → xdg_toplevel_move（frame 才是 toplevel）；
 *   3. 关闭按钮 / 合成器的 close 事件都转成内容窗的 WM_CLOSE——应用自己的
 *      关闭流程（模态循环、确认框）不受影响。
 *
 */
#include "wlframewnd.h"
#include "wndobj.h"
#include "framebuffer.h"

#include <map>
#include <string>
#include <string.h>
#include <stdio.h>

#define kLogTag "wlframe"

static const char *kWlFrameClass = "SWINX_WL_FRAME";
/* 30px 接近 GTK/Adwaita headerbar 的高度，和标题栏按钮的点击区域一起定死，
 * 避免不同 DPI 下内容窗偏移和绘制对不上（SOUI 的缩放由布局层负责，这里不做
 * 二次缩放）。 */
static const int kWlFrameTitleH = 30;       // 96dpi 基准
static const int kWlFrameBorder = 6;        // 边缘 resize 命中宽度
static const int kWlFrameBtnSize = 16;      // 标题栏按钮边长（96dpi 基准）

namespace {

struct FrameInfo
{
    HWND content;
    std::string title;
    BOOL closeDown;  // 关闭按钮按下态（松开时在按钮内才关窗）
    BOOL closeHot;   // 悬停态
    BOOL minDown, minHot;
    BOOL maxDown, maxHot;
};

std::map<HWND, FrameInfo> s_frames;

FrameInfo *frameInfoOf(HWND hFrame)
{
    std::map<HWND, FrameInfo>::iterator it = s_frames.find(hFrame);
    return it == s_frames.end() ? NULL : &it->second;
}

int frameTitleH(HWND hFrame)
{
    FrameInfo *fi = frameInfoOf(hFrame);
    UINT dpi = GetDpiForWindow(fi ? fi->content : 0);
    if (!dpi)
        dpi = 96;
    int h = MulDiv(kWlFrameTitleH, (int)dpi, 96);
    return h < 20 ? 20 : h;
}

int frameBtnSize(HWND hFrame)
{
    UINT dpi = GetDpiForWindow(hFrame);
    if (!dpi)
        dpi = 96;
    int n = MulDiv(kWlFrameBtnSize, (int)dpi, 96);
    return n < 12 ? 12 : n;
}

RECT frameCloseRect(HWND hFrame)
{
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hFrame, &rc);
    const int nSize = frameBtnSize(hFrame) + 6;
    RECT btn;
    btn.left = rc.right - 9 - nSize;
    btn.top = (frameTitleH(hFrame) - nSize) / 2;
    btn.right = rc.right - 9;
    btn.bottom = btn.top + nSize;
    if (btn.left < 0)
        btn.left = 0;
    return btn;
}

RECT frameMaxRect(HWND hFrame)
{
    RECT rcClose = frameCloseRect(hFrame);
    int gap = frameBtnSize(hFrame) + 4;
    RECT rc = rcClose;
    rc.left -= gap;
    rc.right -= gap;
    return rc;
}

RECT frameMinRect(HWND hFrame)
{
    RECT rcMax = frameMaxRect(hFrame);
    int gap = frameBtnSize(hFrame) + 4;
    RECT rc = rcMax;
    rc.left -= gap;
    rc.right -= gap;
    return rc;
}

void framePaint(HWND hFrame, FrameInfo *fi)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hFrame, &ps);
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hFrame, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
    {
        EndPaint(hFrame, &ps);
        return;
    }
    /* 激活态：frame 自己就是 xdg_toplevel，合成器聚焦的是它，所以
     * GetActiveWindow()/GetForegroundWindow() 都能反映真实状态。 */
    BOOL bActive = (GetActiveWindow() == hFrame) || (GetForegroundWindow() == hFrame);

    /* 整块填底：cairo 画布是 ARGB32，一个像素都不留空才能保证 alpha 全是 0xFF，
     * 否则合成器按 premultiplied 透明处理，标题栏会变成半透明/黑洞。 */
    HBRUSH br = CreateSolidBrush(bActive ? RGB(0xED, 0xED, 0xED) : RGB(0xF6, 0xF5, 0xF4));
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    /* 标题栏下沿分隔线 */
    int titleH = frameTitleH(hFrame);
    RECT rcSep = { rc.left, titleH - 1, rc.right, titleH };
    HBRUSH brSep = CreateSolidBrush(RGB(0xCC, 0xCC, 0xCC));
    FillRect(hdc, &rcSep, brSep);
    DeleteObject(brSep);

    /* 最小化 / 最大化 / 关闭：三个按钮共用一套悬停/按下底色。 */
    RECT rcMax = frameMaxRect(hFrame);
    RECT rcMin = frameMinRect(hFrame);
    if (fi && (fi->minDown || fi->minHot))
    {
        HBRUSH br = CreateSolidBrush(fi->minDown ? RGB(0xC8, 0xC8, 0xC8) : RGB(0xDC, 0xDC, 0xDC));
        FillRect(hdc, &rcMin, br);
        DeleteObject(br);
    }
    if (fi && (fi->maxDown || fi->maxHot))
    {
        HBRUSH br = CreateSolidBrush(fi->maxDown ? RGB(0xC8, 0xC8, 0xC8) : RGB(0xDC, 0xDC, 0xDC));
        FillRect(hdc, &rcMax, br);
        DeleteObject(br);
    }
    HPEN hGlyph = CreatePen(PS_SOLID, 1, bActive ? RGB(0x4A, 0x4A, 0x4A) : RGB(0x9A, 0x9A, 0x9A));
    HPEN hOldGlyph = (HPEN)SelectObject(hdc, hGlyph);
    int n = frameBtnSize(hFrame) / 2;
    int mx = (rcMin.left + rcMin.right) / 2;
    int my = (rcMin.top + rcMin.bottom) / 2;
    MoveToEx(hdc, mx - n, my, NULL); LineTo(hdc, mx + n, my);
    unsigned int st = SwinxWlSurfaceGetState(SwinxWlSurfaceGet(hFrame));
    int xx = (rcMax.left + rcMax.right) / 2;
    int yy = (rcMax.top + rcMax.bottom) / 2;
    if (st & (SWINX_WL_TOPSTATE_MAXIMIZED | SWINX_WL_TOPSTATE_FULLSCREEN))
    {
        Rectangle(hdc, xx - n - 2, yy - n + 2, xx + n - 2, yy + n + 2);
        MoveToEx(hdc, xx - n, yy - n, NULL); LineTo(hdc, xx + n, yy - n);
        LineTo(hdc, xx + n, yy + n); LineTo(hdc, xx - n, yy + n); LineTo(hdc, xx - n, yy - n);
    }
    else
        Rectangle(hdc, xx - n, yy - n, xx + n, yy + n);
    SelectObject(hdc, hOldGlyph);
    DeleteObject(hGlyph);

    /* 关闭按钮：悬停/按下给一层底，再画一个 × */
    RECT rcBtn = frameCloseRect(hFrame);
    if (fi && (fi->closeDown || fi->closeHot))
    {
        HBRUSH brHot = CreateSolidBrush(fi->closeDown ? RGB(0xC8, 0xC8, 0xC8) : RGB(0xDC, 0xDC, 0xDC));
        FillRect(hdc, &rcBtn, brHot);
        DeleteObject(brHot);
    }
    HPEN pen = CreatePen(PS_SOLID, 1, bActive ? RGB(0x4A, 0x4A, 0x4A) : RGB(0x9A, 0x9A, 0x9A));
    HPEN hOldPen = (HPEN)SelectObject(hdc, pen);
    int cx = (rcBtn.left + rcBtn.right) / 2;
    int cy = (rcBtn.top + rcBtn.bottom) / 2;
    const int r = 5;
    MoveToEx(hdc, cx - r, cy - r, NULL);
    LineTo(hdc, cx + r + 1, cy + r + 1);
    MoveToEx(hdc, cx + r, cy - r, NULL);
    LineTo(hdc, cx - r - 1, cy + r + 1);
    SelectObject(hdc, hOldPen);
    DeleteObject(pen);

    if (fi && !fi->title.empty())
    {
        RECT rcText = rc;
        rcText.left += 12;
        rcText.right = rcBtn.left - 8;
        rcText.bottom = frameTitleH(hFrame);
        if (rcText.right > rcText.left)
        {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
            int nOldBk = SetBkMode(hdc, TRANSPARENT);
            COLORREF crOld = SetTextColor(hdc, bActive ? RGB(0x2E, 0x34, 0x36) : RGB(0x9A, 0x9A, 0x9A));
            DrawTextA(hdc, fi->title.c_str(), -1, &rcText,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(hdc, crOld);
            SetBkMode(hdc, nOldBk);
            SelectObject(hdc, hOldFont);
        }
    }
    EndPaint(hFrame, &ps);
}

void frameForwardClose(HWND hFrame)
{
    FrameInfo *fi = frameInfoOf(hFrame);
    if (!fi || !fi->content)
        return;
    if (!IsWindow(fi->content))
        return;
    /* 用 Post 而不是 Send：内容窗可能处在自己的模态循环里（swinx 的
     * MessageBox、SOUI 的模态对话框），投递消息才能被那个循环取到。 */
    PostMessageA(fi->content, WM_CLOSE, 0, 0);
}

void frameSysCommand(HWND hFrame, int cmd)
{
    SwinxWlSurface *s = SwinxWlSurfaceGet(hFrame);
    if (!s)
        return;
    unsigned int st = SwinxWlSurfaceGetState(s);
    if (cmd == 1) // 最小化
        SwinxWlSurfaceSetState(s, SWINX_WL_TOPSTATE_MINIMIZED);
    else if (cmd == 2) // 最大化 / 还原
    {
        if (st & (SWINX_WL_TOPSTATE_MAXIMIZED | SWINX_WL_TOPSTATE_FULLSCREEN))
            SwinxWlSurfaceSetState(s, 0);
        else
            SwinxWlSurfaceSetState(s, SWINX_WL_TOPSTATE_MAXIMIZED);
    }
}

static BOOL frameRectHit(RECT rc, POINT pt)
{
    // 返回 RECT 的函数结果是临时量，直接取地址在 GCC 下报错
    // (taking address of rvalue)，先落到按值形参上即可。
    return PtInRect(&rc, pt);
}

LRESULT CALLBACK SwinxWlFrameProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FrameInfo *fi = frameInfoOf(hWnd);
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; // 全部在 WM_PAINT 里画，避免先刷一遍底色再覆盖
    case WM_PAINT:
        framePaint(hWnd, fi);
        return 0;
    case WM_NCPAINT:
        return 0; // 没有传统 NC 区，WM_PAINT 之后的补 NC 绘制直接吞掉
    case WM_NCHITTEST:
    {
        /* lParam 的坐标口径：本端口里 GetCursorPos() 保存的是**当前指针所在
         * surface 的局部坐标**（SConnection::SetCursorPos 直接吃 wl_pointer 的
         * surface-local 值），而 wnd.cpp 在按下时正是用 GetCursorPos 的结果调
         * WM_NCHITTEST。所以这里不能再用 ScreenToClient 减 rc（toplevel 的 rc
         * 已被归一到 (0,0)，减了没意义；popup 的 rc 是屏幕坐标，减了反而错）。 */
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rcBtn = frameCloseRect(hWnd);
        RECT rcMax = frameMaxRect(hWnd);
        RECT rcMin = frameMinRect(hWnd);
        if (PtInRect(&rcBtn, pt) || PtInRect(&rcMax, pt) || PtInRect(&rcMin, pt))
            return HTCLIENT; // 三个按钮走普通客户区消息，下面自己判定
        int titleH = frameTitleH(hWnd);
        if (pt.y >= 0 && pt.y < titleH)
            return HTCAPTION; // 标题区：wnd.cpp 会转成 xdg_toplevel_move

        /* 边缘/角落缩放：内容窗是 frame 的 subsurface，默认会吃掉所有边缘
         * 指针事件；frameCreate/frameApply 已把内容窗的输入区内缩 kWlFrameBorder，
         * 所以这里的边缘命中真的能收到。只有 toplevel 能发 xdg_toplevel_resize。 */
        SwinxWlSurface *s = SwinxWlSurfaceGet(hWnd);
        if (!s || SwinxWlSurfaceGetRole(s) != SWINX_WL_SURF_TOPLEVEL)
            return HTCLIENT; // popup 不参与 resize
        RECT rc;
        GetClientRect(hWnd, &rc);
        const int b = kWlFrameBorder;
        bool left = pt.x < b, right = pt.x >= rc.right - b;
        bool top = pt.y < b, bottom = pt.y >= rc.bottom - b;
        if (top && left)   return HTTOPLEFT;
        if (top && right)  return HTTOPRIGHT;
        if (bottom && left)  return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left)   return HTLEFT;
        if (right)  return HTRIGHT;
        if (top)    return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_MOUSEMOVE:
    {
        if (!fi)
            break;
        /* 悬停态靠 WM_MOUSELEAVE 复位（进入窗口时登记一次即可，wnd.cpp 在
         * 指针离开窗口时投递）。 */
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        BOOL hot = frameRectHit(frameCloseRect(hWnd), pt);
        BOOL maxHot = frameRectHit(frameMaxRect(hWnd), pt);
        BOOL minHot = frameRectHit(frameMinRect(hWnd), pt);
        if (hot != fi->closeHot || maxHot != fi->maxHot || minHot != fi->minHot)
        {
            fi->closeHot = hot;
            fi->maxHot = maxHot;
            fi->minHot = minHot;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (fi && (fi->closeHot || fi->maxHot || fi->minHot))
        {
            fi->closeHot = fi->maxHot = fi->minHot = FALSE;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        break;
    case WM_LBUTTONDOWN:
    {
        if (!fi)
            break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        BOOL changed = FALSE;
        fi->closeDown = frameRectHit(frameCloseRect(hWnd), pt);
        fi->maxDown = frameRectHit(frameMaxRect(hWnd), pt);
        fi->minDown = frameRectHit(frameMinRect(hWnd), pt);
        changed = fi->closeDown || fi->maxDown || fi->minDown;
        if (changed)
            InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!fi || !(fi->closeDown || fi->maxDown || fi->minDown))
            break;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        BOOL doClose = fi->closeDown && frameRectHit(frameCloseRect(hWnd), pt);
        BOOL doMax = fi->maxDown && frameRectHit(frameMaxRect(hWnd), pt);
        BOOL doMin = fi->minDown && frameRectHit(frameMinRect(hWnd), pt);
        fi->closeDown = fi->maxDown = fi->minDown = FALSE;
        InvalidateRect(hWnd, NULL, TRUE);
        if (doClose)
            frameForwardClose(hWnd);
        else if (doMax)
            frameSysCommand(hWnd, 2);
        else if (doMin)
            frameSysCommand(hWnd, 1);
        return 0;
    }
    case WM_RBUTTONUP:
        /* 标题栏右键给系统菜单是 Windows 习惯，这里没有系统菜单：
         * 吞掉，避免冒到内容窗上弹应用自己的上下文菜单。 */
        return 0;
    case WM_CLOSE:
        /* 绝不能落到 DefWindowProc（那里会 DestroyWindow(frame)）：关闭请求
         * 归内容窗，应用自己决定是否真的关。 */
        frameForwardClose(hWnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hWnd, msg, wp, lp);
}

} // namespace

int SwinxWlFrameTitleHeight(void)
{
    return kWlFrameTitleH;
}

int SwinxWlFrameTitleHeightFor(HWND hContent)
{
    UINT dpi = GetDpiForWindow(hContent);
    if (!dpi)
        dpi = 96;
    int h = MulDiv(kWlFrameTitleH, (int)dpi, 96);
    return h < 20 ? 20 : h;
}

BOOL SwinxWlFrameShouldDecorate(DWORD dwStyle, DWORD dwExStyle, HWND hParent)
{
    if (hParent == HWND_MESSAGE)
        return FALSE; // 消息窗没有像素
    if (dwStyle & WS_CHILD)
        return FALSE; // 子控件跟着父窗走
    if ((dwStyle & WS_CAPTION) != WS_CAPTION)
        return FALSE; // 只要带标题栏的窗口（WS_CAPTION = WS_BORDER|WS_DLGFRAME）
    if (dwExStyle & WS_EX_TOOLWINDOW)
        return FALSE; // tooltip / 菜单走 xdg_popup
    if (dwExStyle & WS_EX_COMPOSITED)
        return FALSE; // 半透明宿主没法被不透明装饰窗包住
    return TRUE;
}

HWND SwinxWlFrameCreate(HWND hContent, LPCSTR lpszTitle,
                        int x, int y, int w, int h)
{
    static BOOL s_registered = FALSE;
    if (!s_registered)
    {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = 0;
        wc.lpfnWndProc = SwinxWlFrameProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hIcon = NULL;
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = kWlFrameClass;
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            fprintf(stderr, "[wlframe] RegisterClassExA failed, err=%d\n", (int)GetLastError());
            return 0;
        }
        s_registered = TRUE;
    }
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    FrameInfo fi;
    fi.content = hContent;
    fi.title = lpszTitle ? lpszTitle : "";
    fi.closeDown = FALSE;
    fi.closeHot = FALSE;
    fi.minDown = fi.minHot = FALSE;
    fi.maxDown = fi.maxHot = FALSE;

    HWND hFrame = CreateWindowExA(0, kWlFrameClass, fi.title.c_str(), WS_POPUP,
                                  x, y - SwinxWlFrameTitleHeightFor(hContent),
                                  w, h + SwinxWlFrameTitleHeightFor(hContent),
                                  0, 0, GetModuleHandleA(NULL), 0);
    if (!hFrame)
    {
        fprintf(stderr, "[wlframe] create frame failed for content=%p\n", hContent);
        return 0;
    }
    s_frames[hFrame] = fi;
    WndObj fw = WndMgr::fromHwnd(hFrame);
    if (fw)
        fw->plat.frameContent = hContent;
    SwinxWlSurfaceSetTitle(SwinxWlSurfaceGet(hFrame), fi.title.c_str());
    return hFrame;
}

void SwinxWlFrameDestroy(HWND hFrame)
{
    if (!hFrame)
        return;
    FrameInfo *fi = frameInfoOf(hFrame);
    if (fi)
        fi->content = 0; // 断开：销毁 frame 不该再往内容窗发消息
    if (IsWindow(hFrame))
        DestroyWindow(hFrame);
    s_frames.erase(hFrame);
}

/* 唯一改 frame 矩形的地方。x/y 是**内容窗**坐标，frame 自己是
 * (x, y-titleH, x+w, y+h)。
 *
 * 有一类 surface 的位置不由我们决定：frame 自身是 xdg_toplevel 时，坐标归
 * 合成器独占（用户拖边框、平铺、maximize），而且它的 rc 原点会被
 * normalizeWlToplevelOrigin() 归一到 (0,0)。这时只同步尺寸，别把 x/y 写回
 * rc，否则每次移动都会制造一次 rc 与归一之间的来回 SetWindowPos。 */
static void frameApply(HWND hFrame, int x, int y, int w, int h)
{
    FrameInfo *fi = frameInfoOf(hFrame);
    if (fi)
    {
        /* 让出四边：指针落在边缘时命中装饰窗，wnd.cpp 才会把 HTLEFT 等
         * 转成 xdg_toplevel_resize。内容窗仍然在这些像素上照常绘制。 */
        SwinxWlSurfaceSetInputInset(SwinxWlSurfaceGet(fi->content),
                                    kWlFrameBorder, 0, kWlFrameBorder, kWlFrameBorder);
    }
    WndObj fw = WndMgr::fromHwnd(hFrame);
    if (!fw)
        return;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    SwinxWlSurface *wl = SwinxWlSurfaceGet(hFrame);
    bool posOwned = wl && SwinxWlSurfaceGetRole(wl) == SWINX_WL_SURF_TOPLEVEL;

    int titleH = frameTitleH(hFrame);
    RECT nr = fw->rc;
    if (posOwned)
    {
        /* 尺寸仍然是我们自己的：frame surface 里既装标题栏（顶部 titleH）又装
         * 内容 subsurface（定位于 (0, titleH)），所以 frame 高度恒为
         * 内容高 + titleH。漏掉这个加法会让 frame 比应用请求的矮 30px，接着
         * onWlConfigure() 又按 cy-titleH 转发给内容窗 ⇒ 内容再矮 30px，
         * 表现为「每个窗口都比 SetWindowPos 要的少一条标题栏的高度」。 */
        nr.right = nr.left + w;
        nr.bottom = nr.top + h + titleH;
    }
    else
    {
        nr.left = x;
        nr.top = y - titleH;
        nr.right = x + w;
        nr.bottom = y + h;
    }
    if (fw->rc.left == nr.left && fw->rc.top == nr.top &&
        fw->rc.right == nr.right && fw->rc.bottom == nr.bottom)
        return;
    SetWindowPos(hFrame, NULL, nr.left, nr.top,
                 nr.right - nr.left, nr.bottom - nr.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void SwinxWlFrameSetRect(HWND hFrame, int x, int y, int w, int h)
{
    if (!hFrame)
        return;
    frameApply(hFrame, x, y, w, h);
}

void SwinxWlFrameMove(HWND hFrame, int x, int y)
{
    if (!hFrame)
        return;
    WndObj fw = WndMgr::fromHwnd(hFrame);
    if (!fw)
        return;
    /* 尺寸取**内容窗**的宽高（这一刻它只改了位置，宽高仍是上一帧的最终值），
     * 不用 frame 自己的 rc 反推：frame 只是内容窗的容器，一旦它的宽度在某个
     * 时序里被写成过偏差值（例如内容窗 cairo 画布还没按新尺寸重建时 SetWindowPos
     * 先到），从自身反推会把那个偏差永久固化，表现为窗口右/下边缘多出一条
     * 属于 frame、不属于内容 surface 的窄带——指针走到那里就会被判成"离开
     * 内容窗"，光标提前换成别的形状，而画面几乎看不出差别。 */
    HWND hContent = SwinxWlFrameContent(hFrame);
    WndObj cw = WndMgr::fromHwnd(hContent);
    int w = fw->rc.right - fw->rc.left;
    int h = fw->rc.bottom - fw->rc.top - frameTitleH(hFrame);
    if (cw && cw->rc.right > cw->rc.left && cw->rc.bottom > cw->rc.top)
    {
        w = cw->rc.right - cw->rc.left;
        h = cw->rc.bottom - cw->rc.top;
    }
    frameApply(hFrame, x, y, w, h);
}

void SwinxWlFrameResize(HWND hFrame, int w, int h)
{
    if (!hFrame)
        return;
    WndObj fw = WndMgr::fromHwnd(hFrame);
    if (!fw)
        return;
    /* 同理：原点取 frame 自己的（内含 y-titleH 的偏移），不碰内容窗的 rc。 */
    frameApply(hFrame, fw->rc.left, fw->rc.top + frameTitleH(hFrame), w, h);
}

void SwinxWlFrameSetTitle(HWND hFrame, LPCSTR lpszTitle)
{
    if (!hFrame || !lpszTitle)
        return;
    FrameInfo *fi = frameInfoOf(hFrame);
    if (!fi)
        return;
    if (fi->title == lpszTitle)
        return;
    fi->title = lpszTitle;
    SwinxWlSurfaceSetTitle(SwinxWlSurfaceGet(hFrame), fi->title.c_str());
    InvalidateRect(hFrame, NULL, TRUE);
}

HWND SwinxWlFrameContent(HWND hFrame)
{
    FrameInfo *fi = frameInfoOf(hFrame);
    return fi ? fi->content : 0;
}
