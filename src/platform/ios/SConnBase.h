#ifndef _SCONNBASE_H_
#define _SCONNBASE_H_
#include <ctypes.h>
#include <CoreGraphics/CoreGraphics.h>

class SConnBase {
  public:
    virtual ~SConnBase() {}
    virtual void onTerminate() = 0;
    virtual void OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) = 0;
    virtual void OnDrawRect(HWND hWnd, const RECT &rc, CGContextRef ctx) = 0;
    virtual void OnNsActive(HWND hWnd, BOOL bActive) = 0;
};

#endif //_SCONNBASE_H_
