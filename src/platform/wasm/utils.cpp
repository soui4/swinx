/*
 * swinx/platform/wasm/utils.cpp
 *
 * MessageBeep 的 wasm 实现（契约见 src/SwinxUtils.h）。
 *
 * 浏览器里没有"系统提示音"：发声要走 WebAudio 的 AudioContext，而 AudioContext
 * 在用户手势之前处于 suspended 状态，需要宿主页面在点击回调里 resume()。swinx
 * 不持有页面上下文，也无从得知下一次手势何时到来，因此这里和其它缺失能力一样
 * 显式失败（返回 FALSE），而不是假装入队成功。
 *
 * 补齐路径：仿照 framebuffer.h 的 SwinxFbPresent，在 JS 侧提供
 * `Module.swinxAudioBeep()`，由宿主页面用它自己的 AudioContext 发声；swinx 只在
 * 这里转发 uType 并按其是否注册返回 TRUE/FALSE。
 */
#include <windows.h>

int swinx_messageBeep(UINT uType)
{
    (void)uType;
    return FALSE;
}
