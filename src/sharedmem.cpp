#include "sharedmem.h"
#include <sys/mman.h>
#include <windows.h>
#include <map>
#include <new>
#include "log.h"

// 移动端（Android/OHOS/iOS）的共享内存回退路径需要这些 POSIX 头；无条件包含
// （幂等，不影响桌面端）。android/sharedmem.h 仅 Android 有，留在下方 __ANDROID__ 内。
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

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

struct AndroidSharedMemEntry
{
    std::string name;
    int fd;
    size_t size;
    int refCount;
};

static std::mutex s_androidShmMutex;
static std::map<std::string, AndroidSharedMemEntry *> s_androidShmRegistry;

static int android_shm_open(const char *name, int oflag, mode_t mode)
{
    std::lock_guard<std::mutex> lock(s_androidShmMutex);

    // Check if shared memory already exists in registry
    auto it = s_androidShmRegistry.find(name);
    if (it != s_androidShmRegistry.end())
    {
        // Open existing shared memory
        AndroidSharedMemEntry *entry = it->second;
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
        AndroidSharedMemEntry *entry = new AndroidSharedMemEntry();
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

    AndroidSharedMemEntry *entry = it->second;

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
#define shm_open   android_shm_open
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

#endif //__ANDROID__
namespace swinx
{
SharedMemory::~SharedMemory()
{
    if (shmid == -1 && !m_bHeap)
        return;
    bool bUnlink = false;
    if (m_rwlock)
    {
        m_rwlock->lockExclusive();
        bUnlink = (0 == (--nRef));
        m_rwlock->unlockExclusive();
    }
    if (m_bHeap)
    {
        // 堆内存回退：缓冲区由 new[] 分配。注意 nRef 是引用别名（绑定到
        // m_dwSize，链接期不可重绑），&nRef 指向对象自身成员而非缓冲区，不能
        // 用来释放；真实缓冲区基址 = m_pBuf - sizeof(uint32_t)。
        delete[] (m_pBuf - sizeof(uint32_t));
    }
    else
    {
        munmap(&nRef, m_dwSize + sizeof(uint32_t));
        close(shmid);
    }
    delete m_rwlock;
    SLOG_FMTD("close share memory, name=%s, heap=%d, bUnlink=%d", m_name.c_str(), m_bHeap, bUnlink);

    if (bUnlink && !m_bDetached && !m_bHeap)
    {
#if defined(__ANDROID__)
        // todo:
        android_shm_unlink(m_name.c_str());
#else
        shm_unlink(m_name.c_str());
        // Note: the rwlock backing this SharedMemory is now a POSIX fcntl
        // record lock on a per-platform lock file (lockDir()/soui_flock_*.lock
        // in sharedmem.h; Android/OHOS -> /data/local/tmp, others -> /tmp). We
        // deliberately do NOT unlink that lock file here: fcntl locks are
        // tied to the open file description and released when this fd (and
        // any other holder's fd) closes. Unlinking the path while another
        // process still holds the lock would let a later process create a
        // brand-new file that the stale holder's lock does NOT protect,
        // breaking mutual exclusion. A leftover lock file is harmless — it
        // carries no lock state once every fd is closed.
#endif
    }
}

SharedMemory::InitStat SharedMemory::init(const char *name, uint32_t size)
{
    assert(m_rwlock == nullptr);

#if defined(__ANDROID__) || defined(__OHOS__) || defined(__IOS__)
    // ------------------------------------------------------------------
    // 移动端（Android/OHOS/iOS）：命名内核对象不被支持，且沙箱内 /data/local/tmp
    // 常不可写。这些平台单进程运行，全局句柄表（GLobalHandleTable）只需进程内
    // 共享，不存在跨进程并发。故：
    //   1) 用进程内匿名读写锁（TSemRwLock 移动端实现，即 std::mutex +
    //      std::condition_variable）取代命名 fcntl 文件锁，init 永不失败，
    //      避免在 main 前构造全局句柄表时 open 锁文件失败使其失效；
    //   2) 先尝试真实共享内存（ASharedMemory_create / 临时文件），任一环节失败
    //      则回退到堆内存模拟，保证 init 成功、全局句柄表不失效、程序不崩溃。
    // 跨进程共享在移动端无意义（单进程 App），堆回退对每个实例给出独立缓冲区，
    // 对全局句柄表这类单例语义正确。
    // ------------------------------------------------------------------
    TSemRwLock<kSharedNumber> *rwlock = new TSemRwLock<kSharedNumber>();
    if (!rwlock->init(name))
    {
        delete rwlock;
        return Failed;
    }
    m_rwlock = rwlock;

    const uint32_t memSize = size + sizeof(uint32_t);
    int fd = -1;
    LPBYTE ptr = nullptr;

#if defined(__ANDROID__)
    // ASharedMemory 仅 Android 可用；名字带 '/' 或 '-' 时仍可能创建成功（内核以
    // ashmem 名称记录），失败再走临时文件回退。
    fd = ASharedMemory_create(name, memSize);
    if (fd >= 0)
        ASharedMemory_setProt(fd, PROT_READ | PROT_WRITE);
#endif
    if (fd < 0)
    {
        // 临时文件（OHOS/iOS 无 ASharedMemory；Android 的 ASharedMemory 失败时也走这里）
        char tempPath[256];
        snprintf(tempPath, sizeof(tempPath), "/data/local/tmp/soui_shm_%s_%d",
                 name ? name : "anon", (int)getpid());
        fd = open(tempPath, O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd >= 0 && ftruncate(fd, memSize) == -1)
        {
            close(fd);
            fd = -1;
        }
    }
    if (fd >= 0)
    {
        ptr = (LPBYTE)mmap(0, memSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED)
        {
            close(fd);
            fd = -1;
            ptr = nullptr;
        }
    }

    if (fd < 0 || ptr == nullptr)
    {
        // 堆内存回退：即便临时文件/mapping 都失败也不让全局句柄表失效
        fd = -1;
        ptr = new (std::nothrow) uint8_t[memSize];
        if (ptr == nullptr)
        {
            // 极端情况：连堆都分配不出。m_rwlock 已设置，返回 Failed 让上层感知
            // （与桌面端语义一致），但不再出现 init 部分成功却留下空 m_rwlock。
            delete rwlock;
            m_rwlock = nullptr;
            return Failed;
        }
        m_bHeap = true;
    }
    else
    {
        m_bHeap = false;
    }

    nRef = *(uint32_t *)ptr;
    m_rwlock->lockExclusive();
    nRef = 1;
    m_rwlock->unlockExclusive();
    m_pBuf = ptr + sizeof(uint32_t);
    shmid = fd;
    m_dwSize = size;
    m_name = name ? name : "";
    m_bDetached = false;
    SLOG_FMTD("open share memory (mobile, heap-fallback=%d), name=%s\n", m_bHeap, name);
    return Created;

#else
    // 桌面 POSIX 平台（Linux/macOS）：保留跨进程共享内存语义（命名共享内存 +
    // 命名 fcntl 文件锁），用于进程间句柄/IPC。
    TSemRwLock<kSharedNumber> *rwlock = new TSemRwLock<kSharedNumber>();
    if (!rwlock->init(name))
    {
        delete rwlock;
        return Failed;
    }
    m_rwlock = rwlock;
    InitStat ret = Failed;

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