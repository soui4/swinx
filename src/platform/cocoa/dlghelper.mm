#import <Cocoa/Cocoa.h>
#include <objc/objc.h>
#include <commdlg.h>
#include "SNsWindow.h"
#include "sysapi.h"
#include "winuser.h"
#include "wnd.h"
//todo: implement the following functions
@interface ChooseColorDlg : NSObject<NSWindowDelegate>
-(BOOL)chooseColor:(HWND)parent initClr:(const COLORREF *)initClr out:(COLORREF *)out;
@end

@implementation ChooseColorDlg{
    NSColorPanel *_panel;
    COLORREF _out;
}

-(instancetype)init{
    self = [super init];
    if(self){
        _panel = nil;
        _out = 0;
    }
    return self;
}

- (BOOL)windowShouldClose:(id)sender{
    PostThreadMessage(GetCurrentThreadId(), WM_CANCELMODE, 0, 0);
    return YES;
}

#define FloatColor2IntColor(c) ((int)(c * 255))
- (void)colorChanged:(id)sender {
    NSColor *color = [_panel color];
    _out = RGBA(FloatColor2IntColor(color.redComponent),FloatColor2IntColor(color.greenComponent),FloatColor2IntColor(color.blueComponent),FloatColor2IntColor(color.alphaComponent));
}

-(BOOL)chooseColor:(HWND)parent initClr:(const COLORREF *)initClr out:(COLORREF *)out{
    _panel = [[NSColorPanel alloc] init];
    [_panel setDelegate:self];
    [_panel setTarget:self];
    [_panel setAction:@selector(colorChanged:)];
    [_panel setMode:NSColorPanelModeWheel];
    [_panel setShowsAlpha:YES];

    if(initClr){
        [_panel setColor:[NSColor colorWithRed:(initClr[0])/255.0 green:(initClr[0])/255.0 blue:(initClr[0])/255.0 alpha:(initClr[0])/255.0]];
    }
    if(parent){
        int parentId = getNsWindowId(parent);
        if(parentId){
            NSWindow *pParent = [NSApp windowWithWindowNumber:parentId];
            if(pParent){
                [_panel setParentWindow:pParent];
            }
        }
        EnableWindow(parent, FALSE);
    }
    [_panel setIsVisible:YES];
    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){ 
        if(msg.message == WM_CANCELMODE){
            break;
        }
        if(msg.message == WM_QUIT){
            PostQuitMessage(msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    [_panel setIsVisible:NO];
    [_panel setDelegate:nil];
    if(parent){
        EnableWindow(parent, TRUE);
    }
    if(!_out)
        return FALSE;
    *out = _out;
    return TRUE;
}

@end

BOOL SChooseColor(HWND parent,const COLORREF initClr[16], COLORREF *out){
    @autoreleasepool
    {
    ChooseColorDlg *dlg = [[ChooseColorDlg alloc] init];
    BOOL ret = [dlg chooseColor:parent initClr:initClr out:out];
    dlg = nil;
    return ret;
    }
}

static BOOL GetOpenFileNameMac(OPENFILENAME *lpofn) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        // 设置父窗口
        if (lpofn->hwndOwner) {
            int parentId = getNsWindowId(lpofn->hwndOwner);
            NSWindow *parentWindow = [NSApp windowWithWindowNumber:parentId];
            if (parentWindow) {
                [panel beginSheetModalForWindow:parentWindow completionHandler:^(NSInteger c){}];
            }
        }
        
        // 处理过滤器字符串
        if (lpofn->lpstrFilter) {
            NSMutableArray *allowedTypes = [NSMutableArray array];
            const char *filter = lpofn->lpstrFilter;
            
            while (*filter) {
                NSString *description = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;
                
                NSString *extensions = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;
                
                NSArray *extArray = [extensions componentsSeparatedByString:@";"];
                for (NSString *ext in extArray) {
                    NSString *cleanExt = [ext stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"*."]];
                    if (cleanExt.length > 0) {
                        [allowedTypes addObject:cleanExt];
                    }
                }
            }
            
            if (allowedTypes.count > 0) {
                panel.allowedFileTypes = allowedTypes;
            }
        }
        
        // 设置初始目录
        if (lpofn->lpstrInitialDir) {
            NSString *initialDir = [NSString stringWithUTF8String:lpofn->lpstrInitialDir];
            panel.directoryURL = [NSURL fileURLWithPath:initialDir];
        }
        
        // 设置标题
        if (lpofn->lpstrTitle) {
            panel.title = [NSString stringWithUTF8String:lpofn->lpstrTitle];
        }
        
        // 处理标志位
        panel.allowsMultipleSelection = (lpofn->Flags & OFN_ALLOWMULTISELECT) != 0;
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.showsHiddenFiles = (lpofn->Flags & OFN_HIDEREADONLY) == 0;
        
        if (lpofn->Flags & OFN_FILEMUSTEXIST) {
            panel.canCreateDirectories = NO;
        } else {
            panel.canCreateDirectories = YES;
        }
        
        // 运行面板
        NSInteger result = [panel runModal];
        
        if (result == NSModalResponseOK) {
            NSArray *urls = panel.URLs;
            // 准备缓冲区
            char *buffer = lpofn->lpstrFile;
            size_t remaining = lpofn->nMaxFile;
            
            if (urls.count == 1) {
                // 单个文件选择 - 直接返回完整路径
                NSString *path = [urls[0] path];
                if (path.length + 1 > remaining) {
                    return NO;  // 缓冲区不足
                }
                strncpy(buffer, path.UTF8String, remaining);
                buffer[remaining - 1] = '\0';  // 确保终止
                
                // 填充文件标题（如果有要求）
                if (lpofn->lpstrFileTitle && lpofn->nMaxFileTitle > 0) {
                    NSString *fileName = [urls[0] lastPathComponent];
                    strncpy(lpofn->lpstrFileTitle, fileName.UTF8String, lpofn->nMaxFileTitle);
                    lpofn->lpstrFileTitle[lpofn->nMaxFileTitle - 1] = '\0';
                }
            } else {
                // 多个文件选择 - Windows特殊格式
                NSURL *firstURL = urls[0];
                NSString *commonDir = firstURL.URLByDeletingLastPathComponent.path;
                
                // 1. 写入目录路径
                if (commonDir.length + 1 > remaining) {
                    return NO;
                }
                strncpy(buffer, commonDir.UTF8String, remaining);
                size_t dirLen = strlen(buffer);
                buffer += dirLen;
                remaining -= dirLen;
                
                // 写入第一个NULL分隔符
                if (remaining < 1) return NO;
                *buffer++ = '\0';
                remaining--;
                
                // 2. 写入每个文件名
                for (NSURL *url in urls) {
                    NSString *filename = url.lastPathComponent;
                    const char *utf8Filename = filename.UTF8String;
                    size_t filenameLen = strlen(utf8Filename);
                    
                    if (filenameLen + 1 > remaining) {
                        return NO;  // 缓冲区不足
                    }
                    
                    strncpy(buffer, utf8Filename, remaining);
                    buffer += filenameLen;
                    remaining -= filenameLen;
                    
                    // 写入NULL分隔符
                    if (remaining < 1) return NO;
                    *buffer++ = '\0';
                    remaining--;
                }
                
                // 3. 写入最终的双NULL终止符
                if (remaining < 1) return NO;
                *buffer = '\0';
                
                // 对于多选，Windows API会设置nFileOffset为目录路径的长度
                lpofn->nFileOffset = (WORD)(dirLen + 1);
            }
            
            return YES;
        }
        
        return NO;
    }
}

static BOOL GetSaveFileNameMac(OPENFILENAME *lpofn) {
    @autoreleasepool {
        NSSavePanel *panel = [NSSavePanel savePanel];
        
        // 设置父窗口
        if (lpofn->hwndOwner) {
            int parentId = getNsWindowId(lpofn->hwndOwner);
            NSWindow *parentWindow = [NSApp windowWithWindowNumber:parentId];
            if (parentWindow) {
                [panel beginSheetModalForWindow:parentWindow completionHandler:^(NSInteger c){}];
            }
        }
        
        // 处理过滤器字符串
        if (lpofn->lpstrFilter) {
            NSMutableArray *allowedTypes = [NSMutableArray array];
            const char *filter = lpofn->lpstrFilter;
            
            while (*filter) {
                NSString *description = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;
                
                NSString *extensions = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;
                
                NSArray *extArray = [extensions componentsSeparatedByString:@";"];
                for (NSString *ext in extArray) {
                    NSString *cleanExt = [ext stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"*."]];
                    if (cleanExt.length > 0) {
                        [allowedTypes addObject:cleanExt];
                    }
                }
            }
            
            if (allowedTypes.count > 0) {
                panel.allowedFileTypes = allowedTypes;
            }
        }
        
        // 设置初始目录
        if (lpofn->lpstrInitialDir) {
            NSString *initialDir = [NSString stringWithUTF8String:lpofn->lpstrInitialDir];
            panel.directoryURL = [NSURL fileURLWithPath:initialDir];
        }
        
        // 设置标题
        if (lpofn->lpstrTitle) {
            panel.title = [NSString stringWithUTF8String:lpofn->lpstrTitle];
        }
        
        // 设置默认文件名
        if (lpofn->lpstrFile && strlen(lpofn->lpstrFile) > 0) {
            panel.nameFieldStringValue = [NSString stringWithUTF8String:lpofn->lpstrFile];
        }
        
        // 设置默认扩展名
        if (lpofn->lpstrDefExt) {
            panel.nameFieldStringValue = [panel.nameFieldStringValue stringByAppendingPathExtension:
                                         [NSString stringWithUTF8String:lpofn->lpstrDefExt]];
        }
        
        // 处理标志位
        panel.showsHiddenFiles = (lpofn->Flags & OFN_HIDEREADONLY) == 0;
        
        // 运行面板
        NSInteger result = [panel runModal];
        
        if (result == NSModalResponseOK) {
            NSString *path = panel.URL.path;
            if (path.length + 1 <= lpofn->nMaxFile) {
                strncpy(lpofn->lpstrFile, path.UTF8String, lpofn->nMaxFile);
                
                if (lpofn->lpstrFileTitle && lpofn->nMaxFileTitle > 0) {
                    NSString *fileName = panel.URL.lastPathComponent;
                    strncpy(lpofn->lpstrFileTitle, fileName.UTF8String, lpofn->nMaxFileTitle);
                }
                
                return YES;
            }
        }
        
        return NO;
    }
}

// 选择文件夹函数
static BOOL SelectFolderMac(HWND hwndOwner,const char * lpszTitle,char * lpszFolderPath, int nMaxFolderPath) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        
        // 配置为只能选择文件夹
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        
        // 设置父窗口
        if (hwndOwner) {
            int parentId = getNsWindowId(hwndOwner);
            NSWindow *parentWindow = [NSApp windowWithWindowNumber:parentId];
            if (parentWindow) {
                [panel beginSheetModalForWindow:parentWindow completionHandler:^(NSInteger c){}];
            }
        }
        
        // 设置对话框标题
        if (lpszTitle) {
            NSString *title = [NSString stringWithUTF8String:lpszTitle];
            panel.title = title;
            panel.message = title; // message显示更大的标题
        }
        
        // 运行模态对话框
        NSInteger result = [panel runModal];
        
        if (result == NSModalResponseOK) {
            NSURL *url = [panel.URLs firstObject];
            if (url) {
                NSString *path = [url path];
                if ([path length] + 1 <= nMaxFolderPath) {
                    strncpy(lpszFolderPath, [path UTF8String], nMaxFolderPath);
                    lpszFolderPath[nMaxFolderPath - 1] = '\0'; // 确保终止
                    return YES;
                }
            }
        }
        
        return NO;
    }
}

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode){
    switch(mode){
        case OPEN:
            return GetOpenFileNameMac(p);
        case SAVE:
            return GetSaveFileNameMac(p);
        case FOLDER:
            return SelectFolderMac(p->hwndOwner, p->lpstrTitle, p->lpstrFile, p->nMaxFile);
    }
    return FALSE;
}