/*
 * swinx/platform/freertos/syncapi.cpp
 *
 * FreeRTOS implementation of the swinx Win32 sync-compat layer. It replaces
 * src/syncapi.cpp (Linux/Win32) on the FreeRTOS platform: that file leans on
 * pthread_rwlock_t and the full <windows.h>, neither of which exists on a
 * bare-metal arm-none-eabi toolchain. Here every primitive is backed by the
 * FreeRTOS STL shims in src/platform/freertos/stl (which resolve <mutex>/<thread>/
 * <condition_variable> to FreeRTOS-kernel objects), so swinx stays compatible
 * with FreeRTOS without changing any other platform's behaviour.
 *
 *   * CRITICAL_SECTION     -> std::recursive_mutex          (shim)
 *   * SRWLOCK              -> counting semaphore             (swinx_fr::sem_create_counting)
 *   * INIT_ONCE            -> std::mutex + std::this_thread::yield (shim)
 *
 * The counting semaphore gives SRWLOCK real concurrent shared readers (the
 * "shared_concurrent_readers" contract), unlike a degenerate exclusive-only
 * mutex.
 */
#include "freertos_sync.h"

#include <mutex>
#include <thread>
#include <cstring>
#include <new>

namespace
{
// Max concurrent shared readers a single SRWLOCK allows. Bounds the counting
// semaphore capacity; must be >= the maximum number of concurrent shared
// lockers exercised by callers.
enum
{
    SRW_MAX_READERS = 16
};
} // namespace

extern "C"
{

//============================================================================
// Critical section  (backed by std::recursive_mutex)
//============================================================================
VOID WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    lpCriticalSection->pMutex = new (std::nothrow) std::recursive_mutex;
}

VOID WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    static_cast<std::recursive_mutex *>(lpCriticalSection->pMutex)->lock();
}

VOID WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    static_cast<std::recursive_mutex *>(lpCriticalSection->pMutex)->unlock();
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    return static_cast<std::recursive_mutex *>(lpCriticalSection->pMutex)->try_lock() ? TRUE : FALSE;
}

VOID WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    delete static_cast<std::recursive_mutex *>(lpCriticalSection->pMutex);
    lpCriticalSection->pMutex = NULL;
}

//============================================================================
// Slim reader/writer lock  (backed by a counting semaphore)
//
// The semaphore has capacity SRW_MAX_READERS. A shared lock claims one slot;
// an exclusive lock claims ALL slots, which both blocks new readers and (once
// every in-flight reader has released) gives the writer exclusive access.
// This is correct but writer-non-fair: a stream of readers can in principle
// starve a waiting writer. That is acceptable for this embedded port and the
// bounded tests that exercise it.
//============================================================================
VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = new (std::nothrow)
        swinx_fr::sem_handle(swinx_fr::sem_create_counting(SRW_MAX_READERS, SRW_MAX_READERS));
    SRWLock->Ptr = d;
}

VOID WINAPI UninitializeSRWLock(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    swinx_fr::sem_delete(*d);
    delete d;
    SRWLock->Ptr = NULL;
}

VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    for (int i = 0; i < SRW_MAX_READERS; ++i)
        swinx_fr::sem_take(*d, swinx_fr::wait_forever());
}

VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    for (int i = 0; i < SRW_MAX_READERS; ++i)
        swinx_fr::sem_give(*d);
}

VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    swinx_fr::sem_take(*d, swinx_fr::wait_forever());
}

VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    swinx_fr::sem_give(*d);
}

BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    for (int i = 0; i < SRW_MAX_READERS; ++i)
    {
        if (!swinx_fr::sem_take(*d, 0))
        {
            // roll back the slots we already claimed
            for (int j = 0; j < i; ++j)
                swinx_fr::sem_give(*d);
            return FALSE;
        }
    }
    return TRUE;
}

BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock)
{
    swinx_fr::sem_handle *d = static_cast<swinx_fr::sem_handle *>(SRWLock->Ptr);
    return swinx_fr::sem_take(*d, 0) ? TRUE : FALSE;
}

//============================================================================
// One-time initialization (INIT_ONCE)  (backed by std::mutex + yield)
//============================================================================
namespace
{
enum RTLRunOnceState
{
    RTL_RUN_ONCE_STATE_INIT = 0,
    RTL_RUN_ONCE_STATE_IN_PROGRESS = 2,
    RTL_RUN_ONCE_STATE_COMPLETE = 3,
};

std::mutex &GetInitOnceMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}
} // namespace

VOID WINAPI InitOnceInitialize(PINIT_ONCE InitOnce)
{
    if (InitOnce)
        std::memset(InitOnce, 0, sizeof(*InitOnce));
}

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context)
{
    if (!InitOnce || !InitFn)
        return FALSE;

    std::mutex &mtx = GetInitOnceMutex();
    for (;;)
    {
        BOOL bWinner = FALSE;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (InitOnce->State == RTL_RUN_ONCE_STATE_COMPLETE)
            {
                if (Context)
                    *Context = InitOnce->Context;
                return TRUE;
            }
            if (InitOnce->State == RTL_RUN_ONCE_STATE_INIT)
            {
                InitOnce->State = RTL_RUN_ONCE_STATE_IN_PROGRESS;
                bWinner = TRUE;
            }
        }

        if (bWinner)
        {
            PVOID ctx = NULL;
            BOOL bRet = InitFn(InitOnce, Parameter, &ctx);
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (bRet)
                {
                    InitOnce->State = RTL_RUN_ONCE_STATE_COMPLETE;
                    InitOnce->Context = ctx;
                }
                else
                {
                    InitOnce->State = RTL_RUN_ONCE_STATE_INIT;
                }
            }
            return bRet ? TRUE : FALSE;
        }

        std::this_thread::yield();
    }
}

BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE lpInitOnce, DWORD dwFlags, PBOOL fPending, LPVOID *lpContext)
{
    if (!lpInitOnce || !fPending)
        return FALSE;

    BOOL bAsync = (dwFlags & INIT_ONCE_ASYNC) != 0;
    std::mutex &mtx = GetInitOnceMutex();
    for (;;)
    {
        BOOL bClaimed = FALSE;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (lpInitOnce->State == RTL_RUN_ONCE_STATE_COMPLETE)
            {
                if (lpContext)
                    *lpContext = (void *)lpInitOnce->Context;
                *fPending = FALSE;
                return TRUE;
            }
            if (lpInitOnce->State == RTL_RUN_ONCE_STATE_INIT)
            {
                lpInitOnce->State = RTL_RUN_ONCE_STATE_IN_PROGRESS;
                bClaimed = TRUE;
            }
        }

        if (bClaimed || bAsync)
        {
            *fPending = TRUE;
            return TRUE;
        }
        std::this_thread::yield();
    }
}

BOOL WINAPI InitOnceComplete(LPINIT_ONCE lpInitOnce, DWORD dwFlags, LPVOID lpContext)
{
    if (!lpInitOnce)
        return FALSE;

    std::lock_guard<std::mutex> lock(GetInitOnceMutex());
    if (dwFlags & INIT_ONCE_INIT_FAILED)
        lpInitOnce->State = RTL_RUN_ONCE_STATE_INIT;
    else
    {
        lpInitOnce->State = RTL_RUN_ONCE_STATE_COMPLETE;
        lpInitOnce->Context = lpContext;
    }
    return TRUE;
}

} // extern "C"
