#ifndef _SNSCOORD_H_
#define _SNSCOORD_H_

/* macOS 全局坐标换算（swinx 像素空间 <-> Cocoa point 空间）
 *
 * swinx 约定（与多显示器实现一致）：全局坐标以**物理像素**为单位，原点在主屏
 * 左上角；Cocoa 全局坐标以 **point** 为单位，原点在主屏左下角。屏 S 的
 * backingScaleFactor 为 k、主屏高度为 H1(point) 时：
 *
 *     Cocoa(gx, gy)  ->  swinx(gx * k, (H1 - gy) * k)
 *
 * 这些函数原先是 SNsWindow.mm 的 static 实现，现提取为共享符号供窗口管理与
 * 无障碍桥接共用，避免出现第二套（容易在副屏/混合 scale 下出错的）换算。
 */
#import <Cocoa/Cocoa.h>
#include <windows.h>

/* 注意：这些函数只有 .mm（Objective-C++）实现方，按 C++ 链接导出，
 * 因此不要包 extern "C"；若将来需要纯 C++ 使用方再单独加适配。 */

    /* 主屏高度（point）；screens 中没有原点为 (0,0) 的屏时回退 screens[0] */
    CGFloat swinxNsPrimaryHeight(void);

    /* 找包含 Cocoa 全局点的屏；找不到（菜单栏/Dock 边缘）回退 mainScreen */
    NSScreen *swinxNsScreenForCocoaPoint(NSPoint gp);

    /* Cocoa 全局点(point) -> swinx 全局点(物理像素) */
    void swinxNsWinPointFromCocoa(NSPoint gp, POINT *ppt);

    /* swinx 全局点(物理像素) -> Cocoa 全局点(point) */
    NSPoint swinxNsCocoaPointFromWin(POINT pt);

#endif //_SNSCOORD_H_
