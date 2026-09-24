/*
 * swinx_freertos_api.h - thin abstraction over the RTOS primitive set that the
 * STL-compat shims (std::mutex / std::thread / std::condition_variable /
 * std::chrono) need.
 *
 * Three configurations (selected by macros, never by editing this file):
 *
 *   * SOUI_FREERTOS_REAL      -> forwards to the real FreeRTOS kernel API
 *                                (FreeRTOS.h / task.h / semphr.h). Use this
 *                                when cross-compiling for the actual MCU.
 *
 *   * SOUI_FREERTOS_HOST_EMU  -> emulates the same primitives with POSIX
 *                                pthreads + condvars so the FreeRTOS-backed
 *                                shims can be COMPILED AND RUN on a Linux/macOS
 *                                host (e.g. to unit-test the wrappers without
 *                                target hardware).
 *
 *   * (neither)               -> this header is simply not pulled in; the
 *                                compat standard headers do an #include_next
 *                                so swinx keeps using the host's real std
 *                                library. This is the default when you build
 *                                the freeRTOS config on a workstation just to
 *                                validate the CMake wiring.
 *
 * Everything lives in namespace swinx_fr so it never pollutes ::std.
 *
 * IMPORTANT: all #includes (standard or RTOS headers) are placed OUTSIDE
 * namespace swinx_fr -- pulling a standard library header inside a namespace
 * re-opens std inside it and breaks the whole C++ standard library.
 *
 * NOTE: tick durations. On a real target a "tick" is configTICK_RATE_HZ based.
 * On the host emulation we treat the tick argument as MILLISECONDS for
 * simplicity (there is no real tick there). wait_forever() returns the magic
 * value that means "block without timeout" in both backends.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(SOUI_FREERTOS_REAL) || defined(SOUI_FREERTOS_HOST_EMU)

// ---- backend headers (must be OUTSIDE namespace swinx_fr) -----------------
#if defined(SOUI_FREERTOS_REAL)
    // FreeRTOS headers are plain C; wrap them so they get C linkage when this
    // header is consumed from a C++ translation unit (the swinx STL shims are
    // pulled into std:: from C++). Nested extern "C" is harmless even if the
    // headers already guard themselves.
    extern "C" {
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
    } // extern "C"
#else // SOUI_FREERTOS_HOST_EMU
    #include <pthread.h>
    #include <time.h>
    #include <chrono>
    #ifdef _WIN32
        #include <windows.h>
    #endif
#endif

namespace swinx_fr {

constexpr uint32_t wait_forever() { return 0xFFFFFFFFu; }

// ---------------------------------------------------------------------------
// Backend: REAL FreeRTOS
// ---------------------------------------------------------------------------
#if defined(SOUI_FREERTOS_REAL)

    using mutex_handle = SemaphoreHandle_t;
    using sem_handle   = SemaphoreHandle_t;
    using task_handle  = TaskHandle_t;

    inline mutex_handle mutex_create()             { return xSemaphoreCreateMutex(); }
    inline mutex_handle mutex_create_recursive()   { return xSemaphoreCreateRecursiveMutex(); }
    inline void  mutex_delete(mutex_handle& h)      { if (h) vSemaphoreDelete(h); }
    inline bool  mutex_take(mutex_handle& h, uint32_t ticks)       { return xSemaphoreTake(h, (TickType_t)ticks) == pdTRUE; }
    inline void  mutex_give(mutex_handle& h)         { xSemaphoreGive(h); }
    inline bool  mutex_take_recursive(mutex_handle& h, uint32_t ticks) { return xSemaphoreTakeRecursive(h, (TickType_t)ticks) == pdTRUE; }
    inline void  mutex_give_recursive(mutex_handle& h) { xSemaphoreGiveRecursive(h); }

    inline sem_handle sem_create()                 { return xSemaphoreCreateBinary(); }
    inline sem_handle sem_create_counting(uint32_t max, uint32_t init) {
        return xSemaphoreCreateCounting((UBaseType_t)max, (UBaseType_t)init);
    }
    inline void  sem_delete(sem_handle& h)          { if (h) vSemaphoreDelete(h); }
    inline bool  sem_give(sem_handle& h)            { return xSemaphoreGive(h) == pdTRUE; }
    inline bool  sem_take(const sem_handle& h, uint32_t ticks) { return xSemaphoreTake(h, (TickType_t)ticks) == pdTRUE; }

    inline uint32_t ticks_now()                    { return (uint32_t)xTaskGetTickCount(); }
    inline void  delay_ticks(uint32_t t)           { vTaskDelay((TickType_t)t); }
    inline uint32_t tick_rate_hz()                 { return (uint32_t)configTICK_RATE_HZ; }

    inline task_handle task_create(void (*entry)(void*), void* arg,
                                  const char* name,
                                  uint32_t stack_depth_words,
                                  unsigned priority) {
        TaskHandle_t h = nullptr;
        BaseType_t r = xTaskCreate(entry, name, (configSTACK_DEPTH_TYPE)stack_depth_words,
                                   arg, (UBaseType_t)priority, &h);
        return (r == pdPASS) ? h : nullptr;
    }
    inline void  task_delete(task_handle h)        { if (h) vTaskDelete(h); }
    inline task_handle task_current()              { return xTaskGetCurrentTaskHandle(); }
    inline void  task_yield()                      { taskYIELD(); }
    inline uintptr_t task_id(task_handle h)        { return (uintptr_t)h; }
    inline bool task_handle_valid(task_handle h)   { return h != nullptr; }

    // Sensible defaults for the FreeRTOS port; override per call site if needed.
    // 1024 words (4 KiB) per task keeps the multi-thread fun_test cases inside
    // the 48 KiB lm3s6965 SRAM heap (6 concurrent tasks worst case ~= 24 KiB
    // stacks + kernel objects). Bump per call site if a thread needs more.
    constexpr uint32_t default_stack_depth_words() { return 1024u; }
    constexpr unsigned default_priority()          { return 1u; }

// ---------------------------------------------------------------------------
// Backend: HOST EMULATION (pthreads)
// ---------------------------------------------------------------------------
#else // SOUI_FREERTOS_HOST_EMU

    struct mutex_handle {
        pthread_mutex_t m;
    };
    struct sem_handle {
        pthread_mutex_t m;
        pthread_cond_t  c;
        int count;   // number of signals available
        int max;     // capacity (1 for a binary sem, N for a counting sem)
    };
    struct task_handle {
        pthread_t t;
    };

    inline mutex_handle mutex_create() {
        mutex_handle h;
        pthread_mutex_init(&h.m, nullptr);
        return h;
    }
    inline mutex_handle mutex_create_recursive() {
        mutex_handle h;
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&h.m, &a);
        pthread_mutexattr_destroy(&a);
        return h;
    }
    inline void mutex_delete(mutex_handle& h) { pthread_mutex_destroy(&h.m); }
    inline bool mutex_take(mutex_handle& h, uint32_t) { return pthread_mutex_lock(&h.m) == 0; }
    inline void mutex_give(mutex_handle& h) { pthread_mutex_unlock(&h.m); }
    inline bool mutex_take_recursive(mutex_handle& h, uint32_t) { return pthread_mutex_lock(&h.m) == 0; }
    inline void mutex_give_recursive(mutex_handle& h) { pthread_mutex_unlock(&h.m); }

    inline sem_handle sem_create() {
        sem_handle h;
        pthread_mutex_init(&h.m, nullptr);
        pthread_cond_init(&h.c, nullptr);
        h.count = 0;
        h.max = 1;   // binary semaphore
        return h;
    }
    inline sem_handle sem_create_counting(uint32_t max, uint32_t init) {
        sem_handle h;
        pthread_mutex_init(&h.m, nullptr);
        pthread_cond_init(&h.c, nullptr);
        h.count = (int)init;
        h.max = (int)max;
        return h;
    }
    inline void sem_delete(sem_handle& h) { pthread_mutex_destroy(&h.m); pthread_cond_destroy(&h.c); }
    inline bool sem_give(sem_handle& h) {
        pthread_mutex_lock(&h.m);
        if (h.count < h.max) {      // counting sem: never exceed capacity
            h.count++;
            pthread_cond_signal(&h.c);
        }
        pthread_mutex_unlock(&h.m);
        return true;
    }
    inline bool sem_take(sem_handle& h, uint32_t ms) {
        pthread_mutex_lock(&h.m);
        if (h.count > 0) { h.count--; pthread_mutex_unlock(&h.m); return true; }
        if (ms == 0) { pthread_mutex_unlock(&h.m); return false; }
        if (ms == wait_forever()) {
            pthread_cond_wait(&h.c, &h.m);
            if (h.count > 0) { h.count--; pthread_mutex_unlock(&h.m); return true; }
            pthread_mutex_unlock(&h.m);
            return false;
        }
        auto now = std::chrono::system_clock::now() + std::chrono::milliseconds((int64_t)ms);
        std::time_t sec = std::chrono::system_clock::to_time_t(now);
        struct timespec ts;
        ts.tv_sec  = sec;
        ts.tv_nsec = (long)std::chrono::duration_cast<std::chrono::nanoseconds>(
                         now - std::chrono::system_clock::from_time_t(sec)).count();
        int r = pthread_cond_timedwait(&h.c, &h.m, &ts);
        if (r == 0 && h.count > 0) { h.count--; pthread_mutex_unlock(&h.m); return true; }
        pthread_mutex_unlock(&h.m);
        return false;
    }

    inline uint32_t ticks_now() {
        using namespace std::chrono;
        return (uint32_t)duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch()).count();
    }
    inline void delay_ticks(uint32_t ms) {
        if (ms == 0) return;
    #ifdef _WIN32
        Sleep((DWORD)ms);
    #else
        struct timespec req = { (time_t)(ms / 1000u), (long)((ms % 1000u) * 1000000u) };
        nanosleep(&req, nullptr);
    #endif
    }
    inline uint32_t tick_rate_hz() { return 1000u; }

    inline task_handle task_create(void (*entry)(void*), void* arg,
                                   const char*, uint32_t, unsigned) {
        task_handle h;
        if (pthread_create(&h.t, nullptr, (void*(*)(void*))entry, arg) != 0) {
            h.t = 0;
        }
        return h;
    }
    inline void task_delete(task_handle h) {
        if (h.t) { pthread_detach(h.t); }   // emulate FreeRTOS vTaskDelete by detaching
    }
    inline task_handle task_current() {
        task_handle h;
        h.t = pthread_self();
        return h;
    }
    inline void task_yield() {
    #ifdef _WIN32
        SwitchToThread();
    #else
        sched_yield();
    #endif
    }
    inline uintptr_t task_id(task_handle h) { return (uintptr_t)(h.t); }
    inline bool task_handle_valid(task_handle h) { return h.t != 0; }

    constexpr uint32_t default_stack_depth_words() { return 0u; }
    constexpr unsigned default_priority()          { return 0u; }

#endif // backends

} // namespace swinx_fr

#endif // SOUI_FREERTOS_REAL || SOUI_FREERTOS_HOST_EMU
