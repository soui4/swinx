/*
 * input.cpp -- FreeRTOS input injection (see input.h).
 *
 * Routing: the target window's owning thread is looked up through the
 * window map (_Window::tid), and the event is posted into THAT thread's
 * SConnection -- so a driver task can inject into the UI task's queue while
 * it is blocked in WaitMessage()).  Keyboard state, mouse button state and
 * cursor position are updated on the same (target) connection, keeping
 * GetKeyState / GetAsyncKeyState / GetCursorPos consistent with the
 * delivered messages.
 */
#include "input.h"
#include "SConnection.h"
#include "wndobj.h"

// Win32: GetAsyncKeyState(VK_xBUTTON) reports the physical button state no
// matter which window has capture; richedit41 (CTxtEdit::OnTxMouseMove) and
// drag-drop loops gate selection extension on it.  A touch/driver layer only
// injects messages, so keep the injected button bits in sync here, the same
// way SwinxInjectKey maintains key state.
static void SyncMouseButtonState(SConnection *conn, UINT message)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN:
        conn->SetKeyDown(VK_LBUTTON, TRUE);
        break;
    case WM_LBUTTONUP:
    case WM_NCLBUTTONUP:
        conn->SetKeyDown(VK_LBUTTON, FALSE);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN:
        conn->SetKeyDown(VK_MBUTTON, TRUE);
        break;
    case WM_MBUTTONUP:
    case WM_NCMBUTTONUP:
        conn->SetKeyDown(VK_MBUTTON, FALSE);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN:
        conn->SetKeyDown(VK_RBUTTON, TRUE);
        break;
    case WM_RBUTTONUP:
    case WM_NCRBUTTONUP:
        conn->SetKeyDown(VK_RBUTTON, FALSE);
        break;
    default:
        break;
    }
}

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

    SyncMouseButtonState(conn, message);

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
