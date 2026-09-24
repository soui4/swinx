#ifndef SWINX_FREERTOS_SYNC_H_
#define SWINX_FREERTOS_SYNC_H_
/*
 * swinx/platform/freertos/freertos_sync.h
 *
 * FreeRTOS-port declarations of the swinx Win32 sync-compat layer
 * (CRITICAL_SECTION / SRWLOCK / INIT_ONCE). These mirror the prototypes in
 * include/sysapi.h, but this header is includable on a bare-metal FreeRTOS
 * toolchain without pulling in <dlfcn.h>/<unistd.h>/<strapi.h> OR the full
 * swinx/include/ctypes.h (which carries a few duplicate typedefs that are only
 * an error under C++). The minimal Win32 base types are declared here, each
 * guarded so they never clash if ctypes.h is also pulled in later (e.g. when
 * SOUI itself is compiled against this port).
 *
 * The matching implementation is src/platform/freertos/syncapi.cpp, compiled
 * into the swinx freeRTOS platform build INSTEAD OF src/syncapi.cpp (the
 * Linux/Win32 one, which relies on pthread_rwlock_t + the mega <windows.h>).
 * This is the "bridge in swinx" the FreeRTOS port needs.
 */
#include <cstdint>

// ---- minimal Win32 base types (guarded against ctypes.h) -----------------
#ifndef WINAPI
#define WINAPI
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef VOID
#define VOID void
#endif
#ifndef BOOL
typedef int BOOL;
#endif
typedef unsigned char BOOLEAN;
#ifndef LONG
typedef int32_t LONG;
#endif
#ifndef DWORD
typedef uint32_t DWORD;
#endif
typedef void *PVOID;
typedef void *LPVOID;
#ifndef PBOOL
typedef BOOL *PBOOL;
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct RTL_CRITICAL_SECTION
    {
        void *pMutex;
    } RTL_CRITICAL_SECTION;

    typedef RTL_CRITICAL_SECTION CRITICAL_SECTION;
    typedef CRITICAL_SECTION *PCRITICAL_SECTION;
    typedef CRITICAL_SECTION *LPCRITICAL_SECTION;

    VOID WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
    VOID WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
    VOID WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
    BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection);
    VOID WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection);

    typedef struct _RTL_SRWLOCK
    {
        PVOID Ptr;
    } RTL_SRWLOCK, *PRTL_SRWLOCK;

    typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;

    VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock);
    VOID WINAPI UninitializeSRWLock(PSRWLOCK SRWLock);
    VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock);
    VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
    VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock);
    VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock);
    BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock);
    BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock);

    // ---- One-time initialization (INIT_ONCE) ----
    typedef struct _RTL_RUN_ONCE
    {
        int State;
        LPVOID Context;
    } RTL_RUN_ONCE, *PRTL_RUN_ONCE;

    typedef RTL_RUN_ONCE INIT_ONCE;
    typedef PRTL_RUN_ONCE PINIT_ONCE;
    typedef PRTL_RUN_ONCE LPINIT_ONCE;

#define INIT_ONCE_STATIC_INIT \
    {                         \
        0, 0                  \
    }

#define RTL_RUN_ONCE_CHECK_ONLY  0x00000001
#define RTL_RUN_ONCE_ASYNC       0x00000002
#define RTL_RUN_ONCE_INIT_FAILED 0x00000004

#define INIT_ONCE_CHECK_ONLY  RTL_RUN_ONCE_CHECK_ONLY
#define INIT_ONCE_ASYNC       RTL_RUN_ONCE_ASYNC
#define INIT_ONCE_INIT_FAILED RTL_RUN_ONCE_INIT_FAILED

    typedef BOOL(WINAPI *PINIT_ONCE_FN)(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context);

    VOID WINAPI InitOnceInitialize(PINIT_ONCE InitOnce);
    BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context);
    BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE lpInitOnce, DWORD dwFlags, PBOOL fPending, LPVOID *lpContext);
    BOOL WINAPI InitOnceComplete(LPINIT_ONCE lpInitOnce, DWORD dwFlags, LPVOID lpContext);

#ifdef __cplusplus
}
#endif
#endif // SWINX_FREERTOS_SYNC_H_
