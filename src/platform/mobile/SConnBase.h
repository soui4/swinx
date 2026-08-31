#ifndef _SWINX_MOBILE_SCONNBASE_H_
#define _SWINX_MOBILE_SCONNBASE_H_

class SConnBase {
public:
    virtual ~SConnBase() {}
    virtual void onTerminate() = 0;
    virtual void OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) = 0;
    virtual void OnDrawRect(HWND hWnd, const RECT &rc, void *ctx) = 0;
    virtual void OnNsActive(HWND hWnd, BOOL bActive) = 0;
};

#endif // _SWINX_MOBILE_SCONNBASE_H_