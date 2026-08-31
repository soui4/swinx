#ifndef _SWINX_MOBILE_UUID_UUID_H_
#define _SWINX_MOBILE_UUID_UUID_H_

#include <stdint.h>
#include <string.h>
#include <chrono>
#include <random>
#include <thread>

typedef unsigned char uuid_t[16];

static inline void uuid_generate(uuid_t out)
{
    static std::random_device rd;
    static uint64_t counter = 0;

    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const uint64_t tid = static_cast<uint64_t>(
        std::hash<std::thread::id>()(std::this_thread::get_id()));
    const uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ now ^ tid ^ ++counter;

    memcpy(out, &seed, sizeof(seed));
    uint64_t tail = seed ^ 0x9e3779b97f4a7c15ULL ^ (now << 7);
    memcpy(out + sizeof(seed), &tail, sizeof(tail));

    out[6] = (out[6] & 0x0f) | 0x40;
    out[8] = (out[8] & 0x3f) | 0x80;
}

#endif // _SWINX_MOBILE_UUID_UUID_H_
