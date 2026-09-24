# =============================================================================
# freeRTOS.cmake  --  swinx platform config for the FreeRTOS port
#
# Derived from linux.cmake. Select it with  -DSOUI_PLATFORM=freertos ; the
# normal Linux/Windows/macOS/Android/iOS/OHOS build is completely untouched
# when that is not set.
#
# PURPOSE OF THIS FILE (current): provide the FreeRTOS STL-compat layer so that
# swinx compiles on a FreeRTOS toolchain that lacks the C++ concurrency
# primitives (mutex / thread / condition_variable / chrono clock) but ships the
# containers/strings (typical arm-none-eabi-gcc + newlib-nano). The compat layer
# lives in src/freertos/stl/ and overrides the standard <mutex>/<thread>/
# <condition_variable>/<exception>/<stdexcept> headers ONLY on this platform,
# injecting FreeRTOS-backed std:: types. Existing systems are unaffected.
#
# Three backends are selected by macro (never by editing the layer):
#   * SOUI_FREERTOS_REAL      -> real FreeRTOS kernel API (cross-compile target)
#   * SOUI_FREERTOS_HOST_EMU  -> pthreads emulation (compile+run the wrappers on
#                                a Linux/macOS host, for validation)
#   * (neither)               -> the compat headers pass through to the host's
#                                real std library, so the freeRTOS config can be
#                                BUILD-VALIDATED on a workstation without target
#                                hardware (this is the default for the Ubuntu
#                                compile test the maintainer runs).
#
# The rest of the stack (cairo / X11 / xkbcommon / dbus / ALSA) is still the
# Linux stack so the build links & compiles on a host. For the REAL target you
# must (TODO): replace cairo+X11 with a software framebuffer renderer, drop
# xkbcommon / libxcb / xcb-imdkit / dbus-1 (no X11/DBus on FreeRTOS), make ALSA
# optional, and move src/platform/linux/*.cpp -> src/platform/freertos/*.cpp.
# NOTE on a real cross-compile (CMAKE_SYSTEM_NAME=Generic) find_package(PkgConfig)
# and pkg_check_modules(ALSA) will fail -- those are host-only for now.
# =============================================================================

# ---- FreeRTOS STL-compat layer (this is the only FreeRTOS-specific wiring) ----
add_definitions(-DSOUI_PLATFORM_FREERTOS)

# Build the FreeRTOS shims WITHOUT uSTL. uSTL is no longer used for the
# FreeRTOS port: we keep the toolchain's own containers/strings and only wrap
# the concurrency primitives that newlib-nano cannot provide. (The uSTL
# submodule is left in the repo for reference but is NOT built here.)

# ---- optional: -fno-exceptions -fno-rtti (size-optimized FreeRTOS target) ----
# OFF by default so the freeRTOS config can still be build-validated on a host
# (which keeps exceptions on). Turn it ON (or set it in the real target's
# toolchain file) to disable exceptions/RTTI and activate the exception stubs +
# `throw` neutralization in swinx_stl.h / freertos/stl/exception/stdexcept.
option(SOUI_FREERTOS_NO_EXCEPTIONS
       "Build the FreeRTOS port with -fno-exceptions -fno-rtti" OFF)
if(SOUI_FREERTOS_NO_EXCEPTIONS)
    add_definitions(-DSWINX_NO_EXCEPTIONS)
    add_compile_options(-fno-exceptions -fno-rtti)
endif()

# ---- optional: exercise the FreeRTOS-backed wrappers on a host (pthread) ----
# OFF by default. Enable to compile+run the FreeRTOS-backed std::thread /
# mutex / condition_variable against a pthread emulation instead of the host's
# real std library (validates the wrapper logic without target hardware).
option(SOUI_FREERTOS_EMULATE_HOST
       "Emulate the FreeRTOS RTOS primitives with pthreads on the host" OFF)
if(SOUI_FREERTOS_EMULATE_HOST)
    add_definitions(-DSOUI_FREERTOS_HOST_EMU)
endif()
# SOUI_FREERTOS_REAL is expected to be defined by the real target toolchain
# file (e.g. -DSOUI_FREERTOS_REAL=ON); it needs no CMake option here.

# ---- platform sources (currently = Linux stack; prune for real FreeRTOS) ----
add_compile_options(-Wno-format-truncation)
add_compile_options(-Wno-attributes)
find_package(PkgConfig REQUIRED)

# Find ALSA library for audio playback (now optional)
pkg_check_modules(ALSA QUIET alsa)

# Check if ALSA was found and set compile definition accordingly
if(ALSA_FOUND)
    message(STATUS "ALSA found, enabling native audio support")
    add_definitions(-DHAS_ALSA)
else()
    message(STATUS "ALSA not found, will use avplay fallback for audio, please install libasound2-dev")
endif()

# Use internal compiled libraries instead of system packages
# Ensure our thirdparty libraries are available
if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found. Make sure thirdparty is built first.")
endif()


add_subdirectory(thirdparty/xkbcommon)
add_subdirectory(thirdparty/libxcb)
add_subdirectory(thirdparty/xcb-imdkit)
add_subdirectory(thirdparty/dbus-1.14.10)


get_target_property(CAIRO_INCLUDE_DIRS cairo INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${CAIRO_INCLUDE_DIRS})
get_target_property(DBUS_INCLUDE_DIRS dbus-1 INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${DBUS_INCLUDE_DIRS})

file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    thirdparty/xcb-util-image/*.c
    thirdparty/xcb-util-renderutil/*.c
    thirdparty/xcb-util-keysyms/*.c
    thirdparty/xcb-util-wm/*.c
    thirdparty/xcb-util/*.c
    thirdparty/libxcb/*.c
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/linux/*.cpp
    src/gdi/cairo/*.cpp
    )

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

if (NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})

    # ensure export all symbols, including fontconfig and freetype symbols
    set_target_properties(swinx PROPERTIES
        LINK_FLAGS "-Wl,--export-dynamic"
    )
    set_target_properties(swinx PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()

# Add dependencies to ensure proper build order for all internal libraries
add_dependencies(swinx cairo fontconfig freetype pixman-1 xcb-imdkit xkbcommon dbus-1)
set(SWINX_LIBS dl xcb uuid atomic m stdc++ ${ALSA_LIBRARIES})
if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_link_libraries(swinx
    cairo              # Our internal cairo target
    xkbcommon          # Our internal xkbcommon target
    xcb-imdkit         # Our internal xcb-imdkit target
    dbus-1             # Our internal D-Bus library
    ${SWINX_LIBS}
)

target_include_directories(swinx
    # Make the FreeRTOS STL-compat headers win over the system <mutex> /
    # <thread> / <condition_variable> / <exception> / <stdexcept> so those
    # resolve to our FreeRTOS-backed (or pass-through) versions on this
    # platform only. Other platforms never see this directory.
    PRIVATE BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src/freertos/stl
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xkbcommon
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb-imdkit/include
    PRIVATE ${ALSA_INCLUDE_DIRS}
)
