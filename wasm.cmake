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
#   src/gdi/canvas2d/*.cpp    route C 软件 GDI 后端：基于浏览器 Canvas 2D
#                             （emscripten::val 持有离屏 HTMLCanvasElement），
#                             把 cairo / pixman / freetype / fontconfig / brotli
#                             从 wasm 彻底移除。
#
# FreeRTOS, by contrast, compiles none of src/gdi/** -- its GDI is still the
# pending framebuffer slice (see freeRTOS.cmake TODO) -- which is exactly why
# the wasm port is modeled on mobile.cmake instead.
#
# Not built here (X11/DBus/ALSA/GL stack, all host-only):
#   thirdparty/{libxcb,xcb-util*,xkbcommon,xcb-imdkit,dbus-1.14.10}; these are
#   added by linux.cmake only.  cairo 的 svg/script/xml surface 已按 route A 在
#   thirdparty/cairo/Configure_features.cmake 关闭，但 wasm 走 route C 已不再链接
#   cairo（见下方 target_link_libraries），此处保留说明以备回退。
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
# Every other platform is untouched.
# =============================================================================

add_definitions(-DSOUI_PLATFORM_WASM)

# route C：wasm 不链接 libpng（cairo 移除后不再有 png.h 来源）。PNG 光标/图标解码
# 在 src/platform/posix/cursoricon.cpp 里以 SWINX_NO_LIBPNG 守卫，禁用后 .ico/.cur/.ani
# 路径不受影响；其余平台（cairo 后端）仍保留 PNG 支持。
add_definitions(-DSWINX_NO_LIBPNG)

# route C：wasm 不链接 fontconfig（字体来自浏览器系统字体，无需 fontconfig 做字体
# 匹配/回退）。src/platform/posix/mlang.cpp、usp10.cpp 里依赖 fontconfig 的字体
# 覆盖探测与回退以 SWINX_NO_FONTCONFIG 守卫，退化为「不探测 / 不回退」；
# 字符集转换、UTF-8/UTF-16 编解码、SCRIPT 分析等主体逻辑不受影响。
add_definitions(-DSWINX_NO_FONTCONFIG)

# 诊断用（默认关）：打开后每个 GDI 入口向 stderr 打一行函数名，用来在 trap /
# 死循环时定位"最后进入的 GDI 函数"——wasm 的 C++ 栈在异常穿透时会整体丢失。
# 用法：cmake -DSWINX_GDI_TRACE=ON；输出量大（每帧数百行），诊断完记得关。
option(SWINX_GDI_TRACE "每个 GDI 入口向 stderr 打函数名（诊断用）" OFF)
if(SWINX_GDI_TRACE)
    add_definitions(-DSWINX_GDI_TRACE)
endif()

# NOT defined here, unlike mobile.cmake: ENABLE_VIRTUAL_HWND selects the
# PlatformAPI shape where the host toolkit owns the real window and swinx only
# tracks a mirror registered through SConnection::RegisterVirtualHWND.  The
# wasm SConnection is forked from src/platform/freertos/, which makes every
# window a virtual _Window inside swinx itself and therefore needs no external
# registration entry point (see src/wnd.cpp:4327 for the only consumer).

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

# route C：不链接 cairo/png/fontconfig/freetype/pixman，故无对应 include 目录。
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/posix/*.cpp
    src/platform/wasm/*.cpp
    src/gdi/canvas2d/*.cpp
)

# route C：src/sdc.cpp 只包含 cairo/apple 后端的 HDC__ 生命周期（struct DCState、
# 构造/析构、SaveState/RestoreState）。wasm 由 src/gdi/canvas2d/gdi_canvas2d.cpp
# 提供同一组 HDC__ 方法的 canvas2d 实现，且 sdc.cpp 的 cairo 分支（cairo_save/
# cairo_matrix_t/mtx/currentPath 等）在 __EMSCRIPTEN__ 下根本无法编译，故从
# wasm 构建中排除，避免重复定义 + 编译失败。
list(FILTER SRCS EXCLUDE REGEX "/src/sdc\\.cpp$")

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

# Static archive only: the wasm executable / side module links libswinx.a plus
# the vendor archives, and SOUI components are merged statically
# (SOUI_ENABLE_COM_LIB=ON) because there is no dlopen-based component loader.
add_library(swinx STATIC ${SRCS} ${HEADERS})

# route C：canvas2d 后端使用 emscripten::val（embind），emscripten 的 val.h/wire.h
# 要求 -std=c++17 及以上（"embind requires -std=c++17 or newer"）。仅 wasm 端口的
# swinx 提升标准；其余目标（soui4/utilities4 等）保持原标准，链接 ABI 兼容。
set_target_properties(swinx PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# ---- link libraries ---------------------------------------------------------
# route C：移除 cairo / pixman / freetype / fontconfig（及它们间接拉起的 brotli），
# 仅保留 POSIX 运行时。dl 在 Emscripten 上并入 libc，用 CMAKE_DL_LIBS（此处为空）。
target_link_libraries(swinx
    ${CMAKE_DL_LIBS} m
)

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${CMAKE_DL_LIBS} m CACHE INTERNAL "swinx_dep_libs")
endif()

target_include_directories(swinx
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wasm
)
