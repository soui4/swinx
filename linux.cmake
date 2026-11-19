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

# ---------------------------------------------------------------------------
# Linux 窗口系统后端选择
#
# SWINX_LINUX_BACKEND 由 swinx/CMakeLists.txt 统一解析（根工程可用
# -DSOUI_LINUX_BACKEND=wayland 或 -DSOUI_USE_WAYLAND=ON 覆盖）。这里是该开关
# 唯一消费点：它同时决定参与编译的源文件目录、第三方依赖和导出宏，避免两个
# 后端目录同时被 GLOB 造成 SConnection 等同名符号重复定义。
# ---------------------------------------------------------------------------
if(NOT DEFINED SWINX_LINUX_BACKEND OR "${SWINX_LINUX_BACKEND}" STREQUAL "")
    set(SWINX_LINUX_BACKEND "x11")
endif()
if(NOT "${SWINX_LINUX_BACKEND}" MATCHES "^(x11|wayland)$")
    message(FATAL_ERROR "SWINX_LINUX_BACKEND 必须是 'x11' 或 'wayland'（当前为 '${SWINX_LINUX_BACKEND}'）")
endif()
message(STATUS "####SWINX_LINUX_BACKEND=${SWINX_LINUX_BACKEND}")

if(SWINX_LINUX_BACKEND STREQUAL "x11")
    # ---- X11/XCB 后端（现有实现，行为保持不变）----
    add_subdirectory(thirdparty/xkbcommon)
    add_subdirectory(thirdparty/libxcb)
    add_subdirectory(thirdparty/xcb-imdkit)
    add_subdirectory(thirdparty/dbus-1.14.10)

    get_target_property(DBUS_INCLUDE_DIRS dbus-1 INTERFACE_INCLUDE_DIRECTORIES)
    include_directories(${DBUS_INCLUDE_DIRS})

    set(SWINX_BACKEND_SRCS
        thirdparty/xcb-util-image/*.c
        thirdparty/xcb-util-renderutil/*.c
        thirdparty/xcb-util-keysyms/*.c
        thirdparty/xcb-util-wm/*.c
        thirdparty/xcb-util/*.c
        thirdparty/libxcb/*.c
        src/platform/linux/*.cpp
    )
    set(SWINX_BACKEND_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xkbcommon
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb-imdkit/include
    )
    set(SWINX_BACKEND_LINK_LIBS
        xkbcommon
        xcb-imdkit
        dbus-1
    )
    set(SWINX_BACKEND_DEPENDS
        xcb-imdkit
        xkbcommon
        dbus-1
    )
    set(SWINX_BACKEND_DEFS SWINX_USE_X11=1)
    set(SWINX_BACKEND_SYSTEM_LIBS xcb)
    set(SWINX_BACKEND_GENERATED_SRCS "")
else()
    # ---- Wayland 后端（阶段 0）----
    # wl_display + registry + xdg_wm_base + wl_shm，单个 xdg_toplevel 的
    # configure/close 与双缓冲上屏（见 src/platform/wayland/framebuffer.cpp）。
    #
    # 依赖：
    #   - libwayland-client   （运行时；wayland-client 头文件）
    #   - wayland-protocols   （stable/xdg-shell/xdg-shell.xml）
    #   - wayland-scanner     （生成 xdg-shell 客户端绑定）
    # 三者任一缺失时仍然编译“内存帧缓冲”骨架：库可构建、共享代码可链接，
    # 只是不会连接显示服务器（framebuffer.cpp 用 SWINX_HAVE_WAYLAND_CLIENT 分支）。
    #
    # thirdparty/cairo 由 SWINX_LINUX_BACKEND 驱动，在 wayland 模式下关闭
    # Xlib/XCB surface（见 cairo/Configure_features.cmake），因此本分支既不
    # 需要 xcb 头文件，也不链接 libxcb；渲染只走 cairo image surface。
    pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
    pkg_check_modules(WAYLAND_PROTOCOLS QUIET wayland-protocols)
    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)

    # 系统托盘（Shell_NotifyIcon）在 Wayland 下走 freedesktop StatusNotifierItem
    # over session D-Bus。dbus-1 已 vendor 化（X11 分支也在用），无 X11 硬依赖：
    # DBUS_BUILD_X11 只在 X11_FOUND 时可选打开，Wayland 构建不会引入 libxcb。
    add_subdirectory(thirdparty/dbus-1.14.10)
    get_target_property(DBUS_INCLUDE_DIRS dbus-1 INTERFACE_INCLUDE_DIRECTORIES)

    set(SWINX_BACKEND_GENERATED_SRCS "")
    set(SWINX_BACKEND_SRCS
        src/platform/wayland/*.cpp
    )
    set(SWINX_BACKEND_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/wayland
        ${DBUS_INCLUDE_DIRS}
    )
    set(SWINX_BACKEND_LINK_LIBS dbus-1)
    set(SWINX_BACKEND_DEPENDS dbus-1)
    set(SWINX_BACKEND_DEFS SWINX_USE_WAYLAND=1 SWINX_HAVE_SNI=1)
    set(SWINX_BACKEND_SYSTEM_LIBS "")

    if(WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_FOUND AND WAYLAND_SCANNER_EXECUTABLE)
        pkg_get_variable(SWINX_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        set(SWINX_XDG_SHELL_XML "${SWINX_WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
        if(NOT EXISTS "${SWINX_XDG_SHELL_XML}")
            message(FATAL_ERROR "找不到 xdg-shell 协议描述：${SWINX_XDG_SHELL_XML}（请安装 wayland-protocols）")
        endif()

        set(SWINX_WAYLAND_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
        set(SWINX_XDG_SHELL_HEADER "${SWINX_WAYLAND_PROTO_DIR}/xdg-shell-client-protocol.h")
        set(SWINX_XDG_SHELL_SOURCE "${SWINX_WAYLAND_PROTO_DIR}/xdg-shell-protocol.c")
        add_custom_command(
            OUTPUT "${SWINX_XDG_SHELL_HEADER}" "${SWINX_XDG_SHELL_SOURCE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SWINX_WAYLAND_PROTO_DIR}"
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${SWINX_XDG_SHELL_XML}" "${SWINX_XDG_SHELL_HEADER}"
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code "${SWINX_XDG_SHELL_XML}" "${SWINX_XDG_SHELL_SOURCE}"
            DEPENDS "${SWINX_XDG_SHELL_XML}"
            COMMENT "Generating xdg-shell client protocol"
            VERBATIM
        )
        # text-input-v3 是 unstable 协议，部分发行版不安装；缺失时只退化为
        # 无输入法（纯键盘 WM_CHAR），不能影响 xdg-shell 的硬依赖。
        set(SWINX_TEXT_INPUT_V3_XML "${SWINX_WAYLAND_PROTOCOLS_DIR}/unstable/text-input/text-input-unstable-v3.xml")
        set(SWINX_WL_TEXT_INPUT_SOURCES "")
        if(EXISTS "${SWINX_TEXT_INPUT_V3_XML}")
            set(SWINX_TEXT_INPUT_V3_HEADER "${SWINX_WAYLAND_PROTO_DIR}/text-input-unstable-v3-client-protocol.h")
            set(SWINX_TEXT_INPUT_V3_SOURCE "${SWINX_WAYLAND_PROTO_DIR}/text-input-unstable-v3-protocol.c")
            add_custom_command(
                OUTPUT "${SWINX_TEXT_INPUT_V3_HEADER}" "${SWINX_TEXT_INPUT_V3_SOURCE}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SWINX_WAYLAND_PROTO_DIR}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${SWINX_TEXT_INPUT_V3_XML}" "${SWINX_TEXT_INPUT_V3_HEADER}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code "${SWINX_TEXT_INPUT_V3_XML}" "${SWINX_TEXT_INPUT_V3_SOURCE}"
                DEPENDS "${SWINX_TEXT_INPUT_V3_XML}"
                COMMENT "Generating text-input-unstable-v3 client protocol"
                VERBATIM
            )
            list(APPEND SWINX_WL_TEXT_INPUT_SOURCES "${SWINX_TEXT_INPUT_V3_SOURCE}")
            list(APPEND SWINX_BACKEND_GENERATED_SRCS "${SWINX_TEXT_INPUT_V3_SOURCE}")
            list(APPEND SWINX_BACKEND_DEFS SWINX_HAVE_TEXT_INPUT_V3=1)
        else()
            message(STATUS "未找到 text-input-unstable-v3.xml：Wayland 输入法不可用（键盘输入仍正常）")
        endif()

        # wp_viewporter + wp_fractional_scale_v1：非整数缩放。GNOME 在 125%/150%
        # 下 wl_output.scale 只能报 2，真实倍率只能靠 fractional_scale_v1 的
        # preferred_scale（120=1.0）拿到；viewporter 用来把按物理像素渲染的
        # buffer 映射回合成器的逻辑坐标。两者缺一就退化为整数 scale（旧行为）。
        set(SWINX_VIEWPORTER_XML "${SWINX_WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
        # fractional-scale-v1 在 wayland-protocols 1.31 还是在 unstable/，
        # 1.32 才移到 stable/。多个发行版（如 Ubuntu 22.04）自带 1.20~1.31，
        # 只找 stable/ 会把非整数缩放误判成不可用。
        set(SWINX_FRACTIONAL_SCALE_XML "")
        foreach(_fs_dir stable staging unstable)
            set(_fs_candidate "${SWINX_WAYLAND_PROTOCOLS_DIR}/${_fs_dir}/fractional-scale/fractional-scale-v1.xml")
            if(EXISTS "${_fs_candidate}")
                set(SWINX_FRACTIONAL_SCALE_XML "${_fs_candidate}")
                break()
            endif()
        endforeach()
        set(SWINX_WL_SCALE_SOURCES "")
        if(EXISTS "${SWINX_VIEWPORTER_XML}" AND SWINX_FRACTIONAL_SCALE_XML)
            set(SWINX_VIEWPORTER_HEADER "${SWINX_WAYLAND_PROTO_DIR}/viewporter-client-protocol.h")
            set(SWINX_VIEWPORTER_SOURCE "${SWINX_WAYLAND_PROTO_DIR}/viewporter-protocol.c")
            set(SWINX_FRACTIONAL_SCALE_HEADER "${SWINX_WAYLAND_PROTO_DIR}/fractional-scale-v1-client-protocol.h")
            set(SWINX_FRACTIONAL_SCALE_SOURCE "${SWINX_WAYLAND_PROTO_DIR}/fractional-scale-v1-protocol.c")
            add_custom_command(
                OUTPUT "${SWINX_VIEWPORTER_HEADER}" "${SWINX_VIEWPORTER_SOURCE}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${SWINX_VIEWPORTER_XML}" "${SWINX_VIEWPORTER_HEADER}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code "${SWINX_VIEWPORTER_XML}" "${SWINX_VIEWPORTER_SOURCE}"
                DEPENDS "${SWINX_VIEWPORTER_XML}"
                COMMENT "Generating wp_viewporter client protocol"
                VERBATIM
            )
            add_custom_command(
                OUTPUT "${SWINX_FRACTIONAL_SCALE_HEADER}" "${SWINX_FRACTIONAL_SCALE_SOURCE}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${SWINX_FRACTIONAL_SCALE_XML}" "${SWINX_FRACTIONAL_SCALE_HEADER}"
                COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code "${SWINX_FRACTIONAL_SCALE_XML}" "${SWINX_FRACTIONAL_SCALE_SOURCE}"
                DEPENDS "${SWINX_FRACTIONAL_SCALE_XML}"
                COMMENT "Generating wp_fractional_scale_v1 client protocol"
                VERBATIM
            )
            list(APPEND SWINX_WL_SCALE_SOURCES "${SWINX_VIEWPORTER_SOURCE}" "${SWINX_FRACTIONAL_SCALE_SOURCE}")
            list(APPEND SWINX_BACKEND_GENERATED_SRCS ${SWINX_WL_SCALE_SOURCES})
            list(APPEND SWINX_BACKEND_DEFS SWINX_HAVE_FRACTIONAL_SCALE=1)
            message(STATUS "Wayland 非整数缩放已启用：wp_viewporter + wp_fractional_scale_v1 (${SWINX_FRACTIONAL_SCALE_XML})")
        else()
            message(STATUS "未找到 viewporter/fractional-scale 协议（搜过 ${SWINX_WAYLAND_PROTOCOLS_DIR} 的 stable/staging/unstable）："
                           "Wayland 退化为整数倍率缩放，非整数缩放禁用")
        endif()

        add_custom_target(swinx_wayland_protocol
            DEPENDS "${SWINX_XDG_SHELL_HEADER}" "${SWINX_XDG_SHELL_SOURCE}"
                    ${SWINX_WL_TEXT_INPUT_SOURCES} ${SWINX_WL_SCALE_SOURCES}
        )

        list(APPEND SWINX_BACKEND_GENERATED_SRCS "${SWINX_XDG_SHELL_SOURCE}")
        list(APPEND SWINX_BACKEND_INCLUDE_DIRS
            "${SWINX_WAYLAND_PROTO_DIR}"
            ${WAYLAND_CLIENT_INCLUDE_DIRS}
        )
        list(APPEND SWINX_BACKEND_LINK_LIBS ${WAYLAND_CLIENT_LIBRARIES})
        list(APPEND SWINX_BACKEND_DEPENDS swinx_wayland_protocol)
        list(APPEND SWINX_BACKEND_DEFS SWINX_HAVE_WAYLAND_CLIENT=1)

        message(STATUS "Wayland 后端已启用：wayland-client ${WAYLAND_CLIENT_VERSION}，"
                       "xdg-shell 绑定生成到 ${SWINX_WAYLAND_PROTO_DIR}")
    else()
        message(WARNING "Wayland 后端缺少依赖（需要 libwayland-dev / wayland-protocols / wayland-scanner）："
                        "将只编译内存帧缓冲骨架，不连接显示服务器")
    endif()
endif()

get_target_property(CAIRO_INCLUDE_DIRS cairo INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${CAIRO_INCLUDE_DIRS})

file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    ${SWINX_BACKEND_SRCS}
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/posix/*.cpp
    src/platform/common/*.cpp   # 与 freertos 共用的中立层（消息泵 / 性能计数器 / 码表转换）
    src/gdi/cairo/*.cpp
    )

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

if (NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS} ${SWINX_BACKEND_GENERATED_SRCS})

    # 确保导出所有符号，包括fontconfig和freetype的符号
    set_target_properties(swinx PROPERTIES
        LINK_FLAGS "-Wl,--export-dynamic"
    )
    set_target_properties(swinx PROPERTIES
        INSTALL_RPATH "\$ORIGIN"
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS} ${SWINX_BACKEND_GENERATED_SRCS})
endif()

# 后端选择结果导出给共享代码与下游目标（例如 GL 选择 GLX/EGL、测试过滤）。
target_compile_definitions(swinx PUBLIC ${SWINX_BACKEND_DEFS})

# Add dependencies to ensure proper build order for all internal libraries
add_dependencies(swinx cairo fontconfig freetype pixman-1 ${SWINX_BACKEND_DEPENDS})
set(SWINX_LIBS dl ${SWINX_BACKEND_SYSTEM_LIBS} uuid atomic m stdc++ ${ALSA_LIBRARIES})
if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_link_libraries(swinx
    cairo              # Our internal cairo target
    fontconfig         # Needed by src/gdi/cairo/fontapi.cpp / src/gdi/cairo
    freetype           # Needed by src/gdi/cairo/fontapi.cpp (glyph / metrics queries)
    ${SWINX_BACKEND_LINK_LIBS}
    ${SWINX_LIBS}
)

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${SWINX_BACKEND_INCLUDE_DIRS}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    PRIVATE ${ALSA_INCLUDE_DIRS}
)
