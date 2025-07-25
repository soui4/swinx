# SIMD feature detection for Pixman

include(CheckCSourceCompiles)

# Initialize SIMD variables
set(HAVE_LOONGSON_MMI FALSE)
set(HAVE_MMX FALSE)
set(HAVE_SSE2 FALSE)
set(HAVE_SSSE3 FALSE)
set(HAVE_VMX FALSE)
set(HAVE_ARMV6_SIMD FALSE)
set(HAVE_NEON FALSE)
set(HAVE_A64_NEON FALSE)
set(HAVE_MIPS_DSPR2 FALSE)
set(HAVE_RVV FALSE)

# Initialize flag variables
set(LOONGSON_MMI_FLAGS "")
set(MMX_FLAGS "")
set(SSE2_FLAGS "")
set(SSSE3_FLAGS "")
set(VMX_FLAGS "")
set(MIPS_DSPR2_FLAGS "")
set(RVV_FLAGS "")

# Loongson MMI detection
if(NOT PIXMAN_ENABLE_LOONGSON_MMI STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "mips64")
        set(CMAKE_REQUIRED_FLAGS "-mloongson-mmi")
        set(CMAKE_REQUIRED_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
        check_c_source_compiles("
            #ifndef __mips_loongson_vector_rev
            #error \"Loongson Multimedia Instructions are only available on Loongson\"
            #endif
            #if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4))
            #error \"Need GCC >= 4.4 for Loongson MMI compilation\"
            #endif
            #include \"pixman/loongson-mmintrin.h\"
            int main () {
              union {
                __m64 v;
                char c[8];
              } a = { .c = {1, 2, 3, 4, 5, 6, 7, 8} };
              int b = 4;
              __m64 c = _mm_srli_pi16 (a.v, b);
              return 0;
            }
        " HAVE_LOONGSON_MMI)
        set(CMAKE_REQUIRED_FLAGS)
        set(CMAKE_REQUIRED_INCLUDES)
        
        if(HAVE_LOONGSON_MMI)
            set(LOONGSON_MMI_FLAGS "-mloongson-mmi")
            set(USE_LOONGSON_MMI TRUE)
        endif()
    endif()
    
    if(NOT HAVE_LOONGSON_MMI AND PIXMAN_ENABLE_LOONGSON_MMI STREQUAL "ON")
        message(FATAL_ERROR "Loongson MMI Support unavailable, but required")
    endif()
endif()

# MMX detection
if(NOT PIXMAN_ENABLE_MMX STREQUAL "OFF")
    if(MSVC)
        set(MMX_FLAGS "/w14710;/w14714;/wd4244")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "SunPro")
        set(MMX_FLAGS "-xarch=sse")
    else()
        set(MMX_FLAGS "-mmmx;-Winline")
    endif()
    
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR MSVC)
        set(HAVE_MMX TRUE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86")
        set(CMAKE_REQUIRED_FLAGS "${MMX_FLAGS}")
        check_c_source_compiles("
            #include <mmintrin.h>
            #include <stdint.h>

            /* Check support for block expressions */
            #define _mm_shuffle_pi16(A, N)                    \\
              ({                                              \\
              __m64 ret;                                      \\
                                                              \\
              /* Some versions of clang will choke on K */    \\
              asm (\"pshufw %2, %1, %0\\n\\t\"                    \\
                   : \"=y\" (ret)                               \\
                   : \"y\" (A), \"K\" ((const int8_t)N)           \\
              );                                              \\
                                                              \\
              ret;                                            \\
              })

            int main () {
                __m64 v = _mm_cvtsi32_si64 (1);
                __m64 w;

                w = _mm_shuffle_pi16(v, 5);

                /* Some versions of clang will choke on this */
                asm (\"pmulhuw %1, %0\\n\\t\"
                     : \"+y\" (w)
                     : \"y\" (v)
                );

                return _mm_cvtsi64_si32 (v);
            }
        " HAVE_MMX)
        set(CMAKE_REQUIRED_FLAGS)
    endif()
    
    if(HAVE_MMX)
        # Inline assembly do not work on X64 MSVC, so we use
        # compatibility intrinsics there
        if(NOT (MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64"))
            set(USE_X86_MMX TRUE)
        endif()
    elseif(PIXMAN_ENABLE_MMX STREQUAL "ON")
        message(FATAL_ERROR "MMX Support unavailable, but required")
    endif()
endif()

# SSE2 detection
if(NOT PIXMAN_ENABLE_SSE2 STREQUAL "OFF")
    if(CMAKE_C_COMPILER_ID STREQUAL "SunPro")
        set(SSE2_FLAGS "-xarch=sse2")
    elseif(NOT MSVC)
        set(SSE2_FLAGS "-msse2;-Winline")
    endif()
    
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86")
        set(CMAKE_REQUIRED_FLAGS "${SSE2_FLAGS}")
        check_c_source_compiles("
            #if defined(__GNUC__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 2))
            #   if !defined(__amd64__) && !defined(__x86_64__)
            #      error \"Need GCC >= 4.2 for SSE2 intrinsics on x86\"
            #   endif
            #endif
            #include <mmintrin.h>
            #include <xmmintrin.h>
            #include <emmintrin.h>
            int param;
            int main () {
              __m128i a = _mm_set1_epi32 (param), b = _mm_set1_epi32 (param + 1), c;
              c = _mm_xor_si128 (a, b);
              return _mm_cvtsi128_si32(c);
            }
        " HAVE_SSE2)
        set(CMAKE_REQUIRED_FLAGS)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(HAVE_SSE2 TRUE)
    endif()
    
    if(HAVE_SSE2)
        set(USE_SSE2 TRUE)
    elseif(PIXMAN_ENABLE_SSE2 STREQUAL "ON")
        message(FATAL_ERROR "SSE2 Support unavailable, but required")
    endif()
endif()

# SSSE3 detection
if(NOT PIXMAN_ENABLE_SSSE3 STREQUAL "OFF")
    if(NOT MSVC)
        set(SSSE3_FLAGS "-mssse3;-Winline")
    endif()
    
    # x64 pre-2010 MSVC compilers crashes when building the ssse3 code
    set(SKIP_SSSE3 FALSE)
    if(MSVC AND CMAKE_C_COMPILER_VERSION VERSION_LESS "16" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        set(SKIP_SSSE3 TRUE)
    endif()
    
    if(NOT SKIP_SSSE3 AND CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86_64")
        set(CMAKE_REQUIRED_FLAGS "${SSSE3_FLAGS}")
        check_c_source_compiles("
            #include <mmintrin.h>
            #include <xmmintrin.h>
            #include <emmintrin.h>
            int param;
            int main () {
              __m128i a = _mm_set1_epi32 (param), b = _mm_set1_epi32 (param + 1), c;
              c = _mm_xor_si128 (a, b);
              return _mm_cvtsi128_si32(c);
            }
        " HAVE_SSSE3)
        set(CMAKE_REQUIRED_FLAGS)
    endif()
    
    if(HAVE_SSSE3)
        set(USE_SSSE3 TRUE)
    elseif(PIXMAN_ENABLE_SSSE3 STREQUAL "ON")
        message(FATAL_ERROR "SSSE3 Support unavailable, but required")
    endif()
endif()

# VMX detection
if(NOT PIXMAN_ENABLE_VMX STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc")
        set(VMX_FLAGS "-maltivec;-mabi=altivec")
        set(CMAKE_REQUIRED_FLAGS "${VMX_FLAGS}")
        check_c_source_compiles("
            #include <altivec.h>
            int main () {
                vector unsigned int v = vec_splat_u32 (1);
                v = vec_sub (v, v);
                return 0;
            }
        " HAVE_VMX)
        set(CMAKE_REQUIRED_FLAGS)
    endif()
    
    if(HAVE_VMX)
        set(USE_VMX TRUE)
    elseif(PIXMAN_ENABLE_VMX STREQUAL "ON")
        message(FATAL_ERROR "VMX Support unavailable, but required")
    endif()
endif()

# ARM SIMD detection
if(NOT PIXMAN_ENABLE_ARM_SIMD STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        # Try to compile the ARM SIMD test file
        try_compile(HAVE_ARMV6_SIMD
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/arm-simd-test.S
        )
    endif()

    if(HAVE_ARMV6_SIMD)
        set(USE_ARM_SIMD TRUE)
    elseif(PIXMAN_ENABLE_ARM_SIMD STREQUAL "ON")
        message(FATAL_ERROR "ARMv6 SIMD Support unavailable, but required")
    endif()
endif()

# ARM NEON detection
if(NOT PIXMAN_ENABLE_NEON STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
        # Try to compile the NEON test file
        try_compile(HAVE_NEON
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/neon-test.S
        )
    endif()

    if(HAVE_NEON)
        set(USE_ARM_NEON TRUE)
    elseif(PIXMAN_ENABLE_NEON STREQUAL "ON")
        message(FATAL_ERROR "NEON Support unavailable, but required")
    endif()
endif()

# ARM A64 NEON detection
if(NOT PIXMAN_ENABLE_A64_NEON STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        # Try to compile the A64 NEON test file
        try_compile(HAVE_A64_NEON
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/a64-neon-test.S
        )
    endif()

    if(HAVE_A64_NEON)
        set(USE_ARM_A64_NEON TRUE)
    elseif(PIXMAN_ENABLE_A64_NEON STREQUAL "ON")
        message(FATAL_ERROR "A64 NEON Support unavailable, but required")
    endif()
endif()

# MIPS DSPr2 detection
if(NOT PIXMAN_ENABLE_MIPS_DSPR2 STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
        set(MIPS_DSPR2_FLAGS "-mdspr2")
        set(CMAKE_REQUIRED_FLAGS "${MIPS_DSPR2_FLAGS}")
        check_c_source_compiles("
            #if !(defined(__mips__) &&  __mips_isa_rev >= 2)
            #error MIPS DSPr2 is currently only available on MIPS32r2 platforms.
            #endif
            int
            main ()
            {
                int c = 0, a = 0, b = 0;
                __asm__ __volatile__ (
                    \"precr.qb.ph %[c], %[a], %[b]          \\n\\t\"
                    : [c] \"=r\" (c)
                    : [a] \"r\" (a), [b] \"r\" (b)
                );
                return c;
            }
        " HAVE_MIPS_DSPR2)
        set(CMAKE_REQUIRED_FLAGS)
    endif()

    if(HAVE_MIPS_DSPR2)
        set(USE_MIPS_DSPR2 TRUE)
    elseif(PIXMAN_ENABLE_MIPS_DSPR2 STREQUAL "ON")
        message(FATAL_ERROR "MIPS DSPr2 Support unavailable, but required")
    endif()
endif()

# RISC-V Vector detection
if(NOT PIXMAN_ENABLE_RVV STREQUAL "OFF")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        set(RVV_FLAGS "-march=rv64gcv1p0")
        set(CMAKE_REQUIRED_FLAGS "${RVV_FLAGS}")
        check_c_source_compiles("
            #include <riscv_vector.h>
            #include <asm/hwprobe.h>
            #include <linux/version.h>
            #include <sys/auxv.h>
            #include <sys/syscall.h>
            #include <unistd.h>

            #if defined(__riscv_v) && __riscv_v < 1000000
            #error \"Minimum supported RVV is 1.0\"
            #endif
            #if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
            #error \"Minimum supported kernel is 6.5.0\"
            #endif
            int main() {
                struct riscv_hwprobe pair = {RISCV_HWPROBE_KEY_IMA_EXT_0, 0};
                long result = sys_riscv_hwprobe (&pair, 1, 0, 0, 0);
                vfloat32m1_t tmp1; /* added in gcc-13 */
                vfloat32m1x4_t tmp2; /* added in gcc-14 */
                return 0;
            }
        " HAVE_RVV)
        set(CMAKE_REQUIRED_FLAGS)
    endif()

    if(HAVE_RVV)
        set(USE_RVV TRUE)
    elseif(PIXMAN_ENABLE_RVV STREQUAL "ON")
        message(FATAL_ERROR "RISC-V Vector Support unavailable, but required")
    endif()
endif()
