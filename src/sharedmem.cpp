#include "sharedmem.h"
#include <sys/mman.h>
#include <windows.h>
#include "log.h"
#define kLogTag "sharememory"
namespace swinx
{
SharedMemory::~SharedMemory()
{
    if (shmid == -1)
        return;
    m_rwlock->lockExclusive();
    bool bUnlink = 0 == (--nRef);
    m_rwlock->unlockExclusive();
    munmap(&nRef, m_dwSize + sizeof(uint32_t));
    close(shmid);
    delete m_rwlock;
    SLOG_FMTD("close share memory, name=%s, bUnlink=%d", m_name.c_str(), bUnlink);

    if (bUnlink && !m_bDetached)
    {
        shm_unlink(m_name.c_str());
        sem_unlink(m_name.c_str());
    }
}

SharedMemory::InitStat SharedMemory::init(const char *name, uint32_t size)
{
    assert(m_rwlock == nullptr);
    TNamedSemRwLock<kSharedNumber> *rwlock = new TNamedSemRwLock<kSharedNumber>();
    if (!rwlock->init(name))
    {
        delete rwlock;
        return Failed;
    }
    m_rwlock = rwlock;
    InitStat ret = Failed;
    int fd = shm_open(name, O_RDWR, 0666); // open share memory
    if (fd == -1)
    {
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666); // 创建共享内存对象
        if (fd == -1)
        {
            perror("shm_open with O_CREAT");
            return Failed;
        }
        if (ftruncate(fd, size + sizeof(uint32_t)) == -1)
        {
            close(fd);
            perror("ftruncate");
            return Failed;
        }
        ret = Created;
    }
    else
    {
        ret = Existed;
    }
    // 将共享内存对象映射到进程的地址空间中
    LPBYTE ptr = (LPBYTE)mmap(0, size + sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        close(fd);
        perror("mmap");
        return Failed;
    }
    nRef = *(uint32_t *)ptr;
    m_rwlock->lockExclusive();
    if (ret == Created)
    {
        nRef = 1;
    }
    else
    {
        nRef++;
    }
    m_rwlock->unlockExclusive();
    m_pBuf = ptr + sizeof(uint32_t);

    shmid = fd;
    m_dwSize = size;
    m_name = name;
    m_bDetached = false;
    SLOG_FMTD("open share memory, name=%s, ret=%d\n", name, ret);
    return ret;
}

void SharedMemory::detach()
{
    m_bDetached = true;
}

} // namespace swinx