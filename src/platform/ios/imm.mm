// iOS IME 桩实现：iOS 文本输入通过 UIKeyInput 协议（见 SUIView）直接发送
// WM_IME_CHAR，无需传统 IME 上下文管理。此处保留与 Windows 兼容的 API 壳。

#include <windows.h>
#undef interface   // Prevent basetyps.h #define interface struct from conflicting with ObjC
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

LONG WINAPI ImmGetCompositionStringA(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    return 0;
}
LONG WINAPI ImmGetCompositionStringW(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
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
BOOL ImmIsIME(HKL hKL)
{
    return FALSE;
}
