#ifndef _SHARED_MEM_H_
#define _SHARED_MEM_H_
#include <ctypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <assert.h>
#include <semaphore.h>
#include <string>
#include <errno.h>
#if defined(__ANDROID__)
#include <sys/file.h>
#include <unistd.h>
#include <string.h>
#endif
namespace swinx{
struct ISemRwLock
{
    virtual ~ISemRwLock()
    {
    }
    virtual void lockShared() = 0;
    virtual void unlockShared() = 0;
    virtual void lockExclusive() = 0;
    virtual void unlockExclusive() = 0;
};

// 读写锁的定义
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

template <int kNumLock = 2>
class TSemRwLock : public ISemRwLock {
  private:
    int semid;
    uint32_t key;

  public:
    TSemRwLock()
        : semid(-1)
        , key(-1)
    {
    }
    ~TSemRwLock()
    {
        if (semid != -1)
        {
            semctl(semid, 0, IPC_RMID, NULL);
        }
    }

    uint32_t getKey() const
    {
        return key;
    }

    bool init(uint32_t _key)
    {
        // 创建读写锁
        semid = semget(_key, kNumLock, IPC_CREAT | IPC_EXCL | 0666);
        if (semid == -1)
        {
            if (errno == EEXIST)
            {
                semid = semget(_key, kNumLock, 0666);
            }
            if (semid == -1)
            {
                perror("semget");
                return false;
            }
        }

        bool bRet = true;
        // 初始化读写锁
        union semun arg;
        arg.val = 1;
        for (int i = 0; i < kNumLock && bRet; i++)
        {
            bRet = semctl(semid, i, SETVAL, arg) != -1;
        }
        if (!bRet)
        {
            perror("semctl");
            semctl(semid, 0, IPC_RMID, NULL);
            semid = -1;
        }
        else
        {
            key = _key;
        }
        return bRet;
    }

    void lockShared() override
    {
        struct sembuf sb = { 0, -1, 0 };
        semop(semid, &sb, 1);
    }

    void unlockShared() override
    {
        struct sembuf sb = { 0, 1, 0 };
        semop(semid, &sb, 1);
    }

    void lockExclusive() override
    {
        struct sembuf sb[kNumLock];
        for (int i = 0; i < kNumLock; i++)
        {
            sb[i].sem_num = i;
            sb[i].sem_op = -1;
            sb[i].sem_flg = 0;
        }
        semop(semid, sb, kNumLock);
    }

    void unlockExclusive() override
    {
        struct sembuf sb[kNumLock];
        for (int i = 0; i < kNumLock; i++)
        {
            sb[i].sem_num = i;
            sb[i].sem_op = 1;
            sb[i].sem_flg = 0;
        }
        semop(semid, sb, kNumLock);
    }
};

#ifdef __ANDROID__
template <int kNumLock = 2>
class TNamedSemRwLock : public ISemRwLock {
  private:
    int m_fd;
    std::string m_lockPath;

  private:
    void setFileLock(short type)
    {
        struct flock lock;
        memset(&lock, 0, sizeof(lock));
        lock.l_type = type;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        if (fcntl(m_fd, F_SETLKW, &lock) == -1)
        {
            perror("fcntl(F_SETLKW)");
        }
    }

  public:
    TNamedSemRwLock()
        : m_fd(-1)
    {
    }
    ~TNamedSemRwLock()
    {
        if (m_fd != -1)
        {
            close(m_fd);
        }
    }

    bool init(const char *name)
    {
        assert(m_fd == -1);
        m_lockPath = "/data/local/tmp/soui_lock_";
        m_lockPath += (name != nullptr ? name : "default");
        m_lockPath += ".lock";
        m_fd = open(m_lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd == -1)
        {
            int err = errno;
            printf("open lock file failed, path=%s, errno=%d\n", m_lockPath.c_str(), err);
            return false;
        }
        return true;
    }

    void lockShared() override
    {
        setFileLock(F_RDLCK);
    }

    void unlockShared() override
    {
        setFileLock(F_UNLCK);
    }

    void lockExclusive() override
    {
        setFileLock(F_WRLCK);
    }

    void unlockExclusive() override
    {
        setFileLock(F_UNLCK);
    }
};
#else

template <int kNumLock = 2>
class TNamedSemRwLock : public ISemRwLock {
  private:
    sem_t *semid;

  public:
    TNamedSemRwLock()
        : semid(SEM_FAILED)
    {
    }
    ~TNamedSemRwLock()
    {
        if (semid != SEM_FAILED)
        {
            sem_close(semid);
        }
    }

    bool init(const char *name)
    {
        assert(semid == SEM_FAILED);
        semid = sem_open(name, O_CREAT | O_EXCL, 0666, kNumLock);
        if (semid == SEM_FAILED)
        {
            if (errno == EEXIST)
            {
                semid = sem_open(name, 0666);
            }
            if (semid == SEM_FAILED)
            {
                printf("sem_open failed, errno=%d\n", errno);
                return false;
            }
        }
        return true;
    }

    void lockShared() override
    {
        sem_wait(semid);
    }

    void unlockShared() override
    {
        sem_post(semid);
    }

    void lockExclusive() override
    {
        for (int i = 0; i < kNumLock; i++)
        {
            sem_wait(semid);
        }
    }

    void unlockExclusive() override
    {
        for (int i = 0; i < kNumLock; i++)
        {
            sem_post(semid);
        }
    }
};
#endif//__ANDROID__

class SharedMemory {
    enum
    {
        kSharedNumber = 5
    };

  public:
    enum InitStat
    {
        Failed = 0,
        Created,
        Existed,
    };

  private:
    LPBYTE m_pBuf;
    uint32_t m_dwSize;
    int shmid;
    std::string m_name;
    uint32_t &nRef;
    bool m_bDetached;
    ISemRwLock *m_rwlock;

  public:
    SharedMemory()
        : shmid(-1)
        , m_pBuf((LPBYTE)-1)
        , m_dwSize(0)
        , m_rwlock(nullptr)
        , nRef(m_dwSize) // init nRef to m_dwSize to avoid compile error. nRef will ref to buffer header later. hjx 2024/9/10
    {
    }
    ~SharedMemory();

    InitStat init(const char *name, uint32_t size);
    void detach();

  public:
    LPBYTE buffer() const
    {
        return m_pBuf;
    }
    uint32_t size() const
    {
        return m_dwSize;
    }

    ISemRwLock *getRwLock()
    {
        return m_rwlock;
    }
};
}//end of ns swinx
#endif //_SHARED_MEM_H_