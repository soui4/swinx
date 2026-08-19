#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/runtime.h>
#include <map>
#include <set>
#include <mutex>
#include <assert.h>
#include <windows.h>
#include "SUIWindow.h"
#include "SConnection.h"
#include "SUIDataObjectProxy.h"
#include "wndobj.h"
#include "keyboard.h"
#include "tostring.hpp"
#include <uimsg.h>
#include <cursorid.h>
#include "log.h"
#include <stdio.h>

#undef interface
#define kLogTag "SUIWindow"

// iOS 坐标系为左上角原点（与 Windows 一致），无需像 macOS 那样翻转 Y 轴。
// 但 swinx 内部使用物理像素坐标，需乘以 UIScreen.scale。

#define float2int(x) (int)floor((x)+0.5f)

static RECT CGRect2Rect(CGRect r)
{
    RECT ret;
    ret.left = float2int(r.origin.x);
    ret.top = float2int(r.origin.y);
    ret.right = float2int(r.origin.x + r.size.width);
    ret.bottom = float2int(r.origin.y + r.size.height);
    return ret;
}

class SUIViewMgr{
public:
    SUIViewMgr(){}
    ~SUIViewMgr(){}

    BOOL add(HWND hWnd){
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        return m_hWndSet.insert(hWnd).second;
    }
    BOOL remove(HWND hWnd){
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        return m_hWndSet.erase(hWnd) > 0;
    }
    BOOL contains(HWND hWnd){
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        return m_hWndSet.find(hWnd) != m_hWndSet.end();
    }
private:
    std::set<HWND> m_hWndSet;
    std::recursive_mutex m_mutex;
};

static SUIViewMgr s_hWndMgr;

// --- 全局软键盘状态（通过 UIKeyboard 通知维护）---
static std::mutex s_kbLock;
static BOOL  s_kbVisible = NO;
static int   s_kbHeightPhys = 0;  // 物理像素

// --- 焦点转移 TLS 锁（切断 setUiFocusWindow ⇄ become/resignFirstResponder 递归）---
// 进入 setUiFocusWindow 时置 true，退出时置 false；
// becomeFirstResponder / resignFirstResponder 内部看到为 true 时，不再触发
// SConnection::SetFocus，避免 SConnection::SetFocus → setUiFocusWindow →
// become → SetFocus 的循环递归。
static thread_local BOOL g_souiInFocusTransfer = NO;

// 注：焦点窗口统一由 SConnection::m_hFocus 维护（见 SConnection.h/m_hFocus），
// 这里不再自行维护 s_focusHwnd，避免双份状态不一致。

// --- UITextPosition 关联对象用的全局唯一 key（用其自身地址）---
static const void *kSouiTextPositionIndexKey = &kSouiTextPositionIndexKey;

@class SUIView;

SUIView *getUiView(HWND hWnd){
    if(!s_hWndMgr.contains(hWnd))
        return nil;
    return (__bridge SUIView *)(void*)hWnd;
}

BOOL IsUiWindow(HWND hWnd){
    return getUiView(hWnd) != nil;
}

// SUIView：UIView 子类，作为 HWND 的载体。
// 负责：绘制（cairo）、触摸->鼠标映射、键盘、文本输入。
// 协议升级：从 UIKeyInput → UITextInput，提供完整的 markedText / selectedRange /
// inputDelegate 支持，确保中文等 IME 组合输入流程正确。
@interface SUIView : UIView <UITextInput>{
    @public
    CGRect m_rcPos;
    HWND   m_hWnd;
}
@property (nonatomic, strong) UIWindow *hostWindow;
@property (nonatomic, assign) SConnBase *listener;
@property (nonatomic, assign) BOOL bAutoDblClick;
@property (nonatomic, assign) BYTE byAlpha;
@property (nonatomic, assign) BOOL bMsgTransparent;
@property (nonatomic, assign) BOOL bEnabled;
@property (nonatomic, assign) BOOL bImeEnabled;
@property (nonatomic, strong) UITapGestureRecognizer *doubleTapRecognizer;
@property (nonatomic, strong) UILongPressGestureRecognizer *longPressRecognizer;
@property (nonatomic, assign) NSTimeInterval lastTapTime;
@property (nonatomic, assign) CGPoint lastTapPoint;

#pragma mark - UITextInput 存储字段
/** 当前组合文本（marked text），nil 表示没有进行中的组合输入。 */
@property (nonatomic, copy)   NSString *markedText;
/** 组合文本在整体文本中的范围，无 marked 时为 (NSNotFound, 0)。 */
@property (nonatomic, assign) NSRange  markedRange;
/** 选中文本范围，默认 (0,0) —— SUI 自己绘制光标，这里只做协议存储。 */
@property (nonatomic, assign) NSRange  selectedRange;
/** UITextInput 委托（系统设置，用于文本变化回调）。 */
@property (nonatomic, weak) id<UITextInputDelegate> inputDelegate;
/** marked text 绘制样式（协议要求，可为 nil，使用默认下划线样式）。 */
@property (nonatomic, copy)   NSDictionary<NSAttributedStringKey, id> *markedTextStyle;
/** 文本位置 token 映射：简单用整数索引 → UITextPosition。 */
@property (nonatomic, strong) NSMutableDictionary<NSNumber*, UITextPosition*> *textPositions;

- (instancetype)initWithFrame:(CGRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent withDblClick:(BOOL)bAutoDblClick;
- (void)destroy;
- (void)onActive:(BOOL)isActive;
- (void)setAlphaValue:(BYTE)byAlpha;
- (BYTE)getAlphaValue;
- (void)updateRect:(CGRect)rc;
- (void)invalidRect:(CGRect)rc;
- (void)onStateChange:(int)nState;
- (void)setViewEnabled:(BOOL)bEnabled;
- (BOOL)isImeEnabled;
- (void)setImeEnabled:(BOOL)bEnabled;
/** 显式让本视图弹/收软键盘，供 showUiSoftKeyboard() 调用。 */
- (BOOL)toggleSoftKeyboard:(BOOL)bShow;
@end

// --- 全局 UIKeyboard 通知注册（在 +load 或首次使用时安装一次）---
@interface SouiKeyboardNotificationInstaller : NSObject
+ (void)ensureInstalled;
@end

// =====================================================================
//  SouiTextRange：UITextRange 抽象类的具体子类。
//  UITextRange 没有公开构造方法，必须子类化才能创建实例。
// =====================================================================
@interface SouiTextRange : UITextRange
@property (nonatomic, strong) UITextPosition *startPos;
@property (nonatomic, strong) UITextPosition *endPos;
- (instancetype)initWithStart:(UITextPosition *)start end:(UITextPosition *)end;
@end

@implementation SouiTextRange
- (instancetype)initWithStart:(UITextPosition *)start end:(UITextPosition *)end{
    self = [super init];
    if (self) {
        _startPos = start;
        _endPos = end;
    }
    return self;
}
- (UITextPosition *)start { return self.startPos; }
- (UITextPosition *)end   { return self.endPos; }
- (BOOL)isEmpty { return self.startPos == self.endPos; }
@end

@implementation SUIView

- (instancetype)initWithFrame:(CGRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent withDblClick:(BOOL)bAutoDblClick{
    // 首次初始化时安装全局 UIKeyboard 通知监听（只执行一次）
    [SouiKeyboardNotificationInstaller ensureInstalled];

    m_rcPos = frameRect;
    float sc = [[UIScreen mainScreen] scale];
    CGRect rc = frameRect;
    rc.size.width/=sc;
    rc.size.height/=sc;
    rc.origin.x/=sc;
    rc.origin.y/=sc;
    self = [super initWithFrame:rc];
    if (!self)
        return nil;
    m_hWnd = (HWND)(__bridge_retained void *)self;
    s_hWndMgr.add(m_hWnd);

    SLOG_STMI()<<"hjx initWithFrame, hWnd="<<m_hWnd<<" rc="<<frameRect.origin.x<<","<<frameRect.origin.y<<","<<frameRect.size.width<<","<<frameRect.size.height;
    self.listener = listener;
    self.bAutoDblClick = bAutoDblClick;
    self.byAlpha = 255;
    self.bMsgTransparent = FALSE;
    self.bEnabled = TRUE;
    self.bImeEnabled = TRUE;
    self.hostWindow = nil;
    self.lastTapTime = 0;
    // UITextInput 字段初始化
    self.markedText = nil;
    self.markedRange = NSMakeRange(NSNotFound, 0);
    self.selectedRange = NSMakeRange(0, 0);
    self.inputDelegate = nil;
    self.textPositions = [NSMutableDictionary dictionary];
    self.backgroundColor = [UIColor whiteColor];
    self.multipleTouchEnabled = NO;
    self.contentMode = UIViewContentModeTopLeft;

    // 双击手势
    self.doubleTapRecognizer = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(onDoubleTap:)];
    self.doubleTapRecognizer.numberOfTapsRequired = 2;
    self.doubleTapRecognizer.enabled = bAutoDblClick;
    [self addGestureRecognizer:self.doubleTapRecognizer];

    // 长按手势 -> 右键
    self.longPressRecognizer = [[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(onLongPress:)];
    self.longPressRecognizer.minimumPressDuration = 0.5;
    [self addGestureRecognizer:self.longPressRecognizer];

    if (hParent) {
        SUIView *parent = getUiView(hParent);
        if (parent) {
            [parent addSubview:self];
        }
    }
    return self;
}

- (void)destroy{
    self.listener = nil;
    [self removeFromSuperview];
    s_hWndMgr.remove(m_hWnd);
    CFBridgingRelease((void*)m_hWnd);
}

- (void)dealloc{
}

- (void)updateRect:(CGRect)rc{
    [self invalidRect:rc];
}

- (void)invalidRect:(CGRect)rc{
    if(CGRectIsEmpty(rc))
        return;
    [self setNeedsDisplayInRect:rc];
}

- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event{
    return self.bMsgTransparent ? nil : [super hitTest:point withEvent:event];
}

- (void)setAlphaValue:(BYTE)byAlpha{
    self.byAlpha = byAlpha;
    self.alpha = (CGFloat)byAlpha / 255.0f;
}

- (BYTE)getAlphaValue{
    return self.byAlpha;
}

- (void)setViewEnabled:(BOOL)bEnabled{
    self.bEnabled = bEnabled;
    self.userInteractionEnabled = bEnabled;
    [self setNeedsDisplay];
}

- (BOOL)isImeEnabled{
    return self.bImeEnabled;
}

- (void)setImeEnabled:(BOOL)bEnabled{
    self.bImeEnabled = bEnabled;
}

- (void)onActive:(BOOL)isActive{
    self.listener->OnNsActive(m_hWnd, isActive);
}

- (void)onStateChange:(int)nState{
    self.listener->OnNsEvent(m_hWnd, UM_STATE, nState, 0);
}

// 绘制：iOS 上 cairo 的 quartz 后端不可用，改用 image surface 渲染后
// 转为 CGImage 贴图到 UIGraphics 上下文。cairo 用户坐标使用物理像素，
// 与 swinx 内部坐标一致。
// CGDataProvider release 回调：释放持有的数据副本 buffer。
// 必须设置此回调，否则 [image drawInRect:] 在 iOS 16 上是延迟栅格化（CA display list
// 在 CA::Transaction::commit 时才真正读 CGImage 数据），而本函数返回前 cairo_surface
// 已被 destroy 释放了原始 data，会导致 ERROR_CGDataProvider_BufferIsNotReadable 崩溃。
static void swinx_cgDataProviderRelease(void *info, const void *data, size_t size){
    (void)data; (void)size;
    free(info);
}

- (void)drawRect:(CGRect)dirtyRect{
    CGContextRef cgContext = UIGraphicsGetCurrentContext();
    if(!cgContext || !self.listener)
        return;
    CGFloat scale = self.window.screen.scale ?: [UIScreen mainScreen].scale;
    int physW = (int)ceil(self.bounds.size.width * scale);
    int physH = (int)ceil(self.bounds.size.height * scale);
    if(physW <= 0 || physH <= 0)
        return;

    CGContextSaveGState(cgContext);
    // swinx GDI 内部使用物理像素坐标，通过 1/scale 缩放把物理像素坐标映射为
    // UIKit CGContext 使用的逻辑点坐标（UIKit 已预置左上原点，路径绘制无需 Y 轴翻转）。
    CGContextScaleCTM(cgContext, 1.0 / scale, 1.0 / scale);

    // 填充不透明白色背景，避免透明内容在 UIView 上不可见
    CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 1.0, 1.0);
    CGContextFillRect(cgContext, CGRectMake(0, 0, self.bounds.size.width * scale, self.bounds.size.height * scale));

    // dirtyRect 在 UIKit 中是逻辑点坐标，转换为物理像素构造 OnDrawRect 裁剪 RECT
    RECT rc = {(LONG)(dirtyRect.origin.x * scale),
               (LONG)(dirtyRect.origin.y * scale),
               (LONG)((dirtyRect.origin.x + dirtyRect.size.width) * scale),
               (LONG)((dirtyRect.origin.y + dirtyRect.size.height) * scale)};
    self.listener->OnDrawRect(m_hWnd, rc, cgContext);

    CGContextRestoreGState(cgContext);
}

#pragma mark - Touch -> Mouse

- (CGFloat)screenScale{
    return self.window.screen.scale ?: [UIScreen mainScreen].scale;
}

- (LPARAM)makeLParamFromPoint:(CGPoint)pt{
    CGFloat scale = [self screenScale];
    pt.x *= scale;
    pt.y *= scale;
    return MAKELPARAM(float2int(pt.x), float2int(pt.y));
}

// 将视图局部坐标（逻辑点）转换为屏幕物理像素坐标，并更新 SConnection 中的光标位置
- (void)updateCursorPos:(CGPoint)localPt{
    CGPoint winPt = [self convertPoint:localPt toView:nil]; // nil → window 坐标
    // window 坐标到屏幕坐标：加上 window origin
    CGPoint screenPt = CGPointMake(winPt.x + self.window.frame.origin.x,
                                   winPt.y + self.window.frame.origin.y);
    CGFloat scale = [self screenScale];
    POINT pt;
    pt.x = float2int(screenPt.x * scale);
    pt.y = float2int(screenPt.y * scale);
    // listener 实际是 SConnection*，通过 SConnMgr 获取
    SConnection *conn = SConnMgr::instance()->getConnection(GetCurrentThreadId());
    if(conn){
        conn->UpdateCursorPos(pt);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{
    if(!self.bEnabled)
        return;
    UITouch *touch = [touches anyObject];
    CGPoint pt = [touch locationInView:self];
    [self updateCursorPos:pt];
    self.listener->OnNsEvent(m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, [self makeLParamFromPoint:pt]);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{
    if(!self.bEnabled)
        return;
    UITouch *touch = [touches anyObject];
    CGPoint pt = [touch locationInView:self];
    [self updateCursorPos:pt];
    self.listener->OnNsEvent(m_hWnd, WM_MOUSEMOVE, MK_LBUTTON, [self makeLParamFromPoint:pt]);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{
    if(!self.bEnabled)
        return;
    UITouch *touch = [touches anyObject];
    CGPoint pt = [touch locationInView:self];
    // 双击检测（当未启用手势自动双击时）
    NSTimeInterval now = event.timestamp;
    if(self.bAutoDblClick && (now - self.lastTapTime) < [self doubleClickInterval]){
        self.listener->OnNsEvent(m_hWnd, WM_LBUTTONDBLCLK, MK_LBUTTON, [self makeLParamFromPoint:pt]);
        self.lastTapTime = 0;
    }else{
        self.lastTapTime = now;
    }
    self.listener->OnNsEvent(m_hWnd, WM_LBUTTONUP, 0, [self makeLParamFromPoint:pt]);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{
    if(!self.bEnabled)
        return;
    UITouch *touch = [touches anyObject];
    CGPoint pt = [touch locationInView:self];
    self.listener->OnNsEvent(m_hWnd, WM_LBUTTONUP, 0, [self makeLParamFromPoint:pt]);
}

- (NSTimeInterval)doubleClickInterval{
    return 0.3;
}

- (void)onDoubleTap:(UITapGestureRecognizer *)recognizer{
    if(recognizer.state == UIGestureRecognizerStateEnded){
        CGPoint pt = [recognizer locationInView:self];
        self.listener->OnNsEvent(m_hWnd, WM_LBUTTONDBLCLK, MK_LBUTTON, [self makeLParamFromPoint:pt]);
    }
}

- (void)onLongPress:(UILongPressGestureRecognizer *)recognizer{
    CGPoint pt = [recognizer locationInView:self];
    if(recognizer.state == UIGestureRecognizerStateBegan){
        self.listener->OnNsEvent(m_hWnd, WM_RBUTTONDOWN, MK_RBUTTON, [self makeLParamFromPoint:pt]);
    }else if(recognizer.state == UIGestureRecognizerStateEnded){
        self.listener->OnNsEvent(m_hWnd, WM_RBUTTONUP, 0, [self makeLParamFromPoint:pt]);
    }
}

#pragma mark - Hardware Keyboard

/** bImeEnabled == NO 时，不允许弹软键盘（对齐 Android setInputType 策略）。 */
- (BOOL)canBecomeFirstResponder{
    return self.bEnabled;
}

// 判断 VK 码是否可能产生可打印字符（含 Backspace/Tab/Return/Space）。
// 功能键、方向键、修饰键、导航键等不产生 WM_CHAR。
static BOOL vkCanProduceChar(UINT vk){
    if (vk >= VK_F1 && vk <= VK_F12) return NO;
    if (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT) return NO;
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return NO;
    if (vk >= VK_LSHIFT && vk <= VK_RMENU) return NO;
    if (vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END) return NO;
    if (vk == VK_INSERT || vk == VK_DELETE) return NO;
    if (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) return NO;
    if (vk == VK_SNAPSHOT || vk == VK_PAUSE) return NO;
    return YES;
}

// 将一个 wchar_t 字符作为 WM_CHAR（ASCII）或 WM_IME_CHAR（非 ASCII）投递给 SOUI。
// 与 cocoa 的 TranslateMessage 逻辑保持一致：c<127 走 WM_CHAR，否则走 WM_IME_CHAR。
static void postChar(SConnBase *listener, HWND hWnd, wchar_t c, LPARAM lParam){
    if (c < 0x20 && c != VK_BACK && c != VK_TAB && c != VK_RETURN)
        return; // 过滤不可见控制字符
    UINT msg = (c < 127) ? WM_CHAR : WM_IME_CHAR;
    listener->OnNsEvent(hWnd, msg, (WPARAM)c, lParam);
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event{
    if(!self.bEnabled)
        return;
    BOOL ctrlDown  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altDown   = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL winDown   = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    for(UIPress *press in presses){
        UIKey *key = press.key;
        if(!key)
            continue;
        UINT vkCode = convertKeyCodeToVK((uint16_t)key.keyCode);
        if(vkCode == 0)
            continue;
        Keyboard::instance().setKeyState(vkCode, 1);
        LPARAM lParam = ((LPARAM)key.keyCode << 16);
        self.listener->OnNsEvent(m_hWnd, WM_KEYDOWN, vkCode, lParam);
        // 硬件键盘：iOS 上 scanCodeToChar 始终返回 0，TranslateMessage 无法产生 WM_CHAR。
        // 这里利用 key.characters（已含修饰键效果）直接生成 WM_CHAR/WM_IME_CHAR，
        // 解决模拟器硬件键盘输入无字符消息的问题。
        if (vkCanProduceChar(vkCode) && !ctrlDown && !altDown && !winDown) {
            NSString *chars = key.characters;
            if (chars.length > 0) {
                std::wstring wstr;
                towstring([chars UTF8String], -1, wstr);
                for (int i = 0; i < (int)wstr.length(); i++) {
                    postChar(self.listener, m_hWnd, wstr[i], lParam);
                }
            }
        }
    }
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event{
    for(UIPress *press in presses){
        UIKey *key = press.key;
        if(!key)
            continue;
        UINT vkCode = convertKeyCodeToVK((uint16_t)key.keyCode);
        if(vkCode == 0)
            continue;
        Keyboard::instance().setKeyState(vkCode, 0);
        LPARAM lParam = ((LPARAM)key.keyCode << 16);
        self.listener->OnNsEvent(m_hWnd, WM_KEYUP, vkCode, lParam);
    }
}

#pragma mark - UIKeyInput (软键盘文本输入，UITextInput 继承自它)

- (BOOL)hasText{
    // SUI 渲染层维护真实文本内容，此处简单返回 YES 让系统知道可输入
    return YES;
}

- (void)insertText:(NSString *)text{
    if(!text || text.length == 0)
        return;
    // 有 markedText 时 insertText 代表组合确认：先清 marked
    if(self.markedText){
        self.markedText = nil;
        self.markedRange = NSMakeRange(NSNotFound, 0);
    }
    const char *str = [text UTF8String];
    std::wstring wstr;
    towstring(str, -1, wstr);
    for(int i=0;i<(int)wstr.length();i++){
        // ASCII 字符发 WM_CHAR（SWindow/SRichEdit/SComboBase 等控件消息映射只处理 WM_CHAR），
        // 非 ASCII 字符发 WM_IME_CHAR（SListView/STreeView 等处理 WM_IME_CHAR）。
        // 与 cocoa TranslateMessage 的 c<127 ? WM_CHAR : WM_IME_CHAR 逻辑一致。
        postChar(self.listener, m_hWnd, wstr[i], 0);
    }
    // 通知 inputDelegate：文本已变
    if (self.inputDelegate) {
        [self.inputDelegate textWillChange:self];
        [self.inputDelegate textDidChange:self];
    }
}

- (void)deleteBackward{
    // Backspace：发送 WM_KEYDOWN + WM_CHAR(VK_BACK)。
    // SOUI 的 SEdit/SRichEdit 依赖 WM_CHAR(VK_BACK) 来执行删除，
    // 仅发 WM_KEYDOWN 不会触发 OnChar，控件收不到删除命令。
    self.listener->OnNsEvent(m_hWnd, WM_KEYDOWN, VK_BACK, 0);
    self.listener->OnNsEvent(m_hWnd, WM_CHAR, VK_BACK, 0);
    self.listener->OnNsEvent(m_hWnd, WM_KEYUP, VK_BACK, 0);
    if (self.inputDelegate) {
        [self.inputDelegate textWillChange:self];
        [self.inputDelegate textDidChange:self];
    }
}

#pragma mark - UITextInput (markedText / selectedRange 支持)

// ---- marked text ----
@synthesize markedText = _markedText;
@synthesize markedRange = _markedRange;
@synthesize selectedRange = _selectedRange;
@synthesize inputDelegate = _inputDelegate;
@synthesize markedTextStyle = _markedTextStyle;

// ---- UITextInput 协议：markedTextRange / hasMarkedText / selectedTextRange ----
// 将内部 NSRange（markedRange / selectedRange）转换为 UITextRange* 返回给系统。
// 注意：markedRange.location == NSNotFound 表示没有进行中的组合输入，
// 此时 markedTextRange 必须返回 nil。

- (UITextRange *)markedTextRange{
    if (self.markedRange.location == NSNotFound || self.markedRange.length == 0) {
        return nil;
    }
    UITextPosition *start = [self positionForIndex:(int)self.markedRange.location];
    UITextPosition *end   = [self positionForIndex:(int)(self.markedRange.location + self.markedRange.length)];
    return [self textRangeFromPosition:start toPosition:end];
}

- (BOOL)hasMarkedText{
    return (self.markedRange.location != NSNotFound && self.markedRange.length > 0);
}

- (UITextRange *)selectedTextRange{
    UITextPosition *start = [self positionForIndex:(int)self.selectedRange.location];
    UITextPosition *end   = [self positionForIndex:(int)(self.selectedRange.location + self.selectedRange.length)];
    return [self textRangeFromPosition:start toPosition:end];
}

- (void)setSelectedTextRange:(UITextRange *)selectedTextRange{
    if (!selectedTextRange) {
        self.selectedRange = NSMakeRange(0, 0);
        return;
    }
    NSInteger startIdx = [(NSNumber*)objc_getAssociatedObject(selectedTextRange.start, kSouiTextPositionIndexKey) integerValue] ?: 0;
    NSInteger endIdx   = [(NSNumber*)objc_getAssociatedObject(selectedTextRange.end,   kSouiTextPositionIndexKey) integerValue] ?: 0;
    if (endIdx < startIdx) { NSInteger t = startIdx; startIdx = endIdx; endIdx = t; }
    self.selectedRange = NSMakeRange(startIdx, endIdx - startIdx);
    if (self.inputDelegate) {
        [self.inputDelegate selectionWillChange:self];
        [self.inputDelegate selectionDidChange:self];
    }
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange{
    self.markedText = markedText;
    if (markedText) {
        self.markedRange = NSMakeRange(0, markedText.length);
    } else {
        self.markedRange = NSMakeRange(NSNotFound, 0);
    }
    // 把组合文本通过 WM_IME_COMPOSITION 通知到 C++（如果需要）。
    // SUI 的 SEdit 已能处理 WM_IME_CHAR 逐字输入，这里保留 markedText 仅作协议一致性。
    if (self.inputDelegate) {
        [self.inputDelegate textWillChange:self];
        [self.inputDelegate textDidChange:self];
        [self.inputDelegate selectionWillChange:self];
        [self.inputDelegate selectionDidChange:self];
    }
}

- (void)unmarkText{
    self.markedText = nil;
    self.markedRange = NSMakeRange(NSNotFound, 0);
    if (self.inputDelegate) {
        [self.inputDelegate textWillChange:self];
        [self.inputDelegate textDidChange:self];
    }
}

// ---- text positions: 用整数索引包装成 UITextPosition ----
- (UITextPosition *)beginningOfDocument{
    return [self positionForIndex:0];
}

- (UITextPosition *)endOfDocument{
    // SUI 层不维护"字符总数"，这里用 0 做边界；由于我们不提供真实文本编辑，
    // 只是让 UITextInput 协议不至于为空，足以驱动 IME 输入流程。
    return [self positionForIndex:0];
}

- (UITextPosition *)positionFromPosition:(UITextPosition *)position
                                 offset:(NSInteger)offset{
    NSInteger idx = [(NSNumber*)objc_getAssociatedObject(position, kSouiTextPositionIndexKey) integerValue] ?: 0;
    return [self positionForIndex:(int)(idx + offset)];
}

- (NSComparisonResult)comparePosition:(UITextPosition *)position
                           toPosition:(UITextPosition *)other{
    NSInteger a = [(NSNumber*)objc_getAssociatedObject(position, kSouiTextPositionIndexKey) integerValue] ?: 0;
    NSInteger b = [(NSNumber*)objc_getAssociatedObject(other,  kSouiTextPositionIndexKey) integerValue] ?: 0;
    if (a < b) return NSOrderedAscending;
    if (a > b) return NSOrderedDescending;
    return NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition *)from
                     toPosition:(UITextPosition *)toPosition{
    NSInteger a = [(NSNumber*)objc_getAssociatedObject(from,     kSouiTextPositionIndexKey) integerValue] ?: 0;
    NSInteger b = [(NSNumber*)objc_getAssociatedObject(toPosition, kSouiTextPositionIndexKey) integerValue] ?: 0;
    return b - a;
}

- (UITextPosition *)positionForIndex:(int)idx{
    NSNumber *dictKey = @(idx);
    UITextPosition *p = self.textPositions[dictKey];
    if (!p) {
        p = [[UITextPosition alloc] init];
        // 注意：kSouiTextPositionIndexKey 是"写入 idx 的 key"，dictKey 是 NSMutableDictionary 的 key
        objc_setAssociatedObject(p, kSouiTextPositionIndexKey, dictKey, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        self.textPositions[dictKey] = p;
    }
    return p;
}

// ---- Text ranges（用开始/结束位置构造）----
- (UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition
                            toPosition:(UITextPosition *)toPosition{
    return [[SouiTextRange alloc] initWithStart:fromPosition end:toPosition];
}

- (CGRect)caretRectForPosition:(UITextPosition *)position{
    // SUI 自己绘制光标/选区，这里返回占位 CGRect，避免系统对光标位置做断言。
    return CGRectMake(0, 0, 1, 20);
}

- (CGRect)firstRectForRange:(UITextRange *)range{
    return CGRectMake(0, 0, 10, 20);
}

- (NSArray<UITextSelectionRect *> *)selectionRectsForRange:(UITextRange *)range{
    return @[];
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point{
    return [self beginningOfDocument];
}

- (UITextPosition *)closestPositionToPoint:(CGPoint)point
                               withinRange:(UITextRange *)range{
    return range.start;
}

- (UITextRange *)characterRangeAtPoint:(CGPoint)point{
    return [self textRangeFromPosition:[self beginningOfDocument]
                                toPosition:[self beginningOfDocument]];
}

// ---- Tokenizer：简单返回 UITextGranularity 任意范围 ----
- (id<UITextInputTokenizer>)tokenizer{
    // 使用系统默认词法分析器即可
    return [[UITextInputStringTokenizer alloc] initWithTextInput:self];
}

// ---- 布局方向辅助方法（UITextInput 协议要求；becomeFirstResponder 后系统可能调用） ----

- (nullable UITextPosition *)positionFromPosition:(UITextPosition *)position
                                      inDirection:(UITextLayoutDirection)direction
                                           offset:(NSInteger)offset{
    // 简化：只按 +/- offset 线性映射，不区分水平/垂直方向（SOUI 只有一行光标模型）。
    NSInteger idx = [(NSNumber*)objc_getAssociatedObject(position, kSouiTextPositionIndexKey) integerValue] ?: 0;
    switch (direction) {
        case UITextLayoutDirectionRight:
        case UITextLayoutDirectionDown:
            return [self positionForIndex:(int)(idx + offset)];
        case UITextLayoutDirectionLeft:
        case UITextLayoutDirectionUp:
            return [self positionForIndex:(int)(idx - offset)];
        default:
            return [self positionForIndex:(int)(idx + offset)];
    }
}

- (nullable UITextPosition *)positionWithinRange:(UITextRange *)range
                           farthestInDirection:(UITextLayoutDirection)direction{
    // 简单实现：根据方向返回 range.start 或 range.end
    switch (direction) {
        case UITextLayoutDirectionRight:
        case UITextLayoutDirectionDown:
            return range.end;
        case UITextLayoutDirectionLeft:
        case UITextLayoutDirectionUp:
            return range.start;
        default:
            return range.end;
    }
}

- (nullable UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position
                                                inDirection:(UITextLayoutDirection)direction{
    // 简化：向指定方向扩展一个字符（offset±1）
    UITextPosition *other;
    switch (direction) {
        case UITextLayoutDirectionRight:
        case UITextLayoutDirectionDown:
            other = [self positionFromPosition:position offset:1];
            return [self textRangeFromPosition:position toPosition:other];
        case UITextLayoutDirectionLeft:
        case UITextLayoutDirectionUp:
            other = [self positionFromPosition:position offset:-1];
            return [self textRangeFromPosition:other toPosition:position];
        default:
            other = [self positionFromPosition:position offset:1];
            return [self textRangeFromPosition:position toPosition:other];
    }
}

// ---- Replace / Storage (协议要求；SOUI 不做真实文本存储，保持空实现即可) ----
- (NSString *)textInRange:(UITextRange *)range{
    return nil;
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text{
    if (text) {
        [self insertText:text];
    }
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection
                        forRange:(UITextRange *)range{
    // no-op：SOUI 布局层统一 LTR
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position
                                         inDirection:(UITextStorageDirection)direction{
    return NSWritingDirectionLeftToRight;
}

#pragma mark - 软键盘显式控制（对齐 Android showSoftKeyboard）

- (BOOL)toggleSoftKeyboard:(BOOL)bShow{
    if (!self.bEnabled) return NO;
    if (bShow) {
        if (!self.bImeEnabled) {
            // IME 被 disable：不应主动弹（对齐 Android imm.hideSoftInputFromWindow 的策略）
            return NO;
        }
        if (!self.isFirstResponder) {
            return [self becomeFirstResponder];  // becomeFirstResponder 会触发系统弹键盘
        }
        return YES;  // 已是第一响应者 → 键盘已显示（或被系统收起，但 iOS 无 API 强制再弹）
    } else {
        if (self.isFirstResponder) {
            return [self resignFirstResponder];  // resign 会触发收键盘
        }
        return YES;  // 已经不是 FR → 键盘已收
    }
}

@end

// 把键盘高度变化通知到 C++（对齐 Android nativeSetKeyboardHeight）。
static void notifyKeyboardHeightToCpp(int heightPhys) {
    @autoreleasepool {
        // 焦点窗口统一从 SConnection::m_hFocus 取（单一事实源）
        SConnection *conn = SConnMgr::instance()->getConnection(GetCurrentThreadId());
        if (!conn) return;
        HWND target = conn->GetFocus();
        if (!target) return;
        SUIView *view = getUiView(target);
        if (!view || !view.listener) return;
        // WM_KEYBOARD_HEIGHT = WM_SWINX_MSG_FIRST + 0（见 winuser.h）
        //   wParam = heightPhys（物理像素高度，0 表示关闭），lParam = 0
        view.listener->OnNsEvent(target, WM_KEYBOARD_HEIGHT, heightPhys, 0);
    }
}

// =====================================================================
//  SouiKeyboardNotificationInstaller：监听 UIKeyboardWillShow / WillHide
//  → 维护全局键盘可见标记 / 高度 → 通知 C++（对齐 Android nativeSetKeyboardHeight）
// =====================================================================
@implementation SouiKeyboardNotificationInstaller

+ (void)ensureInstalled{
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];

        // UIKeyboardWillShow：记录高度 + 标可见
        [nc addObserverForName:UIKeyboardWillShowNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            @autoreleasepool {
                NSDictionary *info = note.userInfo;
                CGRect kbEndFrame = [info[UIKeyboardFrameEndUserInfoKey] CGRectValue];
                CGFloat scale = [UIScreen mainScreen].scale;
                int heightPhys = (int)round(kbEndFrame.size.height * scale);
                {
                    std::unique_lock<std::mutex> lk(s_kbLock);
                    s_kbVisible = YES;
                    s_kbHeightPhys = heightPhys;
                }
                notifyKeyboardHeightToCpp(heightPhys);
            }
        }];

        // UIKeyboardWillHide：清零
        [nc addObserverForName:UIKeyboardWillHideNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *note) {
            @autoreleasepool {
                (void)note;
                {
                    std::unique_lock<std::mutex> lk(s_kbLock);
                    s_kbVisible = NO;
                    s_kbHeightPhys = 0;
                }
                notifyKeyboardHeightToCpp(0);
            }
        }];
    });
}

@end


// 判断是否为根视图（直接挂在 UIWindow 上）
static BOOL IsRootView(SUIView *pView){
    if(pView.superview == nil)
        return YES;
    if(pView.window != nil && pView.superview == pView.window)
        return YES;
    return NO;
}

static UIScreen *getUiScreen(HWND hWnd){
    @autoreleasepool {
        if(hWnd){
            SUIView *view = getUiView(hWnd);
            if(view && view.window){
                return view.window.screen;
            }
        }
        return [UIScreen mainScreen];
    }
}

HWND createUiWindow(HWND hParent, DWORD dwStyle,DWORD dwExStyle, BOOL bAutoDblClick, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener)
{
    SLOG_STMI()<<"createUiWindow enter, hParent="<<hParent<<" dwStyle=0x"<<std::hex<<dwStyle<<" dwExStyle=0x"<<dwExStyle<<" x="<<std::dec<<x<<" y="<<y<<" cx="<<cx<<" cy="<<cy;
    @autoreleasepool {
        CGRect rect = CGRectMake(x, y, cx, cy);
        if(!(dwStyle&WS_CHILD))
            hParent=0;
        SUIView *view = [[SUIView alloc] initWithFrame:rect withListener:pListener withParent:hParent withDblClick:bAutoDblClick];
        return view->m_hWnd;
    }
}

BOOL showUiWindow(HWND hWnd,int nCmdShow){
    //SLOG_STMI()<<"showUiWindow enter, hWnd="<<hWnd<<" nCmdShow="<<nCmdShow;
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        BOOL bRoot = IsRootView(view);
        if(nCmdShow == SW_HIDE){
            if(bRoot && view.hostWindow){
                view.hostWindow.hidden = YES;
            }else{
                view.hidden = YES;
            }
            return TRUE;
        }
        if(bRoot){
            if(view.hostWindow == nil){
                CGFloat sc = [UIScreen mainScreen].scale;
                CGRect winRect = [UIScreen mainScreen].bounds;
                winRect = view->m_rcPos;
                winRect.size.width/=sc;
                winRect.size.height/=sc;
                winRect.origin.x/=sc;
                winRect.origin.y/=sc;
                UIWindow *window = [[UIWindow alloc] initWithFrame:winRect];
                window.backgroundColor = [UIColor whiteColor];
                window.windowLevel = UIWindowLevelNormal;
                [window addSubview:view];
                view.frame = window.bounds;
                // 同步 m_rcPos 为物理像素，保持与 GetClientRect 返回值一致
                //CGFloat sc = window.screen.scale ?: [UIScreen mainScreen].scale;
                //view->m_rcPos = CGRectMake(0, 0, winRect.size.width * sc, winRect.size.height * sc);
                view.hostWindow = window;
                window.hidden = YES;  // 先隐藏，等 makeKeyAndVisible 时显示
            }
        //SLOG_STMI()<<"showUiWindow makeKeyAndVisible, hWnd="<<hWnd;
            [view.hostWindow makeKeyAndVisible];
            [view onActive:TRUE];
            [view setNeedsDisplay];
        }else{
            view.hidden = NO;
        }
        return TRUE;
    }
}

BOOL setUiWindowPos(HWND hWnd, int x, int y){
    SLOG_STMI()<<"hjx setUiWindowPos hWnd="<<hWnd<<" x="<<x<< " y="<<y;
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        CGRect rect = view->m_rcPos;
        rect.origin.x = x;
        rect.origin.y = y;
        view->m_rcPos = rect;
        UIScreen *screen = getUiScreen(hWnd);
        CGFloat scale = screen.scale;
        rect.origin.x /= scale;
        rect.origin.y /= scale;
        if(IsRootView(view) && view.hostWindow){
            view.hostWindow.frame = rect;
        }else{
            view.frame = rect;
        }
        return TRUE;
    }
}

BOOL setUiWindowSize(HWND hWnd, int cx, int cy){
    SLOG_STMI()<<"hjx setUiWindowSize hWnd="<<hWnd<<" cx="<<cx<< " cy="<<cy;
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        CGRect rect = view->m_rcPos;
        rect.size.width = cx;
        rect.size.height = cy;
        view->m_rcPos = rect;
        UIScreen *screen = getUiScreen(hWnd);
        CGFloat scale = screen.scale;
        rect.origin.x /= scale;
        rect.origin.y /= scale;
        rect.size.width /= scale;
        rect.size.height /= scale;
        if(IsRootView(view) && view.hostWindow){
            view.hostWindow.frame = rect;
        }else{
            view.frame = rect;
            [view setNeedsDisplay];
        }
        return TRUE;
    }
}

void closeUiWindow(HWND hWnd)
{
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view){
            if(view.hostWindow){
                view.hostWindow.hidden = YES;
                [view.hostWindow removeFromSuperview];
                view.hostWindow = nil;
            }
            [view destroy];
        }
    }
}

HWND getUiWindow(HWND hParent, int code)
{
    @autoreleasepool {
        SUIView *parent = getUiView(hParent);
        if(!parent)
            return 0;
        HWND hRet = 0;
        switch (code)
        {
        case GW_CHILDFIRST:
            if(parent.subviews.count > 0){
                SUIView *first = parent.subviews.firstObject;
                if([first isKindOfClass:[SUIView class]])
                    hRet = first->m_hWnd;
            }
            break;
        case GW_CHILDLAST:
            if(parent.subviews.count > 0){
                SUIView *last = parent.subviews.lastObject;
                if([last isKindOfClass:[SUIView class]])
                    hRet = last->m_hWnd;
            }
            break;
        case GW_HWNDFIRST:
            hRet = getUiWindow(hParent, GW_CHILDFIRST);
            break;
        case GW_HWNDLAST:
            hRet = getUiWindow(hParent, GW_CHILDLAST);
            break;
        case GW_HWNDPREV:
            {
                UIView *superview = parent.superview;
                if(superview){
                    NSArray *siblings = superview.subviews;
                    NSUInteger index = [siblings indexOfObject:parent];
                    if(index > 0){
                        UIView *prev = siblings[index-1];
                        if([prev isKindOfClass:[SUIView class]])
                            hRet = ((SUIView*)prev)->m_hWnd;
                    }
                }
            }
            break;
        case GW_HWNDNEXT:
            {
                UIView *superview = parent.superview;
                if(superview){
                    NSArray *siblings = superview.subviews;
                    NSUInteger index = [siblings indexOfObject:parent];
                    if(index < siblings.count-1){
                        UIView *next = siblings[index+1];
                        if([next isKindOfClass:[SUIView class]])
                            hRet = ((SUIView*)next)->m_hWnd;
                    }
                }
            }
            break;
        }
        return hRet;
    }
}

BOOL setUiActiveWindow(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view || !view.hostWindow)
            return FALSE;
        [view.hostWindow makeKeyWindow];
        [view onActive:TRUE];
        return TRUE;
    }
}

HWND getUiActiveWindow(){
    @autoreleasepool {
        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if(keyWindow){
            for(UIView *v in keyWindow.subviews){
                if([v isKindOfClass:[SUIView class]])
                    return ((SUIView*)v)->m_hWnd;
            }
        }
        return 0;
    }
}

BOOL setUiFocusWindow(HWND hWnd){
    @autoreleasepool {
        // 焦点管理唯一事实源：SConnection::m_hFocus。
        // 本函数被 SConnection::SetFocus(hWnd) 调用，其顺序：
        //   1) SendMessage(hOldFocus, WM_KILLFOCUS)
        //   2) setUiFocusWindow(hWnd)  ← 本函数
        //   3) SendMessage(hWnd, WM_SETFOCUS)
        //   4) m_hFocus = hWnd
        // （见 SConnection.mm SetFocus）
        // 因此本函数的职责仅是：驱动 UIKit 层 become/resignFirstResponder
        // 控制软键盘弹/收，**不管理焦点值本身**。
        //
        // 为了避免 become/resignFirstResponder 内部再次触发
        // conn->SetFocus(...) 形成递归，通过 g_souiInFocusTransfer
        // TLS 标志告知重载实现跳过 SetFocus 调用。

        SConnection *conn = SConnMgr::instance()->getConnection(GetCurrentThreadId());
        if (!conn) return FALSE;

        HWND oldHwnd = conn->GetFocus();
        if (oldHwnd == hWnd) return TRUE;  // 无变化

        // 设置 TLS 锁 → become/resign 内部跳过 conn->SetFocus 调用
        g_souiInFocusTransfer = YES;

        // 旧焦点：先 resign（收键盘）
        if (oldHwnd && oldHwnd != hWnd) {
            SUIView *oldView = getUiView(oldHwnd);
            if (oldView && oldView.isFirstResponder) {
                [oldView resignFirstResponder];
            }
        }

        // hWnd==0：只清焦点（oldHwnd 已经 resign 处理过了）
        if (hWnd == 0) {
            g_souiInFocusTransfer = NO;
            return TRUE;
        }

        SUIView *view = getUiView(hWnd);
        if (!view) {
            g_souiInFocusTransfer = NO;
            return FALSE;
        }
        if (view.hostWindow && !view.hostWindow.isKeyWindow) {
            [view.hostWindow makeKeyWindow];
        }
        // bImeEnabled == NO：只做焦点转移，不弹 IME（由 canBecomeFirstResponder 过滤）
        //(void)[view becomeFirstResponder];

        g_souiInFocusTransfer = NO;
        return TRUE;
    }
}

void invalidateUiWindow(HWND hWnd, LPCRECT rc){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return;
        if(view.hostWindow && view.hostWindow.hidden)
            return;
        if(rc){
            CGRect rect = CGRectMake(rc->left, rc->top, rc->right-rc->left, rc->bottom-rc->top);
            CGFloat scale = [view screenScale];
            rect.origin.x /= scale;
            rect.origin.y /= scale;
            rect.size.width /= scale;
            rect.size.height /= scale;
            [view invalidRect:rect];
        }else{
            [view setNeedsDisplay];
        }
    }
}

void updateUiWindow(HWND hWnd, const RECT &rc){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return;
        CGRect rect = CGRectMake(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top);
        CGFloat scale = [view screenScale];
        rect.origin.x /= scale;
        rect.origin.y /= scale;
        rect.size.width /= scale;
        rect.size.height /= scale;
        [view updateRect:rect];
    }
}

BOOL isUiWindowVisible(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        if(IsRootView(view))
            return view.hostWindow && !view.hostWindow.hidden;
        return !view.hidden;
    }
}

BOOL getUiWindowRect(HWND hWnd, RECT *rc){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        CGRect rect = view->m_rcPos;
        *rc = CGRect2Rect(rect);
        return TRUE;
    }
}

HWND hwndFromPoint(HWND hWnd,POINT pt){
    @autoreleasepool {
        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if(!keyWindow)
            return 0;
        CGFloat scale = keyWindow.screen.scale;
        CGPoint windowPt = CGPointMake(pt.x/scale, pt.y/scale);
        UIView *view = [keyWindow hitTest:windowPt withEvent:nil];
        while(view){
            if([view isKindOfClass:[SUIView class]])
                return ((SUIView*)view)->m_hWnd;
            view = view.superview;
        }
        return 0;
    }
}

BOOL setUiWindowZorder(HWND hWnd, HWND hWndInsertAfter){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        UIView *superview = view.superview;
        if(!superview)
            return FALSE;
        if(hWndInsertAfter == HWND_TOP){
            [superview bringSubviewToFront:view];
            return TRUE;
        }
        if(hWndInsertAfter == HWND_BOTTOM){
            [superview sendSubviewToBack:view];
            return TRUE;
        }
        SUIView *insertAfter = getUiView(hWndInsertAfter);
        if(!insertAfter)
            return FALSE;
        [superview insertSubview:view belowSubview:insertAfter];
        return TRUE;
    }
}

BOOL setUiWindowCapture(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        // iOS 无全局事件捕获，通过标记记录捕获窗口
        objc_setAssociatedObject([UIApplication sharedApplication], @selector(setUiWindowCapture), [NSNumber numberWithUnsignedLong:(unsigned long)hWnd], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return TRUE;
    }
}

BOOL releaseUiWindowCapture(HWND hWnd){
    @autoreleasepool {
        objc_setAssociatedObject([UIApplication sharedApplication], @selector(setUiWindowCapture), nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return TRUE;
    }
}

BOOL setUiWindowAlpha(HWND hWnd,BYTE byAlpha){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        [view setAlphaValue:byAlpha];
        return TRUE;
    }
}

BYTE getUiWindowAlpha(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return 0;
        return [view getAlphaValue];
    }
}

HWND getUiForegroundWindow(){
    @autoreleasepool {
        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if(keyWindow){
            for(UIView *v in keyWindow.subviews){
                if([v isKindOfClass:[SUIView class]])
                    return ((SUIView*)v)->m_hWnd;
            }
        }
        return NULL;
    }
}

BOOL setUiForegroundWindow(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view || !view.hostWindow)
            return FALSE;
        SLOG_STMI()<<"showUiWindow makeKeyAndVisible, hWnd="<<hWnd;
        [view.hostWindow makeKeyAndVisible];
        return TRUE;
    }
}

BOOL setUiWindowToTop(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view || !view.hostWindow)
            return FALSE;
        SLOG_STMI()<<"showUiWindow makeKeyAndVisible, hWnd="<<hWnd;
        [view.hostWindow makeKeyAndVisible];
        return TRUE;
    }
}

BOOL setUiMsgTransparent(HWND hWnd, BOOL bTransparent){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        view.bMsgTransparent = bTransparent;
        if(view.hostWindow)
            view.hostWindow.userInteractionEnabled = !bTransparent;
        return TRUE;
    }
}

BOOL sendUiSysCommand(HWND hWnd, int nCmd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        if(nCmd == SC_MAXIMIZE){
            UIScreen *screen = getUiScreen(hWnd);
            CGFloat scale = screen.scale;
            CGRect full = screen.bounds;
            view->m_rcPos = CGRectMake(0,0,full.size.width*scale,full.size.height*scale);
            if(view.hostWindow)
                view.hostWindow.frame = full;
            [view onStateChange:SIZE_MAXIMIZED];
            return TRUE;
        }
        if(nCmd == SC_RESTORE){
            [view onStateChange:SIZE_RESTORED];
            return TRUE;
        }
        if(nCmd == SC_MINIMIZE){
            [view onStateChange:SIZE_MINIMIZED];
            return TRUE;
        }
        return FALSE;
    }
}

BOOL setUiWindowIcon(HWND hWnd, HICON hIcon, BOOL bBigIcon){
    // iOS 无窗口图标概念
    return TRUE;
}

BOOL setUiParent(HWND hWnd, HWND hParent){
    @autoreleasepool {
        if(!(GetWindowLongPtrA(hWnd, GWL_STYLE) & WS_CHILD))
            return FALSE;
        SUIView *view = getUiView(hWnd);
        SUIView *parent = getUiView(hParent);
        if(!view)
            return FALSE;
        if(view.hostWindow){
            view.hostWindow = nil;
        }
        if(parent){
            [parent addSubview:view];
        }
        return TRUE;
    }
}

BOOL flashUiWindow(HWND hWnd, DWORD dwFlags, UINT uCount, DWORD dwTimeout){
    // iOS 无窗口闪烁机制
    return TRUE;
}

BOOL isUiDropTarget(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        return view.interactions.count > 0;
    }
}

BOOL setUiDropTarget(HWND hWnd, BOOL bEnable){
    // iOS 拖放使用 UIDropInteraction（iOS 11+），此处简化处理
    return TRUE;
}

HRESULT doUiDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,
                          DWORD *pdwEffect){
    // iOS 拖放通过 UIDragInteraction 实现，与 macOS 的 NSDraggingSession 模型不同。
    // 此处返回取消，业务层应改用 UIDragInteraction。
    if(pdwEffect) *pdwEffect = DROPEFFECT_NONE;
    return DRAGDROP_S_CANCEL;
}

BOOL setUiWindowCursor(HWND hWnd, HCURSOR cursor){
    // iOS 无鼠标光标
    return TRUE;
}

float getScale(){
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return  [screen scale];
    } 
}


int getUiDpi(bool bx){
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return (int)(screen.scale * 96);
    }
}

HWND findUiKeyWindow(){
    @autoreleasepool {
        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if(keyWindow){
            for(UIView *v in keyWindow.subviews){
                if([v isKindOfClass:[SUIView class]])
                    return ((SUIView*)v)->m_hWnd;
            }
        }
        for(UIWindow *window in [UIApplication sharedApplication].windows){
            if(!window.hidden){
                for(UIView *v in window.subviews){
                    if([v isKindOfClass:[SUIView class]])
                        return ((SUIView*)v)->m_hWnd;
                }
            }
        }
        return NULL;
    }
}

BOOL setUiWindowRgn(HWND hWnd, const RECT *prc, int nCount){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(!view)
            return FALSE;
        if(prc && nCount){
            UIBezierPath *path = [UIBezierPath bezierPath];
            for(int i = 0; i < nCount; i++){
                const RECT &rc = prc[i];
                [path appendPath:[UIBezierPath bezierPathWithRect:CGRectMake(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top)]];
            }
            CAShapeLayer *maskLayer = [CAShapeLayer layer];
            maskLayer.path = path.CGPath;
            view.layer.mask = maskLayer;
            view.layer.masksToBounds = YES;
        }else{
            view.layer.mask = nil;
            view.layer.masksToBounds = NO;
        }
        [view setNeedsDisplay];
        return TRUE;
    }
}

int getUiWindowId(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view && view.hostWindow){
            return (int)(intptr_t)view.hostWindow;
        }
        return 0;
    }
}

BOOL enableUiWindow(HWND hWnd, BOOL bEnable){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view){
            [view setViewEnabled:bEnable];
            return TRUE;
        }
        return FALSE;
    }
}

void enableUiWindowIme(HWND hWnd, BOOL bEnable){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view){
            [view setImeEnabled:bEnable];
        }
    }
}

BOOL isUiWindowEnableIme(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view){
            return [view isImeEnabled];
        }
        return FALSE;
    }
}

void setUiWindowToolWindow(HWND hWnd, BOOL bToolWindow){
    // iOS 无工具窗口概念
}

BOOL isUiWindowMinimized(HWND hWnd){
    @autoreleasepool {
        SUIView *view = getUiView(hWnd);
        if(view && view.hostWindow)
            return view.hostWindow.hidden;
        return FALSE;
    }
}

BOOL isUiWindowMaximized(HWND hWnd){
    return FALSE;
}

HWND getHwndFromUiView(void *v){
    @autoreleasepool {
        UIView *view = (__bridge UIView*)v;
        if([view isKindOfClass:[SUIView class]])
            return ((SUIView*)view)->m_hWnd;
    }
    return NULL;
}

// =====================================================================
//  软键盘全局 C API（对应 SUIWindow.h 中新增的三个声明）
//  对齐 Win32 ShowSoftKeyboard / Android showSoftKeyboard。
// =====================================================================

BOOL showUiSoftKeyboard(HWND hWnd, BOOL bShow){
    @autoreleasepool {
        // iOS 弹/收键盘必须在主线程执行；当前不在主线程时通过 dispatch_sync 同步过去
        if (![NSThread isMainThread]) {
            __block BOOL result = NO;
            dispatch_sync(dispatch_get_main_queue(), ^{
                result = showUiSoftKeyboard(hWnd, bShow);
            });
            return result;
        }
        if (hWnd == 0) {
            // hWnd == 0 表示全局操作：
            //   - 收键盘：从 SConnection::m_hFocus 取当前焦点窗口后 resign；
            //   - 弹键盘：无目标 HWND，忽略（返回 NO）。
            if (!bShow) {
                SConnection *conn = SConnMgr::instance()->getConnection(GetCurrentThreadId());
                HWND focus = conn ? conn->GetFocus() : 0;
                if (focus) {
                    SUIView *view = getUiView(focus);
                    if (view && view.isFirstResponder) {
                        return [view resignFirstResponder] ? TRUE : FALSE;
                    }
                }
                return TRUE;
            }
            return FALSE;
        }
        SUIView *view = getUiView(hWnd);
        if (!view) return FALSE;
        return [view toggleSoftKeyboard:bShow];
    }
}

BOOL isUiSoftKeyboardVisible(void){
    @autoreleasepool {
        std::unique_lock<std::mutex> lk(s_kbLock);
        return s_kbVisible;
    }
}

int getUiSoftKeyboardHeight(void){
    @autoreleasepool {
        std::unique_lock<std::mutex> lk(s_kbLock);
        return s_kbHeightPhys;
    }
}
