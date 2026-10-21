/*
 * swinx/platform/freertos/sys/mman.h -- FreeRTOS stub for <sys/mman.h>.
 *
 * Bare-metal newlib has no mmap; SOUI sources include this header
 * unconditionally in their POSIX branch but never call the entry points.
 * The freertos include path (src/platform/freertos) precedes the toolchain
 * dirs, so this file shadows the toolchain's <sys/mman.h> if it provides one.
 * Real implementations live in a swinx platform shim if ever necessary.
 */
#ifndef SWINX_FREERTOS_SYS_MMAN_H_
#define SWINX_FREERTOS_SYS_MMAN_H_

#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01
#define MAP_FAILED  ((void *)-1)

#endif /* SWINX_FREERTOS_SYS_MMAN_H_ */
