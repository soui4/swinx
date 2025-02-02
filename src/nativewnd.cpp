#include "nativewnd.h"
#include <src/wndobj.h>
#include <vector>

ATOM CNativeWnd::RegisterCls(LPCSTR clsName)
{
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEXA), 0 };
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = StartWindowProc;
    wcex.hInstance = 0;
    wcex.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = clsName;
    return ::RegisterClassEx(&wcex);
}

BOOL CNativeWnd::Invalidate()
{
    RECT rcWnd;
    if (GetWindowRect(m_hWnd, &rcWnd))
    {
        InvalidateRect(m_hWnd, &rcWnd, FALSE);
        return UpdateWindow(m_hWnd);
    }
    return FALSE;
}

BOOL CNativeWnd::CreateWindowW(DWORD exStyle, LPCWSTR clsName, LPCWSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance)
{
    CreateWindowExW(exStyle, clsName, windowName, style, x, y, width, height, parent, menu, instance, this);
    return m_hWnd != 0;
}

BOOL CNativeWnd::CreateWindowA(DWORD exStyle, LPCSTR clsName, LPCSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance)
{
    HWND hRet = CreateWindowExA(exStyle, clsName, windowName, style, x, y, width, height, parent, menu, instance, this);
    if (hRet && !m_hWnd)
    {
        Attach(hRet);
    }
    return m_hWnd != 0;
}

void CNativeWnd::Attach(HWND hWnd)
{
    ::SetWindowLongPtr(hWnd, GWL_OPAQUE, (LONG_PTR)this);
    ::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)WindowProc);
    m_hWnd = hWnd;
}

LRESULT CNativeWnd::StartWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
    CNativeWnd *pThis = (CNativeWnd *)cs->lpCreateParams;
    pThis->Attach(hWnd);
    return WindowProc(hWnd, message, wParam, lParam);
}

LRESULT CNativeWnd::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CNativeWnd *pThis = (CNativeWnd *)GetWindowLongPtr(hWnd, GWL_OPAQUE);
    LRESULT lRes = 0;
    pThis->m_msgRecursiveCount++;
    BOOL bRet = pThis->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, 0);
    // do the default processing if message was not handled
    if (!bRet)
    {
        lRes = DefWindowProc(pThis->m_hWnd, uMsg, wParam, lParam);
        if (uMsg == WM_NCDESTROY)
        {
            // mark window as destryed
            pThis->m_bDestroyed = TRUE;
        }
    }
    pThis->m_msgRecursiveCount--;
    if (pThis->m_msgRecursiveCount == 0 && pThis->m_bDestroyed)
    {
        HWND hWndThis = pThis->m_hWnd;
        pThis->m_hWnd = 0;
        pThis->m_bDestroyed = FALSE;
        pThis->OnFinalMessage(hWndThis);
    }
    return lRes;
}

BOOL CNativeWnd::ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult, DWORD dwMsgMapID)
{
    return FALSE;
}