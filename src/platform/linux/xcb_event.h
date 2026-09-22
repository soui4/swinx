#pragma once

//=====================================================================
// xcb_send_event() 的 32 字节陷阱 —— 发送 xcb 事件的唯一正确入口
//=====================================================================
// X11 协议中每个事件在线上恒为 32 字节，因此 libxcb 的 xcb_send_event()
// 会无条件 memcpy 32 字节（见 xcb_send_event(3) 手册，以及
// https://bugs.freedesktop.org/show_bug.cgi?id=99946）。
// 而多数 xcb_*_event_t 结构体本身不足 32 字节：
//   xcb_expose_event_t           20 字节
//   xcb_unmap_notify_event_t     16 字节
//   xcb_selection_notify_event_t 24~32 字节（随 xcb 版本而异）
//   xcb_client_message_event_t   32 字节（恰好够，但 data.data32[] 可能只填了一部分）
// 若直接把结构体地址交给 xcb_send_event()，libxcb 会读过对象尾部，把未初始化
// 的栈字节当成事件内容发给 X server，valgrind 报为：
//   Syscall param writev(vector[...]) points to uninitialised byte(s)
//   ... xcb_flush ... <调用点>
// 除噪声外这也是信息泄漏：栈上的指针值可能被其它 X client 读到
// （KWin MR !1400、LyX 19c41bd、Qt QTBUG-56518 均为同一问题）。
//
// 因此所有事件发送一律走 xcb_send_event32()：它先把事件复制进一个 32 字节的
// 零缓冲区再发送，缓冲区尾部恒为 0。
// 注意：结构体自身的 padding 字段（pad0/pad1...）仍需调用方用 `= {}` 初始化，
// 因为 memcpy 会连同 padding 一起复制。
//=====================================================================
#include <xcb/xcb.h>

template <typename T>
inline void xcb_send_event32(xcb_connection_t *c,
                             uint8_t propagate,
                             xcb_window_t destination,
                             uint32_t event_mask,
                             const T &event)
{
    static_assert(sizeof(T) <= 32, "xcb event struct must not exceed 32 bytes");
    char buf[32] = {};
    memcpy(buf, &event, sizeof(T));
    xcb_send_event(c, propagate, destination, event_mask, buf);
}
