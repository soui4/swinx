
#import <Cocoa/Cocoa.h>
#include <cairo-quartz.h>
#include "STrayIconMgr.h"
#include <gdi.h>

#include <wnd.h>
#include "log.h"
#define kLogTag "traywnd"


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
    if(bitmap.bmBitsPixel != 32 || bitmap.bmBits == NULL)
        return nil;
    unsigned char *bitmapData = (unsigned char *)bitmap.bmBits;
    NSBitmapImageRep *bitmapRep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:&bitmapData
                                                                               pixelsWide:bitmap.bmWidth
                                                                               pixelsHigh:bitmap.bmHeight
                                                                            bitsPerSample:8
                                                                          samplesPerPixel:4
                                                                                 hasAlpha:YES
                                                                                 isPlanar:NO
                                                                               colorSpaceName:NSCalibratedRGBColorSpace
                                                                                  bytesPerRow:bitmap.bmWidth * 4
                                                                                 bitsPerPixel:32];

    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(bitmap.bmWidth, bitmap.bmHeight)];
    [image addRepresentation:bitmapRep];
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

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
        [_statusItem setTarget:self];
    }
    return self;
}

- (void)statusItemClicked:(id)sender {
    if (_lpData->hWnd && (_lpData->uFlags & NIF_MESSAGE))
    {
        PostMessage(_lpData->hWnd, _lpData->uCallbackMessage, _lpData->uID, WM_LBUTTONDOWN);
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

