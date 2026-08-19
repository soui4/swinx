// iOS 对话框实现：使用 UIDocumentPickerViewController 替代 NSOpenPanel/NSSavePanel。
// 由于 UIDocumentPicker 为异步回调，此处通过 dispatch_semaphore + CFRunLoop 阻塞等待，
// 以兼容 Windows 同步式 API（GetOpenFileNameA 等）。
// iOS 无内置颜色/字体选择面板，SChooseColor/ChooseFont 返回 FALSE。

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include <objc/objc.h>
#include <commdlg.h>
#include "sysapi.h"
#include "winuser.h"
#include "wnd.h"
#include "log.h"
#undef interface   // 防止 basetyps.h 中 #define interface struct 与 ObjC @interface 冲突
#define kLogTag "dlghelper"

// 文档选择代理：桥接 UIDocumentPickerDelegate 到 block
@interface SwinxDocPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^completion)(NSArray<NSURL *> *urls);
@end

@implementation SwinxDocPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (self.completion) self.completion(urls);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (self.completion) self.completion(@[]);
}
@end

// 获取用于呈现的 ViewController
static UIViewController *getPresentingViewController(HWND hwndOwner) {
    UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
    UIViewController *vc = keyWindow.rootViewController;
    while (vc.presentedViewController) {
        vc = vc.presentedViewController;
    }
    return vc;
}

// 阻塞等待 picker 完成
static NSArray<NSURL *> *runDocPicker(UIDocumentPickerViewController *picker, HWND hwndOwner) {
    __block NSArray<NSURL *> *resultUrls = @[];
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    SwinxDocPickerDelegate *delegate = [[SwinxDocPickerDelegate alloc] init];
    delegate.completion = ^(NSArray<NSURL *> *urls) {
        resultUrls = urls ?: @[];
        dispatch_semaphore_signal(sem);
    };
    picker.delegate = delegate;

    UIViewController *presenter = getPresentingViewController(hwndOwner);
    if (!presenter) {
        SLOG_STMW() << "no presenting view controller for doc picker";
        return @[];
    }

    [presenter presentViewController:picker animated:YES completion:nil];

    // 运行 runloop 直到完成回调触发
    while (dispatch_semaphore_wait(sem, DISPATCH_TIME_NOW) != 0) {
        CFRunLoopRunInMode((CFStringRef)NSDefaultRunLoopMode, 0.05, true);
    }
    return resultUrls;
}

// 从 OPENFILENAMEA 的 lpstrFilter 解析扩展名列表
static NSMutableArray<NSString *> *parseExtensions(LPCSTR lpstrFilter) {
    NSMutableArray<NSString *> *exts = [NSMutableArray array];
    if (!lpstrFilter)
        return exts;
    const char *filter = lpstrFilter;
    while (*filter) {
        filter += strlen(filter) + 1; // skip description
        if (!*filter) break;
        NSString *extensions = [NSString stringWithUTF8String:filter];
        filter += strlen(filter) + 1;
        NSArray *extArray = [extensions componentsSeparatedByString:@";"];
        for (NSString *ext in extArray) {
            NSString *cleanExt = [ext stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if ([cleanExt hasPrefix:@"*."])
                cleanExt = [cleanExt substringFromIndex:2];
            else if ([cleanExt hasPrefix:@"."])
                cleanExt = [cleanExt substringFromIndex:1];
            if (cleanExt.length > 0 && ![cleanExt isEqualToString:@"*"]) {
                cleanExt = [cleanExt lowercaseString];
                if (![exts containsObject:cleanExt])
                    [exts addObject:cleanExt];
            }
        }
    }
    return exts;
}

// 将扩展名映射为 UTI
static NSMutableArray<NSString *> *extensionsToUTIs(NSArray<NSString *> *exts) {
    // 常见扩展名到 UTI 的映射
    NSDictionary<NSString *, NSString *> *extToUti = @{
        @"txt": @"public.plain-text",
        @"rtf": @"public.rtf",
        @"html": @"public.html",
        @"htm": @"public.html",
        @"xml": @"public.xml",
        @"json": @"public.json",
        @"pdf": @"com.adobe.pdf",
        @"png": @"public.png",
        @"jpg": @"public.jpeg",
        @"jpeg": @"public.jpeg",
        @"gif": @"com.compuserve.gif",
        @"bmp": @"com.microsoft.bmp",
        @"ico": @"com.microsoft.icon",
        @"tif": @"public.tiff",
        @"tiff": @"public.tiff",
        @"zip": @"public.zip-archive",
        @"gz": @"org.gnu.gnu-zip-archive",
        @"tar": @"public.tar-archive",
        @"csv": @"public.comma-separated-values-text",
        @"mp3": @"public.mp3",
        @"mp4": @"public.mpeg-4",
        @"mov": @"com.apple.quicktime-movie",
        @"wav": @"com.microsoft.waveform-audio",
        @"css": @"public.css",
        @"js": @"com.netscape.javascript-source",
        @"c": @"public.c-source",
        @"cpp": @"public.c-plus-plus-source",
        @"h": @"public.c-header",
        @"hpp": @"public.c-plus-plus-header",
        @"m": @"public.objective-c-source",
        @"mm": @"public.objective-c-plus-plus-source",
        @"swift": @"public.swift-source",
        @"plist": @"com.apple.property-list",
    };
    NSMutableArray<NSString *> *utis = [NSMutableArray array];
    for (NSString *ext in exts) {
        NSString *uti = extToUti[ext.lowercaseString];
        if (uti && ![utis containsObject:uti])
            [utis addObject:uti];
    }
    return utis;
}

static BOOL GetOpenFileNameiOS(OPENFILENAMEA *lpofn) {
    @autoreleasepool {
        NSMutableArray<NSString *> *utis = extensionsToUTIs(parseExtensions(lpofn->lpstrFilter));
        if (utis.count == 0)
            [utis addObject:@"public.data"];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initWithDocumentTypes:utis
                                                                  inMode:UIDocumentPickerModeOpen];
#pragma clang diagnostic pop
        picker.allowsMultipleSelection = (lpofn->Flags & OFN_ALLOWMULTISELECT) != 0;
        picker.modalPresentationStyle = UIModalPresentationFormSheet;

        if (lpofn->lpstrTitle) {
            picker.title = [NSString stringWithUTF8String:lpofn->lpstrTitle];
        }

        NSArray<NSURL *> *urls = runDocPicker(picker, lpofn->hwndOwner);
        if (urls.count == 0)
            return NO;

        char *buffer = lpofn->lpstrFile;
        size_t remaining = lpofn->nMaxFile;

        if (urls.count == 1) {
            NSString *path = [urls[0] path];
            if (path.length + 1 > remaining)
                return NO;
            strncpy(buffer, path.UTF8String, remaining);
            buffer[remaining - 1] = '\0';
            if (lpofn->lpstrFileTitle && lpofn->nMaxFileTitle > 0) {
                NSString *fileName = [urls[0] lastPathComponent];
                strncpy(lpofn->lpstrFileTitle, fileName.UTF8String, lpofn->nMaxFileTitle);
                lpofn->lpstrFileTitle[lpofn->nMaxFileTitle - 1] = '\0';
            }
        } else {
            // 多选：Windows 格式 目录\0文件1\0文件2\0\0
            NSURL *firstURL = urls[0];
            NSString *commonDir = firstURL.URLByDeletingLastPathComponent.path;
            if (commonDir.length + 1 > remaining)
                return NO;
            strncpy(buffer, commonDir.UTF8String, remaining);
            size_t dirLen = strlen(buffer);
            buffer += dirLen;
            remaining -= dirLen;
            if (remaining < 1) return NO;
            *buffer++ = '\0';
            remaining--;
            for (NSURL *url in urls) {
                NSString *filename = url.lastPathComponent;
                const char *utf8 = filename.UTF8String;
                size_t fnLen = strlen(utf8);
                if (fnLen + 1 > remaining) return NO;
                strncpy(buffer, utf8, remaining);
                buffer += fnLen;
                remaining -= fnLen;
                if (remaining < 1) return NO;
                *buffer++ = '\0';
                remaining--;
            }
            if (remaining < 1) return NO;
            *buffer = '\0';
            lpofn->nFileOffset = (WORD)(dirLen + 1);
        }
        return YES;
    }
}

static BOOL GetSaveFileNameiOS(OPENFILENAMEA *lpofn) {
    /*
    @autoreleasepool {
        // iOS 无传统"另存为"面板，使用 ExportTo 模式导出临时文件到用户选择的位置。
        NSString *fileName = lpofn->lpstrFile && strlen(lpofn->lpstrFile) > 0
            ? [NSString stringWithUTF8String:lpofn->lpstrFile].lastPathComponent
            : @"untitled";
        if (lpofn->lpstrDefExt) {
            NSString *defExt = [NSString stringWithUTF8String:lpofn->lpstrDefExt];
            NSString *curExt = fileName.pathExtension.lowercaseString;
            if (![curExt isEqualToString:defExt.lowercaseString]) {
                fileName = [fileName stringByAppendingPathExtension:defExt];
            }
        }

        NSString *tempPath = [NSTemporaryDirectory() stringByAppendingPathComponent:fileName];
        [@"" writeToFile:tempPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
        NSURL *tempUrl = [NSURL fileURLWithPath:tempPath];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initWithURL:tempUrl
                                                         inMode:UIDocumentPickerModeExportTo];
#pragma clang diagnostic pop
        picker.modalPresentationStyle = UIModalPresentationFormSheet;
        if (lpofn->lpstrTitle) {
            picker.title = [NSString stringWithUTF8String:lpofn->lpstrTitle];
        }

        NSArray<NSURL *> *urls = runDocPicker(picker, lpofn->hwndOwner);
        if (urls.count == 0)
            return NO;

        NSString *path = [urls[0] path];
        if (path.length + 1 <= lpofn->nMaxFile) {
            strncpy(lpofn->lpstrFile, path.UTF8String, lpofn->nMaxFile);
            lpofn->lpstrFile[lpofn->nMaxFile - 1] = '\0';
            if (lpofn->lpstrFileTitle && lpofn->nMaxFileTitle > 0) {
                NSString *fn = [urls[0] lastPathComponent];
                strncpy(lpofn->lpstrFileTitle, fn.UTF8String, lpofn->nMaxFileTitle);
                lpofn->lpstrFileTitle[lpofn->nMaxFileTitle - 1] = '\0';
            }
            return YES;
        }
        return NO;
    }
     */
    return FALSE;
}

static BOOL SelectFolderiOS(HWND hwndOwner, const char *lpszTitle, char *lpszFolderPath, int nMaxFolderPath) {
    @autoreleasepool {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.folder"]
                                                                  inMode:UIDocumentPickerModeOpen];
#pragma clang diagnostic pop
        picker.allowsMultipleSelection = NO;
        picker.modalPresentationStyle = UIModalPresentationFormSheet;
        if (lpszTitle) {
            picker.title = [NSString stringWithUTF8String:lpszTitle];
        }

        NSArray<NSURL *> *urls = runDocPicker(picker, hwndOwner);
        if (urls.count == 0)
            return NO;

        NSString *path = [urls[0] path];
        if (path.length + 1 <= (NSUInteger)nMaxFolderPath) {
            strncpy(lpszFolderPath, path.UTF8String, nMaxFolderPath);
            lpszFolderPath[nMaxFolderPath - 1] = '\0';
            return YES;
        }
        return NO;
    }
}

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode) {
    switch (mode) {
        case OPEN:
            return GetOpenFileNameiOS(p);
        case SAVE:
            return GetSaveFileNameiOS(p);
        case FOLDER:
            return SelectFolderiOS(p->hwndOwner, p->lpstrTitle, p->lpstrFile, p->nMaxFile);
    }
    return FALSE;
}

// iOS 无内置颜色选择面板
BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out) {
    (void)parent;
    (void)initClr;
    (void)out;
    return FALSE;
}

// iOS 无内置字体选择面板
BOOL WINAPI ChooseFontA(LPCHOOSEFONTA p) {
    (void)p;
    return FALSE;
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW p) {
    if (!p || !p->lpLogFont)
        return FALSE;

    CHOOSEFONTA chooseA;
    LOGFONTA logfontA;

    chooseA.lStructSize = sizeof(CHOOSEFONTA);
    chooseA.hwndOwner = p->hwndOwner;
    chooseA.hDC = p->hDC;
    chooseA.lpLogFont = &logfontA;
    chooseA.iPointSize = p->iPointSize;
    chooseA.Flags = p->Flags;
    chooseA.rgbColors = p->rgbColors;
    chooseA.lCustData = p->lCustData;
    chooseA.lpfnHook = p->lpfnHook;
    chooseA.lpTemplateName = nullptr;
    chooseA.hInstance = p->hInstance;
    chooseA.lpszStyle = nullptr;
    chooseA.nFontType = p->nFontType;
    chooseA.nSizeMin = p->nSizeMin;
    chooseA.nSizeMax = p->nSizeMax;

    logfontA.lfHeight = p->lpLogFont->lfHeight;
    logfontA.lfWidth = p->lpLogFont->lfWidth;
    logfontA.lfEscapement = p->lpLogFont->lfEscapement;
    logfontA.lfOrientation = p->lpLogFont->lfOrientation;
    logfontA.lfWeight = p->lpLogFont->lfWeight;
    logfontA.lfItalic = p->lpLogFont->lfItalic;
    logfontA.lfUnderline = p->lpLogFont->lfUnderline;
    logfontA.lfStrikeOut = p->lpLogFont->lfStrikeOut;
    logfontA.lfCharSet = p->lpLogFont->lfCharSet;
    logfontA.lfOutPrecision = p->lpLogFont->lfOutPrecision;
    logfontA.lfClipPrecision = p->lpLogFont->lfClipPrecision;
    logfontA.lfQuality = p->lpLogFont->lfQuality;
    logfontA.lfPitchAndFamily = p->lpLogFont->lfPitchAndFamily;

    WideCharToMultiByte(CP_UTF8, 0, p->lpLogFont->lfFaceName, -1,
                    logfontA.lfFaceName, LF_FACESIZE, NULL, NULL);
    chooseA.lpLogFont = &logfontA;

    BOOL result = ChooseFontA(&chooseA);

    if (result) {
        p->lpLogFont->lfHeight = logfontA.lfHeight;
        p->lpLogFont->lfWidth = logfontA.lfWidth;
        p->lpLogFont->lfEscapement = logfontA.lfEscapement;
        p->lpLogFont->lfOrientation = logfontA.lfOrientation;
        p->lpLogFont->lfWeight = logfontA.lfWeight;
        p->lpLogFont->lfItalic = logfontA.lfItalic;
        p->lpLogFont->lfUnderline = logfontA.lfUnderline;
        p->lpLogFont->lfStrikeOut = logfontA.lfStrikeOut;
        p->lpLogFont->lfCharSet = logfontA.lfCharSet;
        p->lpLogFont->lfOutPrecision = logfontA.lfOutPrecision;
        p->lpLogFont->lfClipPrecision = logfontA.lfClipPrecision;
        p->lpLogFont->lfQuality = logfontA.lfQuality;
        p->lpLogFont->lfPitchAndFamily = logfontA.lfPitchAndFamily;

        MultiByteToWideChar(CP_UTF8, 0, logfontA.lfFaceName, -1,
                           p->lpLogFont->lfFaceName, LF_FACESIZE);

        p->iPointSize = chooseA.iPointSize;
        p->rgbColors = chooseA.rgbColors;
        p->nFontType = chooseA.nFontType;
    }

    return result;
}
