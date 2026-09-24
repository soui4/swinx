/*
 * SConnBase.h -- bridge interface the host app implements to feed platform
 * events into swinx on the FreeRTOS port (touch/input/render callbacks),
 * mirroring src/platform/mobile/SConnBase.h.
 */
#ifndef _SWINX_FREERTOS_SCONNBASE_H_
#define _SWINX_FREERTOS_SCONNBASE_H_

class SConnBase {
public:
    virtual ~SConnBase() {}
    virtual void onTerminate() = 0;
    virtual void OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) = 0;
    virtual void OnDrawRect(HWND hWnd, const RECT &rc, void *ctx) = 0;
    virtual void OnNsActive(HWND hWnd, BOOL bActive) = 0;
};

#endif // _SWINX_FREERTOS_SCONNBASE_H_
