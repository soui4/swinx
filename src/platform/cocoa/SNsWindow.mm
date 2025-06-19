#import <Cocoa/Cocoa.h>
#include <objc/NSObjCRuntime.h>
#include <cairo-quartz.h>
#include <map>
#include <mutex>
#include <ctypes.h>
#include <winuser.h>
#include <vector>
#include <sysapi.h>
#include "SNsWindow.h"
#include "ctrl_types.h"
#include "wnd.h"
#include "keyboard.h"
#include "helper.h"
#include "tostring.hpp"
#include <uimsg.h>
#include <cursorid.h>
#include "log.h"

#define kLogTag "SNsWindow"

static NSString *NSPasteboardTypeSOUI = @"NSPasteboardTypeSOUI";

extern "C"{
    LONG_PTR GetWindowLongPtrA(HWND hWnd, int nIndex);
}

cairo_t * getNsWindowSurface(HWND hWnd);


static void ConvertNSRect(NSScreen *screen, BOOL fullscreen, NSRect *r)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    r->origin.y = screen_height - r->origin.y - r->size.height;
}

static void ConvertNSPoint(NSScreen *screen, NSSize wndSize, NSPoint *pt)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    pt->y = screen_height - pt->y - wndSize.height;
}

static void RevertNSRect(NSScreen *screen, BOOL fullscreen, NSRect *r)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    r->origin.y = screen_height - r->origin.y - r->size.height;
}

@class SNsWindow;

class NsWndMap{
  public:
    NsWndMap(){
    }
    ~NsWndMap(){
        std::lock_guard<std::mutex> lock(m_mutex);
        SLOG_STMW()<<"remain "<<m_map.size()<<" windows";
        m_map.clear();
    }
    SNsWindow* get(HWND hWnd){
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(hWnd);
        if(it == m_map.end())
            return nullptr;
        return it->second;
    }
    void set(HWND hWnd, SNsWindow* pWin){
      std::lock_guard<std::mutex> lock(m_mutex);
        m_map[hWnd] = pWin;
    }
    void remove(HWND hWnd){
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(hWnd);
        if(it == m_map.end())
            return;
        m_map.erase(hWnd);
    }
  private:
    std::map<HWND, SNsWindow*> m_map;
    std::mutex m_mutex;
};

static NsWndMap s_hwnd2nsWin;



BOOL IsNsWindow(HWND hWnd){
	return s_hwnd2nsWin.get(hWnd)!=nullptr;
}

SNsWindow *getNsWindow(HWND hWnd){
	return s_hwnd2nsWin.get(hWnd);
}

@protocol MouseCapture 
    -(BOOL)setCapture:(SNsWindow *)pWin;
    -(BOOL)releaseCapture:(SNsWindow *)pWin;
@end

// 自定义窗口类
@interface SNsWindowHost : NSWindow<NSWindowDelegate,MouseCapture>
- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag;

-(BOOL)setCapture:(SNsWindow *)pWin;
-(BOOL)releaseCapture:(SNsWindow *)pWin;
-(void)unzoom;
@end


// 自定义窗口类
@interface SNsPanelHost : NSPanel<NSWindowDelegate, MouseCapture>
- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag;

-(BOOL)setCapture:(SNsWindow *)pWin;
-(BOOL)releaseCapture:(SNsWindow *)pWin;

@end

// SNsWindow 实现
@interface SNsWindow : NSView <NSDraggingDestination,NSTextInputClient>{
    @public
    NSRect m_rcPos;
    HWND   m_hWnd;
    BOOL m_bSetActive;
    @private
    NSString *_markedText;
    NSRange   _markedRange;
    NSRange   _selectedRange;
    NSRect    _inputRect;
}
- (instancetype)initWithFrame:(NSRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent;
- (void)destroy;
- (BOOL)startCapture;
- (BOOL)stopCapture;
- (void)onActive: (BOOL)isActive;
- (void)setAlpha:(BYTE)byAlpha;
- (BYTE)getAlpha;
- (void)updateRect:(NSRect)rc;
- (void)invalidRect:(NSRect)rc;
- (void)onStateChange:(int) nState;
@end

@implementation SNsWindow{
    SConnBase *m_pListener;
    BYTE m_byAlpha;
    BOOL m_bMsgTransparent;
    BOOL m_bDrawing;
    BOOL m_bCommitCache;
    NSEventModifierFlags m_modifierFlags;
}

- (instancetype)initWithFrame:(NSRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent{
    m_pListener = listener;
    m_hWnd = (HWND)(__bridge void *)self;
    self = [super initWithFrame:frameRect];
    if (self) {
        self.wantsLayer = YES;    
        m_rcPos = frameRect;
        m_byAlpha = 255;
        m_bMsgTransparent = FALSE;
         m_bDrawing = FALSE;
         m_bCommitCache = FALSE;
         m_bSetActive = FALSE;
         m_modifierFlags = 0;
         _markedText = nil;
    }
    SNsWindow *parent = getNsWindow(hParent);
    if(parent){
        [parent addSubview:self];
    }
    return self;
}

- (void)updateRect:(NSRect)rc;{
    if(m_bDrawing)
        return;
    m_bCommitCache = TRUE;
    [self displayRect:rc];
}

- (void)invalidRect:(NSRect)rc;{
    if(m_bDrawing)
        return;
    if(NSIsEmptyRect(rc))
        return;
    [self setNeedsDisplayInRect:rc];
}

- (void)setMsgTransparent:(BOOL)bTransparent{
    m_bMsgTransparent = bTransparent;
}

-(nullable NSView *)hitTest:(NSPoint)point {
    return  m_bMsgTransparent?nil:[super hitTest:point];
}

- (void)setAlpha:(BYTE)byAlpha{
    m_byAlpha = byAlpha;
    if(self.window){
        [self.window setAlphaValue:(CGFloat)byAlpha/255.0f];
    }
}
- (BYTE)getAlpha{
    return m_byAlpha;
}

- (BOOL)startCapture {
    if(!self.window)
        return FALSE;
    if([self.window conformsToProtocol:@protocol(MouseCapture)]){
        return [(id<MouseCapture>)self.window setCapture:self];
    }else {
        return FALSE;
    }
}

- (BOOL)stopCapture {
    if(!self.window)
        return FALSE;
    if([self.window conformsToProtocol:@protocol(MouseCapture)]){
        return [(id<MouseCapture>)self.window releaseCapture:self];
    }else {
        return FALSE;
    }
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    m_pListener->OnNsEvent(m_hWnd, WM_SIZE, 0,MAKELONG((int)newSize.width, (int)newSize.height));
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    m_bDrawing = TRUE;
    CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
    cairo_surface_t *windowSurface = cairo_quartz_surface_create_for_cg_context(
        cgContext,
        self.bounds.size.width,
        self.bounds.size.height
    );
    cairo_t *windowCr = cairo_create(windowSurface);
    cairo_rectangle(windowCr, dirtyRect.origin.x, dirtyRect.origin.y, dirtyRect.size.width, dirtyRect.size.height);
    cairo_clip(windowCr);
    
    // if(m_bCommitCache){
    //     cairo_t *cr = getNsWindowSurface(m_hWnd);
    //     cairo_surface_t* src_surface = cairo_get_target(cr);
    //     cairo_set_source_surface(windowCr, src_surface, 0, 0);
    //     cairo_rectangle(windowCr, dirtyRect.origin.x, dirtyRect.origin.y, dirtyRect.size.width, dirtyRect.size.height);
    //     cairo_paint(windowCr);
    //     m_bCommitCache = FALSE;      
    // }else
    {
        RECT rc = {(LONG)dirtyRect.origin.x, (LONG)dirtyRect.origin.y, (LONG)(dirtyRect.origin.x+dirtyRect.size.width), (LONG)(dirtyRect.origin.y+dirtyRect.size.height)};
        m_pListener->OnDrawRect(m_hWnd, rc, windowCr);
    }
    
    cairo_destroy(windowCr);
    cairo_surface_destroy(windowSurface);
    m_bDrawing = FALSE;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) onMouseEvent: (NSEvent *) theEvent withMsgId:(UINT) msg{
    UINT uFlags = 0;
    NSEventModifierFlags modifiers = [theEvent modifierFlags];
    NSUInteger pressedButtons = [NSEvent pressedMouseButtons];
    if (modifiers & NSEventModifierFlagShift) {
        uFlags |= MK_SHIFT;
    }
    if (modifiers & NSEventModifierFlagControl) {
        uFlags |= MK_CONTROL;
    }
    if(pressedButtons & (1<<0)){
        uFlags |= MK_LBUTTON;
    }
    if(pressedButtons & (1<<1)){
        uFlags |= MK_RBUTTON;
    }
    if(pressedButtons & (1<<2)){
        uFlags |= MK_MBUTTON;
    }
    if(pressedButtons & (1<<3)){
        uFlags |= MK_XBUTTON1;
    }
    if(pressedButtons & (1<<4)){
        uFlags |= MK_XBUTTON2;
    }
    NSPoint locationInView = [self convertPoint:theEvent.locationInWindow fromView:nil];
    LPARAM lParam = MAKELPARAM((int)locationInView.x,(int)locationInView.y);
    m_pListener->OnNsEvent(m_hWnd,msg,uFlags,lParam);
}

- (void)mouseDragged:(NSEvent *)event {
    [self onMouseEvent:event withMsgId:WM_MOUSEMOVE];
}

- (void)mouseMoved:(NSEvent *) theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSEMOVE];
}

- (void) mouseDown: (NSEvent *) theEvent {
    SLOG_STMI()<<"mouseDown,m_hWnd="<<m_hWnd;
    [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDOWN];
}

- (void) mouseUp: (NSEvent *) theEvent {
        SLOG_STMI()<<"mouseUp,m_hWnd="<<m_hWnd;
    [self onMouseEvent:theEvent withMsgId:WM_LBUTTONUP];
}

- (void)rightMouseDown:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_RBUTTONDOWN];
}

- (void)rightMouseUp:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_RBUTTONUP];
}
- (void)otherMouseDown:(NSEvent *)theEvent{
    if(theEvent.buttonNumber == 2){
        [self onMouseEvent:theEvent withMsgId:WM_MBUTTONDOWN];
    }
}

- (void)otherMouseUp:(NSEvent *)theEvent{
    if(theEvent.buttonNumber == 2){
        [self onMouseEvent:theEvent withMsgId:WM_MBUTTONUP];
    }
}

- (void)mouseEntered:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSEHOVER];
}

- (void)mouseExited:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSELEAVE];
}

- (SHORT) getKeyModifiers{
    SHORT modifiers = 0;
    if(m_modifierFlags & NSEventModifierFlagShift){
        modifiers |= MK_SHIFT;
    }
    if(m_modifierFlags & NSEventModifierFlagControl){
        modifiers |= MK_CONTROL;
    }
    if(m_modifierFlags & NSEventModifierFlagOption){
        //modifiers |= MK_ALT;
    }
    if(m_modifierFlags & NSEventModifierFlagCommand){
        //modifiers |= MK_COMMAND;
    }
    return modifiers;
}

- (void)scrollWheel:(NSEvent *)event{
    CGFloat deltaY = [event scrollingDeltaY];  // 垂直滚动量
    CGFloat deltaX = [event scrollingDeltaX];  // 水平滚动量
    NSPoint location = [event locationInWindow]; // 鼠标位置
    
    // 转换为 Windows 消息参数
    // WM_MOUSEWHEEL 的 wParam 和 lParam:
    // wParam: 高16位是 delta (120单位为一个"刻度")，低16位是按键状态
    // lParam: 低16位是x坐标，高16位是y坐标
    
    // 模拟 WM_MOUSEWHEEL 消息
    WPARAM wParam = MAKEWPARAM([self getKeyModifiers], (short)(deltaY * 120));
    LPARAM lParam = MAKELPARAM((short)location.x, (short)location.y);
    
    // 发送到 Windows 窗口
    m_pListener->OnNsEvent(m_hWnd, WM_MOUSEWHEEL, wParam, lParam);
}

- (void)onKeyDown:(NSEvent *)event{ 
    NSUInteger keyCode = [event keyCode];     // 物理键码
    SLOG_STMI()<<"hjx onkeyDown:"<<keyCode;
    UINT vkCode = convertKeyCodeToVK(keyCode);
    Keyboard::instance().setKeyState(vkCode, 1);
    SHORT repCount=[event isARepeat]?1:0;
    LPARAM lParam = (keyCode<<16)|repCount;
    if([event modifierFlags]&NSEventModifierFlagOption)
    {
        lParam |=(1<<29);
    }
    m_pListener->OnNsEvent(m_hWnd, WM_KEYDOWN, vkCode, lParam);
}

-(BOOL)isFunKey: (NSInteger)keyCode{
    int vk = convertKeyCodeToVK(keyCode);
    return vk >= VK_F1 && vk <= VK_F12;
}

- (void)keyDown:(NSEvent *)event {
    if([self isFunKey: [event keyCode]]){
        [self onKeyDown:event];
        return;
    }
    if (self.inputContext && [self.inputContext handleEvent:event]) {
        return;
    }
    [self onKeyDown:event];
}

- (void)onKeyUp:(NSEvent *)event{
    NSUInteger keyCode = [event keyCode];     // 物理键码
    SLOG_STMI()<<"hjx onkeyUp:"<<keyCode;
    UINT vkCode = convertKeyCodeToVK(keyCode);
    Keyboard::instance().setKeyState(vkCode, 0);
    SHORT repCount=[event isARepeat]?1:0;
    LPARAM lParam = (keyCode<<16)|repCount; 
    if([event modifierFlags]&NSEventModifierFlagOption)
    {
        lParam |=(1<<29);
    }
    m_pListener->OnNsEvent(m_hWnd, WM_KEYUP, vkCode, lParam);
}

- (void)keyUp:(NSEvent *)event{
    if([self isFunKey: [event keyCode]]){
        [self onKeyUp:event];
        return;
    }
    if (self.inputContext && [self.inputContext handleEvent:event]) {
        return;
    }
    [self onKeyUp:event];
}

- (void)checkModifier:(NSEventModifierFlags)modifier 
             current:(NSEventModifierFlags)current 
             changed:(NSEventModifierFlags)changed 
             lParam:(LPARAM)lp
             {
    
    if (changed & modifier) {
        BOOL isPressed = (current & modifier) == modifier;
        WPARAM wp = 0;
        switch(modifier){
            case NSEventModifierFlagShift:
                wp = VK_SHIFT;
                break;
            case NSEventModifierFlagControl:
                wp = VK_CONTROL;
                break;
            case NSEventModifierFlagOption:
                wp = VK_MENU;
                break;
            case NSEventModifierFlagCommand:
                wp = VK_LWIN;
                break;
        }
        m_pListener->OnNsEvent(m_hWnd, isPressed ? WM_SYSKEYDOWN : WM_SYSKEYUP, wp, lp);
    }
}

- (void) flagsChanged : (NSEvent*) event
{
    NSEventModifierFlags current = [event modifierFlags];
    NSEventModifierFlags changed = m_modifierFlags ^ current; // 使用异或找出变化
    LPARAM lp  = event.keyCode<<16;
    [self checkModifier:NSEventModifierFlagShift  current:current changed:changed lParam:lp];
    [self checkModifier:NSEventModifierFlagControl  current:current changed:changed lParam:lp];
    [self checkModifier:NSEventModifierFlagOption  current:current changed:changed lParam:lp];
    [self checkModifier:NSEventModifierFlagCommand  current:current changed:changed lParam:lp];
    
    // 特殊处理 Caps Lock
    if (changed & NSEventModifierFlagCapsLock) {
        BOOL isNowOn = (current & NSEventModifierFlagCapsLock) == NSEventModifierFlagCapsLock;
        m_pListener->OnNsEvent(m_hWnd, isNowOn ? WM_SYSKEYDOWN : WM_SYSKEYUP, VK_CAPITAL, lp);
    }
    
    m_modifierFlags = current;
}

- (void) destroy{
    m_pListener->OnNsEvent(m_hWnd, WM_DESTROY, 0, 0);
}

- (void)onActive: (BOOL)isActive{
    if(m_bSetActive)
        return;
    SLOG_STMI()<<"hjx onActive:"<<isActive<<" hWnd="<<m_hWnd;
    m_pListener->OnNsActive(m_hWnd, isActive);
}

- (void)onStateChange:(int) nState{
    SLOG_STMI()<<"hjx onStateChange:"<<nState<<" hWnd="<<m_hWnd;

    m_pListener->OnNsEvent(m_hWnd,  UM_STATE, nState,0);
}

// 当拖动进入视图时调用
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard *pboard = [sender draggingPasteboard];
    
    // 检查是否是文本类型
    if ([pboard canReadItemWithDataConformingToTypes:@[NSPasteboardTypeSOUI]]) {
        return NSDragOperationCopy;
    }
    
    return NSDragOperationNone;
}

// 当拖动在视图内移动时调用（可选）
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

// 当拖动离开视图时调用
- (void)draggingExited:(nullable id<NSDraggingInfo>)sender {
    //self.layer.borderColor = [[NSColor grayColor] CGColor];
}

// 当释放鼠标执行拖放操作时调用
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard *pboard = [sender draggingPasteboard];
    
    // 读取文本内容
    NSArray *strings = [pboard readObjectsForClasses:@[[NSString class]] options:nil];
    if (strings.count > 0) {
        NSString *droppedString = strings[0];
       
        return YES;
    }
    
    return NO;
}

// 拖放操作完成后调用（可选）
- (void)concludeDragOperation:(nullable id<NSDraggingInfo>)sender {

}

#pragma mark - NSTextInputClient Protocol
-(void)onFunctionKey{
    NSEvent *currentEvent = [NSApp currentEvent];
    SLOG_STMI()<<"hjx onFunctionKey: hWnd="<<m_hWnd;
    if(currentEvent.type==NSEventTypeKeyDown){
        [self onKeyDown:currentEvent];
    }else{
        [self onKeyUp:currentEvent];
    }
}

- (void)pageUp:(id)sender {
    [self onFunctionKey];
}
- (void)pageDown:(id)sender {
    [self onFunctionKey];
}
- (void)pageLeft:(id)sender {
    [self onFunctionKey];
}
- (void)pageRight:(id)sender {
    [self onFunctionKey];
}

- (void)deleteBackward:(id)sender {
    [self onFunctionKey];
}

- (void)deleteForward:(id)sender {
    [self onFunctionKey];
}

- (void)insertNewline:(id)sender {
    [self onFunctionKey];
}

- (void)insertTab:(id)sender {
    [self onFunctionKey];
}

- (void)moveLeft:(id)sender {
    [self onFunctionKey];
}

- (void)moveRight:(id)sender {
    [self onFunctionKey];
}

- (void)moveUp:(id)sender {
    [self onFunctionKey];
}

- (void)moveDown:(id)sender {
    [self onFunctionKey];
}

- (void)insertText:(id)aString replacementRange:(NSRange)replacementRange {
    NSEvent *currentEvent = [NSApp currentEvent];
    if([self hasMarkedText]){
        [self unmarkText];
        //send WM_IME_CHAR  message
        const char *str;
        /* Could be NSString or NSAttributedString, so we have
        * to test and convert it before return as SDL event */
        if ([aString isKindOfClass: [NSAttributedString class]]) {
            str = [[aString string] UTF8String];
        } else {
            str = [aString UTF8String];
        }
        SLOG_STMI()<<"hjx insertText:"<<str<<" hWnd="<<m_hWnd;
        std::wstring wstr;
        towstring(str, -1, wstr);
        for(int i=0;i<wstr.length();i++){
            m_pListener->OnNsEvent(m_hWnd, WM_IME_CHAR, wstr[i], 0);
        }
    }else{
        if(currentEvent.type==NSEventTypeKeyDown){
            [self onKeyDown:currentEvent];
        }else{
            [self onKeyUp:currentEvent];
        }
    }

}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    if ([aString isKindOfClass:[NSAttributedString class]]) {
        aString = [aString string];
    }

    if ([aString length] == 0) {
        [self unmarkText];
        return;
    }

    if (_markedText != aString) {
        _markedText = aString;
    }

    _selectedRange = selectedRange;
    _markedRange = NSMakeRange(0, [aString length]);
}

- (void)unmarkText
{
    _markedText = nil;
}

- (NSRange)selectedRange {
    return _selectedRange;
}

- (NSRange)markedRange {
    return _markedRange;
}

- (BOOL)hasMarkedText {
    return _markedText != nil;
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    return nil;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    return [NSArray array];
}

- (NSRect)firstRectForCharacterRange:(NSRange)aRange actualRange:(NSRangePointer)actualRange
{
    NSWindow *window = [self window];
    NSRect contentRect = [window contentRectForFrameRect:[window frame]];
    float windowHeight = contentRect.size.height;
    NSRect rect = _inputRect;

    if (actualRange) {
        *actualRange = aRange;
    }

    rect = [window convertRectToScreen:rect];

    return rect;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return 0;
}

@end

// SNsWindowHost 实现
@implementation SNsWindowHost{
        id eventMonitor;
        BOOL isResizing;
        NSRect m_defSize;
        BOOL m_bZoomed;
        SNsWindow *m_pCapture;
        __weak NSView * m_pHover;

}

- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag
{
    m_bZoomed = FALSE;
    self = [super initWithContentRect:contentRect
                            styleMask:styleMask 
                              backing:backingType
                                defer:flag];
    [self setAcceptsMouseMovedEvents:YES];
    [self setLevel:(NSWindowLevel)NSNormalWindowLevel];
    [self setDelegate:self];
    self.ignoresMouseEvents = NO;
    self.movableByWindowBackground = NO;
    eventMonitor=nil;
    isResizing = FALSE;
    m_pCapture = nil;
    return self;
}

- (void)resizeWithEvent:(NSEvent *)event {
}

- (void)cursorUpdate:(NSEvent *)event {
}

-(BOOL)setCapture:(SNsWindow *)pWin{
    if (eventMonitor) 
        return FALSE;
//    SLOG_STMI()<<"setCapture hWnd="<<pWin->m_hWnd;
    m_pCapture = pWin;

    __weak typeof(self) weakSelf = self;
    eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
                        NSEventMaskLeftMouseDown | 
                        NSEventMaskLeftMouseUp |
                        NSEventMaskRightMouseDown |
                        NSEventMaskRightMouseUp|
                        NSEventMaskOtherMouseDown|
                        NSEventMaskOtherMouseUp|
                        NSEventMaskMouseMoved
                        handler:^NSEvent *(NSEvent *event) {
        
        typeof(self) strongSelf = weakSelf;
        if (!strongSelf) return event;

        if(event.windowNumber != strongSelf.windowNumber){
            NSPoint ptScreen = [event locationInWindow];
            if(event.window != nil){
                ptScreen = [event.window convertPointToScreen:ptScreen];
            }
            NSWindow *targetWindow = strongSelf;
            NSPoint point = [targetWindow convertPointFromScreen:ptScreen];

            event = [NSEvent mouseEventWithType:event.type
                                            location:point
                                       modifierFlags:event.modifierFlags
                                           timestamp:event.timestamp
                                        windowNumber:strongSelf.windowNumber
                                             context:event.context
                                          eventNumber:event.eventNumber
                                       clickCount:event.clickCount
                                         pressure:event.pressure];            
        }
        switch(event.type){
            case NSLeftMouseDown:
                [m_pCapture mouseDown:event];
                break;
            case NSLeftMouseUp:
                [m_pCapture mouseUp:event];
                break;
            case NSRightMouseDown:
                [m_pCapture rightMouseDown:event];
                break;
            case NSRightMouseUp:
                [m_pCapture rightMouseUp:event];
                break;
            case NSOtherMouseDown:
                [m_pCapture otherMouseDown:event];
                break;
            case NSOtherMouseUp:
                [m_pCapture otherMouseUp:event];
                break;
            case NSMouseMoved:
                [m_pCapture mouseMoved:event];
                break;
            default:
                break;
        }
        return nil;
    }];

    return TRUE;
}

-(BOOL)releaseCapture:(SNsWindow *)pWin{
    if(m_pCapture != pWin)
        return FALSE;
    if (!eventMonitor) 
        return FALSE;
//    SLOG_STMI()<<"releaseCapture hWnd="<<pWin->m_hWnd;
    [NSEvent removeMonitor:eventMonitor];
    eventMonitor = nil;
    m_pCapture = nil;
    return TRUE;
}

-(void)mouseExited:(NSEvent *)event {
    if(!m_pCapture){
        [self.contentView mouseExited:event];
        [[NSCursor arrowCursor] set];
    }
}

- (void)sendEvent:(NSEvent *)event {
    if (event.type == NSEventTypeMouseMoved) {
        [self mouseMoved:event];
    }else if(event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown)
    {
        if([self canBecomeKeyWindow] && ![self isKeyWindow]){
            //[self makeKeyWindow];
        }
        NSView *view = m_pCapture?m_pCapture:[self.contentView hitTest:event.locationInWindow];
        switch(event.type){
            case NSEventTypeLeftMouseDown:
                [view mouseDown:event];                
                break;
            case NSEventTypeRightMouseDown:
                [view rightMouseDown:event];               
                break;
            case NSEventTypeOtherMouseDown:
                [view otherMouseDown:event];
                break;
            default:
                break;
        }
    }
    else if(event.type==NSEventTypeLeftMouseUp || event.type==NSEventTypeRightMouseUp || event.type==NSEventTypeOtherMouseDragged ){
        NSView *view = m_pCapture?m_pCapture:[self.contentView hitTest:event.locationInWindow];
        switch(event.type){
            case NSEventTypeLeftMouseUp:
                [view mouseUp:event];                
                break;
            case NSEventTypeRightMouseUp:
                [view rightMouseUp:event];               
                break;
            case NSEventTypeOtherMouseUp:
                [view otherMouseUp:event];
                break;
            default:
                break;
        }
    }

    else if(event.type==NSEventTypeLeftMouseDragged || event.type==NSEventTypeRightMouseDragged || event.type==NSEventTypeOtherMouseDragged ){
        [self mouseDragged:event];
    }else if(event.type==NSEventTypeMouseMoved){
        [self mouseMoved:event];
    }
    else
    {
        [super sendEvent:event];
    }
}

- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}

- (void)mouseMoved:(NSEvent *)event{
    if(m_pCapture){
        [m_pCapture mouseMoved:(NSEvent *)event];
        return;
    }
//    SLOG_STMI()<<"mouseMoved,pos="<<(int)event.locationInWindow.x<<","<<(int)event.locationInWindow.y;
    NSView * pView = [self.contentView hitTest:event.locationInWindow];
    __strong NSView * pHover = m_pHover;
    if(pView != pHover){
        if(pHover){
            [pHover mouseExited:event];
            m_pHover = nil;
        }
        m_pHover = pView;
        if(m_pHover){
            [m_pHover mouseEntered:event];
        }
    }
    if(pView)
    {
        [pView mouseMoved:(NSEvent *)event];
    }   
}


// 在窗口即将关闭时调用
- (void)close {
    if(m_pCapture){
        [m_pCapture stopCapture];
    }
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"window close, hwnd="<<root->m_hWnd;
    [root destroy];
    [super close]; 
}

- (void)windowDidResize:(NSNotification *)notification{
    
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSArray *screens = [NSScreen screens];
    ConvertNSRect([screens objectAtIndex:0], FALSE, &contentRect);
    SNsWindow * root = self.contentView;
    if(!isResizing)
    {
        isResizing=TRUE;
        SetWindowPos(root->m_hWnd, 0, contentRect.origin.x, contentRect.origin.y, contentRect.size.width, contentRect.size.height, SWP_NOZORDER|SWP_NOACTIVATE);
        isResizing=FALSE;
    }    
    BOOL bZoomed = [self isZoomed];
    if(!bZoomed)
    {
        if(m_bZoomed){
            [root onStateChange:(int)SIZE_RESTORED];
        }
    }else{
        [root onStateChange:(int)SIZE_MAXIMIZED];
    }
    m_bZoomed = bZoomed;
}

- (void)windowDidMove:(NSNotification *)notification {
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSArray *screens = [NSScreen screens];
    ConvertNSRect([screens objectAtIndex:0], FALSE, &contentRect);
    SNsWindow * root = self.contentView;

    isResizing=TRUE;
    SetWindowPos(root->m_hWnd, 0, contentRect.origin.x, contentRect.origin.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    isResizing=FALSE;
}

- (void)setFrame:(NSRect)frameRect display:(BOOL)flag{
    if(isResizing)
        return;
    [super setFrame:frameRect display:flag];
}

- (void)windowDidResignKey:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onActive:FALSE];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onActive:TRUE];
    NSEvent * event = [NSApp currentEvent];
    if(event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown)
    {
        [self sendEvent:event];
    }
}

- (BOOL)canBecomeKeyWindow {
    SNsWindow * root = self.contentView;
    DWORD dwStyle= GetWindowLongA(root->m_hWnd, GWL_STYLE);
    DWORD dwExStyle= GetWindowLongA(root->m_hWnd, GWL_EXSTYLE);
    if(dwExStyle & (WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW))
        return NO;
    if(dwStyle & (WS_CHILD|WS_DISABLED))
        return NO;
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) windowDidMiniaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onStateChange:SIZE_MINIMIZED];
    SLOG_STMI()<<"windowDidMiniaturize,hWnd="<<root->m_hWnd;
}

- (void) windowDidDeminiaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onStateChange:SIZE_RESTORED];
    SLOG_STMI()<<"windowDidDeminiaturize,hWnd="<<root->m_hWnd;
}

- (void) windowDidEnterFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidEnterFullScreen,hWnd="<<root->m_hWnd;
}

- (void) windowDidExitFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidExitFullScreen,hWnd="<<root->m_hWnd;
}

-(void)unzoom{
    if([self isZoomed]){
        [self setFrame:m_defSize display:YES];
    }
}

- (void)zoom:(nullable id)sender;{
    m_defSize = [self frame];
    [super zoom:sender];
}

@end


// SNsPanelHost 实现
@implementation SNsPanelHost{
        id eventMonitor;
        BOOL isResizing;
        SNsWindow *m_pCapture;
}

- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag
{
    self = [super initWithContentRect:contentRect
                            styleMask:styleMask 
                              backing:backingType
                                defer:flag];
    [self setAcceptsMouseMovedEvents:YES];
    [self setDelegate:self];
    eventMonitor=nil;
    isResizing = FALSE;
    return self;
}

-(BOOL)setCapture:(SNsWindow *)pWin{
    if (eventMonitor) 
        return FALSE;
    m_pCapture = pWin;

    __weak typeof(self) weakSelf = self;
    eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:
                        NSEventMaskLeftMouseDown | 
                        NSEventMaskLeftMouseUp |
                        NSEventMaskRightMouseDown |
                        NSEventMaskRightMouseUp|
                        NSEventMaskOtherMouseDown|
                        NSEventMaskOtherMouseUp|
                        NSEventMaskMouseMoved
                        handler:^NSEvent *(NSEvent *event) {
        [weakSelf sendEvent:event];
        return nil;
    }];

    return TRUE;
}

-(BOOL)releaseCapture:(SNsWindow *)pWin{
    if(m_pCapture != pWin)
        return FALSE;
    if (!eventMonitor) 
        return FALSE;
    [NSEvent removeMonitor:eventMonitor];
    eventMonitor = nil;
    m_pCapture = nil;
    return TRUE;
}

 - (void)mouseMoved:(NSEvent *)event{
    if(m_pCapture){
        [m_pCapture mouseMoved:(NSEvent *)event];
    }
    NSView * pView = [self.contentView hitTest:event.locationInWindow];
    if([pView isKindOfClass:[SNsWindow class]]){
        SNsWindow * pNsWin = (SNsWindow *)pView;
        [pNsWin mouseMoved:(NSEvent *)event];
    }
}

// 在窗口即将关闭时调用
- (void)close {
    SNsWindow * root = self.contentView;
    [root destroy];
    [super close]; 
}

- (void)windowDidResize:(NSNotification *)notification{
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSArray *screens = [NSScreen screens];
    ConvertNSRect([screens objectAtIndex:0], FALSE, &contentRect);
    //[self.contentView setFrameSize:contentRect.size];
    SNsWindow * root = self.contentView;
    isResizing=TRUE;
    SetWindowPos(root->m_hWnd, 0, contentRect.origin.x, contentRect.origin.y, contentRect.size.width, contentRect.size.height, SWP_NOZORDER|SWP_NOACTIVATE);
    isResizing=FALSE;
}

- (void)windowDidMove:(NSNotification *)notification {
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSArray *screens = [NSScreen screens];
    ConvertNSRect([screens objectAtIndex:0], FALSE, &contentRect);
    SNsWindow * root = self.contentView;

    isResizing=TRUE;
    SetWindowPos(root->m_hWnd, 0, contentRect.origin.x, contentRect.origin.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    isResizing=FALSE;
}

- (void)setFrame:(NSRect)frameRect display:(BOOL)flag{
    if(isResizing)
        return;
    [super setFrame:frameRect display:flag];
}

- (void)windowDidResignKey:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onActive:FALSE];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onActive:TRUE];
    NSEvent * event = [NSApp currentEvent];
    if(event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown)
    {
        [self sendEvent:event];
    }
}

- (BOOL)canBecomeKeyWindow {
    return NO;
}
@end

HWND createNsWindow(HWND hParent, DWORD dwStyle,DWORD dwExStyle, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener)
{
	NSRect rect = NSMakeRect(x, y, cx, cy);
    if(!(dwStyle&WS_CHILD))
        hParent=0;
    SNsWindow * nswindow = [[SNsWindow alloc] initWithFrame:rect withListener:pListener withParent:hParent];
    HWND hWnd  = nswindow->m_hWnd;
	s_hwnd2nsWin.set(hWnd, nswindow);
	return hWnd;
}


static BOOL IsRootView(SNsWindow *pView){
    if(pView.superview == nil)
        return TRUE;
    if(pView.window == nil)
        return FALSE;
    if(pView == pView.window.contentView)
        return TRUE;
    return FALSE;
}

BOOL showNsWindow(HWND hWnd,int nCmdShow){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    BOOL bRoot = IsRootView(nswindow);
    //SLOG_STMI()<<"hjx showNsWindow: hWnd="<<hWnd<<" nCmdShow="<<nCmdShow;
    if(nCmdShow == SW_HIDE)
    {
        if(bRoot)
        {
            [nswindow.window orderOut:nil];
        }else{
            [nswindow setHidden:YES];
        }
        return TRUE;
    }else{
        if(bRoot)
        {
            DWORD dwStyle = GetWindowLongPtrA(hWnd,GWL_STYLE);
            DWORD dwExStyle = GetWindowLongPtrA(hWnd,GWL_EXSTYLE);
            if(nswindow.window == nil){
                NSWindowStyleMask styleMask = 0;
                if(dwStyle & WS_CAPTION)
                {
                    styleMask |= NSWindowStyleMaskTitled;
                }
                if(dwStyle & WS_THICKFRAME)
                    styleMask |= NSWindowStyleMaskResizable;
                if(dwStyle & WS_MAXIMIZEBOX)
                    styleMask |= NSWindowStyleMaskMiniaturizable;
                if(dwStyle & WS_SYSMENU)
                    styleMask |= NSWindowStyleMaskClosable;
                if(dwExStyle & (WS_EX_NOACTIVATE))
                    styleMask |= NSWindowStyleMaskNonactivatingPanel|NSWindowStyleMaskUtilityWindow;
                NSRect rect = nswindow->m_rcPos;
                NSArray *screens = [NSScreen screens];
                ConvertNSRect([screens objectAtIndex:0], FALSE, &rect);

                NSWindow * host = nil;
                if(dwExStyle & (WS_EX_NOACTIVATE)){
                    host = [[SNsPanelHost alloc] initWithContentRect:rect styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
                }else{
                    host = [[SNsWindowHost alloc] initWithContentRect:rect styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
                }
                [host setContentView:nswindow];
                [host setAnimationBehavior:NSWindowAnimationBehaviorNone];
                [host.contentView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

                assert(nswindow.window != nil);
                if(dwExStyle & WS_EX_TOPMOST){
                    [host setLevel:NSFloatingWindowLevel];
                }
                if(dwExStyle & WS_EX_COMPOSITED){
                    host.backgroundColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.0];
                    [host setOpaque:NO];
                }else{
                    [host setOpaque:YES];
                }
                if(dwExStyle & WS_EX_TRANSPARENT){
                    [host setIgnoresMouseEvents:TRUE];
                }
                //update alpha and msg transparent
                [nswindow setAlpha:[nswindow getAlpha]];

                HWND hParent = GetParent(hWnd);
                if(hParent){
                    SNsWindow * pParent = getNsWindow(hParent);
                    if(pParent){
                        [host setParentWindow:pParent.window];
                    }
                }
            }else{
                if(dwExStyle & WS_EX_TRANSPARENT){
                    [nswindow setMsgTransparent:TRUE];
                }
            }
            assert(nswindow.window != nil);
            if(nCmdShow == SW_SHOWMINIMIZED){
                [nswindow.window miniaturize:nil];
            }else if(nCmdShow == SW_SHOWMAXIMIZED){
                [nswindow.window zoom:nil];
            }
            //SLOG_STMI()<<"showNsWindow:  hWnd="<<nswindow->m_hWnd;
            if(nCmdShow == SW_SHOWNA || nCmdShow == SW_SHOWNOACTIVATE ||![nswindow.window canBecomeKeyWindow]){
                [nswindow.window orderFront:nil];
            }else{
                [nswindow.window makeKeyAndOrderFront:nil];
            }
        }else{
            [nswindow setHidden : NO];
        }
    }
    return TRUE;
}


BOOL setNsWindowPos(HWND hWnd, int x, int y){
    
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    NSRect rect = nswindow->m_rcPos;
    rect.origin.x = x;
    rect.origin.y = y;
    nswindow->m_rcPos=rect;
    if(IsRootView(nswindow)){
        if(nswindow.window != nil){
            NSArray *screens = [NSScreen screens];
            ConvertNSRect([screens objectAtIndex:0], FALSE, &rect);
            [nswindow.window setFrame:rect display:YES animate:NO];
        }
    }else{
        [nswindow setFrameOrigin:(NSPoint)rect.origin];
    }
    return TRUE;
}


BOOL setNsWindowSize(HWND hWnd, int cx, int cy){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    SLOG_STMI()<<"setNsWindowSize, hWnd="<<hWnd<<" cx="<<cx<<" cy="<<cy;
    NSRect rect = nswindow->m_rcPos;
    rect.size.width = cx;
    rect.size.height = cy;
    nswindow->m_rcPos=rect;
    if(IsRootView(nswindow)){
        if(nswindow.window != nil){
            NSArray *screens = [NSScreen screens];
            ConvertNSRect([screens objectAtIndex:0], FALSE, &rect);
            [nswindow.window setFrame:rect display:YES animate:NO];
        }
    }else{
        [nswindow setFrame:rect];
        [nswindow setNeedsDisplay:YES];
    }
    return TRUE;
}


void closeNsWindow(HWND hWnd)
{
	SNsWindow* pWin = s_hwnd2nsWin.get(hWnd);
	if(pWin)
	{
        if(IsRootView(pWin)){
            if(pWin.window){
                [pWin.window close];
                SLOG_STMI()<<"closeNsWindow: hWnd="<<pWin->m_hWnd;
            }
        }else{
            [pWin removeFromSuperview];
        }
		s_hwnd2nsWin.remove(hWnd);
	}
}

HWND getNsWindow(HWND hParent, int code)
{
    SNsWindow *nsParent = getNsWindow(hParent);
    if(!nsParent)
        return 0;
    HWND hRet = 0;
    switch (code)
    {
    case GW_CHILDFIRST:
        if([nsParent.subviews count] > 0) {
            SNsWindow *firstChild = [nsParent.subviews objectAtIndex:0];
            hRet = firstChild->m_hWnd;
        }
        break;
    case GW_CHILDLAST:
        if([nsParent.subviews count] > 0) {
            SNsWindow *lastChild = [nsParent.subviews lastObject];
            hRet = lastChild->m_hWnd;
        }
        break;
    case GW_HWNDFIRST:
        hRet = getNsWindow(hParent, GW_CHILDFIRST);
        break;
    case GW_HWNDLAST:
        hRet = getNsWindow(hParent, GW_CHILDLAST);
        break;
    case GW_HWNDPREV:
        {
            NSView *superview = nsParent.superview;
            if(superview) {
                NSArray *siblings = [superview subviews];
                NSUInteger index = [siblings indexOfObject:nsParent];
                if(index > 0) {
                    SNsWindow *prev = [siblings objectAtIndex:index-1];
                    hRet = prev->m_hWnd;
                }
            }
        }
        break;
    case GW_HWNDNEXT:
        {
            NSView *superview = nsParent.superview;
            if(superview) {
                NSArray *siblings = [superview subviews];
                NSUInteger index = [siblings indexOfObject:nsParent];
                if(index < [siblings count]-1) {
                    SNsWindow *next = [siblings objectAtIndex:index+1];
                    hRet = next->m_hWnd;
                }
            }
        }
        break;
    }
    return hRet;
}

BOOL setNsActiveWindow(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if(nswindow.window == nil)
        return FALSE;
    SLOG_STMI()<<"setNsActiveWindow: hWnd="<<hWnd;
    nswindow->m_bSetActive = TRUE;
    [nswindow.window makeKeyWindow];
    nswindow->m_bSetActive = FALSE;
    return TRUE;
    }
}

HWND getNsActiveWindow(){
    @autoreleasepool {
    NSWindow *activeWindow = [NSApp keyWindow];
    if(activeWindow){
        SNsWindow *nswindow = (SNsWindow *)activeWindow.contentView;
        return nswindow->m_hWnd;
    }
    return 0;
    }
}

BOOL setNsFocusWindow(HWND hWnd){
    @autoreleasepool {
        SLOG_STMI()<<"hjx setNsFocusWindow: hWnd="<<hWnd;
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return FALSE;
        if(nswindow.window == nil)
            return FALSE;
        if(![nswindow.window isKeyWindow])
            [nswindow.window makeKeyWindow];
        [nswindow.window makeFirstResponder:nswindow];
        return TRUE;
    }
}

HWND getNsFocusWindow(){
    @autoreleasepool {
        NSWindow *activeWindow = [NSApp keyWindow];
        if(activeWindow){
            NSResponder *firstResponder = [activeWindow firstResponder];
            if([firstResponder isKindOfClass:[SNsWindow class]]){
                SNsWindow *nswindow = (SNsWindow *)firstResponder;
                return nswindow->m_hWnd;
            }
        }
        return 0;
    }
}

void invalidateNsWindow(HWND hWnd, LPCRECT rc){
    @autoreleasepool {
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return;
        NSRect rect = NSMakeRect(rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top);
        [nswindow invalidRect:rect];
    }
}


void updateNsWindow(HWND hWnd, const RECT &rc){
    @autoreleasepool {
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return;
        NSRect nsRect = NSMakeRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        [nswindow updateRect:nsRect];
    }
}

BOOL isNsWindowVisible(HWND hWnd){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if([nswindow window] == nil)
        return FALSE;
    return [nswindow isHidden] == NO;
}

BOOL getNsWindowRect(HWND hWnd, RECT *rc){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    NSRect rect = nswindow->m_rcPos;
    rc->left = rect.origin.x;
    rc->top = rect.origin.y;
    rc->right = rect.origin.x + rect.size.width;
    rc->bottom = rect.origin.y + rect.size.height;
    return TRUE;
}

static NSScreen *screenForPoint(NSPoint point) {
    // 获取包含该点的显示器ID
    CGDirectDisplayID displayID;
    uint32_t displayCount = 0;
    CGGetDisplaysWithPoint(NSPointToCGPoint(point), 1, &displayID, &displayCount);
    
    if (displayCount > 0) {
        // 将 CGDirectDisplayID 转换为 NSScreen
        for (NSScreen *screen in [NSScreen screens]) {
            NSDictionary *deviceDescription = [screen deviceDescription];
            NSNumber *screenNumber = deviceDescription[@"NSScreenNumber"];
            if ([screenNumber unsignedIntValue] == displayID) {
                return screen;
            }
        }
    }
    
    return [NSScreen mainScreen];
}

static NSWindow *windowAtPoint(NSPoint screenPoint) {
    NSScreen *screen = screenForPoint(screenPoint);
    // 将点转换为屏幕坐标系
    NSPoint windowPoint = screenPoint;
    windowPoint.y = screen.frame.size.height - windowPoint.y;
    // 获取位于该点的窗口编号
    NSInteger windowNumber = [NSWindow windowNumberAtPoint:windowPoint belowWindowWithWindowNumber:0];
    return [NSApp windowWithWindowNumber:windowNumber];
}

HWND hwndFromPoint(HWND hWnd,POINT pt){
    @autoreleasepool{
    NSPoint nspt = {(CGFloat)pt.x,(CGFloat)pt.y};
    SNsWindow *nswindow = nil;
    if(hWnd){
        NSWindow *host = windowAtPoint(nspt);
        nswindow = (SNsWindow *)host.contentView;
    }
    if(!nswindow)
        return 0;
    NSView *view = [nswindow hitTest:nspt];
    if(!view)
        return 0;
    if([view isKindOfClass:[SNsWindow class]]){
        nswindow = (SNsWindow *)view;
        return nswindow->m_hWnd;
    }
    return 0;
    }
}

BOOL setNsWindowZorder(HWND hWnd, HWND hWndInsertAfter){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    return TRUE;
    NSView *root = nswindow;
    while(root.superview != nil)
        root = root.superview;

    NSWindow *window = nswindow.window;
    if(hWndInsertAfter == HWND_TOP){
        [nswindow removeFromSuperview];
        [root addSubview:nswindow positioned:NSWindowAbove relativeTo:nil];
        return TRUE;
    }
    if(hWndInsertAfter == HWND_BOTTOM){
        [nswindow removeFromSuperview];
        [root addSubview:nswindow positioned:NSWindowBelow relativeTo:nil];
        return TRUE;
    }
    SNsWindow * nsInsertAfter = getNsWindow(hWndInsertAfter);
    if(!nsInsertAfter)
        return FALSE;
    [nswindow removeFromSuperview];
    [nsInsertAfter.superview addSubview:nswindow positioned:NSWindowAbove relativeTo:nsInsertAfter];
    return TRUE;
    }
}

BOOL setNsWindowCapture(HWND hWnd){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    return [nswindow startCapture];
}
BOOL releaseNsWindowCapture(HWND hWnd){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    return [nswindow stopCapture];
}

BOOL setNsWindowAlpha(HWND hWnd,BYTE byAlpha){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    [nswindow setAlpha:byAlpha];
    return TRUE;
}
BYTE getNsWindowAlpha(HWND hWnd){
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return 0;
    return [nswindow getAlpha];
}


HWND getNsForegroundWindow() {
    @autoreleasepool {
        NSWindow *topWindow = [[NSApplication sharedApplication] orderedWindows].firstObject;
        if(topWindow && topWindow.contentView != nil){
            SNsWindow *nswindow = (SNsWindow *)topWindow.contentView;
            if(nswindow){
                return nswindow->m_hWnd;
            }
        }
        return NULL;
    }
}

BOOL setNsForegroundWindow(HWND hWnd) {
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if(![nswindow.window isVisible]){
        return FALSE;
    }
    [nswindow.window orderFront:nil];
    return TRUE;
    }
}

BOOL setNsMsgTransparent(HWND hWnd, BOOL bTransparent)
{
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if(nswindow.window != nil)
    {
        [nswindow.window setIgnoresMouseEvents:bTransparent];
    }
    return TRUE;
    }
}

BOOL sendNsSysCommand(HWND hWnd, int nCmd){
    @autoreleasepool {
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return FALSE;
        if(nswindow.window == nil)
            return FALSE;
        if(nCmd == SC_RESTORE){
            if([nswindow.window isMiniaturized])
                [nswindow.window deminiaturize:nil];
            if([nswindow.window isZoomed])
            {
                SNsWindowHost * host = (SNsWindowHost *)nswindow.window;
                if(!host)
                    return FALSE;
                [host unzoom];
            }
            return TRUE;
        }
        if(nCmd == SC_MINIMIZE && ![nswindow.window isMiniaturized]){
            [nswindow.window miniaturize:nil];
            return TRUE;
        }
        if(nCmd == SC_MAXIMIZE && ![nswindow.window isZoomed]){
            [nswindow.window zoom:nil];
            return TRUE;
        }
        return FALSE;
    }
}

NSImage *imageFromHICON(HICON hIcon);

BOOL setNsWindowIcon(HWND hWnd, HICON hIcon, BOOL bBigIcon){
    @autoreleasepool { 
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return FALSE;
        if(nswindow.window == nil)
            return FALSE;
        NSImage *image = imageFromHICON(hIcon);
        if(bBigIcon){
            [NSApp setApplicationIconImage:image];
        }else{
            [[nswindow.window standardWindowButton:NSWindowDocumentIconButton] setImage:image];
        }
        return TRUE;
    }
}

BOOL setNsParent(HWND hWnd, HWND hParent){
    @autoreleasepool {
        if(!(GetWindowLongPtrA(hWnd, GWL_STYLE) & WS_CHILD))
            return FALSE;
        SNsWindow * nsWindow = getNsWindow(hWnd);
        SNsWindow * nsParent = getNsWindow(hParent);
        if(!nsWindow)
            return FALSE;
        if(!nsParent)
        {
            if(nsWindow.window==nil){
                NSWindow * host = nsWindow.window;
                [host setContentView:nil];
                [host close];
            }
        }else{
            [nsParent addSubview:nsWindow];
        }
        return TRUE;
    }
}

BOOL flashNsWindow(HWND hwnd,
        DWORD dwFlags,
        UINT uCount,
        DWORD dwTimeout){
            @autoreleasepool {
            SNsWindow * nsWindow = getNsWindow(hwnd);
            if(!nsWindow)
                return FALSE;
            if(nsWindow.window == nil)
                return FALSE;
            if(dwFlags == FLASHW_STOP){
                [nsWindow.window setHasShadow:YES];
                return TRUE;
            }
            if(dwFlags == FLASHW_ALL){
                [nsWindow.window setHasShadow:NO];
                return TRUE;
            }
            return FALSE;
            
            }
        }

BOOL isNsDropTarget(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nsWindow = getNsWindow(hWnd);
    if(!nsWindow)
        return FALSE;
    return [[nsWindow registeredDraggedTypes] count] > 0;
    }
}

BOOL setNsDropTarget(HWND hWnd, BOOL bEnable){
    @autoreleasepool {
    SNsWindow * nsWindow = getNsWindow(hWnd);
    if(!nsWindow)
        return FALSE;
    if(bEnable){
        [nsWindow registerForDraggedTypes:@[NSFilenamesPboardType,NSPasteboardTypeSOUI]];
    }else{
        [nsWindow unregisterDraggedTypes];
    }
    return TRUE;
    }
}

BOOL EnumDataOjbectCb(WORD fmt, HGLOBAL hMem, void *param){
    @autoreleasepool { 
        NSPasteboardItem * item = (__bridge NSPasteboardItem *)param;
        if(fmt == CF_UNICODETEXT){
            const wchar_t *src = (const wchar_t *)GlobalLock(hMem);
            size_t len = GlobalSize(hMem) / sizeof(wchar_t);
            std::string str;
            tostring(src, len, str);
            GlobalUnlock(hMem);
            [item setString:[NSString stringWithUTF8String:str.c_str()] forType:NSPasteboardTypeString];
        }else if(fmt == CF_TEXT){
            const char *src = (const char *)GlobalLock(hMem);
            size_t len = GlobalSize(hMem);
            std::string str(src, len);
            GlobalUnlock(hMem);
            [item setString:[NSString stringWithUTF8String:str.c_str()] forType:NSPasteboardTypeString];
        }else{
            const void *src = GlobalLock(hMem);
            size_t len = GlobalSize(hMem);
            NSData *data = [NSData dataWithBytes:src length:len];
            GlobalUnlock(hMem);
            [item setData:data forType:[NSString stringWithFormat:@"SOUIClipboardFMT_%d", fmt]];
        }
        return TRUE;
    }
}

HRESULT doNsDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,     
                          DWORD *pdwEffect){
    @autoreleasepool {
        //convert dataobject to nsdataobject
        NSPasteboardItem *item = [[NSPasteboardItem alloc] init];
        EnumDataOjbect(pDataObject, EnumDataOjbectCb, (__bridge void*)item);
        if([item types].count == 0)
            return E_UNEXPECTED;    
        NSDraggingItem *draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:item];

        return S_OK;
    }
}

WORD GetCursorID(HICON hIcon);
POINT GetIconHotSpot(HICON hIcon);

static NSCursor *cursorFromHCursor(HCURSOR cursor){
    @autoreleasepool {
    WORD cursorID = GetCursorID(cursor);
    switch(cursorID){
        case CIDC_ARROW:
            return [NSCursor arrowCursor];
        case CIDC_IBEAM:
            return [NSCursor IBeamCursor];
        case CIDC_WAIT:
            return [NSCursor openHandCursor];
        case CIDC_CROSS:
            return [NSCursor crosshairCursor];
        case CIDC_UPARROW:
            return [NSCursor pointingHandCursor];
        case CIDC_SIZE:
            return [NSCursor arrowCursor];
        case CIDC_SIZEALL:
            return [NSCursor arrowCursor];
        case CIDC_ICON:
            return [NSCursor arrowCursor];
        case CIDC_SIZENWSE:
            return [NSCursor resizeLeftRightCursor];//todo:hjx
        case CIDC_SIZENESW:
            return [NSCursor resizeLeftRightCursor];//todo:hjx
        case CIDC_SIZEWE:
            return [NSCursor resizeLeftRightCursor];
        case CIDC_SIZENS:
            return [NSCursor resizeUpDownCursor];
        case CIDC_NO:
            return [NSCursor operationNotAllowedCursor];
        case CIDC_HAND:
            return [NSCursor pointingHandCursor];
        case CIDC_APPSTARTING:
            return [NSCursor pointingHandCursor];
        case CIDC_HELP:
            return [NSCursor pointingHandCursor];
    }
    static std::map<HCURSOR, NSCursor *> s_cursorMap;
    auto it = s_cursorMap.find(cursor);
    if(it != s_cursorMap.end())
        return it->second;
    NSImage  *nsImage = imageFromHICON(cursor);
    POINT hotSpot = GetIconHotSpot(cursor);
    NSCursor *nsCursor = [[NSCursor alloc] initWithImage:nsImage hotSpot:NSMakePoint(hotSpot.x, hotSpot.y)];
    s_cursorMap.insert(std::make_pair(cursor, nsCursor));
    return nsCursor;
    }
}

BOOL setNsWindowCursor(HWND hWnd, HCURSOR cursor){
    @autoreleasepool {
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return FALSE;
        if(nswindow.window == nil)
            return FALSE;
        if(!cursor){
            [[NSCursor arrowCursor] set];
        }else{
            [cursorFromHCursor(cursor) set];
        }
        return TRUE;
    }
}