#ifndef _CMNCTL32_H__
#define _CMNCTL32_H__

#include "../nativewnd.h"

class CControl : public CNativeWnd {
  public:
    CControl(BOOL bAutoFree)
        : CNativeWnd(bAutoFree){};
    virtual ~CControl(){};
    void DrawText(HDC dc, LPCSTR lpText, RECT *rc, UINT format = DT_CENTER | DT_VCENTER)
    {
        ::DrawText(dc, lpText, -1, rc, format);
    }
    void DrawBitmap(HDC dc, HBITMAP hBitmap, RECT *rc)
    {
    }
    void DrawIcon(HDC dc, HICON hBitmap, RECT *rc)
    {
    }
};

template <class T>
LRESULT CALLBACK CtrlStartWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
    T *pThis = (T *)pcs->lpCreateParams;
    if (!pThis || IsBadReadPtr(pThis, sizeof(T)))
        pThis = new T(TRUE);
    pThis->m_hWnd = hWnd;
    ::SetWindowLongPtr(hWnd, GWL_OPAQUE, (LONG_PTR)pThis);
    ::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)CNativeWnd::WindowProc);
    return CNativeWnd::WindowProc(hWnd, message, wParam, lParam);
}

template <class T>
ATOM TRegisterClass(LPCSTR clsName)
{
    WNDCLASSEXA wcex = { sizeof(WNDCLASSEXA), 0 };
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = CtrlStartWindowProc<T>;
    wcex.hInstance = 0;
    wcex.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = ::CreateSolidBrush(RGB(255, 255, 255));
    wcex.lpszClassName = clsName;
    return ::RegisterClassEx(&wcex);
}

class CStatic : public CControl {
    HBITMAP m_hBitMap = NULL;
    HICON m_hIcon = NULL;

  public:
    CStatic(BOOL bAutoFree = FALSE)
        : CControl(bAutoFree){};

    ~CStatic(){};

    void OnPaint(HDC dc);
    BOOL OnEraseBkgnd(HDC dc);
    HBITMAP SetBitmap(HBITMAP hBitmap);
    HICON SetIcon(HICON hIcon);
    BEGIN_MSG_MAP_EX(CStatic)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_ERASEBKGND(OnEraseBkgnd) // comment this line will cause crash.
    END_MSG_MAP()
};

class CButton : public CControl {

    int m_nButtonState = 0;

  public:
    CButton(BOOL bAutoFree = FALSE)
        : CControl(bAutoFree){};
    ~CButton(){};

    BOOL OnEraseBkgnd(HDC dc);
    void OnPaint(HDC dc);
    void OnLButtonUp(UINT nFlags, POINT point);
    void OnMouseHover(UINT nFlags, POINT point);
    void OnMouseLeave();
    void OnMouseMove(UINT nFlags, POINT point);

    BEGIN_MSG_MAP_EX(CButton)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_LBUTTONUP(OnLButtonUp)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MSG_WM_MOUSEHOVER(OnMouseHover)
        MSG_WM_MOUSELEAVE(OnMouseLeave)
    END_MSG_MAP()
};

#endif //_CMNCTL32_H__
