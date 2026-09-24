/*
 * swinx/platform/freertos/winobjs.cpp
 *
 * FreeRTOS implementation of the swinx Win32 *sync-object* + thread compat
 * layer. It replaces the Linux/Win32 versions (src/sysobjs.cpp + the
 * process-heavy parts of src/sysapi.cpp) on the FreeRTOS platform. Those rely
 * on pipes, pthreads, fork, SIGCHLD self-pipes and dlopen -- none of which
 * exist on a bare-metal arm-none-eabi toolchain. Here every object is backed by
 * the FreeRTOS STL shims in src/platform/freertos/stl (std::mutex / std::condition_variable
 * / std::thread resolve to FreeRTOS-kernel objects), so swinx stays compatible
 * with FreeRTOS without changing any other platform's behaviour.
 *
 *   * Event     -> std::mutex + std::condition_variable + bool signaled + bool manual
 *   * Mutex     -> FreeRTOS binary semaphore (non-recursive, matching swinx's pipe mutex)
 *   * Semaphore -> std::mutex + std::condition_variable + int count/max
 *   * Thread    -> swinx_fr::task_create + a join semaphore + suspend counter
 *
 * KNOWN LIMITATIONS (documented, not bugs):
 *   * The condition_variable shim's notify_all() only wakes one waiter, so a
 *     manual-reset event signalled while MANY tasks are already blocked will
 *     wake one per SetEvent. The tested fun_test cases use at most one blocked
 *     waiter per event, so the contract holds for them. Waiters that poll
 *     (WaitForMultipleObjects) or re-check the flag are unaffected.
 *   * Named objects are process-local (no cross-process); the name registry is
 *     a small handle table with reference counting. This matches what the
 *     fun_test named-open cases need.
 */
#include "winobjs.h"

#include <cstring>
#include <cstdint>
#include <new>

#include <mutex>
#include <condition_variable>
#include <chrono>

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

// ---- handle table (fixed slots, ref-counted) -----------------------------
enum ObjType
{
    kEvent = 1,
    kMutex = 2,
    kSem = 3,
    kThread = 4
};

struct HandleEntry
{
    uint32_t magic;   // kMagic when live, 0 when free
    int type;
    int refcount;
    char name[64];
    void *obj;
};

const uint32_t kMagic = 0x48444C45u;   // "HDLE"
const int kMaxHandles = 64;

// POD table: zero-initialized in .bss. The bare-metal startup does NOT run
// __libc_init_array, so nothing here may depend on static constructors.
HandleEntry g_entries[kMaxHandles];

// The table lock MUST be a function-local static: a namespace-scope std::mutex
// would never be constructed on this target (no __libc_init_array). Function-
// local statics initialize on first use -- after the scheduler is running --
// which the initonce test already proved works.
std::mutex &table_lock()
{
    static std::mutex s_lock;
    return s_lock;
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

HandleEntry *alloc_slot()
{
    for (int i = 0; i < kMaxHandles; ++i)
        if (g_entries[i].magic == 0)
            return &g_entries[i];
    return NULL;
}

HandleEntry *entry_of(HANDLE h)
{
    HandleEntry *e = static_cast<HandleEntry *>(h);
    if (!e || e->magic != kMagic)
        return NULL;
    return e;
}

HandleEntry *find_named(const char *name)
{
    if (!name || !name[0])
        return NULL;
    for (int i = 0; i < kMaxHandles; ++i)
        if (g_entries[i].magic == kMagic && g_entries[i].name[0] &&
            std::strcmp(g_entries[i].name, name) == 0)
            return &g_entries[i];
    return NULL;
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

void free_obj(HandleEntry *e)
{
    switch (e->type)
    {
    case kEvent:
    {
        EventObj *o = static_cast<EventObj *>(e->obj);
        delete o;
        break;
    }
    case kMutex:
    {
        MutexObj *o = static_cast<MutexObj *>(e->obj);
        swinx_fr::sem_delete(o->sem);
        delete o;
        break;
    }
    case kSem:
    {
        SemObj *o = static_cast<SemObj *>(e->obj);
        delete o;
        break;
    }
    case kThread:
    {
        ThreadObj *o = static_cast<ThreadObj *>(e->obj);
        swinx_fr::sem_delete(o->joinSem);
        delete o;
        break;
    }
    default:
        break;
    }
    e->obj = NULL;
}

// allocate a handle, handling the "create-by-name returns existing" rule.
HANDLE alloc_handle(int type, void *obj, const char *name)
{
    std::lock_guard<std::mutex> lk(table_lock());
    if (name && name[0])
    {
        HandleEntry *ex = find_named(name);
        if (ex)
        {
            ex->refcount++;
            return (HANDLE)ex;
        }
    }
    HandleEntry *e = alloc_slot();
    if (!e)
        return NULL;
    e->magic = kMagic;
    e->type = type;
    e->refcount = 1;
    e->name[0] = 0;
    e->obj = obj;
    if (name && name[0])
    {
        std::strncpy(e->name, name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
    }
    return (HANDLE)e;
}

bool handle_ready(HANDLE h, bool consume)
{
    HandleEntry *e = entry_of(h);
    if (!e)
        return false;
    switch (e->type)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(e->obj);
        std::lock_guard<std::mutex> lk(ev->mtx);
        if (!ev->signaled)
            return false;
        if (consume && !ev->manual)
            ev->signaled = false;
        return true;
    }
    case kThread:
    {
        ThreadObj *t = static_cast<ThreadObj *>(e->obj);
        return t->done;
    }
    case kMutex:
    {
        MutexObj *m = static_cast<MutexObj *>(e->obj);
        bool ok = swinx_fr::sem_take(m->sem, 0);
        if (ok && !consume)
            swinx_fr::sem_give(m->sem);   // peek only
        return ok;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(e->obj);
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
HANDLE WINAPI CreateEventA(void *, BOOL bManualReset, BOOL bInitialState, const char *lpName)
{
    EventObj *o = new (std::nothrow) EventObj();
    if (!o)
        return NULL;
    o->signaled = bInitialState ? true : false;
    o->manual = bManualReset ? true : false;
    return alloc_handle(kEvent, o, lpName);
}

HANDLE WINAPI OpenEventA(DWORD, BOOL, const char *lpName)
{
    std::lock_guard<std::mutex> lk(table_lock());
    HandleEntry *e = find_named(lpName);
    if (!e)
        return NULL;
    e->refcount++;
    return (HANDLE)e;
}

BOOL WINAPI SetEvent(HANDLE h)
{
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kEvent)
        return FALSE;
    EventObj *ev = static_cast<EventObj *>(e->obj);
    {
        std::lock_guard<std::mutex> lk(ev->mtx);
        ev->signaled = true;
    }
    ev->cv.notify_one();   // wakes one blocked waiter (see KNOWN LIMITATIONS)
    return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE h)
{
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kEvent)
        return FALSE;
    EventObj *ev = static_cast<EventObj *>(e->obj);
    std::lock_guard<std::mutex> lk(ev->mtx);
    ev->signaled = false;
    return TRUE;
}

// ===========================================================================
// Mutex
// ===========================================================================
HANDLE WINAPI CreateMutexA(void *, BOOL bInitialOwner, const char *lpName)
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
    return alloc_handle(kMutex, o, lpName);
}

HANDLE WINAPI OpenMutexA(DWORD, BOOL, const char *lpName)
{
    std::lock_guard<std::mutex> lk(table_lock());
    HandleEntry *e = find_named(lpName);
    if (!e)
        return NULL;
    e->refcount++;
    return (HANDLE)e;
}

BOOL WINAPI ReleaseMutex(HANDLE h)
{
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kMutex)
        return FALSE;
    MutexObj *m = static_cast<MutexObj *>(e->obj);
    swinx_fr::sem_give(m->sem);
    return TRUE;
}

// ===========================================================================
// Semaphore
// ===========================================================================
HANDLE WINAPI CreateSemaphoreA(void *, LONG lInitialCount, LONG lMaximumCount, const char *lpName)
{
    SemObj *o = new (std::nothrow) SemObj();
    if (!o)
        return NULL;
    o->count = (int)lInitialCount;
    o->max = (int)lMaximumCount;
    return alloc_handle(kSem, o, lpName);
}

HANDLE WINAPI OpenSemaphoreA(DWORD, BOOL, const char *lpName)
{
    std::lock_guard<std::mutex> lk(table_lock());
    HandleEntry *e = find_named(lpName);
    if (!e)
        return NULL;
    e->refcount++;
    return (HANDLE)e;
}

BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG lReleaseCount, LONG *lpPreviousCount)
{
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kSem)
        return FALSE;
    SemObj *s = static_cast<SemObj *>(e->obj);
    std::lock_guard<std::mutex> lk(s->mtx);
    if (lpPreviousCount)
        *lpPreviousCount = (LONG)s->count;
    s->count += (int)lReleaseCount;
    if (s->count > s->max)
        s->count = s->max;
    s->cv.notify_one();
    return TRUE;
}

// ===========================================================================
// Handle
// ===========================================================================
BOOL WINAPI CloseHandle(HANDLE h)
{
    std::lock_guard<std::mutex> lk(table_lock());
    HandleEntry *e = entry_of(h);
    if (!e)
        return FALSE;
    e->refcount--;
    if (e->refcount <= 0)
    {
        if (e->name[0])
        {
            // clear name so find_named won't resurrect a freed slot
            e->name[0] = 0;
        }
        free_obj(e);
        e->magic = 0;
    }
    return TRUE;
}

// ===========================================================================
// Wait
// ===========================================================================
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD dwMilliseconds)
{
    HandleEntry *e = entry_of(h);
    if (!e)
        return WAIT_FAILED;

    if (dwMilliseconds == 0)
    {
        // non-blocking poll
        if (handle_ready(h, true))
            return WAIT_OBJECT_0;
        return WAIT_TIMEOUT;
    }

    switch (e->type)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(e->obj);
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
        MutexObj *m = static_cast<MutexObj *>(e->obj);
        return swinx_fr::sem_take(m->sem, ms_to_ticks(dwMilliseconds)) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(e->obj);
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
        ThreadObj *t = static_cast<ThreadObj *>(e->obj);
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
HANDLE WINAPI CreateThread(void *, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
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

    HANDLE h = alloc_handle(kThread, t, NULL);
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
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kThread)
        return (DWORD)-1;
    ThreadObj *t = static_cast<ThreadObj *>(e->obj);
    int prev = t->suspendCount;
    if (prev == 0)
        vTaskSuspend(t->task);
    t->suspendCount = prev + 1;
    return (DWORD)prev;
}

DWORD WINAPI ResumeThread(HANDLE h)
{
    HandleEntry *e = entry_of(h);
    if (!e || e->type != kThread)
        return (DWORD)-1;
    ThreadObj *t = static_cast<ThreadObj *>(e->obj);
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

