
#import <Cocoa/Cocoa.h>
#include <cairo-quartz.h>
#include "STrayIconMgr.h"
#include <gdi.h>

#include <wnd.h>
#include "log.h"
#define kLogTag "traywnd"
#undef interface

@interface IconProxy : NSObject{
    NSStatusItem *_statusItem;
    PNOTIFYICONDATAA _lpData;
}
- (instancetype)init:(PNOTIFYICONDATAA)lpData;
-(void)modify:(PNOTIFYICONDATAA) lpData;
@end

// 将 HICON 转换为 NSImage
NSImage *imageFromHICON(HICON hIcon) {
    @autoreleasepool{
    // 获取图标尺寸
    ICONINFO iconInfo;
    GetIconInfo(hIcon, &iconInfo);
    BITMAP bitmap;
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bitmap);
    if(bitmap.bmBitsPixel != 32 || bitmap.bmBits == NULL) {
        DeleteObject(iconInfo.hbmColor);
        DeleteObject(iconInfo.hbmMask);
        return nil;
    }
    
    int width = bitmap.bmWidth;
    int height = bitmap.bmHeight;
    int bytesPerRow = bitmap.bmWidthBytes;
    
    // 复制 bitmap 数据
    size_t dataSize = bytesPerRow * height;
    unsigned char *copiedData = (unsigned char *)malloc(dataSize);
    if (!copiedData) {
        DeleteObject(iconInfo.hbmColor);
        DeleteObject(iconInfo.hbmMask);
        return nil;
    }
    memcpy(copiedData, bitmap.bmBits, dataSize);
    
    // 现在可以安全地删除 GDI 对象
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
    
    // 创建 CGDataProvider 使用复制的数据
    // 提供 release callback 来释放内存
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        NULL,  // info - 不需要
        copiedData,  // data
        dataSize,  // size
        [](void *info, const void *data, size_t size) {
            // Release callback - 释放复制的数据
            free((void *)data);
        }
    );
    
    if (!provider) {
        free(copiedData);
        return nil;
    }
    
    // 创建 CGImage
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef cgImage = CGImageCreate(
        width,  // width
        height,  // height
        8,  // bitsPerComponent
        32,  // bitsPerPixel
        bytesPerRow,  // bytesPerRow
        colorSpace,  // space
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst,  // bitmapInfo
        provider,  // provider
        NULL,  // decode
        false,  // shouldInterpolate
        kCGRenderingIntentDefault  // intent
    );
    
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
    
    if (!cgImage) {
        return nil;
    }
    
    // 从 CGImage 创建 NSImage
    NSImage *image = [[NSImage alloc] initWithCGImage:cgImage size:NSMakeSize(width, height)];
    [image setTemplate:NO];
    
    CGImageRelease(cgImage);
    
    return image;
    }
}

@implementation IconProxy

- (instancetype)init:(PNOTIFYICONDATAA)lpData {
    self = [super init];
    if (self) {
        _lpData = lpData;
        // 创建状态栏项
        _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        _statusItem.image = imageFromHICON(lpData->hIcon);

        // 设置点击事件
        [_statusItem setAction:@selector(statusItemClicked:)];
        [_statusItem sendActionOn:NSLeftMouseDownMask | NSRightMouseDownMask];
        [_statusItem setTarget:self];
    }
    return self;
}

- (void)statusItemClicked:(id)sender {
    NSEvent *event = [NSApp currentEvent];
    if (_lpData->hWnd && (_lpData->uFlags & NIF_MESSAGE))
    {
        PostMessage(_lpData->hWnd, _lpData->uCallbackMessage, _lpData->uID, event.type == NSEventTypeLeftMouseDown? WM_LBUTTONDOWN:WM_RBUTTONDOWN);
    }
}

-(void)modify:(PNOTIFYICONDATAA) lpData{
    @autoreleasepool { 
    
    if (lpData->uFlags & NIF_MESSAGE)
        _lpData->uCallbackMessage = lpData->uCallbackMessage;
    if (lpData->uFlags & NIF_ICON)
    {
        _statusItem.image = imageFromHICON(lpData->hIcon);
    }
    if (lpData->uFlags & NIF_TIP)
    {
        _statusItem.toolTip = [NSString stringWithUTF8String:lpData->szTip];
    }
    if (lpData->uFlags & NIF_INFO)
    {
        strcpy_s(_lpData->szInfo, 256, lpData->szInfo);
    }
    if (lpData->uFlags & NIF_GUID)
    {
        memcpy(&_lpData->guidItem, &lpData->guidItem, sizeof(GUID));
    }
    }
}

@end

STrayIconMgr::STrayIconMgr()
{
}

STrayIconMgr::~STrayIconMgr()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        CFRelease((*it)->hTrayProxy);
        free(*it);
    }
    m_lstTrays.clear();
}

BOOL STrayIconMgr::AddIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (findIcon(lpData) != m_lstTrays.end())
        return FALSE;
    //@autoreleasepool 
    {
        TrayIconData *icon = (TrayIconData *)malloc(sizeof(TrayIconData));
        memcpy(icon, lpData, sizeof(NOTIFYICONDATAA));
        IconProxy *proxy = [[IconProxy alloc] init:lpData];
        icon->hTrayProxy = (__bridge_retained void *)proxy;
        m_lstTrays.push_back(icon);
        return TRUE;
    }
}

BOOL STrayIconMgr::ModifyIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;
    @autoreleasepool {
        IconProxy *proxy = (__bridge IconProxy *)((*it)->hTrayProxy);
        [proxy modify:lpData];
        return TRUE;
    }
}

BOOL STrayIconMgr::DelIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;
    CFRelease((*it)->hTrayProxy);
    free(*it);
    m_lstTrays.erase(it);
    return TRUE;
}

STrayIconMgr::TRAYLIST::iterator STrayIconMgr::findIcon(PNOTIFYICONDATAA src)
{
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        if ((*it)->hWnd == src->hWnd && (*it)->uID == src->uID)
            return it;
    }
    return m_lstTrays.end();
}

BOOL STrayIconMgr::NotifyIcon(DWORD  dwMessage, PNOTIFYICONDATAA lpData) {
		switch (dwMessage) {
		case NIM_ADD:return AddIcon(lpData);
		case NIM_DELETE:return DelIcon(lpData);
		case NIM_MODIFY:return ModifyIcon(lpData);
		}
		return FALSE;
	}

