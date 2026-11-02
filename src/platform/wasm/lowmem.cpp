/*
 * lowmem.cpp -- 低 64 KB 布局自检的实现，成因与调用点见 lowmem.h。
 *
 * 只在 wasm 构建里编译（swinx/wasm.cmake 的 GLOB 覆盖 src/platform/wasm 目录下的 .cpp）。
 */
#include "lowmem.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Win32 的 MAKEINTRESOURCE 编码在 16 位以内，系统颜色索引更是只有几十，所以低 64 KB 里
 * 除了"整数"不该有任何东西。真实指针的下限就是这里；正好等于 0x10000 时
 * (p >> 16) == 1，IS_INTRESOURCE 为假，是合法的字符串地址。 */
#define SWINX_WASM_PTR_FLOOR 0x10000u

/* wasm-ld 在带 --global-base 链接时定义这个符号，它就是静态数据段的下界：所有数据段都
 * 排在它之后。emcc 无论用哪种布局都会传这个选项（--stack-first 时等于栈大小，
 * --no-stack-first 时等于 --global-base 的值），所以它比"取一个字面量的地址"可靠——
 * 后者只代表那个字面量所在的段，实测同一份模块里字面量可以分散在 0x400 到 0xE9080
 * 之间，探针落在高位时会把坏布局误判成好布局。
 * 声明成 weak：万一某个工具链不定义它，退回到字面量探针。 */
extern "C" {
extern const char __global_base[] __attribute__((weak));
}

void swinx_wasmCheckLowMemoryFree(void)
{
    static bool s_checked = false;
    if (s_checked)
        return;
    s_checked = true;

    /* 字面量探针：与窗口类名、资源名、光标名同处只读数据，作为 __global_base 缺失时的
     * 兜底信号。取地址本身会阻止常量折叠。 */
    static const char s_probe[] = "swinx wasm low-memory probe";
    uintptr_t probe = (uintptr_t)s_probe;
    uintptr_t floor = (uintptr_t)__global_base;
    if (floor != 0 && floor >= SWINX_WASM_PTR_FLOOR && probe >= SWINX_WASM_PTR_FLOOR)
        return;

    fprintf(stderr,
            "[swinx/wasm] 致命：静态数据落在低 64 KB（数据段下界 0x%x，字面量探针 0x%x）。"
            "Win32 用 IS_INTRESOURCE(p)=((p>>16)==0) 区分字符串指针与 MAKEINTRESOURCE 整数；"
            "指针落在这里时窗口类名会被当成原子号，CreateWindowEx 返回 0，"
            "而 GetLastError() 仍是 0。\n"
            "  修法：链接时加 -Wl,--stack-first（栈不小于 64 KB），"
            "或 -Wl,--no-stack-first -Wl,--global-base=65536（也可 -sGLOBAL_BASE=65536）。\n"
            "  注意 --global-base 必须带 -Wl, 前缀：裸写会被 emcc 在 -O1 及以上的默认值覆盖，"
            "等于没写。\n",
            (unsigned)floor, (unsigned)probe);
    abort();
}
