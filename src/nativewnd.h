#ifndef __NATIVE_WND_H__
#define __NATIVE_WND_H__

#include <windows.h>
#include "msgcrack.h"

class CNativeWnd {
    BOOL m_bDestroyed;
    int m_msgRecursiveCount;
    BOOL m_bAutoFree;

  public:
    HWND m_hWnd;

  public:
    CNativeWnd(BOOL bAutoFree = FALSE)
        : m_hWnd(0)
        , m_bDestroyed(FALSE)
        , m_msgRecursiveCount(0)
        , m_bAutoFree(bAutoFree)        
    {
    }

    virtual ~CNativeWnd(){};

  public:

    static ATOM RegisterCls(LPCSTR clsName);
    static LRESULT CALLBACK StartWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

  public:
      void Attach(HWND hWnd);
    BOOL CreateWindowA(DWORD exStyle, LPCSTR clsName, LPCSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance);
    BOOL CreateWindowW(DWORD exStyle, LPCWSTR clsName, LPCWSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance);

    HWND SetCapture() {
        return ::SetCapture(m_hWnd);
    }

    BOOL ReleaseCapture() {
        return ::ReleaseCapture();
    }

    void DestroyWindow() {
        ::DestroyWindow(m_hWnd);
    }
    BOOL Invalidate();

    virtual BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult, DWORD dwMsgMapID = 0);
 protected:
    virtual void OnFinalMessage(HWND hWnd) {
        if (m_bAutoFree)
            delete this;
    }

};

#endif //__NATIVE_WND_H__