# =============================================================================
# freeRTOS.cmake  --  swinx platform config for the FreeRTOS port (WIP)
#
# Derived from linux.cmake. PURPOSE (temporary): let the swinx -> uSTL container
# substitution be COMPILE-TESTED on a Linux host before real FreeRTOS
# cross-compilation. Select it with  -DSOUI_PLATFORM=freertos ; the normal
# Linux build is completely untouched when that is not set.
#
# What is FreeRTOS-specific here TODAY:
#   * uSTL is forced ON (swinx/thirdparty/ustl submodule) -- see swinx_stl.h shim.
# The rest (cairo / X11 / xkbcommon / dbus / ALSA) is still the Linux stack so
# the build links & compiles on a host. For the REAL target you must:
#   TODO  replace cairo+X11 with a software framebuffer renderer (route A in the
#         feasibility report: cairo image-surface blitted to /dev/fb0),
#   TODO  drop xkbcommon / libxcb / xcb-imdkit / dbus-1 (no X11/DBus on FreeRTOS),
#   TODO  make ALSA optional / replace with a FreeRTOS audio driver,
#   TODO  add -fno-rtti -fno-exceptions and link newlib-nano + FreeRTOS libs,
#   TODO  cross-compile uSTL with the FreeRTOS toolchain instead of the host build,
#   TODO  move src/platform/linux/*.cpp -> src/platform/freertos/*.cpp.
# NOTE  on a real cross-compile (CMAKE_SYSTEM_NAME=Generic) find_package(PkgConfig)
#       and pkg_check_modules(ALSA) will fail -- those are host-only for now.
# =============================================================================

# ---- uSTL (swinx/thirdparty/ustl submodule, built with CMake) ----------------
# Self-contained wiring: the top-level SOUI_USE_USTL block runs BEFORE
# add_subdirectory(swinx), so we cannot rely on it. Force it here.
set(SOUI_USE_USTL ON CACHE BOOL "Substitute swinx std containers/strings with uSTL" FORCE)

set(USTL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ustl")
if(NOT IS_DIRECTORY "${USTL_SRC_DIR}")
    message(FATAL_ERROR
        "uSTL submodule not found at ${USTL_SRC_DIR}.\n"
        "Initialise it first:\n"
        "  git -C ${CMAKE_CURRENT_SOURCE_DIR} submodule update --init thirdparty/ustl")
endif()

# Build uSTL with CMake as part of this build -- no ./configure && make needed.
# Its CMakeLists.txt generates config.h and compiles a static 'ustl' library.
# The 'ustl' target exports its include dir (which contains ustl/ustl.h and the
# generated ustl/config.h), so swinx picks it up automatically via
# target_link_libraries below.
add_subdirectory(${USTL_SRC_DIR})

add_definitions(-DSOUI_USE_USTL)
set(SOUI_USTL_LIB ustl CACHE INTERNAL "uSTL target name for swinx")

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
set(SWINX_LIBS dl xcb uuid atomic m stdc++ ${ALSA_LIBRARIES} ${SOUI_USTL_LIB})
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
