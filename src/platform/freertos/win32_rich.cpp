/*
 * win32_rich.cpp -- the Win32 entry points richedit41 needs that the FreeRTOS
 * shim does not model.
 *
 * The vendored text engine (third-part/richedit41) is a full Win32 program:
 * apart from CreateTextServices() it calls the clipboard, the OLE clipboard,
 * drag/drop, the IME, the profile API and kernel32's file API.  On a Windows
 * host build those resolve against user32/ole32/kernel32; a bare target has
 * none of them, so they have to be answered here.
 *
 * Scope of the port, stated up front:
 *   * clipboard    -- REAL.  Forwarded to the calling thread's SConnection,
 *                     whose SClipboard is a per-process in-memory clipboard
 *                     (SConnection.cpp:50-135).  Copy/paste inside the app
 *                     therefore works; pasting from outside obviously cannot.
 *   * IME          -- inert.  There is no input-method server, and swinx's
 *                     freertos imm.h layer already reports "no context".
 *   * OLE / DnD    -- inert.  IDataObject marshalling needs a COM apartment;
 *                     richedit's own OLE paths are compiled out
 *                     (see the -D list in components/soui/CMakeLists.txt), so
 *                     only the TOM Save/Open entry points reach them.
 *   * file API     -- inert.  richedit can only load/save RTF through an
 *                     IStream here; TOM's file-name variant returns failure
 *                     instead of pretending.
 *
 * Compiled by both bare-metal builds: the ESP-IDF component lists it in
 * components/soui/CMakeLists.txt, and the Cortex-M3 QEMU target lists it in
 * SOUI_FR_SWINX_PLATFORM_SRCS (cmake/SouiFreeRTOS.cmake).  The latter used to
 * exclude it (no consumer + a would-be OpenClipboard clash), but lite <edit>
 * (SEditLite.cpp) now calls DoDragDrop/ReleaseStgMedium, so the OLE/DnD stubs
 * here are required there too.  No duplicate symbols: gapstubs.cpp only has
 * the non-overlapping A/W variants (GlobalAlloc/Lock/Free, OutputDebugStringA).
 *
 * KEEP IN SYNC: signatures must match swinx/include/{sysapi,fileapi,imm,ole2}.h.
 */
#include <sfr_config.h>
#include <windows.h>
#include <imm.h>     // CANDIDATEFORM / COMPOSITIONFORM: not pulled in by windows.h

#include "SConnection.h"
#include "wndobj.h"

namespace
{
// The thread that owns the window: richedit always calls these from the UI
// thread, the same thread that owns the SConnection (see input.cpp for the
// same lookup).
SConnection *currentConn()
{
    return SConnMgr::instance()->getConnection(GetCurrentThreadId());
}
} // namespace

extern "C"
{

// ---- clipboard: forwarded to the thread's SConnection ----------------------
BOOL WINAPI OpenClipboard(HWND hWndNewOwner)
{
    SConnection *conn = currentConn();
    return conn ? conn->OpenClipboard(hWndNewOwner) : FALSE;
}

BOOL WINAPI CloseClipboard(void)
{
    SConnection *conn = currentConn();
    return conn ? conn->CloseClipboard() : FALSE;
}

BOOL WINAPI EmptyClipboard(void)
{
    SConnection *conn = currentConn();
    return conn ? conn->EmptyClipboard() : FALSE;
}

HWND WINAPI GetClipboardOwner(void)
{
    SConnection *conn = currentConn();
    return conn ? conn->GetClipboardOwner() : NULL;
}

HANDLE WINAPI GetClipboardData(UINT uFormat)
{
    SConnection *conn = currentConn();
    return conn ? conn->GetClipboardData(uFormat) : NULL;
}

HANDLE WINAPI SetClipboardData(UINT uFormat, HANDLE hMem)
{
    SConnection *conn = currentConn();
    return conn ? conn->SetClipboardData(uFormat, hMem) : NULL;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT format)
{
    SConnection *conn = currentConn();
    return conn ? conn->IsClipboardFormatAvailable(format) : FALSE;
}

// Static: the format table is process-wide, so this must work even before a
// connection exists (CTxtEdit registers its formats while being constructed).
UINT WINAPI RegisterClipboardFormatA(LPCSTR lpszFormat)
{
    return SClipboard::RegisterClipboardFormatA(lpszFormat);
}

// ---- OLE clipboard / drag & drop: not modelled -----------------------------
HRESULT WINAPI OleSetClipboard(IDataObject *)
{
    return E_NOTIMPL;
}

HRESULT WINAPI OleGetClipboard(IDataObject **ppdo)
{
    if (ppdo)
        *ppdo = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI OleFlushClipboard(void)
{
    return S_OK;
}

HRESULT WINAPI OleIsCurrentClipboard(IDataObject *)
{
    return S_FALSE;
}

void WINAPI ReleaseStgMedium(STGMEDIUM *pmedium)
{
    (void)pmedium;
}

HRESULT WINAPI DoDragDrop(LPDATAOBJECT, LPDROPSOURCE, DWORD dwOKEffects, LPDWORD pdwEffect)
{
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    (void)dwOKEffects;
    return DRAGDROP_S_CANCEL;
}

HRESULT WINAPI RevokeDragDrop(HWND)
{
    return S_OK;
}

// ---- IME: inert ------------------------------------------------------------
BOOL WINAPI ImmGetConversionStatus(HIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence)
{
    if (lpfdwConversion)
        *lpfdwConversion = 0;
    if (lpfdwSentence)
        *lpfdwSentence = 0;
    return FALSE;
}

BOOL WINAPI ImmGetOpenStatus(HIMC) { return FALSE; }
BOOL WINAPI ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
BOOL WINAPI ImmSetConversionStatus(HIMC, DWORD, DWORD) { return FALSE; }
DWORD WINAPI ImmGetProperty(HKL, DWORD) { return 0; }
BOOL WINAPI ImmIsIME(HKL) { return FALSE; }
HWND WINAPI ImmGetDefaultIMEWnd(HWND) { return NULL; }

LONG WINAPI ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD)
{
    return 0;   // 0 == "empty composition string" (a negative value is an error)
}

LONG WINAPI ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD)
{
    return 0;
}

BOOL WINAPI ImmSetCompositionStringW(HIMC, DWORD, LPVOID, DWORD, LPVOID, DWORD)
{
    return FALSE;
}

BOOL WINAPI ImmGetCandidateWindow(HIMC, DWORD, LPCANDIDATEFORM) { return FALSE; }
BOOL WINAPI ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM) { return FALSE; }
BOOL WINAPI ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return FALSE; }
BOOL WINAPI ImmSetCompositionFontA(HIMC, LPLOGFONTA) { return FALSE; }
BOOL WINAPI ImmNotifyIME(HIMC, DWORD, DWORD, DWORD) { return FALSE; }
LRESULT WINAPI ImmEscapeW(HKL, HIMC, UINT, LPVOID) { return 0; }

// ---- keyboard layout / misc ui ---------------------------------------------
HKL WINAPI ActivateKeyboardLayout(HKL hkl, UINT)
{
    return hkl;
}

UINT WINAPI GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    if (lpList && nBuff > 0)
        lpList[0] = NULL;
    return 1;
}

// GetDoubleClickTime 已下沉到 src/platform/common/sysapi.cpp（转发到
// SConnection::GetDoubleClickTime()）。原来这里有一份硬编码 500 的副本，与本文件
// 同目录 SConnection 里的 400 互相矛盾，等于同一个问题两个答案。
BOOL WINAPI MessageBeep(UINT) { return TRUE; }

// ---- kernel32 / profile ----------------------------------------------------
static const DWORD kInvalidFilePointer = (DWORD)-1;

HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)
{
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED)
{
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = 0;
    return FALSE;
}

BOOL WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED)
{
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;
    return FALSE;
}

DWORD WINAPI SetFilePointer(HANDLE, LONG, LONG *highword, DWORD)
{
    if (highword)
        *highword = 0;
    return kInvalidFilePointer;
}

UINT WINAPI GetProfileIntA(LPCSTR, LPCSTR, INT def_val)
{
    return (UINT)def_val;
}

INT WINAPI GetProfileSectionW(LPCWSTR, LPWSTR buffer, DWORD len)
{
    if (buffer && len)
        buffer[0] = 0;
    return 0;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR) { return NULL; }

DWORD WINAPI GetModuleFileNameW(HMODULE, LPWSTR lpFilename, DWORD nSize)
{
    if (lpFilename && nSize)
        lpFilename[0] = 0;
    return 0;
}

void WINAPI OutputDebugStringW(LPCWSTR) {}

// GetACP 已下沉到 src/platform/common/codepage.cpp（值由 swinx_ansi_codepage()
// 提供，实现在 freertos/sysapi.cpp —— 本文件在部分 target（soui_fr / swinx_test）
// 里不参与编译，把契约放在这里会让那些 target 链接失败）。

// ---- global heap helpers ---------------------------------------------------
UINT WINAPI GlobalFlags(HGLOBAL hMem)
{
    (void)hMem;
    return 0;
}

HLOCAL WINAPI GlobalHandle(LPCVOID pMem)
{
    return (HLOCAL)pMem;
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags)
{
    (void)uFlags;
    return (HGLOBAL)HeapReAlloc(GetProcessHeap(), 0, (LPVOID)hMem, dwBytes);
}

SIZE_T WINAPI GlobalSize(HGLOBAL hMem)
{
    return hMem ? HeapSize(GetProcessHeap(), 0, (LPCVOID)hMem) : 0;
}

BOOL WINAPI IsBadWritePtr(const void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return FALSE;
}

} // extern "C"
