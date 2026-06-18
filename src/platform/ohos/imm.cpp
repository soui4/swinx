#include <windows.h>
#include <imm.h>
#include "SImContext.h"
#include "wndobj.h"

HIMC WINAPI ImmCreateContext(void)
{
    return new IMContext();
}

BOOL ImmDestroyContext(HIMC hIMC)
{
    if (!hIMC)
        return FALSE;
    hIMC->Release();
    return TRUE;
}

HIMC WINAPI ImmGetContext(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wndObj->hIMC)
        return 0;
    wndObj->hIMC->AddRef();
    return wndObj->hIMC;
}

BOOL ImmReleaseContext(HWND hWnd, HIMC hIMC)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !hIMC || wndObj->hIMC != hIMC)
        return FALSE;
    hIMC->Release();
    return TRUE;
}

HIMC ImmAssociateContext(HWND hWnd, HIMC hIMC)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return nullptr;
    HIMC old = wndObj->hIMC;
    if (hIMC)
        hIMC->AddRef();
    wndObj->hIMC = hIMC;
    wndObj->mConnection->AssociateHIMC(hWnd, wndObj.data(), hIMC);
    if (old)
        old->Release();
    return old;
}

LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD) { return 0; }
LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD) { return 0; }
BOOL WINAPI ImmGetStatusWindowPos(HIMC, LPPOINT) { return FALSE; }
BOOL WINAPI ImmSetStatusWindowPos(HIMC, LPPOINT) { return FALSE; }
BOOL WINAPI ImmGetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return FALSE; }
BOOL WINAPI ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return FALSE; }
BOOL WINAPI ImmGetCandidateWindow(HIMC, DWORD, LPCANDIDATEFORM) { return FALSE; }
BOOL WINAPI ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM) { return FALSE; }
BOOL WINAPI ImmNotifyIME(HIMC, DWORD, DWORD, DWORD) { return FALSE; }
LRESULT ImmEscapeW(HKL, HIMC, UINT, LPVOID) { return 0; }
LRESULT ImmEscapeA(HKL, HIMC, UINT, LPVOID) { return 0; }
BOOL WINAPI ImmSetCompositionFontA(HIMC, LPLOGFONTA) { return FALSE; }
BOOL WINAPI ImmSetCompositionFontW(HIMC, LPLOGFONTW) { return FALSE; }
BOOL WINAPI ImmSetCompositionStringA(HIMC, DWORD, LPVOID, DWORD, LPVOID, DWORD) { return FALSE; }
BOOL WINAPI ImmSetCompositionStringW(HIMC, DWORD, LPVOID, DWORD, LPVOID, DWORD) { return FALSE; }
DWORD WINAPI ImmGetProperty(HKL, DWORD) { return 0; }
BOOL ImmGetOpenStatus(HIMC) { return FALSE; }
BOOL ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
UINT ImmGetVirtualKey(HWND) { return 0; }
HWND ImmGetDefaultIMEWnd(HWND) { return 0; }
BOOL ImmSetConversionStatus(HIMC, DWORD, DWORD) { return FALSE; }
BOOL ImmGetConversionStatus(HIMC, LPDWORD, LPDWORD) { return FALSE; }
BOOL ImmIsIME(HKL) { return FALSE; }
