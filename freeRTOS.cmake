# =============================================================================
# freeRTOS.cmake  --  swinx platform config for the FreeRTOS port
#
# Modeled on mobile.cmake: core src/*.cpp + cmnctl32 + the platform directory
# src/platform/freertos/*.  All FreeRTOS-specific bridging lives inside
# src/platform/freertos/:
#
#   src/platform/freertos/stl/           FreeRTOS-backed <mutex>/<thread>/... shims
#                                        (three backends: SOUI_FREERTOS_REAL /
#                                        SOUI_FREERTOS_HOST_EMU / passthrough)
#   src/platform/freertos/syncapi.cpp    CRITICAL_SECTION / SRWLOCK / INIT_ONCE
#   src/platform/freertos/winobjs.cpp    events/mutexes/semas/threads/
#                                        interlocked + pthread_self()
#   src/platform/freertos/SConnection.*  message queue / timers / keyboard /
#                                        caret (mobile-shaped, FreeRTOS waits)
#   src/platform/freertos/atoms.cpp      atom registry (port of mobile's)
#   src/platform/freertos/dlfcn.h        <dlfcn.h> declarations (no dynamic
#                                        loader on bare metal)
#   src/platform/freertos/cairo-features.h  stub: cairo built without backends
#   src/platform/freertos/uuid/uuid.h    <uuid/uuid.h> on the FreeRTOS tick
#
# Files EXCLUDED from the core GLOB (POSIX process/file services that do not
# exist on bare metal; their Win32-compat symbols are provided by
# src/platform/freertos/winobjs.cpp + syncapi.cpp instead):
#   cursoricon fileapi memory mmsystem profile sharedmem shellapi shellobj
#   syncapi sysapi sysobjs
#
# Not built on this platform (X11/DBus/ALSA stack):
#   thirdparty/{libxcb,xcb-util*,xkbcommon,xcb-imdkit,dbus-1.14.10}; dbus is
#   only used by src/platform/linux/{dlghelper,SAtSpi}.cpp (native dialogs /
#   AT-SPI accessibility), which are not compiled here either.
#
# TODO (later slices): software-framebuffer GDI backend (cairo image surface +
# blit), audio, then SConnection::CreateWindowSurface/commitCanvas un-stubbing.
#
# Select with -DSOUI_PLATFORM=freertos.  Every other platform is untouched.
# =============================================================================

add_definitions(-DSOUI_PLATFORM_FREERTOS)

# ---- optional: -fno-exceptions -fno-rtti (size-optimized FreeRTOS target) ----
# swinx_stl.h neutralizes `throw` when SWINX_NO_EXCEPTIONS is defined; the
# freertos STL shims carry the exception/stdexcept stubs.  Turn ON for the
# real target (the cross toolchain usually sets this), OFF to build-validate
# on a workstation with exceptions enabled.
option(SOUI_FREERTOS_NO_EXCEPTIONS
       "Build the FreeRTOS port with -fno-exceptions -fno-rtti" OFF)
if(SOUI_FREERTOS_NO_EXCEPTIONS)
    add_definitions(-DSWINX_NO_EXCEPTIONS)
    add_compile_options(-fno-exceptions -fno-rtti)
endif()

# ---- optional: exercise the FreeRTOS-backed wrappers on a host (pthread) ----
option(SOUI_FREERTOS_EMULATE_HOST
       "Emulate the FreeRTOS RTOS primitives with pthreads on the host" OFF)
if(SOUI_FREERTOS_EMULATE_HOST)
    add_definitions(-DSOUI_FREERTOS_HOST_EMU)
endif()
# SOUI_FREERTOS_REAL is expected to be defined by the real target toolchain
# file (e.g. -DSOUI_FREERTOS_REAL=ON); it needs no CMake option here.

# ---- sources -----------------------------------------------------------------
add_compile_options(-Wno-format-truncation -Wno-attributes)

set(SWINX_FREERTOS_EXCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cursoricon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fileapi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mmsystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/profile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sharedmem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shellapi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shellobj.cpp
    # replaced by src/platform/freertos/syncapi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/syncapi.cpp
    # replaced by src/platform/freertos/winobjs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysapi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysobjs.cpp
)

file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SWINX_CORE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
foreach(_f ${SWINX_FREERTOS_EXCLUDES})
    list(REMOVE_ITEM SWINX_CORE_SRCS ${_f})
endforeach()

file(GLOB SWINX_CMNCTL_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmnctl32/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmnctl32/*.c)
file(GLOB SWINX_FREERTOS_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/freertos/*.cpp)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SWINX_CORE_SRCS} ${SWINX_CMNCTL_SRCS} ${SWINX_FREERTOS_SRCS})

# bare-metal: static archive only
add_library(swinx STATIC
    ${SWINX_CORE_SRCS}
    ${SWINX_CMNCTL_SRCS}
    ${SWINX_FREERTOS_SRCS}
    ${HEADERS})

target_include_directories(swinx
    # STL shims must win over the toolchain's own <mutex>/<thread>/... on this
    # platform only.  Other platforms never see this directory.
    PRIVATE BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/freertos/stl
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/freertos/uuid
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/freertos
    # cairo core headers (types only; no cairo lib is built for this target
    # until the framebuffer renderer lands)
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cairo/src)

# Force-include the swinx_stl shim into every swinx C++ TU (same convention as
# the other platform configs).  GCC/arm-none-eabi only.
target_compile_options(swinx PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include>
    $<$<COMPILE_LANGUAGE:CXX>:swinx_stl.h>)
