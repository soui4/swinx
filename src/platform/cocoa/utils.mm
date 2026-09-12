#import <Cocoa/Cocoa.h>
#include <windows.h>
#include "SwinxUtils.h"

// Windows MessageBeep 在 macOS 上的实现（契约见 SwinxUtils.h）。
//
// 使用 Cocoa 的 NSBeep()，播放用户在"系统设置 - 声音"中配置的提示音——这正是
// Windows MessageBeep 在 macOS 上的对应语义。macOS 没有与 MB_ICON* 一一对应的
// 提示音集合，因此所有 uType 统一播放系统提示音。
//
// NSBeep 无返回值，调用即已把声音交给系统播放，因此这里返回 TRUE。
int swinx_messageBeep(UINT uType)
{
    (void)uType;
    NSBeep();
    return TRUE;
}
