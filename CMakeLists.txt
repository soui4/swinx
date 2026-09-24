cmake_minimum_required(VERSION 3.16)

project(swinx)
set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-result)

add_subdirectory(thirdparty)

if(SOUI_ENABLE_ACC)
    add_definitions(-DSOUI_ENABLE_ACC)
endif()

# Explicit platform override. Empty => auto-select by CMAKE_SYSTEM_NAME (unchanged
# Linux/macOS/OHOS/Android/iOS behaviour). Set to "freertos" to build the swinx
# FreeRTOS port: a Linux-host compile test driven by the FreeRTOS STL-compat
# layer in src/freertos/stl/ (no uSTL; only the missing concurrency primitives
# are wrapped with FreeRTOS/pthread-backed std:: types).
set(SOUI_PLATFORM "" CACHE STRING "swinx platform override: linux|freertos (empty = auto)")

if(SOUI_PLATFORM STREQUAL "freertos")
    include(freeRTOS.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS|Android")
include(mobile.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "iOS")
include(ios.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
include(macos.cmake)
else()
include(linux.cmake)
endif()

# ---- uSTL substitution (swinx-internal container/string replacement) --------
# Moved out of the top-level SOUI CMake so the SOUI layer stays uSTL-free.
# OFF by default => zero behaviour change; the mechanical replacement in swinx
# source resolves swinx_stl:: to std:: either way. The FreeRTOS port no longer
# uses uSTL (it keeps the toolchain's own containers and only wraps the missing
# concurrency primitives), so this block stays OFF on that platform.
option(SOUI_USE_USTL "Substitute swinx std containers/strings with uSTL" OFF)
if(SOUI_USE_USTL AND NOT TARGET ustl)
    if(NOT DEFINED USTL_INCLUDE_DIR)
        # Default matches a sibling checkout: ../ustl relative to soui4.
        set(USTL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../ustl" CACHE PATH "uSTL include dir (must contain ustl/ustl.h)")
    endif()
    if(NOT DEFINED USTL_LIBRARY)
        set(USTL_LIBRARY "${USTL_INCLUDE_DIR}/libustl.a" CACHE FILEPATH "uSTL static library")
    endif()
    if(NOT EXISTS "${USTL_INCLUDE_DIR}/ustl/ustl.h")
        message(FATAL_ERROR "SOUI_USE_USTL=ON but <ustl/ustl.h> not found under ${USTL_INCLUDE_DIR}. "
            "Build/install uSTL so its headers land in a 'ustl/' subdir "
            "(e.g. copy *.h into ${USTL_INCLUDE_DIR}/ustl/), then re-run. "
            "See doc/SOUI-FreeRTOS可行性分析报告 for the exact steps.")
    endif()
    include_directories(${USTL_INCLUDE_DIR})
    add_definitions(-DSOUI_USE_USTL)
    set(SOUI_USTL_LIB ${USTL_LIBRARY} CACHE INTERNAL "uSTL static lib for swinx")
    message(STATUS "SOUI_USE_USTL=ON  uSTL headers: ${USTL_INCLUDE_DIR}  lib: ${USTL_LIBRARY}")
endif()

if(SOUI_ENABLE_CORE_LIB)
	target_compile_options(swinx PRIVATE -fPIC)
endif()

# Force-include the swinx_stl shim into every swinx C++ TU so the
# substituted swinx_stl:: types resolve in both SOUI_USE_USTL ON and OFF
# modes. Restricted to CXX so the .c sources compiled into this target are
# untouched. swinx is built with GCC/Clang only, so -include is valid.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(swinx PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/swinx_stl.h>)
endif()

# In uSTL mode swinx uses ustl containers, so it must link libustl.
if(SOUI_USE_USTL)
    target_link_libraries(swinx ${SOUI_USTL_LIB})
endif()

# Stricter warnings for the swinx module itself (PRIVATE scope keeps
# thirdparty unaffected). GCC/Clang only; swinx is not built with MSVC.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(swinx PRIVATE -Wall -Wextra)
endif()
set_target_properties (swinx PROPERTIES
    FOLDER swinx
)

set(SOUI_SWINX swinx CACHE INTERNAL "swinx_lib")

install(TARGETS swinx
    CONFIGURATIONS Release
    RUNTIME DESTINATION bin/Release
    LIBRARY DESTINATION lib/Release
    ARCHIVE DESTINATION lib/Release
)


install(TARGETS swinx
    CONFIGURATIONS Debug
    RUNTIME DESTINATION bin/Debug
    LIBRARY DESTINATION lib/Debug
    ARCHIVE DESTINATION lib/Debug
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION include/swinx)



