#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/objc.h>
#include <objc/NSObjCRuntime.h>
#include <cairo-quartz.h>
#include <map>
#include <mutex>
#include <assert.h>
#include <windows.h>
#include "SNsWindow.h"
#include "SNsDataObjectProxy.h"
#include "wndobj.h"
#include "keyboard.h"
#include "sdragsourcehelper.h"
#include "tostring.hpp"
#include <uimsg.h>
#include <cursorid.h>
#include "log.h"

#undef interface    //interface is keyword usedd in macos sdk.
#define kLogTag "SNsWindow"

typedef BOOL (*FUNENUMDATOBOJECT)(WORD fmt, HGLOBAL hMem, NSPasteboardItem *param);
static BOOL EnumDataOjbect(IDataObject *pdo, FUNENUMDATOBOJECT fun, NSPasteboardItem *param){
    IEnumFORMATETC *enum_fmt;
    HRESULT hr = pdo->EnumFormatEtc(DATADIR_GET, &enum_fmt);
    if (FAILED(hr))
        return FALSE;
    FORMATETC fmt;
    while (enum_fmt->Next(1, &fmt, NULL) == S_OK)
    {
        STGMEDIUM medium;
        hr = pdo->GetData(&fmt, &medium);
        if (FAILED(hr))
            continue;
        if (medium.tymed != TYMED_HGLOBAL)
            continue;
        if (!fun(fmt.cfFormat, medium.hGlobal, param))
            break;
    }
    enum_fmt->Release();
    return TRUE;    
}

// 创建拖拽图像的辅助函数
static NSImage* CreateDragImageForDataObject(IDataObject *pDataObject,POINT *ptOffset) {
    // 默认图像大小
    SHDRAGIMAGE shdi;
    if(S_OK==SDragSourceHelper::GetDragImage(pDataObject, &shdi))
    {//using external drag image
        BITMAP bm;
        GetObject(shdi.hbmpDragImage, sizeof(BITMAP), &bm);
        if(bm.bmWidth>0 && bm.bmHeight>0 && bm.bmBitsPixel==32 && bm.bmBits){
            *ptOffset=shdi.ptOffset;
            return [[NSImage alloc] initWithData:[NSData dataWithBytes:bm.bmBits length:bm.bmWidthBytes*bm.bmHeight]];
        }
    }
    NSSize imageSize = NSMakeSize(48, 48);
    ptOffset->x = ptOffset->y = - imageSize.width/2;
    NSImage *dragImage = [[NSImage alloc] initWithSize:imageSize];

    // 检查是否有文本数据 - 优先检查Unicode文本
    FORMATETC fmtUnicodeText = {CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    FORMATETC fmtText = {CF_TEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium;
    BOOL hasText = FALSE;
    NSString *textContent = nil;

    // 首先尝试Unicode文本
    if (SUCCEEDED(pDataObject->GetData(&fmtUnicodeText, &medium))) {
        hasText = TRUE;
        const wchar_t * pwstr = (const wchar_t *)GlobalLock(medium.hGlobal);
        textContent = [[NSString alloc] initWithBytes:pwstr
                                               length:wcslen(pwstr) * sizeof(wchar_t)
                                             encoding:NSUTF32LittleEndianStringEncoding];
        if (!textContent) {
            // 如果UTF32失败，尝试UTF16
            textContent = [[NSString alloc] initWithBytes:pwstr
                                                   length:wcslen(pwstr) * sizeof(wchar_t)
                                                 encoding:NSUTF16LittleEndianStringEncoding];
        }
        GlobalUnlock(medium.hGlobal);
    }
    // 如果Unicode失败，尝试ANSI文本
    else if (SUCCEEDED(pDataObject->GetData(&fmtText, &medium))) {
        hasText = TRUE;
        const char * pstr = (const char *)GlobalLock(medium.hGlobal);
        textContent = [NSString stringWithUTF8String:pstr];
        if (!textContent) {
            // 如果UTF8失败，尝试Latin1
            textContent = [[NSString alloc] initWithBytes:pstr
                                                   length:strlen(pstr)
                                                 encoding:NSISOLatin1StringEncoding];
        }
        GlobalUnlock(medium.hGlobal);
    }

    if (hasText && textContent) {

        // 限制文本长度以避免图像过大
        if (textContent.length > 100) {
            textContent = [[textContent substringToIndex:97] stringByAppendingString:@"..."];
        }

        // 设置文本属性
        NSMutableParagraphStyle *paragraphStyle = [[NSMutableParagraphStyle alloc] init];
        [paragraphStyle setLineBreakMode:NSLineBreakByWordWrapping];
        [paragraphStyle setAlignment:NSTextAlignmentLeft];

        NSDictionary *textAttrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:11],
            NSForegroundColorAttributeName: [NSColor blackColor],
            NSParagraphStyleAttributeName: paragraphStyle
        };

        // 计算文本所需的尺寸，考虑换行
        CGFloat maxWidth = 180; // 最大宽度，留出边距
        CGFloat maxHeight = 80; // 最大高度，留出边距

        NSSize textSize = [textContent boundingRectWithSize:NSMakeSize(maxWidth, maxHeight)
                                                    options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                                                 attributes:textAttrs].size;

        // 添加边距和最小尺寸
        CGFloat padding = 10;
        CGFloat minWidth = 60;
        CGFloat minHeight = 30;

        imageSize.width = MAX(minWidth, MIN(200, textSize.width + padding * 2));
        imageSize.height = MAX(minHeight, MIN(100, textSize.height + padding * 2));

        // 重新创建适当大小的图像
        dragImage = [[NSImage alloc] initWithSize:imageSize];
        ptOffset->x = ptOffset->y = -imageSize.width/2;

        [dragImage lockFocus];

        // 绘制半透明背景
        [[NSColor colorWithCalibratedWhite:1.0 alpha:0.9] setFill];
        NSBezierPath *backgroundPath = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(0, 0, imageSize.width, imageSize.height)
                                                                       xRadius:4
                                                                       yRadius:4];
        [backgroundPath fill];

        // 绘制边框
        [[NSColor colorWithCalibratedWhite:0.7 alpha:1.0] setStroke];
        [backgroundPath setLineWidth:1.0];
        [backgroundPath stroke];

        // 绘制文本内容，支持多行显示
        NSRect textRect = NSMakeRect(padding, padding, imageSize.width - padding * 2, imageSize.height - padding * 2);

        // 创建NSAttributedString以支持更好的文本渲染
        NSAttributedString *attributedText = [[NSAttributedString alloc] initWithString:textContent attributes:textAttrs];
        [attributedText drawInRect:textRect];

        [dragImage unlockFocus];

        // 释放资源
        ReleaseStgMedium(&medium);
    } else {
        // 检查是否有文件数据
        FORMATETC fmtFile = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        BOOL hasFiles = SUCCEEDED(pDataObject->GetData(&fmtFile, &medium));

        if (hasFiles) {
            // 为文件创建一个文件夹图标
            [dragImage lockFocus];

            // 绘制文件夹图标
            [[NSColor colorWithCalibratedRed:0.95 green:0.95 blue:0.7 alpha:1.0] setFill];
            NSBezierPath *folderPath = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(4, 4, imageSize.width - 8, imageSize.height - 8)
                                                                       xRadius:4
                                                                       yRadius:4];
            [folderPath fill];

            // 绘制文件夹边框
            [[NSColor brownColor] setStroke];
            [folderPath setLineWidth:1.5];
            [folderPath stroke];

            // 绘制文件夹标签
            [[NSColor colorWithCalibratedRed:0.9 green:0.8 blue:0.5 alpha:1.0] setFill];
            NSBezierPath *tabPath = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(4, imageSize.height - 20, imageSize.width * 0.7, 12)
                                                                    xRadius:2
                                                                    yRadius:2];
            [tabPath fill];

            [dragImage unlockFocus];

            // 释放资源
            ReleaseStgMedium(&medium);
        } else {
            // 默认图像
            [dragImage lockFocus];
            [[NSColor colorWithCalibratedRed:0.8 green:0.8 blue:0.8 alpha:0.8] setFill];
            NSRectFill(NSMakeRect(0, 0, imageSize.width, imageSize.height));
            [[NSColor darkGrayColor] setStroke];
            NSBezierPath *path = [NSBezierPath bezierPathWithRect:NSInsetRect(NSMakeRect(0, 0, imageSize.width, imageSize.height), 2, 2)];
            [path setLineWidth:1.0];
            [path stroke];
            [dragImage unlockFocus];
        }
    }

    return dragImage;
}

static SHORT ConvertNSEventFlagsToWindowsFlags(NSEventModifierFlags nsFlags) {
    SHORT winFlags = 0;
    
    if (nsFlags & NSEventModifierFlagShift) {
        winFlags |= MK_SHIFT;
    }
    if (nsFlags & NSEventModifierFlagControl) {
        winFlags |= MK_CONTROL;
    }
    if (nsFlags & NSEventModifierFlagOption) {
        winFlags |= MK_ALT;  // macOS Option = Windows Alt
    }
    if (nsFlags & NSEventModifierFlagCommand) {
        winFlags |= MK_WINDOW;   // macOS Command = Windows Key
    }
    
    return winFlags;
}

// 将NSDragOperation转换为DROPEFFECT
static DWORD convertToDROPEFFECT(NSDragOperation operation) {
    DWORD effect = DROPEFFECT_NONE;

    if (operation & NSDragOperationCopy)
        effect = DROPEFFECT_COPY;
    else if (operation & NSDragOperationMove)
        effect = DROPEFFECT_MOVE;
    else if (operation & NSDragOperationLink)
        effect = DROPEFFECT_LINK;

    return effect;
}

// 将DROPEFFECT转换为NSDragOperation
static NSDragOperation convertToNSDragOperation(DWORD effect) {
    NSDragOperation operation = NSDragOperationNone;

    if (effect & DROPEFFECT_COPY)
        operation |= NSDragOperationCopy;
    if (effect & DROPEFFECT_MOVE)
        operation |= NSDragOperationMove;
    if (effect & DROPEFFECT_LINK)
        operation |= NSDragOperationLink;

    return operation;
}


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
- (BOOL)isImeEnabled;
- (void)setImeEnabled:(BOOL)bEnabled;
@end

@implementation SNsWindow{
    SConnBase *m_pListener;
    BYTE m_byAlpha;
    BOOL m_bMsgTransparent;
    NSEventModifierFlags m_modifierFlags;

    BOOL  m_bIsImeEnabled;
    NSString *_markedText;
    NSRange   _markedRange;
    NSRange   _selectedRange;
    NSRect    _inputRect;
    BOOL      _bEnabled;
    IDataObject *_doDragging;
    DWORD _dwDragEffect;
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
    m_modifierFlags = 0;
    _markedText = nil;
    _bEnabled = TRUE;
    _doDragging = nil;
    _dwDragEffect = DROPEFFECT_NONE;
    m_bIsImeEnabled = TRUE;
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
}

- (void)dealloc
{
    SLOG_STMI()<<"hjx SNsWindow dealloc, m_hWnd="<<m_hWnd;
}

- (void)updateRect:(NSRect)rc;{
    //todo:hjx
    [self invalidRect:rc];
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

#ifdef MOCOS_PATTERN_TEST
- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
    float scale = [self.window backingScaleFactor];
    cairo_surface_t *windowSurface = cairo_quartz_surface_create_for_cg_context(
        cgContext,
        self.bounds.size.width,
        self.bounds.size.height
    );
    double width = self.bounds.size.width*scale;
    double height = self.bounds.size.height*scale;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width/2, height/2);
    cairo_surface_set_device_scale(surface, 0.5,0.5);
    cairo_t *cr = cairo_create(surface);

    dirtyRect.origin.x *= scale;
    dirtyRect.origin.y *= scale;
    dirtyRect.size.width *= scale;
    dirtyRect.size.height *= scale;

    {
        RECT rc = {(LONG)dirtyRect.origin.x, (LONG)dirtyRect.origin.y, (LONG)(dirtyRect.origin.x+dirtyRect.size.width), (LONG)(dirtyRect.origin.y+dirtyRect.size.height)};
        m_pListener->OnDrawRect(m_hWnd, rc, cr);
    }
    cairo_t *windowCr = cairo_create(windowSurface);
    cairo_surface_set_device_scale(windowSurface, 1.0f/scale, 1.0f/scale);
    cairo_set_source_surface(windowCr, surface, 0, 0);
    cairo_paint(windowCr);
    cairo_destroy(windowCr);
    cairo_surface_destroy(windowSurface);
}
#else
- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
    float scale = [self.window backingScaleFactor];
    cairo_surface_t *windowSurface = cairo_quartz_surface_create_for_cg_context(
        cgContext,
        self.bounds.size.width,
        self.bounds.size.height
    );
    cairo_surface_set_device_scale(windowSurface, 1.0f/scale, 1.0f/scale);
    dirtyRect.origin.x *= scale;
    dirtyRect.origin.y *= scale;
    dirtyRect.size.width *= scale;
    dirtyRect.size.height *= scale;

    cairo_t *windowCr = cairo_create(windowSurface);
    {
        RECT rc = {(LONG)dirtyRect.origin.x, (LONG)dirtyRect.origin.y, (LONG)(dirtyRect.origin.x+dirtyRect.size.width), (LONG)(dirtyRect.origin.y+dirtyRect.size.height)};
        m_pListener->OnDrawRect(m_hWnd, rc, windowCr);
    }
    cairo_destroy(windowCr);
    cairo_surface_destroy(windowSurface);
}

#endif//MOCOS_PATTERN_TEST
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
    if ([theEvent clickCount] == 2) {
        [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDBLCLK];
    } else {
        [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDOWN];
    }
}

- (void) mouseUp: (NSEvent *) theEvent {
    [self onMouseEvent:theEvent withMsgId:WM_LBUTTONUP];
}

- (void)rightMouseDown:(NSEvent *)theEvent{
    if([theEvent clickCount] == 2)
        [self onMouseEvent:theEvent withMsgId:WM_RBUTTONDBLCLK];
    else
        [self onMouseEvent:theEvent withMsgId:WM_RBUTTONDOWN];
}

- (void)rightMouseUp:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_RBUTTONUP];
}

-(void)onXbuttonEvent:(NSEvent *)theEvent withMsgId:(int)msgId{
    int xbutton = 0;
    if(theEvent.buttonNumber == 3)
        xbutton = XBUTTON1;
    else if(theEvent.buttonNumber == 4)
        xbutton = XBUTTON2;
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
    m_pListener->OnNsEvent(m_hWnd,msgId,MAKEWPARAM(xbutton, uFlags),lParam);    
}

- (void)otherMouseDown:(NSEvent *)theEvent{

    if(theEvent.buttonNumber == 2){
        if([theEvent clickCount] == 2)
            [self onMouseEvent:theEvent withMsgId:WM_MBUTTONDBLCLK];
        else
            [self onMouseEvent:theEvent withMsgId:WM_MBUTTONDOWN];
    }else if(theEvent.buttonNumber<=4){
        if([theEvent clickCount] == 2)
            [self onXbuttonEvent:theEvent withMsgId:WM_XBUTTONDBLCLK];
        else
            [self onXbuttonEvent:theEvent withMsgId:WM_XBUTTONDOWN];
    }
}

- (void)otherMouseUp:(NSEvent *)theEvent{
    if(theEvent.buttonNumber == 2){
        [self onMouseEvent:theEvent withMsgId:WM_MBUTTONUP];
    }else if(theEvent.buttonNumber<=4){
        [self onXbuttonEvent:theEvent withMsgId:WM_XBUTTONUP];
    }
}

- (void)mouseEntered:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSEHOVER];
}

- (void)mouseExited:(NSEvent *)theEvent{
    [self onMouseEvent:theEvent withMsgId:WM_MOUSELEAVE];
}

- (SHORT) getKeyModifiers{
    return ConvertNSEventFlagsToWindowsFlags(m_modifierFlags);
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
    if(!m_bIsImeEnabled || [event modifierFlags] & NSEventModifierFlagCommand){
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
    if(!m_bIsImeEnabled || [event modifierFlags] & NSEventModifierFlagCommand){
        [self onKeyUp:event];
        return;
    }
    if (self.inputContext && [self.inputContext handleEvent:event]) {
        return;
    }
    [self onKeyUp:event];
}

-(void) setImeEnabled:(BOOL)bEnabled {
    m_bIsImeEnabled = bEnabled;
}

-(BOOL) isImeEnabled {
    return m_bIsImeEnabled;
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
    //SLOG_STMI()<<"hjx onStateChange:"<<nState<<" hWnd="<<m_hWnd;

    m_pListener->OnNsEvent(m_hWnd,  UM_STATE, nState,0);
}

// 当拖动进入视图时调用
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    assert(!_doDragging);
    NSPasteboard *pboard = [sender draggingPasteboard];
    _doDragging = new SNsDataObjectProxy(pboard);
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(!wndObj->dropTarget)
        return NSDragOperationNone;
    float scale = [self.window backingScaleFactor];
    NSPoint nspt = [sender draggingLocation];
    POINTL pt = {float2int(nspt.x*scale),float2int(nspt.y*scale)};
    _dwDragEffect = DROPEFFECT_NONE;
    NSEventModifierFlags modifierFlags = [NSEvent modifierFlags];
    DWORD modifier = ConvertNSEventFlagsToWindowsFlags(modifierFlags);
    wndObj->dropTarget->DragEnter(_doDragging, modifier, pt, &_dwDragEffect);
    return convertToNSDragOperation(_dwDragEffect);
}

// 当拖动在视图内移动时调用（可选）
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    float scale = [self.window backingScaleFactor];
    NSPoint nspt = [sender draggingLocation];
    nspt = [self.window convertPointToScreen:nspt];
    nspt.y = [self.window.screen frame].size.height - nspt.y;//convert to ns coordinate.
    POINTL pt = {float2int(nspt.x*scale),float2int(nspt.y*scale)};
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(wndObj->dropTarget){
        NSEventModifierFlags modifierFlags = [NSEvent modifierFlags];
        DWORD modifier = ConvertNSEventFlagsToWindowsFlags(modifierFlags);
        wndObj->dropTarget->DragOver(modifier, pt, &_dwDragEffect);
    }
    return convertToNSDragOperation(_dwDragEffect);
}

// 当拖动离开视图时调用
- (void)draggingExited:(nullable id<NSDraggingInfo>)sender {
    SLOG_STMI()<<"draggingExited";
    if(_doDragging){
        _doDragging->Release();
        _doDragging = nullptr;
    }
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(wndObj->dropTarget)
        return;
    wndObj->dropTarget->DragLeave();
}


// 当释放鼠标执行拖放操作时调用
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if(!_doDragging)
        return NO;
        SLOG_STMI()<<"performDragOperation";
    NSPasteboard *pboard = [sender draggingPasteboard];
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(!wndObj->dropTarget)
        return NO;
    NSEventModifierFlags modifierFlags = [NSEvent modifierFlags];
    DWORD modifier = ConvertNSEventFlagsToWindowsFlags(modifierFlags);
    float scale = [self.window backingScaleFactor];
    NSPoint nspt = [sender draggingLocation];
    nspt = [self.window convertPointToScreen:nspt];
    nspt.y = [self.window.screen frame].size.height - nspt.y;//convert to ns coordinate.
    POINTL pt = {float2int(nspt.x*scale),float2int(nspt.y*scale)};
    HRESULT hr =wndObj->dropTarget->Drop(_doDragging, modifier, pt, &_dwDragEffect);
    return hr==S_OK;
}

// 拖放操作完成后调用（可选）
- (void)concludeDragOperation:(nullable id<NSDraggingInfo>)sender {
    SLOG_STMI()<<"concludeDragOperation";
    [super concludeDragOperation:sender];
    if(_doDragging){
        _doDragging->Release();
        _doDragging = nullptr;
    }
}

#pragma mark - NSTextInputClient Protocol
-(void)onFunctionKey{
    NSEvent *currentEvent = [NSApp currentEvent];
    //SLOG_STMI()<<"hjx onFunctionKey: hWnd="<<m_hWnd;
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

-(BOOL)windowShouldClose:(NSWindow *)sender {
    SNsWindow *pWin = (SNsWindow *)self.contentView;
    if(!pWin)
        return YES;
    return SendMessageA(pWin->m_hWnd, WM_CLOSE, 0, 0)!=0;
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
        if(!view){
            [super sendEvent:event];
        }
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
        if(!view){
            [super sendEvent:event];
        }
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

-(BOOL)windowShouldClose:(NSWindow *)sender {
    SNsWindow *pWin = (SNsWindow *)self.contentView;
    if(!pWin)
        return YES;
    return SendMessageA(pWin->m_hWnd, WM_CLOSE, 0, 0)!=0;
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
        if(!view){
            [super sendEvent:event];
        }
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
        if(!view){
            [super sendEvent:event];
        }
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
                if(dwStyle & WS_DLGFRAME)
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
                        [pParent.window addChildWindow:host ordered:NSWindowAbove];
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
    //SLOG_STMI()<<"setNsWindowSize, hWnd="<<hWnd<<" cx="<<cx<<" cy="<<cy;
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
        float scale = [nswindow.window backingScaleFactor];
        rect.origin.x /= scale;
        rect.origin.y /= scale;
        rect.size.width /= scale;
        rect.size.height /= scale;
        [nswindow invalidRect:rect];
    }
}


void updateNsWindow(HWND hWnd, const RECT &rc){
    @autoreleasepool {
        SNsWindow * nswindow = getNsWindow(hWnd);
        if(!nswindow)
            return;
        NSRect rect = NSMakeRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
        float scale = [nswindow.window backingScaleFactor];
        rect.origin.x /= scale;
        rect.origin.y /= scale;
        rect.size.width /= scale;
        rect.size.height /= scale;
        [nswindow updateRect:rect];
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

BOOL flashNsWindow(HWND hWnd, DWORD dwFlags, UINT uCount, DWORD dwTimeout) {
    @autoreleasepool {
        SNsWindow * nsWindow = getNsWindow(hWnd);
        if (!nsWindow || !nsWindow.window) {
            return FALSE;
        }
        NSWindow * host = nsWindow.window;

        // 如果是停止闪烁
        if (dwFlags == FLASHW_STOP) {
            [NSApp cancelUserAttentionRequest:NSCriticalRequest];
            [NSApp cancelUserAttentionRequest:NSInformationalRequest];
            return TRUE;
        }
        // 如果窗口已经是活动窗口且不是强制闪烁，则不执行闪烁
        if ([host isKeyWindow] && !(dwFlags & FLASHW_TIMER)) {
            return TRUE;
        }

        // 处理不同的闪烁标志
        BOOL shouldContinue =(dwFlags & FLASHW_TIMER) || (dwFlags & FLASHW_TIMERNOFG);
        // Dock图标闪烁
        if (shouldContinue) {
          [NSApp requestUserAttention:NSCriticalRequest]; // 持续闪烁直到用户点击
        } else {
          [NSApp requestUserAttention:NSInformationalRequest]; // 闪烁一次
        }

        return TRUE;
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
        [nsWindow registerForDraggedTypes:@[(__bridge NSString *)kUTTypeItem]];
    }else{
        [nsWindow unregisterDraggedTypes];
    }
    return TRUE;
    }
}

BOOL EnumDataOjbectCb(WORD fmt, HGLOBAL hMem, NSPasteboardItem *item){
    @autoreleasepool { 
        if(fmt == CF_UNICODETEXT){
            if([[item types] containsObject:NSPasteboardTypeString])
                return TRUE;
            const wchar_t *src = (const wchar_t *)GlobalLock(hMem);
            std::string str;
            tostring(src, -1, str);
            GlobalUnlock(hMem);
            [item setString:[NSString stringWithUTF8String:str.c_str()] forType:NSPasteboardTypeString];
        }else if(fmt == CF_TEXT){
            if([[item types] containsObject:NSPasteboardTypeString])
                return TRUE;
            const char *src = (const char *)GlobalLock(hMem);
            [item setString:[NSString stringWithUTF8String:src] forType:NSPasteboardTypeString];
            GlobalUnlock(hMem);
        }else{
            const void *src = GlobalLock(hMem);
            size_t len = GlobalSize(hMem);
            NSData *data = [NSData dataWithBytes:src length:len];
            GlobalUnlock(hMem);
            NSString *type = SNsDataObjectProxy::getPasteboardType(fmt);
            [item setData:data forType:type];
        }
        return TRUE;
    }
}

// 拖拽源代理类，用于处理拖拽过程中的回调
@interface NSDragSourceProxy : NSObject <NSDraggingSource>
@property (nonatomic, assign) IDropSource *dropSource;
@property (nonatomic, assign) DWORD *pdwEffect;
@property (nonatomic, assign) DWORD dwOKEffect;
@property (nonatomic, assign) BOOL dragCompleted;
@property (nonatomic, assign) HRESULT result;
@end

@implementation NSDragSourceProxy
- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    NSDragOperation allowedOperations = NSDragOperationNone;

    if (self.dwOKEffect & DROPEFFECT_COPY)
        allowedOperations |= NSDragOperationCopy;
    if (self.dwOKEffect & DROPEFFECT_MOVE)
        allowedOperations |= NSDragOperationMove;
    if (self.dwOKEffect & DROPEFFECT_LINK)
        allowedOperations |= NSDragOperationLink;

    // 如果有IDropSource，询问它允许的操作
    if (self.dropSource) {
        DWORD effect = self.dwOKEffect;
        HRESULT hr = self.dropSource->GiveFeedback(effect);
        if (hr == S_OK) {
            // 使用IDropSource提供的效果
            return  convertToNSDragOperation(effect);
        }
    }

    return allowedOperations;
}

- (void)draggingSession:(NSDraggingSession *)session willBeginAtPoint:(NSPoint)screenPoint {

}

- (void)draggingSession:(NSDraggingSession *)session movedToPoint:(NSPoint)screenPoint {
     // 获取当前拖动操作
    NSDragOperation operation = [session draggingSequenceNumber];
    DWORD dwEffect = convertToDROPEFFECT(operation);
    if(self.pdwEffect){
        *self.pdwEffect = dwEffect;
    }
    if (self.dropSource) {
        HRESULT hr = self.dropSource->GiveFeedback(dwEffect);
        if(hr == DRAGDROP_S_USEDEFAULTCURSORS){
            LPCTSTR res = IDC_NODROP;
            if (dwEffect & DROPEFFECT_MOVE)
                res = IDC_MOVE;
            else if (dwEffect & DROPEFFECT_LINK)
                res = IDC_LINK;
            else if (dwEffect & DROPEFFECT_COPY)
                res = IDC_COPY;
            else
                res = IDC_NODROP;
            SetCursor(LoadCursor(0, res));
        }
    }
}

- (void)draggingSession:(NSDraggingSession *)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation {
    // 拖拽结束，设置结果
    self.dragCompleted = YES;

    // 转换操作类型
    DWORD effect =  convertToDROPEFFECT(operation);

    // 设置返回值
    if (self.pdwEffect) {
        *(self.pdwEffect) = effect;
    }

    // 设置结果代码
    if (operation == NSDragOperationNone) {
        self.result = DRAGDROP_S_CANCEL;
    } else {
        self.result = DRAGDROP_S_DROP;
    }
}

@end

HRESULT doNsDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,
                          DWORD *pdwEffect){
    @autoreleasepool {
        // 初始化返回值
        if (pdwEffect) *pdwEffect = DROPEFFECT_NONE;

        // 转换IDataObject到NSPasteboardItem
        NSPasteboardItem *item = [[NSPasteboardItem alloc] init];
        if (!EnumDataOjbect(pDataObject, EnumDataOjbectCb, item)) {
            return E_UNEXPECTED;
        }

        if([item types].count == 0) {
            return E_UNEXPECTED;
        }

        // 创建NSDraggingItem
        NSDraggingItem *draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:item];

        HWND hActive = GetActiveWindow();
        if(!hActive)
            return E_FAIL;
        SNsWindow * nswindow = getNsWindow(hActive);
        if(!nswindow)
            return E_FAIL;
        // 获取当前鼠标位置作为拖拽起始点
        NSPoint mouseLocation = [NSEvent mouseLocation];
        // 将鼠标位置转换为窗口坐标
        NSPoint windowPoint = [nswindow.window convertPointFromScreen:mouseLocation];
        POINT ptOffset;
        // 创建拖拽图像
        NSImage *dragImage = CreateDragImageForDataObject(pDataObject, &ptOffset);
        NSSize imageSize = dragImage.size;

        // 计算拖拽图像的正确位置，确保图像直接显示在光标位置
        NSRect imageFrame;
        imageFrame.origin.x = windowPoint.x + ptOffset.x;
        imageFrame.origin.y = windowPoint.y + ptOffset.y;
        imageFrame.size = imageSize;
        [draggingItem setDraggingFrame:imageFrame contents:dragImage];
        // 创建拖拽源代理
        NSDragSourceProxy *proxy = [[NSDragSourceProxy alloc] init];
        proxy.dropSource = pDropSource;
        proxy.pdwEffect = pdwEffect;
        proxy.dwOKEffect = dwOKEffect;
        proxy.dragCompleted = NO;
        proxy.result = DRAGDROP_S_CANCEL;
        NSTimeInterval currentTime = [NSDate timeIntervalSinceReferenceDate];
        NSEvent *startEvent = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                  location:windowPoint
                                             modifierFlags:0
                                                 timestamp:currentTime
                                              windowNumber:[nswindow.window windowNumber]
                                                   context:nil
                                               eventNumber:0
                                                clickCount:1
                                                  pressure:1.0];

        // 开始拖拽操作，使用我们的代理作为拖拽源
        NSDraggingSession *session = [nswindow beginDraggingSessionWithItems:@[draggingItem]
                                                                        event:startEvent
                                                                       source:proxy];

        if (!session)
            return E_FAIL;
        session.draggingFormation = NSDraggingFormationNone;
        session.animatesToStartingPositionsOnCancelOrFail = NO; // 取消时不动画回到起始位置

        MSG msg;
        while (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                break;
            if(proxy.dragCompleted)
                break;
        }
        return proxy.result;
    }
}

extern WORD GetCursorID(HICON hIcon);
extern POINT GetIconHotSpot(HICON hIcon);

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
        case CIDC_ICON:
            return [NSCursor arrowCursor];
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

void enableNsWindowIme(HWND hWnd, BOOL bEnable){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win ){
            [win setImeEnabled:bEnable];
        }
    }
}

BOOL isNsWindowEnableIme(HWND hWnd){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win ){
            return [win isImeEnabled];
        }
        return FALSE;
    }
}

void setNsWindowToolWindow(HWND hWnd, BOOL bToolWindow){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if([win isHidden])
            return;
        // 获取当前宿主窗口
        NSWindow *hostWin = [win window];
        if(hostWin){
            // 记录当前位置和大小
            NSRect frame = [win frame];
            // 将SNsWindow从宿主窗口移除
            [win removeFromSuperview];
            // 重新显示窗口，保持原来的位置和大小
            [win setFrame:frame];
            // 重新显示窗口
            showNsWindow(hWnd, SW_SHOWNORMAL);
        }
    }
}