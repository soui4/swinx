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
#include <FreeRTOS.h>
#include <task.h>

typedef unsigned char uuid_t[16];

static inline void uuid_generate(uuid_t out)
{
    static uint32_t counter = 0;

    const uint64_t now = (uint64_t)xTaskGetTickCount();
    const uint64_t seed = now ^ ((uint64_t)(uintptr_t)out << 32) ^ ((uint64_t)++counter << 16)
                          ^ 0x9e3779b97f4a7c15ULL;

    memcpy(out, &seed, sizeof(seed));
    uint64_t tail = seed ^ 0x2545F4914F6CDD1DULL ^ (now << 7);
    memcpy(out + sizeof(seed), &tail, sizeof(tail));

    out[6] = (out[6] & 0x0f) | 0x40; // version 4
    out[8] = (out[8] & 0x3f) | 0x80; // variant 10
}

#endif //_SWINX_FREERTOS_UUID_UUID_H_
