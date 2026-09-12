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
    if (hIMC->xic)
    {
        xcb_xim_destroy_ic(hIMC->xim, hIMC->xic, nullptr, nullptr);
        hIMC->xic = 0;
    }

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
    if (hIMC)
        hIMC->AddRef();
    wndObj->mConnection->AssociateHIMC(hWnd, wndObj.data(), hIMC);
    if (hRet)
        hRet->Release();
    return hRet;
}

LONG WINAPI ImmGetCompositionStringA(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf __attribute__((unused)), IN DWORD dwBufLen __attribute__((unused)))
{
    return 0;
}
LONG WINAPI ImmGetCompositionStringW(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf __attribute__((unused)), IN DWORD dwBufLen __attribute__((unused)))
{
    return 0;
}

BOOL WINAPI ImmGetStatusWindowPos(IN HIMC, _Out_ LPPOINT lpptPos __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmSetStatusWindowPos(IN HIMC, _In_ LPPOINT lpptPos __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmGetCompositionWindow(IN HIMC, _Out_ LPCOMPOSITIONFORM lpCompForm __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionWindow(IN HIMC, _In_ LPCOMPOSITIONFORM lpCompForm __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmGetCandidateWindow(IN HIMC, IN DWORD, _Out_ LPCANDIDATEFORM lpCandidate __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmSetCandidateWindow(IN HIMC, _In_ LPCANDIDATEFORM lpCandidate __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI ImmNotifyIME(IN HIMC, IN DWORD dwAction __attribute__((unused)), IN DWORD dwIndex __attribute__((unused)), IN DWORD dwValue __attribute__((unused)))
{
    return FALSE;
}

LRESULT ImmEscapeW(HKL unnamedParam1 __attribute__((unused)), HIMC unnamedParam2 __attribute__((unused)), UINT unnamedParam3 __attribute__((unused)), LPVOID unnamedParam4 __attribute__((unused)))
{
    return 0;
}

LRESULT ImmEscapeA(HKL unnamedParam1 __attribute__((unused)), HIMC unnamedParam2 __attribute__((unused)), UINT unnamedParam3 __attribute__((unused)), LPVOID unnamedParam4 __attribute__((unused)))
{
    return 0;
}

BOOL WINAPI ImmSetCompositionFontA(IN HIMC, _In_ LPLOGFONTA lplf __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionFontW(IN HIMC, _In_ LPLOGFONTW lplf __attribute__((unused)))
{
    return FALSE;
}

BOOL WINAPI ImmSetCompositionStringA(IN HIMC, IN DWORD dwIndex __attribute__((unused)), _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp __attribute__((unused)), IN DWORD dwCompLen __attribute__((unused)), _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead __attribute__((unused)), IN DWORD dwReadLen __attribute__((unused)))
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionStringW(IN HIMC, IN DWORD dwIndex __attribute__((unused)), _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp __attribute__((unused)), IN DWORD dwCompLen __attribute__((unused)), _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead __attribute__((unused)), IN DWORD dwReadLen __attribute__((unused)))
{
    return FALSE;
}

DWORD WINAPI ImmGetProperty(HKL hKL __attribute__((unused)), DWORD fdwIndex __attribute__((unused)))
{
    return 0;
}

BOOL ImmGetOpenStatus(HIMC hIMC __attribute__((unused)))
{
    return FALSE;
}
BOOL ImmSetOpenStatus(HIMC hIMC __attribute__((unused)), BOOL fOpen __attribute__((unused)))
{
    return FALSE;
}

UINT ImmGetVirtualKey(HWND hWnd __attribute__((unused)))
{
    return 0;
}

HWND ImmGetDefaultIMEWnd(HWND hWnd __attribute__((unused)))
{
    return 0;
}

BOOL ImmSetConversionStatus(HIMC hIMC __attribute__((unused)), DWORD fdwConversion __attribute__((unused)), DWORD fdwSentence __attribute__((unused)))
{
    return FALSE;
}

BOOL ImmGetConversionStatus(HIMC hIMC __attribute__((unused)), LPDWORD lpfdwConversion __attribute__((unused)), LPDWORD lpfdwSentence __attribute__((unused)))
{
    return FALSE;
}
BOOL ImmIsIME(HKL hKL __attribute__((unused)))
{
    return FALSE;
}