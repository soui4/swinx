/*
 * input.cpp -- WebAssembly input injection (see input.h).
 *
 * Routing: the target window's owning thread is looked up through the
 * window map (_Window::tid), and the event is posted into THAT thread's
 * SConnection -- so the JS host may inject from whichever thread its event
 * handler runs on, and the messages still land in the queue the UI thread is
 * blocked in.  Keyboard state and cursor position are updated on the same
 * (target) connection, keeping GetKeyState / GetCursorPos consistent with the
 * delivered messages.
 */
#include "input.h"
#include "SConnection.h"
#include "wndobj.h"
#include <stdio.h>

// Win32: GetAsyncKeyState(VK_xBUTTON) reports the physical button state no
// matter which window has capture; richedit41 (CTxtEdit::OnTxMouseMove) and
// drag-drop loops gate selection extension on it.  The JS host injects
// messages, so keep the injected button bits in sync here.
//
// fwKeys（页面从 e.buttons 算出）是注入时刻**物理按键状态**的权威来源：
// 若只按消息类型同步，pointerup 在画布外丢失时（鼠标移出 canvas 后释放，
// pointerup 落在别的元素上）VK_LBUTTON 会永远粘在按下态，richedit 的
// GetAsyncKeyState(VK_LBUTTON)<0 恒真 —— 弹起后移动鼠标仍在扩展选区。
// 以 fwKeys 对账则下一条 mousemove（buttons=0）就能把状态纠正回来。
static void SyncMouseButtonState(SConnection *conn, UINT message, WPARAM fwKeys)
{
    (void)message;
    conn->SetKeyDown(VK_LBUTTON, (fwKeys & MK_LBUTTON) != 0);
    conn->SetKeyDown(VK_RBUTTON, (fwKeys & MK_RBUTTON) != 0);
    conn->SetKeyDown(VK_MBUTTON, (fwKeys & MK_MBUTTON) != 0);
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

    SyncMouseButtonState(conn, message, fwKeys);

    // Win32 路由语义：鼠标消息不是"投给注入者指定的窗口"，而是
    //   1) 有窗口持有 capture（下拉窗 SetCapture 打开期间）→ 全部投给 capture 窗
    //      （坐标可以是负的/超出客户区，Win32 就是这样，靠它实现"点外面关闭"）；
    //   2) 否则投给光标下最顶层的可见窗口（WindowFromPoint，z 序命中）。
    // 注入参数里的 hWnd 只用来挑连接（线程）。主窗全屏时第 2 条命中的仍是
    // 主窗，行为与单窗口时期一致；弹出窗盖住的区域则命中弹出窗。
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
    // Lock keys: Win32 keeps a toggle bit (the low bit GetKeyState returns)
    // that flips on each key-DOWN, independent of the up/down transition bit.
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
// Overlong forms, surrogates and anything past U+10FFFF are rejected rather than
// turned into a plausible-looking code point: a wrong character in a text buffer
// is far harder to notice than a missing one.
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
    // 2-byte form must not encode < U+0080, 3-byte must not encode < U+0800,
    // 4-byte must not encode < U+10000 (overlong), and U+D800..U+DFFF / beyond
    // U+10FFFF are not characters at all.
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
