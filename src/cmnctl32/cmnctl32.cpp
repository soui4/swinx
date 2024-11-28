#include "cmnctl32.h"
#include <wnd.h>
#include <src/wndobj.h>

void CStatic::OnPaint(HDC dc)
{
    PAINTSTRUCT ps;
    dc = BeginPaint(m_hWnd, &ps);
    RECT rc;
    GetClientRect(m_hWnd, &rc);

    if (m_hIcon)
    {
        CControl::DrawIcon(dc, m_hIcon, &rc);
    }
    else if (m_hBitMap)
    {
        CControl::DrawBitmap(dc, m_hBitMap, &rc);
    }
    else
    {

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(dc, hFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));

        WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
        CControl::DrawText(dc, wndObj->title.c_str(), &rc);
        SelectObject(dc, hOldFont);
    }
    EndPaint(m_hWnd, &ps);
}

BOOL CStatic::OnEraseBkgnd(HDC dc)
{
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    FillRect(dc, &rc, GetStockObject(WHITE_BRUSH));
    return TRUE;
}

HBITMAP CStatic::SetBitmap(HBITMAP hBitmap)
{
    m_hIcon = NULL;
    return m_hBitMap;
}

HICON CStatic::SetIcon(HICON hIcon)
{
    m_hBitMap = NULL;
    return m_hIcon;
}

//-------------------------------------------------------------------
BOOL CButton::OnEraseBkgnd(HDC)
{
    return TRUE;
}

void CButton::OnPaint(HDC hdc)
{
    WndObj pWnd = WndMgr::fromHwnd(m_hWnd);
    if (!pWnd)
        return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);

    PAINTSTRUCT ps;
    hdc = BeginPaint(m_hWnd, &ps);

    FillRect(hdc, &rc, (HBRUSH)GetStockObject(m_nButtonState == 0 ? WHITE_BRUSH : GRAY_BRUSH));
    FrameRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, m_nButtonState == 0 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    DrawText(hdc, pWnd->title.c_str(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
    EndPaint(m_hWnd, &ps);
}

void CButton::OnLButtonUp(UINT nFlags, POINT point)
{
    WndObj pWnd = WndMgr::fromHwnd(m_hWnd);
    if (!pWnd)
        return;
    LONG id = GetWindowLong(m_hWnd, GWL_ID);
    ::SendMessage(GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)m_hWnd);
}

void CButton::OnMouseHover(UINT nFlags, POINT point)
{
    TRACKMOUSEEVENT trackMouseEvent = { 0 };
    trackMouseEvent.cbSize = sizeof(TRACKMOUSEEVENT);
    trackMouseEvent.dwFlags = TME_LEAVE;
    trackMouseEvent.hwndTrack = m_hWnd;
    trackMouseEvent.dwHoverTime = 1;
    TrackMouseEvent(&trackMouseEvent);

    m_nButtonState = 1;
    Invalidate();
}

void CButton::OnMouseLeave()
{
    m_nButtonState = 0;
    Invalidate();
}

void CButton::OnMouseMove(UINT nFlags, POINT point)
{
    if (m_nButtonState != 1)
    {
        m_nButtonState = 1;
        Invalidate();
        TRACKMOUSEEVENT trackMouseEvent = { 0 };
        trackMouseEvent.cbSize = sizeof(TRACKMOUSEEVENT);
        trackMouseEvent.dwFlags = TME_LEAVE; // TME_HOVER;
        trackMouseEvent.hwndTrack = m_hWnd;
        trackMouseEvent.dwHoverTime = 1;
        TrackMouseEvent(&trackMouseEvent);
    }
}
