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
    HIMC hRet = wndObj->hIMC;   // 返回旧上下文（本函数已 Release 其引用，调用方无需再释放）
    if (hRet)
        hRet->Release();
    wndObj->hIMC = hIMC;        // 先清旧引用，再接管新引用
    if (hIMC)
        hIMC->AddRef();
    wndObj->mConnection->AssociateHIMC(hWnd, wndObj.data(), hIMC);
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

BOOL WINAPI ImmGetStatusWindowPos(IN HIMC hIMC, _Out_ LPPOINT lpptPos)
{
    if (!hIMC || !lpptPos)
        return FALSE;
    *lpptPos = hIMC->ptStatus;
    return TRUE;
}
BOOL WINAPI ImmSetStatusWindowPos(IN HIMC hIMC, _In_ LPPOINT lpptPos)
{
    if (!hIMC || !lpptPos)
        return FALSE;
    hIMC->ptStatus = *lpptPos;
    return TRUE;
}
BOOL WINAPI ImmGetCompositionWindow(IN HIMC hIMC, _Out_ LPCOMPOSITIONFORM lpCompForm)
{
    if (!hIMC || !lpCompForm)
        return FALSE;
    *lpCompForm = hIMC->compForm;
    return TRUE;
}
BOOL WINAPI ImmSetCompositionWindow(IN HIMC hIMC, _In_ LPCOMPOSITIONFORM lpCompForm)
{
    if (!hIMC || !lpCompForm)
        return FALSE;
    hIMC->compForm = *lpCompForm;
    return TRUE;
}
BOOL WINAPI ImmGetCandidateWindow(IN HIMC hIMC, IN DWORD dwIndex, _Out_ LPCANDIDATEFORM lpCandidate)
{
    if (!hIMC || !lpCandidate)
        return FALSE;
    if (dwIndex >= IMC_MAXCANDIDATEWINDOW)
        return FALSE;
    *lpCandidate = hIMC->candForm[dwIndex];
    return TRUE;
}
BOOL WINAPI ImmSetCandidateWindow(IN HIMC hIMC, _In_ LPCANDIDATEFORM lpCandidate)
{
    if (!hIMC || !lpCandidate)
        return FALSE;
    if (lpCandidate->dwIndex >= IMC_MAXCANDIDATEWINDOW)
        return FALSE;
    hIMC->candForm[lpCandidate->dwIndex] = *lpCandidate;
    return TRUE;
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

BOOL ImmGetOpenStatus(HIMC hIMC)
{
    if (!hIMC)
        return FALSE;
    return hIMC->fOpen;
}
BOOL ImmSetOpenStatus(HIMC hIMC, BOOL fOpen)
{
    if (!hIMC)
        return FALSE;
    hIMC->fOpen = fOpen;
    return TRUE;
}

UINT ImmGetVirtualKey(HWND hWnd __attribute__((unused)))
{
    return 0;
}

HWND ImmGetDefaultIMEWnd(HWND hWnd __attribute__((unused)))
{
    return 0;
}

BOOL ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)
{
    if (!hIMC)
        return FALSE;
    hIMC->fdwConversion = fdwConversion;
    hIMC->fdwSentence = fdwSentence;
    return TRUE;
}

BOOL ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    if (!hIMC)
        return FALSE;
    if (lpfdwConversion)
        *lpfdwConversion = hIMC->fdwConversion;
    if (lpfdwSentence)
        *lpfdwSentence = hIMC->fdwSentence;
    return TRUE;
}
BOOL ImmIsIME(HKL hKL __attribute__((unused)))
{
    return FALSE;
}