#include <windows.h>
#include <imm.h>
#include "SImContext.h"
#include "wndobj.h"

HIMC WINAPI ImmCreateContext(void)
{
    return 0;
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
    if (!wndObj)
        return 0;
    else
    {
        if (wndObj->hIMC)
            wndObj->hIMC->AddRef();
        return wndObj->hIMC;
    }
}

BOOL ImmReleaseContext(HWND hWnd, HIMC hIMC)
{
    if (!hIMC)
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wndObj->hIMC)
        return FALSE;
    if (wndObj->hIMC != hIMC)
        return FALSE;
    wndObj->hIMC->Release();
    return TRUE;
}

HIMC ImmAssociateContext(HWND hWnd, HIMC hIMC)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return nullptr;
    HIMC hRet = wndObj->hIMC;
    wndObj->mConnection->AssociateHIMC(hWnd, wndObj.data(), hIMC);
    return hRet;
}

LONG WINAPI ImmGetCompositionStringA(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    return 0;
}
LONG WINAPI ImmGetCompositionStringW(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    return 0;
}

BOOL WINAPI ImmGetStatusWindowPos(IN HIMC, _Out_ LPPOINT lpptPos)
{
    return FALSE;
}
BOOL WINAPI ImmSetStatusWindowPos(IN HIMC, _In_ LPPOINT lpptPos)
{
    return FALSE;
}
BOOL WINAPI ImmGetCompositionWindow(IN HIMC, _Out_ LPCOMPOSITIONFORM lpCompForm)
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionWindow(IN HIMC, _In_ LPCOMPOSITIONFORM lpCompForm)
{
    return FALSE;
}
BOOL WINAPI ImmGetCandidateWindow(IN HIMC, IN DWORD, _Out_ LPCANDIDATEFORM lpCandidate)
{
    return FALSE;
}
BOOL WINAPI ImmSetCandidateWindow(IN HIMC, _In_ LPCANDIDATEFORM lpCandidate)
{
    return FALSE;
}

BOOL WINAPI ImmNotifyIME(IN HIMC, IN DWORD dwAction, IN DWORD dwIndex, IN DWORD dwValue)
{
    return FALSE;
}

LRESULT ImmEscapeW(HKL unnamedParam1, HIMC unnamedParam2, UINT unnamedParam3, LPVOID unnamedParam4)
{
    return 0;
}

LRESULT ImmEscapeA(HKL unnamedParam1, HIMC unnamedParam2, UINT unnamedParam3, LPVOID unnamedParam4)
{
    return 0;
}

BOOL WINAPI ImmSetCompositionFontA(IN HIMC, _In_ LPLOGFONTA lplf)
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionFontW(IN HIMC, _In_ LPLOGFONTW lplf)
{
    return FALSE;
}

BOOL WINAPI ImmSetCompositionStringA(IN HIMC, IN DWORD dwIndex, _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp, IN DWORD dwCompLen, _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead, IN DWORD dwReadLen)
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionStringW(IN HIMC, IN DWORD dwIndex, _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp, IN DWORD dwCompLen, _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead, IN DWORD dwReadLen)
{
    return FALSE;
}

DWORD WINAPI ImmGetProperty(HKL hKL, DWORD fdwIndex)
{
    return 0;
}

BOOL ImmGetOpenStatus(HIMC hIMC)
{
    return FALSE;
}
BOOL ImmSetOpenStatus(HIMC hIMC, BOOL fOpen)
{
    return FALSE;
}

UINT ImmGetVirtualKey(HWND hWnd)
{
    return 0;
}

HWND ImmGetDefaultIMEWnd(HWND hWnd)
{
    return 0;
}

BOOL ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)
{
    return FALSE;
}

BOOL ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    return FALSE;
}
BOOL ImmIsIME(HKL hKL)
{
    return FALSE;
}