#ifndef SWINX_FREERTOS_WINOBJS_H_
#define SWINX_FREERTOS_WINOBJS_H_
/*
 * swinx/platform/freertos/winobjs.h
 *
 * FreeRTOS-port declarations of the swinx Win32 *sync-object* + thread compat
 * layer: events, mutexes, semaphores, WaitForSingleObject /
 * WaitForMultipleObjects, CreateThread / SuspendThread / ResumeThread, Sleep,
 * GetTickCount, GetCurrentThreadId and the Interlocked family.
 *
 * These mirror the prototypes used by demos/fun_test/test_sync.cpp and
 * test_thread.cpp. This header is includable on a bare-metal FreeRTOS
 * toolchain WITHOUT pulling in <windows.h> / <dlfcn.h> / <pthread.h>: it only
 * reuses the minimal Win32 base types from freertos_sync.h (each guarded) and
 * adds the handle/thread types itself. The matching implementation lives in
 * src/platform/freertos/winobjs.cpp, compiled into the swinx FreeRTOS build
 * INSTEAD OF src/sysobjs.cpp + the process-heavy parts of src/sysapi.cpp
 * (pipe-based mutexes, fork, SIGCHLD self-pipe, dlopen... none of which exist
 * on a bare-metal MCU). This is the "bridge in swinx" for this API surface.
 */
#include "freertos_sync.h"   // DWORD, LONG, BOOL, LPVOID, VOID, WINAPI, TRUE/FALSE
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

    typedef void *HANDLE;
    typedef HANDLE *PHANDLE;
    typedef HANDLE *LPHANDLE;
    typedef unsigned int UINT;
    typedef unsigned long ULONG;
    typedef uintptr_t SIZE_T;
    typedef DWORD tid_t;            // matches sysapi.h's tid_t on non-Windows

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0
#endif
#define WAIT_TIMEOUT   258
#define WAIT_FAILED    0xFFFFFFFFu
#ifndef INFINITE
#define INFINITE       0xFFFFFFFFu
#endif

    // Access flags for OpenXxx -- ignored on FreeRTOS (no real ACLs).
#define EVENT_MODIFY_STATE      0x0002
#define SYNCHRONIZE             0x00100000
#define SEMAPHORE_MODIFY_STATE  0x0002
#define MUTEX_ALL_ACCESS        0x001F0000
#define CREATE_SUSPENDED        0x00000004

    typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);

    // ---- Events ----------------------------------------------------------
    HANDLE WINAPI CreateEventA(void *lpEventAttributes, BOOL bManualReset, BOOL bInitialState, const char *lpName);
    HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, const char *lpName);
    BOOL WINAPI SetEvent(HANDLE h);
    BOOL WINAPI ResetEvent(HANDLE h);

    // ---- Mutex ------------------------------------------------------------
    HANDLE WINAPI CreateMutexA(void *lpMutexAttributes, BOOL bInitialOwner, const char *lpName);
    HANDLE WINAPI OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle, const char *lpName);
    BOOL WINAPI ReleaseMutex(HANDLE h);

    // ---- Semaphore --------------------------------------------------------
    HANDLE WINAPI CreateSemaphoreA(void *lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, const char *lpName);
    HANDLE WINAPI OpenSemaphoreA(DWORD dwDesiredAccess, BOOL bInheritHandle, const char *lpName);
    BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG lReleaseCount, LONG *lpPreviousCount);

    // ---- Handle / wait ---------------------------------------------------
    BOOL WINAPI CloseHandle(HANDLE h);
    DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD dwMilliseconds);
    DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);

    // ---- Threads ---------------------------------------------------------
    HANDLE WINAPI CreateThread(void *lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                               LPVOID lpParameter, DWORD dwCreationFlags, tid_t *lpThreadId);
    DWORD WINAPI SuspendThread(HANDLE h);
    DWORD WINAPI ResumeThread(HANDLE h);

    // ---- Misc ------------------------------------------------------------
    VOID WINAPI Sleep(DWORD dwMilliseconds);
    DWORD WINAPI GetTickCount(VOID);
    tid_t WINAPI GetCurrentThreadId(VOID);

    // ---- Interlocked -----------------------------------------------------
    LONG WINAPI InterlockedIncrement(LONG volatile *v);
    LONG WINAPI InterlockedDecrement(LONG volatile *v);
    LONG WINAPI InterlockedCompareExchange(LONG volatile *v, LONG Exchange, LONG Comparand);
    LONG WINAPI InterlockedExchangeAdd(LONG volatile *v, LONG Increment);
    int64_t WINAPI InterlockedIncrement64(int64_t volatile *v);
    int64_t WINAPI InterlockedDecrement64(int64_t volatile *v);

#ifdef __cplusplus
}
#endif
#endif // SWINX_FREERTOS_WINOBJS_H_
