#ifndef __NATIVE_WND_H__
#define __NATIVE_WND_H__

#include <windows.h>
#include "msgcrack.h"

#define WC_NATIVE "native_wnd"

class CNativeWnd {
    BOOL m_bDestroyed;
    int m_msgRecursiveCount;
   
  public:
    HWND m_hWnd;

  public:
    CNativeWnd()
        : m_hWnd(0)
        , m_bDestroyed(FALSE)
        , m_msgRecursiveCount(0)
        
    {
    }

    virtual ~CNativeWnd(){};

  public:

    static ATOM RegisterCls(LPCSTR clsName);
    static LRESULT CALLBACK StartWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

  public:

    BOOL CreateWindow(DWORD exStyle, LPCSTR clsName, LPCSTR windowName, DWORD style, INT x, INT y, INT width, INT height, HWND parent, HMENU menu, HINSTANCE instance);


    BOOL Invalidate();

    virtual BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult, DWORD dwMsgMapID = 0);
    
    virtual void OnFinalMessage(HWND hWnd)
    {
      
    }
};

#endif //__NATIVE_WND_H__