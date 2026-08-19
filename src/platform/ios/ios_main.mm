// ============================================================================
//  swinx iOS 应用入口（所有 Objective-C 代码都集中在这里，swinx 内部实现）
//
//  设计说明：
//  1. UIApplicationMain 是阻塞调用，启动 main runloop 并永不返回。
//     因此不能在同一个 main() 中先调用 UIApplicationMain 再调用 _tWinMain。
//  2. 方案：AppDelegate 的 didFinishLaunching 通过 dispatch_async 异步调用
//     宿主程序提供的 _tWinMain。此时 UIApplication 已创建，UIKit 事件系统就绪。
//  3. _tWinMain 中的 GetMessage 循环通过 CFRunLoopRunInMode 运行 main runloop，
//     替代 UIApplicationMain 的 CFRunLoopRun()，从而接收 UIKit 触摸事件。
//  4. 对外暴露 C API swinx_ios_entry()，应用层纯 C++ 通过 swinx_entry.h 调用。
// ============================================================================

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <windows.h>
#include <ios_entry.h>
#undef interface   // 防止 basetyps.h 中 #define interface struct 与 ObjC @interface 冲突
#include <stdio.h>

// 宿主程序（应用层 C++）必须实现此函数

static funIosMain s_iosMain = nil;
// ---------------------------------------------------------------------------
// SwinxAppDelegate：iOS 应用生命周期事件接收
// ---------------------------------------------------------------------------
@interface SwinxAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

@implementation SwinxAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // 使用 dispatch_async（与 ios_basic_demo 一致），让 didFinishLaunching 先返回。
    // _tWinMain 内部的消息循环通过 CFRunLoopRunInMode 驱动 main runloop。
    dispatch_async(dispatch_get_main_queue(), ^{
        if(s_iosMain){
            HINSTANCE hInst = GetModuleHandle(NULL);
            int ret = s_iosMain(hInst, 0, NULL, SW_SHOWNORMAL);
            exit(ret);
        }else{
            exit(-1);
        }
    });
    return YES;
}

@end

// ---------------------------------------------------------------------------
// 对外 C API：由应用层 C++ main()（通过 swinx_entry.h 中的宏）调用。
// 所有 UIApplicationMain / AppDelegate / UIKit 生命周期逻辑都封装在这里。
// ---------------------------------------------------------------------------
extern "C"
int swinx_ios_entry(int argc, char *argv[],funIosMain iosMain) {
    @autoreleasepool {
        s_iosMain = iosMain;
        // 第四个参数：principal class=nil（默认 UIApplication），delegate class=SwinxAppDelegate
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([SwinxAppDelegate class]));
    }
}
