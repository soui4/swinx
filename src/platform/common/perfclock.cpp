/*
 * perfclock.cpp -- QueryPerformanceFrequency / QueryPerformanceCounter
 *
 * 这两个 Win32 API 的语义与平台无关：声明计数刻度、读一个单调递增的计数
 * （SDK 明确说 QPC 的起点未定义、只保证单调）。所以这里编**一份**，各平台
 * 只提供「读数」这一个原语，契约见 src/SwinxUtils.h 的
 * swinx_perf_ticks_100ns()：
 *
 *     src/platform/posix/sysapi.cpp      gettimeofday()，1601 纪元
 *     src/platform/freertos/winobjs.cpp  esp_timer / 内核 tick，开机纪元
 *
 * 刻度统一为 10MHz（1 tick = 100ns）：Win32 的惯例单位，也是 posix 端口历来
 * 的取值（SOUI 与 wke 的计时路径按它换算）。改造前两个平台各写一份，posix 是
 * 10MHz、freertos 是 1MHz 微秒，同一段剖析代码在不同平台上要除不同的频率；
 * freertos 还为此长出一个 SwinxProfNowUs() 导出符号（已随本次收敛删除）。
 *
 * 进程级状态：本文件没有。读数的缓存（频率）由调用方自己按需做，QPF 是常量。
 */
#include <windows.h>
#include "SwinxUtils.h"

/* 私有名字，故意不复用 posix/sysapi.cpp 里的 TICKSPERSEC 宏：那个宏还服务于
 * ticks_from_time_t() 的 1601 纪元换算，两处含义相同但生命周期不同，同名宏
 * 跨文件容易让人误以为改一处就够。 */
#define SWINX_PERF_TICKS_PER_SEC 10000000

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    if (!lpFrequency)
        return FALSE;
    lpFrequency->QuadPart = SWINX_PERF_TICKS_PER_SEC;
    return TRUE;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    if (!lpPerformanceCount)
        return FALSE;
    lpPerformanceCount->QuadPart = swinx_perf_ticks_100ns();
    return TRUE;
}
