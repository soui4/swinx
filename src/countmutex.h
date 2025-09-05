
#ifndef _COUNT_MUTEX_H_
#define _COUNT_MUTEX_H_
#include <ctypes.h>
#include <mutex>


class CountMutex : public std::recursive_mutex {
    LONG cLock;
  public:
    CountMutex()
        : cLock(0)
    {
    }
    virtual void lock()
    {
        std::recursive_mutex::lock();
        cLock++;
    }
    virtual void unlock()
    {
        cLock--;
        std::recursive_mutex::unlock();
    }

    LONG getLockCount() const
    {
        return cLock;
    }

    LONG FreeLock()
    {
        LONG ret = cLock;
        while (cLock > 0)
        {
            std::recursive_mutex::unlock();
            cLock--;
        }
        return ret;
    }

    void RestoreLock(LONG preLock)
    {
        while (cLock < preLock)
        {
            std::recursive_mutex::lock();
            cLock++;
        }
    }
};

#endif//_COUNTMUTEX_H