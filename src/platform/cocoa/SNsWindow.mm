#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/objc.h>
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

cairo_t * getNsWindowCanvas(HWND hWnd);

#define float2int(x) (int)floor((x)+0.5f)
static RECT NSRect2Rect(NSRect r)
{
    RECT ret;
    ret.left = float2int(r.origin.x);
    ret.top = float2int(r.origin.y);
    ret.right = float2int(r.origin.x + r.size.width);
    ret.bottom = float2int(r.origin.y + r.size.height);
    return ret;
}

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


SNsWindow *getNsWindow(HWND hWnd){
    @try{
        return (__bridge SNsWindow *)(void*)hWnd;
    }@catch(NSException *exception){
        SLOG_STMW()<<"getNsWindow() exception:"<<exception.description.UTF8String;
        return nil;
    }
}

BOOL IsNsWindow(HWND hWnd){
	return getNsWindow(hWnd) != nil;
}


@protocol MouseCapture 
    -(BOOL)setCapture:(SNsWindow *)pWin;
    -(BOOL)releaseCapture:(SNsWindow *)pWin;
@end

@protocol SizeingMark
    -(void)setSizeingMark:(BOOL) bSizeing;
@end

// 自定义窗口类
@interface SNsWindowHost : NSWindow<NSWindowDelegate,MouseCapture, SizeingMark>
- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag;

-(BOOL)setCapture:(SNsWindow *)pWin;
-(BOOL)releaseCapture:(SNsWindow *)pWin;
-(void)unzoom;
-(void)setSizeingMark:(BOOL) bSizeing;
@end

// 自定义窗口类
@interface SNsPanelHost : NSPanel<NSWindowDelegate,MouseCapture, SizeingMark>
- (instancetype)initWithContentRect : (NSRect)contentRect 
styleMask:(NSWindowStyleMask)styleMask 
backing:(NSBackingStoreType)backingType 
defer:(BOOL)flag;

-(BOOL)setCapture:(SNsWindow *)pWin;
-(BOOL)releaseCapture:(SNsWindow *)pWin;
-(void)unzoom;
-(void)setSizeingMark:(BOOL) bSizeing;
@end

// SNsWindow 实现
@interface SNsWindow : NSView <NSDraggingDestination,NSTextInputClient>{
    @public
    NSRect m_rcPos;
    HWND   m_hWnd;
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
- (void)setEnabled:(BOOL)bEnabled;
@end

@implementation SNsWindow{
    SConnBase *m_pListener;
    BYTE m_byAlpha;
    BOOL m_bMsgTransparent;
    BOOL m_bCommitCache;
    NSEventModifierFlags m_modifierFlags;

    NSString *_markedText;
    NSRange   _markedRange;
    NSRange   _selectedRange;
    NSRect    _inputRect;
    BOOL      _bEnabled;
    cairo_surface_t * _offscreenSur;
}

- (instancetype)initWithFrame:(NSRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent{
    m_rcPos = frameRect;
    NSScreen * screen = [NSScreen mainScreen];//todo, get screen from position.
    float scale = [screen backingScaleFactor];
    frameRect.origin.x /= scale;
    frameRect.origin.y /= scale;
    frameRect.size.width /= scale;
    frameRect.size.height /= scale;

    self = [super initWithFrame:frameRect];
    if (!self)
        return nil;
    m_hWnd = (HWND)(__bridge_retained void *)self;
    SLOG_STMI()<<"hjx SNsWindow initWithFrame, m_hWnd="<<m_hWnd;
    m_pListener = listener;
    m_byAlpha = 255;
    m_bMsgTransparent = FALSE;
    m_bCommitCache = FALSE;
    m_modifierFlags = 0;
    _markedText = nil;
    _offscreenSur = nil;
    _bEnabled = TRUE;

    SNsWindow *parent = getNsWindow(hParent);
    if (parent) {
      [parent addSubview:self];
    }
    return self;
}

-(void)setEnabled:(BOOL)bEnabled {
    _bEnabled = bEnabled;
    [self setNeedsDisplay:TRUE];
}

- (void) destroy{
    [[NSNotificationCenter defaultCenter] removeObserver:self]; 
    //[self removeObserver:self forKeyPath:(nonnull NSString *)]
    [self removeFromSuperview];
    m_pListener->OnNsEvent(m_hWnd, WM_DESTROY, 0, 0);
    SLOG_STMI()<<"hjx destroy: hWnd="<<m_hWnd;
    CFBridgingRelease((void*)m_hWnd);
    if(_offscreenSur){
        cairo_surface_destroy(_offscreenSur);
        _offscreenSur = nil;       
    }
}

- (void)dealloc
{
    SLOG_STMI()<<"hjx SNsWindow dealloc, m_hWnd="<<m_hWnd;
}

- (void)updateRect:(NSRect)rc;{
    m_bCommitCache = TRUE;
    [self displayRect:rc];
}

- (void)invalidRect:(NSRect)rc;{
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

- (void)layout {
    [super layout];
    NSRect rect = self.frame;
    float scale = [self.window backingScaleFactor];
    int wid = (int)(rect.size.width * scale);
    int hei = (int)(rect.size.height * scale);
    if(_offscreenSur!=nil){
        int oldWid = cairo_image_surface_get_width(_offscreenSur);
        int oldHei = cairo_image_surface_get_height(_offscreenSur);
        if(oldWid == wid && oldHei == hei)
            return;
        cairo_surface_destroy(_offscreenSur);
        _offscreenSur = nil;
    }
    _offscreenSur = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, wid, hei);
}

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
    cairo_surface_t *windowSurface = cairo_quartz_surface_create_for_cg_context(
        cgContext,
        self.bounds.size.width,
        self.bounds.size.height
    );

    cairo_t *windowCr = cairo_create(windowSurface);
    cairo_rectangle(windowCr, dirtyRect.origin.x, dirtyRect.origin.y, dirtyRect.size.width, dirtyRect.size.height);
    cairo_clip(windowCr);
    float scale = [self.window backingScaleFactor];
    cairo_scale(windowCr, 1.0f/scale, 1.0f/scale);
    // if(m_bCommitCache){
    //     cairo_t *cr = getNsWindowCanvas(m_hWnd);
    //     cairo_surface_t* src_surface = cairo_get_target(cr);
    //     cairo_set_source_surface(windowCr, src_surface, 0, 0);
    //     cairo_rectangle(windowCr, dirtyRect.origin.x, dirtyRect.origin.y, dirtyRect.size.width, dirtyRect.size.height);
    //     cairo_paint(windowCr);
    //     m_bCommitCache = FALSE;      
    // }else
    {
        dirtyRect.origin.x *= scale;
        dirtyRect.origin.y *= scale;
        dirtyRect.size.width *= scale;
        dirtyRect.size.height *= scale;

        cairo_t * offscreenCr = cairo_create(_offscreenSur);
        RECT rc = {(LONG)dirtyRect.origin.x, (LONG)dirtyRect.origin.y, (LONG)(dirtyRect.origin.x+dirtyRect.size.width), (LONG)(dirtyRect.origin.y+dirtyRect.size.height)};
        m_pListener->OnDrawRect(m_hWnd, rc, offscreenCr);
        cairo_destroy(offscreenCr);
        cairo_set_source_surface(windowCr, _offscreenSur,0,0);
        cairo_paint(windowCr);
    }
    
    cairo_destroy(windowCr);
    cairo_surface_destroy(windowSurface);
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) onMouseEvent: (NSEvent *) theEvent withMsgId:(UINT) msg{
    if(!_bEnabled && msg != WM_MOUSEMOVE)
        return;
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
    float scale = [self.window backingScaleFactor];
    locationInView.x *= scale;
    locationInView.y *= scale;

    LPARAM lParam = MAKELPARAM(float2int(locationInView.x),float2int(locationInView.y));
    m_pListener->OnNsEvent(m_hWnd,msg,uFlags,lParam);
}

- (void)mouseDragged:(NSEvent *)event {
    [self onMouseEvent:event withMsgId:WM_MOUSEMOVE];
}

- (void)mouseMoved:(NSEvent *) theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSEMOVE];
}

- (void) mouseDown: (NSEvent *) theEvent {
    [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDOWN];
}

- (void) mouseUp: (NSEvent *) theEvent {
//        SLOG_STMI()<<"mouseUp,m_hWnd="<<m_hWnd;
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
    NSPoint location = [NSEvent mouseLocation];
    location.y = [self.window.screen frame].size.height - location.y;//convert to ns coordinate
    float scale = [self.window backingScaleFactor];
    location.x *= scale;
    location.y *= scale;
    WPARAM wParam = MAKEWPARAM([self getKeyModifiers], (short)(deltaY * 120));
    LPARAM lParam = MAKELPARAM(float2int(location.x),float2int(location.y));    
    // 发送到 Windows 窗口
    m_pListener->OnNsEvent(m_hWnd, WM_MOUSEWHEEL, wParam, lParam);
}

- (void)onKeyDown:(NSEvent *)event{ 
    NSUInteger keyCode = [event keyCode];     // 物理键码
    //SLOG_STMI()<<"hjx onkeyDown:"<<keyCode;
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
    if(!_bEnabled)
        return;
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
    //SLOG_STMI()<<"hjx onkeyUp:"<<keyCode;
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
    if(!_bEnabled)
        return;
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

- (void)onActive: (BOOL)isActive{
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
        BOOL m_bSizing;
        BOOL m_bZoomed;
        NSRect m_defSize;
        SNsWindow *m_pCapture;
        NSView * m_pHover;
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
    [self setLevel:(NSWindowLevel)NSNormalWindowLevel];
    [self setDelegate:self];
    self.ignoresMouseEvents = NO;
    self.movableByWindowBackground = NO;
    eventMonitor=nil;
    m_bSizing = FALSE;
    m_bZoomed = FALSE;
    m_pCapture = nil;
    m_pHover = nil;
    return self;
}

-(BOOL)setCapture:(SNsWindow *)pWin{
    if (m_pCapture==pWin)
        return TRUE;    
    if (eventMonitor || m_pCapture) 
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
                [strongSelf->m_pCapture mouseDown:event];
                break;
            case NSLeftMouseUp:
                [strongSelf->m_pCapture mouseUp:event];
                break;
            case NSRightMouseDown:
                [strongSelf->m_pCapture rightMouseDown:event];
                break;
            case NSRightMouseUp:
                [strongSelf->m_pCapture rightMouseUp:event];
                break;
            case NSOtherMouseDown:
                [strongSelf->m_pCapture otherMouseDown:event];
                break;
            case NSOtherMouseUp:
                [strongSelf->m_pCapture otherMouseUp:event];
                break;
            case NSMouseMoved:
                [strongSelf->m_pCapture mouseMoved:event];
                break;
            default:
                break;
        }
        return nil;
    }];

    return TRUE;
}

-(void) dealloc {
    SLOG_STMI()<<"hjx dealloc SNsWindowHost, self="<<(long long)self;
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

-(void)setSizeingMark:(BOOL) bSizeing{
    m_bSizing = bSizeing;
}

-(void)mouseExited:(NSEvent *)event {
    if(!m_pCapture){
        if(m_pHover){
            [m_pHover mouseExited:event];
            m_pHover = nil;
        }
        [[NSCursor arrowCursor] set];
    }
}

- (void)sendEvent:(NSEvent *)event {
    if (event.type == NSEventTypeMouseMoved) {
        [self mouseMoved:event];
    }else if(event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown)
    {
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
    NSView * pHover = [self.contentView hitTest:event.locationInWindow];
    if(m_pHover != pHover){
        if(m_pHover){
            [m_pHover mouseExited:event];
            m_pHover = nil;
        }
        m_pHover = pHover;
        if(m_pHover){
            [m_pHover mouseEntered:event];
        }
    }
    if(pHover)
    {
        [pHover mouseMoved:(NSEvent *)event];
    }   
}

- (void)close {
    m_pHover = nil;
    if(m_pCapture){
        [self releaseCapture:m_pCapture];
    }
    [self makeFirstResponder:nil];
    [self setContentView:nil];
    [self setDelegate:nil];
    [self orderOut:nil];
    [self update];
    //[self setReleasedWhenClosed:YES];
    //[super close];
    //SLOG_STMI()<<"window close, hwnd="<<root->m_hWnd;
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    BOOL bZoomed = [self isZoomed];
    if(bZoomed){
        [root onStateChange:SIZE_MAXIMIZED];
    }else {
        [root onStateChange:SIZE_RESTORED];
    }
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onStateChange:SIZE_MINIMIZED];
}

- (void)windowDidResize:(NSNotification *)notification{
    if(m_bSizing)
        return;
    SNsWindow * root = self.contentView;
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSScreen *screen = [self screen];
    ConvertNSRect(screen, FALSE, &contentRect);
    float scale = [screen backingScaleFactor];
    contentRect.origin.x *= scale;
    contentRect.origin.y *= scale;
    contentRect.size.width *= scale;
    contentRect.size.height *= scale;
    m_bSizing=TRUE;
    RECT rc = NSRect2Rect(contentRect);
    SetWindowPos(root->m_hWnd,0,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,SWP_NOZORDER|SWP_NOACTIVATE);
    m_bSizing=FALSE;  
    BOOL bZoomed = [self isZoomed];
    if(bZoomed){
        [root onStateChange:SIZE_MAXIMIZED];
    }else {
        [root onStateChange:SIZE_RESTORED];
    }
}

- (void)windowDidMove:(NSNotification *)notification {
    if(m_bSizing)
        return;
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSScreen *screen = [self screen];
    ConvertNSRect(screen, FALSE, &contentRect);
    float scale = [screen backingScaleFactor];
    contentRect.origin.x *= scale;
    contentRect.origin.y *= scale;
    contentRect.size.width *= scale;
    contentRect.size.height *= scale;

    SNsWindow * root = self.contentView;
    m_bSizing=TRUE;
    RECT rc = NSRect2Rect(contentRect);
    SetWindowPos(root->m_hWnd,0,rc.left,rc.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    m_bSizing=FALSE;
}

- (void)setFrame:(NSRect)frameRect display:(BOOL)flag{
    if(m_bSizing)
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
    if(!root)
        return NO;
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

- (void) windowDidEnterFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidEnterFullScreen,hWnd="<<root->m_hWnd;
}

- (void) windowDidExitFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidExitFullScreen,hWnd="<<root->m_hWnd;
}

-(void)unzoom{
    if(m_bZoomed){
        m_bZoomed = FALSE;
        [self setFrame:m_defSize display:YES animate:YES];
    }
}

- (void)zoom:(nullable id)sender;{
    if(!m_bZoomed){
        m_defSize = [self frame];
        NSScreen * screen = [self screen];
        NSRect frame = [screen visibleFrame];
        m_bZoomed = TRUE;
        [self setFrame:frame display:YES animate:YES];
    }
}

-(BOOL)isZoomed{
    return m_bZoomed;
}

@end



// SNsPanelHost 实现
@implementation SNsPanelHost{
        id eventMonitor;
        BOOL m_bSizing;
        BOOL m_bZoomed;
        NSRect m_defSize;
        SNsWindow *m_pCapture;
        NSView * m_pHover;
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
    [self setLevel:(NSWindowLevel)NSNormalWindowLevel];
    [self setDelegate:self];
    self.ignoresMouseEvents = NO;
    self.movableByWindowBackground = NO;
    eventMonitor=nil;
    m_bSizing = FALSE;
    m_bZoomed = FALSE;
    m_pCapture = nil;
    m_pHover = nil;
    return self;
}

-(BOOL)setCapture:(SNsWindow *)pWin{
        return TRUE;
    if (eventMonitor) 
        return FALSE;
    if(m_pCapture!=nil)
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
                [strongSelf->m_pCapture mouseDown:event];
                break;
            case NSLeftMouseUp:
                [strongSelf->m_pCapture mouseUp:event];
                break;
            case NSRightMouseDown:
                [strongSelf->m_pCapture rightMouseDown:event];
                break;
            case NSRightMouseUp:
                [strongSelf->m_pCapture rightMouseUp:event];
                break;
            case NSOtherMouseDown:
                [strongSelf->m_pCapture otherMouseDown:event];
                break;
            case NSOtherMouseUp:
                [strongSelf->m_pCapture otherMouseUp:event];
                break;
            case NSMouseMoved:
                [strongSelf->m_pCapture mouseMoved:event];
                break;
            default:
                break;
        }
        return nil;
    }];

    return TRUE;
}

-(void) dealloc {
    //SLOG_STMI()<<"Dealloc SNsPanelHost, self="<<self;
}

-(BOOL)releaseCapture:(SNsWindow *)pWin{
        return TRUE;
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

-(void)setSizeingMark:(BOOL) bSizeing{
    m_bSizing = bSizeing;
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
    NSView * pHover = [self.contentView hitTest:event.locationInWindow];
    if(m_pHover != pHover){
        if(m_pHover){
            [m_pHover mouseExited:event];
            m_pHover = nil;
        }
        m_pHover = pHover;
        if(m_pHover){
            [m_pHover mouseEntered:event];
        }
    }
    if(pHover)
    {
        [pHover mouseMoved:(NSEvent *)event];
    }   
}

- (void)close {
    m_pHover = nil;
    if(m_pCapture){
        [self releaseCapture:m_pCapture];
    }
    [self makeFirstResponder:nil];
    [self setContentView:nil];
    [self setDelegate:nil];
    [self orderOut:nil];
    [self update];
    //[super close];
    //SLOG_STMI()<<"window close, hwnd="<<root->m_hWnd;
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    BOOL bZoomed = [self isZoomed];
    if(bZoomed){
        [root onStateChange:SIZE_MAXIMIZED];
    }else {
        [root onStateChange:SIZE_RESTORED];
    }
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    [root onStateChange:SIZE_MINIMIZED];
}

- (void)windowDidResize:(NSNotification *)notification{
    if(m_bSizing)
        return;
    SNsWindow * root = self.contentView;
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSScreen *screen = [self screen];
    ConvertNSRect(screen, FALSE, &contentRect);
    float scale = [screen backingScaleFactor];
    contentRect.origin.x *= scale;
    contentRect.origin.y *= scale;
    contentRect.size.width *= scale;
    contentRect.size.height *= scale;
    m_bSizing=TRUE;
    RECT rc = NSRect2Rect(contentRect);
    SetWindowPos(root->m_hWnd,0,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,SWP_NOZORDER|SWP_NOACTIVATE);
    m_bSizing=FALSE;  
    BOOL bZoomed = [self isZoomed];
    if(bZoomed){
        [root onStateChange:SIZE_MAXIMIZED];
    }else {
        [root onStateChange:SIZE_RESTORED];
    }
}

- (void)windowDidMove:(NSNotification *)notification {
    if(m_bSizing)
        return;
    NSRect contentRect = [self contentRectForFrameRect:[self frame]];
    NSScreen *screen = [self screen];
    ConvertNSRect(screen, FALSE, &contentRect);
    float scale = [screen backingScaleFactor];
    contentRect.origin.x *= scale;
    contentRect.origin.y *= scale;
    contentRect.size.width *= scale;
    contentRect.size.height *= scale;

    SNsWindow * root = self.contentView;
    m_bSizing=TRUE;
    RECT rc = NSRect2Rect(contentRect);
    SetWindowPos(root->m_hWnd,0,rc.left,rc.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    m_bSizing=FALSE;
}

- (void)setFrame:(NSRect)frameRect display:(BOOL)flag{
    if(m_bSizing)
        return;
    [super setFrame:frameRect display:flag];
}

- (BOOL)canBecomeKeyWindow {
    return NO;
}
- (BOOL)acceptsFirstResponder {
    return YES;
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
    if(m_bZoomed){
        m_bZoomed = FALSE;
        [self setFrame:m_defSize display:YES animate:YES];
    }
}

- (void)zoom:(nullable id)sender;{
    if(!m_bZoomed){
        m_defSize = [self frame];
        NSScreen * screen = [self screen];
        NSRect frame = [screen visibleFrame];
        m_bZoomed = TRUE;
        [self setFrame:frame display:YES animate:YES];
    }
}

-(BOOL) isZoomed {
    return m_bZoomed;
}
@end

static NSScreen * getNsScreen(HWND hWnd){
    @autoreleasepool {
        if(hWnd){
            SNsWindow * nswindow = getNsWindow(hWnd);
            if(nswindow && nswindow.window){
                return [nswindow.window screen];
            }
        }
        return [NSScreen mainScreen];
    }
}

HWND createNsWindow(HWND hParent, DWORD dwStyle,DWORD dwExStyle, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener)
{
    @autoreleasepool {
	NSRect rect = NSMakeRect(x, y, cx, cy);
    if(!(dwStyle&WS_CHILD))
        hParent=0;
    SNsWindow * nswindow = [[SNsWindow alloc] initWithFrame:rect withListener:pListener withParent:hParent];
    return nswindow->m_hWnd;
    }
}


static BOOL IsRootView(SNsWindow *pView){
    @autoreleasepool {
    if(pView.superview == nil)
        return TRUE;
    if(pView.window == nil)
        return FALSE;
    if(pView == pView.window.contentView)
        return TRUE;
    return FALSE;
    }
}

BOOL showNsWindow(HWND hWnd,int nCmdShow){
    @autoreleasepool {
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
                    if(dwStyle & WS_THICKFRAME)//keep resize only for window with caption 
                        styleMask |= NSWindowStyleMaskResizable;
                }
                if(dwStyle & WS_MAXIMIZEBOX)
                    styleMask |= NSWindowStyleMaskMiniaturizable;
                if(dwStyle & WS_SYSMENU)
                    styleMask |= NSWindowStyleMaskClosable;
                if(dwExStyle & (WS_EX_NOACTIVATE))
                    styleMask |= NSWindowStyleMaskNonactivatingPanel|NSWindowStyleMaskUtilityWindow;
                NSScreen *screen = getNsScreen(hWnd);
                NSRect rect = nswindow->m_rcPos;
                float scale = [screen backingScaleFactor];
                rect.origin.x /= scale;
                rect.origin.y /= scale;
                rect.size.width /= scale;
                rect.size.height /= scale;

                ConvertNSRect(screen, FALSE, &rect);
                NSWindow *host=nil;
                if(dwExStyle & WS_EX_NOACTIVATE)
                    host = [[SNsPanelHost alloc] initWithContentRect:rect styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
                else
                    host = [[SNsWindowHost alloc] initWithContentRect:rect styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
                [host setContentView:nswindow];
                [host setAnimationBehavior:NSWindowAnimationBehaviorNone];
                assert(nswindow.window != nil);
                if(dwExStyle & WS_EX_TOPMOST){
                    [host setLevel:NSFloatingWindowLevel];
                }
                host.backgroundColor = [NSColor colorWithCalibratedWhite:1.0 alpha:0.0];
                if(dwExStyle & WS_EX_COMPOSITED){
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
            if(nCmdShow == SW_SHOWNA || nCmdShow == SW_SHOWNOACTIVATE ||![nswindow.window canBecomeKeyWindow]){
                [nswindow.window orderFront:nil];
            }else{
                [nswindow.window makeKeyAndOrderFront:nil];
                [nswindow onActive:TRUE];//force active
            }
        }else{
            [nswindow setHidden : NO];
        }
    }
    return TRUE;
    }
}


BOOL setNsWindowPos(HWND hWnd, int x, int y){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    NSRect rect = nswindow->m_rcPos;
    rect.origin.x = x;
    rect.origin.y = y;
    nswindow->m_rcPos=rect;
    NSScreen *screen = getNsScreen(hWnd);
    float scale = [screen backingScaleFactor];
    rect.origin.x /= scale;
    rect.origin.y /= scale;
    rect.size.width /= scale;
    rect.size.height /= scale;

    if(IsRootView(nswindow)){
        if(nswindow.window != nil){
            ConvertNSRect(screen, FALSE, &rect);

            [(id<SizeingMark>)nswindow.window setSizeingMark:TRUE];
            [nswindow.window setFrameOrigin:rect.origin];
            [(id<SizeingMark>)nswindow.window setSizeingMark:FALSE];
        }
    }else{
        [nswindow setFrameOrigin:rect.origin];
    }
    return TRUE;
    }
}


BOOL setNsWindowSize(HWND hWnd, int cx, int cy){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    SLOG_STMI()<<"setNsWindowSize, hWnd="<<hWnd<<" cx="<<cx<<" cy="<<cy;
    NSRect rect = nswindow->m_rcPos;
    rect.size.width = cx;
    rect.size.height = cy;
    nswindow->m_rcPos=rect;

    NSScreen *screen = getNsScreen(hWnd);
    float scale = [screen backingScaleFactor];
    rect.origin.x /= scale;
    rect.origin.y /= scale;
    rect.size.width /= scale;
    rect.size.height /= scale;
    if(IsRootView(nswindow)){
        if(nswindow.window != nil){
            ConvertNSRect(screen, FALSE, &rect);
            [(id<SizeingMark>)nswindow.window setSizeingMark:TRUE];
            [nswindow.window setFrame:rect display:YES animate:NO];
            [(id<SizeingMark>)nswindow.window setSizeingMark:FALSE];
        }
    }else{
        [nswindow setFrame:rect];
        [nswindow setNeedsDisplay:YES];
    }
    return TRUE;
    }
}


void closeNsWindow(HWND hWnd)
{
    @autoreleasepool 
    {
	SNsWindow* pWin = getNsWindow(hWnd);
	if(pWin)
	{
        BOOL bRoot = IsRootView(pWin);
        if(bRoot && pWin.window){
            if(pWin.window){
                [pWin.window close];
                SLOG_STMW()<<"hjx closeNsWindow: hWnd="<<hWnd;
            }
        }
        [pWin destroy];
	}else{
        SLOG_STMW()<<"hjx closeNsWindow: hWnd="<<hWnd<<" not found";
    }
    }
}

HWND getNsWindow(HWND hParent, int code)
{
    @autoreleasepool {
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
}

BOOL setNsActiveWindow(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if(nswindow.window == nil)
        return FALSE;
    SLOG_STMI()<<"setNsActiveWindow: hWnd="<<hWnd;
    [nswindow.window makeKeyWindow];
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
    SLOG_STMW()<<"hjx No active window!";
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
        if([nswindow.window isMiniaturized])
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
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if([nswindow window] == nil)
        return FALSE;
    if(IsRootView(nswindow))
        return [nswindow.window isVisible];
    else
        return ![nswindow isHidden];
    }
}

BOOL getNsWindowRect(HWND hWnd, RECT *rc){
    @autoreleasepool {
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
}

static NSScreen *screenForPoint(NSPoint point) {
    @autoreleasepool {
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
}

static NSWindow *windowAtPoint(NSPoint screenPoint) {
    @autoreleasepool {
    NSScreen *screen = screenForPoint(screenPoint);
    // 将点转换为屏幕坐标系
    NSPoint windowPoint = screenPoint;
    windowPoint.y = screen.frame.size.height - windowPoint.y;
    // 获取位于该点的窗口编号
    NSInteger windowNumber = [NSWindow windowNumberAtPoint:windowPoint belowWindowWithWindowNumber:0];
    return [NSApp windowWithWindowNumber:windowNumber];
    }
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
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    return [nswindow startCapture];
    }
}
BOOL releaseNsWindowCapture(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    return [nswindow stopCapture];
    }
}

BOOL setNsWindowAlpha(HWND hWnd,BYTE byAlpha){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    [nswindow setAlpha:byAlpha];
    return TRUE;
    }
}
BYTE getNsWindowAlpha(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return 0;
    return [nswindow getAlpha];
    }
}


HWND getNsForegroundWindow() {
    @autoreleasepool {
        NSWindow *topWindow = [[NSApplication sharedApplication] orderedWindows].firstObject;
        if(topWindow && topWindow.contentView != nil){
            SNsWindow *nswindow = (SNsWindow *)topWindow.contentView;
            if(nswindow){
                SLOG_STMI()<<"getNsForegroundWindow: hWnd="<<nswindow->m_hWnd;
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
    SLOG_STMI()<<"setNsForegroundWindow: hWnd="<<nswindow->m_hWnd;
    [nswindow.window orderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
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

BOOL flashNsWindow(HWND hwnd, DWORD dwFlags, UINT uCount, DWORD dwTimeout) {
  @autoreleasepool {
    SNsWindow *nsWindow = getNsWindow(hwnd);
    if (!nsWindow)
      return FALSE;
    if (nsWindow.window == nil)
      return FALSE;
    if (dwFlags == FLASHW_STOP) {
      [nsWindow.window setHasShadow:YES];
      return TRUE;
    }
    if (dwFlags == FLASHW_ALL) {
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
    //SLOG_STMI()<<"hjx, cursorID: "<<cursorID;
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
    int height = nsImage.size.height;
    NSCursor *nsCursor = [[NSCursor alloc] initWithImage:nsImage hotSpot:NSMakePoint(hotSpot.x, height-hotSpot.y)];
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

BOOL getNsCursorPos(LPPOINT ppt) {
    @autoreleasepool {
    NSPoint mouseLocation = [NSEvent mouseLocation];
    ppt->x = mouseLocation.x;
    ppt->y = mouseLocation.y;

    NSScreen *screen = nil;
    for (NSScreen *currentScreen in [NSScreen screens]) {
      if (NSPointInRect(mouseLocation, [currentScreen frame])) {
        screen = currentScreen;
        break;
      }
    }
    if(screen == nil)
        screen = [NSScreen mainScreen];
    NSRect rect = [screen frame];
    //convert to ns coordinate
    ppt->y = rect.size.height - ppt->y;
    float scale = [screen backingScaleFactor];
    ppt->x *= scale;
    ppt->y *= scale;
    return TRUE;
    }
}

int getNsDpi(bool bx) {
    @autoreleasepool {
    NSScreen *screen = [NSScreen mainScreen];
    float scale = [screen backingScaleFactor];
    return scale * 96;
    }
}

HWND findNsKeyWindow(){
    @autoreleasepool{
        NSArray<NSWindow *> *windows = [NSApp windows];
        for (NSWindow *window in windows) {
            if ([window canBecomeKeyWindow] && [window isVisible]) {
                NSView *view = window.contentView;
                if([view isKindOfClass: [SNsWindow class]])
                {
                    SNsWindow *win = (SNsWindow *)view;
                    return win->m_hWnd;
                }
            }
        }
    }
    return NULL;
}

HWND getHwndFromView(NSView *view){
    @autoreleasepool{
        if([view isKindOfClass: [SNsWindow class]])
        {
            SNsWindow *win = (SNsWindow *)view;
            return win->m_hWnd;
        }
    }
    return NULL;
}

static CGPathRef CGPathCreateFromNSBezierPath(NSBezierPath *bezierPath) {
    // 创建一个可变的 CGPath
    CGMutablePathRef cgPath = CGPathCreateMutable();
    
    // 获取路径的各个部分
    NSInteger elementCount = bezierPath.elementCount;
    
    for (NSInteger i = 0; i < elementCount; i++) {
        NSPoint points[3]; // 存储贝塞尔曲线的点
        NSBezierPathElement element = [bezierPath elementAtIndex:i associatedPoints:points];
        
        switch (element) {
            case NSMoveToBezierPathElement:
                CGPathMoveToPoint(cgPath, NULL, points[0].x, points[0].y);
                break;
            case NSLineToBezierPathElement:
                CGPathAddLineToPoint(cgPath, NULL, points[0].x, points[0].y);
                break;
            case NSCurveToBezierPathElement:
                CGPathAddCurveToPoint(cgPath, NULL, points[0].x, points[0].y, points[1].x, points[1].y, points[2].x, points[2].y);
                break;
            case NSClosePathBezierPathElement:
                CGPathCloseSubpath(cgPath);
                break;
            default:
                break;
        }
    }
    
    return cgPath; // 返回 CGPathRef
}

BOOL setNsWindowRgn(HWND hWnd, const RECT *prc, int nCount){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(!win)
            return FALSE;
        if(prc && nCount){
            NSBezierPath *path = [NSBezierPath bezierPath];
            for(int i = 0; i < nCount; i++){
                const RECT &rc = prc[i];
                [path appendBezierPathWithRect: NSMakeRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)];
            }

            CAShapeLayer *maskLayer = [CAShapeLayer layer];
            maskLayer.path = CGPathCreateFromNSBezierPath(path);  // 转换为CGPath
            win.wantsLayer = YES;
            win.layer.mask = maskLayer;
            win.layer.masksToBounds = YES;
        }else{
            win.layer.mask = nil;
            win.layer.masksToBounds = NO;
            win.wantsLayer = NO;
        }
        [win setNeedsDisplay: YES];
        [win displayIfNeeded];
        return TRUE;
    }
}

int  getNsWindowId(HWND hWnd){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win && win.window ){
            return win.window.windowNumber;
        }
        return 0;
    }
}

BOOL enableNsWindow(HWND hWnd, BOOL bEnable){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win ){
            [win setEnabled:bEnable];
            return TRUE;
        }
        return FALSE;
    }
}