#include <windows.h>
#include <imm.h>
#include "platform_api.h"

HIMC WINAPI ImmCreateContext(void)
{
    if (g_platformAPI.ime.immCreateContext)
        return g_platformAPI.ime.immCreateContext();
    return (HIMC)1;
}

BOOL ImmDestroyContext(HIMC hIMC)
{
    if (g_platformAPI.ime.immDestroyContext)
        return g_platformAPI.ime.immDestroyContext(hIMC);
    return TRUE;
}

HIMC WINAPI ImmGetContext(HWND hWnd)
{
    if (g_platformAPI.ime.immGetContext)
        return g_platformAPI.ime.immGetContext(hWnd);
    return (HIMC)1;
}

HIMC WINAPI ImmAssociateContext(HWND hWnd, HIMC hIMC)
{
    if (g_platformAPI.ime.immAssociateContext)
        return g_platformAPI.ime.immAssociateContext(hWnd, hIMC);
    return hIMC;
}

BOOL WINAPI ImmReleaseContext(HWND hWnd, HIMC hIMC)
{
    if (g_platformAPI.ime.immReleaseContext)
        return g_platformAPI.ime.immReleaseContext(hWnd, hIMC);
    return TRUE;
}

LONG WINAPI ImmGetCompositionStringA(IN HIMC hIMC, IN DWORD dwIndex, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    if (g_platformAPI.ime.immGetCompositionStringA)
        return g_platformAPI.ime.immGetCompositionStringA(hIMC, dwIndex, lpBuf, dwBufLen);
    return 0;
}

LONG WINAPI ImmGetCompositionStringW(IN HIMC hIMC, IN DWORD dwIndex, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    if (g_platformAPI.ime.immGetCompositionStringW)
        return g_platformAPI.ime.immGetCompositionStringW(hIMC, dwIndex, lpBuf, dwBufLen);
    return 0;
}

BOOL WINAPI ImmSetCompositionStringA(IN HIMC hIMC, IN DWORD dwIndex, _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp, IN DWORD dwCompLen, _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead, IN DWORD dwReadLen)
{
    if (g_platformAPI.ime.immSetCompositionStringA)
        return g_platformAPI.ime.immSetCompositionStringA(hIMC, dwIndex, lpComp, dwCompLen, lpRead, dwReadLen);
    return TRUE;
}

BOOL WINAPI ImmSetCompositionStringW(IN HIMC hIMC, IN DWORD dwIndex, _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp, IN DWORD dwCompLen, _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead, IN DWORD dwReadLen)
{
    if (g_platformAPI.ime.immSetCompositionStringW)
        return g_platformAPI.ime.immSetCompositionStringW(hIMC, dwIndex, lpComp, dwCompLen, lpRead, dwReadLen);
    return TRUE;
}

BOOL WINAPI ImmNotifyIME(HIMC hIMC, DWORD dwAction, DWORD dwIndex, DWORD dwValue)
{
    if (g_platformAPI.ime.immNotifyIME)
        return g_platformAPI.ime.immNotifyIME(hIMC, dwAction, dwIndex, dwValue);
    return TRUE;
}

BOOL WINAPI ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    if (g_platformAPI.ime.immGetConversionStatus)
        return g_platformAPI.ime.immGetConversionStatus(hIMC, lpfdwConversion, lpfdwSentence);
    if (lpfdwConversion)
        *lpfdwConversion = IME_CMODE_NATIVE;
    if (lpfdwSentence)
        *lpfdwSentence = IME_SMODE_NONE;
    return TRUE;
}

BOOL WINAPI ImmSetConversionStatus(HIMC hIMC, DWORD dwConversion, DWORD dwSentence)
{
    if (g_platformAPI.ime.immSetConversionStatus)
        return g_platformAPI.ime.immSetConversionStatus(hIMC, dwConversion, dwSentence);
    return TRUE;
}

BOOL WINAPI ImmGetOpenStatus(HIMC hIMC)
{
    if (g_platformAPI.ime.immGetOpenStatus)
        return g_platformAPI.ime.immGetOpenStatus(hIMC);
    return FALSE;
}

BOOL WINAPI ImmSetOpenStatus(HIMC hIMC, BOOL bOpen)
{
    if (g_platformAPI.ime.immSetOpenStatus)
        return g_platformAPI.ime.immSetOpenStatus(hIMC, bOpen);
    return TRUE;
}

BOOL WINAPI ImmGetStatusWindowPos(IN HIMC hIMC, _Out_ LPPOINT lpptPos)
{
    if (g_platformAPI.ime.immGetStatusWindowPos)
        return g_platformAPI.ime.immGetStatusWindowPos(hIMC, lpptPos);
    if (lpptPos)
        lpptPos->x = lpptPos->y = 0;
    return TRUE;
}

BOOL WINAPI ImmSetStatusWindowPos(IN HIMC hIMC, _In_ LPPOINT lpptPos)
{
    if (g_platformAPI.ime.immSetStatusWindowPos)
        return g_platformAPI.ime.immSetStatusWindowPos(hIMC, lpptPos);
    return TRUE;
}

BOOL WINAPI ImmGetCompositionWindow(IN HIMC hIMC, _Out_ LPCOMPOSITIONFORM lpCompForm)
{
    if (g_platformAPI.ime.immGetCompositionWindow)
        return g_platformAPI.ime.immGetCompositionWindow(hIMC, lpCompForm);
    if (lpCompForm)
    {
        lpCompForm->dwStyle = CFS_DEFAULT;
        lpCompForm->ptCurrentPos.x = 0;
        lpCompForm->ptCurrentPos.y = 0;
        lpCompForm->rcArea.left = lpCompForm->rcArea.top = 0;
        lpCompForm->rcArea.right = lpCompForm->rcArea.bottom = 0;
    }
    return TRUE;
}

BOOL WINAPI ImmSetCompositionWindow(IN HIMC hIMC, _In_ LPCOMPOSITIONFORM lpCompForm)
{
    if (g_platformAPI.ime.immSetCompositionWindow)
        return g_platformAPI.ime.immSetCompositionWindow(hIMC, lpCompForm);
    return TRUE;
}

BOOL WINAPI ImmGetCandidateWindow(IN HIMC hIMC, IN DWORD dwIndex, _Out_ LPCANDIDATEFORM lpCandidate)
{
    if (g_platformAPI.ime.immGetCandidateWindow)
        return g_platformAPI.ime.immGetCandidateWindow(hIMC, dwIndex, lpCandidate);
    if (lpCandidate)
    {
        lpCandidate->dwIndex = dwIndex;
        lpCandidate->dwStyle = CFS_DEFAULT;
        lpCandidate->ptCurrentPos.x = 0;
        lpCandidate->ptCurrentPos.y = 0;
        lpCandidate->rcArea.left = lpCandidate->rcArea.top = 0;
        lpCandidate->rcArea.right = lpCandidate->rcArea.bottom = 0;
    }
    return TRUE;
}

BOOL WINAPI ImmSetCandidateWindow(IN HIMC hIMC, _In_ LPCANDIDATEFORM lpCandidate)
{
    if (g_platformAPI.ime.immSetCandidateWindow)
        return g_platformAPI.ime.immSetCandidateWindow(hIMC, lpCandidate);
    return TRUE;
}

LRESULT WINAPI ImmEscapeA(HKL hKL, HIMC hIMC, UINT uEscape, LPVOID lpData)
{
    if (g_platformAPI.ime.immEscapeA)
        return g_platformAPI.ime.immEscapeA(hKL, hIMC, uEscape, lpData);
    return 0;
}

LRESULT WINAPI ImmEscapeW(HKL hKL, HIMC hIMC, UINT uEscape, LPVOID lpData)
{
    if (g_platformAPI.ime.immEscapeW)
        return g_platformAPI.ime.immEscapeW(hKL, hIMC, uEscape, lpData);
    return 0;
}

BOOL WINAPI ImmSetCompositionFontA(IN HIMC hIMC, _In_ LPLOGFONTA lplf)
{
    if (g_platformAPI.ime.immSetCompositionFontA)
        return g_platformAPI.ime.immSetCompositionFontA(hIMC, lplf);
    return TRUE;
}

BOOL WINAPI ImmSetCompositionFontW(IN HIMC hIMC, _In_ LPLOGFONTW lplf)
{
    if (g_platformAPI.ime.immSetCompositionFontW)
        return g_platformAPI.ime.immSetCompositionFontW(hIMC, lplf);
    return TRUE;
}

DWORD WINAPI ImmGetProperty(HKL hKL, DWORD fdwIndex)
{
    if (g_platformAPI.ime.immGetProperty)
        return g_platformAPI.ime.immGetProperty(hKL, fdwIndex);
    return 0;
}

UINT WINAPI ImmGetVirtualKey(HWND hWnd)
{
    if (g_platformAPI.ime.immGetVirtualKey)
        return g_platformAPI.ime.immGetVirtualKey(hWnd);
    return 0;
}

HWND WINAPI ImmGetDefaultIMEWnd(HWND hWnd)
{
    if (g_platformAPI.ime.immGetDefaultIMEWnd)
        return g_platformAPI.ime.immGetDefaultIMEWnd(hWnd);
    return NULL;
}

BOOL WINAPI ImmIsIME(HKL hKL)
{
    if (g_platformAPI.ime.immIsIME)
        return g_platformAPI.ime.immIsIME(hKL);
    return FALSE;
}
