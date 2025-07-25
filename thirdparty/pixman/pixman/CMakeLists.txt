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

# Set version variables for pixman-version.h
string(REPLACE "." ";" VERSION_LIST ${PROJECT_VERSION})
list(GET VERSION_LIST 0 PIXMAN_VERSION_MAJOR)
list(GET VERSION_LIST 1 PIXMAN_VERSION_MINOR)
list(GET VERSION_LIST 2 PIXMAN_VERSION_MICRO)

# Generate pixman-version.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/pixman-version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/pixman-version.h
    @ONLY
)

# Install pixman-version.h
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pixman-version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pixman-1
)

# Extra compile arguments for shared library
set(PIXMAN_EXTRA_CARGS)
if(BUILD_SHARED_LIBS AND CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(PIXMAN_EXTRA_CARGS "-DPIXMAN_API=__declspec(dllexport)")
endif()

# SIMD libraries
set(PIXMAN_SIMD_LIBS)

# MMX library (can be compiled with mmx on x86/x86_64 or loongson mmi on loongson mips systems)
if(HAVE_MMX OR HAVE_LOONGSON_MMI)
    set(MMX_SOURCES pixman-mmx.c)
    set(MMX_FLAGS)
    if(HAVE_MMX)
        set(MMX_FLAGS ${MMX_FLAGS})
    endif()
    if(HAVE_LOONGSON_MMI)
        set(MMX_FLAGS ${LOONGSON_MMI_FLAGS})
    endif()
    
    add_library(pixman-mmx STATIC ${MMX_SOURCES})
    target_compile_options(pixman-mmx PRIVATE ${MMX_FLAGS})
    target_include_directories(pixman-mmx PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-mmx)
endif()

# SSE2 library
if(HAVE_SSE2)
    add_library(pixman-sse2 STATIC pixman-sse2.c)
    target_compile_options(pixman-sse2 PRIVATE ${SSE2_FLAGS})
    target_include_directories(pixman-sse2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-sse2)
endif()

# SSSE3 library
if(HAVE_SSSE3)
    add_library(pixman-ssse3 STATIC pixman-ssse3.c)
    target_compile_options(pixman-ssse3 PRIVATE ${SSSE3_FLAGS})
    target_include_directories(pixman-ssse3 PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-ssse3)
endif()

# VMX library
if(HAVE_VMX)
    add_library(pixman-vmx STATIC pixman-vmx.c)
    target_compile_options(pixman-vmx PRIVATE ${VMX_FLAGS})
    target_include_directories(pixman-vmx PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-vmx)
endif()

# ARM SIMD library
if(HAVE_ARMV6_SIMD)
    add_library(pixman-arm-simd STATIC 
        pixman-arm-simd.c
        pixman-arm-simd-asm.S
        pixman-arm-simd-asm-scaled.S
    )
    target_include_directories(pixman-arm-simd PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-arm-simd)
endif()

# ARM NEON library
if(HAVE_NEON)
    add_library(pixman-arm-neon STATIC 
        pixman-arm-neon.c
        pixman-arm-neon-asm.S
        pixman-arm-neon-asm-bilinear.S
    )
    target_include_directories(pixman-arm-neon PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-arm-neon)
endif()

# ARM A64 NEON library
if(HAVE_A64_NEON)
    add_library(pixman-arm-neon STATIC 
        pixman-arm-neon.c
        pixman-arma64-neon-asm.S
        pixman-arma64-neon-asm-bilinear.S
    )
    target_include_directories(pixman-arm-neon PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-arm-neon)
endif()

# MIPS DSPr2 library
if(HAVE_MIPS_DSPR2)
    add_library(pixman-mips-dspr2 STATIC 
        pixman-mips-dspr2.c
        pixman-mips-dspr2-asm.S
        pixman-mips-memcpy-asm.S
    )
    target_compile_options(pixman-mips-dspr2 PRIVATE ${MIPS_DSPR2_FLAGS})
    target_include_directories(pixman-mips-dspr2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-mips-dspr2)
endif()

# RISC-V Vector library
if(HAVE_RVV)
    add_library(pixman-rvv STATIC pixman-rvv.c)
    target_compile_options(pixman-rvv PRIVATE ${RVV_FLAGS})
    target_include_directories(pixman-rvv PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
    list(APPEND PIXMAN_SIMD_LIBS pixman-rvv)
endif()

# Main pixman library sources
set(PIXMAN_SOURCES
    pixman.c
    pixman-access.c
    pixman-access-accessors.c
    pixman-arm.c
    pixman-bits-image.c
    pixman-combine32.c
    pixman-combine-float.c
    pixman-conical-gradient.c
    pixman-edge.c
    pixman-edge-accessors.c
    pixman-fast-path.c
    pixman-filter.c
    pixman-glyph.c
    pixman-general.c
    pixman-gradient-walker.c
    pixman-image.c
    pixman-implementation.c
    pixman-linear-gradient.c
    pixman-matrix.c
    pixman-mips.c
    pixman-noop.c
    pixman-ppc.c
    pixman-radial-gradient.c
    pixman-region16.c
    pixman-region32.c
    pixman-region64f.c
    pixman-riscv.c
    pixman-solid-fill.c
    pixman-timer.c
    pixman-trap.c
    pixman-utils.c
    pixman-x86.c
)

# Android cpu-features support
set(CPU_FEATURES_SOURCES)
set(CPU_FEATURES_INCLUDE_DIRS)
if(PIXMAN_CPU_FEATURES_PATH)
    message(STATUS "Using cpu-features.[ch] from ${PIXMAN_CPU_FEATURES_PATH}")
    set(CPU_FEATURES_SOURCES
        ${PIXMAN_CPU_FEATURES_PATH}/cpu-features.h
        ${PIXMAN_CPU_FEATURES_PATH}/cpu-features.c
    )
    set(CPU_FEATURES_INCLUDE_DIRS ${PIXMAN_CPU_FEATURES_PATH})
endif()

# Create the main pixman library
add_library(pixman-1
    ${PIXMAN_SOURCES}
    ${CPU_FEATURES_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/pixman-version.h
    ${CMAKE_CURRENT_BINARY_DIR}/pixman-config.h
)

# Set library properties
set_target_properties(pixman-1 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER "pixman.h"
)

# Link with SIMD libraries
if(PIXMAN_SIMD_LIBS)
    target_link_libraries(pixman-1 PRIVATE ${PIXMAN_SIMD_LIBS})
endif()

# Link with system libraries
target_link_libraries(pixman-1 PRIVATE ${CMAKE_THREAD_LIBS_INIT})
if(MATH_LIBRARY)
    target_link_libraries(pixman-1 PRIVATE ${MATH_LIBRARY})
endif()

# Set compile options
target_compile_options(pixman-1 PRIVATE ${PIXMAN_EXTRA_CARGS})
target_compile_options(pixman-1 PRIVATE -fPIC)
# Set include directories
target_include_directories(pixman-1
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/pixman-1>
    PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CPU_FEATURES_INCLUDE_DIRS}
)

# Install the library
install(TARGETS pixman-1
    EXPORT pixman-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pixman-1
)

# Create an alias for internal use
add_library(pixman::pixman-1 ALIAS pixman-1)
