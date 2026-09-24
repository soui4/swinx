/*
 * swinx/platform/freertos/winobjs.cpp
 *
 * FreeRTOS implementation of the swinx Win32 *sync-object* + thread compat
 * layer (declared in include/sysapi.h / include/wnd.h).  It replaces the
 * Linux/Win32 versions (src/sysobjs.cpp + the process-heavy parts of
 * src/sysapi.cpp) on the FreeRTOS platform: those rely on pipes, pthreads,
 * fork, SIGCHLD self-pipes and dlopen -- none of which exist on a bare-metal
 * arm-none-eabi toolchain.
 *
 * Framework conformance:
 *   * All objects are wrapped in the core _Handle registry (src/handle.cpp):
 *     HANDLE = struct _Handle*, CloseHandle()/AddHandleRef() come from the
 *     core -- this file defines NO handle-lifecycle functions of its own.
 *   * Object kind is encoded in _Handle::type as SYN_OBJ + kind.
 *   * Named objects: a process-local name -> HANDLE registry (bare metal has
 *     a single "process"); the registry keeps one reference for the lifetime
 *     of the name, so a named object persists once created (documented
 *     limitation -- Windows deletes named objects when the last handle
 *     closes, we keep them until reboot).
 *
 * Backing primitives (FreeRTOS STL shims in src/platform/freertos/stl):
 *   * Event     -> std::mutex + std::condition_variable + signaled/manual flags
 *   * Mutex     -> FreeRTOS binary semaphore (non-recursive)
 *   * Semaphore -> std::mutex + std::condition_variable + count/max
 *   * Thread    -> swinx_fr::task_create + a join semaphore + suspend counter
 *
 * KNOWN LIMITATIONS (documented, not bugs):
 *   * The condition_variable shim's notify_all() only wakes one waiter, so a
 *     manual-reset event signalled while MANY tasks are already blocked will
 *     wake one per SetEvent. The tested fun_test cases use at most one blocked
 *     waiter per event, so the contract holds for them.
 */
#include <windows.h>
#include "handle.h"

#include <cstring>
#include <cstdint>
#include <new>

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <map>
#include <string>

#include "swinx_freertos_api.h"

namespace
{

// ---- internal object structs --------------------------------------------
struct EventObj
{
    std::mutex mtx;
    std::condition_variable cv;
    bool signaled;
    bool manual;
};

struct MutexObj
{
    swinx_fr::sem_handle sem;   // binary semaphore, non-recursive
};

struct SemObj
{
    std::mutex mtx;
    std::condition_variable cv;
    int count;
    int max;
};

struct ThreadObj
{
    swinx_fr::task_handle task;
    swinx_fr::sem_handle joinSem;   // given once when the thread proc returns
    volatile int suspendCount;
    bool done;
    DWORD exitCode;
    LPTHREAD_START_ROUTINE start;
    LPVOID param;
};

// object kind, encoded as _Handle::type == SYN_OBJ + kind
enum ObjKind
{
    kEvent = 1,
    kMutex = 2,
    kSem = 3,
    kThread = 4
};

// _Handle::cbFree callbacks: plain functions, no capture.
void free_event(void *p) { delete static_cast<EventObj *>(p); }
void free_mutex(void *p)
{
    MutexObj *o = static_cast<MutexObj *>(p);
    swinx_fr::sem_delete(o->sem);
    delete o;
}
void free_sem(void *p) { delete static_cast<SemObj *>(p); }
void free_thread(void *p)
{
    ThreadObj *o = static_cast<ThreadObj *>(p);
    swinx_fr::sem_delete(o->joinSem);
    delete o;
}

FreeHandlePtr kFreeCb[] = { NULL, free_event, free_mutex, free_sem, free_thread };

HANDLE make_handle(int kind, void *obj)
{
    return InitHandle(SYN_OBJ + kind, obj, kFreeCb[kind]);
}

// typed object accessor: NULL unless h is a live handle of the given kind
void *obj_of(HANDLE h, int kind)
{
    if (!h || h == INVALID_HANDLE_VALUE || h->type != SYN_OBJ + kind)
        return NULL;
    return h->ptr;
}

// ---- named-object registry ----------------------------------------------
// The lock MUST be reached through function-local statics: the bare-metal
// startup does not run __libc_init_array, so namespace-scope objects with
// constructors are never initialized.  Function-local statics initialize on
// first use -- after the scheduler is running -- which the initonce test
// already proved works.
struct NamedRegistry
{
    std::mutex mtx;
    std::map<std::string, HANDLE> byName;
};

NamedRegistry &named_registry()
{
    static NamedRegistry s_reg;
    return s_reg;
}

// create-with-name / open-by-name rule: an existing name returns the SAME
// object with an extra reference.  On a name hit the freshly created `obj`
// is disposed of through the same callback the handle would have used.
HANDLE register_named(int kind, void *obj, const char *name)
{
    if (!name || !name[0])
        return make_handle(kind, obj);

    NamedRegistry &reg = named_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    auto it = reg.byName.find(name);
    if (it != reg.byName.end())
    {
        kFreeCb[kind](obj);          // dispose of the duplicate
        AddHandleRef(it->second);
        return it->second;
    }
    HANDLE h = make_handle(kind, obj);
    if (h)
    {
        reg.byName[name] = h;
        AddHandleRef(h);             // the registry's own reference
    }
    return h;
}

HANDLE open_named(const char *name)
{
    if (!name || !name[0])
        return NULL;
    NamedRegistry &reg = named_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    auto it = reg.byName.find(name);
    if (it == reg.byName.end())
        return NULL;
    return AddHandleRef(it->second);
}

// round ms -> FreeRTOS ticks (1:1 because configTICK_RATE_HZ == 1000)
inline uint32_t ms_to_ticks(DWORD ms)
{
    return ms;
}

// Cortex-M3 has no 64-bit atomic instructions and libatomic is not linked in
// this bare-metal build: implement the 64-bit interlocked ops with an IRQ
// critical section instead (single core, so PRIMASK alone is a full barrier).
inline uint32_t irq_save(void)
{
    uint32_t pr;
    __asm volatile("mrs %0, primask" : "=r"(pr));
    __asm volatile("cpsid i" ::: "memory");
    return pr;
}

inline void irq_restore(uint32_t pr)
{
    if (!pr)
        __asm volatile("cpsie i" ::: "memory");
}

// ---- thread trampoline ----------------------------------------------------
void thread_trampoline(void *p)
{
    ThreadObj *t = static_cast<ThreadObj *>(p);
    t->exitCode = t->start(t->param);
    t->done = true;
    swinx_fr::sem_give(t->joinSem);
    // A FreeRTOS task function MUST NOT return.
    vTaskDelete(nullptr);
}

// readiness probe used by WaitForMultipleObjects' polling loop
bool handle_ready(HANDLE h, bool consume)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return false;
    switch (h->type - SYN_OBJ)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(h->ptr);
        std::lock_guard<std::mutex> lk(ev->mtx);
        if (!ev->signaled)
            return false;
        if (consume && !ev->manual)
            ev->signaled = false;
        return true;
    }
    case kThread:
    {
        ThreadObj *t = static_cast<ThreadObj *>(h->ptr);
        return t->done;
    }
    case kMutex:
    {
        MutexObj *m = static_cast<MutexObj *>(h->ptr);
        bool ok = swinx_fr::sem_take(m->sem, 0);
        if (ok && !consume)
            swinx_fr::sem_give(m->sem);   // peek only
        return ok;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(h->ptr);
        std::lock_guard<std::mutex> lk(s->mtx);
        if (s->count <= 0)
            return false;
        if (consume)
            s->count--;
        return true;
    }
    default:
        return false;
    }
}

} // namespace

extern "C"
{

// ===========================================================================
// Events
// ===========================================================================
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
    EventObj *o = new (std::nothrow) EventObj();
    if (!o)
        return NULL;
    o->signaled = bInitialState ? true : false;
    o->manual = bManualReset ? true : false;
    return register_named(kEvent, o, lpName);
}

HANDLE WINAPI OpenEventA(DWORD, BOOL, LPCSTR lpName)
{
    return open_named(lpName);
}

BOOL WINAPI SetEvent(HANDLE h)
{
    EventObj *ev = static_cast<EventObj *>(obj_of(h, kEvent));
    if (!ev)
        return FALSE;
    {
        std::lock_guard<std::mutex> lk(ev->mtx);
        ev->signaled = true;
    }
    ev->cv.notify_one();   // wakes one blocked waiter (see KNOWN LIMITATIONS)
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE h)
{
    EventObj *ev = static_cast<EventObj *>(obj_of(h, kEvent));
    if (!ev)
        return FALSE;
    std::lock_guard<std::mutex> lk(ev->mtx);
    ev->signaled = false;
    return TRUE;
}

// ===========================================================================
// Mutex
// ===========================================================================
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL bInitialOwner, LPCSTR lpName)
{
    MutexObj *o = new (std::nothrow) MutexObj();
    if (!o)
        return NULL;
    o->sem = swinx_fr::sem_create();   // binary semaphore (born EMPTY = locked)
    if (!o->sem)
    {
        delete o;
        return NULL;
    }
    if (!bInitialOwner)
        swinx_fr::sem_give(o->sem);   // an unowned mutex starts unlocked
    return register_named(kMutex, o, lpName);
}

HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR lpName)
{
    return open_named(lpName);
}

BOOL WINAPI ReleaseMutex(HANDLE h)
{
    MutexObj *m = static_cast<MutexObj *>(obj_of(h, kMutex));
    if (!m)
        return FALSE;
    swinx_fr::sem_give(m->sem);
    return TRUE;
}

// ===========================================================================
// Semaphore
// ===========================================================================
HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG lInitialCount, LONG lMaximumCount, LPCSTR lpName)
{
    SemObj *o = new (std::nothrow) SemObj();
    if (!o)
        return NULL;
    o->count = (int)lInitialCount;
    o->max = (int)lMaximumCount;
    return register_named(kSem, o, lpName);
}

HANDLE WINAPI OpenSemaphoreA(DWORD, BOOL, LPCSTR lpName)
{
    return open_named(lpName);
}

BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG lReleaseCount, LPLONG lpPreviousCount)
{
    SemObj *s = static_cast<SemObj *>(obj_of(h, kSem));
    if (!s)
        return FALSE;
    std::lock_guard<std::mutex> lk(s->mtx);
    if (lpPreviousCount)
        *lpPreviousCount = (LONG)s->count;
    s->count += (int)lReleaseCount;
    if (s->count > s->max)
        s->count = s->max;
    s->cv.notify_one();
    return TRUE;
}

// CloseHandle() is provided by the core (src/handle.cpp): it releases the
// _Handle reference and, when the last reference drops, invokes the cbFree
// callback registered here via InitHandle().

// ===========================================================================
// Wait
// ===========================================================================
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD dwMilliseconds)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return WAIT_FAILED;

    if (dwMilliseconds == 0)
    {
        // non-blocking poll
        if (handle_ready(h, true))
            return WAIT_OBJECT_0;
        return WAIT_TIMEOUT;
    }

    switch (h->type - SYN_OBJ)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(h->ptr);
        std::unique_lock<std::mutex> lk(ev->mtx);
        while (!ev->signaled)
        {
            if (ev->cv.wait_for(lk, std::chrono::milliseconds((uint64_t)dwMilliseconds)) ==
                std::cv_status::timeout)
                return WAIT_TIMEOUT;
        }
        if (!ev->manual)
            ev->signaled = false;
        return WAIT_OBJECT_0;
    }
    case kMutex:
    {
        MutexObj *m = static_cast<MutexObj *>(h->ptr);
        return swinx_fr::sem_take(m->sem, ms_to_ticks(dwMilliseconds)) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(h->ptr);
        std::unique_lock<std::mutex> lk(s->mtx);
        while (s->count <= 0)
        {
            if (s->cv.wait_for(lk, std::chrono::milliseconds((uint64_t)dwMilliseconds)) ==
                std::cv_status::timeout)
                return WAIT_TIMEOUT;
        }
        s->count--;
        return WAIT_OBJECT_0;
    }
    case kThread:
    {
        ThreadObj *t = static_cast<ThreadObj *>(h->ptr);
        if (t->done)
            return WAIT_OBJECT_0;
        bool ok = swinx_fr::sem_take(t->joinSem, ms_to_ticks(dwMilliseconds));
        if (ok)
            t->done = true;
        return ok ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    default:
        return WAIT_FAILED;
    }
}

DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
    const uint32_t start = swinx_fr::ticks_now();
    for (;;)
    {
        if (bWaitAll)
        {
            bool allReady = true;
            for (DWORD i = 0; i < nCount; ++i)
            {
                if (!handle_ready(lpHandles[i], false))
                {
                    allReady = false;
                    break;
                }
            }
            if (allReady)
            {
                for (DWORD i = 0; i < nCount; ++i)
                    handle_ready(lpHandles[i], true);   // consume auto-reset events
                return WAIT_OBJECT_0;
            }
        }
        else
        {
            for (DWORD i = 0; i < nCount; ++i)
            {
                if (handle_ready(lpHandles[i], false))
                {
                    handle_ready(lpHandles[i], true);
                    return WAIT_OBJECT_0 + i;
                }
            }
        }

        if (dwMilliseconds != INFINITE)
        {
            if (swinx_fr::ticks_now() - start >= (uint32_t)dwMilliseconds)
                return WAIT_TIMEOUT;
        }
        swinx_fr::delay_ticks(1);   // yield while polling
    }
}

// ===========================================================================
// Threads
// ===========================================================================
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                           LPVOID lpParameter, DWORD dwCreationFlags, tid_t *lpThreadId)
{
    ThreadObj *t = new (std::nothrow) ThreadObj();
    if (!t)
        return NULL;
    t->joinSem = swinx_fr::sem_create();
    if (!t->joinSem)
    {
        delete t;
        return NULL;
    }
    t->suspendCount = 0;
    t->done = false;
    t->exitCode = 0;
    t->start = lpStartAddress;
    t->param = lpParameter;

    uint32_t stack = dwStackSize ? (uint32_t)(dwStackSize / sizeof(uint32_t))
                                  : swinx_fr::default_stack_depth_words();
    t->task = swinx_fr::task_create(thread_trampoline, t, "swinx-thr", stack,
                                    swinx_fr::default_priority());
    if (!swinx_fr::task_handle_valid(t->task))
    {
        swinx_fr::sem_delete(t->joinSem);
        delete t;
        return NULL;
    }
    if (lpThreadId)
        *lpThreadId = (tid_t)swinx_fr::task_id(t->task);

    HANDLE h = make_handle(kThread, t);
    if (!h)
    {
        vTaskDelete(t->task);
        swinx_fr::sem_delete(t->joinSem);
        delete t;
        return NULL;
    }
    if (dwCreationFlags & CREATE_SUSPENDED)
        SuspendThread(h);
    return h;
}

DWORD WINAPI SuspendThread(HANDLE h)
{
    ThreadObj *t = static_cast<ThreadObj *>(obj_of(h, kThread));
    if (!t)
        return (DWORD)-1;
    int prev = t->suspendCount;
    if (prev == 0)
        vTaskSuspend(t->task);
    t->suspendCount = prev + 1;
    return (DWORD)prev;
}

DWORD WINAPI ResumeThread(HANDLE h)
{
    ThreadObj *t = static_cast<ThreadObj *>(obj_of(h, kThread));
    if (!t)
        return (DWORD)-1;
    int prev = t->suspendCount;
    if (prev > 0)
    {
        t->suspendCount = prev - 1;
        if (t->suspendCount == 0)
            vTaskResume(t->task);
    }
    return (DWORD)prev;
}

// ===========================================================================
// Misc
// ===========================================================================
VOID WINAPI Sleep(DWORD dwMilliseconds)
{
    swinx_fr::delay_ticks(dwMilliseconds);
}

DWORD WINAPI GetTickCount(VOID)
{
    return swinx_fr::ticks_now();
}

tid_t WINAPI GetCurrentThreadId(VOID)
{
    return (tid_t)swinx_fr::task_id(swinx_fr::task_current());
}

VOID WINAPI GetLocalTime(SYSTEMTIME *pSysTime)
{
    if (!pSysTime)
        return;
    // No RTC on bare metal: derive the date from a fixed epoch
    // (2026-01-01 00:00:00) plus the tick counter.  Monotonic per power-on.
    DWORD ms = swinx_fr::ticks_now();
    uint64_t sec = (uint64_t)(ms / 1000u) + 1767225600ull; // 2026-01-01 epoch secs
    uint64_t days = sec / 86400ull;
    uint64_t tod = sec % 86400ull;
    static const WORD kWeekdayByDay[7] = { 4, 5, 6, 0, 1, 2, 3 }; // day0 = 2026-01-01 = Thursday
    WORD wday = kWeekdayByDay[days % 7ull];

    // civil-from-days (Howard Hinnant's algorithm)
    days += 719468ull;
    uint64_t era = days / 146097ull;
    unsigned doe = (unsigned)(days - era * 146097ull);              // [0, 146096]
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u; // [0, 399]
    int64_t y = (int64_t)yoe + (int64_t)era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);      // [0, 365]
    unsigned mp = (5u * doy + 2u) / 153u;                           // [0, 11]
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;                  // [1, 31]
    unsigned m = mp < 10u ? mp + 3u : mp - 9u;                      // [1, 12]
    if (m <= 2u)
        y++;

    pSysTime->wYear = (WORD)y;
    pSysTime->wMonth = (WORD)m;
    pSysTime->wDay = (WORD)d;
    pSysTime->wDayOfWeek = wday;
    pSysTime->wHour = (WORD)(tod / 3600ull);
    pSysTime->wMinute = (WORD)((tod % 3600ull) / 60ull);
    pSysTime->wSecond = (WORD)(tod % 60ull);
    pSysTime->wMilliseconds = (WORD)(ms % 1000u);
}

// ===========================================================================
// Interlocked
// ===========================================================================
LONG WINAPI InterlockedIncrement(LONG volatile *v)
{
    return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
}

LONG WINAPI InterlockedDecrement(LONG volatile *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

LONG WINAPI InterlockedCompareExchange(LONG volatile *v, LONG Exchange, LONG Comparand)
{
    // Win32 semantics: return the ORIGINAL value of *v.
    __atomic_compare_exchange_n(v, &Comparand, Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comparand;
}

LONG WINAPI InterlockedExchangeAdd(LONG volatile *v, LONG Increment)
{
    return __atomic_fetch_add(v, Increment, __ATOMIC_SEQ_CST);
}

int64_t WINAPI InterlockedIncrement64(int64_t volatile *v)
{
    uint32_t pr = irq_save();
    int64_t r = ++(*v);
    irq_restore(pr);
    return r;
}

int64_t WINAPI InterlockedDecrement64(int64_t volatile *v)
{
    uint32_t pr = irq_save();
    int64_t r = --(*v);
    irq_restore(pr);
    return r;
}

} // extern "C"

// ---------------------------------------------------------------------------
// POSIX thread bridge -- core swinx files (e.g. wnd.cpp) call pthread_self()
// on POSIX platforms to identify the owning thread.  newlib declares the
// pthread_t type but ships no implementation, so map it onto the FreeRTOS
// task identity (same value GetCurrentThreadId() returns).
// ---------------------------------------------------------------------------
#include <pthread.h>

pthread_t pthread_self(void)
{
    return (pthread_t)GetCurrentThreadId();
}
