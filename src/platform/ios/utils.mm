// iOS tool
//
//////////////////////////////////////////////////////////////////////

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <windows.h>
#include <shlobj.h>
#include "SwinxUtils.h"
#include <string.h>

// 返回值约定与 Win32 GetTempPathA 一致：
// 成功为写入 lpBuffer 的字节数（含结尾 '\0"），缓冲区不足或失败返回 0
DWORD swinx_iOSTempPathA(DWORD nBufferLength, LPSTR lpBuffer)
{
    @autoreleasepool {
        NSString *tmp = NSTemporaryDirectory();
        if (!tmp)
            return 0;
        const char *utf8 = [tmp UTF8String];
        if (!utf8 || *utf8 == 0)
            return 0;
        DWORD nLen = (DWORD)strlen(utf8) + 1; // 含结尾 '\0"
        if (nBufferLength < nLen)
            return 0;
        memcpy(lpBuffer, utf8, nLen);
        return nLen;
    }
}

// 将 CSIDL_* 常量映射到 iOS 沙盒目录。返回 nil 表示无对应目录（调用方回退到 home）。
// 关键映射：CSIDL_LOCAL_APPDATA -> NSCachesDirectory（应用缓存目录，可被系统清理）
static NSString *swinx_iOSDirectoryForCSIDL(int nFolder)
{
    NSSearchPathDirectory dir = NSApplicationDirectory;
    switch (nFolder) {
        case CSIDL_LOCAL_APPDATA:   // 应用缓存目录
            dir = NSCachesDirectory;
            break;
        case CSIDL_APPDATA:         // 应用数据目录
            dir = NSApplicationSupportDirectory;
            break;
        case CSIDL_MYDOCUMENTS://CSIDL_PERSONAL,文档目录
            dir = NSDocumentDirectory;
            break;
        case CSIDL_MYPICTURES:      // 图片目录
            dir = NSPicturesDirectory;
            break;
        case CSIDL_MYMUSIC:         // 音乐目录
            dir = NSMusicDirectory;
            break;
        case CSIDL_MYVIDEO:         // 视频目录
            dir = NSMoviesDirectory;
            break;
        default:
            return nil;
    }
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(dir, NSUserDomainMask, YES);
    return (paths && paths.count > 0) ? paths[0] : nil;
}

// 返回值约定与 Win32 SHGetSpecialFolderPathA 一致：
// 成功返回 TRUE 并将含结尾 '\0" 的路径写入 lpszPath，失败返回 FALSE
BOOL swinx_iOSSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate)
{
    if (!lpszPath)
        return FALSE;
    @autoreleasepool {
        NSString *path = swinx_iOSDirectoryForCSIDL(nFolder);
        if (!path) {
            // 回退到 home 目录（CSIDL_PROFILE 及其它无对应项）
            if (nFolder == CSIDL_PROFILE)
                path = NSHomeDirectory();
            else
                return FALSE;
        }
        const char *utf8 = [path UTF8String];
        if (!utf8 || *utf8 == 0)
            return FALSE;
        size_t nLen = strlen(utf8) + 1; // 含结尾 '\0"
        if (nLen > MAX_PATH)
            return FALSE;
        memcpy(lpszPath, utf8, nLen);

        // fCreate 为真时创建目录（iOS 沙盒目录通常已存在，这里做幂等处理）
        if (fCreate) {
            NSFileManager *fm = [NSFileManager defaultManager];
            if (![fm fileExistsAtPath:path]) {
                NSError *err = nil;
                [fm createDirectoryAtPath:path
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&err];
                if (err)
                    return FALSE;
            }
        }
        return TRUE;
    }
}

// Windows MessageBeep 在 iOS 上的实现（契约见 SwinxUtils.h）。
//
// iOS 没有 AppKit（因此没有 NSBeep），也没有公开的"用户首选提示音"接口
// （kSystemSoundID_UserPreferredAlert 仅 macOS 可用），所以只能使用系统内置提示音。
// 用 AudioServicesPlayAlertSound 而不是 AudioServicesPlaySystemSound：后者只发声，
// 前者在设备处于静音模式时会转为振动提示，更接近 Windows"提示音"的语义。
// iOS 没有与 MB_ICON* 一一对应的提示音集合，因此所有 uType 统一播放同一个提示音。
static const SystemSoundID kSwinxAlertTone = 1005; // 系统内置提示音（AlertTone）

int swinx_messageBeep(UINT uType)
{
    (void)uType;
    AudioServicesPlayAlertSound(kSwinxAlertTone);
    return TRUE;
}
