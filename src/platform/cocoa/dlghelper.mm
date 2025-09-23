#import <Cocoa/Cocoa.h>
#include <objc/objc.h>
#include <commdlg.h>
#include "SNsWindow.h"
#include "sysapi.h"
#include "winuser.h"
#include "wnd.h"

BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out) {
    @autoreleasepool {
        // 创建颜色面板
        NSColorPanel *colorPanel = [NSColorPanel sharedColorPanel];
        colorPanel.showsAlpha = YES;

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

        // 监听关闭和颜色变更事件
        __block BOOL didStopModal = NO;
        __block id closeObserver = nil;
        __block id colorObserver = nil;
        __block NSColor *pickedColor = nil;

        colorObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSColorPanelColorDidChangeNotification object:colorPanel queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification * _Nonnull note) {
            pickedColor = [[colorPanel color] copy];
        }];
        closeObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification object:colorPanel queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification * _Nonnull note) {
            if (!didStopModal) {
                didStopModal = YES;
                if (pickedColor) {
                    [NSApp stopModalWithCode:NSModalResponseOK];
                } else {
                    [NSApp stopModalWithCode:NSModalResponseCancel];
                }
            }
        }];

        [colorPanel setIsVisible:YES];
        NSInteger modalResult = [NSApp runModalForWindow:colorPanel];
        [colorPanel orderOut:nil];

        [[NSNotificationCenter defaultCenter] removeObserver:closeObserver];
        [[NSNotificationCenter defaultCenter] removeObserver:colorObserver];

        if (modalResult == NSModalResponseOK && pickedColor) {
            NSColor *rgbColor = [pickedColor colorUsingColorSpace:[NSColorSpace genericRGBColorSpace]];
            if (!rgbColor) return FALSE;

            // 转换为COLORREF格式
            uint8_t r = (uint8_t)([rgbColor redComponent] * 255);
            uint8_t g = (uint8_t)([rgbColor greenComponent] * 255);
            uint8_t b = (uint8_t)([rgbColor blueComponent] * 255);
            uint32_t a = (uint8_t)([rgbColor alphaComponent] * 255);
            *out = RGBA(r, g, b, a);

            return TRUE;
        }
        return FALSE;
    }
}

static BOOL GetOpenFileNameMac(OPENFILENAMEA *lpofn) {
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

static BOOL GetSaveFileNameMac(OPENFILENAMEA *lpofn) {
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
    NSString *fontName = [font fontName];
    if (fontName) {
        strncpy(logFont->lfFaceName, [fontName UTF8String], LF_FACESIZE - 1);
        logFont->lfFaceName[LF_FACESIZE - 1] = '\0';
    }
}

// 简化的字体选择委托类
@interface FontPanelDelegate : NSObject
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
    NSFontManager *fontManager = [NSFontManager sharedFontManager];
    if (self.selectedFont) {
        self.selectedFont = [fontManager convertFont:self.selectedFont];
        self.fontChanged = YES;
    }
}

@end

static BOOL ChooseFontMac(LPCHOOSEFONTA p) {
    @autoreleasepool {
        if (!p || !p->lpLogFont) {
            return FALSE;
        }

        // 获取字体面板
        NSFontPanel *fontPanel = [NSFontPanel sharedFontPanel];
        FontPanelDelegate *delegate = [[FontPanelDelegate alloc] init];
        [fontPanel setEnabled:YES];
        // 设置初始字体
        NSFont *initialFont = LogFontToNSFont(p->lpLogFont);
        if (initialFont) {
            [[NSFontManager sharedFontManager] setSelectedFont:initialFont isMultiple:NO];
        }            
        // 创建委托对象
        delegate.selectedFont = initialFont;

        // 设置字体管理器的目标
        NSFontManager *fontManager = [NSFontManager sharedFontManager];
        [fontManager setTarget:delegate];
        [fontManager setAction:@selector(changeFont:)];

        // 设置父窗口（如果有）
        NSWindow *parentWindow = nil;
        if (p->hwndOwner) {
            int parentId = getNsWindowId(p->hwndOwner);
            parentWindow = [NSApp windowWithWindowNumber:parentId];
        }

        // 显示字体面板并等待用户操作
        [fontPanel makeKeyAndOrderFront:nil];

        // 使用简单的事件循环等待用户操作
        BOOL result = FALSE;
        while ([fontPanel isVisible]) {
            // 处理事件
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) {
                [NSApp sendEvent:event];
            }
        }
        if (delegate.selectedFont) {
            NSFontToLogFont(delegate.selectedFont, p->lpLogFont);
            // 设置点大小
            if (p->Flags & CF_INITTOLOGFONTSTRUCT) {
                p->iPointSize = (INT)([delegate.selectedFont pointSize] * 10);
            }
            result = TRUE;
        }

        // 清理
        [fontPanel orderOut:nil];
        // 恢复字体管理器的目标
        [fontManager setTarget:nil];

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