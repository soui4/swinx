#import <Cocoa/Cocoa.h>
#include <objc/objc.h>
#include <commdlg.h>
#include "SNsWindow.h"
#include "sysapi.h"
#include "winuser.h"
#include "wnd.h"
#include "log.h"
#define kLogTag "dlghelper"

// 为了支持进程激活，需要包含ApplicationServices
#include <ApplicationServices/ApplicationServices.h>

// 自定义文件过滤委托
@interface FileFilterDelegate : NSObject <NSOpenSavePanelDelegate>
@property (nonatomic, strong) NSArray<NSString *> *allowedExtensions;
@property (nonatomic, assign) BOOL strictFiltering;
- (instancetype)initWithExtensions:(NSArray<NSString *> *)extensions;
@end

@implementation FileFilterDelegate

- (instancetype)initWithExtensions:(NSArray<NSString *> *)extensions {
    self = [super init];
    if (self) {
        _allowedExtensions = [extensions copy];
        _strictFiltering = YES;
    }
    return self;
}

- (BOOL)panel:(id)sender shouldEnableURL:(NSURL *)url {
    if (!self.strictFiltering || !self.allowedExtensions || self.allowedExtensions.count == 0) {
        return YES;
    }

    // 允许目录
    NSNumber *isDirectory;
    if ([url getResourceValue:&isDirectory forKey:NSURLIsDirectoryKey error:nil] && [isDirectory boolValue]) {
        return YES;
    }

    // 检查文件扩展名
    NSString *pathExtension = [[url pathExtension] lowercaseString];
    for (NSString *allowedExt in self.allowedExtensions) {
        if ([pathExtension isEqualToString:[allowedExt lowercaseString]]) {
            return YES;
        }
    }

    return NO;
}

@end

static NSInteger RunModalPanel(HWND hwndOwner, NSSavePanel *panel) {
    @autoreleasepool {
        SLOG_STMI()<<"RunModalPanel, hwndOwner: "<<hwndOwner;
                // Check application activation state
        if (![NSApp isActive]) {
            SLOG_STMI()<<"WARNING: Application is not active. Panel may not display correctly.";
            SLOG_STMI()<<"Please click on the app's Dock icon to activate it, then try again.";
            // Don't show panel when app is not active - it will fail
            return NSModalResponseCancel;
        }
        
        SLOG_STMI()<<"Application is active, showing panel...";
        // 确保应用程序处于前台状态
        [NSApp activateIgnoringOtherApps:YES];
        [[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateIgnoringOtherApps];

        // 禁用 owner 窗口以实现真正的模态行为
        if (hwndOwner) {
            EnableWindow(hwndOwner, FALSE);
        }

        // 使用 runModal 显示对话框（这会阻塞直到用户操作）
        NSInteger result = [panel runModal];
        
        // 重新启用 owner 窗口并将其带到前台
        if (hwndOwner) {
            EnableWindow(hwndOwner, TRUE);
            setNsWindowToTop(hwndOwner);
        }
        
        return result;
    }
}
// 颜色面板委托：监听窗口关闭事件并退出 modal
@interface ColorPanelDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) BOOL colorChanged;
@end

@implementation ColorPanelDelegate

- (void)windowWillClose:(NSNotification *)notification {
    // 点击关闭按钮时停止 modal session，避免程序卡死
    if ([NSApp modalWindow]) {
        [NSApp stopModalWithCode:NSModalResponseCancel];
    }
}

@end

BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out) {
    if(!out)
        return FALSE;
    @autoreleasepool {
        // 创建颜色面板
        NSColorPanel *colorPanel = [NSColorPanel sharedColorPanel];
        colorPanel.showsAlpha = YES;

        // 颜色面板委托：处理关闭按钮事件
        ColorPanelDelegate *colorDelegate = [[ColorPanelDelegate alloc] init];
        colorPanel.delegate = colorDelegate;

        // 设置初始颜色（如果有）
        if (initClr && out) {
            // COLORREF: 0x00bbggrr
            uint32_t c = initClr[0];
            CGFloat r = ((c >>  0) & 0xFF) / 255.0;
            CGFloat g = ((c >>  8) & 0xFF) / 255.0;
            CGFloat b = ((c >> 16) & 0xFF) / 255.0;
            NSColor *initColor = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
            [colorPanel setColor:initColor];
        }

        // 禁用父窗口
        if (parent) {
            EnableWindow(parent, FALSE);
        }

        // 显示颜色面板并运行模态
        [colorPanel makeKeyAndOrderFront:nil];
        NSInteger modalResult = [NSApp runModalForWindow:colorPanel];
        [colorPanel orderOut:nil];

        // 清理 delegate（sharedColorPanel 是全局共享的，避免野指针）
        colorPanel.delegate = nil;

        // 重新启用父窗口
        if (parent) {
            EnableWindow(parent, TRUE);
            setNsWindowToTop(parent);
        }
        NSColor *pickedColor = [colorPanel color];
        if(!pickedColor)
            return FALSE;
        NSColor *rgbColor = [pickedColor colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]];
        if (!rgbColor) 
            return FALSE;

        // 转换为COLORREF格式
        uint8_t r = (uint8_t)([rgbColor redComponent] * 255);
        uint8_t g = (uint8_t)([rgbColor greenComponent] * 255);
        uint8_t b = (uint8_t)([rgbColor blueComponent] * 255);
        uint32_t a = (uint8_t)([rgbColor alphaComponent] * 255);
        *out = RGBA(r, g, b, a);
        return TRUE;
    }
}

static BOOL GetOpenFileNameMac(OPENFILENAMEA *lpofn) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];

        // 处理过滤器字符串
        NSMutableArray *allowedTypes = [NSMutableArray array];
        if (lpofn->lpstrFilter) {
            const char *filter = lpofn->lpstrFilter;

            while (*filter) {
                NSString *description = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;

                NSString *extensions = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;

                // 处理扩展名字符串，支持多种格式
                NSArray *extArray = [extensions componentsSeparatedByString:@";"];
                for (NSString *ext in extArray) {
                    // 更仔细地清理扩展名
                    NSString *cleanExt = [ext stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

                    // 移除 "*." 前缀
                    if ([cleanExt hasPrefix:@"*."]) {
                        cleanExt = [cleanExt substringFromIndex:2];
                    } else if ([cleanExt hasPrefix:@"."]) {
                        cleanExt = [cleanExt substringFromIndex:1];
                    }

                    // 确保扩展名不为空且不是通配符
                    if (cleanExt.length > 0 && ![cleanExt isEqualToString:@"*"]) {
                        // 转换为小写以确保匹配
                        cleanExt = [cleanExt lowercaseString];
                        if (![allowedTypes containsObject:cleanExt]) {
                            [allowedTypes addObject:cleanExt];
                        }
                    }
                }
            }
        }

        // 设置文件类型过滤
        if (allowedTypes.count > 0) {
            NSLog(@"[FileDialog] Parsed extensions: %@", allowedTypes);

            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel.allowedFileTypes = allowedTypes;
            #pragma clang diagnostic pop

            panel.allowsOtherFileTypes = NO;
        } else {
            NSLog(@"[FileDialog] No file type filters applied");
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

        // 设置文件过滤委托以实现更精确的过滤
        FileFilterDelegate *filterDelegate = nil;
        if (allowedTypes.count > 0) {
            filterDelegate = [[FileFilterDelegate alloc] initWithExtensions:allowedTypes];
            panel.delegate = filterDelegate;
        }

        // 使用 runModal 方式显示对话框
        NSInteger result = RunModalPanel(lpofn->hwndOwner, panel);
        
        if (result == NSModalResponseOK) {
            NSArray<NSURL *> *urls = panel.URLs;
            
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

static BOOL GetSaveFileNameMac(OPENFILENAMEA *lpofn) {
    @autoreleasepool {
        NSSavePanel *panel = [NSSavePanel savePanel];

        // 处理过滤器字符串
        NSMutableArray *allowedTypes = [NSMutableArray array];
        if (lpofn->lpstrFilter) {
            const char *filter = lpofn->lpstrFilter;

            while (*filter) {
                NSString *description = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;

                NSString *extensions = [NSString stringWithUTF8String:filter];
                filter += strlen(filter) + 1;

                // 处理扩展名字符串，支持多种格式
                NSArray *extArray = [extensions componentsSeparatedByString:@";"];
                for (NSString *ext in extArray) {
                    // 更仔细地清理扩展名
                    NSString *cleanExt = [ext stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

                    // 移除 "*." 前缀
                    if ([cleanExt hasPrefix:@"*."]) {
                        cleanExt = [cleanExt substringFromIndex:2];
                    } else if ([cleanExt hasPrefix:@"."]) {
                        cleanExt = [cleanExt substringFromIndex:1];
                    }

                    // 确保扩展名不为空且不是通配符
                    if (cleanExt.length > 0 && ![cleanExt isEqualToString:@"*"]) {
                        // 转换为小写以确保匹配
                        cleanExt = [cleanExt lowercaseString];
                        if (![allowedTypes containsObject:cleanExt]) {
                            [allowedTypes addObject:cleanExt];
                        }
                    }
                }
            }
        }

        // 设置文件类型过滤
        if (allowedTypes.count > 0) {
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel.allowedFileTypes = allowedTypes;
            #pragma clang diagnostic pop

            panel.allowsOtherFileTypes = NO;
        }

        // 设置文件过滤委托以实现更精确的过滤
        FileFilterDelegate *filterDelegate = nil;
        if (allowedTypes.count > 0) {
            filterDelegate = [[FileFilterDelegate alloc] initWithExtensions:allowedTypes];
            panel.delegate = filterDelegate;
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
            NSString *defExt = [NSString stringWithUTF8String:lpofn->lpstrDefExt];
            if (![panel.nameFieldStringValue.pathExtension isEqualToString:defExt]) {
                panel.nameFieldStringValue = [panel.nameFieldStringValue stringByAppendingPathExtension:defExt];
            }
        }
        
        // 处理标志位
        panel.showsHiddenFiles = (lpofn->Flags & OFN_HIDEREADONLY) == 0;

        // 使用 runModal 方式显示对话框
        NSInteger result = RunModalPanel(lpofn->hwndOwner, panel);
        
        if (result == NSModalResponseOK) {
            NSURL *selectedURL = panel.URL;
            NSString *path = selectedURL.path;
            if (path.length + 1 <= lpofn->nMaxFile) {
                strncpy(lpofn->lpstrFile, path.UTF8String, lpofn->nMaxFile);
                lpofn->lpstrFile[lpofn->nMaxFile - 1] = '\0';
                
                if (lpofn->lpstrFileTitle && lpofn->nMaxFileTitle > 0) {
                    NSString *fileName = selectedURL.lastPathComponent;
                    strncpy(lpofn->lpstrFileTitle, fileName.UTF8String, lpofn->nMaxFileTitle);
                    lpofn->lpstrFileTitle[lpofn->nMaxFileTitle - 1] = '\0';
                }
                
                return YES;
            }
        }
        
        return NO;
    }
}

// 选择文件夹函数
static BOOL SelectFolderMac(HWND hwndOwner, const char *lpszTitle, char *lpszFolderPath, int nMaxFolderPath) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        
        // 配置为只能选择文件夹
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        
        // 设置对话框标题
        if (lpszTitle) {
            NSString *title = [NSString stringWithUTF8String:lpszTitle];
            panel.title = title;
            panel.message = title; // message显示更大的标题
        }

        // 使用 runModal 方式显示对话框
        NSInteger result = RunModalPanel(hwndOwner, panel);
        
        if (result == NSModalResponseOK) {
            NSURL *selectedURL = [panel.URLs firstObject];
            NSString *path = [selectedURL path];
            if ([path length] + 1 <= nMaxFolderPath) {
                strncpy(lpszFolderPath, [path UTF8String], nMaxFolderPath);
                lpszFolderPath[nMaxFolderPath - 1] = '\0'; // 确保终止
                return YES;
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

// 辅助函数：将LOGFONT转换为NSFont
static NSFont* LogFontToNSFont(const LOGFONTA* logFont) {
    if (!logFont) return nil;

    // 获取字体名称
    NSString *fontName = nil;
    if (strlen(logFont->lfFaceName) > 0) {
        fontName = [NSString stringWithUTF8String:logFont->lfFaceName];
    } else {
        fontName = @"Helvetica"; // 默认字体
    }

    // 计算字体大小（从逻辑单位转换为点）
    CGFloat fontSize = abs(logFont->lfHeight);
    if (fontSize == 0) fontSize = 12; // 默认大小

    // 处理字体样式
    NSFontTraitMask traits = 0;
    if (logFont->lfWeight >= FW_BOLD) {
        traits |= NSBoldFontMask;
    }
    if (logFont->lfItalic) {
        traits |= NSItalicFontMask;
    }

    // 尝试创建字体
    NSFont *font = nil;
    if (traits != 0) {
        NSFontManager *fontManager = [NSFontManager sharedFontManager];
        NSFont *baseFont = [NSFont fontWithName:fontName size:fontSize];
        if (baseFont) {
            font = [fontManager convertFont:baseFont toHaveTrait:traits];
        }
    }

    if (!font) {
        font = [NSFont fontWithName:fontName size:fontSize];
    }

    if (!font) {
        font = [NSFont systemFontOfSize:fontSize];
    }

    return font;
}

// 辅助函数：将NSFont转换为LOGFONT
static void NSFontToLogFont(NSFont* font, LOGFONTA* logFont) {
    if (!font || !logFont) return;

    memset(logFont, 0, sizeof(LOGFONTA));

    // 设置字体高度（转换为逻辑单位）
    logFont->lfHeight = -(LONG)[font pointSize];
    logFont->lfWidth = 0;
    logFont->lfEscapement = 0;
    logFont->lfOrientation = 0;

    // 设置字体权重和样式
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
    NSFontTraitMask traits = [fontManager traitsOfFont:font];

    if (traits & NSBoldFontMask) {
        logFont->lfWeight = FW_BOLD;
    } else {
        logFont->lfWeight = FW_NORMAL;
    }

    logFont->lfItalic = (traits & NSItalicFontMask) ? 1 : 0;
    logFont->lfUnderline = 0; // NSFont不直接支持下划线属性
    logFont->lfStrikeOut = 0; // NSFont不直接支持删除线属性

    // 设置字符集和其他属性
    logFont->lfCharSet = DEFAULT_CHARSET;
    logFont->lfOutPrecision = OUT_DEFAULT_PRECIS;
    logFont->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    logFont->lfQuality = DEFAULT_QUALITY;
    logFont->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    // 设置字体名称
    NSString *familyName = [font familyName];
    if (familyName) {
        strncpy(logFont->lfFaceName, [familyName UTF8String], LF_FACESIZE - 1);
        logFont->lfFaceName[LF_FACESIZE - 1] = '\0';
    }
}

// 简化的字体选择委托类


// 字体面板委托：监听字体变化 + 窗口关闭事件
@interface FontPanelDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSFont *selectedFont;
@property (nonatomic, assign) BOOL fontChanged;
@end

@implementation FontPanelDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _selectedFont = nil;
        _fontChanged = NO;
    }
    return self;
}

- (void)changeFont:(id)sender {
    // 直接从 fontManager 取用户在面板上选择的完整字体
    // （convertFont: 只应用 trait/size 改变，不更新 family）
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
    NSFont *newFont = [fontManager selectedFont];
    if (newFont) {
        self.selectedFont = newFont;
        self.fontChanged = YES;
    }
}

- (void)windowWillClose:(NSNotification *)notification {
    // 点击关闭按钮时停止 modal session，避免程序卡死
    if ([NSApp modalWindow]) {
        [NSApp stopModalWithCode:NSModalResponseCancel];
    }
}

@end
static FontPanelDelegate *g_fontPanelDelegate = nil;          // 当前有效的字体面板委托
static id g_fontPanelActivationObserver = nil;               // 激活通知观察者（持久）

static void registerFontPanelActivationObserver(void) {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        g_fontPanelActivationObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSApplicationDidBecomeActiveNotification
                         object:nil
                          queue:[NSOperationQueue mainQueue]
                     usingBlock:^(NSNotification * _Nonnull note) {
            NSFontPanel *fp = [NSFontPanel sharedFontPanel];
            // 如果当前正在模态运行（即在 ChooseFontMac 内部），不干预
            if ([NSApp modalWindow] == fp) return;
            // 只有当我们自己设置的委托还在时，才表示这个面板是我们打开的，需要隐藏
            if ([fp delegate] == g_fontPanelDelegate) {
                [fp orderOut:nil];
                fp.delegate = nil;
                g_fontPanelDelegate = nil;   // 清理委托引用，此后不再自动隐藏
            }
        }];
    });
}

static BOOL ChooseFontMac(LPCHOOSEFONTA p) {
    @autoreleasepool {
        if (!p || !p->lpLogFont) {
            return FALSE;
        }

        // 禁用父窗口
        if (p->hwndOwner) {
            EnableWindow(p->hwndOwner, FALSE);
        }

        // 获取字体面板和字体管理器
        NSFontPanel *fontPanel = [NSFontPanel sharedFontPanel];
        NSFontManager *fontManager = [NSFontManager sharedFontManager];

        // ---- 新增：禁用窗口状态恢复 ----
        if ([fontPanel respondsToSelector:@selector(setRestorationClass:)]) {
            [fontPanel setRestorationClass:nil];
        }
        // -----------------------------

        // 清理前一个未释放的委托（如果有）
        if (g_fontPanelDelegate) {
            if ([fontPanel delegate] == g_fontPanelDelegate) {
                fontPanel.delegate = nil;
            }
            g_fontPanelDelegate = nil;
        }

        // 创建新的委托并保存为全局
        FontPanelDelegate *delegate = [[FontPanelDelegate alloc] init];
        g_fontPanelDelegate = delegate;
        fontPanel.delegate = delegate;

        // 设置初始字体
        NSFont *initialFont = LogFontToNSFont(p->lpLogFont);
        if (initialFont) {
            [fontManager setSelectedFont:initialFont isMultiple:NO];
        }
        delegate.selectedFont = initialFont;

        // 设置字体管理器的目标，监听字体变化
        [fontManager setTarget:delegate];
        [fontManager setAction:@selector(changeFont:)];

        // 注册持久化的激活观察者（只注册一次）
        registerFontPanelActivationObserver();

        // 显示字体面板并运行模态
        [fontPanel makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:fontPanel];
        [fontPanel orderOut:nil];

        // 清理字体管理器的 target（但保留 delegate 以便激活时处理）
        [fontManager setTarget:nil];

        // 重新启用父窗口
        if (p->hwndOwner) {
            EnableWindow(p->hwndOwner, TRUE);
            setNsWindowToTop(p->hwndOwner);
        }

        // 获取最终选择的字体
        NSFont *finalFont = [fontManager selectedFont];
        BOOL result = FALSE;
        if (finalFont) {
            NSFontToLogFont(finalFont, p->lpLogFont);
            if (p->Flags & CF_INITTOLOGFONTSTRUCT) {
                p->iPointSize = (INT)([finalFont pointSize] * 10);
            }
            result = TRUE;
        }

        // 注意：delegate 未在这里置 nil，保留给激活观察者使用。
        // 但若用户不再切换激活，则 delegate 会一直保留，直到下次调用 ChooseFontMac 时被清理。
        return result;
    }
}

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA p) {
    return ChooseFontMac(p);
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW p) {
    if (!p || !p->lpLogFont) {
        return FALSE;
    }

    // 转换CHOOSEFONTW到CHOOSEFONTA
    CHOOSEFONTA chooseA;
    LOGFONTA logfontA;

    // 复制结构体字段
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

    // 转换LOGFONTW到LOGFONTA
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

    // 转换字体名称从宽字符到多字节
    WideCharToMultiByte(CP_UTF8, 0, p->lpLogFont->lfFaceName, -1,
                    logfontA.lfFaceName, LF_FACESIZE, NULL, NULL);
    chooseA.lpLogFont = &logfontA;

    // 调用ANSI版本
    BOOL result = ChooseFontA(&chooseA);

    if (result) {
        // 转换结果回LOGFONTW
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

        // 转换字体名称从多字节到宽字符
        MultiByteToWideChar(CP_UTF8, 0, logfontA.lfFaceName, -1,
                           p->lpLogFont->lfFaceName, LF_FACESIZE);

        // 复制其他输出字段
        p->iPointSize = chooseA.iPointSize;
        p->rgbColors = chooseA.rgbColors;
        p->nFontType = chooseA.nFontType;
    }

    return result;
}