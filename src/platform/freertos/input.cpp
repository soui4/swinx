/*
 * input.cpp -- FreeRTOS input injection (see input.h).
 *
 * Routing: the target window's owning thread is looked up through the
 * window map (_Window::tid), and the event is posted into THAT thread's
 * SConnection -- so a driver task can inject into the UI task's queue while
 * it is blocked in WaitMessage().  Keyboard state and cursor position are
 * updated on the same (target) connection, keeping GetKeyState /
 * GetCursorPos consistent with the delivered messages.
 */
#include "input.h"
#include "SConnection.h"
#include "wndobj.h"

void SwinxInjectMouse(HWND hWnd, UINT message, int x, int y, WPARAM fwKeys)
{
    tid_t tid = 0;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        tid = wndObj->tid;
    SConnection *conn = SConnMgr::instance()->getConnection(tid);
    if (!conn)
        return;

    // update the (virtual) cursor position in screen coordinates
    conn->SetCursorPos(x, y);

    // screen -> client (client origin == window origin on this port)
    if (wndObj)
    {
        x -= wndObj->rc.left;
        y -= wndObj->rc.top;
    }
    conn->postMsg(hWnd, message, fwKeys, MAKELPARAM(x, y));
}

void SwinxInjectKey(HWND hWnd, UINT message, UINT vk, UINT scanCode)
{
    tid_t tid = 0;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        tid = wndObj->tid;
    SConnection *conn = SConnMgr::instance()->getConnection(tid);
    if (!conn)
        return;

    bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    conn->SetKeyDown((UINT)(vk & 0xff), down);

    // Win32 key message lParam: repeat count | scan code << 16, with the
    // transition (bit 30) and previous-state (bit 31) flags set on key-up
    LPARAM lp = 1 | ((LPARAM)(scanCode & 0xff) << 16);
    if (!down)
        lp |= (LPARAM)0xC0000000;
    conn->postMsg(hWnd, message, (WPARAM)vk, lp);
}
