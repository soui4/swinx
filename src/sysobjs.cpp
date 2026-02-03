#include <windows.h>
#include <sys/stat.h>
#include <mutex>
#include <map>
#include <string>
#include <atomic>
#include <assert.h>
#include <fcntl.h>
#include <semaphore.h>
#include <uuid/uuid.h>
#include <sys/mman.h>
#include <locale.h>
#include "tostring.hpp"
#include "sharedmem.h"
#include "handle.h"
#include "synhandle.h"
#include "uimsg.h"
using namespace swinx;

enum
{
    kKey_SharedMem = 0x00110207, // key for shared memory
    kGlobal_Handle_Size = 65535, // global handle size
};

static const char *kFifoRoot = "/tmp";
#ifdef __APPLE__
static const char *kFifoNameTemplate = "/tmp/soui-B699-A95AB24431E1.%d";
static const char *kFileMapNameTemplate = "/shm-soui-B699-A95AB24431E1.%d";
static const char *kGlobalShareMemName = "/share_soui_A95AB24431E7";
#else
static const char *kFifoNameTemplate = "/tmp/soui-2BACFFE6-9ED7-4AC8-B699-A95AB24431E1.%d";
static const char *kFileMapNameTemplate = "/shm-soui-2BACFFE6-9ED7-4AC8-B699-A95AB24431E1.%d";
static const char *kGlobalShareMemName = "/share_mem_soui-2BACFFE6-9ED7-4AC8-B699-A95AB24431E7";
#endif //__APPLE__
struct EventData
{
    BOOL bManualReset;
};
struct SemaphoreData
{
    LONG sigCount;
    LONG maxSignal;
};

struct MutexData
{
    tid_t tid_owner;
};

struct FileMapData
{
    UINT uFlags;
    LARGE_INTEGER size;
    LARGE_INTEGER offset;
    LARGE_INTEGER mappedSize;
};

struct TimerData
{
    BOOL bManualReset;
    BOOL bPeriodic;
    LARGE_INTEGER liDueTime;
    LONG lPeriod;
    HANDLE hTimerQueue;
    PVOID lpCompletionRoutine;
    PVOID lpArgToCompletionRoutine;
    BOOL fResume;
};

struct HandleData
{
    int type;
    int nRef;
    char szName[100];
    char szNick[MAX_PATH + 1];
    union {
        EventData eventData;
        SemaphoreData semaphoreData;
        MutexData mutexData;
        FileMapData fmData;
        TimerData timerData;
    } data;
};

#pragma pack(push, 4)
struct TableHeader
{
    uint32_t key;
    uint32_t totalSize;
    uint32_t used;
};
#pragma pack(pop)

/*--------------------------------------------------------------
global table layout:
table total length: 4 bytes
table used : 4 bytes
next mutex key: 4 bytes
table data: HandleData *
---------------------------------------------------------------*/
class GLobalHandleTable {
    SharedMemory *m_sharedMem;
    TableHeader *m_header;
    HandleData *m_table;
    uint32_t m_key;

  public:
    GLobalHandleTable(const char *name, int maxObjects, uint32_t key)
        : m_header(nullptr)
        , m_table(nullptr)
    {
        setlocale(LC_ALL, "zh_CN.UTF-8"); // init locale

        m_sharedMem = new SharedMemory();
        SharedMemory::InitStat ret = m_sharedMem->init(name, maxObjects * sizeof(HandleData) + sizeof(TableHeader));
        assert(ret);
        if (!ret)
        {
            delete m_sharedMem;
            m_sharedMem = nullptr;
        }
        else
        {
            getRwLock()->lockExclusive();
            m_header = (TableHeader *)m_sharedMem->buffer();
            m_table = (HandleData *)(m_header + 1);
            if (ret == SharedMemory::Existed)
            {
                if (m_header->key != key || m_header->totalSize != maxObjects)
                {
                    getRwLock()->unlockExclusive();
                    delete m_sharedMem;
                    m_sharedMem = nullptr;
                    assert(false);
                    return;
                }
            }
            else
            {
                // init global table
                m_header->key = key;
                m_header->totalSize = maxObjects;
                m_header->used = 0;
                HandleData *tbl = m_table;
                for (int i = 0; i < maxObjects; i++)
                {
                    tbl->type = HUnknown;
                    tbl++;
                }
            }
            m_key = key;
            getRwLock()->unlockExclusive();
        }
    }
    ~GLobalHandleTable()
    {
        if (m_sharedMem)
        {
            assert(m_key == m_header->key);
            delete m_sharedMem;
            m_sharedMem = nullptr;
        }
    };

    TableHeader *getHeader()
    {
        return m_header;
    }
    HandleData *getHandleData(int iHandle)
    {
        assert(m_sharedMem);
        return m_table + iHandle;
    }

    int findExistedHandleDataIndex(const char *name, int type)
    {
        HandleData *data = getHandleData(0);
        int objs = 0;
        for (uint32_t i = 0, maxObj = getHeader()->totalSize; i < maxObj; i++, data++)
        {
            if (data->type == type && strcmp(data->szNick, name) == 0)
            {
                return i;
            }
            if (data->type != HUnknown)
                objs++;
            if (objs == getTableUsed())
                break;
        }
        return -1;
    }

    int findAvailableHandleDataIndex()
    {
        HandleData *data = getHandleData(0);
        for (uint32_t i = 0, maxObj = getHeader()->totalSize; i < maxObj; i++)
        {
            if (data->type == HUnknown)
            {
                return i;
            }
            data++;
        }
        return -1;
    }

    ISemRwLock *getRwLock()
    {
        return m_sharedMem->getRwLock();
    }

    uint32_t &getTableUsed()
    {
        assert(m_sharedMem);
        return getHeader()->used;
    }
};

static GLobalHandleTable s_globalHandleTable(kGlobalShareMemName, kGlobal_Handle_Size, kKey_SharedMem);

struct GlobalMutex : TSemRwLock<1>
{
    void lock()
    {
        lockExclusive();
    }
    void unlock()
    {
        unlockExclusive();
    }
};

struct NoNameWaitbleObj : _SynHandle
{
    std::recursive_mutex mutex;
    int fd[2];
    NoNameWaitbleObj()
    {
        fd[0] = fd[1] = -1;
    }

    ~NoNameWaitbleObj()
    {
        if (fd[0] != -1)
            close(fd[0]);
        if (fd[1] != -1)
            close(fd[1]);
    }

    void lock() override
    {
        mutex.lock();
    }
    void unlock() override
    {
        mutex.unlock();
    }
    int getReadFd() override
    {
        return fd[0];
    }

    int getWriteFd() override
    {
        return fd[1];
    }

    bool init(LPCSTR pszName, void *initData) override
    {
        bool bRet = pipe(fd) != -1;
        if (!bRet)
            return false;
        onInit(nullptr, initData);
        return true;
    }
    virtual void onInit(HandleData *pData, void *initData) = 0;

    virtual LPCSTR getName() const override
    {
        return nullptr;
    }
};

struct NamedWaitbleObj : _SynHandle
{
    int index;
    GlobalMutex mutex;
    int fifo;
    NamedWaitbleObj()
        : fifo(-1)
        , index(-1)
    {
    }

    ~NamedWaitbleObj()
    {
        if (index != -1)
        {
            // update global table
            s_globalHandleTable.getRwLock()->lockExclusive();
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            if (fifo != -1)
            {
                close(fifo);
            }
            if (--pData->nRef == 0)
            {
                unlink(pData->szName);
                pData->type = HUnknown;
                --s_globalHandleTable.getTableUsed();
            }
            s_globalHandleTable.getRwLock()->unlockExclusive();
        }
    }

    void lock() override
    {
        mutex.lock();
    }
    void unlock() override
    {
        mutex.unlock();
    }

    int getReadFd() override
    {
        return fifo;
    }

    int getWriteFd() override
    {
        return fifo;
    }

    HandleData *handleData()
    {
        assert(index != -1);
        return s_globalHandleTable.getHandleData(index);
    }

    bool init2(int idx)
    {
        HandleData *pData = s_globalHandleTable.getHandleData(idx);
        errno = NOERROR;

        mkdir(kFifoRoot, 0666); // make sure fifo root is valid
        int ret = mkfifo(pData->szName, 0666);
        if (ret != NOERROR && errno != EEXIST)
        {
            perror("mkfifo");
            return false;
        }
        fifo = open(pData->szName, O_RDWR);
        if (fifo == -1)
        {
            perror("open fifo");
            return false;
        }
        pData->nRef++;
        mutex.init(s_globalHandleTable.getHeader()->key + idx + 10000);
        this->index = idx;
        return true;
    }

    bool init(LPCSTR pszName, void *initData) override
    {
        s_globalHandleTable.getRwLock()->lockExclusive();
        bool bExisted = true;
        int index = s_globalHandleTable.findExistedHandleDataIndex(pszName, type);
        if (index == -1)
        {
            s_globalHandleTable.getTableUsed()++;
            index = s_globalHandleTable.findAvailableHandleDataIndex();
            assert(index != -1);
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            strcpy(pData->szNick, pszName);
            sprintf(pData->szName, kFifoNameTemplate, index);
            pData->type = type;
            pData->nRef = 0;
            onInit(pData, initData);
            bExisted = false;
        }
        assert(index != -1);
        if (!init2(index))
        {
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            pData->type = HUnknown;
            pData->nRef = 0;
            s_globalHandleTable.getTableUsed()--;
            s_globalHandleTable.getRwLock()->unlockExclusive();
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        s_globalHandleTable.getRwLock()->unlockExclusive();
        return true;
    }

    virtual void onInit(HandleData *pData, void *initData) = 0;

    virtual LPCSTR getName() const override
    {
        if (index == -1)
        {
            return nullptr;
        }
        else
        {
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            return pData->szNick;
        }
    }
};

struct NoNameEvent
    : NoNameWaitbleObj
    , EventData
{
    NoNameEvent()
    {
        type = HEvent;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy((EventData *)this, initData, sizeof(EventData));
    }
    void *getData() override
    {
        return (EventData *)this;
    }
};

struct NamedEvent : NamedWaitbleObj
{
    NamedEvent()
    {
        type = HNamedEvent;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy(&pData->data.eventData, initData, sizeof(EventData));
    }
    void *getData() override
    {
        return &handleData()->data.eventData;
    }
};

struct NoNameSemaphore
    : NoNameWaitbleObj
    , SemaphoreData
{
    NoNameSemaphore()
    {
        type = HSemaphore;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy((SemaphoreData *)this, initData, sizeof(SemaphoreData));
    }
    void *getData() override
    {
        return (SemaphoreData *)this;
    }
};

struct NamedSemaphore : NamedWaitbleObj
{
    NamedSemaphore()
    {
        type = HNamedSemaphore;
    }

    void onInit(HandleData *pData, void *initData) override
    {
        memcpy(&pData->data.semaphoreData, initData, sizeof(SemaphoreData));
    }

    void *getData() override
    {
        return &handleData()->data.semaphoreData;
    }
};

struct NoNameMutex
    : NoNameWaitbleObj
    , MutexData
{
    NoNameMutex()
    {
        type = HMutex;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy((MutexData *)this, initData, sizeof(MutexData));
    }
    void *getData() override
    {
        return (MutexData *)this;
    }
};

struct NamedMutex : NamedWaitbleObj
{
    NamedMutex()
    {
        type = HNamedMutex;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy(&pData->data.mutexData, initData, sizeof(MutexData));
    }
    void *getData() override
    {
        return &handleData()->data.mutexData;
    }
};

template <typename T>
struct EventOp : T
{
    bool onWaitDone() override
    {
        EventData *data = (EventData *)T::getData();
        if (data->bManualReset)
            return true;
        // clear all signals
        int signals = 0;
        while (T::readSignal())
            signals++;
        return signals > 0;
    }
};

template <typename T>
struct SemaphoreOp : T
{
    bool init(LPCSTR pszName, void *initData) override
    {
        if (!T::init(pszName, initData))
            return false;
        SemaphoreData *data = (SemaphoreData *)T::getData();
        for (LONG i = 0, cnt = data->sigCount; i < cnt; i++)
        {
            T::writeSignal();
        }
        return true;
    }

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
                data->sigCount--;
            }
        }
        T::unlock();
        return ret;
    }
};

template <typename T>
struct MutexOp : T
{
    bool onWaitDone() override
    {
        bool ret = T::readSignal();
        if (!ret)
            return false;
        MutexData *data = (MutexData *)T::getData();
        data->tid_owner = GetCurrentThreadId();
        return true;
    }
};

typedef EventOp<NoNameEvent> NoNameEventObj;
typedef EventOp<NamedEvent> NamedEventObj;
typedef SemaphoreOp<NoNameSemaphore> NoNameSemaphoreObj;
typedef SemaphoreOp<NamedSemaphore> NamedSemaphoreObj;
typedef MutexOp<NoNameMutex> NoNameMutexObj;
typedef MutexOp<NamedMutex> NamedMutexObj;

struct NoNameTimer
    : NoNameWaitbleObj
    , TimerData
{
    NoNameTimer()
    {
        type = HTimer;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy((TimerData *)this, initData, sizeof(TimerData));
    }
    void *getData() override
    {
        return (TimerData *)this;
    }
};

struct NamedTimer : NamedWaitbleObj
{
    NamedTimer()
    {
        type = HNamedTimer;
    }
    void onInit(HandleData *pData, void *initData) override
    {
        memcpy(&pData->data.timerData, initData, sizeof(TimerData));
    }
    void *getData() override
    {
        return &handleData()->data.timerData;
    }
};

template <typename T>
struct TimerOp : T
{
    bool onWaitDone() override
    {
        TimerData *data = (TimerData *)T::getData();
        if (data->bManualReset)
            return true;
        // clear all signals
        int signals = 0;
        while (T::readSignal())
            signals++;
        return signals > 0;
    }
};

typedef TimerOp<NoNameTimer> NoNameTimerObj;
typedef TimerOp<NamedTimer> NamedTimerObj;

//------------------------------------------------------------
// Timer related APIs

HANDLE WINAPI CreateWaitableTimerA(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCSTR lpTimerName)
{
    if (lpTimerName && *lpTimerName)
    {
        NamedTimerObj *timer = new NamedTimerObj();
        TimerData data = { bManualReset, FALSE, {0}, 0, NULL, NULL, NULL, FALSE };
        if (!timer->init(lpTimerName, &data))
        {
            delete timer;
            return NULL;
        }
        return NewSynHandle(timer);
    }
    else
    {
        NoNameTimerObj *timer = new NoNameTimerObj();
        TimerData data = { bManualReset, FALSE, {0}, 0, NULL, NULL, NULL, FALSE };
        if (!timer->init(NULL, &data))
        {
            delete timer;
            return NULL;
        }
        return NewSynHandle(timer);
    }
}

HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, LPCWSTR lpTimerName)
{
    if (lpTimerName && *lpTimerName)
    {
        std::string strName;
        tostring(lpTimerName, -1, strName);
        NamedTimerObj *timer = new NamedTimerObj();
        TimerData data = { bManualReset, FALSE, {0}, 0, NULL, NULL, NULL, FALSE };
        if (!timer->init(strName.c_str(), &data))
        {
            delete timer;
            return NULL;
        }
        return NewSynHandle(timer);
    }
    else
    {
        NoNameTimerObj *timer = new NoNameTimerObj();
        TimerData data = { bManualReset, FALSE, {0}, 0, NULL, NULL, NULL, FALSE };
        if (!timer->init(NULL, &data))
        {
            delete timer;
            return NULL;
        }
        return NewSynHandle(timer);
    }
}

BOOL WINAPI SetWaitableTimer(HANDLE hTimer, const LARGE_INTEGER *lpDueTime, LONG lPeriod, PTIMERAPCROUTINE lpCompletionRoutine, LPVOID lpArgToCompletionRoutine, BOOL fResume)
{
    _SynHandle *synHandle = GetSynHandle(hTimer);
    if (!synHandle)
        return FALSE;

    TimerData *data = (TimerData *)synHandle->getData();
    if (!data)
        return FALSE;

    data->bPeriodic = (lPeriod != 0);
    if (lpDueTime)
        data->liDueTime = *lpDueTime;
    data->lPeriod = lPeriod;
    data->lpCompletionRoutine = (PVOID)lpCompletionRoutine;
    data->lpArgToCompletionRoutine = lpArgToCompletionRoutine;
    data->fResume = fResume;

    // For simplicity, we just signal the timer immediately
    // In a real implementation, we would use a thread or timerfd to handle the actual timing
    synHandle->writeSignal();

    return TRUE;
}

BOOL WINAPI CancelWaitableTimer(HANDLE hTimer)
{
    _SynHandle *synHandle = GetSynHandle(hTimer);
    if (!synHandle)
        return FALSE;

    TimerData *data = (TimerData *)synHandle->getData();
    if (!data)
        return FALSE;

    data->bPeriodic = FALSE;
    data->lPeriod = 0;
    data->lpCompletionRoutine = NULL;
    data->lpArgToCompletionRoutine = NULL;

    // Clear any pending signals
    while (synHandle->readSignal());

    return TRUE;
}

// Timer Queue implementation
class TimerQueue
{
public:
    TimerQueue() : m_nextTimerId(1)
    {
    }
    
    ~TimerQueue()
    {
        // Clean up all timers
        for (auto &timer : m_timers)
        {
            CancelWaitableTimer(timer.second.hTimer);
            CloseHandle(timer.second.hTimer);
        }
        m_timers.clear();
    }
    
    BOOL CreateTimer(PHANDLE phNewTimer, WAITORTIMERCALLBACK Callback, PVOID Parameter, DWORD DueTime, DWORD Period, ULONG Flags)
    {
        HANDLE hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
        if (!hTimer)
            return FALSE;
        
        TimerInfo info;
        info.hTimer = hTimer;
        info.Callback = Callback;
        info.Parameter = Parameter;
        info.DueTime = DueTime;
        info.Period = Period;
        info.Flags = Flags;
        
        UINT timerId = m_nextTimerId++;
        m_timers[timerId] = info;
        
        *phNewTimer = (HANDLE)(UINT_PTR)timerId;
        
        LARGE_INTEGER liDueTime;
        liDueTime.QuadPart = -((LONGLONG)DueTime * 10000); // Convert to 100ns intervals
        return SetWaitableTimer(hTimer, &liDueTime, Period, NULL, NULL, FALSE);
    }
    
    BOOL ChangeTimer(HANDLE hTimer, DWORD DueTime, DWORD Period)
    {
        UINT timerId = (UINT)(UINT_PTR)hTimer;
        auto it = m_timers.find(timerId);
        if (it == m_timers.end())
            return FALSE;
        
        it->second.DueTime = DueTime;
        it->second.Period = Period;
        
        LARGE_INTEGER liDueTime;
        liDueTime.QuadPart = -((LONGLONG)DueTime * 10000); // Convert to 100ns intervals
        return SetWaitableTimer(it->second.hTimer, &liDueTime, Period, NULL, NULL, FALSE);
    }
    
    BOOL DeleteTimer(HANDLE hTimer, HANDLE hCompletionEvent)
    {
        UINT timerId = (UINT)(UINT_PTR)hTimer;
        auto it = m_timers.find(timerId);
        if (it == m_timers.end())
            return FALSE;
        
        CancelWaitableTimer(it->second.hTimer);
        CloseHandle(it->second.hTimer);
        m_timers.erase(it);
        
        return TRUE;
    }
    
private:
    struct TimerInfo
    {
        HANDLE hTimer;
        WAITORTIMERCALLBACK Callback;
        PVOID Parameter;
        DWORD DueTime;
        DWORD Period;
        ULONG Flags;
    };
    
    std::map<UINT, TimerInfo> m_timers;
    UINT m_nextTimerId;
};

static std::map<HANDLE, TimerQueue*> s_timerQueues;
static std::mutex s_timerQueueMutex;


BOOL WINAPI DeleteTimerQueue(HANDLE hTimerQueue)
{
    if (!hTimerQueue || hTimerQueue == (HANDLE)1)
        return FALSE;
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    auto it = s_timerQueues.find(hTimerQueue);
    if (it == s_timerQueues.end())
        return FALSE;
    
    delete it->second;
    s_timerQueues.erase(it);
    
    return TRUE;
}

BOOL WINAPI DeleteTimerQueueEx(HANDLE hTimerQueue, HANDLE hCompletionEvent)
{
    if (!hTimerQueue || hTimerQueue == (HANDLE)1)
        return FALSE;
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    auto it = s_timerQueues.find(hTimerQueue);
    if (it == s_timerQueues.end())
        return FALSE;
    
    delete it->second;
    s_timerQueues.erase(it);
    
    // If completion event is provided, signal it
    if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE)
    {
        SetEvent(hCompletionEvent);
    }
    
    return TRUE;
}

HANDLE WINAPI CreateTimerQueue(void)
{
    static HANDLE nextQueueId = (HANDLE)2; // Start from 2 to avoid conflict with simplified implementation
    
    TimerQueue *queue = new TimerQueue();
    if (!queue)
        return NULL;
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    HANDLE queueId = nextQueueId++;
    s_timerQueues[queueId] = queue;
    
    return queueId;
}

BOOL WINAPI CreateTimerQueueTimer(PHANDLE phNewTimer, HANDLE hTimerQueue, WAITORTIMERCALLBACK Callback, PVOID Parameter, DWORD DueTime, DWORD Period, ULONG Flags)
{
    if (!phNewTimer)
        return FALSE;
    
    // Handle simplified implementation case
    if (hTimerQueue == (HANDLE)1)
    {
        HANDLE hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
        if (!hTimer)
            return FALSE;
        
        *phNewTimer = hTimer;
        
        LARGE_INTEGER liDueTime;
        liDueTime.QuadPart = -((LONGLONG)DueTime * 10000); // Convert to 100ns intervals
        return SetWaitableTimer(hTimer, &liDueTime, Period, NULL, NULL, FALSE);
    }
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    auto it = s_timerQueues.find(hTimerQueue);
    if (it == s_timerQueues.end())
        return FALSE;
    
    return it->second->CreateTimer(phNewTimer, Callback, Parameter, DueTime, Period, Flags);
}

BOOL WINAPI ChangeTimerQueueTimer(HANDLE hTimerQueue, HANDLE hTimer, DWORD DueTime, DWORD Period)
{
    // Handle simplified implementation case
    if (hTimerQueue == (HANDLE)1)
    {
        LARGE_INTEGER liDueTime;
        liDueTime.QuadPart = -((LONGLONG)DueTime * 10000); // Convert to 100ns intervals
        return SetWaitableTimer(hTimer, &liDueTime, Period, NULL, NULL, FALSE);
    }
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    auto it = s_timerQueues.find(hTimerQueue);
    if (it == s_timerQueues.end())
        return FALSE;
    
    return it->second->ChangeTimer(hTimer, DueTime, Period);
}

BOOL WINAPI DeleteTimerQueueTimer(HANDLE hTimerQueue, HANDLE hTimer, HANDLE hCompletionEvent)
{
    // Handle simplified implementation case
    if (hTimerQueue == (HANDLE)1)
    {
        BOOL result = CancelWaitableTimer(hTimer) && CloseHandle(hTimer);
        if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE)
        {
            SetEvent(hCompletionEvent);
        }
        return result;
    }
    
    std::lock_guard<std::mutex> lock(s_timerQueueMutex);
    auto it = s_timerQueues.find(hTimerQueue);
    if (it == s_timerQueues.end())
        return FALSE;
    
    BOOL result = it->second->DeleteTimer(hTimer, hCompletionEvent);
    if (hCompletionEvent && hCompletionEvent != INVALID_HANDLE_VALUE)
    {
        SetEvent(hCompletionEvent);
    }
    return result;
}

struct FileMapObject
{
    int index;
    HandleData localFmd;
    GlobalMutex mutex;
    int fd;
    void *ptr;
    FileMapObject()
        : index(-1)
        , fd(-1)
        , ptr(nullptr)
    {
    }

    ~FileMapObject()
    {
        if (index != -1)
        {
            if (ptr != nullptr)
            {
                // unmap now
                UnmapViewOfFile(ptr);
            }
            // update global table
            s_globalHandleTable.getRwLock()->lockExclusive();
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            if (--pData->nRef == 0)
            {
                shm_unlink(pData->szName);
                pData->type = HUnknown;
                --s_globalHandleTable.getTableUsed();
            }
            s_globalHandleTable.getRwLock()->unlockExclusive();
        }
    }

    void lock()
    {
        if (index != -1)
            mutex.lock();
    }
    void unlock()
    {
        if (index != -1)
            mutex.unlock();
    }

    void init3(int fd_, FileMapData fmd)
    {
        fd = fd_;
        localFmd.data.fmData = fmd;
    }

    HandleData *handleData()
    {
        if (index == -1)
        {
            return &localFmd;
        }
        else
        {
            return s_globalHandleTable.getHandleData(index);
        }
    }

    bool init2(int idx)
    {
        HandleData *pData = s_globalHandleTable.getHandleData(idx);
        FileMapData &fmData = pData->data.fmData;
        int flag = 0;
        switch (fmData.uFlags)
        {
        case PAGE_READONLY:
        case PAGE_EXECUTE_READ:
            flag = O_RDONLY;
            break;
        case PAGE_EXECUTE_READWRITE:
        case PAGE_READWRITE:
            flag = O_RDWR;
            break;
        }
        int fd = shm_open(pData->szName, flag, 0666);
        if (fd == -1)
        {
            fd = shm_open(pData->szName, O_CREAT | flag, 0666);
            if (fd == -1)
                return false;
            if (ftruncate(fd, fmData.size.QuadPart) == -1)
            {
                close(fd);
                perror("ftruncate");
                return false;
            }
        }
        this->fd = fd;

        errno = NOERROR;
        pData->nRef++;
        mutex.init(s_globalHandleTable.getHeader()->key + idx + 10000);
        this->index = idx;
        return true;
    }

    bool init(LPCSTR pszName, void *initData)
    {
        s_globalHandleTable.getRwLock()->lockExclusive();
        bool bExisted = true;
        int index = s_globalHandleTable.findExistedHandleDataIndex(pszName, HFileMap);
        if (index == -1)
        {
            s_globalHandleTable.getTableUsed()++;
            index = s_globalHandleTable.findAvailableHandleDataIndex();
            assert(index != -1);
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            strcpy(pData->szNick, pszName);
            sprintf(pData->szName, kFileMapNameTemplate, index);
            pData->type = HFileMap;
            pData->nRef = 0;
            memcpy(&pData->data.fmData, initData, sizeof(FileMapData));
            bExisted = false;
        }
        assert(index != -1);
        if (!init2(index))
        {
            HandleData *pData = s_globalHandleTable.getHandleData(index);
            pData->type = HUnknown;
            pData->nRef = 0;
            s_globalHandleTable.getTableUsed()--;
            s_globalHandleTable.getRwLock()->unlockExclusive();
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        s_globalHandleTable.getRwLock()->unlockExclusive();
        return true;
    }
};

static void FreeFileMapObj(void *ptr)
{
    FileMapObject *fmObj = (FileMapObject *)ptr;
    delete fmObj;
}

static HANDLE NewFmHandle(FileMapObject *ptr)
{
    return new _Handle(FILEMAP_OBJ, ptr, FreeFileMapObj);
}

static HANDLE OpenExistObject(LPCSTR name, int type)
{
    HANDLE ret = 0;
    s_globalHandleTable.getRwLock()->lockExclusive();
    int idx = s_globalHandleTable.findExistedHandleDataIndex(name, type);
    if (idx != -1)
    {
        switch (type)
        {
        case HNamedEvent:
        {
            NamedEventObj *new_obj = new NamedEventObj();
            new_obj->init2(idx);
            ret = NewSynHandle(new_obj);
        }
        break;
        case HNamedSemaphore:
        {
            NamedSemaphoreObj *new_obj = new NamedSemaphoreObj();
            new_obj->init2(idx);
            ret = NewSynHandle(new_obj);
        }
        break;
        case HNamedMutex:
        {
            NamedMutexObj *new_obj = new NamedMutexObj();
            new_obj->init2(idx);
            ret = NewSynHandle(new_obj);
        }
        break;
        case HNamedTimer:
        {
            NamedTimerObj *new_obj = new NamedTimerObj();
            new_obj->init2(idx);
            ret = NewSynHandle(new_obj);
        }
        break;
        case HFileMap:
        {
            FileMapObject *new_obj = new FileMapObject();
            new_obj->init2(idx);
            ret = NewFmHandle(new_obj);
        }
        break;
        }
        SetLastError(ERROR_ALREADY_EXISTS);
    }
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
    }
    s_globalHandleTable.getRwLock()->unlockExclusive();
    return ret;
}

HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCSTR name)
{
    _SynHandle *ret = 0;
    if (name)
    {
        if (HANDLE exist = OpenExistObject(name, HNamedSemaphore))
        {
            return exist;
        }
        NamedWaitbleObj *obj = new NamedSemaphoreObj;
        ret = obj;
    }
    else
    {
        ret = new NoNameSemaphoreObj;
    }
    SemaphoreData data = { lInitialCount, lMaximumCount };
    if (!ret->init(name, &data))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        FreeSynObj(ret);
        return 0;
    }
    return NewSynHandle(ret);
}

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCWSTR lpName)
{
    if (!lpName)
        return CreateSemaphoreA(lpSemaphoreAttributes, lInitialCount, lMaximumCount, nullptr);
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return CreateSemaphoreA(lpSemaphoreAttributes, lInitialCount, lMaximumCount, szName);
}

HANDLE WINAPI OpenSemaphoreA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    return OpenExistObject(lpName, HNamedSemaphore);
}

HANDLE WINAPI OpenSemaphoreW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return OpenExistObject(szName, HNamedSemaphore);
}

BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG lReleaseCount, LPLONG lpPreviousCount)
{
    _SynHandle *hSemaphore = GetSynHandle(h);
    if (!hSemaphore)
        return FALSE;
    if ((hSemaphore->type != HSemaphore && hSemaphore->type != HNamedSemaphore) || lReleaseCount <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    hSemaphore->lock();
    SemaphoreData *pData = (SemaphoreData *)hSemaphore->getData();
    if (lReleaseCount > pData->maxSignal - pData->sigCount)
    {
        hSemaphore->unlock();
        return FALSE;
    }
    if (lpPreviousCount)
    {
        *lpPreviousCount = pData->sigCount;
    }
    for (LONG i = 0; i < lReleaseCount; i++)
    {
        hSemaphore->writeSignal();
    }
    pData->sigCount += lReleaseCount;
    hSemaphore->unlock();
    return TRUE;
}

HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    return OpenExistObject(lpName, HNamedEvent);
}

HANDLE WINAPI OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return OpenExistObject(szName, HNamedEvent);
}

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
    _SynHandle *ret = 0;
    if (lpName)
    {
        if (HANDLE ret = OpenExistObject(lpName, HNamedEvent))
        {
            return ret;
        }
        ret = new NamedEventObj;
    }
    else
    {
        ret = new NoNameEventObj;
    }

    EventData data = { bManualReset };
    if (!ret->init(lpName, &data))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        FreeSynObj(ret);
        return 0;
    }
    if (bInitialState)
    {
        ret->writeSignal();
    }
    return NewSynHandle(ret);
}

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName)
{
    if (!lpName)
        return CreateEventA(lpEventAttributes, bManualReset, bInitialState, nullptr);
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return CreateEventA(lpEventAttributes, bManualReset, bInitialState, szName);
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    _SynHandle *ret = 0;
    if (lpName)
    {
        if (HANDLE ret = OpenExistObject(lpName, HNamedMutex))
        {
            return ret;
        }
        ret = new NamedMutexObj;
    }
    else
    {
        ret = new NoNameMutexObj;
    }

    MutexData data = { bInitialOwner ? GetCurrentThreadId() : 0 };
    if (!ret->init(lpName, &data))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        FreeSynObj(ret);
        return 0;
    }
    return NewSynHandle(ret);
}

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName)
{
    if (!lpName)
        return CreateMutexA(lpMutexAttributes, bInitialOwner, nullptr);
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return CreateMutexA(lpMutexAttributes, bInitialOwner, szName);
}

BOOL WINAPI ReleaseMutex(HANDLE h)
{
    _SynHandle *hMutex = GetSynHandle(h);
    if (!hMutex)
        return FALSE;
    if (hMutex->type != HMutex && hMutex->type != HNamedMutex)
        return FALSE;
    MutexData *data = (MutexData *)hMutex->getData();
    if (data->tid_owner != GetCurrentThreadId())
        return FALSE;
    return hMutex->writeSignal();
}

HANDLE WINAPI OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    return OpenExistObject(lpName, HNamedMutex);
}

HANDLE WINAPI OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return OpenExistObject(szName, HNamedMutex);
}

BOOL WINAPI ResetEvent(HANDLE h)
{
    _SynHandle *hEvent = GetSynHandle(h);
    if (!hEvent)
        return FALSE;
    if (hEvent->getType() != HEvent && hEvent->getType() != HNamedEvent)
        return FALSE;
    int signals = 0;
    while (hEvent->readSignal())
        signals++;
    return TRUE;
}

BOOL WINAPI SetEvent(HANDLE h)
{
    _SynHandle *hEvent = GetSynHandle(h);
    if (!hEvent)
        return FALSE;

    if (hEvent->type == HEvent || hEvent->type == HNamedEvent)
    {
        hEvent->writeSignal();
        return TRUE;
    }
    return FALSE;
}

static int selectfds(int *fds, int nCount, DWORD timeoutMs, bool *states)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;
    for (int i = 0; i < nCount; i++)
    {
        if ((fds[i] != -1) && (!states || !states[i]))
        {
            FD_SET(fds[i], &read_fds);
            max_fd = std::max(max_fd, fds[i]);
        }
    }

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

DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
    int fd = -1;
    s_globalHandleTable.getRwLock()->lockShared();
    HANDLE hTmp = AddHandleRef(hHandle);
    _SynHandle *synObj = GetSynHandle(hHandle);
    fd = synObj->getReadFd();
    s_globalHandleTable.getRwLock()->unlockShared();
    if (fd == -1)
    {
        CloseHandle(hTmp);
        return WAIT_ABANDONED;
    }
start_wait:
    uint64_t ts1 = GetTickCount64();
    int ret = selectfds(&fd, 1, dwMilliseconds, nullptr);
    if (ret == 0)
    {
        CloseHandle(hTmp);
        return WAIT_TIMEOUT;
    }
    else if (ret < 0)
    {
        // error
        CloseHandle(hTmp);
        return WAIT_ABANDONED;
    }
    else
    {
        if (synObj->onWaitDone())
        {
            CloseHandle(hTmp);
            return WAIT_OBJECT_0;
        }
        else
        {
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
            goto start_wait;
        }
    }
}

DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
    if (nCount > MAXIMUM_WAIT_OBJECTS)
        return 0;
    int fd[MAXIMUM_WAIT_OBJECTS] = { 0 };
    bool states[MAXIMUM_WAIT_OBJECTS] = { false };
    HANDLE tmpHandles[MAXIMUM_WAIT_OBJECTS] = { 0 };
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
            states[i] = true;
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
        uint64_t ts1 = GetTickCount64();
        nRet = selectfds(fd, nCount, dwMilliseconds, states);
        if (nRet <= 0)
        {
            nRet = nRet == 0 ? WAIT_TIMEOUT : WAIT_ABANDONED;
            break; // timeout or error
        }
        if (!bWaitAll)
        {
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
            // wait for all objects.
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
    if (nRet >= WAIT_OBJECT_0)
    {
        // check for state again
        bool bValid = true;
        for (int i = 0; i < nCount; i++)
        {
            if (states[i] && tmpHandles[i] != INVALID_HANDLE_VALUE)
            {
                if (!GetSynHandle(tmpHandles[i])->onWaitDone())
                {
                    states[i] = false; // reset to false
                    bValid = false;
                    break;
                }
            }
        }
        if (!bValid)
        {
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

    for (int i = 0; i < nCount; i++)
    {
        CloseHandle(tmpHandles[i]);
    }

    return nRet;
}

HANDLE OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    return OpenExistObject(lpName, HFileMap);
}

HANDLE OpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    char szPath[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpName, -1, szPath, MAX_PATH, nullptr, nullptr))
        return FALSE;
    return OpenFileMappingA(dwDesiredAccess, bInheritHandle, szPath);
}

HANDLE CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName)
{
    if (hFile != INVALID_HANDLE_VALUE)
    {
        int fd = _open_osfhandle(hFile, 0);
        if (fd == -1)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HANDLE_VALUE;
        }
        LARGE_INTEGER fsize = { 0 };
        GetFileSizeEx(hFile, &fsize);
        if (dwMaximumSizeHigh != 0 || dwMaximumSizeLow != 0)
        {
            if (fsize.LowPart != dwMaximumSizeLow || fsize.HighPart != dwMaximumSizeHigh)
            {
                fsize.LowPart = dwMaximumSizeLow;
                fsize.HighPart = dwMaximumSizeHigh;
                if (SetFilePointerEx(hFile, fsize, NULL, SEEK_SET) || SetEndOfFile(hFile))
                {
                    return INVALID_HANDLE_VALUE;
                }
            }
        }
        // create file map from fd.
        FileMapObject *fmObj = new FileMapObject();
        FileMapData fmData;
        fmData.uFlags = flProtect;
        fmData.mappedSize.QuadPart = 0;
        fmData.offset.QuadPart = 0;
        fmData.size = fsize;
        fmObj->init3(fd, fmData);
        return NewFmHandle(fmObj);
    }
    FileMapData fmData;
    fmData.uFlags = flProtect;
    fmData.mappedSize.QuadPart = 0;
    fmData.offset.QuadPart = 0;
    fmData.size.HighPart = dwMaximumSizeHigh;
    fmData.size.LowPart = dwMaximumSizeLow;
    std::string name(lpName ? lpName : "");
    if (name.empty())
    {
        // generate random name.
        suid_t id;
        IpcMsg::gen_suid(&id);
        char szBuf[25];
        IpcMsg::suid2string(id, szBuf);
        name += "/random_share_map_";
        name += szBuf;
    }
    FileMapObject *fmObj = new FileMapObject();
    if (!fmObj->init(name.c_str(), &fmData))
    {
        delete fmObj;
        return INVALID_HANDLE_VALUE;
    }
    return NewFmHandle(fmObj);
}

HANDLE WINAPI CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName)
{
    if (!lpName)
    {
        return CreateFileMappingA(hFile, lpAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, nullptr);
    }
    char szPath[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpName, -1, szPath, MAX_PATH, nullptr, nullptr))
        return FALSE;
    return CreateFileMappingA(hFile, lpAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, szPath);
}

class MapViewMgr {
  public:
    MapViewMgr()
    {
    }

    ~MapViewMgr()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto it : m_fmviewMap)
        {
            CloseHandle(it.second);
        }
        m_fmviewMap.clear();
    }

    void addMapView(const void *ptr, HANDLE handle)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_fmviewMap.insert(std::make_pair(ptr, handle));
        AddHandleRef(handle);
    }

    HANDLE getViewHandle(const void *ptr)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_fmviewMap.find(ptr);
        if (it == m_fmviewMap.end())
            return INVALID_HANDLE_VALUE;
        return it->second;
    }

    void removeMapView(const void *ptr)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_fmviewMap.find(ptr);
        if (it == m_fmviewMap.end())
            return;
        CloseHandle(it->second);
        m_fmviewMap.erase(it);
    }

  protected:
    std::mutex m_mutex;
    std::map<const void *, HANDLE> m_fmviewMap;
};

static MapViewMgr s_mapViewMgr;

LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, size_t dwNumberOfBytesToMap)
{
    if (hFileMappingObject->type != FILEMAP_OBJ)
        return nullptr;
    LPVOID ret = nullptr;
    FileMapObject *fmObj = (FileMapObject *)hFileMappingObject->ptr;
    fmObj->lock();
    FileMapData &fmData = fmObj->handleData()->data.fmData;
    LARGE_INTEGER offset;
    offset.HighPart = dwFileOffsetHigh;
    offset.LowPart = dwFileOffsetLow;
    int flag = 0;
    if (offset.QuadPart + dwNumberOfBytesToMap >= fmData.size.QuadPart)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        goto end;
    }
    assert(fmObj->fd != -1);
    if (dwDesiredAccess & FILE_MAP_READ)
        flag |= PROT_READ;
    if (dwDesiredAccess & FILE_MAP_WRITE)
        flag |= PROT_WRITE;
    if (dwDesiredAccess & FILE_MAP_EXECUTE)
        flag |= PROT_EXEC;
    if (dwNumberOfBytesToMap == 0)
        dwNumberOfBytesToMap = fmData.size.QuadPart - offset.QuadPart;
    ret = mmap(nullptr, dwNumberOfBytesToMap, flag, MAP_SHARED, fmObj->fd, offset.QuadPart);
    if (ret != MAP_FAILED)
    {
        fmData.offset = offset;
        fmData.mappedSize.QuadPart = dwNumberOfBytesToMap;
        s_mapViewMgr.addMapView(ret, hFileMappingObject);
    }
    fmObj->ptr = ret;
end:
    fmObj->unlock();
    return ret;
}

BOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress)
{
    if (lpBaseAddress == nullptr)
        return FALSE;
    HANDLE hMemMap = s_mapViewMgr.getViewHandle(lpBaseAddress);
    if (hMemMap == INVALID_HANDLE_VALUE)
        return FALSE;
    FileMapObject *fmObj = (FileMapObject *)hMemMap->ptr;
    fmObj->lock();
    assert(fmObj->ptr == lpBaseAddress);
    FileMapData &fmData = fmObj->handleData()->data.fmData;
    munmap(fmObj->ptr, fmData.mappedSize.QuadPart);
    fmObj->ptr = nullptr;
    fmData.mappedSize.QuadPart = 0;
    fmObj->unlock();
    s_mapViewMgr.removeMapView(lpBaseAddress);
    return TRUE;
}

BOOL GetHandleName(HANDLE h, char szName[1001])
{
    _SynHandle *pData = GetSynHandle(h);
    if (!pData)
        return FALSE;
    int index = -1;
    if (pData->type == HNamedEvent)
    {
        NamedEventObj *pEvent = (NamedEventObj *)pData;
        index = pEvent->index;
    }
    else if (pData->type == HNamedMutex)
    {
        NamedMutexObj *pMutex = (NamedMutexObj *)pData;
        index = pMutex->index;
    }
    else if (pData->type == HNamedSemaphore)
    {
        NamedSemaphoreObj *pSemaphore = (NamedSemaphoreObj *)pData;
        index = pSemaphore->index;
    }
    if (index == -1)
    {
        return FALSE;
    }
    s_globalHandleTable.getRwLock()->lockShared();
    HandleData *pHandleData = s_globalHandleTable.getHandleData(index);
    strcpy(szName, pHandleData->szNick);
    s_globalHandleTable.getRwLock()->unlockShared();
    return TRUE;
}


HANDLE WINAPI OpenWaitableTimerA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpTimerName)
{
    return OpenExistObject(lpTimerName, HNamedTimer);
}

HANDLE WINAPI OpenWaitableTimerW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpTimerName)
{
    char szName[MAX_PATH] = { 0 };
    if (0 == WideCharToMultiByte(CP_ACP, 0, lpTimerName, -1, szName, MAX_PATH, nullptr, nullptr))
        return 0;
    return OpenExistObject(szName, HNamedTimer);
}
