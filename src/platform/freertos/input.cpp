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

    // Win32 路由语义（与 platform/wasm/input.cpp 一致）：鼠标消息不是"投给注入者
    // 指定的窗口"，而是
    //   1) 有窗口持有 capture（下拉窗 SetCapture 打开期间）→ 全部投给 capture 窗，
    //      坐标按该窗换算（可以是负值/超出客户区，Win32 就是这样，SDropDownWnd
    //      靠它实现"点外面关闭"）；
    //   2) 否则才投给参数指定的窗口 —— 本端口还没有 z 序命中
    //      （SConnection::WindowFromPoint 是返回 NULL 的桩）。
    // 只做 capture 这一支就够覆盖下拉列表：没有它，SComboBox 的列表窗收不到点击，
    // 下拉一开就再也关不掉。
    HWND hTarget = hWnd;
    HWND hCapture = conn->GetCapture();
    if (hCapture)
    {
        WndObj capObj = WndMgr::fromHwnd(hCapture);
        if (capObj && !capObj->bDestroyed)
            hTarget = hCapture;
        else
            conn->ReleaseCapture(); // capture 窗已销毁：清掉悬空的 capture
    }
    if (hTarget == hWnd)
    {
        POINT pt = { x, y };
        HWND hUnder = conn->WindowFromPoint(pt);
        if (hUnder)
            hTarget = hUnder;
    }

    WndObj tgtObj = WndMgr::fromHwnd(hTarget);
    if (!tgtObj || tgtObj->bDestroyed)
        return;

    // screen -> client (client origin == window origin on this port)
    x -= tgtObj->rc.left;
    y -= tgtObj->rc.top;
    conn->postMsg(hTarget, message, fwKeys, MAKELPARAM(x, y));
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
    // Lock keys: Win32 keeps a toggle bit (the low bit of GetKeyState) that
    // flips on each key-DOWN, independent of the up/down transition bit.
    // TranslateMessage needs it to decide the case of a letter.
    if (down && (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL))
        conn->ToggleKeyState((UINT)(vk & 0xff));

    // Win32 key message lParam: repeat count | scan code << 16, with the
    // transition (bit 30) and previous-state (bit 31) flags set on key-up
    LPARAM lp = 1 | ((LPARAM)(scanCode & 0xff) << 16);
    if (!down)
        lp |= (LPARAM)0xC0000000;
    conn->postMsg(hWnd, message, (WPARAM)vk, lp);
}

// Decode one UTF-8 sequence.  Returns the number of bytes consumed (0 ends the
// string, -1 means "this byte is not a valid start" and the caller skips it).
// Overlong forms, surrogates and anything past U+10FFFF are rejected rather
// than turned into a plausible-looking code point: a wrong character in a text
// buffer is far harder to notice than a missing one.
// Kept in sync with platform/wasm/input.cpp's nextCodePoint().
static int nextCodePoint(const char *psz, unsigned int *pCp)
{
    unsigned char b0 = (unsigned char)psz[0];
    if (b0 == 0)
        return 0;
    unsigned int cp;
    int need;
    if (b0 < 0x80)
    {
        *pCp = b0;
        return 1;
    }
    else if ((b0 & 0xE0) == 0xC0)
    {
        cp = b0 & 0x1Fu;
        need = 1;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        cp = b0 & 0x0Fu;
        need = 2;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        cp = b0 & 0x07u;
        need = 3;
    }
    else
        return -1; // a continuation byte on its own, or 0xF8..

    for (int i = 1; i <= need; ++i)
    {
        unsigned char b = (unsigned char)psz[i];
        if ((b & 0xC0) != 0x80)
            return -1;
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return -1;
    *pCp = cp;
    return need + 1;
}

void SwinxInjectText(HWND hWnd, const char *pszUtf8)
{
    if (!pszUtf8)
        return;
    tid_t tid = 0;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        tid = wndObj->tid;
    SConnection *conn = SConnMgr::instance()->getConnection(tid);
    if (!conn)
        return;

    const char *p = pszUtf8;
    unsigned int cp = 0;
    for (;;)
    {
        int n = nextCodePoint(p, &cp);
        if (n == 0)
            break; // end of string
        if (n < 0)
        {
            ++p; // not a sequence start: drop the byte and keep going
            continue;
        }
        // lParam 0: Win32 leaves it undefined for IME characters (the repeat /
        // scan-code fields describe a key press, which this is not).
        conn->postMsg(hWnd, WM_IME_CHAR, (WPARAM)cp, 0);
        p += n;
    }
}
