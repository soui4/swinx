#include <windows.h>
#include <mutex>
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