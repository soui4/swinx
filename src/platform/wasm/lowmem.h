/*
 * lowmem.h -- wasm 端口对 Win32「低 64 KB 没有指针」这一隐含前提的启动自检。
 *
 * Win32 用 IS_INTRESOURCE(p) == (((uintptr_t)p >> 16) == 0) 区分「字符串指针」与
 * MAKEINTRESOURCE 整数（include/winuser.h）。桌面平台由用户态地址空间布局保证真实指针
 * 永远不落在低 64 KB，所以这个判据成立；wasm32 的线性内存从 0 开始，数据段位置由链接
 * 选项决定（emcc -O1 及以上默认 --no-stack-first --global-base=1024），前提不再成立。
 *
 * 后果不是"画错一点"，而是窗口建不出来：类名 "HOSTWND" 的地址若在低 64 KB，会被当成
 * 原子号 LOWORD(ptr)，GetAtomNameA 查不到，CreateWindowExA 在进 WIN_CreateWindowEx
 * 之前就返回 0；这条路径又不设 LastError，调用方看到的是"Create 失败且 err=0"。
 *
 * 所以在两条最早入口上做一次幂等自检：第一次登记窗口类（类名→原子）与第一次建立
 * SConnection（DC / 窗口 / 消息）。前提不满足时立即终止并打印链接选项的修法，不让问题
 * 以"布局 XML 或控件类注册有问题"的形态出现在上层。
 */
#ifndef _SWINX_WASM_LOWMEM_H_
#define _SWINX_WASM_LOWMEM_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 幂等：第一次调用做一次探针检查，之后直接返回。违反前提时 fprintf + abort()。 */
void swinx_wasmCheckLowMemoryFree(void);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_WASM_LOWMEM_H_
