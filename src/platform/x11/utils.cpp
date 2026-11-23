#include <windows.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include "SConnection.h"

// Windows MessageBeep 在 Linux 上的实现（契约见 SwinxUtils.h）。
//
// 使用 X11 核心协议的 Bell 请求（xcb_bell，等价于 Xlib 的 XBell），也就是 X11 的
// "系统铃声"。X11 没有与 Windows MB_ICON* 一一对应的提示音集合，因此所有 uType
// 统一播放系统铃声。
//
// 该请求不产生错误，也不等待回复；返回值只表示请求是否已成功发出（与 Win32 的
// "声音已入队即返回 TRUE" 语义一致）。铃声是否真的可闻取决于 X server 的 bell
// 设置（xset b），客户端无法控制——这是 X11 的既有行为。
int swinx_messageBeep(UINT uType)
{
    (void)uType;

    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn || !conn->connection)
        return FALSE;

    // percent = -1：使用 X server 的默认音量
    xcb_bell(conn->connection, -1);
    // 请求默认在 xcb 内部缓冲，立即冲刷以保证它被发出去
    xcb_flush(conn->connection);
    return TRUE;
}
