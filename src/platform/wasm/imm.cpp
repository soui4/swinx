/*
 * swinx/platform/wasm/imm.cpp
 *
 * 输入法的 wasm 实现。其它 OS 目录各有各的真实后端（linux/imm.cpp 走 XIM、
 * cocoa/imm.mm 走 Cocoa、mobile/imm.cpp 转发 g_platformAPI），wasm 没有本地
 * IME：候选窗口、组合串位置这些概念都归浏览器的 DOM 输入框管，swinx 只能看到
 * Emscripten 派发的 keydown/keypress（见 src/platform/wasm/input.cpp）。
 *
 * 因此这里全部按「能力不存在」显式失败，返回 NULL/FALSE，绝不伪造一个 HIMC：
 * SOUI::SHostWnd::EnableIME 的用法是 ImmGetContext 拿不到就 ImmCreateContext +
 * ImmAssociateContext，拿到 NULL 之后自然退回纯键盘路径（WM_CHAR 照常），
 * 这正是浏览器里当前唯一可用的文本输入方式。
 *
 * 将来要接真正的组合输入，缺的不是这几个函数，而是 DOM 侧
 * compositionstart/update/end → ImmGetCompositionString 的一条桥；补那天请连同
 * doc/tools/gen_doc.py 的 OVERRIDES 备注一起更新。
 *
 * 后半段（ImmGetCompositionStringW 起的十七个）是为 third-part/Scintilla 的 win32
 * 层和 third-part/richedit41 补齐的：它们对 IME 的组合串、候选窗、状态窗、
 * 转换状态一整套都要链接到符号，而 swinx 之前只实现了最上面的五个「拿/还上下文」。
 * 语义同上——组合串不存在（长度 0），所有查询都回「没有 IME」，所有设置类请求
 * 都失败，调用方据此走纯键盘路径。
 */
#include <windows.h>
#include <imm.h>

HIMC WINAPI ImmCreateContext(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL ImmDestroyContext(HIMC hIMC)
{
    (void)hIMC;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

HIMC WINAPI ImmGetContext(HWND hWnd)
{
    (void)hWnd;
    return NULL;
}

HIMC WINAPI ImmAssociateContext(HWND hWnd, HIMC hIMC)
{
    (void)hWnd;
    (void)hIMC;
    return NULL;
}

BOOL WINAPI ImmReleaseContext(HWND hWnd, HIMC hIMC)
{
    (void)hWnd;
    (void)hIMC;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/* 没有组合串：返回 0 而不是负数，调用方（含 Scintilla）都把 <=0 当作「没有文本」。 */
LONG WINAPI ImmGetCompositionStringW(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    (void)lpBuf;
    (void)dwBufLen;
    return 0;
}

BOOL WINAPI ImmSetCompositionWindow(IN HIMC, _In_ LPCOMPOSITIONFORM lpCompForm)
{
    (void)lpCompForm;
    return FALSE;
}

BOOL WINAPI ImmSetCandidateWindow(IN HIMC, _In_ LPCANDIDATEFORM lpCandidate)
{
    (void)lpCandidate;
    return FALSE;
}

BOOL WINAPI ImmNotifyIME(IN HIMC, IN DWORD dwAction, IN DWORD dwIndex, IN DWORD dwValue)
{
    (void)dwAction;
    (void)dwIndex;
    (void)dwValue;
    return FALSE;
}

LRESULT WINAPI ImmEscapeW(HKL unnamedParam1, HIMC unnamedParam2, UINT unnamedParam3, LPVOID unnamedParam4)
{
    (void)unnamedParam1;
    (void)unnamedParam2;
    (void)unnamedParam3;
    (void)unnamedParam4;
    return 0;
}

BOOL WINAPI ImmSetCompositionFontW(IN HIMC, _In_ LPLOGFONTW lplf)
{
    (void)lplf;
    return FALSE;
}

BOOL WINAPI ImmSetCompositionStringW(IN HIMC, IN DWORD dwIndex, _In_reads_bytes_opt_(dwCompLen) LPVOID lpComp, IN DWORD dwCompLen, _In_reads_bytes_opt_(dwReadLen) LPVOID lpRead, IN DWORD dwReadLen)
{
    (void)dwIndex;
    (void)lpComp;
    (void)dwCompLen;
    (void)lpRead;
    (void)dwReadLen;
    return FALSE;
}

LONG WINAPI ImmGetCompositionStringA(IN HIMC, IN DWORD, __out_bcount_opt(dwBufLen) LPVOID lpBuf, IN DWORD dwBufLen)
{
    (void)lpBuf;
    (void)dwBufLen;
    return 0;
}

BOOL WINAPI ImmGetCandidateWindow(IN HIMC, IN DWORD, _Out_ LPCANDIDATEFORM lpCandidate)
{
    (void)lpCandidate;
    return FALSE;
}

BOOL WINAPI ImmSetCompositionFontA(IN HIMC, _In_ LPLOGFONTA lplf)
{
    (void)lplf;
    return FALSE;
}

/* 下面四个是「这个窗口有没有 IME」的查询：wasm 上恒为没有，调用方（richedit41
 * 的 ime.cpp、Scintilla 的 ScintillaWin）据此关掉 IME 分支。 */
DWORD WINAPI ImmGetProperty(HKL hKL, DWORD fdwIndex)
{
    (void)hKL;
    (void)fdwIndex;
    return 0;
}

BOOL WINAPI ImmGetOpenStatus(HIMC hIMC)
{
    (void)hIMC;
    return FALSE;
}

BOOL WINAPI ImmSetOpenStatus(HIMC hIMC, BOOL fOpen)
{
    (void)hIMC;
    (void)fOpen;
    return FALSE;
}

HWND WINAPI ImmGetDefaultIMEWnd(HWND hWnd)
{
    (void)hWnd;
    return NULL;
}

BOOL WINAPI ImmGetConversionStatus(HIMC hIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    (void)hIMC;
    if (lpfdwConversion)
        *lpfdwConversion = 0;
    if (lpfdwSentence)
        *lpfdwSentence = 0;
    return FALSE;
}

BOOL WINAPI ImmSetConversionStatus(HIMC hIMC, DWORD fdwConversion, DWORD fdwSentence)
{
    (void)hIMC;
    (void)fdwConversion;
    (void)fdwSentence;
    return FALSE;
}

BOOL WINAPI ImmIsIME(HKL hKL)
{
    (void)hKL;
    return FALSE;
}
