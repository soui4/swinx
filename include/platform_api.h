#ifndef _PLATFORM_API_H_
#define _PLATFORM_API_H_

#include <windows.h>
#include <imm.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_API_VERSION 2

struct PlatformClipboardAPI {
    BOOL (*openClipboard)(HWND hWndNewOwner);
    BOOL (*closeClipboard)(void);
    BOOL (*emptyClipboard)(void);
    HANDLE (*getClipboardData)(UINT uFormat);
    HANDLE (*setClipboardData)(UINT uFormat, HANDLE hMem);
    BOOL (*isClipboardFormatAvailable)(UINT format);
    UINT (*registerClipboardFormat)(LPCSTR pszName);
    HWND (*getClipboardOwner)(void);
    BOOL (*hasFormat)(UINT format);
};

struct PlatformWindowAPI {
    UINT_PTR (*createWindow)(UINT_PTR hParent, LPCTSTR pszClsName, LPCTSTR pszTitle,
                             DWORD dwStyle, DWORD dwExStyle,
                             int x, int y, int nWidth, int nHeight,
                             UINT_PTR hMenu, UINT_PTR hInstance, LPVOID lpParam);
    BOOL (*destroyWindow)(UINT_PTR hWnd);
    HWND (*getWindow)(HWND hWnd,int code);
    /* Timer APIs: platform may supply timers when SConnection doesn't run its own loop */
    UINT_PTR (*setTimer)(UINT_PTR hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc);
    BOOL (*killTimer)(UINT_PTR hWnd, UINT_PTR id);
    BOOL (*killWindowTimers)(UINT_PTR hWnd);
    BOOL (*moveWindow)(HWND hWnd, int x, int y, int nWidth, int nHeight, BOOL bRepaint);
    BOOL (*setWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
    BOOL (*setWindowSize)(HWND hWnd, int cx, int cy);
    BOOL (*setWindowPosEx)(HWND hWnd, int x, int y);
    void (*invalidRect)(HWND hWnd,LPCRECT prc);
    BOOL (*isWindowVisible)(HWND hWnd);
    BOOL (*showWindow)(HWND hWnd, int nCmdShow);
    BOOL (*enableWindow)(HWND hWnd, BOOL bEnable);
    BOOL (*isWindowEnabled)(HWND hWnd);
    HWND (*getForegroundWindow)(void);
    BOOL (*setForegroundWindow)(HWND hWnd);
    HWND (*getFocus)(void);
    HWND (*setFocus)(HWND hWnd);
    HWND (*getScreen)();
    int (*getScreenWidth)(int screenIndex);
    int (*getScreenHeight)(int screenIndex);
    void (*getWorkArea)(HMONITOR hMonitor, RECT *prc);
    HWND (*setCapture)(HWND hWnd);
    BOOL (*releaseCapture)();
    void (*postMessage)(void);
    int (*getDpi)(void);
    BOOL (*getCursorPos)(LPPOINT);

    /* 查询当前鼠标按键状态，返回 MK_LBUTTON/MK_RBUTTON/MK_MBUTTON 位掩码 */
    DWORD (*getMouseButtons)(void);

    UINT (*getRawInputDeviceList)(PRAWINPUTDEVICELIST pRawInputDeviceList,PUINT puiNumDevices,UINT cbSize);
    UINT (*getRawInputDeviceInfoA)(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize);
    UINT (*getRawInputDeviceInfoW)(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize);
    BOOL (*showSoftKeyboard)(HWND hWnd,BOOL bShow);
};

struct PlatformIMEAPI {
    HIMC (*immCreateContext)(void);
    BOOL (*immDestroyContext)(HIMC hIMC);
    HIMC (*immGetContext)(HWND hWnd);
    HIMC (*immAssociateContext)(HWND hWnd, HIMC hIMC);
    BOOL (*immReleaseContext)(HWND hWnd, HIMC hIMC);
    LONG (*immGetCompositionStringA)(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen);
    LONG (*immGetCompositionStringW)(HIMC hIMC, DWORD dwIndex, LPVOID lpBuf, DWORD dwBufLen);
    BOOL (*immSetCompositionStringA)(HIMC hIMC, DWORD dwIndex, LPVOID lpComp, DWORD dwCompLen, LPVOID lpRead, DWORD dwReadLen);
    BOOL (*immSetCompositionStringW)(HIMC hIMC, DWORD dwIndex, LPVOID lpComp, DWORD dwCompLen, LPVOID lpRead, DWORD dwReadLen);
    BOOL (*immNotifyIME)(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue);
    BOOL (*immGetConversionStatus)(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence);
    BOOL (*immSetConversionStatus)(HIMC hIMC, DWORD dwConversion, DWORD dwSentence);
    BOOL (*immGetOpenStatus)(HIMC hIMC);
    BOOL (*immSetOpenStatus)(HIMC hIMC, BOOL bOpen);
    BOOL (*immGetStatusWindowPos)(HIMC hIMC, LPPOINT lpptPos);
    BOOL (*immSetStatusWindowPos)(HIMC hIMC, LPPOINT lpptPos);
    BOOL (*immGetCompositionWindow)(HIMC hIMC, LPCOMPOSITIONFORM lpCompForm);
    BOOL (*immSetCompositionWindow)(HIMC hIMC, LPCOMPOSITIONFORM lpCompForm);
    BOOL (*immGetCandidateWindow)(HIMC hIMC, DWORD dwIndex, LPCANDIDATEFORM lpCandidate);
    BOOL (*immSetCandidateWindow)(HIMC hIMC, LPCANDIDATEFORM lpCandidate);
    LRESULT (*immEscapeA)(HKL hKL, HIMC hIMC, UINT uEscape, LPVOID lpData);
    LRESULT (*immEscapeW)(HKL hKL, HIMC hIMC, UINT uEscape, LPVOID lpData);
    BOOL (*immSetCompositionFontA)(HIMC hIMC, LPLOGFONTA lplf);
    BOOL (*immSetCompositionFontW)(HIMC hIMC, LPLOGFONTW lplf);
    DWORD (*immGetProperty)(HKL hKL, DWORD fdwIndex);
    UINT (*immGetVirtualKey)(HWND hWnd);
    HWND (*immGetDefaultIMEWnd)(HWND hWnd);
    BOOL (*immIsIME)(HKL hKL);
};

struct PlatformAudioAPI {
    BOOL (*playSound)(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound);
};

struct PlatformPathAPI {
    // 获取临时目录路径（UTF-8）。返回值约定与 Win32 GetTempPathA 一致：
    // 成功为写入 lpBuffer 的字节数（含结尾 '\0"），缓冲区不足或失败返回 0
    DWORD (*getTempPathA)(DWORD nBufferLength, LPSTR lpBuffer);
    // 获取特殊文件夹路径（UTF-8）。语义与 Win32 SHGetSpecialFolderPathA 一致：
    // nFolder 为 CSIDL_* 常量；fCreate 指示是否在目录不存在时创建。
    // 成功返回 TRUE 并将含结尾 '\0" 的路径写入 lpszPath，失败返回 FALSE
    BOOL (*getSpecialFolderPathA)(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate);
};

struct PlatformAPI {
    int version;
    struct PlatformClipboardAPI clipboard;
    struct PlatformWindowAPI window;
    struct PlatformIMEAPI ime;
    struct PlatformAudioAPI audio;
    struct PlatformPathAPI path;
};

extern struct PlatformAPI g_platformAPI;

BOOL PlatformAPI_Init(struct PlatformAPI *api);
void PlatformAPI_Deinit(void);

#ifdef __cplusplus
}
#endif

#endif // _PLATFORM_API_H_
