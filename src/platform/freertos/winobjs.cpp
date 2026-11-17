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
 *     the public HANDLE is void* (as on Windows) and points at that wrapper;
 *     CloseHandle()/AddHandleRef() come from the core -- this file defines NO
 *     handle-lifecycle functions of its own.
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
 *   * Under SOUI_FREERTOS_HOST_EMU there is no portable way to freeze one
 *     running pthread (pthread_kill(tid, SIGSTOP) stops the whole process), so
 *     SuspendThread()/ResumeThread() return (DWORD)-1 and a thread created with
 *     CREATE_SUSPENDED starts running. The FreeRTOS target keeps the real
 *     vTaskSuspend/vTaskResume behavior. No in-tree caller uses either API.
 */
#include <sfr_config.h>
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
// Declared by newlib on the bare-metal target too (pthread_self() below maps
// onto the FreeRTOS task identity), so this include is safe in every mode;
// the SFR_KERNEL_EMU irq_save/irq_restore pair really needs it.
#include <pthread.h>

#include "swinx_freertos_api.h"
#include "SwinxUtils.h" // swinx_perf_ticks_100ns() 的契约声明

#if defined(ESP_PLATFORM)
#include <esp_timer.h> /* swinx_perf_ticks_100ns(): monotonic microseconds */
#endif

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
    if (!h || h == INVALID_HANDLE_VALUE)
        return NULL;
    _Handle *handle = AsHandle(h);
    if (handle->type != SYN_OBJ + kind)
        return NULL;
    return handle->ptr;
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

// The 64-bit interlocked ops below cannot use a single atomic instruction on
// 32-bit targets (Xtensa ESP32-S3 and Cortex-M3 both need libatomic, which is
// not linked in the bare-metal builds).  Guard them with a critical section
// instead.  The primitive differs per backend:
//   * SFR_HOSTED        MSVC-MingW port: taskENTER_CRITICAL() (no argument)
//   * SFR_KERNEL_EMU    POSIX host emulation: a process-wide pthread mutex
//   * SFR_ARCH_XTENSA   ESP-IDF Xtensa SMP: taskENTER_CRITICAL(&mux), where
//                       portMUX_TYPE* is MANDATORY in IDF 5.4's portmacro.h
//   * default           Cortex-M3 bare metal: PRIMASK IRQ masking
#if SFR_HOSTED
// Windows-host simulator (MSVC-MingW port): no CPU interrupt masking; use the
// port's critical section instead (nesting is counted per task, so the
// save/restore shape is preserved).
inline uint32_t irq_save(void)
{
    taskENTER_CRITICAL();
    return 0;
}

inline void irq_restore(uint32_t pr)
{
    (void)pr;
    taskEXIT_CRITICAL();
}
#elif SFR_KERNEL_EMU
// Host pthread emulation: there is no interrupt mask, and a per-thread
// SIGSTOP-style freeze is not portable.  The only thing these two helpers
// guard is the pair of 64-bit interlocked ops below, so a process-wide lock
// gives the same mutual exclusion.
inline pthread_mutex_t &irq_mutex(void)
{
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    return mtx;
}

inline uint32_t irq_save(void)
{
    pthread_mutex_lock(&irq_mutex());
    return 0;
}

inline void irq_restore(uint32_t pr)
{
    (void)pr;
    pthread_mutex_unlock(&irq_mutex());
}
#elif SFR_ARCH_XTENSA
// ESP-IDF / Xtensa (ESP32-S3, dual-core SMP FreeRTOS).
//
// IDF 5.4's portmacro.h defines
//   #define portENTER_CRITICAL(mux) vPortEnterCritical(mux)
//   #define portEXIT_CRITICAL(mux)  vPortExitCritical(mux)
// and task.h forwards taskENTER_CRITICAL(x) -> portENTER_CRITICAL(x), so the
// spinlock argument is REQUIRED (unlike the zero-argument MSVC-MingW form
// above).  A single static portMUX_TYPE serialises the two Interlocked64
// helpers across both cores; IDF critical sections are nestable, so the
// save/restore shape is preserved even though there is no PRIMASK to save.
//
// portMUX_TYPE / portMUX_INITIALIZER_UNLOCKED reach this TU through
// swinx_freertos_api.h, which includes FreeRTOS.h + task.h + semphr.h when
// SFR_KERNEL_REAL is set (the esp32s3 CMake component defines
// SOUI_FREERTOS_REAL).  Do NOT include those headers here: this code sits
// inside an anonymous namespace.
static portMUX_TYPE s_interlockMux = portMUX_INITIALIZER_UNLOCKED;

inline uint32_t irq_save(void)
{
    taskENTER_CRITICAL(&s_interlockMux);
    return 0;
}

inline void irq_restore(uint32_t pr)
{
    (void)pr;
    taskEXIT_CRITICAL(&s_interlockMux);
}
#else
// Cortex-M3 bare metal: single core, PRIMASK alone is a full barrier.
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
#endif // SFR_HOSTED / SFR_KERNEL_EMU / SFR_ARCH_XTENSA / bare-metal Cortex-M

// ---- thread trampoline ----------------------------------------------------
void thread_trampoline(void *p)
{
    ThreadObj *t = static_cast<ThreadObj *>(p);
    t->exitCode = t->start(t->param);
    t->done = true;
    swinx_fr::sem_give(t->joinSem);
    // A FreeRTOS task function MUST NOT return.
    swinx_fr::task_exit();
}

// readiness probe used by WaitForMultipleObjects' polling loop
bool handle_ready(HANDLE h, bool consume)
{
    if (!h || h == INVALID_HANDLE_VALUE)
        return false;
    _Handle *handle = AsHandle(h);
    switch (handle->type - SYN_OBJ)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(handle->ptr);
        std::lock_guard<std::mutex> lk(ev->mtx);
        if (!ev->signaled)
            return false;
        if (consume && !ev->manual)
            ev->signaled = false;
        return true;
    }
    case kThread:
    {
        ThreadObj *t = static_cast<ThreadObj *>(handle->ptr);
        return t->done;
    }
    case kMutex:
    {
        MutexObj *m = static_cast<MutexObj *>(handle->ptr);
        bool ok = swinx_fr::sem_take(m->sem, 0);
        if (ok && !consume)
            swinx_fr::sem_give(m->sem);   // peek only
        return ok;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(handle->ptr);
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
    if (!swinx_fr::sem_handle_valid(o->sem))
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

    _Handle *handle = AsHandle(h);
    switch (handle->type - SYN_OBJ)
    {
    case kEvent:
    {
        EventObj *ev = static_cast<EventObj *>(handle->ptr);
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
        MutexObj *m = static_cast<MutexObj *>(handle->ptr);
        return swinx_fr::sem_take(m->sem, ms_to_ticks(dwMilliseconds)) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    case kSem:
    {
        SemObj *s = static_cast<SemObj *>(handle->ptr);
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
        ThreadObj *t = static_cast<ThreadObj *>(handle->ptr);
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
    if (!swinx_fr::sem_handle_valid(t->joinSem))
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
        swinx_fr::task_delete(t->task);
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
    // task_suspend reports false where the backend has no freeze primitive
    // (the host emulation); fail the call instead of leaving the caller
    // believing a running thread stopped.
    if (prev == 0 && !swinx_fr::task_suspend(t->task))
        return (DWORD)-1;
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
        if (t->suspendCount == 0 && !swinx_fr::task_resume(t->task))
            return (DWORD)-1;
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
// 32 位那一族（Increment/Decrement/Exchange/ExchangeAdd/CompareExchange）已下沉到
// src/platform/common/interlocked.cpp —— 一条 LDREX/STREX 就能编出来，与平台无关，
// 而且合并时抓到 posix 那份 ExchangeAdd 返回值是错的（返回加完之后的值）。
// 这里只留 64 位：32 位核上 8 字节原子要么调 libatomic、要么关中断，实现按平台走。
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
//
// NOT under SOUI_FREERTOS_HOST_EMU: there swinx_fr::task_current() asks the
// host's pthread_self(), so defining this symbol here would replace libc's
// and the two would call each other forever (GetCurrentThreadId ->
// task_current -> our pthread_self -> GetCurrentThreadId -> stack overflow).
// The host already has a real pthread_self, and its pthread_t is exactly the
// per-thread identity the port needs.
// ---------------------------------------------------------------------------
#include <pthread.h>

// SFR_ESPIDF: ESP-IDF ships libpthread, which already provides pthread_self().
// Keeping this definition would be a duplicate symbol at link time -- and
// letting libpthread's version win would be worse than useless (it returns an
// esp_pthread_t* and assert()s when the caller is not a pthread).  Nothing in
// this port calls pthread_self() anyway: wnd.cpp uses GetCurrentThreadId()
// under SFR_PLATFORM, see wnd.cpp:379.
#if !SFR_KERNEL_EMU && !SFR_ESPIDF
pthread_t pthread_self(void)
{
    return (pthread_t)GetCurrentThreadId();
}
#endif

// ---------------------------------------------------------------------------
// QueryPerformanceCounter() 的 tick 源（平台助手契约，见 src/SwinxUtils.h）
//
// QPF/QPC 本身只有一份，在 src/platform/common/perfclock.cpp（频率 10MHz，
// 与 posix 端口同刻度）：那是 Win32 语义，与「时间从哪来」无关。这里只回答
// 「现在多少 tick」。
//
// 放在 extern "C" 块之外是有意的：SwinxUtils.h 里这批平台助手用 C++ linkage
// 声明（同 swinx_messageBeep），定义跟着走，否则链接期会因为两套名字修饰而
// 报 "conflicting declaration with C linkage" —— 注意 windows.h 里的 Win32
// 符号不受影响，它们由头文件自己给出的 extern "C" 决定。
//
// 时间源：
//   * ESP-IDF: esp_timer_get_time() 已经是单调微秒，x10 换到 100ns 刻度；
//     纪元是开机时刻而不是 1601 —— QPC 只用来算区间，不影响任何调用方。
//   * 其它（host 模拟 / 无 esp_timer 的裸机）: 内核 tick 是毫秒，x10000 换算。
//     精度只有 1ms，但保持"同一频率、单调"的契约，剖析代码不必分平台。
//
// 历史：这个端口一度完全没有 QPF/QPC，剖析器只好自造一个导出的
// SwinxProfNowUs()，每个想取时间戳的 TU 都带一份 weak 声明；现在统一回到
// Win32 已经声明的这两个 API 上。
// ---------------------------------------------------------------------------
long long swinx_perf_ticks_100ns(void)
{
#if defined(ESP_PLATFORM)
    return (long long)esp_timer_get_time() * 10;
#else
    return (long long)swinx_fr::ticks_now() * 10000;
#endif
}
