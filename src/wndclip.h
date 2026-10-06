#ifndef _SWINX_WND_CLIP_H_
#define _SWINX_WND_CLIP_H_

/* WS_CHILD 窗口的客户区裁剪规则
 *
 * Win32 的文档契约：The system clips a child window so that it cannot appear
 * outside the client area of its parent. 也就是说，子窗口的**可见区与输入区**
 * 恒等于
 *     「子窗口矩形 ∩ 直接父窗口客户区 ∩ 更上层各祖先的客户区」
 *
 * swinx 的每个 HWND 都是一扇真实原生窗口（X11 子窗口 / Cocoa 的 NSView 子视图），
 * 而原生窗口系统并不提供这条裁剪——父窗口画在自己边缘的滚动条会被子窗口的绘制
 * 盖住，落在那块区域的鼠标消息也只会命中子窗口。于是"父窗口有滚动条且子窗口覆盖
 * 该区域"的布局就同时坏在绘制与输入两侧。
 *
 * 本文件提供这条规则的实现，由 wnd.cpp 在窗口几何/样式变化时调用，结果经平台已有
 * 的 SetWindowRgn 落地（X11 走 XShape，macOS 走 CAShapeLayer 蒙版）。因为用的是
 * 平台现成通路，调用方不需要任何平台 #ifdef。
 *
 * 坐标约定：与 swinx 的 GetClientRect 一致——客户区左上角与窗口左上角重合，只在
 * 右边/下边减去 SM_CXEDGE / SM_CYEDGE 与滚动条宽度。祖先链的换算利用
 * GetWindowRect（它已经把逐层父窗口原点累加，返回根坐标系下的矩形）。
 */

#include <windows.h>

/* 纯几何：把"直接父坐标系下的子窗口矩形"裁到"同一坐标系下的父客户区"，再把结果
 * 换算到子窗口自身的坐标系（左上角归零）。
 *
 * prcChild / prcClient 都是父坐标系；prcOut 是子窗口坐标系。
 * 返回 FALSE 表示裁剪后为空，即子窗口完全落在父客户区之外（Win32 下既不可见、
 * 也不接收消息），此时 prcOut 被置为空矩形。 */
inline BOOL SbClipChildToClient(const RECT *prcChild, const RECT *prcClient, RECT *prcOut)
{
    RECT rc;
    if (!IntersectRect(&rc, prcChild, prcClient))
    {
        if (prcOut)
            SetRectEmpty(prcOut);
        return FALSE;
    }
    OffsetRect(&rc, -prcChild->left, -prcChild->top);
    if (prcOut)
        *prcOut = rc;
    return TRUE;
}

/* 取窗口自身坐标系下的可行区域（= 上面那条规则的最终结果）。
 *
 * 顶层窗口的"父"其实是它的 owner，其客户区不构成裁剪依据，因此祖先链在遇到非
 * WS_CHILD 窗口时终止——这正是 Win32 的行为（owner 不裁剪被拥有的窗口）。
 *
 * 返回 FALSE 表示该窗口完全落在祖先客户区之外，prcClip 被置为空矩形。 */
inline BOOL SbGetWindowClientClip(HWND hWnd, RECT *prcClip)
{
    if (prcClip)
        SetRectEmpty(prcClip);
    if (!hWnd)
        return FALSE;

    RECT rcWnd; /* 根坐标系 */
    if (!GetWindowRect(hWnd, &rcWnd))
        return FALSE;

    RECT rcAllowed = rcWnd;
    HWND hCur = hWnd;
    for (int nDepth = 0; nDepth < 64; ++nDepth)
    {
        if (!(GetWindowLongA(hCur, GWL_STYLE) & WS_CHILD))
            break; /* 顶层窗口：不再往上裁剪 */
        HWND hParent = GetParent(hCur);
        if (!hParent)
            break;
        RECT rcParent, rcClient;
        if (!GetWindowRect(hParent, &rcParent) || !GetClientRect(hParent, &rcClient))
            break;
        OffsetRect(&rcClient, rcParent.left, rcParent.top); /* 父客户区 → 根坐标系 */
        RECT rcNext;
        if (!IntersectRect(&rcNext, &rcAllowed, &rcClient))
        {
            if (prcClip)
                SetRectEmpty(prcClip);
            return FALSE;
        }
        rcAllowed = rcNext;
        hCur = hParent;
    }

    OffsetRect(&rcAllowed, -rcWnd.left, -rcWnd.top); /* 根坐标系 → 窗口自身坐标系 */
    if (prcClip)
        *prcClip = rcAllowed;
    return TRUE;
}

/* 期望的区域形态——纯判定，便于单元测试覆盖状态迁移。
 *
 * 返回 FALSE 表示"不需要区域（不裁剪）"；TRUE 表示需要 *prcWant 这块矩形：
 *   · 窗口自身没有面积（0 尺寸，或还没被摆到真实几何）：本来就不可见，不要为此去动
 *     平台。这一档很关键，见下面 SbNeedPushClipRgn 里关于"下发有副作用"的说明；
 *     demos/uieditor 的真实子窗口正是 0 尺寸建窗的。
 *   · 整窗落在祖先客户区内：无需裁剪。
 *   · 完全落在祖先客户区之外：给一块零宽矩形——Win32 下既不可见也不收消息，而"空
 *     区域"与"无区域"在不同平台上的表达不一致，零宽矩形没有这个歧义。
 *   · 部分重叠：给可行区域本身。
 *
 * prcWnd          本窗口矩形（窗口自身坐标系）
 * bClipped/prcClip SbGetWindowClientClip 的结果（窗口自身坐标系）
 */
inline BOOL SbCalcClipWant(const RECT *prcWnd, BOOL bClipped, const RECT *prcClip, RECT *prcWant)
{
    if (prcWant)
        SetRectEmpty(prcWant);
    if (!prcWnd || IsRectEmpty(prcWnd))
        return FALSE; /* 0 尺寸窗口本来就不可见：不要为了裁剪去动平台 */
    if (!bClipped)
    {
        if (prcWant)
            SetRect(prcWant, 0, 0, 0, 1); /* 空区域：零宽矩形，没有"空/无"的歧义 */
        return TRUE;
    }
    if (prcClip->left <= prcWnd->left && prcClip->top <= prcWnd->top &&
        prcClip->right >= prcWnd->right && prcClip->bottom >= prcWnd->bottom)
        return FALSE; /* 整窗可见：不需要区域 */
    if (prcWant)
        *prcWant = *prcClip;
    return TRUE;
}

/* 是否真的要把区域交给平台——纯判定，便于单元测试覆盖状态迁移。
 *
 * 判定的关键不在于"期望状态是什么"，而在于"平台侧当前挂着的是否就是期望的那一份"：
 * 平台不会自己清理区域，一旦下发过就必须由我们显式撤销。最容易漏的是"撤销"这一档
 * ——若只看期望状态，就会以为"无需动作"而跳过，结果平台侧永远挂着上一次下发的区域。
 * demos/uieditor 的真实子窗口就踩在这里：窗口先以 0 尺寸创建、随后被摆到真实几何变
 * 成整窗可见，若不补一次撤销，子窗口整片不显示。
 *
 * 反向同样要紧：期望与现状一致时一次都不该下发。下发是有副作用的——macOS 的
 * setNsWindowRgn 会切 wantsLayer 并触发重绘，X11 会走一次 Shape 请求。空转的下发会
 * 打断上层正在排队的绘制/失效安排：SOUI 的 autoShape 宿主每次 Present 都会调
 * SetWindowRgn，若这时再叠一次同步重绘，正在排队的那次 WM_NCPAINT 就失去了失效区，
 * 画在非客户区的滚动条再也不画（demos/uieditor 的子窗口滚动条就是这么丢的）。
 *
 * prcWant  期望的裁剪矩形；NULL 表示"期望无区域"
 * bPushed  平台侧当前是否挂着本逻辑下发的裁剪区域
 * prcPushed平台侧当前挂着的那块裁剪矩形（bPushed 为真时有效）
 * bForce   调用方（app 改了 SetWindowRgn 的区域）要求强制下发一次
 */
inline BOOL SbNeedPushClipRgn(const RECT *prcWant, BOOL bPushed, const RECT *prcPushed, BOOL bForce)
{
    if (bForce)
        return TRUE;
    if (!prcWant)
        return bPushed; /* 期望无区域：只有平台侧还挂着才需要撤销 */
    if (!bPushed)
        return TRUE; /* 期望有区域、平台侧还没有 */
    return !EqualRect(prcWant, prcPushed); /* 两边都有区域：只有变了才值得下发 */
}

#endif // _SWINX_WND_CLIP_H_
