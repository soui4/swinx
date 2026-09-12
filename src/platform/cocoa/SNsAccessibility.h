#ifndef _SNSACCESSIBILITY_H_
#define _SNSACCESSIBILITY_H_

/* swinx MSAA (IAccessible) <-> macOS NSAccessibility 桥接（胶水层）
 *
 * SOUI 的窗口内容整体绘制在一个 NSView（SNsWindow）里，macOS 的辅助功能
 * （VoiceOver 等）默认只能看到一个不透明的大视图。本模块把 swinx 侧由
 * IAccessible 暴露的控件树映射成 NSAccessibility 元素树，挂到内容视图下：
 *
 *   NSWindow -> contentView(SNsWindow)
 *                  └─ SwinxAccElement(根：OBJID_CLIENT / OBJID_WINDOW)
 *                        └─ SwinxAccElement ...（递归）
 *
 * 架构约定（与 Linux 的 AT-SPI 桥一致）：**swinx 不持有 IAccessible**。
 * SwinxAccElement 只是"身份壳"——只记 (hwnd, child id 链)，不 AddRef 任何
 * COM 接口。系统的每次属性/动作访问都通过 SwinxAccResolvePath（swinx 内部
 * 助手，见 src/SwinxAccGlue.h，不属公共 API 面）重新向窗口发 WM_GETOBJECT
 * （SHostWnd 按 SOUI_ENABLE_ACC 决定返回 NULL 还是 IAccessible），拿到接口
 * 现查现用现放。控件销毁只会让解析失败（表现为空属性），绝不会访问悬垂指针。
 *
 * 身份壳按需构建并缓存，用于维持 NSAccessibility 对象身份稳定（VoiceOver
 * 依赖同一元素对象追踪焦点）；结构变化事件只丢弃壳缓存，无 COM 泄漏。
 *
 * 坐标：MSAA accLocation 返回 swinx 全局像素坐标（主屏左上角为原点、y 向下），
 * 与 SNsCoord.h 的换算保持一致后转成 Cocoa 屏幕坐标（point、y 向上）。
 *
 * 注意：本头文件只被 .mm（Objective-C++）包含——IAccessible 是 C++ 类型。
 *
 * include 顺序约束（macOS 编译的关键）：
 *  - Cocoa 必须先于 swinx 头导入：swinx 的 gdi.h 有 "#define Polygon
 *    Polygon_Priv"（避开 Quickdraw 的 Polygon typedef），若先于 Cocoa 定义，
 *    Quickdraw.h 的 "typedef MacPolygon Polygon" 会被宏破坏而编译失败
 *    （SNsWindow.mm 先 #import Cocoa 再 include swinx 头，同一约束）。
 *  - swinx 的 COM 头（basetyps.h）把 interface 宏定义为 struct，而 macOS
 *    SDK 的头文件用到 interface 这个词——进入 ObjC 世界前必须 #undef。
 */
#import <Cocoa/Cocoa.h>
#include <windows.h>
#include <oleacc.h>
#undef interface
/* 一个可访问元素的"身份壳"。元素位置由 (hwnd, childId 链) 唯一确定：
 * 链为空 = 窗口客户区根；链 [i, j, ...] = 根的第 i 个孩子的第 j 个孩子……
 * （MSAA child id 从 1 开始，按枚举序编号）。 */
@interface SwinxAccElement : NSAccessibilityElement

- (instancetype)initWithHwnd:(HWND)hwnd
                       chain:(NSArray<NSNumber *> *)chain
                      parent:(id)parent;
- (instancetype)init NS_UNAVAILABLE;

@property (nonatomic, readonly) HWND swinxHwnd;
@property (nonatomic, readonly) NSArray<NSNumber *> *swinxChain;

/* 丢弃缓存的子元素壳（结构变化后重新构建；子壳不持 COM 引用，丢弃零成本） */
- (void)swinxInvalidateChildren;

@end

/* 注册 WinEvent 钩子（惰性、幂等）。返回是否已注册。 */
BOOL SwinxNsAccInit(void);

/* 取 hwnd 的根元素壳；窗口未提供 IAccessible 时返回 nil。
 * 第一次调用会顺带完成钩子注册。 */
SwinxAccElement *SwinxNsAccRootElement(HWND hwnd);

/* 内容视图的 accessibilityChildren 入口：返回根元素壳（并把它挂到 parent 下）。
 * 由 SNsWindow 的 -accessibilityChildren 调用。 */
NSArray *SwinxNsAccChildrenForHwnd(HWND hwnd, id parent);

/* 把 parent（内容视图）挂为 hwnd 根元素壳的 AXParent（幂等）。壳元素的祖先链
 * 必须终止于 AppKit 原生锚点（NSView/NSWindow/NSApp）：AppKit 解析元素时要沿
 * accessibilityParent 上溯找锚点，中途遇到 nil 会解析失败，VoiceOver 会无限
 * 重试同一批属性查询（表现为应用被 AX 查询洪水钉死）。hit test 等不经过
 * -accessibilityChildren 的路径必须在查询前调用本函数。 */
void SwinxNsAccAttachRootParent(HWND hwnd, id parent);

/* 鼠标命中测试入口（VoiceOver 鼠标跟随朗读的关键路径）：screenPoint 为 Cocoa
 * 屏幕坐标（point、原点左下），转成 swinx 像素坐标后在 IAccessible 树上逐级
 * accHitTest 下钻，返回最深元素壳（保持 parent 链与对象身份稳定）。
 * 窗口未提供 IAccessible 时返回 nil，由调用方回退默认实现。 */
id SwinxNsAccHitTest(HWND hwnd, NSPoint screenPoint);

/* 丢弃 hwnd 的元素壳缓存（窗口销毁时调用） */
void SwinxNsAccInvalidate(HWND hwnd);

#endif // _SNSACCESSIBILITY_H_
