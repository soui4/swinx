#ifndef __SWINX_ENTRY_H__
#define __SWINX_ENTRY_H__

#include <windows.h>
#include <tchar.h>

// 应用层 iOS 入口函数原型（与 Win32 WinMain 完全一致）
typedef int (*funIosMain)(HINSTANCE hInstance,
                          HINSTANCE hPrevInstance,
                          LPTSTR    lpstrCmdLine,
                          int       nCmdShow);

#ifdef __cplusplus
extern "C" {
#endif

int swinx_ios_entry(int argc, char *argv[], funIosMain iosMain);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // __SWINX_ENTRY_H__
