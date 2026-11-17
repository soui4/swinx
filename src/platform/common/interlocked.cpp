/*
 * interlocked.cpp -- 32 位 Interlocked 家族（KERNEL32 原子操作）
 *
 * 32 位原子在所有目标上都能编成一条指令（ARM 是 LDREX/STREX，x86 是 lock xadd），
 * 实现与平台无关，所以只留这一份：posix/sysapi.cpp 与 freertos/winobjs.cpp 里
 * 原先各有一份逐字拷贝。
 *
 * 合并时抓到一处真实分叉 —— InterlockedExchangeAdd 的返回值：
 *     posix    `__sync_add_and_fetch()`  → 返回**加完之后**的值（错）
 *     freertos `__atomic_fetch_add()`    → 返回**加之前**的值（对）
 * Win32 的语义是返回旧值（"加之前是多少"），这里按 Win32 统一。原来依赖旧值的
 * 调用方在 posix 家族上会拿到差一个增量的结果，这正是"两份实现靠注释保持同步"
 * 的典型代价。
 *
 * 64 位那一半**不在这里**：32 位核上 8 字节原子要么调 libatomic、要么关中断，
 * 各平台不同（freertos 用 irq_save 包住 ++/--，posix 用 __atomic_*），仍留在
 * 各自的平台目录。混在一起会逼着这里出现 #ifdef，那就不是中立层了。
 */
#include <windows.h>
#include "sysapi.h"

LONG WINAPI InterlockedIncrement(LONG volatile *v)
{
    return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
}

LONG WINAPI InterlockedDecrement(LONG volatile *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

LONG WINAPI InterlockedExchange(LONG volatile *Target, LONG Value)
{
    // Win32 semantics: unconditional swap, returning the ORIGINAL value.
    return __atomic_exchange_n(Target, Value, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedExchangeAdd(LONG volatile *v, LONG Increment)
{
    // Win32 semantics: return the value BEFORE the add (see file header).
    return __atomic_fetch_add(v, Increment, __ATOMIC_SEQ_CST);
}

LONG WINAPI InterlockedCompareExchange(LONG volatile *v, LONG Exchange, LONG Comparand)
{
    // Win32 semantics: return the ORIGINAL value of *v.
    // On failure __atomic_compare_exchange_n stores the original into
    // Comparand; on success Comparand already holds it (it matched).
    __atomic_compare_exchange_n(v, &Comparand, Exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return Comparand;
}
