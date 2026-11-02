cmake_minimum_required(VERSION 3.16)

project(swinx)
set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-result)

# thirdparty（cairo / fontconfig）用 try_run 探测 __attribute__((constructor)) /
# ((destructor)) 支持度。交叉编译时 try_run 生成的程序跑不起来，配置会直接报
# "try_run() invoked in cross-compiling mode"，除非把结果固化进 cache —— 下游
# （soui-android-lib / cnchess）一直是这么做的，swinx 单独交叉编译时也必须补上。
if(CMAKE_CROSSCOMPILING)
    set(TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR_EXITCODE "0" CACHE STRING "cross-compiling: assume supported" FORCE)
    set(TOOLCHAIN_SUPPORTS_ATTRIBUTE_CONSTRUCTOR_EXITCODE__TRYRUN_OUTPUT "" CACHE STRING "" FORCE)
    set(TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR_EXITCODE "0" CACHE STRING "cross-compiling: assume supported" FORCE)
    set(TOOLCHAIN_SUPPORTS_ATTRIBUTE_DESTRUCTOR_EXITCODE__TRYRUN_OUTPUT "" CACHE STRING "" FORCE)
endif()

# Emscripten 下 cairo 的 CHECK_TYPE_SIZE("uint64_t" SIZEOF_UINT64_T) 走 try_run，
# 探测结果落空（cache 里 SIZEOF_UINT64_T 为空），于是 Configure_config.cmake 的
# `if(SIZEOF_UINT64_T) set(HAVE_UINT64_T 1)` 不执行；此时 cairo 继承的是上游
# fontconfig 用 check_type_size("uint64_t" UINT64_T) 写进同一 cache 的伴生变量
# HAVE_UINT64_T=TRUE，config.h 里就生成 `#define HAVE_UINT64_T TRUE`。预处理把
# 未定义的 TRUE 当 0，cairo 于是选中"两个 32 位拼 64 位"的 wideint 分支，而
# cairo-fixed-private.h 等仍按原生 int64_t 传参，交叉编译直接报 19 个类型错误。
# 这里把尺寸固化进 cache（uint64_t 存在即为 8 字节），只在 Emscripten 下生效，
# 其他平台的探测结果不变。
if(EMSCRIPTEN)
    set(SIZEOF_UINT64_T "8" CACHE INTERNAL "emscripten: try_run cannot measure uint64_t" FORCE)
endif()

add_subdirectory(thirdparty)

if(SOUI_ENABLE_ACC)
    add_definitions(-DSOUI_ENABLE_ACC)
endif()

# Explicit platform override. Empty => auto-select by CMAKE_SYSTEM_NAME (unchanged
# Linux/macOS/OHOS/Android/iOS behaviour). Set to "freertos" to build the swinx
# FreeRTOS port: a Linux-host compile test driven by the FreeRTOS STL-compat
# layer in src/platform/freertos/stl/ (no uSTL; only the missing concurrency primitives
# are wrapped with FreeRTOS/pthread-backed std:: types). Set to "wasm" to build the
# WebAssembly port (Emscripten): src/platform/wasm/ + the POSIX system-API layer and
# the cairo software GDI backend, producing a static libswinx.a that a JS host pumps.
set(SOUI_PLATFORM "" CACHE STRING "swinx platform override: linux|freertos|wasm (empty = auto)")
message(STATUS "####SOUI_PLATFORM=${SOUI_PLATFORM} (empty = auto-select by CMAKE_SYSTEM_NAME)")
if(SOUI_PLATFORM STREQUAL "freertos")
include(freeRTOS.cmake)
elseif(SOUI_PLATFORM STREQUAL "wasm")
include(wasm.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS|Android")
include(mobile.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "iOS")
include(ios.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
include(macos.cmake)
elseif(EMSCRIPTEN)
# Emscripten must not fall through to linux.cmake: that branch globs the X11 /
# DBus / xkbcommon / ALSA stack, none of which exists in a browser.
include(wasm.cmake)
else()
include(linux.cmake)
endif()

if(SOUI_ENABLE_CORE_LIB)
	target_compile_options(swinx PRIVATE -fPIC)
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



