#include "sharedmem.h"
#include <sys/mman.h>
#include <windows.h>
#include <map>
#include "log.h"

#define kLogTag "sharememory"

#ifdef __ANDROID__
#include <android/sharedmem.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

// Android-specific shared memory implementation for SharedMemory class
// This is similar to the implementation in sysobjs.cpp but specific to SharedMemory

struct AndroidSharedMemEntry {
    std::string name;
    int fd;
    size_t size;
    int refCount;
};

static std::mutex s_androidShmMutex;
static std::map<std::string, AndroidSharedMemEntry*> s_androidShmRegistry;

static int android_shm_open(const char *name, int oflag, mode_t mode)
{
    std::lock_guard<std::mutex> lock(s_androidShmMutex);
    
    // Check if shared memory already exists in registry
    auto it = s_androidShmRegistry.find(name);
    if (it != s_androidShmRegistry.end())
    {
        // Open existing shared memory
        AndroidSharedMemEntry* entry = it->second;
        if (oflag & O_CREAT && !(oflag & O_EXCL))
        {
            // Open existing
            entry->refCount++;
            return dup(entry->fd);
        }
        else if (oflag & O_CREAT && (oflag & O_EXCL))
        {
            // Fail if exists and O_EXCL is set
            errno = EEXIST;
            return -1;
        }
        else
        {
            // Open existing
            entry->refCount++;
            return dup(entry->fd);
        }
    }
    
    // Shared memory doesn't exist
    if (oflag & O_CREAT)
    {
        // Create new shared memory using ASharedMemory_create
        // For SharedMemory, we'll use a default size and let caller resize via ftruncate
        size_t defaultSize = 4096;
        int fd = ASharedMemory_create(name, defaultSize);
        if (fd < 0)
        {
            // Fallback to temporary file
            char tempPath[256];
            snprintf(tempPath, sizeof(tempPath), "/data/local/tmp/soui_shm_%s_%d", name, getpid());
            
            int flags = O_RDWR | O_CREAT;
            if (oflag & O_EXCL)
                flags |= O_EXCL;
            
            fd = open(tempPath, flags, mode);
            if (fd < 0)
            {
                return -1;
            }
            
            // Set default size
            ftruncate(fd, defaultSize);
        }
        
        // Set protection flags
        int prot = 0;
        if (oflag & O_RDONLY)
            prot |= PROT_READ;
        if (oflag & O_RDWR)
            prot |= PROT_READ | PROT_WRITE;
        
        if (prot != 0)
        {
            ASharedMemory_setProt(fd, prot);
        }
        
        // Register the shared memory
        AndroidSharedMemEntry* entry = new AndroidSharedMemEntry();
        entry->name = name;
        entry->fd = fd;
        entry->size = defaultSize;
        entry->refCount = 1;
        s_androidShmRegistry[name] = entry;
        
        return dup(fd);
    }
    else
    {
        // Not found and not creating
        errno = ENOENT;
        return -1;
    }
}

static int android_shm_unlink(const char *name)
{
    std::lock_guard<std::mutex> lock(s_androidShmMutex);
    
    auto it = s_androidShmRegistry.find(name);
    if (it == s_androidShmRegistry.end())
    {
        errno = ENOENT;
        return -1;
    }
    
    AndroidSharedMemEntry* entry = it->second;
    
    // Close the original fd
    if (entry->fd >= 0)
    {
        close(entry->fd);
    }
    
    // Remove from registry
    s_androidShmRegistry.erase(it);
    delete entry;
    
    return 0;
}

// Redefine shm_open and shm_unlink for Android
#define shm_open android_shm_open
#define shm_unlink android_shm_unlink

// Android-specific ftruncate for ASharedMemory
static int android_ftruncate(int fd, off_t length)
{
    // ASharedMemory doesn't support ftruncate after creation
    // For SharedMemory class, we need to handle this differently
    // We'll return success and the actual size is set at creation
    return 0;
}

// Redefine ftruncate for Android if needed
#ifndef ftruncate
#define ftruncate android_ftruncate
#endif

#endif//__ANDROID__
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
#if defined(__ANDROID__)
        //todo:
        android_shm_unlink(m_name.c_str());
#else
        shm_unlink(m_name.c_str());
        sem_unlink(m_name.c_str());
#endif
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
    
#ifdef __ANDROID__
    // Android-specific implementation
    // Check if shared memory already exists in registry
    {
        std::lock_guard<std::mutex> lock(s_androidShmMutex);
        auto it = s_androidShmRegistry.find(name);
        if (it != s_androidShmRegistry.end())
        {
            // Open existing shared memory
            AndroidSharedMemEntry* entry = it->second;
            int fd = dup(entry->fd);
            if (fd >= 0)
            {
                // Map the shared memory
                LPBYTE ptr = (LPBYTE)mmap(0, entry->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (ptr == MAP_FAILED)
                {
                    close(fd);
                    perror("mmap");
                    delete rwlock;
                    return Failed;
                }
                
                nRef = *(uint32_t *)ptr;
                m_rwlock->lockExclusive();
                nRef++;
                m_rwlock->unlockExclusive();
                m_pBuf = ptr + sizeof(uint32_t);
                
                shmid = fd;
                m_dwSize = entry->size - sizeof(uint32_t);
                m_name = name;
                m_bDetached = false;
                entry->refCount++;
                ret = Existed;
                SLOG_FMTD("open share memory (Android), name=%s, ret=%d\n", name, ret);
                return ret;
            }
        }
    }
    
    // Create new shared memory with correct size
    size_t memSize = size + sizeof(uint32_t);
    int fd = ASharedMemory_create(name, memSize);
    if (fd < 0)
    {
        // Fallback to temporary file
        char tempPath[256];
        snprintf(tempPath, sizeof(tempPath), "/data/local/tmp/soui_shm_%s_%d", name, getpid());
        
        int flags = O_RDWR | O_CREAT | O_EXCL;
        fd = open(tempPath, flags, 0666);
        if (fd < 0)
        {
            perror("open temp file");
            delete rwlock;
            return Failed;
        }
        
        // Set size
        if (ftruncate(fd, memSize) == -1)
        {
            close(fd);
            perror("ftruncate");
            delete rwlock;
            return Failed;
        }
    }
    else
    {
        // Set protection flags
        ASharedMemory_setProt(fd, PROT_READ | PROT_WRITE);
        
        // Register the shared memory
        std::lock_guard<std::mutex> lock(s_androidShmMutex);
        AndroidSharedMemEntry* entry = new AndroidSharedMemEntry();
        entry->name = name;
        entry->fd = fd;
        entry->size = memSize;
        entry->refCount = 1;
        s_androidShmRegistry[name] = entry;
    }
    
    // Map the shared memory
    LPBYTE ptr = (LPBYTE)mmap(0, memSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
    {
        close(fd);
        perror("mmap");
        delete rwlock;
        return Failed;
    }
    
    nRef = *(uint32_t *)ptr;
    m_rwlock->lockExclusive();
    nRef = 1;
    m_rwlock->unlockExclusive();
    m_pBuf = ptr + sizeof(uint32_t);
    
    shmid = fd;
    m_dwSize = size;
    m_name = name;
    m_bDetached = false;
    ret = Created;
    SLOG_FMTD("open share memory (Android), name=%s, ret=%d\n", name, ret);
    return ret;
    
#else
    // Non-Android platforms use shm_open
    int fd = shm_open(name, O_RDWR, 0666); // open share memory
    if (fd == -1)
    {
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666); // 创建共享内存对象
        if (fd == -1)
        {
            perror("shm_open with O_CREAT");
            delete rwlock;
            return Failed;
        }
        if (ftruncate(fd, size + sizeof(uint32_t)) == -1)
        {
            close(fd);
            perror("ftruncate");
            delete rwlock;
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
        delete rwlock;
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
#endif
}

void SharedMemory::detach()
{
    m_bDetached = true;
}

} // namespace swinx