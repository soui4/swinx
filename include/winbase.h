#ifndef __WINBASE_H_
#define __WINBASE_H_

/*
 * winbase.h — the SDK's catch-all kernel32 header. swinx splits the same API
 * surface across sysapi.h (kernel32 misc), fileapi.h (file I/O) and process.h
 * (threads/processes); this header exists so source written against the SDK
 * (e.g. WebCore's platform/win/FileSystemWin.cpp, which includes <winbase.h>
 * directly) keeps compiling unchanged.
 */

#include <windows.h>
#include <sysapi.h>
#include <fileapi.h>
#include <process.h>

#endif // __WINBASE_H_
