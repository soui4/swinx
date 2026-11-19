/*
 * swinx/platform/wayland/imm.cpp
 *
 * Win32 IMM 兼容层。真正的输入法状态在 framebuffer.cpp 的 zwp_text_input_v3：
 * commit_string → WM_IME_CHAR（与 X11 xim_commit_string 同款）。这里让
 * HIMC 真正存在并与 HWND 关联，SHostWnd::EnableIME 才能把窗口标记为支持
 * 输入法；组合串读取仍为空（预编辑内联尚未实现）。
 */
#include <windows.h>
#include <imm.h>
#include <string.h>
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
        return NULL;
    wndObj->hIMC->AddRef();
    return wndObj->hIMC;
}

HIMC WINAPI ImmAssociateContext(HWND hWnd, HIMC hIMC)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return NULL;
    HIMC hRet = wndObj->hIMC;
    if (hRet)
        hRet->Release();
    wndObj->hIMC = hIMC;
    if (hIMC)
        hIMC->AddRef();
    wndObj->mConnection->AssociateHIMC(hWnd, wndObj.data(), hIMC);
    return hRet;
}

BOOL WINAPI ImmReleaseContext(HWND hWnd, HIMC hIMC)
{
    if (!hIMC)
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->hIMC != hIMC)
        return FALSE;
    hIMC->Release();
    return TRUE;
}

/* 没有组合串：返回 0，调用方（含 Scintilla）都把 <=0 当作「没有文本」。 */
LONG WINAPI ImmGetCompositionStringW(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID, IN DWORD)
{
    return 0;
}
LONG WINAPI ImmGetCompositionStringA(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID, IN DWORD)
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
    if (!hIMC || !lpCandidate || dwIndex >= IMC_MAXCANDIDATEWINDOW)
        return FALSE;
    *lpCandidate = hIMC->candForm[dwIndex];
    return TRUE;
}
BOOL WINAPI ImmSetCandidateWindow(IN HIMC hIMC, _In_ LPCANDIDATEFORM lpCandidate)
{
    if (!hIMC || !lpCandidate || lpCandidate->dwIndex >= IMC_MAXCANDIDATEWINDOW)
        return FALSE;
    hIMC->candForm[lpCandidate->dwIndex] = *lpCandidate;
    return TRUE;
}

BOOL WINAPI ImmNotifyIME(IN HIMC, IN DWORD, IN DWORD, IN DWORD)
{
    return FALSE;
}

LRESULT WINAPI ImmEscapeW(HKL, HIMC, UINT, LPVOID) { return 0; }
LRESULT WINAPI ImmEscapeA(HKL, HIMC, UINT, LPVOID) { return 0; }
BOOL WINAPI ImmSetCompositionFontA(IN HIMC, _In_ LPLOGFONTA) { return FALSE; }
BOOL WINAPI ImmSetCompositionFontW(IN HIMC, _In_ LPLOGFONTW) { return FALSE; }
BOOL WINAPI ImmSetCompositionStringW(IN HIMC, IN DWORD, _In_reads_bytes_opt_(dwCompLen) LPVOID, IN DWORD, _In_reads_bytes_opt_(dwReadLen) LPVOID, IN DWORD)
{
    return FALSE;
}
BOOL WINAPI ImmSetCompositionStringA(IN HIMC, IN DWORD, _In_reads_bytes_opt_(dwCompLen) LPVOID, IN DWORD, _In_reads_bytes_opt_(dwReadLen) LPVOID, IN DWORD)
{
    return FALSE;
}
DWORD WINAPI ImmGetProperty(HKL, DWORD) { return 0; }

BOOL WINAPI ImmGetOpenStatus(HIMC hIMC)
{
    return hIMC ? hIMC->fOpen : FALSE;
}
BOOL WINAPI ImmSetOpenStatus(HIMC hIMC, BOOL fOpen)
{
    if (!hIMC)
        return FALSE;
    hIMC->fOpen = fOpen;
    return TRUE;
}
HWND WINAPI ImmGetDefaultIMEWnd(HWND) { return NULL; }

BOOL WINAPI ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    if (!hIMC)
        return FALSE;
    if (lpfdwConversion)
        *lpfdwConversion = hIMC->fdwConversion;
    if (lpfdwSentence)
        *lpfdwSentence = hIMC->fdwSentence;
    return TRUE;
}
BOOL WINAPI ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)
{
    if (!hIMC)
        return FALSE;
    hIMC->fdwConversion = fdwConversion;
    hIMC->fdwSentence = fdwSentence;
    return TRUE;
}
BOOL WINAPI ImmIsIME(HKL) { return FALSE; }
UINT WINAPI ImmGetVirtualKey(HWND) { return 0; }
