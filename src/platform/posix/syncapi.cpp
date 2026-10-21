#include <windows.h>
#include <mutex>
#include <thread>
#include <cstring>
#include <pthread.h>
#include <assert.h>

VOID WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    lpCriticalSection->pMutex = new std::recursive_mutex;
}

VOID WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    assert(lpCriticalSection->pMutex);
    ((std::recursive_mutex *)lpCriticalSection->pMutex)->lock();
}

VOID WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    assert(lpCriticalSection->pMutex);
    ((std::recursive_mutex *)lpCriticalSection->pMutex)->unlock();
}

BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    assert(lpCriticalSection->pMutex);
    return ((std::recursive_mutex *)lpCriticalSection->pMutex)->try_lock();
}

VOID WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    delete (std::recursive_mutex *)lpCriticalSection->pMutex;
    lpCriticalSection->pMutex = NULL;
}

//================================================

VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock)
{
    SRWLock->Ptr = new pthread_rwlock_t;
    ::pthread_rwlock_init((pthread_rwlock_t *)SRWLock->Ptr, NULL);
}

VOID WINAPI UninitializeSRWLock(PSRWLOCK SRWLock)
{
    assert(SRWLock->Ptr);
    ::pthread_rwlock_destroy((pthread_rwlock_t *)SRWLock->Ptr);
    delete (pthread_rwlock_t *)SRWLock->Ptr;
}

VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_wrlock((pthread_rwlock_t *)SRWLock->Ptr);
}

VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_unlock((pthread_rwlock_t *)SRWLock->Ptr);
}

VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_rdlock((pthread_rwlock_t *)SRWLock->Ptr);
}

VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_unlock((pthread_rwlock_t *)SRWLock->Ptr);
}

BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    // pthread_rwlock_trywrlock returns EBUSY when the lock is held;
    // it never blocks, so no deadlock risk on re-entrant misuse.
    return ::pthread_rwlock_trywrlock((pthread_rwlock_t *)SRWLock->Ptr) == 0 ? TRUE : FALSE;
}

BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock)
{
    return ::pthread_rwlock_tryrdlock((pthread_rwlock_t *)SRWLock->Ptr) == 0 ? TRUE : FALSE;
}

//================================================
// One-time initialization (INIT_ONCE)
//
// The run-once state lives in the low 2 bits of RTL_RUN_ONCE::State, while the
// caller supplied context is kept in the high bits (RTL_RUN_ONCE::Context).
// State transitions are serialized with a process-wide mutex; threads that
// lose the race spin with a yield until the owner publishes the result. When
// an initialization fails, the state is reset to INIT so a later caller (or a
// waiting thread) can take over and retry it.
namespace
{
enum RTLRunOnceState
{
    RTL_RUN_ONCE_STATE_INIT = 0,        // not yet initialized
    RTL_RUN_ONCE_STATE_IN_PROGRESS = 2, // a thread is running the init
    RTL_RUN_ONCE_STATE_COMPLETE = 3,    // init has completed
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
    {
        std::memset(InitOnce, 0, sizeof(*InitOnce));
    }
}

BOOL WINAPI InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN InitFn, PVOID Parameter, LPVOID *Context)
{
    if (!InitOnce || !InitFn)
    {
        return FALSE;
    }

    std::mutex &mtx = GetInitOnceMutex();

    for (;;)
    {
        BOOL bWinner = FALSE;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (InitOnce->State == RTL_RUN_ONCE_STATE_COMPLETE)
            {
                if (Context)
                {
                    *Context = InitOnce->Context;
                }
                return TRUE;
            }
            if (InitOnce->State == RTL_RUN_ONCE_STATE_INIT)
            {
                // Claim ownership of the initialization.
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
                    // Reset so that a later caller retries the initialization.
                    InitOnce->State = RTL_RUN_ONCE_STATE_INIT;
                }
            }
            return bRet ? TRUE : FALSE;
        }

        // Loser: wait and re-check (picks up the work if the owner failed).
        std::this_thread::yield();
    }
}

BOOL WINAPI InitOnceBeginInitialize(LPINIT_ONCE lpInitOnce, DWORD dwFlags, PBOOL fPending, LPVOID *lpContext)
{
    if (!lpInitOnce || !fPending)
    {
        return FALSE;
    }

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
                {
                    *lpContext = (void *)lpInitOnce->Context;
                }
                *fPending = FALSE;
                return TRUE;
            }
            if (lpInitOnce->State == RTL_RUN_ONCE_STATE_INIT)
            {
                // Claim ownership of the initialization.
                lpInitOnce->State = RTL_RUN_ONCE_STATE_IN_PROGRESS;
                bClaimed = TRUE;
            }
        }

        if (bClaimed || bAsync)
        {
            // Async convention: caller performs its own (possibly redundant)
            // work and publishes the result via InitOnceComplete.
            *fPending = TRUE;
            return TRUE;
        }

        // Blocking mode: a peer owns the init; wait for it to finish or fail.
        std::this_thread::yield();
    }
}

BOOL WINAPI InitOnceComplete(LPINIT_ONCE lpInitOnce, DWORD dwFlags, LPVOID lpContext)
{
    if (!lpInitOnce)
    {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(GetInitOnceMutex());
    if (dwFlags & INIT_ONCE_INIT_FAILED)
    {
        // Mark this attempt as failed and let another thread retry.
        lpInitOnce->State = RTL_RUN_ONCE_STATE_INIT;
    }
    else
    {
        lpInitOnce->State = RTL_RUN_ONCE_STATE_COMPLETE;
        lpInitOnce->Context = lpContext;
    }
    return TRUE;
}