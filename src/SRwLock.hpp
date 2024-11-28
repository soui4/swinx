#ifndef _SYNC_SRW_LOCK_
#define _SYNC_SRW_LOCK_
#include <windows.h>

namespace swinx
{
class SRwLock {
  public:
    SRwLock()
    {
        InitializeSRWLock(&m_rwlock);
    }
    ~SRwLock()
    {
        UninitializeSRWLock(&m_rwlock);
    }

    void LockExclusive()
    {
        AcquireSRWLockExclusive(&m_rwlock);
    }

    void UnlockExclusive()
    {
        ReleaseSRWLockExclusive(&m_rwlock);
    }

    void LockShared()
    {
        AcquireSRWLockShared(&m_rwlock);
    }

    void UnlockShared()
    {
        ReleaseSRWLockShared(&m_rwlock);
    }

  protected:
    SRWLOCK m_rwlock;
};

class SAutoWriteLock {
  public:
    SAutoWriteLock(SRwLock &lock)
        : m_lock(lock)
    {
        m_lock.LockExclusive();
    }
    ~SAutoWriteLock()
    {
        m_lock.UnlockExclusive();
    }

  private:
    SRwLock &m_lock;
};

class SAutoReadLock {
  public:
    SAutoReadLock(SRwLock &lock)
        : m_lock(lock)
    {
        m_lock.LockShared();
    }
    ~SAutoReadLock()
    {
        m_lock.UnlockShared();
    }

  private:
    SRwLock &m_lock;
};
} // namespace swinx

#endif //_SYNC_SRW_LOCK_