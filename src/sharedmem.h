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
#include <condition_variable>
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
// 解决方式：以锁文件路径为 key 维护每路径一份进程内状态（由 FlockGateTable
// 单例持有，堆上分配且不参与静态析构 —— 原因见下方 FlockGateTable 处的说明 ——
// 因此可安全用于全局构造期与全局析构期）：
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

// gate 状态表与配套互斥量。
//
// 为什么把互斥量与表放进同一个对象，而不是两个各自独立的函数局部静态量：
//   1. 二者的相对析构顺序由语言给定——成员按"声明逆序"销毁，互斥量声明在表之前，
//      于是表先拆、锁后毁：绝不会出现"锁已销毁、表还在被操作"或"表已销毁、
//      锁还被取用"这类由静态初始化次序决定的未定义行为；
//   2. 表只能在它自己的锁保护下读写——这是同一个不变量；封装成对象后不存在
//      "取到 A 的表、却用 B 的锁"的可能；
//   3. 只分配一次，退出时只多一份 still-reachable。
//
// 为什么整份对象仍然刻意不析构（leak on purpose）：本对象在多个编译单元里各
// 有一份弱定义（例如 exe 的 fun_test 与 libswinx.so 各一份），静态局部量的析构
// 由"首次初始化它的那个编译单元"通过 __cxa_atexit 注册到该 DSO 的 __dso_handle
// 上。DLL 模式下会出现"exe 注册了析构、DLL 仍在使用"的组合：进程退出时 exe 的
// __cxa_finalize 先跑（本对象被销毁），随后才轮到 libswinx.so 的 fini_array，而
// sysobjs.cpp 的静态对象 GLobalHandleTable 的析构会经 SharedMemory::~SharedMemory
// -> ~TSemRwLock -> flockGateRelease 再次访问本表，对已被释放的 map 节点做
// find/erase。
//
// 该顺序无法靠调整构造顺序修复：exe 的 __cxa_finalize 恒早于 _dl_fini，而本对象
// 又必然是在使用者的构造函数里"后构造"、按 LIFO 就先析构。故 instance() 在堆上
// 分配且永不释放：退出时只多一份空表与互斥量计入 still-reachable，远小于
// use-after-free 的代价（这正是 valgrind 报 23 处 Invalid read 与 2 处 Invalid
// free 的根因）。表内条目仍在 refs 归零时正常 delete 并 erase，运行期行为与
// 原来完全一致。
class FlockGateTable
{
  public:
    static FlockGateTable &instance()
    {
        static FlockGateTable *s_table = new FlockGateTable();
        return *s_table;
    }

    // 取 path 对应的 gate 状态；不存在则新建，引用计数 +1
    FlockGateState *acquire(const std::string &path)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::map<std::string, FlockGateState *>::iterator it = m_states.find(path);
        if (it == m_states.end())
        {
            FlockGateState *st = new FlockGateState();
            m_states[path] = st;
            st->refs = 1;
            return st;
        }
        FlockGateState *st = it->second;
        st->refs++;
        return st;
    }

    // 释放 path 对应的 gate 状态；引用计数归零则销毁条目
    void release(const std::string &path)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::map<std::string, FlockGateState *>::iterator it = m_states.find(path);
        if (it != m_states.end() && --it->second->refs == 0)
        {
            delete it->second;
            m_states.erase(it);
        }
    }

  private:
    FlockGateTable()
    {
    }

    // 声明次序即析构逆序：表先析构，护着它的互斥量后析构
    std::mutex m_mutex;
    std::map<std::string, FlockGateState *> m_states;
};

inline FlockGateState *flockGateAcquire(const std::string &path)
{
    return FlockGateTable::instance().acquire(path);
}
inline void flockGateRelease(const std::string &path)
{
    FlockGateTable::instance().release(path);
}

// ---------------------------------------------------------------------------
// TSemRwLock：swinx 唯一的读写锁实现，跨平台以同一个模板类暴露，移动端与
// 桌面端分别提供实现（见下方 #if / #else 分支）。所有需要"rwlock"的地方
// （GlobalMutex、SharedMemory 全局句柄表等）只认 TSemRwLock，不再有
// TAnonymousRwLock / TNamedSemRwLock 等别名类型。
//
//   - 移动端（Android/OHOS/iOS）：这些平台单进程运行，全局句柄表
//     （GLobalHandleTable）只需进程内共享，不存在跨进程并发；同时移动沙箱里
//     /data/local/tmp 等目录常不可写，基于 fcntl 的命名文件锁在 init 时 open
//     锁文件会失败，令全局句柄表失效、进而运行时 getRwLock() 返回空指针并崩溃。
//     故移动端用 std::mutex + std::condition_variable 实现进程内匿名读写锁，
//     init 永不失败，配合 SharedMemory 在移动端的"堆内存回退"即可根治该崩溃。
//   - 桌面端（Linux/macOS）：以 fcntl POSIX 记录锁实现，取代 XSI System V
//     信号量 / 命名计数信号量（sem_open/sem_wait/sem_post），用于跨进程互斥
//     （命名等待对象、文件映射对象、全局句柄表所在的共享内存）。fcntl 记录锁
//     在持有进程退出时由内核自动释放，可根治"持锁进程崩溃后其它进程永久阻塞"
//     的残留卡死。
//
// 语义与 ISemRwLock 一致：lockShared=读锁、lockExclusive=写锁、不可重入
// （每段临界区一次 lock 配一次 unlock）。kNumLock 仅保留为模板签名兼容
// （GlobalMutex 用 TSemRwLock<1>、SharedMemory 用 TSemRwLock<kSharedNumber>），
// 记录锁不需要显式槽位计数，故以 static_assert 引用以满足 -Wall -Wextra。
// ---------------------------------------------------------------------------
#if defined(__ANDROID__) || defined(__OHOS__) || defined(__IOS__)

// 移动端实现：进程内匿名读写锁。
//
// 为兼顾"写者优先"避免读饿死，加入 m_writersWaiting 计数；全局句柄表实际
// 使用中以排他路径为主，读锁路径也存在（WaitForSingleObject 等），保持完整
// 读写语义以防回归。init 接受 key 或 name 均直接成功，不创建任何文件。
template <int kNumLock = 2>
class TSemRwLock : public ISemRwLock {
    static_assert(kNumLock >= 1, "kNumLock retained for source compatibility");
  private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_readers;
    int m_writersWaiting;
    bool m_writer;

  public:
    TSemRwLock()
        : m_readers(0)
        , m_writersWaiting(0)
        , m_writer(false)
    {
    }
    ~TSemRwLock() override {}

    // 移动端无需命名内核对象：init 永不失败。
    bool init(uint32_t) { return true; }
    bool init(const char *) { return true; }

    void lockShared() override
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this] { return !m_writer && m_writersWaiting == 0; });
        ++m_readers;
    }
    void unlockShared() override
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (--m_readers == 0)
            m_cv.notify_all();
    }
    void lockExclusive() override
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        ++m_writersWaiting;
        m_cv.wait(lk, [this] { return !m_writer && m_readers == 0; });
        --m_writersWaiting;
        m_writer = true;
    }
    void unlockExclusive() override
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_writer = false;
        m_cv.notify_all();
    }
};

#else // 桌面 POSIX 平台（Linux/macOS）

// 桌面端实现：以 fcntl POSIX 记录锁实现跨进程互斥。
//
// key / name 语义：
//   - init(uint32_t key)：锁文件名 = /tmp/soui_flock_key_<key十进制>.lock
//     （GlobalMutex : TSemRwLock<1> 使用）。key 为纯数字，天然文件名安全。
//   - init(const char* name)：锁文件名 = /tmp/soui_flock_<净化名>.lock
//     （SharedMemory 使用）。name 中的非 [A-Za-z0-9_-] 字符统一替换为 '_'，
//     杜绝路径穿越。
//   同一命名对象跨进程计算出的 key/name 一致，从而 open 同一锁文件并通过 fcntl
//   实现跨进程互斥，与 XSI 的 semget(key) 等价。
//
// 语义映射（本类实际以 GlobalMutex / TSemRwLock<1> 使用，只走排他路径）：
//   - lockExclusive  = F_WRLCK（排他），unlockExclusive = F_UNLCK
//   - lockShared     = F_RDLCK（共享，通用读写锁语义保留），unlockShared = F_UNLCK
//
// 约束：进程内线程间互斥、及同进程多个锁对象实例映射到同一锁文件时的互斥，
// 由 FlockGateState（见文件顶部说明）恢复，与原 sem_wait 行为一致。使用方仍
// 须保证不嵌套加锁（fcntl 记录锁与信号量一样不可重入）。
template <int kNumLock = 2>
class TSemRwLock : public ISemRwLock {
    static_assert(kNumLock >= 1, "kNumLock retained for source compatibility");
  private:
    int m_fd;
    uint32_t m_key;
    std::string m_lockPath;
    FlockGateState *m_gate;

    // 共享内存名形如 "/share_soui_..."；剥离路径分隔符等非法文件名字符。
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
        m_lockPath = std::string("/tmp/") + "soui_flock_key_" + keybuf + ".lock";
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

    bool init(const char *name)
    {
        assert(m_fd == -1);
        m_lockPath = std::string("/tmp/") + "soui_flock_" + sanitizeName(name) + ".lock";
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

#endif // __ANDROID__ / __OHOS__ / __IOS__

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
    bool m_bHeap;
    ISemRwLock *m_rwlock;

  public:
    SharedMemory()
        : m_pBuf((LPBYTE)-1)
        , m_dwSize(0)
        , shmid(-1)
        , nRef(m_dwSize)
        , m_bDetached(false)
        , m_bHeap(false)
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
