#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#import "helper.h"
#include <objidl.h>
#include <cairo.h>
#include <log.h>
#define kLogTag "helper"

BOOL EnumDataOjbect(IDataObject *pdo, FUNENUMDATOBOJECT fun, void *param){
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

#if defined(CAIRO_HAS_QUARTZ_FONT) && CAIRO_HAS_QUARTZ_FONT

BOOL macos_register_font(const char *utf8Path) {
    @autoreleasepool {
        // 1. 将UTF-8路径转换为NSString
        NSString *path = [NSString stringWithUTF8String:utf8Path];
        if (!path) {
            SLOG_STMI()<<"Invalid UTF-8 path="<<utf8Path;
            return NO;
        }
        
        // 2. 创建文件URL
        NSURL *fontURL = [NSURL fileURLWithPath:path];
        if (!fontURL) {
            SLOG_STMI()<<"create fileURL failed, path="<<utf8Path;
            return NO;
        }
        
        CFErrorRef error;
        // 4. 注册字体(仅当前进程有效)
        BOOL success = CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL, 
                                                     kCTFontManagerScopeProcess, 
                                                     &error);
        if (!success) {
            CFStringRef errorDescription = CFErrorCopyDescription(error);
            if(errorDescription){
                SLOG_STMW() << "Failed to register font: " << utf8Path << " error="<< CFStringGetCStringPtr(errorDescription, kCFStringEncodingUTF8);
                CFRelease(errorDescription);
            }else{
                SLOG_STMW() << "Failed to register font: " << utf8Path;
            }
            CFRelease(error);
            return NO;
        }
        
        return YES;
    }
}
#endif