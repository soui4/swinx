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
#include <map>
#include <mutex>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/file.h>
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

// ---------------------------------------------------------------------------
// 进程内线程串行化辅助层（fcntl 文件锁的配套层）
//
// 为什么需要：fcntl POSIX 记录锁的所有权属于"进程"而非线程——
//   1. 同进程的任何线程发起 F_SETLKW 都不会阻塞（进程已持锁即成功）；
//   2. 任一线程的 F_UNLCK 会释放整个进程在该文件上的锁，即使另一个线程
//      仍认为自己持有它。
// 而被替换的 XSI 信号量 sem_wait 按计数阻塞，同进程多线程之间、以及同进程
// 映射到同一信号量的多个锁对象实例之间同样互斥。若只用裸文件锁，多线程
// 应用对同一命名对象的并发访问（如两个线程同时对同一命名信号量 Set/Wait）
// 将失去互斥——这是功能回归。
//
// 解决方式：以锁文件路径为 key 维护每路径一份进程内状态（函数局部静态，
// 可安全用于全局构造期）：
//   - gate     : 线程临界区互斥，恢复与 sem_wait 等价的线程间串行；
//   - opMutex  : 保护 holders 计数；
//   - holders  : 本进程当前处于该锁临界区的线程数；文件锁只在第一个持有
//                者进入时加、最后一个持有者退出时解，避免跨线程误释放，
//                也保证文件锁覆盖窗口恰好是"有本进程线程在临界区内"。
// 锁获取顺序 gate -> opMutex -> 内核(F_SETLKW)：线程在等待其它进程释放文
// 件锁时持有 gate（与旧实现 sem_wait 阻塞等价），不会引入原实现没有的死锁。
// ---------------------------------------------------------------------------
struct FlockGateState
{
    std::mutex gate;
    std::mutex opMutex;
    int holders;
    int refs;
    FlockGateState()
        : holders(0)
        , refs(0)
    {
    }
};

inline std::map<std::string, FlockGateState *> &flockGateStates()
{
    static std::map<std::string, FlockGateState *> s_states;
    return s_states;
}
inline std::mutex &flockGateStatesMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}
inline FlockGateState *flockGateAcquire(const std::string &path)
{
    std::lock_guard<std::mutex> lk(flockGateStatesMutex());
    FlockGateState *st = nullptr;
    std::map<std::string, FlockGateState *>::iterator it = flockGateStates().find(path);
    if (it == flockGateStates().end())
    {
        st = new FlockGateState();
        flockGateStates()[path] = st;
        st->refs = 1;
    }
    else
    {
        st = it->second;
        st->refs++;
    }
    return st;
}
inline void flockGateRelease(const std::string &path)
{
    std::lock_guard<std::mutex> lk(flockGateStatesMutex());
    std::map<std::string, FlockGateState *>::iterator it = flockGateStates().find(path);
    if (it != flockGateStates().end() && --it->second->refs == 0)
    {
        delete it->second;
        flockGateStates().erase(it);
    }
}

// 通用读写锁：以 fcntl POSIX 记录锁实现，取代 XSI System V 信号量。
//
// 为什么要替换：XSI 信号量集（semget/semop）由内核持久持有，当最后一个持
// 有进程崩溃退出时，其计数状态不会自动恢复。GlobalMutex（TSemRwLock<1>）
// 被命名等待对象/文件映射对象用于跨进程互斥；若持锁进程在 semop 拉空计数
// 后崩溃，其它进程下一次 semop 会永久阻塞（与命名信号量同类问题）。
// fcntl 记录锁在持有进程退出（无论正常或崩溃）时由内核自动释放，可根治此
// 残留卡死。
//
// 关键点：本类是跨平台通用类（Android/OHOS 与非 Android 均编译，见
// FileMapObject 在 Android/OHOS 分支也调用 mutex.init），因此锁文件目录须按
// 平台区分——移动沙箱没有标准 /tmp（Android 无 /tmp，OpenHarmony 应用沙箱
// 内 /tmp 亦不可作为跨进程命名目录），统一用 /data/local/tmp；Linux/macOS
// 等桌面 POSIX 平台用 /tmp。
//
// key 语义保持不变：调用方以整数 key 标识一把锁（例如
//  s_globalHandleTable.getHeader()->key + idx + 10000
// ），同一命名对象跨进程计算出的 key 一致，从而 open 同一锁文件并通过
// fcntl 实现跨进程互斥。与 XSI 的 semget(key) 同步到同一信号量集等价。
//
// 锁映射：锁文件名 = <dir>/soui_flock_key_<key十进制>.lock。key 为纯数字，
// 天然文件名安全、无路径穿越。锁文件进程退出即无状态（不残留锁），unlink
// 非必要也不应做——残留 .lock 文件无害。
//
// 语义映射（本类实际以 GlobalMutex / TSemRwLock<1> 使用，只走排他路径）：
//   - lockExclusive  = F_WRLCK（排他）
//   - unlockExclusive = F_UNLCK
//   - lockShared     = F_RDLCK（共享，本模板为通用读写锁语义保留）
//   - unlockShared   = F_UNLCK
//
// 约束（与 XSI 版相同）：进程内的线程间互斥、以及同进程多个锁对象实例映射
// 到同一锁文件时的互斥，由 FlockGateState（见文件顶部说明）恢复，与原
// sem_wait 行为一致。使用方仍须保证不嵌套加锁：每段临界区一次 lock 配一次
// unlock（fcntl 记录锁与信号量一样不可重入）。
// kNumLock 仅保留为模板签名兼容（GlobalMutex 用 TSemRwLock<1>），记录锁
// 不需要显式槽位计数，故 static_assert 引用之以满足 -Wall -Wextra。
template <int kNumLock = 2>
class TSemRwLock : public ISemRwLock {
    static_assert(kNumLock >= 1, "kNumLock retained for source compatibility");
  private:
    int m_fd;
    uint32_t m_key;
    std::string m_lockPath;
    FlockGateState *m_gate;

    static const char *lockDir()
    {
#if defined(__ANDROID__) || defined(__OHOS__)
        // 移动/鸿蒙沙箱无可写 /tmp；/data/local/tmp 与 Android 分支既有约定
        // 一致（调试/具 shell 权限形态可写）。单进程 App 内跨进程锁不参与真
        // 实并发，但全局静态对象仍在 main 前 open 该目录下的锁文件，目录必须
        // 可写，否则 init 失败会让全局句柄表失效。
        return "/data/local/tmp/";
#else
        return "/tmp/";
#endif
    }

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
    TSemRwLock()
        : m_fd(-1)
        , m_key(0)
        , m_gate(nullptr)
    {
    }
    ~TSemRwLock()
    {
        if (m_gate != nullptr)
        {
            flockGateRelease(m_lockPath);
        }
        if (m_fd != -1)
        {
            close(m_fd);
        }
    }

    uint32_t getKey() const
    {
        return m_key;
    }

    bool init(uint32_t _key)
    {
        assert(m_fd == -1);
        m_key = _key;
        char keybuf[32];
        snprintf(keybuf, sizeof(keybuf), "%u", (unsigned)_key);
        m_lockPath = std::string(lockDir()) + "soui_flock_key_" + keybuf + ".lock";
        // O_RDWR 让单一 fd 可同时取读(F_RDLCK)/写(F_WRLCK)锁；历史残留文件
        // 无害，因为 fcntl 记录锁不跨进程退出残留。
        m_fd = open(m_lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd == -1)
        {
            int err = errno;
            printf("open lock file failed, path=%s, errno=%d\n", m_lockPath.c_str(), err);
            return false;
        }
        m_gate = flockGateAcquire(m_lockPath);
        return true;
    }

    void lockShared() override
    {
        m_gate->gate.lock(); // 线程间互斥（等价于旧 sem_wait 的进程内阻塞）
        m_gate->opMutex.lock();
        if (m_gate->holders++ == 0) // 本进程第一个持有者负责加文件锁
            setFileLock(F_RDLCK);
        m_gate->opMutex.unlock();
    }

    void unlockShared() override
    {
        m_gate->opMutex.lock();
        if (--m_gate->holders == 0) // 最后一个持有者退出才释放文件锁
            setFileLock(F_UNLCK);
        m_gate->opMutex.unlock();
        m_gate->gate.unlock();
    }

    void lockExclusive() override
    {
        m_gate->gate.lock();
        m_gate->opMutex.lock();
        if (m_gate->holders++ == 0)
            setFileLock(F_WRLCK);
        m_gate->opMutex.unlock();
    }

    void unlockExclusive() override
    {
        m_gate->opMutex.lock();
        if (--m_gate->holders == 0)
            setFileLock(F_UNLCK);
        m_gate->opMutex.unlock();
        m_gate->gate.unlock();
    }
};

// 命名的读写锁：以 fcntl POSIX 记录锁实现，取代命名计数信号量
// （sem_open/sem_wait/sem_post）。本实现为 Android / OHOS / Linux / macOS
// 等所有 POSIX 平台共用；平台差异仅体现在锁文件目录（lockDir）。
//
// Why a file lock: a POSIX named semaphore (sem_open/sem_wait/sem_post) keeps
// its kernel state after every process using it exits. If the last holder
// crashes while holding the lock (count already drained by sem_wait, never
// returned by sem_post), the semaphore stays exhausted, so the NEXT process
// blocks forever in its global-object constructor (GLobalHandleTable is a
// static that runs before main) — the program cannot start. fcntl record
// locks are released by the kernel automatically when the owning process
// exits, whether cleanly or by a crash, so a stale lock can never wedge a
// subsequent startup.
//
// Semantics vs the counting-semaphore version:
//   - lockShared  = F_RDLCK (multiple readers may hold it concurrently)
//   - lockExclusive = F_WRLCK (mutually exclusive with readers and writers)
//   - The kNumLock template parameter is kept only for signature/ABI
//     compatibility with call sites (TNamedSemRwLock<kSharedNumber>); the
//     POSIX record lock needs no explicit slot counting.
//
// Usage constraint: each critical section must pair one lock with exactly
// one unlock (neither fcntl record locks nor the replaced semaphores are
// re-entrant — do not nest). Intra-process mutual exclusion between threads
// — and between multiple lock-object instances mapped to the same lock file
// — is restored by FlockGateState (see the block comment near the top of
// this header), matching the old sem_wait behavior.
template <int kNumLock = 2>
class TNamedSemRwLock : public ISemRwLock {
    // kNumLock kept for source compatibility with TNamedSemRwLock<kSharedNumber>;
    // a record lock needs no slot count. Referenced so -Wall -Wextra sees it.
    static_assert(kNumLock >= 1, "kNumLock retained for ABI compatibility");
  private:
    int m_fd;
    std::string m_lockPath;
    FlockGateState *m_gate;

    static const char *lockDir()
    {
#if defined(__ANDROID__) || defined(__OHOS__)
        // Android / OpenHarmony 无标准 /tmp：与共享内存/句柄表的既有移动端
        // 目录约定一致，都用 /data/local/tmp（调试/具权限形态可写）。全局静
        // 态对象（GLobalHandleTable）在 main 前即 open 此目录下的锁文件，故
        // 目录必须可写；单进程 App 内跨进程锁不参与真实并发。
        return "/data/local/tmp/";
#else
        return "/tmp/";
#endif
    }

    // shared-memory names look like "/share_soui_..."; strip path separators
    // and any other character that is not file-name safe. 对 Android/OHOS 同样
    // 适用（共享内存名带前导 '/'，直接拼进文件名会开出子目录使 open 失败）。
    static std::string sanitizeName(const char *name)
    {
        std::string s = name ? name : "default";
        for (char &c : s)
        {
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
                !(c >= '0' && c <= '9') && c != '_' && c != '-')
            {
                c = '_';
            }
        }
        return s;
    }

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
        , m_gate(nullptr)
    {
    }
    ~TNamedSemRwLock()
    {
        if (m_gate != nullptr)
        {
            flockGateRelease(m_lockPath);
        }
        if (m_fd != -1)
        {
            close(m_fd);
        }
    }

    bool init(const char *name)
    {
        assert(m_fd == -1);
        m_lockPath = std::string(lockDir()) + "soui_flock_" + sanitizeName(name) + ".lock";
        // O_RDWR so the single fd can take both read (F_RDLCK) and write
        // (F_WRLCK) locks; a stale file from a previous run is harmless
        // because fcntl locks do not persist across process exit.
        m_fd = open(m_lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd == -1)
        {
            int err = errno;
            printf("open lock file failed, path=%s, errno=%d\n", m_lockPath.c_str(), err);
            return false;
        }
        m_gate = flockGateAcquire(m_lockPath);
        return true;
    }

    void lockShared() override
    {
        m_gate->gate.lock(); // 线程间互斥（等价于旧 sem_wait 的进程内阻塞）
        m_gate->opMutex.lock();
        if (m_gate->holders++ == 0) // 本进程第一个持有者负责加文件锁
            setFileLock(F_RDLCK);
        m_gate->opMutex.unlock();
    }

    void unlockShared() override
    {
        m_gate->opMutex.lock();
        if (--m_gate->holders == 0) // 最后一个持有者退出才释放文件锁
            setFileLock(F_UNLCK);
        m_gate->opMutex.unlock();
        m_gate->gate.unlock();
    }

    void lockExclusive() override
    {
        m_gate->gate.lock();
        m_gate->opMutex.lock();
        if (m_gate->holders++ == 0)
            setFileLock(F_WRLCK);
        m_gate->opMutex.unlock();
    }

    void unlockExclusive() override
    {
        m_gate->opMutex.lock();
        if (--m_gate->holders == 0)
            setFileLock(F_UNLCK);
        m_gate->opMutex.unlock();
        m_gate->gate.unlock();
    }
};

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
        : m_pBuf((LPBYTE)-1)
        , m_dwSize(0)
        , shmid(-1)
        , nRef(m_dwSize)
        , m_rwlock(nullptr) // init nRef to m_dwSize to avoid compile error. nRef will ref to buffer header later. hjx 2024/9/10
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