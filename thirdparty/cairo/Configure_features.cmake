if(CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS")
    set(CAIRO_HAS_XCB_SURFACE 0)
elseif(CMAKE_SYSTEM_NAME MATCHES Android)
    set(CAIRO_HAS_XCB_SURFACE 0)
elseif(EMSCRIPTEN)
    # 浏览器里既没有 X server 也没有 xcb：thirdparty 的 xcb/dbus 家族只在
    # linux.cmake 里加入构建，Emscripten 分支（wasm.cmake）不编译它们，而宿主机的
    # /usr/include/xcb 又不在 emcc 的 sysroot 里，默认开启会让 cairo 编译期直接
    # fatal error: 'xcb/xcb.h' file not found。wasm 只用 image surface 后端。
    set(CAIRO_HAS_XCB_SURFACE 0)
elseif(APPLE)
    # Quartz backends are enabled only when quartz_deps (CoreFoundation and
    # ApplicationServices) were found via ENABLE_QUARTZ, mirroring meson's
    # option('quartz') behavior.
    if(CAIRO_QUARTZ_DEPS_FOUND)
        set(CAIRO_HAS_QUARTZ_SURFACE 1)
        set(CAIRO_HAS_QUARTZ_IMAGE_SURFACE 1)
        if(HAVE_CTFontDrawGlyphs)
            set(CAIRO_HAS_QUARTZ_FONT 1)
        endif()
    endif()
else()
    set(CAIRO_HAS_XCB_SURFACE 1)
endif()

if(BEOS)
    set(CAIRO_HAS_BEOS_SURFACE 1)
endif()

if(OS2)
    set(CAIRO_HAS_OS2_SURFACE 1)
endif()

if(WIN32)
    set(CAIRO_HAS_WIN32_SURFACE 1)
    set(CAIRO_HAS_WIN32_FONT 1)
endif()

if(COGL_FOUND)
    set(CAIRO_HAS_COGL_SURFACE 1)
endif()

if(DIRECTFB_FOUND)
    set(CAIRO_HAS_DIRECTFB_SURFACE 1)
endif()

if(LIBDRM_FOUND)
    set(CAIRO_HAS_DRM_SURFACE 1)
endif()


set(CAIRO_HAS_FT_FONT 1)
set(CAIRO_HAS_FC_FONT 1)

if(GLIB_FOUND)
    set(CAIRO_HAS_GOBJECT_FUNCTIONS 1)
endif()

if(LIBXML2_FOUND)
    set(CAIRO_HAS_XML_SURFACE 1)
endif()

if(MESA_FOUND)
    set(CAIRO_HAS_GALLIUM_SURFACE 1)
endif()

if(OPENGL_FOUND)
    set(CAIRO_HAS_GL_SURFACE 1)
endif()

if(PNG_FOUND)
    set(CAIRO_HAS_PNG_FUNCTIONS 1)
endif()

#if(QT_FOUND)
#    set(CAIRO_HAS_QT_SURFACE 1)
#endif()

if(X11_FOUND AND NOT CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS")
    set(CAIRO_HAS_XLIB_SURFACE 1)
    set(CAIRO_HAS_XLIB_XRENDER_SURFACE 1)
    set(CAIRO_HAS_XCB_SURFACE 1)
    set(CAIRO_HAS_XLIB_XCB_FUNCTIONS 1)
    set(CAIRO_HAS_XCB_SHM_FUNCTIONS 1)
endif()

#if(ZLIB_FOUND)
#    set(CAIRO_HAS_PS_SURFACE 1)
#    set(CAIRO_HAS_PDF_SURFACE 1)
#endif()

#if(SKIA_FOUND)
#    set(CAIRO_HAS_SKIA_SURFACE 1)
#endif()

# Route 1 (soui-wasm 体积治理): SOUI 的 swinx GDI 后端只用 cairo image surface +
# freetype/fontconfig，从不创建 SVG / script / xml 这三类"记录/输出型" surface。
# swinx 维护者确认不使用 cairo 的 svg / xml 能力，故全部平台关闭；源文件由
# src/CMakeLists.txt + src/sources.cmake 按宏排除 (cairo-svg-surface.c /
# cairo-script-surface.c / cairo-xml-surface.c 不再编入 libcairo.a)，可减小静态库体积。
set(CAIRO_HAS_SVG_SURFACE 0)
set(CAIRO_HAS_SCRIPT_SURFACE 0)
set(CAIRO_HAS_INTERPRETER 0)
# XML surface 依赖 libxml2；swinx 各平台均不 find libxml2，原本就是 undef。
# 显式置 0 以防未来误探测到 libxml2 时把它打开。
set(CAIRO_HAS_XML_SURFACE 0)


set(CAIRO_HAS_GLESV2_SURFACE 0)
set(CAIRO_HAS_VG_SURFACE 0)
set(CAIRO_HAS_EGL_FUNCTIONS 0)
set(CAIRO_HAS_GLX_FUNCTIONS 0)
set(CAIRO_HAS_WGL_FUNCTIONS 0)
set(CAIRO_HAS_TEST_SURFACES 0)
set(CAIRO_HAS_TEE_SURFACE 0)

set(CAIRO_HAS_PTHREAD 0)
set(CAIRO_HAS_TRACE 0)
set(CAIRO_HAS_SYMBOL_LOOKUP 0)
set(CAIRO_HAS_FC_FONT 1)
configure_file(features.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/cairo-features.h)
configure_file(features.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/src/cairo-features.h)
