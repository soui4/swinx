#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/objc.h>
#include <objc/NSObjCRuntime.h>

#include <map>
#include <set>
#include <mutex>
#include <assert.h>
#include <windows.h>
#include "SNsWindow.h"
#include "SNsDataObjectProxy.h"
#include "wndobj.h"
#include "keyboard.h"
#include "sdragsourcehelper.h"
#include "tostring.h"
#include <uimsg.h>
#include <cursorid.h>
#include "log.h"

#undef interface    //interface is keyword usedd in macos sdk.
#define kLogTag "SNsWindow"

// 多显示器坐标辅助函数（定义在文件后部 hwndFromPoint 附近），供
// scrollWheel 等位于 @implementation 早期的方法使用。实现见同文件后部，
// 声明由 SNsCoord.h 提供（无障碍桥接复用同一套换算）。
#include "SNsCoord.h"
// swinx MSAA (IAccessible) -> NSAccessibility 桥接入口
#include "SNsAccessibility.h"

static NSString *kDragTypeSwinxMark = @"com.swinx.internal.drag.marker";

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
        {
            ReleaseStgMedium(&medium);
            continue;
        }
        BOOL ret = fun(fmt.cfFormat, medium.hGlobal, param);
        ReleaseStgMedium(&medium);
        if (!ret)
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
    STGMEDIUM medium={0};
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
    ReleaseStgMedium(&medium);

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
        ReleaseStgMedium(&medium);
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

// ---- swinx 全局坐标 <-> Cocoa 全局坐标 翻转 ----
// swinx 全局坐标为物理像素（主屏左上为原点、y 向下）；Cocoa 全局坐标为
// point（主屏左下为原点、y 向上）。两者 x 相同，y 以主屏高度 H1（point）
// 为常数翻转：对任意显示器上的点均有 winY_px = (H1 - cocoaY) * k（k 为该
// 点所在屏的 scale，单位换算由调用方完成）。
// 因此这里必须以主屏高度翻转，不能用矩形所在屏的高度：副屏与主屏高度
// 不同（或上下排列）时，按所在屏高度翻转会产生 (H1 - hS)*k 像素的垂直
// 错位（单屏时两者相等，历史上未暴露）。screen 参数保留仅为兼容签名，
// 翻转不再依赖它。
static void ConvertNSRect(NSScreen *screen __attribute__((unused)), BOOL fullscreen __attribute__((unused)), NSRect *r)
{
    CGFloat h1 = swinxNsPrimaryHeight();
    r->origin.y = h1 - r->origin.y - r->size.height;
}

static void ConvertNSPoint(NSScreen *screen __attribute__((unused)), NSSize wndSize, NSPoint *pt)
{
    CGFloat h1 = swinxNsPrimaryHeight();
    pt->y = h1 - pt->y - wndSize.height;
}

static void RevertNSRect(NSScreen *screen __attribute__((unused)), BOOL fullscreen __attribute__((unused)), NSRect *r)
{
    CGFloat h1 = swinxNsPrimaryHeight();
    r->origin.y = h1 - r->origin.y - r->size.height;
}


class SNsWindowMgr{
public:
    SNsWindowMgr(){}
    ~SNsWindowMgr(){}

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

static SNsWindowMgr s_hWndMgr;

@class SNsWindow;

SNsWindow *getNsWindow(HWND hWnd){
    if(!s_hWndMgr.contains(hWnd))
        return nil;
    return (__bridge SNsWindow *)(void*)hWnd;
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
- (instancetype)initWithFrame:(NSRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent withDblClick:(BOOL)bAutoDblClick;
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
    BOOL m_bAutoDblClick;
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

- (instancetype)initWithFrame:(NSRect)frameRect withListener:(SConnBase*)listener withParent:(HWND)hParent withDblClick:(BOOL)bAutoDblClick{
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
    s_hWndMgr.add(m_hWnd);

    m_pListener = listener;
    m_bAutoDblClick = bAutoDblClick;
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
    //SLOG_STMI()<<"hjx destroy: hWnd="<<m_hWnd;
    s_hWndMgr.remove(m_hWnd);
    CFBridgingRelease((void*)m_hWnd);
}

- (void)dealloc
{
    //SLOG_STMI()<<"hjx SNsWindow dealloc, m_hWnd="<<m_hWnd;
}

- (void)updateRect:(NSRect)rc;{
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

- (void)drawRect:(NSRect)dirtyRect {
    CGContextRef cgContext = [[NSGraphicsContext currentContext] CGContext];
    if (!cgContext || !m_pListener) return;
    float scale = [self.window backingScaleFactor];
    CGContextSaveGState(cgContext);
    CGContextScaleCTM(cgContext, 1.0 / scale, 1.0 / scale);
    // 以物理像素构造裁剪矩形传入 OnDrawRect
    const NSRect physRect = NSMakeRect(dirtyRect.origin.x * scale,
                                       dirtyRect.origin.y * scale,
                                       dirtyRect.size.width * scale,
                                       dirtyRect.size.height * scale);
    RECT rc = {(LONG)physRect.origin.x, (LONG)physRect.origin.y,
               (LONG)(physRect.origin.x + physRect.size.width),
               (LONG)(physRect.origin.y + physRect.size.height)};
    m_pListener->OnDrawRect(m_hWnd, rc, cgContext);
    CGContextRestoreGState(cgContext);
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
    if ([theEvent clickCount] == 2 && m_bAutoDblClick) {
        [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDBLCLK];
    } else {
        [self onMouseEvent:theEvent withMsgId:WM_LBUTTONDOWN];
    }
}

- (void) mouseUp: (NSEvent *) theEvent {
    [self onMouseEvent:theEvent withMsgId:WM_LBUTTONUP];
}

- (void)rightMouseDown:(NSEvent *)theEvent{
    if([theEvent clickCount] == 2 && m_bAutoDblClick)
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
        if([theEvent clickCount] == 2 && m_bAutoDblClick)
            [self onMouseEvent:theEvent withMsgId:WM_MBUTTONDBLCLK];
        else
            [self onMouseEvent:theEvent withMsgId:WM_MBUTTONDOWN];
    }else if(theEvent.buttonNumber<=4){
        if([theEvent clickCount] == 2 && m_bAutoDblClick)
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
    // WM_MOUSEWHEEL 的 lParam 为屏幕坐标（Win32 约定，与 linux 后端一致）：
    // swinx 全局坐标 = 主屏左上为原点、y 向下、物理像素，与窗口/显示器矩形
    // 同一坐标系。旧实现按"鼠标所在屏"的高度翻转并乘 backingScaleFactor，
    // 多屏时高度取错；现统一以主屏高度 H1 为全局翻转基准。
    NSPoint location = [NSEvent mouseLocation];
    POINT ptWin;
    swinxNsWinPointFromCocoa(location, &ptWin);
    WPARAM wParam = MAKEWPARAM([self getKeyModifiers], (short)(deltaY * 120));
    LPARAM lParam = MAKELPARAM(ptWin.x, ptWin.y);
    // 发送到 Windows 窗口
    m_pListener->OnNsEvent(m_hWnd, WM_MOUSEWHEEL, wParam, lParam);
}

- (void)onKeyDown:(NSEvent *)event{ 
    NSUInteger keyCode = [event keyCode];     // 物理键码
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
    m_pListener->OnNsActive(m_hWnd, isActive);
}

- (void)onStateChange:(int) nState{
    m_pListener->OnNsEvent(m_hWnd,  UM_STATE, nState,0);
}

// 当拖动进入视图时调用
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    assert(!_doDragging);
    NSPasteboard *pboard = [sender draggingPasteboard];
    NSArray *types = [pboard types];
    _doDragging = new SNsDataObjectProxy(pboard);
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(!wndObj->dropTarget){
        return NSDragOperationNone;
    }
    float scale = [self.window backingScaleFactor];
    NSPoint nspt = [sender draggingLocation];
    POINTL pt = {float2int(nspt.x*scale),float2int(nspt.y*scale)};
    NSDragOperation allowedOps = sender.draggingSourceOperationMask;
    _dwDragEffect = convertToDROPEFFECT(allowedOps);
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
    NSDragOperation allowedOps = sender.draggingSourceOperationMask;
    DWORD dwOKEffect = convertToDROPEFFECT(allowedOps);
    _dwDragEffect = DROPEFFECT_NONE;
    if(wndObj->dropTarget){
        NSEventModifierFlags modifierFlags = [NSEvent modifierFlags];
        DWORD modifier = ConvertNSEventFlagsToWindowsFlags(modifierFlags);
        _dwDragEffect = dwOKEffect;
        wndObj->dropTarget->DragOver(modifier, pt, &_dwDragEffect);
    }else{
        LOG_STMW(kLogTag)<<"draggingUpdated: No drop target for window: "<<m_hWnd;
    }
    LPCTSTR idCursor = IDC_ARROW;
    if(_dwDragEffect & DROPEFFECT_MOVE)
        idCursor = IDC_MOVE;
    else if(_dwDragEffect & DROPEFFECT_LINK)
        idCursor = IDC_LINK;
    else if(_dwDragEffect & DROPEFFECT_COPY)
        idCursor = IDC_COPY;
    else
        idCursor = IDC_NO;
    SetCursor(LoadCursor(0, idCursor));
    
    return convertToNSDragOperation(_dwDragEffect);
}

// 当拖动离开视图时调用
- (void)draggingExited:(nullable id<NSDraggingInfo>)sender {
    if(_doDragging){
        _doDragging->Release();
        _doDragging = nullptr;
    }
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(wndObj->dropTarget){
        wndObj->dropTarget->DragLeave();
    }else{
        SLOG_STMW()<<"draggingExited: No drop target for window: "<<m_hWnd;
    }
}


// 当释放鼠标执行拖放操作时调用
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if(!_doDragging){
        SLOG_STMW()<<"performDragOperation: No dragging data object";
        return NO;
    }
    NSPasteboard *pboard = [sender draggingPasteboard];
    WndObj wndObj = WndMgr::fromHwnd(m_hWnd);
    if(!wndObj->dropTarget){
        SLOG_STMW()<<"performDragOperation: No drop target for window: "<<m_hWnd;
        return NO;
    }
    NSEventModifierFlags modifierFlags = [NSEvent modifierFlags];
    DWORD modifier = ConvertNSEventFlagsToWindowsFlags(modifierFlags);
    float scale = [self.window backingScaleFactor];
    NSPoint nspt = [sender draggingLocation];
    nspt = [self.window convertPointToScreen:nspt];
    nspt.y = [self.window.screen frame].size.height - nspt.y;//convert to ns coordinate.
    POINTL pt = {float2int(nspt.x*scale),float2int(nspt.y*scale)};
    HRESULT hr =wndObj->dropTarget->Drop(_doDragging, modifier, pt, &_dwDragEffect);
    SLOG_STMI()<<"performDragOperation result: hr="<<hr<<", effect="<<_dwDragEffect;
    return hr==S_OK;
}

// 拖放操作完成后调用（可选）
- (void)concludeDragOperation:(nullable id<NSDraggingInfo>)sender {
    [super concludeDragOperation:sender];
    if(_doDragging){
        _doDragging->Release();
        _doDragging = nullptr;
    }
}

#pragma mark - NSTextInputClient Protocol
-(void)onFunctionKey{
    NSEvent *currentEvent = [NSApp currentEvent];
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
    @try {
        if (!aString) {
            return;
        }
        
        NSEvent *currentEvent = [NSApp currentEvent];
        if([self hasMarkedText]){
            [self unmarkText];
            //send WM_IME_CHAR  message
            const char *str = NULL;
            /* Could be NSString or NSAttributedString, so we have
            * to test and convert it before return as SDL event */
            if ([aString isKindOfClass: [NSAttributedString class]]) {
                NSString *stringValue = [aString string];
                if (stringValue) {
                    str = [stringValue UTF8String];
                }
            } else if ([aString isKindOfClass: [NSString class]]) {
                str = [aString UTF8String];
            }
            
            if (str) {
                SLOG_STMI()<<"hjx insertText:"<<str<<" hWnd="<<m_hWnd;
                std::wstring wstr;
                towstring(str, -1, wstr);
                for(int i=0;i<wstr.length();i++){
                    m_pListener->OnNsEvent(m_hWnd, WM_IME_CHAR, wstr[i], 0);
                }
            }
        }else{
            if(currentEvent && currentEvent.type==NSEventTypeKeyDown){
                [self onKeyDown:currentEvent];
            }else if(currentEvent && currentEvent.type==NSEventTypeKeyUp){
                [self onKeyUp:currentEvent];
            }
        }
    } @catch (NSException *exception) {
        SLOG_STMI()<<"hjx insertText exception:"<<[exception description];
    }

}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    @try {
        if (!aString) {
            [self unmarkText];
            return;
        }
        
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
    } @catch (NSException *exception) {
        SLOG_STMI()<<"hjx setMarkedText exception:"<<[exception description];
        [self unmarkText];
    }
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

#pragma mark - NSAccessibility（swinx MSAA 桥接）

// 视图整体是一个"组"容器：真正的控件树由 swinx 侧的 IAccessible 提供，
// 见 SNsAccessibility.mm。这里只负责把根元素挂到视图下，元素按需惰性展开。
- (BOOL)isAccessibilityElement
{
    return YES;
}

- (NSString *)accessibilityRole
{
    return NSAccessibilityGroupRole;
}

- (NSString *)accessibilityRoleDescription
{
    return NSAccessibilityRoleDescription(NSAccessibilityGroupRole, nil);
}

- (NSArray *)accessibilityChildren
{
    return SwinxNsAccChildrenForHwnd(m_hWnd, self);
}

/* VoiceOver 鼠标跟随朗读：系统沿 app -> window -> view 链调用
 * accessibilityHitTest（point 为 Cocoa 屏幕坐标），默认实现只能定位到
 * 视图自身的元素。转交 swinx 的 IAccessible 树逐级下钻，返回鼠标下的
 * 最深层元素。 */
- (id)accessibilityHitTest:(NSPoint)point
{
    /* 关键：先把 self 挂为根壳的 AXParent。hit test 可能是本进程首次 AX 查询
     * （先于 -accessibilityChildren），若根壳 parent 为 nil，VoiceOver 拿到
     * 命中元素后沿 parent 上溯找不到 AppKit 锚点，会无限重试属性查询把主
     * 线程钉死（AX 查询洪水）。 */
    SwinxNsAccAttachRootParent(m_hWnd, self);
    id el = SwinxNsAccHitTest(m_hWnd, point);
    if (el)
        return el;
    return [super accessibilityHitTest:point];
}

@end

/* 窗口层 AX 命中转发（SNsWindowHost / SNsPanelHost 共用）：内容区内的查询
 * 显式交给 contentView（SNsWindow，其 -accessibilityHitTest: 下钻 swinx 的
 * IAccessible 树）。不依赖 AppKit 窗口层默认转发——自定义窗口/视图结构下
 * 默认转发可能被截断。point 为 Cocoa 屏幕坐标。返回 nil 时调用方走 super。 */
static id SwinxNsHostAccHitTest(NSWindow *window, NSPoint point)
{
    NSView *content = window.contentView;
    if ([content respondsToSelector:@selector(accessibilityHitTest:)])
    {
        NSRect contentScreen =
            [window convertRectToScreen:[content convertRect:content.bounds toView:nil]];
        if (NSPointInRect(point, contentScreen))
            return [content accessibilityHitTest:point];
    }
    return nil; /* 内容区外（标题栏/边框）或 contentView 不支持：走默认实现 */
}

// SNsWindowHost 实现
@implementation SNsWindowHost{
        id eventMonitor;
        BOOL m_bSizing;
        BOOL m_bInFsTransition; // 原生全屏过渡动画进行中，抑制窗口矩形回写
        BOOL m_bFsAnimPending;  // 自定义全屏动画未完成：didEnter/didExit 通知提前到达时不做最终同步
        NSRect m_fsRestoreFrame; // 进入原生全屏前的 frame，退出时恢复
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
    m_bInFsTransition = FALSE;
    m_bFsAnimPending = FALSE;
    m_fsRestoreFrame = NSZeroRect;
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
    else if(event.type != NSEventTypeAppKitDefined)
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

- (void)updateWindowPosition:(BOOL)bResize {
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
    SetWindowPos(root->m_hWnd,0,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,SWP_NOZORDER|SWP_NOACTIVATE|(bResize?0:SWP_NOSIZE));
    m_bSizing=FALSE;  
}

- (void)windowDidResize:(NSNotification *)notification{
    // 原生全屏过渡动画期间 AppKit 逐帧改 frame，此时回写 SetWindowPos 会经
    // setNsWindowPos/setNsWindowSize 反向改窗口（setFrameOrigin 不受 m_bSizing
    // 保护），与动画互相干扰导致最终矩形丢失，故过渡结束后统一同步。
    if(m_bSizing || m_bInFsTransition)
        return;
    [self updateWindowPosition: YES];
    SNsWindow * root = self.contentView;
    BOOL bZoomed = [self isZoomed];
    if(bZoomed){
        [root onStateChange:SIZE_MAXIMIZED];
    }else {
        [root onStateChange:SIZE_RESTORED];
    }
}

- (void)windowDidMove:(NSNotification *)notification {
    if(m_bSizing || m_bInFsTransition)
        return;
    [self updateWindowPosition: NO];
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

/* VoiceOver 鼠标跟随的命中查询到达窗口层：显式转发给内容视图 */
- (id)accessibilityHitTest:(NSPoint)point
{
    id el = SwinxNsHostAccHitTest(self, point);
    if (el)
        return el;
    return [super accessibilityHitTest:point];
}
- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) windowWillEnterFullScreen:(NSNotification *)notification {
    m_bInFsTransition = TRUE;
}

- (void) windowDidEnterFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidEnterFullScreen,hWnd="<<root->m_hWnd;
    if(m_bFsAnimPending)
        return; // 自定义进入动画未结束：完成后在 completionHandler 中统一同步
    m_bInFsTransition = FALSE;
    // 过渡结束、frame 已定型：borderless 窗口有时不会被 AppKit 自动放大到
    // 满屏，这里显式铺满所在屏，再强制把最终矩形同步给 SOUI（WM_SIZE）。
    @autoreleasepool {
        NSScreen *screen = [self screen] ?: [NSScreen mainScreen];
        NSRect sf = [screen frame];
        if (fabs(sf.size.width - [self frame].size.width) > 0.5 ||
            fabs(sf.size.height - [self frame].size.height) > 0.5) {
            [self setFrame:sf display:YES];
        }
        [self updateWindowPosition:YES];
    }
}

- (void) windowWillExitFullScreen:(NSNotification *)notification {
    m_bInFsTransition = TRUE;
}

- (void) windowDidExitFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidExitFullScreen,hWnd="<<root->m_hWnd;
    if(m_bFsAnimPending)
        return; // 自定义退出动画未结束：完成后在 completionHandler 中统一同步
    m_bInFsTransition = FALSE;
    // AppKit 已恢复全屏前的 frame，强制同步最终矩形给 SOUI
    [self updateWindowPosition:YES];
}

#pragma mark - 原生全屏自定义动画（NSWindowDelegate）
// borderless 窗口走系统默认全屏过渡时是两段式（先进 Space 再放大），且退出
// 恢复的 frame 不可靠。按 Apple FullScreenWindow 示例的做法，通过
// customWindowsToEnter/ExitFullScreenForWindow: 接管窗口自身的动画：
// 进入时记录原始 frame 并一段动画铺满屏幕（与系统 Space 切换同步），
// 退出时一段动画还原到记录的 frame。

- (NSArray<NSWindow *> *)customWindowsToEnterFullScreenForWindow:(NSWindow *)window {
    return @[self];
}

- (void)window:(NSWindow *)window startCustomAnimationToEnterFullScreenWithDuration:(NSTimeInterval)duration {
    m_fsRestoreFrame = [self frame];
    m_bFsAnimPending = TRUE;
    NSScreen *screen = [self screen] ?: [NSScreen mainScreen];
    NSRect target = [screen frame];
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = duration;
        context.allowsImplicitAnimation = YES;
        [[self animator] setFrame:target display:YES];
    } completionHandler:^{
        // 动画真正结束后才解除抑制并同步最终矩形。didEnter/didExit 通知可能
        // 早于动画完成到达，若在中间帧回写 setFrameOrigin 会与 animator 剩余
        // 动画抢窗口，导致窗口闪跳一帧。
        dispatch_async(dispatch_get_main_queue(), ^{
            m_bFsAnimPending = FALSE;
            m_bInFsTransition = FALSE;
            if (!m_bSizing)
                [self updateWindowPosition:YES];
        });
    }];
}

- (NSArray<NSWindow *> *)customWindowsToExitFullScreenForWindow:(NSWindow *)window {
    return @[self];
}

- (void)window:(NSWindow *)window startCustomAnimationToExitFullScreenWithDuration:(NSTimeInterval)duration {
    NSRect restore = m_fsRestoreFrame;
    if (NSIsEmptyRect(restore))
        restore = [self frame]; // 兜底：没有记录时保持系统恢复的结果
    m_bFsAnimPending = TRUE;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = duration;
        context.allowsImplicitAnimation = YES;
        [[self animator] setFrame:restore display:YES];
    } completionHandler:^{
        dispatch_async(dispatch_get_main_queue(), ^{
            m_bFsAnimPending = FALSE;
            m_bInFsTransition = FALSE;
            if (!m_bSizing)
                [self updateWindowPosition:YES];
        });
    }];
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
        BOOL m_bInFsTransition; // 原生全屏过渡动画进行中，抑制窗口矩形回写
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
    m_bInFsTransition = FALSE;
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
    else if(event.type != NSEventTypeAppKitDefined)
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
    // 原生全屏过渡动画期间抑制回写（与 SNsWindowHost 同理），结束后在
    // windowDidEnter/ExitFullScreen 中统一同步。
    if(m_bSizing || m_bInFsTransition)
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
    if(m_bSizing || m_bInFsTransition)
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

/* VoiceOver 鼠标跟随的命中查询到达窗口层：显式转发给内容视图 */
- (id)accessibilityHitTest:(NSPoint)point
{
    id el = SwinxNsHostAccHitTest(self, point);
    if (el)
        return el;
    return [super accessibilityHitTest:point];
}
- (BOOL)acceptsFirstResponder {
    return YES;
}

- (void) windowWillEnterFullScreen:(NSNotification *)notification {
    m_bInFsTransition = TRUE;
}

- (void) windowDidEnterFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidEnterFullScreen,hWnd="<<root->m_hWnd;
    m_bInFsTransition = FALSE;
    if(m_bSizing)
        return;
    NSScreen *screen = [self screen] ?: [NSScreen mainScreen];
    NSRect sf = [screen frame];
    if (fabs(sf.size.width - [self frame].size.width) > 0.5 ||
        fabs(sf.size.height - [self frame].size.height) > 0.5) {
        [self setFrame:sf display:YES];
    }
    [self windowDidResize:nil]; // 复用内联同步逻辑把最终矩形同步给 SOUI
}

- (void) windowWillExitFullScreen:(NSNotification *)notification {
    m_bInFsTransition = TRUE;
}

- (void) windowDidExitFullScreen:(NSNotification *)notification {
    SNsWindow * root = self.contentView;
    SLOG_STMI()<<"windowDidExitFullScreen,hWnd="<<root->m_hWnd;
    m_bInFsTransition = FALSE;
    if(m_bSizing)
        return;
    [self windowDidResize:nil];
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
                NSScreen *ret = [nswindow.window screen];
                if(ret)
                    return ret;
            }
        }
        return [NSScreen mainScreen];
    }
}

HWND createNsWindow(HWND hParent, DWORD dwStyle,DWORD dwExStyle, BOOL bAutoDblClick, LPCSTR pszTitle, int x,int y,int cx,int cy, SConnBase *pListener)
{
    @autoreleasepool {
	NSRect rect = NSMakeRect(x, y, cx, cy);
    if(!(dwStyle&WS_CHILD))
        hParent=0;
    SNsWindow * nswindow = [[SNsWindow alloc] initWithFrame:rect withListener:pListener withParent:hParent withDblClick:bAutoDblClick];
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

// Create the backing NSWindow for a root SNsWindow view if it doesn't exist yet.
// This mirrors the window-creation branch of showNsWindow(); it is used by
// getAppleHostWindow() so the NSWindow is available before ShowWindow() is called
// (e.g. for SDL3 external-window integration, which needs the NSWindow pointer
// during OnHostCreate while the SWINX window is not yet shown). It intentionally
// does NOT order the window to front / make it key — that is left to showNsWindow().
static void createNsHostWindow(SNsWindow *nswindow, HWND hWnd){
    if(!nswindow || nswindow.window != nil)
        return;
    if(!IsRootView(nswindow))
        return;
    DWORD dwStyle = GetWindowLongPtrA(hWnd,GWL_STYLE);
    DWORD dwExStyle = GetWindowLongPtrA(hWnd,GWL_EXSTYLE);
    NSWindowStyleMask styleMask = 0;
    if(dwStyle & WS_DLGFRAME)
    {
        styleMask |= NSWindowStyleMaskTitled;
        if(dwStyle & WS_SYSMENU)//关闭按钮只在有标题栏时才有意义
            styleMask |= NSWindowStyleMaskClosable;
        if(dwStyle & WS_THICKFRAME)//keep resize only for window with caption
            styleMask |= NSWindowStyleMaskResizable;
    }
    if(dwStyle & WS_MAXIMIZEBOX)
        styleMask |= NSWindowStyleMaskMiniaturizable;
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
}

// Returns the NSWindow* (as void*) that hosts the given SOUI HWND on macOS.

extern "C" void* getAppleHostWindow(HWND hWnd){
@autoreleasepool {
	if(!IsWindow(hWnd))
		return nullptr;
    if(GetWindowLongPtr(hWnd,GWL_STYLE) & WS_CHILD)
		hWnd = GetAncestor(hWnd, GA_ROOT);
    SNsWindow *root = getNsWindow(hWnd);
    NSWindow *win = root.window;
    if(!win)
        return nullptr;
    return (__bridge void*)win;
}
}

// 取给定 SOUI 窗口自己的宿主 NSWindow（不向 GA_ROOT 解析）。
// 供 SConnection 的 MonitorFromWindow 等使用：SNsWindow 类定义在本文件内，
// 外部 ObjC++ 文件无法直接访问其 window 属性。
NSWindow *getNsHostWindow(HWND hWnd){
    @autoreleasepool {
        SNsWindow *nsw = getNsWindow(hWnd);
        return nsw ? nsw.window : nil;
    }
}

BOOL showNsWindow(HWND hWnd,int nCmdShow){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    BOOL bRoot = IsRootView(nswindow);
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
            DWORD dwExStyle = GetWindowLongPtrA(hWnd,GWL_EXSTYLE);
            if(nswindow.window == nil){
                // Reuse the shared helper so the window-creation logic lives in one place.
                createNsHostWindow(nswindow, hWnd);
            }
            else if(dwExStyle & WS_EX_TRANSPARENT){
                [nswindow setMsgTransparent:TRUE];
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
            // 用受 m_bSizing 保护的 setFrame: 携带完整矩形（origin+size）。
            // 不能用 setFrameOrigin：宿主发起的同步(updateWindowPosition)中
            // m_rcPos 的 size 尚未更新（仍是全屏旧高度），ConvertNSRect 翻转
            // 出错误的 y 会把窗口推到屏幕左下角，产生一帧闪现；
            // setFrame:display: 在 m_bSizing=TRUE（宿主回写）时被覆写跳过，
            // SOUI 主动调用（m_bSizing=FALSE）时正常执行。
            [nswindow.window setFrame:rect display:YES];
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
        // 丢弃该窗口的无障碍元素树缓存（否则下次查询会用到已释放的 IAccessible）
        SwinxNsAccInvalidate(hWnd);
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
        if(int nChilds = [nsParent.subviews count] > 0) {
            for(int i=0;i<nChilds;i++){
                NSView *child = [nsParent.subviews objectAtIndex:0];
                if([child isKindOfClass:[SNsWindow class]])
                {
                    SNsWindow* p = (SNsWindow*)child;
                    hRet = p->m_hWnd;
                    break;
                }
            }
        }
        break;
    case GW_CHILDLAST:
        if(int nChilds = [nsParent.subviews count] > 0) {
            for(int i=nChilds-1;i>=0;i--){
                NSView *child = [nsParent.subviews objectAtIndex:0];
                if([child isKindOfClass:[SNsWindow class]])
                {
                    SNsWindow* p = (SNsWindow*)child;
                    hRet = p->m_hWnd;
                    break;
                }
            }
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
                // Walk backward over siblings, skipping any NSView that is not an
                // SNsWindow (e.g. views created by business code via native Cocoa
                // APIs) so they are never mistaken for SOUI windows.
                for(NSInteger i = (NSInteger)index - 1; i >= 0; i--) {
                    NSView *sib = [siblings objectAtIndex:i];
                    if([sib isKindOfClass:[SNsWindow class]]) {
                        SNsWindow *prev = (SNsWindow*)sib;
                        hRet = prev->m_hWnd;
                        break;
                    }
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
                // Walk forward over siblings, skipping any NSView that is not an
                // SNsWindow (same reason as GW_HWNDPREV).
                for(NSUInteger i = index + 1; i < [siblings count]; i++) {
                    NSView *sib = [siblings objectAtIndex:i];
                    if([sib isKindOfClass:[SNsWindow class]]) {
                        SNsWindow *next = (SNsWindow*)sib;
                        hRet = next->m_hWnd;
                        break;
                    }
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
    [NSApp activateIgnoringOtherApps:YES];
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

// 主屏高度 H1（Cocoa 全局坐标原点 (0,0) 所在屏）：swinx 全局坐标（主屏左上
// 为原点、y 向下）与 Cocoa 全局坐标（主屏左下为原点、y 向上）之间的常数翻转：
// cocoaY = H1 - winY（point 单位）。
CGFloat swinxNsPrimaryHeight() {
    NSArray<NSScreen *> *screens = [NSScreen screens];
    for (NSScreen *s in screens) {
        NSRect f = [s frame];
        if (f.origin.x == 0 && f.origin.y == 0)
            return f.size.height;
    }
    return [screens count] ? [[screens objectAtIndex:0] frame].size.height : 0;
}

// 找包含 Cocoa 全局点的屏；找不到（如菜单栏/Dock 边缘）回退 mainScreen
NSScreen *swinxNsScreenForCocoaPoint(NSPoint gp) {
    for (NSScreen *s in [NSScreen screens]) {
        if (NSPointInRect(gp, [s frame]))
            return s;
    }
    return [NSScreen mainScreen];
}

// Cocoa 全局点（point）-> swinx 全局点（物理像素）：用包含该点的屏的
// backingScaleFactor 乘上并按主屏高度翻转，与窗口矩形（像素）同一坐标系。
void swinxNsWinPointFromCocoa(NSPoint gp, POINT *ppt) {
    NSScreen *screen = swinxNsScreenForCocoaPoint(gp);
    CGFloat k = screen ? [screen backingScaleFactor] : 1.f;
    CGFloat h1 = swinxNsPrimaryHeight();
    ppt->x = (int)(gp.x * k);
    ppt->y = (int)((h1 - gp.y) * k);
}

// swinx 全局点（物理像素）-> Cocoa 全局点（point）：先按各屏的 swinx 像素
// 矩形找到包含该点的屏，再用其 scale 反推；找不到时用 screens[0] 的 scale。
NSPoint swinxNsCocoaPointFromWin(POINT pt) {
    NSArray<NSScreen *> *screens = [NSScreen screens];
    CGFloat h1 = swinxNsPrimaryHeight();
    for (NSScreen *s in screens) {
        NSRect f = [s frame];
        CGFloat k = [s backingScaleFactor];
        double left = f.origin.x * k;
        double right = (f.origin.x + f.size.width) * k;
        double top = (h1 - (f.origin.y + f.size.height)) * k;
        double bottom = (h1 - f.origin.y) * k;
        if (pt.x >= left && pt.x < right && pt.y >= top && pt.y < bottom)
            return NSMakePoint(pt.x / k, h1 - pt.y / k);
    }
    CGFloat k = [screens count] ? [[screens objectAtIndex:0] backingScaleFactor] : 1.f;
    return NSMakePoint(pt.x / k, h1 - pt.y / k);
}

static NSWindow *windowAtPoint(NSPoint cocoaGlobalPt) {
    @autoreleasepool {
    // windowNumberAtPoint 接受 Cocoa 全局屏幕坐标（y 向上），无需再按屏翻转；
    // 多显示器下同一全局坐标系覆盖所有屏幕。非本进程窗口返回 nil。
    NSInteger windowNumber = [NSWindow windowNumberAtPoint:cocoaGlobalPt
                               belowWindowWithWindowNumber:0];
    return [NSApp windowWithWindowNumber:windowNumber];
    }
}

HWND hwndFromPoint(HWND hWnd,POINT pt){
    @autoreleasepool{
    // pt 为 swinx 全局坐标（主屏左上为原点、y 向下、物理像素），先换算成
    // Cocoa 全局坐标（point）再查询该点最上层的窗口。
    NSPoint cocoaPt = swinxNsCocoaPointFromWin(pt);
    NSWindow *host = nil;
    if (hWnd) {
        SNsWindow *hint = getNsWindow(hWnd);
        host = hint ? hint.window : windowAtPoint(cocoaPt);
    } else {
        host = windowAtPoint(cocoaPt);
    }
    if (!host)
        return 0;
    SNsWindow *nswindow = (SNsWindow *)host.contentView;
    if (![nswindow isKindOfClass:[SNsWindow class]])
        return 0;
    // -[NSView hitTest:] 的入点位于接收者的 superview（即窗口基坐标系）下，
    // 需先将 Cocoa 全局点转换到窗口坐标系，不能用 swinx 全局点直接判定。
    NSPoint ptInHost = [host convertPointFromScreen:cocoaPt];
    NSView *view = [nswindow hitTest:ptInHost];
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

BOOL setNsWindowToTop(HWND hWnd){
    @autoreleasepool {
    SNsWindow * nswindow = getNsWindow(hWnd);
    if(!nswindow)
        return FALSE;
    if(![nswindow.window isVisible]){
        return FALSE;
    }
    [nswindow.window orderFront:nil];
    [nswindow.window makeKeyAndOrderFront:nil];
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
            // 处于原生全屏(Space)时先退出全屏
            if([nswindow.window styleMask] & NSWindowStyleMaskFullScreen){
                [nswindow.window toggleFullScreen:nil];
                return TRUE;
            }
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
        if(nCmd == SC_FULLSCREEN){
            // macOS 原生全屏：窗口独占一个新桌面（Space）。toggleFullScreen:
            // 为切换语义，再次发送 SC_FULLSCREEN 即退出全屏。
            // 全屏作用于根窗口的宿主 NSWindow；子窗口先解析到根。
            HWND hRoot = GetAncestor(hWnd, GA_ROOT);
            SNsWindow * rootView = getNsWindow(hRoot);
            NSWindow * host = (rootView && rootView.window) ? rootView.window : nswindow.window;
            // SOUI 窗口多为 borderless(WS_POPUP)，默认集合行为下全屏会退化为
            // "当前 Space 内铺满"(FullScreenAuxiliary)。必须显式声明为
            // FullScreenPrimary，窗口才会进入独立桌面的原生全屏。
            NSWindowCollectionBehavior behavior = [host collectionBehavior];
            behavior |= NSWindowCollectionBehaviorFullScreenPrimary;
            behavior &= ~NSWindowCollectionBehaviorFullScreenAuxiliary;
            [host setCollectionBehavior:behavior];
            [host toggleFullScreen:nil];
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
        // macOS drag registration doesn't support wildcards, so we use a comprehensive approach:
        // Register internal swinx drag marker - ensures we receive drags from swinx sources
        NSArray *draggedTypes = @[
            // Swinx internal drag marker - this is the key to guaranteed delivery
            kDragTypeSwinxMark,
            
            // Broadest types - these will catch custom UTIs that conform to standard hierarchy
            @"public.item",              // Root of all items (broader than public.data)
            @"public.data",              // All data types
            
            // Content hierarchy - catches content-based drags
            (__bridge NSString *)kUTTypeContent,
            (__bridge NSString *)kUTTypeCompositeContent,
            
            // Common standard types (for performance optimization)
            NSPasteboardTypeString,
            NSPasteboardTypeFileURL,
            NSPasteboardTypeURL,
            NSPasteboardTypePNG,
            NSPasteboardTypeTIFF,
            NSPasteboardTypeRTF,
            NSPasteboardTypeHTML,
        ];
        
        [nsWindow registerForDraggedTypes:draggedTypes];
    }else{
        [nsWindow unregisterDraggedTypes];
    }
    return TRUE;
    }
}

static BOOL EnumDataOjbectCb(WORD fmt, HGLOBAL hMem, NSPasteboardItem *item){
    @autoreleasepool{ 
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
            GlobalUnlock(hMem);
            
            // Validate data before proceeding
            if(!src || len == 0){
                LOG_FMT(kLogTag, SLOG_WARN, "Invalid data for format %u", (unsigned int)fmt);
                return FALSE;
            }
            
            NSData *data = [NSData dataWithBytes:src length:len];
            NSString *type = SNsDataObjectProxy::getPasteboardType(fmt);
            
            // Validate pasteboard type
            if(!type || [type length] == 0){
                LOG_FMT(kLogTag, SLOG_WARN, "Invalid pasteboard type for format %u", (unsigned int)fmt);
                return FALSE;
            }
            
            // Check if type already exists
            if([[item types] containsObject:type])
                return TRUE;
            
            BOOL ret = [item setData:data forType:type];
            if(!ret){
                LOG_FMT(kLogTag, SLOG_WARN, "Failed to set data for type: %@ (format: %u)", type, (unsigned int)fmt);
                return FALSE;
            }
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

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession *)session {
    return NO; // 不忽略修饰键，这样可以根据修饰键改变拖拽操作
}

// 重要：添加这个方法来确保拖拽源能接收到所有拖拽事件
- (NSDragOperation)draggingSession:(NSDraggingSession *)session
                  sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    NSDragOperation allowedOperations = NSDragOperationNone;
    if (self.dwOKEffect & DROPEFFECT_COPY)
        allowedOperations |= NSDragOperationCopy;
    if (self.dwOKEffect & DROPEFFECT_MOVE)
        allowedOperations |= NSDragOperationMove;
    if (self.dwOKEffect & DROPEFFECT_LINK)
        allowedOperations |= NSDragOperationLink;
    return allowedOperations;
}

- (void)draggingSession:(NSDraggingSession *)session willBeginAtPoint:(NSPoint)screenPoint {
    if (self.dropSource) {
        self.dropSource->GiveFeedback(DROPEFFECT_NONE);
    }
}

// 添加这个方法来处理拖拽会话的移动
- (void)draggingSession:(NSDraggingSession *)session movedToPoint:(NSPoint)screenPoint {
    if (self.dropSource) {
        self.dropSource->QueryContinueDrag(FALSE, DROPEFFECT_COPY);
    }
}

- (void)draggingSession:(NSDraggingSession *)session endedAtPoint:(NSPoint)screenPoint operation:(NSDragOperation)operation {
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

        // CRITICAL: Add internal swinx drag marker to ensure target windows can receive this drag
        // This marker type is registered by all swinx drop targets, guaranteeing drag event delivery
        NSData *markerData = [NSData dataWithBytes:"SWINX_DRAG" length:10];
        [item setData:markerData forType: kDragTypeSwinxMark];

        // 创建NSDraggingItem
        NSDraggingItem *draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:item];

        // 获取当前鼠标位置
        NSPoint mouseLocation = [NSEvent mouseLocation];
        
        // 获取鼠标所在的屏幕
        NSScreen *screen = nil;
        for (NSScreen *currentScreen in [NSScreen screens]) {
            if (NSPointInRect(mouseLocation, [currentScreen frame])) {
                screen = currentScreen;
                break;
            }
        }
        if (screen == nil) {
            screen = [NSScreen mainScreen];
        }
        
        // 创建一个临时的透明窗口来启动拖动会话
        NSRect windowFrame = NSMakeRect(mouseLocation.x, mouseLocation.y, 100, 100);
        NSWindow *temporaryWindow = [[NSWindow alloc] initWithContentRect:windowFrame
                                                                 styleMask:NSWindowStyleMaskBorderless
                                                                   backing: NSBackingStoreBuffered
                                                                     defer:NO
                                                                    screen:screen];
        [temporaryWindow setOpaque:NO];
        [temporaryWindow setBackgroundColor:[NSColor clearColor]];
        [temporaryWindow setIgnoresMouseEvents:YES];
        [temporaryWindow setLevel:NSScreenSaverWindowLevel];
        
        // 创建临时的透明NSView作为内容视图
        NSView *temporaryView = [[NSView alloc] initWithFrame:temporaryWindow.contentView.bounds];
        [temporaryView setWantsLayer:YES];
        [temporaryView.layer setOpacity:0.0];
        [temporaryWindow setContentView:temporaryView];
        
        // 获取窗口坐标
        NSPoint windowPoint = [temporaryWindow convertPointFromScreen:mouseLocation];
        
        NSImage *dragImage = nil;
        NSRect imageFrame;
        
        if (!pDropSource) {
            // 创建真实的拖动图像
            POINT ptOffset;
            dragImage = CreateDragImageForDataObject(pDataObject, &ptOffset);
            NSSize imageSize = dragImage.size;

            // 计算拖拽图像的正确位置，确保图像直接显示在光标位置
            imageFrame.origin.x = windowPoint.x + ptOffset.x;
            imageFrame.origin.y = windowPoint.y + ptOffset.y;
            imageFrame.size = imageSize;
        } else {
            // 创建空的拖动图像
            dragImage = [[NSImage alloc] initWithSize:NSMakeSize(1, 1)];
            imageFrame = NSMakeRect(windowPoint.x, windowPoint.y, 1, 1);
        }
        
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
                                              windowNumber:[temporaryWindow windowNumber]
                                                   context:nil
                                               eventNumber:0
                                                clickCount:1
                                                  pressure:1.0];

        // 开始拖拽操作，使用临时视图作为拖拽源
        SLOG_STMI()<<"Starting drag session from temporary view at point: ("<<windowPoint.x<<","<<windowPoint.y<<")";
        NSDraggingSession *session = [temporaryView beginDraggingSessionWithItems:@[draggingItem]
                                                                             event:startEvent
                                                                            source:proxy];

        if (!session) {
            SLOG_STME()<<"Failed to create dragging session";
            temporaryWindow = nil;
            return E_FAIL;
        }
        SLOG_STMI()<<"Drag session created successfully";
        session.draggingFormation = NSDraggingFormationNone;
        session.animatesToStartingPositionsOnCancelOrFail = NO; // 取消时不动画回到起始位置

        // 运行事件循环直到拖动完成
        while (!proxy.dragCompleted && [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode 
                                             beforeDate:[NSDate distantFuture]]) {
        }
        SLOG_STMI()<<"Drag session done, result="<<proxy.result;
        
        temporaryWindow = nil;
        return proxy.result;
    }
}

extern WORD GetCursorID(HICON hIcon);
extern POINT GetIconHotSpot(HICON hIcon);

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
    // swinx 全局坐标 = 主屏左上为原点、y 向下（Win32 约定），单位为物理像素，
    // 与窗口矩形/显示器矩形同一坐标系（SOUI 全程使用物理坐标）。
    // Cocoa 全局坐标（point）按"包含光标的屏"的 backingScaleFactor 换算：
    //   swinxX = cocoaX * k;  swinxY = (H1 - cocoaY) * k
    // 单屏时与旧实现（所在屏翻转+乘 scale）完全一致，多屏时以主屏高度 H1
    // 为全局翻转基准，保证与显示器矩形（像素）衔接。
    swinxNsWinPointFromCocoa(mouseLocation, ppt);
    return TRUE;
    }
}

float getScale(){
    @autoreleasepool {
    NSScreen *screen = [NSScreen mainScreen];
    return  [screen backingScaleFactor];
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
            CGPathRef cgPath = CGPathCreateFromNSBezierPath(path);  // 转换为CGPath
            maskLayer.path = cgPath;      // CAShapeLayer 会拷贝一份 path
            CGPathRelease(cgPath);        // 释放创建引用，避免每次调用泄漏
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

BOOL isNsWindowMinimized(HWND hWnd){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win && win.window ){
            return [win window].isMiniaturized;
        }
        return FALSE;
    }
}
BOOL isNsWindowMaximized(HWND hWnd){
    @autoreleasepool{
        SNsWindow *win = getNsWindow(hWnd);
        if(win && win.window ){
            return [win window].isZoomed;
        }
        return FALSE;
    }
}