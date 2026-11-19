/*
 * swinx/platform/wayland/utils.cpp
 *
 * MessageBeep 的 Wayland 实现（契约见 src/SwinxUtils.h）。
 *
 * Wayland 核心协议没有“系统铃声”这类请求：提示音要么交给桌面环境的通知
 * 服务（org.freedesktop.Notifications，D-Bus），要么由应用自己播放。阶段 0
 * 没有接入 D-Bus，因此按“能力不存在”显式失败，而不是伪造成功。
 */
#include <windows.h>

int swinx_messageBeep(UINT uType)
{
    (void)uType;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
