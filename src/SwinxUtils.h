#ifndef _SWINX_UTILS_H_
#define _SWINX_UTILS_H_

/* swinx 平台助手契约
 *
 * 声明在本文件，实现在 src/platform/<平台>/utils.*。同一个符号在多个平台目录里各有一份
 * 实现，构建时只有当前平台的目录进入源文件列表（见 linux.cmake / macos.cmake / ios.cmake
 * / mobile.cmake 里的 file(GLOB)），由链接器决定最终链接哪一份。因此调用方写的是同一个
 * 函数名，不需要任何平台 #ifdef 分支。
 *
 * 注意：这是 **实现细节，不是公共 API**——本文件位于 src/，不会随 include/ 安装出去。
 *
 * 返回类型用 int 而不是 BOOL：这些函数跨 TU 边界（实现在平台目录、调用方在 src 下的 cpp），
 * 而 swinx 的 BOOL 在 Objective-C++ 编译单元里是 bool / signed char（clang 预定义
 * __OBJC_BOOL_IS_BOOL）、在 .cpp 里是 int，两边不一致（见 include/ctypes.h）。
 */

#include <windows.h>

/* Windows MessageBeep 的平台实现，调用方见 src/sysapi.cpp。
 *
 * uType 为 0xFFFFFFFF(-1) 时 Windows 播放"简单提示音"，其余为 MB_ICON* 系列。各平台都
 * 只有一种系统提示音，没有与 MB_ICON* 一一对应的提示音集合，因此统一映射到系统提示音：
 *     Linux    X11 核心协议 Bell 请求（platform/linux/utils.cpp）
 *     macOS    NSBeep()（platform/cocoa/utils.mm）
 *     iOS      AudioServicesPlayAlertSound（platform/ios/utils.mm）
 *     移动端   转发宿主应用的平台回调 g_platformAPI.audio.messageBeep（platform/mobile/utils.cpp），
 *              由应用层用 Android ToneGenerator/RingtoneManager 或 OHOS AudioRenderer 发声；
 *              回调未注册时返回 FALSE
 * 返回 TRUE 表示提示音请求已交给系统，与 Win32"声音已入队即返回 TRUE"的语义一致。
 */
int swinx_messageBeep(UINT uType);

#endif // _SWINX_UTILS_H_
