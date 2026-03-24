# Swinx 跨平台 Windows 内核对象实现机制

## 目录

- [1. 概述](#1-概述)
- [2. 核心架构设计](#2-核心架构设计)
- [3. HANDLE 对象管理系统](#3-handle-对象管理系统)
- [4. 同步对象实现](#4-同步对象实现)
- [5. 命名对象与跨进程共享](#5-命名对象与跨进程共享)
- [6. 等待机制实现](#6-等待机制实现)
- [7. 关键数据结构](#7-关键数据结构)
- [8. 实现示例](#8-实现示例)
- [9. 总结](#9-总结)

---

## 1. 概述

### 1.1 项目背景

**swinx** 是 SOUI5 项目的跨平台适配层，为 Linux/macOS 提供 Windows API 兼容性支持。其核心目标是：
- 在 POSIX 系统（Linux/macOS）上模拟 Windows 内核对象行为
- 保持与 Windows API 高度兼容的接口
- 支持跨进程对象共享和命名对象语义
- 提供高效的同步和等待机制

### 1.2 支持的内核对象类型

Swinx 实现了以下 Windows 内核对象的跨平台支持：

#### 同步对象 (SYN_OBJ = 100)
- **事件 (Event)**: `CreateEventA/W`, `SetEvent`, `ResetEvent`, `OpenEvent`
- **信号量 (Semaphore)**: `CreateSemaphoreA/W`, `ReleaseSemaphore`, `OpenSemaphore`
- **互斥量 (Mutex)**: `CreateMutexA/W`, `ReleaseMutex`, `OpenMutex`
- **可等待定时器 (Timer)**: `CreateWaitableTimer`, `SetWaitableTimer`, `OpenWaitableTimer`

#### GDI 对象 (GDI_OBJ = 1)
- 设备上下文 (DC)
- 位图 (Bitmap)
- 区域 (Region)
- 字体 (Font)
- 画刷 (Brush)
- 画笔 (Pen)

#### 文件对象 (FILE_OBJ = 20)
- 文件句柄
- 文件通知句柄

#### 堆对象 (HEAP_OBJ = 21)
- 私有堆创建和管理

#### 文件映射对象 (FILEMAP_OBJ = 200)
- 内存映射文件
- 共享内存

### 1.3 跨平台策略

Swinx 采用以下策略实现跨平台兼容：

1. **统一接口层**: 保持与 Windows API 完全一致的函数签名和行为
2. **原生实现**: 底层使用 POSIX API（pthread、FIFO、共享内存等）
3. **透明转换**: 自动处理 Unicode/ANSI 字符串转换
4. **对象抽象**: 通过虚函数和模板实现多态行为

---

## 2. 核心架构设计

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────┐
│              Windows API Compatibility Layer            │
│  (CreateEvent, CreateMutex, WaitForSingleObject, etc.) │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                  Handle Management Layer                │
│         (_Handle, Reference Counting, Type Check)       │
└─────────────────────────────────────────────────────────┘
                            ↓
        ┌───────────────────┴───────────────────┐
        ↓                                       ↓
┌──────────────────┐                  ┌──────────────────┐
│  Anonymous Obj   │                  │   Named Obj      │
│  (pthread-based) │                  │ (FIFO + Shm)     │
└──────────────────┘                  └──────────────────┘
        ↓                                       ↓
┌──────────────────┐                  ┌──────────────────┐
│ pthread_mutex    │                  │ Global Handle    │
│ pthread_cond     │                  │ Table (Shm)      │
│ pipe(fd[2])      │                  │ pipe(fifo)       │
└──────────────────┘                  └──────────────────┘
```

### 2.2 设计模式

#### 2.2.1 类型安全枚举

```cpp
enum
{
    GDI_OBJ = 1,      // GDI 对象
    FILE_OBJ = 20,    // 文件对象
    HEAP_OBJ = 21,    // 堆对象
    SYN_OBJ = 100,    // 同步对象
    FILEMAP_OBJ = 200,// 文件映射对象
};
```

#### 2.2.2 CRTP 模板模式

使用 Curiously Recurring Template Pattern 实现代码复用：

```cpp
template <typename T>
struct EventOp : T
{
    bool onWaitDone() override
    {
        EventData *data = (EventData *)T::getData();
        if (data->bManualReset)
            return true;
        // 清除所有信号
        int signals = 0;
        while (T::readSignal())
            signals++;
        return signals > 0;
    }
};

// 使用方式
typedef EventOp<NoNameEvent> NoNameEventObj;
typedef EventOp<NamedEvent> NamedEventObj;
```

#### 2.2.3 策略模式

为不同对象类型定义不同的操作策略：
- **EventOp**: 事件对象操作（支持手动重置/自动重置）
- **SemaphoreOp**: 信号量操作（计数管理）
- **MutexOp**: 互斥量操作（所有权跟踪）

---

## 3. HANDLE 对象管理系统

### 3.1 基础 HANDLE 结构

所有内核对象都封装在统一的 `_Handle` 结构中：

```cpp
typedef struct _Handle
{
    std::recursive_mutex mutex;    // 线程安全的互斥锁
    int type;                       // 对象类型标识
    int nRef;                       // 引用计数
    void *ptr;                      // 实际对象数据指针
    FreeHandlePtr cbFree;           // 析构回调函数
    
    _Handle(int _type, void *_ptr, FreeHandlePtr _cbFree);
    virtual ~_Handle();
} * HANDLE;
```

**关键字段说明**：
- `mutex`: 保护引用计数的线程安全
- `type`: 运行时类型检查，确保 API 正确使用
- `nRef`: 支持 COM 风格的引用计数管理
- `ptr`: 多态指针，指向具体对象数据
- `cbFree`: RAII 风格的资源清理回调

### 3.2 引用计数管理

```cpp
HANDLE WINAPI AddHandleRef(HANDLE hHandle)
{
    if (hHandle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    if (!hHandle)
        return 0;
    hHandle->mutex.lock();
    hHandle->nRef++;
    hHandle->mutex.unlock();
    return hHandle;
}

BOOL WINAPI CloseHandle(HANDLE h)
{
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!h)
        return TRUE;
    if (!h->cbFree) // 静态对象不释放
        return TRUE;
    h->mutex.lock();
    BOOL bFree = --h->nRef == 0;
    h->mutex.unlock();
    if (bFree)
    {
        delete h;
    }
    return TRUE;
}
```

### 3.3 对象创建与初始化

```cpp
HANDLE InitHandle(int type, void *ptr, FreeHandlePtr cbFree)
{
    return new _Handle(type, ptr, cbFree);
}

// 使用示例
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, 
                          BOOL bInitialOwner, LPCSTR lpName)
{
    _MutexData *mutex = new _MutexData;
    if (bInitialOwner)
    {
        pthread_mutex_lock(&mutex->mutex);
    }
    return InitHandle(MUTEX_OBJ, mutex, FreeMutexData);
}
```

---

## 4. 同步对象实现

### 4.1 临界区 (Critical Section)

基于 `std::recursive_mutex` 实现：

```cpp
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

VOID WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    delete (std::recursive_mutex *)lpCriticalSection->pMutex;
    lpCriticalSection->pMutex = NULL;
}
```

### 4.2 SRW 锁 (Slim Reader-Writer Lock)

基于 `pthread_rwlock_t` 实现：

```cpp
VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock)
{
    SRWLock->Ptr = new pthread_rwlock_t;
    ::pthread_rwlock_init((pthread_rwlock_t *)SRWLock->Ptr, NULL);
}

VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_wrlock((pthread_rwlock_t *)SRWLock->Ptr);
}

VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_rdlock((pthread_rwlock_t *)SRWLock->Ptr);
}

VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock)
{
    ::pthread_rwlock_unlock((pthread_rwlock_t *)SRWLock->Ptr);
}
```

### 4.3 互斥量 (Mutex)

#### 4.3.1 数据结构

```cpp
struct _MutexData
{
    pthread_mutex_t mutex;
    _MutexData()
    {
        pthread_mutex_init(&mutex, NULL);
    }
    ~_MutexData()
    {
        pthread_mutex_destroy(&mutex);
    }
};
```

#### 4.3.2 创建与释放

```cpp
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, 
                          BOOL bInitialOwner, LPCSTR lpName)
{
    _MutexData *mutex = new _MutexData;
    if (bInitialOwner)
    {
        pthread_mutex_lock(&mutex->mutex);
    }
    return InitHandle(MUTEX_OBJ, mutex, FreeMutexData);
}

BOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
    if (!hMutex || hMutex->type != MUTEX_OBJ)
    {
        return FALSE;
    }
    _MutexData *mutex = (_MutexData *)hMutex->ptr;
    return pthread_mutex_unlock(&mutex->mutex) == 0;
}
```

### 4.4 信号量 (Semaphore)

#### 4.4.1 数据结构

```cpp
struct _SemaphoreData
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;       // 当前计数
    int maxCount;    // 最大计数
    
    _SemaphoreData(int initialCount, int maximumCount)
        : count(initialCount), maxCount(maximumCount)
    {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cond, NULL);
    }
};
```

#### 4.4.2 核心操作

```cpp
HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, 
                               LONG lInitialCount, LONG lMaximumCount, 
                               LPCSTR lpName)
{
    _SemaphoreData *semaphore = new _SemaphoreData(lInitialCount, lMaximumCount);
    return InitHandle(SEMAPHORE_OBJ, semaphore, FreeSemaphoreData);
}

BOOL WINAPI ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, 
                            LPLONG lpPreviousCount)
{
    if (!hSemaphore || hSemaphore->type != SEMAPHORE_OBJ)
        return FALSE;
    
    _SemaphoreData *semaphore = (_SemaphoreData *)hSemaphore->ptr;
    pthread_mutex_lock(&semaphore->mutex);
    
    int previousCount = semaphore->count;
    semaphore->count = min(semaphore->count + lReleaseCount, semaphore->maxCount);
    
    if (lpPreviousCount)
        *lpPreviousCount = previousCount;
    
    pthread_cond_broadcast(&semaphore->cond);
    pthread_mutex_unlock(&semaphore->mutex);
    
    return TRUE;
}
```

### 4.5 事件 (Event)

#### 4.5.1 数据结构

```cpp
struct _EventData
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    BOOL manualReset;   // 手动重置 or 自动重置
    BOOL signaled;      // 信号状态
    
    _EventData(BOOL bManualReset, BOOL bInitialState)
        : manualReset(bManualReset), signaled(bInitialState)
    {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cond, NULL);
    }
};
```

#### 4.5.2 核心操作

```cpp
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, 
                          BOOL bManualReset, BOOL bInitialState, 
                          LPCSTR lpName)
{
    _EventData *event = new _EventData(bManualReset, bInitialState);
    return InitHandle(EVENT_OBJ, event, FreeEventData);
}

BOOL WINAPI SetEvent(HANDLE hEvent)
{
    if (!hEvent || hEvent->type != EVENT_OBJ)
        return FALSE;
    
    _EventData *event = (_EventData *)hEvent->ptr;
    pthread_mutex_lock(&event->mutex);
    event->signaled = TRUE;
    pthread_cond_broadcast(&event->cond);
    pthread_mutex_unlock(&event->mutex);
    
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE hEvent)
{
    if (!hEvent || hEvent->type != EVENT_OBJ)
        return FALSE;
    
    _EventData *event = (_EventData *)hEvent->ptr;
    pthread_mutex_lock(&event->mutex);
    event->signaled = FALSE;
    pthread_mutex_unlock(&event->mutex);
    
    return TRUE;
}
```

---

## 5. 命名对象与跨进程共享

### 5.1 全局句柄表

命名对象需要跨进程共享，swinx 使用共享内存实现全局句柄表：

```cpp
struct HandleData
{
    int type;                    // 对象类型
    int nRef;                    // 引用计数
    char szName[100];            // 对象名称
    char szNick[MAX_PATH + 1];   // 规范化名称
    union {
        EventData eventData;
        SemaphoreData semaphoreData;
        MutexData mutexData;
        FileMapData fmData;
        TimerData timerData;
    } data;
};

class GLobalHandleTable {
    SharedMemory *m_sharedMem;
    TableHeader *m_header;
    HandleData *m_table;
    uint32_t m_key;
    
public:
    GLobalHandleTable(const char *name, size_t size, uint32_t key);
    HandleData* getHandleData(uint32_t index);
    int findAvailableHandleDataIndex();
    int findExistedHandleDataIndex(const char* name, int type);
};

static GLobalHandleTable s_globalHandleTable(kGlobalShareMemName, 
                                             kGlobal_Handle_Size, 
                                             kKey_SharedMem);
```

### 5.2 命名对象基类

```cpp
struct NamedWaitbleObj : _SynHandle
{
    int index;              // 在全局表中的索引
    GlobalMutex mutex;      // 保护对象操作的互斥锁
    int fifo;               // FIFO 文件描述符
    
    NamedWaitbleObj()
        : fifo(-1), index(-1)
    {
    }

    ~NamedWaitbleObj()
    {
        if (index != -1)
        {
            s_globalHandleTable.getRwLock()->lockExclusive();
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            if (fifo != -1)
                close(fifo);
            if (--pData->nRef == 0)
            {
                unlink(pData->szName);  // 删除 FIFO
                pData->type = HUnknown;
                --s_globalHandleTable.getTableUsed();
            }
            s_globalHandleTable.getRwLock()->unlockExclusive();
        }
    }
};
```

### 5.3 命名对象创建流程

#### 5.3.1 创建命名事件

```cpp
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, 
                          BOOL bManualReset, BOOL bInitialState, 
                          LPCSTR lpName)
{
    if (!lpName)
    {
        // 匿名事件
        return createAnonymousEvent(bManualReset, bInitialState);
    }
    
    // 命名事件：检查是否已存在
    HANDLE exist = OpenExistObject(lpName, HNamedEvent);
    if (exist)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return exist;
    }
    
    // 创建新对象
    NamedEventObj *obj = new NamedEventObj;
    EventData data = { bManualReset };
    
    if (!obj->init(lpName, &data))
    {
        FreeSynObj(obj);
        return NULL;
    }
    
    return NewSynHandle(obj);
}
```

#### 5.3.2 打开已有对象

```cpp
HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, 
                        BOOL bInheritHandle, 
                        LPCSTR lpName)
{
    return OpenExistObject(lpName, HNamedEvent);
}

HANDLE OpenExistObject(const char *name, int type)
{
    HANDLE ret = 0;
    s_globalHandleTable.getRwLock()->lockExclusive();
    int idx = s_globalHandleTable.findExistedHandleDataIndex(name, type);
    
    if (idx != -1)
    {
        // 对象已存在，创建新引用
        switch (type)
        {
        case HNamedEvent:
        {
            NamedEventObj *new_obj = new NamedEventObj();
            new_obj->init2(idx);  // 从全局表初始化
            ret = NewSynHandle(new_obj);
        }
        break;
        // ... 其他类型
        }
        SetLastError(ERROR_ALREADY_EXISTS);
    }
    s_globalHandleTable.getRwLock()->unlockExclusive();
    return ret;
}
```

### 5.4 FIFO 通信机制

命名对象使用 FIFO（命名管道）实现跨进程信号传递：

```cpp
struct NamedWaitbleObj : _SynHandle
{
    int fifo;  // FIFO 文件描述符
    
    bool init(LPCSTR pszName, void *initData) override
    {
        // 1. 在全局表中分配条目
        index = s_globalHandleTable.allocHandleData();
        
        // 2. 创建 FIFO
        char fifoName[MAX_PATH];
        snprintf(fifoName, MAX_PATH, "%s.fifo", pszName);
        mkfifo(fifoName, 0666);
        fifo = open(fifoName, O_RDWR);
        
        // 3. 注册到全局表
        HandleData *pData = s_globalHandleTable.getHandleData(index);
        pData->type = type;
        strcpy(pData->szName, fifoName);
        
        return true;
    }
};
```

---

## 6. 等待机制实现（基于 Select）

### 6.1 核心设计思想

Swinx **不使用 pthread_cond_wait**，而是采用 **select + pipe/fifo** 的机制实现等待功能：

- **匿名对象**: 使用 `pipe(fd[2])` 创建管道
- **命名对象**: 使用 FIFO（命名管道）
- **信号通知**: 通过 `write(fd, "a", 1)` 写入单字节数据
- **等待检测**: 使用 `select()` 监测文件描述符可读事件

### 6.2 基础信号机制

所有同步对象都继承自 `_SynHandle`，提供统一的信号读写接口：

```cpp
struct _SynHandle
{
    // 读取信号（非阻塞）
    bool readSignal(bool bLock = true)
    {
        if (bLock)
            lock();
        
        fd_set readfds;
        int fd = getReadFd();
        struct timeval timeout = {0, 0};
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        // 使用 select 检测是否可读
        select(fd + 1, &readfds, NULL, NULL, &timeout);
        bool bSignal = FD_ISSET(fd, &readfds);
        
        if (bSignal)
        {
            char buffer[2] = {0};
            ssize_t len = read(fd, buffer, 1);  // 读取信号字节
        }
        
        if (bLock)
            unlock();
        return bSignal;
    }
    
    // 写入信号
    bool writeSignal()
    {
        int fd = getWriteFd();
        return write(fd, "a", 1) == 1;  // 写入单字节
    }
};
```

### 6.3 WaitForSingleObject 实现

**完全基于 select 实现**：

```cpp
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
    int fd = -1;
    s_globalHandleTable.getRwLock()->lockShared();
    HANDLE hTmp = AddHandleRef(hHandle);
    _SynHandle *synObj = GetSynHandle(hHandle);
    fd = synObj->getReadFd();  // 获取对象的文件描述符
    s_globalHandleTable.getRwLock()->unlockShared();
    
    if (fd == -1)
    {
        CloseHandle(hTmp);
        return WAIT_ABANDONED;
    }
    
start_wait:
    uint64_t ts1 = GetTickCount64();
    
    // 调用 select 等待
    int ret = selectfds(&fd, 1, dwMilliseconds, nullptr);
    
    if (ret == 0)
    {
        CloseHandle(hTmp);
        return WAIT_TIMEOUT;
    }
    else if (ret < 0)
    {
        CloseHandle(hTmp);
        return WAIT_ABANDONED;
    }
    else
    {
        // select 返回就绪，处理对象特定逻辑
        if (synObj->onWaitDone())
        {
            CloseHandle(hTmp);
            return WAIT_OBJECT_0;
        }
        else
        {
            // 未完全就绪（如自动重置事件），重新等待
            if (dwMilliseconds != INFINITE)
            {
                uint64_t ts2 = GetTickCount64();
                if (ts2 - ts1 > dwMilliseconds)
                {
                    CloseHandle(hTmp);
                    return WAIT_TIMEOUT;
                }
                dwMilliseconds -= ts2 - ts1;
            }
            goto start_wait;  // 循环等待
        }
    }
}
```

### 6.4 selectfds 辅助函数

封装 select 调用，支持超时和状态管理：

```cpp
static int selectfds(int *fds, int nCount, DWORD timeoutMs, bool *states)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    
    // 构建读集合
    for (int i = 0; i < nCount; i++)
    {
        if ((fds[i] != -1) && (!states || !states[i]))
        {
            FD_SET(fds[i], &read_fds);
            max_fd = std::max(max_fd, fds[i]);
        }
    }

    // 调用 select
    int ret = 0;
    if (timeoutMs == INFINITE)
        ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    else
    {
        timeval val;
        val.tv_sec = timeoutMs / 1000;
        val.tv_usec = (timeoutMs % 1000) * 1000;
        ret = select(max_fd + 1, &read_fds, NULL, NULL, &val);
    }
    
    // 更新状态
    if (ret > 0 && states)
    {
        for (int i = 0; i < nCount; i++)
        {
            if (!states[i] && fds[i] != -1)
            {
                states[i] = FD_ISSET(fds[i], &read_fds);
            }
        }
    }
    return ret;
}
```

### 6.5 WaitForMultipleObjects 实现

基于 select 同时等待多个文件描述符：

```cpp
DWORD WINAPI WaitForMultipleObjects(DWORD nCount, 
                                    const HANDLE *lpHandles, 
                                    BOOL bWaitAll, 
                                    DWORD dwMilliseconds)
{
    if (nCount > MAXIMUM_WAIT_OBJECTS)
        return 0;
    
    int fd[MAXIMUM_WAIT_OBJECTS] = {0};
    bool states[MAXIMUM_WAIT_OBJECTS] = {false};
    HANDLE tmpHandles[MAXIMUM_WAIT_OBJECTS] = {0};
    
    // 收集所有文件描述符
    s_globalHandleTable.getRwLock()->lockShared();
    for (int i = 0; i < nCount; i++)
    {
        if (lpHandles[i] != INVALID_HANDLE_VALUE)
        {
            tmpHandles[i] = AddHandleRef(lpHandles[i]);
            _SynHandle *e = GetSynHandle(tmpHandles[i]);
            fd[i] = e->getReadFd();
        }
        else
        {
            fd[i] = -1;
            states[i] = true;  // 无效句柄视为已就绪
            tmpHandles[i] = INVALID_HANDLE_VALUE;
        }
    }
    s_globalHandleTable.getRwLock()->unlockShared();

start_wait:
    DWORD dwToInit = dwMilliseconds;
    uint64_t ts1_all = GetTickCount64();
    int nRet = 0;
    
    for (;;)
    {
        // 调用 select 等待多个 FD
        nRet = selectfds(fd, nCount, dwMilliseconds, states);
        
        if (nRet <= 0)
        {
            nRet = (nRet == 0) ? WAIT_TIMEOUT : WAIT_ABANDONED;
            break;
        }
        
        if (!bWaitAll)
        {
            // 等待任一：返回第一个就绪的对象
            for (int i = 0; i < nCount; i++)
            {
                if (states[i])
                {
                    nRet = WAIT_OBJECT_0 + i;
                    break;
                }
            }
            break;
        }
        else
        {
            // 等待所有：检查是否全部就绪
            bool bAllReady = true;
            for (int i = 0; i < nCount; i++)
            {
                if (!states[i])
                {
                    bAllReady = false;
                    break;
                }
            }
            
            if (bAllReady)
            {
                nRet = WAIT_OBJECT_0;
                break;
            }
            
            // 继续等待剩余对象
            uint64_t ts2 = GetTickCount64();
            if (dwMilliseconds != INFINITE)
            {
                if (ts2 - ts1 > dwMilliseconds)
                {
                    nRet = WAIT_TIMEOUT;
                    break;
                }
                dwMilliseconds -= ts2 - ts1;
            }
        }
    }
    
    // 验证并清理对象状态
    if (nRet >= WAIT_OBJECT_0)
    {
        bool bValid = true;
        for (int i = 0; i < nCount; i++)
        {
            if (states[i] && tmpHandles[i] != INVALID_HANDLE_VALUE)
            {
                if (!GetSynHandle(tmpHandles[i])->onWaitDone())
                {
                    states[i] = false;
                    bValid = false;
                    break;
                }
            }
        }
        
        if (!bValid)
        {
            // 状态失效，重新等待
            uint64_t ts2_all = GetTickCount64();
            if (dwToInit != INFINITE)
            {
                if (ts2_all - ts1_all > dwToInit)
                {
                    nRet = WAIT_TIMEOUT;
                }
                else
                {
                    dwMilliseconds = dwToInit - (ts2_all - ts1_all);
                }
            }
            if (nRet != WAIT_TIMEOUT)
                goto start_wait;
        }
    }

    // 清理临时句柄
    for (int i = 0; i < nCount; i++)
    {
        CloseHandle(tmpHandles[i]);
    }

    return nRet;
}
```

### 6.6 对象特定的 onWaitDone 实现

不同对象类型在 select 返回后有不同的处理逻辑：

#### 6.6.1 事件对象

```cpp
template <typename T>
struct EventOp : T
{
    bool onWaitDone() override
    {
        EventData *data = (EventData *)T::getData();
        if (data->bManualReset)
            return true;  // 手动重置：保持信号
        
        // 自动重置：清除所有信号
        int signals = 0;
        while (T::readSignal())
            signals++;
        return signals > 0;
    }
};
```

#### 6.6.2 信号量对象

```cpp
template <typename T>
struct SemaphoreOp : T
{
    bool onWaitDone() override
    {
        bool ret = false;
        T::lock();
        
        SemaphoreData *data = (SemaphoreData *)T::getData();
        if (data->sigCount > 0)
        {
            ret = T::readSignal(false);
            if (ret)
            {
                data->sigCount--;  // 减少计数
            }
        }
        
        T::unlock();
        return ret;
    }
};
```

#### 6.6.3 互斥量对象

```cpp
template <typename T>
struct MutexOp : T
{
    bool onWaitDone() override
    {
        bool ret = T::readSignal();
        if (!ret)
            return false;
        
        MutexData *data = (MutexData *)T::getData();
        data->tid_owner = GetCurrentThreadId();  // 记录所有者
        return true;
    }
};
```

### 6.7 信号触发机制

当对象被触发时（如 SetEvent、ReleaseSemaphore），通过写管道唤醒等待者：

```cpp
// 示例：SetEvent 实现
BOOL WINAPI SetEvent(HANDLE hEvent)
{
    if (!hEvent || hEvent->type != EVENT_OBJ)
        return FALSE;
    
    _EventData *event = (_EventData *)hEvent->ptr;
    pthread_mutex_lock(&event->mutex);
    event->signaled = TRUE;
    
    // 关键：通过管道发送信号
    // 对于匿名对象：write(fd[1], "a", 1)
    // 对于命名对象：write(fifo, "a", 1)
    hEvent->writeSignal();  
    
    pthread_mutex_unlock(&event->mutex);
    return TRUE;
}
```

### 6.8 匿名对象的管道创建

```cpp
struct NoNameWaitbleObj : _SynHandle
{
    std::recursive_mutex mutex;
    int fd[2];  // 管道文件描述符
    
    NoNameWaitbleObj()
    {
        fd[0] = fd[1] = -1;
    }

    bool init(LPCSTR pszName, void *initData) override
    {
        // 创建无名管道
        bool bRet = pipe(fd) != -1;
        if (!bRet)
            return false;
        onInit(nullptr, initData);
        return true;
    }
    
    int getReadFd() override { return fd[0]; }
    int getWriteFd() override { return fd[1]; }
};
```

### 6.9 命名对象的 FIFO 创建

```cpp
struct NamedWaitbleObj : _SynHandle
{
    int index;   // 全局表索引
    GlobalMutex mutex;
    int fifo;    // FIFO 文件描述符
    
    bool init(LPCSTR pszName, void *initData) override
    {
        // 在全局表中注册
        index = s_globalHandleTable.allocHandleData();
        
        // 创建 FIFO
        HandleData *pData = s_globalHandleTable.getHandleData(index);
        mkdir(kFifoRoot, 0666);
        int ret = mkfifo(pData->szName, 0666);
        
        // 打开 FIFO（读写模式避免阻塞）
        fifo = open(pData->szName, O_RDWR);
        
        return true;
    }
    
    int getReadFd() override { return fifo; }
    int getWriteFd() override { return fifo; }
};
```

---


## 7. 关键数据结构

### 7.1 对象类型层次

```
_SynHandle (跨平台同步对象基类)
├── PipeSynHandle (基于 pipe 的实现)
│   └── NamedWaitbleObj (命名对象基类)
│       ├── NamedEvent (命名事件)
│       ├── NamedSemaphore (命名信号量)
│       ├── NamedMutex (命名互斥量)
│       └── NamedTimer (命名定时器)
└── NoNameWaitbleObj (匿名对象基类)
    ├── NoNameEvent (匿名事件)
    ├── NoNameSemaphore (匿名信号量)
    ├── NoNameMutex (匿名互斥量)
    └── NoNameTimer (匿名定时器)
```

### 7.2 模板装饰器模式

```cpp
// 基础对象
struct NoNameEvent : NoNameWaitbleObj, EventData {
    type = HEvent;
    void onInit(HandleData*, void*) override { ... }
    void* getData() override { return this; }
};

// 添加事件特定操作
template <typename T>
struct EventOp : T {
    bool onWaitDone() override {
        EventData *data = (EventData*)T::getData();
        if (data->bManualReset) return true;
        // 自动重置：清除所有信号
        int signals = 0;
        while (T::readSignal()) signals++;
        return signals > 0;
    }
};

// 最终类型
typedef EventOp<NoNameEvent> NoNameEventObj;
typedef EventOp<NamedEvent> NamedEventObj;
```

### 7.3 共享内存布局

```
共享内存区域 (64KB)
├── TableHeader (12 bytes)
│   ├── key (4 bytes): 共享内存密钥
│   ├── totalSize (4 bytes): 总条目数
│   └── used (4 bytes): 已使用条目数
└── HandleData 数组
    ├── HandleData[0]
    ├── HandleData[1]
    └── ...
```

---

## 8. 实现示例

### 8.1 完整示例：创建和使用命名事件

```cpp
// 进程 A: 创建命名事件并触发
HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, "MySharedEvent");
if (hEvent)
{
    // 触发事件
    SetEvent(hEvent);
    
    // 等待一段时间后关闭
    Sleep(1000);
    CloseHandle(hEvent);
}

// 进程 B: 打开并使用同一事件
HANDLE hEvent2 = OpenEventA(EVENT_MODIFY_STATE, FALSE, "MySharedEvent");
if (hEvent2)
{
    // 等待事件
    DWORD ret = WaitForSingleObject(hEvent2, INFINITE);
    if (ret == WAIT_OBJECT_0)
    {
        // 事件被触发
        printf("Event signaled!\n");
    }
    CloseHandle(hEvent2);
}
```

### 8.2 完整示例：使用信号量控制资源访问

```cpp
// 创建信号量，初始计数 3，最大计数 5
HANDLE hSem = CreateSemaphoreA(NULL, 3, 5, "ResourcePool");

// 工作线程
DWORD WINAPI WorkerThread(LPVOID param)
{
    // 等待获取资源
    DWORD ret = WaitForSingleObject(hSem, INFINITE);
    if (ret == WAIT_OBJECT_0)
    {
        // 使用资源...
        printf("Thread %d got resource\n", GetCurrentThreadId());
        Sleep(1000);
        
        // 释放资源
        ReleaseSemaphore(hSem, 1, NULL);
    }
    return 0;
}

// 创建多个工作线程
for (int i = 0; i < 10; i++)
{
    CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
}

// 主线程等待
Sleep(5000);
CloseHandle(hSem);
```

### 8.3 完整示例：跨进程互斥量

```cpp
// 进程 A 和进程 B 使用同一互斥量
HANDLE hMutex = CreateMutexA(NULL, FALSE, "GlobalMutex");

// 进入临界区
WaitForSingleObject(hMutex, INFINITE);

// ... 执行临界区代码 ...

// 离开临界区
ReleaseMutex(hMutex);

// 清理
CloseHandle(hMutex);
```

---

## 9. 总结

### 9.1 技术亮点

1. **统一的对象模型**: 通过 `_Handle` 结构统一管理所有内核对象类型
2. **双模实现策略**: 匿名对象使用 pthread 高效实现，命名对象使用 FIFO+ 共享内存
3. **模板元编程**: 使用 CRTP 模式实现代码复用和类型安全
4. **跨进程共享**: 通过共享内存全局表实现真正的跨进程对象语义
5. **RAII 资源管理**: 引用计数和自动资源清理机制

### 9.2 性能优化

- **匿名对象零开销**: 直接使用 pthread 原语，无额外开销
- **命名对象延迟创建**: FIFO 仅在需要时创建
- **引用计数优化**: 使用原子操作减少锁竞争
- **等待多路复用**: 使用 select/poll 实现高效的 WaitForMultipleObjects

### 9.3 兼容性保证

- ✅ 完全兼容 Windows API 函数签名
- ✅ 支持 ANSI 和 Unicode 版本
- ✅ 正确的错误码设置（SetLastError）
- ✅ 支持跨进程对象共享语义
- ✅ 线程安全的对象生命周期管理

### 9.4 局限性与改进方向

**当前局限性**：
- 命名对象依赖文件系统（FIFO）
- 不支持安全属性（SECURITY_ATTRIBUTES）
- 部分高级特性未实现（如互斥量的 abandoning 状态）

**未来改进方向**：
- 使用 POSIX 共享内存替代文件系统 FIFO
- 实现完整的安全描述符支持
- 添加性能监控和调试工具
- 支持更多 Windows 内核对象类型（Job Object, IO Completion Port 等）

---

## 附录 A: 文件清单

### 核心实现文件

| 文件路径 | 说明 |
|---------|------|
| `src/handle.cpp` | 基础 HANDLE 管理 |
| `src/handle.h` | HANDLE 结构定义 |
| `src/syncapi.cpp` | 同步对象 API（匿名） |
| `src/sysobjs.cpp` | 命名对象和全局表 |
| `src/sysapi.cpp` | 系统 API 实现 |
| `src/synhandle.h` | 同步对象辅助类 |
| `include/sysapi.h` | API 头文件 |

### 平台相关实现

| 文件路径 | 说明 |
|---------|------|
| `src/platform/linux/` | Linux 平台特定实现 |
| `src/platform/cocoa/` | macOS Cocoa 实现 |

---

## 附录 B: 术语表

| 术语 | 说明 |
|-----|------|
| HANDLE | Windows 内核对象句柄 |
| SYN_OBJ | 同步对象类型标识 |
| FIFO | First In First Out 命名管道 |
| CRTP | Curiously Recurring Template Pattern |
| RAII | Resource Acquisition Is Initialization |
| SRW Lock | Slim Reader-Writer Lock |

---

**文档版本**: 1.0  
**最后更新**: 2026-03-24  
**作者**: SOUI Team  
**参考源代码**: swinx (https://gitee.com/setoutsoft/swinx)
