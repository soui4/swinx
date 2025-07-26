# Copyright © 2018 Intel Corporation
# Copyright © 2024 Pixman Contributors

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

cmake_minimum_required(VERSION 3.16)

project(pixman
    VERSION 0.46.5
    LANGUAGES C
    DESCRIPTION "The pixman library (version 1)"
)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build" FORCE)
endif()

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include necessary modules
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckTypeSize)
include(TestBigEndian)
include(GNUInstallDirs)

# Options
option(PIXMAN_BUILD_TESTS "Build tests" ON)
option(PIXMAN_BUILD_DEMOS "Build demos" OFF)
option(PIXMAN_ENABLE_TIMERS "Enable TIMER_* macros" OFF)
option(PIXMAN_ENABLE_GNUPLOT "Enable output of filters that can be piped to gnuplot" OFF)
option(PIXMAN_ENABLE_GTK "Enable demos using GTK" ON)
option(PIXMAN_ENABLE_LIBPNG "Use libpng in tests" ON)
option(PIXMAN_ENABLE_OPENMP "Enable OpenMP for tests" ON)

# SIMD options
option(PIXMAN_ENABLE_LOONGSON_MMI "Use Loongson MMI intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_MMX "Use X86 MMX intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_SSE2 "Use X86 SSE2 intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_SSSE3 "Use X86 SSSE3 intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_VMX "Use PPC VMX/Altivec intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_ARM_SIMD "Use ARMv6 SIMD intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_NEON "Use ARM NEON intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_A64_NEON "Use ARM A64 NEON intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_MIPS_DSPR2 "Use MIPS32 DSPr2 intrinsic optimized paths" AUTO)
option(PIXMAN_ENABLE_RVV "Use RISC-V Vector extension" AUTO)
option(PIXMAN_ENABLE_GNU_INLINE_ASM "Use GNU style inline assembler" AUTO)
option(PIXMAN_ENABLE_TLS "Use compiler support for thread-local storage" AUTO)

# CPU features path for Android
set(PIXMAN_CPU_FEATURES_PATH "" CACHE STRING "Path to platform-specific cpu-features.[ch] for systems that do not provide it (e.g. Android)")

# Compiler flags
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wdeclaration-after-statement
        -fno-strict-aliasing
        -fvisibility=hidden
        -Wundef
        -ftrapping-math
    )

    # Check for and disable specific warnings
    include(CheckCCompilerFlag)
    check_c_compiler_flag(-Wunused-local-typedefs HAS_WUNUSED_LOCAL_TYPEDEFS)
    if(HAS_WUNUSED_LOCAL_TYPEDEFS)
        add_compile_options(-Wno-unused-local-typedefs)
    endif()
endif()

# Set module path for custom modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Add -DHAVE_CONFIG_H to all targets
add_compile_definitions(HAVE_CONFIG_H)

# Initialize configuration
set(PACKAGE "pixman")

# Find dependencies
find_package(Threads)
find_package(PkgConfig)

# Math library
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    set(HAVE_LIBM TRUE)
endif()

# OpenMP
if(PIXMAN_ENABLE_OPENMP AND NOT MSVC)
    find_package(OpenMP COMPONENTS C)
    if(OpenMP_C_FOUND)
        set(USE_OPENMP TRUE)
    endif()
endif()

# PNG library
if(PIXMAN_ENABLE_LIBPNG)
    # Use internal PNG library directly instead of system package
    if(TARGET png_static)
        set(PNG_FOUND TRUE)
        set(PNG_LIBRARY png_static)
        set(PNG_LIBRARIES png_static)
        get_target_property(PNG_INCLUDE_DIRS png_static INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT PNG_INCLUDE_DIRS)
            set(PNG_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../libpng")
        endif()
        set(HAVE_LIBPNG TRUE)
    else()
        find_package(PNG)
        if(PNG_FOUND)
            set(HAVE_LIBPNG TRUE)
        endif()
    endif()
endif()

# GTK for demos
if(PIXMAN_ENABLE_GTK AND PIXMAN_BUILD_DEMOS)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK3 gtk+-3.0)
        pkg_check_modules(GLIB2 glib-2.0)
        if(GTK3_FOUND AND GLIB2_FOUND)
            set(HAVE_GTK TRUE)
        endif()
    endif()
endif()

# System feature detection
check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
check_include_file(fenv.h HAVE_FENV_H)
check_include_file(unistd.h HAVE_UNISTD_H)

# Function checks
check_function_exists(sigaction HAVE_SIGACTION)
check_function_exists(alarm HAVE_ALARM)
check_function_exists(mprotect HAVE_MPROTECT)
check_function_exists(getpagesize HAVE_GETPAGESIZE)
check_function_exists(mmap HAVE_MMAP)
check_function_exists(getisax HAVE_GETISAX)
check_function_exists(gettimeofday HAVE_GETTIMEOFDAY)

# mingw claims to have posix_memalign, but it doesn't
if(NOT WIN32)
    check_function_exists(posix_memalign HAVE_POSIX_MEMALIGN)
endif()

# This is only used in one test, that defines _GNU_SOURCE
if(MATH_LIBRARY)
    set(CMAKE_REQUIRED_LIBRARIES ${MATH_LIBRARY})
    check_symbol_exists(feenableexcept "fenv.h" HAVE_FEENABLEEXCEPT)
    set(CMAKE_REQUIRED_LIBRARIES)
endif()

check_symbol_exists(FE_DIVBYZERO "fenv.h" HAVE_FEDIVBYZERO)

# Check for pthreads
if(Threads_FOUND)
    check_include_file(pthread.h HAVE_PTHREAD_H)
    if(HAVE_PTHREAD_H)
        if(CMAKE_USE_WIN32_THREADS_INIT)
            # MSVC-style compilers do not come with pthreads, so we must link
            # to it explicitly, currently pthreads-win32 is supported
            set(PTHREAD_LIBS_TO_TRY VC3 VSE3 VCE3 VC2 VSE2 VCE2)
            foreach(pthread_type ${PTHREAD_LIBS_TO_TRY})
                find_library(PTHREAD_LIB_${pthread_type} pthread${pthread_type})
                if(PTHREAD_LIB_${pthread_type})
                    set(CMAKE_THREAD_LIBS_INIT ${PTHREAD_LIB_${pthread_type}})
                    set(HAVE_PTHREADS TRUE)
                    break()
                endif()
            endforeach()
        else()
            set(HAVE_PTHREADS TRUE)
        endif()
    endif()
endif()

# Check for builtin functions
check_c_source_compiles("
    int main() {
        return __builtin_clz(1);
    }
" HAVE_BUILTIN_CLZ)

# Check for GCC vector extensions
check_c_source_compiles("
    unsigned int __attribute__ ((vector_size(16))) e, a, b;
    int main (void) {
        e = a - ((b << 27) + (b >> (32 - 27))) + 1;
        return e[0];
    }
" HAVE_GCC_VECTOR_EXTENSIONS)

# Check for float128 support
check_c_source_compiles("
    __float128 a = 1.0Q, b = 2.0Q;
    int main (void) {
        return a + b;
    }
" HAVE_FLOAT128)

# Check for constructor/destructor attributes
check_c_source_runs("
    static int x = 1;
    static void __attribute__((constructor)) constructor_function () { x = 0; }
    int main (void) { return x; }
" TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR)

check_c_source_runs("
    static int x = 1;
    static void __attribute__((destructor)) destructor_function () { x = 0; }
    int main (void) { return x; }
" TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR)

# Check endianness
test_big_endian(WORDS_BIGENDIAN)

# Check size of long
check_type_size(long SIZEOF_LONG)

# TLS support
if(NOT PIXMAN_ENABLE_TLS STREQUAL "OFF")
    if(WIN32)
        check_c_source_compiles("
            int __declspec(thread) foo;
            int main() { return 0; }
        " HAVE_DECLSPEC_THREAD)
        if(HAVE_DECLSPEC_THREAD)
            set(TLS "__declspec(thread)")
        endif()
    else()
        check_c_source_compiles("
            int __thread foo;
            int main() { return 0; }
        " HAVE_THREAD_KEYWORD)
        if(HAVE_THREAD_KEYWORD)
            set(TLS "__thread")
        endif()
    endif()

    if(NOT TLS AND PIXMAN_ENABLE_TLS STREQUAL "ON")
        message(FATAL_ERROR "Compiler TLS Support unavailable, but required")
    endif()
endif()

# GNU inline assembly support
if(NOT PIXMAN_ENABLE_GNU_INLINE_ASM STREQUAL "OFF")
    check_c_source_compiles("
        int main () {
            /* Most modern architectures have a NOP instruction, so this is a fairly generic test. */
            asm volatile ( \"\\tnop\\n\" : : : \"cc\", \"memory\" );
            return 0;
        }
    " HAVE_GNU_INLINE_ASM)

    if(HAVE_GNU_INLINE_ASM)
        set(USE_GCC_INLINE_ASM TRUE)
    elseif(PIXMAN_ENABLE_GNU_INLINE_ASM STREQUAL "ON")
        message(FATAL_ERROR "GNU inline assembly support missing but required.")
    endif()
endif()

# Assembly directive checks
check_c_source_compiles("
    __asm__ (
    \".func meson_test\"
    \".endfunc\"
    );
    int main() { return 0; }
" ASM_HAVE_FUNC_DIRECTIVE)

check_c_source_compiles("
    __asm__ (
    \".syntax unified\\n\"
    );
    int main() { return 0; }
" ASM_HAVE_SYNTAX_UNIFIED)

check_c_source_runs("
    #include <stdint.h>

    __asm__ (
        \"   .global _testlabel\\n\"
        \"_testlabel:\\n\"
    );

    int testlabel();
    int main(int argc, char* argv[]) {
        return testlabel();
    }
" ASM_LEADING_UNDERSCORE)

# Include SIMD detection
include(cmake/PixmanSIMD.cmake)

# Set timers and gnuplot options
if(PIXMAN_ENABLE_TIMERS)
    set(PIXMAN_TIMERS TRUE)
endif()

if(PIXMAN_ENABLE_GNUPLOT)
    set(PIXMAN_GNUPLOT TRUE)
endif()

# Generate config.h
configure_file( 
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/pixman/pixman-config.h
)

# Include subdirectories
add_subdirectory(pixman)
