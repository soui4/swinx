# =============================================================================
# wasm.cmake  --  swinx platform config for the WebAssembly (Emscripten) port
#
# Shape follows mobile.cmake, not freeRTOS.cmake: Emscripten ships a musl-based
# libc with pthreads, real <mutex>/<thread>/<chrono>, dirent and dlfcn, so the
# bare-metal scaffolding of src/platform/freertos/ (stl/ shims, sys/mman.h,
# uuid/uuid.h, dlfcn.h, winobjs.cpp, syncapi.cpp, sysbridge.cpp) is not needed
# and would collide with the POSIX implementations.  Therefore this target
# compiles:
#
#   src/*.cpp                 swinx core (Win32 compat surface)
#   src/cmnctl32/*            common controls
#   src/platform/posix/*.cpp  POSIX system-API implementations, same set the
#                             linux/macos/ios/mobile builds use
#   src/platform/wasm/*.cpp   the wasm SConnection + framebuffer + input
#                             injection (fork of src/platform/freertos/)
#   src/gdi/<backend>/*.cpp   the GDI backend -- 由 SWINX_WASM_GDI_BACKEND 选择：
#                             "canvas2d"（默认）：src/gdi/canvas2d/*，把每个窗口
#                               画进离屏 HTMLCanvasElement，脏矩形由 C++ 侧直接
#                               drawImage 合成到显示画布，文本走浏览器内置字体；
#                             "cairo"：src/gdi/cairo/*，软件 cairo image surface，
#                               提交时把脏区 memcpy 进 framebuffer，字体由 freetype
#                               + fontconfig 负责（需把字体文件预载进 MEMFS）。
#
# FreeRTOS, by contrast, compiles none of src/gdi/** -- its GDI is still the
# pending framebuffer slice (see freeRTOS.cmake TODO) -- which is exactly why
# the wasm port is modeled on mobile.cmake instead.
#
# Not built here (X11/DBus/ALSA/GL stack, all host-only):
#   thirdparty/{libxcb,xcb-util*,xkbcommon,xcb-imdkit,dbus-1.14.10}; these are
#   added by linux.cmake only.  cairo's xcb backend is switched off for
#   Emscripten in thirdparty/cairo/Configure_features.cmake.
#
# Build notes for this platform (already handled above/at the call site):
#   - thirdparty must be STATIC: swinx links into a wasm module, and brotli
#     forces BUILD_SHARED_LIBS OFF, so a shared cairo fails to link with
#     "relocation R_WASM_MEMORY_ADDR_LEB cannot be used against symbol
#     kBrotliBitMask".  Use -DSOUI_ENABLE_CORE_LIB=ON.
#   - the cross-compile try_run/CHECK_TYPE_SIZE fixups live in
#     swinx/CMakeLists.txt (before add_subdirectory(thirdparty)).
#
# Select with -DSOUI_PLATFORM=wasm, or let EMSCRIPTEN auto-select it.
# GDI backend: -DSWINX_WASM_GDI_BACKEND=canvas2d (default) | cairo.
# Every other platform is untouched.
# =============================================================================

add_definitions(-DSOUI_PLATFORM_WASM)

# NOT defined here, unlike mobile.cmake: ENABLE_VIRTUAL_HWND selects the
# PlatformAPI shape where the host toolkit owns the real window and swinx only
# tracks a mirror registered through SConnection::RegisterVirtualHWND.  The
# wasm SConnection is forked from src/platform/freertos/, which makes every
# window a virtual _Window inside swinx itself and therefore needs no external
# registration entry point (see src/wnd.cpp:4327 for the only consumer).

# ---------- GDI 后端选择 ----------
# canvas2d：浏览器 Canvas 2D 绘制，文本走浏览器内置字体，不链接 cairo/freetype/
# fontconfig/pixman，不预载字体文件；代码里用 SWINX_WASM_CANVAS2D 宏在 sdc.h /
# SConnection.cpp 里切分两条路径。
# cairo：软件光栅化后端，文本/字体由 freetype + fontconfig 负责，必须把字体文件
# 预载进 wasm 虚拟文件系统（见 games/cnchess/client/wasm 的字体打包）。
# 必须是 set(... CACHE STRING)，不能用 option()：option() 建的是 BOOL 型缓存条目，
# 而 "canvas2d"/"cairo" 都不是合法的布尔字面量 —— 全新配置时 CMake 会把默认值归一
# 成 OFF（本地因为有历史缓存一直看不出来，CI 一跑就挂：
# "必须是 'canvas2d' 或 'cairo'（当前为 'OFF'）"）。CACHE STRING 才原样保存字符串。
if(NOT DEFINED SWINX_WASM_GDI_BACKEND)
    set(SWINX_WASM_GDI_BACKEND "canvas2d" CACHE STRING
        "wasm GDI backend: canvas2d (browser canvas, no bundled fonts) | cairo (software raster, fonts bundled)")
endif()
if(NOT SWINX_WASM_GDI_BACKEND STREQUAL "canvas2d" AND NOT SWINX_WASM_GDI_BACKEND STREQUAL "cairo")
    message(FATAL_ERROR "SWINX_WASM_GDI_BACKEND 必须是 'canvas2d' 或 'cairo'（当前为 '${SWINX_WASM_GDI_BACKEND}'）")
endif()

if(SWINX_WASM_GDI_BACKEND STREQUAL "canvas2d")
    add_definitions(-DSWINX_WASM_CANVAS2D)
    # canvas2d 不链接 cairo/png/fontconfig/freetype/pixman，故无对应 include 目录。
    # PNG 光标/图标解码在 src/gdi/canvas2d/builtin_image.cpp 自带精简解码器；
    # 字体走浏览器内置，不链接 fontconfig。
    add_definitions(-DSWINX_NO_LIBPNG -DSWINX_NO_FONTCONFIG)
    # canvas2d 用 emscripten::val / C++17 特性（结构化绑定等），与历史 canvas2d 构建一致。
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # canvas2d 的 commitCanvas 里 surface 仅作判空、不再使用，屏蔽 unused 告警。
    add_compile_options(-Wno-unused-variable)
else()
    # cairo 后端：需要 thirdparty 静态库（cairo/fontconfig/freetype/pixman-1）。
    if(NOT TARGET cairo)
        message(FATAL_ERROR "cairo target not found -- build thirdparty as static libs (-DSOUI_ENABLE_CORE_LIB=ON).")
    endif()
    get_target_property(cairo_inc cairo INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(png_inc png_static INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${cairo_inc} ${png_inc})
endif()

# WCHAR_SIZE (byte size of wchar_t, consumed by src/uniconv.h) comes from the
# compiler builtin per TU: 4 on Emscripten (same as the POSIX targets).
add_definitions(-DWCHAR_SIZE=__SIZEOF_WCHAR_T__)

# Screen geometry for src/platform/wasm/framebuffer.h.  The framebuffer is a
# static array in the wasm linear-memory .bss, so the size is baked into
# libswinx.a: a demo or host page that indexes SwinxFbBits() with a different
# width would write past the array.  Declaring it as a CACHE STRING here (rather
# than leaving the #ifndef defaults in the header) keeps the library and every
# consumer configured from one place; consumers still read the actual value at
# runtime through SwinxFbWidth()/SwinxFbHeight()/SwinxFbStride().
set(SWINX_FB_WIDTH  "800" CACHE STRING "wasm framebuffer width in pixels (ARGB32)")
set(SWINX_FB_HEIGHT "600" CACHE STRING "wasm framebuffer height in pixels (ARGB32)")
if(SWINX_FB_WIDTH LESS 1 OR SWINX_FB_HEIGHT LESS 1)
    message(FATAL_ERROR "SWINX_FB_WIDTH/HEIGHT must be >= 1 (got ${SWINX_FB_WIDTH}x${SWINX_FB_HEIGHT})")
endif()
add_definitions(-DSWINX_FB_WIDTH=${SWINX_FB_WIDTH} -DSWINX_FB_HEIGHT=${SWINX_FB_HEIGHT})

add_compile_options(-Wno-format-truncation -Wno-attributes
                    -Wno-missing-field-initializers
                    -Wno-deprecated-declarations
                    -Wno-unused-result -Wno-format)

file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/posix/*.cpp
    src/platform/wasm/*.cpp
)

# GDI 后端源文件：canvas2d 编译 src/gdi/canvas2d/* 并排除 src/sdc.cpp（后者只含
# cairo/apple 后端的 HDC__ 生命周期，且 cairo 分支在 __EMSCRIPTEN__ 下无法编译，
# 由 src/gdi/canvas2d/gdi.cpp 提供 canvas2d 的 HDC__ 方法）；cairo 编译 src/gdi/cairo/*。
if(SWINX_WASM_GDI_BACKEND STREQUAL "canvas2d")
    file(GLOB SRCS_GDI src/gdi/canvas2d/*.cpp)
    list(FILTER SRCS EXCLUDE REGEX "/src/sdc\\.cpp$")
else()
    file(GLOB SRCS_GDI src/gdi/cairo/*.cpp)
endif()
list(APPEND SRCS ${SRCS_GDI})

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

# Static archive only: the wasm executable / side module links libswinx.a plus
# the vendor archives, and SOUI components are merged statically
# (SOUI_ENABLE_COM_LIB=ON) because there is no dlopen-based component loader.
add_library(swinx STATIC ${SRCS} ${HEADERS})

if(SWINX_WASM_GDI_BACKEND STREQUAL "cairo")
    add_dependencies(swinx cairo fontconfig freetype pixman-1)
    # brotlidec/brotlicommon 没有被上面任何目标间接拉起来，但最终 wasm 链接需要它们：
    # freetype 只在 PRIVATE 里链 brotlidec，而 brotli 自己的 CMakeLists 把
    # target_link_libraries(brotlidec brotlicommon) 写在 if(NOT BROTLI_EMSCRIPTEN)
    # 里——Emscripten 下这条边被刻意去掉了，所以 common 不会随 dec 一起构建。
    # SWINX_DEP_LIBS（thirdparty/brotli/CMakeLists.txt）已经把三个库都登记给调用方，
    # 这里补上构建图，让 --target swinx 一次就能产出全部档案。
    foreach(_brotli_lib brotlidec brotlicommon)
        if(TARGET ${_brotli_lib})
            add_dependencies(swinx ${_brotli_lib})
        endif()
    endforeach()
endif()

# ---- link libraries ---------------------------------------------------------
# No xcb / dbus / xkbcommon / ALSA (see header).  dl is folded into libc on
# Emscripten, so CMAKE_DL_LIBS (which is empty there) is used instead of a
# literal "dl".  cairo 后端额外链接 cairo/fontconfig/freetype/pixman-1；canvas2d
# 只链 dl + m（字体/光栅化都在浏览器侧）。
if(SWINX_WASM_GDI_BACKEND STREQUAL "cairo")
    target_link_libraries(swinx
        cairo fontconfig freetype pixman-1
        ${CMAKE_DL_LIBS} m
    )
    if(SOUI_ENABLE_CORE_LIB)
        set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} cairo fontconfig freetype pixman-1 ${CMAKE_DL_LIBS} m CACHE INTERNAL "swinx_dep_libs")
    endif()
else()
    target_link_libraries(swinx ${CMAKE_DL_LIBS} m)
    if(SOUI_ENABLE_CORE_LIB)
        set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${CMAKE_DL_LIBS} m CACHE INTERNAL "swinx_dep_libs")
    endif()
endif()

target_include_directories(swinx
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wasm
)

message(STATUS "SWINX_WASM_GDI_BACKEND=${SWINX_WASM_GDI_BACKEND}  (swinx wasm GDI backend)")
