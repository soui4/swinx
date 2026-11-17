#ifndef __SFR_CONFIG_H_
#define __SFR_CONFIG_H_
/*
 * sfr_config.h -- the one place that knows how the FreeRTOS port is built.
 *
 * "SFR" = SOUI FreeRTOS.  The build system may set any of the historical
 * flags below; everything else in swinx only ever tests the SFR_* switches,
 * so a new configuration never has to be chased through dozens of #ifs.
 *
 *  build flag (what the CMakeLists pass)        SFR_* it produces
 *  ------------------------------------------   -------------------------
 *  SOUI_PLATFORM_FREERTOS                       SFR_PLATFORM = 1
 *  FREERTOS_HOST                                SFR_PLATFORM = 1
 *                                               SFR_HOSTED   = 1
 *  SOUI_FREERTOS_REAL                           SFR_KERNEL_REAL = 1
 *  SOUI_FREERTOS_HOST_EMU                       SFR_KERNEL_EMU  = 1
 *  SOUI_PLATFORM_ESPIDF / ESP_PLATFORM          SFR_ESPIDF       = 1
 *  __XTENSA__ (compiler builtin)                SFR_ARCH_XTENSA  = 1
 *
 * The switches answer two independent questions, which is why they are
 * spelled out separately instead of being re-derived at every call site:
 *
 *   SFR_PLATFORM      this translation unit belongs to the FreeRTOS port's
 *                     platform layer (so the Win32 API is *implemented* here,
 *                     not imported, and no real platform window system exists).
 *
 *   SFR_HOSTED        a full host OS + C runtime sits underneath (Windows or
 *                     POSIX host builds).  On bare metal this is 0, which is
 *                     the same condition as "swinx has to supply the CRT
 *                     surface itself" (_wtof, wcscpy_s, __int64, ...).
 *
 *   SFR_KERNEL_REAL   call the real FreeRTOS kernel API (taskENTER_CRITICAL,
 *                     xSemaphoreTake, ...).  True for the bare-metal target
 *                     and for the MSVC-MinGW port that runs on a host.
 *
 *   SFR_KERNEL_EMU    the same primitives emulated with POSIX calls (used by
 *                     test builds that do not link the kernel).
 *
 *   SFR_ESPIDF        ESP-IDF is the RTOS/CRT provider.  It is a THIRD runtime
 *                     shape: a full newlib + libstdc++ + VFS + pthread CRT, but
 *                     no host window system.  SFR_HOSTED cannot express it --
 *                     HOSTED=1 means "the Win32 API comes from a host OS",
 *                     HOSTED=0 means "swinx must supply the CRT surface".
 *                     The esp32s3 skeleton therefore builds with
 *                     SFR_PLATFORM=1 / SFR_HOSTED=0 / SFR_KERNEL_REAL=1 and
 *                     SFR_ESPIDF=1; the ~25 SFR_HOSTED call sites (strapi.h,
 *                     sysapi.h, ctypes.h, fileapi.h, synhandle.h,
 *                     sysbridge.cpp, ...) still need per-site calibration
 *                     against IDF's newlib.  This switch is the hook for that
 *                     work and deliberately changes NO existing decision yet.
 *
 *   SFR_ARCH_XTENSA   derived from the compiler builtin, never from a build
 *                     flag.  It selects the ESP32-S3 critical-section form in
 *                     winobjs.cpp instead of the Cortex-M3 PRIMASK assembly.
 *
 * SFR_KERNEL_REAL and SFR_KERNEL_EMU are mutually exclusive and both 0 is a
 * legal configuration (nothing in the port needs kernel primitives yet).
 *
 * Every switch is defined in both states, on every platform, so #if works
 * without #ifdef and "not defined" can never be mistaken for "0".
 */

#if defined(SOUI_PLATFORM_ESPIDF) || defined(ESP_PLATFORM)
#define SFR_ESPIDF 1
#else
#define SFR_ESPIDF 0
#endif

#if defined(__XTENSA__)
#define SFR_ARCH_XTENSA 1
#else
#define SFR_ARCH_XTENSA 0
#endif

#if defined(FREERTOS_HOST)
#define SFR_PLATFORM 1
#define SFR_HOSTED   1
#elif defined(SOUI_PLATFORM_FREERTOS)
#define SFR_PLATFORM 1
#define SFR_HOSTED   0
#else
#define SFR_PLATFORM 0
#define SFR_HOSTED   0
#endif

#if defined(SOUI_FREERTOS_REAL)
#define SFR_KERNEL_REAL 1
#else
#define SFR_KERNEL_REAL 0
#endif

#if defined(SOUI_FREERTOS_HOST_EMU)
#define SFR_KERNEL_EMU 1
#else
#define SFR_KERNEL_EMU 0
#endif

#if SFR_KERNEL_REAL && SFR_KERNEL_EMU
#error "SOUI_FREERTOS_REAL and SOUI_FREERTOS_HOST_EMU are mutually exclusive"
#endif

#endif // __SFR_CONFIG_H_
