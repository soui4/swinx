/*
 * input.h -- FreeRTOS input injection stubs (driver / test facing).
 *
 * On a real platform the OS input server injects keyboard/mouse events into
 * the window message queue.  On FreeRTOS there is no input server: button /
 * touch / keypad drivers (and the tests) call these functions instead.  They
 * update the per-connection keyboard state / cursor position and post the
 * Win32 messages into the target window's owning thread queue via
 * SConnection::postMsg(), which wakes a blocked WaitMessage()/waitMsg() on
 * that thread (cross-task safe: queue mutex + auto-reset winobjs event).
 *
 * Coordinates are SCREEN coordinates; SwinxInjectMouse converts them to the
 * client coordinates of the target window before posting (like real input
 * routing).  This port's virtual windows have client origin == window origin
 * (no NC area), so client = screen - wndObj->rc.origin.  hWnd == NULL posts
 * to the calling thread's connection without conversion.
 */
#ifndef _SWINX_FREERTOS_INPUT_H_
#define _SWINX_FREERTOS_INPUT_H_

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

// post IME text: one WM_IME_CHAR per UTF-8 code point.  This is the path a
// software keyboard (or any IME / clipboard paste) must use -- it does NOT
// touch keyboard state and does NOT synthesize WM_KEYDOWN/WM_CHAR pairs.
// SConnection::TranslateMessage() on this port only maps US-layout VKs and
// deliberately has no IME, so non-ASCII input cannot go through InjectKey.
// SEditLite/SRichEdit already handle WM_IME_CHAR on every platform.
void SwinxInjectText(HWND hWnd, const char *pszUtf8);

// Focus-driven software keyboard. SEditLite / SRichEdit call
// ShowSoftKeyboard() when an edit gains or loses focus; on this port that
// call is routed to the handler registered here (normally the shared
// CSoftKeyboard component). Pass NULL to unregister. One active handler.
typedef BOOL (*SwinxSoftKeyboardHandler)(HWND hWnd, BOOL bShow);
void SwinxSetSoftKeyboardHandler(SwinxSoftKeyboardHandler handler);

#endif // _SWINX_FREERTOS_INPUT_H_
