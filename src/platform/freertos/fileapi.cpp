/*
 * fileapi.cpp -- 文件/目录查询（KERNEL32 文件族）
 *
 * 裸机没有文件系统：目录枚举恒不命中，属性查询恒 INVALID_FILE_ATTRIBUTES，
 * CRT 的 _open/_stat 返回失败。这不是占位桩而是正确行为 —— SOUI 的文件类
 * ResProvider 会干净地失败，资源由内存里的 PE blob 提供（见 main/app_main.cpp）。
 *
 * 对应 posix 侧 src/fileapi.cpp 的同名入口。改这里之前先确认调用方：任何新增的
 * "假装成功" 都会让上层走到一条永远读不到数据的路径上，比直接失败更难查。
 */
#include <sfr_config.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

extern "C" {

// ---- file enumeration / attributes (Linux: fileapi.cpp) --------------------
// No filesystem on bare metal: enumeration never finds anything.  This is
// the correct behaviour -- SOUI's file-based resource paths fail cleanly and
// the memory ResProvider serves the assets instead.
HANDLE FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA fd)
{
    (void)name;
    (void)fd;
    return INVALID_HANDLE_VALUE;
}

BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd)
{
    (void)h;
    (void)fd;
    return FALSE;
}

BOOL FindClose(HANDLE h)
{
    (void)h;
    return FALSE;
}

DWORD GetFileAttributesA(LPCSTR name)
{
    (void)name;
    return INVALID_FILE_ATTRIBUTES;
}

DWORD GetFullPathNameA(LPCSTR fn, DWORD len, LPSTR buf, LPSTR *part)
{
    (void)fn;
    (void)part;
    if (buf && len)
        buf[0] = 0;
    return 0;
}

// _wfopen 已下沉到 src/platform/common/sysapi.cpp（wchar_t 路径/模式转 UTF-8 是
// 纯 CRT 事情，两份实现逐字相同）。裸机上 fopen 仍然永远失败，但那是 libc 的事。

// MSVC CRT file names referenced by SOUI helpers
int _open(const char *path, int oflag, ...)
{
    (void)path;
    (void)oflag;
    return -1;
}

int _unlink(const char *path)
{
    (void)path;
    return -1;
}

int _link(const char *oldp, const char *newp)
{
    (void)oldp;
    (void)newp;
    return -1;
}

#if !SFR_HOSTED
// host: MinGW's <sys/stat.h> maps _stat to _stat64i32 with a different
// struct type; let the CRT version serve.
int _stat(const char *path, struct stat *buf)
{
    (void)path;
    (void)buf;
    return -1;
}
#endif

} /* extern "C" */
