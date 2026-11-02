/*
 * uuid.h -- FreeRTOS bridge for <uuid/uuid.h> (libuuid does not exist on a
 * bare-metal target).  Mirrors src/platform/mobile/uuid/uuid.h in shape but
 * seeds from the FreeRTOS tick instead of std::random_device / std::thread,
 * which newlib-nano + the STL shims do not fully provide.
 */
#ifndef _SWINX_FREERTOS_UUID_UUID_H_
#define _SWINX_FREERTOS_UUID_UUID_H_

#include <stdint.h>
#include <string.h>
#include <sfr_config.h>

/* 种子的来源按内核形态选，和 stl/swinx_freertos_api.h 用同一组开关：
 * REAL 用 FreeRTOS tick；主机 emu / passthrough 模式下根本没有 FreeRTOS.h，
 * 用 POSIX 单调时钟取同一个 32 位毫秒值。原来这里无条件 #include <FreeRTOS.h>，
 * 于是 -DSOUI_PLATFORM=freertos -DSOUI_FREERTOS_EMULATE_HOST=ON 的主机编译测试
 * 在第一个 include 它的 TU（src/objbase.cpp）就 fatal error。 */
#if SFR_KERNEL_REAL
#include <FreeRTOS.h>
#include <task.h>
static inline uint32_t swinxUuidTickMs(void)
{
    return (uint32_t)xTaskGetTickCount();
}
#else /* !SFR_KERNEL_REAL */
#include <time.h>
static inline uint32_t swinxUuidTickMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}
#endif /* SFR_KERNEL_REAL */

typedef unsigned char uuid_t[16];

static inline void uuid_generate(uuid_t out)
{
    static uint32_t counter = 0;

    const uint64_t now = (uint64_t)swinxUuidTickMs();
    const uint64_t seed = now ^ ((uint64_t)(uintptr_t)out << 32) ^ ((uint64_t)++counter << 16)
                          ^ 0x9e3779b97f4a7c15ULL;

    memcpy(out, &seed, sizeof(seed));
    uint64_t tail = seed ^ 0x2545F4914F6CDD1DULL ^ (now << 7);
    memcpy(out + sizeof(seed), &tail, sizeof(tail));

    out[6] = (out[6] & 0x0f) | 0x40; // version 4
    out[8] = (out[8] & 0x3f) | 0x80; // variant 10
}

#endif //_SWINX_FREERTOS_UUID_UUID_H_
