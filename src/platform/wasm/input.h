/*
 * input.h -- WebAssembly input injection (JS host / test facing).
 *
 * On a real platform the OS input server injects keyboard/mouse events into
 * the window message queue.  In a browser there is no input server either:
 * the JS host translates DOM pointer / keyboard events into these calls.  They
 * update the per-connection keyboard state / cursor position and post the
 * Win32 messages into the target window's owning thread queue via
 * SConnection::postMsg(), which wakes a blocked WaitMessage()/waitMsg() on
 * that thread (queue mutex + auto-reset event, same as the other non-Windows
 * ports).
 *
 * Coordinates are SCREEN coordinates; SwinxInjectMouse converts them to the
 * client coordinates of the target window before posting (like real input
 * routing).  This port's virtual windows have client origin == window origin
 * (no NC area), so client = screen - wndObj->rc.origin.  hWnd == NULL posts
 * to the calling thread's connection without conversion.
 */
#ifndef _SWINX_WASM_INPUT_H_
#define _SWINX_WASM_INPUT_H_

#include <windows.h>

// post a mouse event: WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_LBUTTONUP /
// WM_RBUTTONDOWN / WM_RBUTTONUP / WM_MBUTTONDOWN / WM_MBUTTONUP.
// (x, y) are screen coordinates; fwKeys are the MK_* key flags.
void SwinxInjectMouse(HWND hWnd, UINT message, int x, int y, WPARAM fwKeys);

// post a keyboard event: WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN /
// WM_SYSKEYUP.  Updates the connection keyboard state so GetKeyState /
// GetAsyncKeyState / GetKeyboardState reflect the injected keys.  The core
// pump's TranslateMessage() turns ASCII key-downs into WM_CHAR.
void SwinxInjectKey(HWND hWnd, UINT message, UINT vk, UINT scanCode);

// post IME-composed (committed) text.  pszUtf8 is a UTF-8 string; each Unicode
// code point in it becomes one WM_IME_CHAR, which is how Win32 delivers text an
// input method produced -- the same shape as the Android/OHOS sendImeString
// bridge and the cocoa/iOS ports.
//
// No keyboard state is touched and no WM_KEYDOWN/WM_CHAR pair is synthesized:
// a composition commit is not a physical key press, and the WM_CHAR that
// TranslateMessage() would derive from a key can only ever be a layout guess
// (see SConnection::vkToChar).  WM_IME_CHAR carries the code point itself, so
// receivers that store wide text (richedit41, Scintilla) get the real
// character; controls that only read WM_CHAR keep the keyboard path unchanged.
void SwinxInjectText(HWND hWnd, const char *pszUtf8);

#endif // _SWINX_WASM_INPUT_H_
