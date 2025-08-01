set(cairo_headers cairo.h cairo-deprecated.h)

set(cairo_private
    cairoint.h
    cairo-analysis-surface-private.h
    cairo-arc-private.h
    cairo-array-private.h
    cairo-atomic-private.h
    cairo-backend-private.h
    cairo-box-inline.h
    cairo-boxes-private.h
    cairo-cache-private.h
    cairo-clip-inline.h
    cairo-clip-private.h
    cairo-combsort-inline.h
    cairo-compiler-private.h
    cairo-compositor-private.h
    cairo-contour-inline.h
    cairo-contour-private.h
    cairo-composite-rectangles-private.h
    cairo-damage-private.h
    cairo-default-context-private.h
    cairo-device-private.h
    cairo-error-inline.h
    cairo-error-private.h
    cairo-fixed-private.h
    cairo-fixed-type-private.h
    cairo-freelist-private.h
    cairo-freelist-type-private.h
    cairo-freed-pool-private.h
    cairo-fontconfig-private.h
    cairo-gstate-private.h
    cairo-hash-private.h
    cairo-image-info-private.h
    cairo-image-surface-inline.h
    cairo-image-surface-private.h
    cairo-list-inline.h
    cairo-list-private.h
    cairo-malloc-private.h
    cairo-mempool-private.h
    cairo-mutex-impl-private.h
    cairo-mutex-list-private.h
    cairo-mutex-private.h
    cairo-mutex-type-private.h
    cairo-output-stream-private.h
    cairo-paginated-private.h
    cairo-paginated-surface-private.h
    cairo-path-fixed-private.h
    cairo-path-private.h
    cairo-pattern-inline.h
    cairo-pattern-private.h
    cairo-pixman-private.h
    cairo-private.h
    cairo-recording-surface-inline.h
    cairo-recording-surface-private.h
    cairo-reference-count-private.h
    cairo-region-private.h
    cairo-rtree-private.h
    cairo-scaled-font-private.h
    cairo-slope-private.h
    cairo-spans-private.h
    cairo-spans-compositor-private.h
    cairo-stroke-dash-private.h
    cairo-surface-inline.h
    cairo-surface-private.h
    cairo-surface-backend-private.h
    cairo-surface-clipper-private.h
    cairo-surface-fallback-private.h
    cairo-surface-observer-inline.h
    cairo-surface-observer-private.h
    cairo-surface-offset-private.h
    cairo-surface-subsurface-inline.h
    cairo-surface-subsurface-private.h
    cairo-surface-snapshot-inline.h
    cairo-surface-snapshot-private.h
    cairo-surface-wrapper-private.h
    cairo-time-private.h
    cairo-types-private.h
    cairo-traps-private.h
    cairo-tristrip-private.h
    cairo-user-font-private.h
    cairo-wideint-private.h
    cairo-wideint-type-private.h 
)

set(cairo_sources
    cairo-analysis-surface.c 
    cairo-arc.c 
    cairo-array.c 
    cairo-atomic.c 
    cairo-base64-stream.c 
    cairo-base85-stream.c 
    cairo-bentley-ottmann.c 
    cairo-bentley-ottmann-rectangular.c 
    cairo-bentley-ottmann-rectilinear.c 
    cairo-botor-scan-converter.c 
    cairo-boxes.c 
    cairo-boxes-intersect.c 
    cairo.c 
    cairo-cache.c 
    cairo-clip.c 
    cairo-clip-boxes.c 
    cairo-clip-polygon.c 
    cairo-clip-region.c 
    cairo-clip-surface.c 
    cairo-color.c 
    cairo-composite-rectangles.c 
    cairo-compositor.c 
    cairo-contour.c 
    cairo-damage.c 
    cairo-debug.c 
    cairo-default-context.c 
    cairo-device.c 
    cairo-error.c 
    cairo-fallback-compositor.c 
    cairo-fixed.c 
    cairo-font-face.c 
    cairo-font-face-twin.c 
    cairo-font-face-twin-data.c 
    cairo-font-options.c 
    cairo-freelist.c 
    cairo-freed-pool.c 
    cairo-gstate.c 
    cairo-hash.c 
    cairo-hull.c 
    cairo-image-compositor.c 
    cairo-image-info.c 
    cairo-image-source.c 
    cairo-image-surface.c 
    cairo-lzw.c 
    cairo-matrix.c 
    cairo-mask-compositor.c 
    cairo-mesh-pattern-rasterizer.c 
    cairo-mempool.c 
    cairo-misc.c 
    cairo-mono-scan-converter.c 
    cairo-mutex.c 
    cairo-no-compositor.c 
    cairo-observer.c 
    cairo-output-stream.c 
    cairo-paginated-surface.c 
    cairo-path-bounds.c 
    cairo-path.c 
    cairo-path-fill.c 
    cairo-path-fixed.c 
    cairo-path-in-fill.c 
    cairo-path-stroke.c 
    cairo-path-stroke-boxes.c 
    cairo-path-stroke-polygon.c 
    cairo-path-stroke-traps.c 
    cairo-path-stroke-tristrip.c 
    cairo-pattern.c 
    cairo-pen.c 
    cairo-polygon.c 
    cairo-polygon-intersect.c 
    cairo-polygon-reduce.c 
    cairo-raster-source-pattern.c 
    cairo-recording-surface.c 
    cairo-rectangle.c 
    cairo-rectangular-scan-converter.c 
    cairo-region.c 
    cairo-rtree.c 
    cairo-scaled-font.c 
    cairo-shape-mask-compositor.c 
    cairo-slope.c 
    cairo-spans.c 
    cairo-spans-compositor.c 
    cairo-spline.c 
    cairo-stroke-dash.c 
    cairo-stroke-style.c 
    cairo-surface.c 
    cairo-surface-clipper.c 
    cairo-surface-fallback.c 
    cairo-surface-observer.c 
    cairo-surface-offset.c 
    cairo-surface-snapshot.c 
    cairo-surface-subsurface.c 
    cairo-surface-wrapper.c 
    cairo-time.c 
    cairo-tor-scan-converter.c 
    cairo-tor22-scan-converter.c 
    cairo-clip-tor-scan-converter.c 
    cairo-toy-font-face.c 
    cairo-traps.c 
    cairo-tristrip.c 
    cairo-traps-compositor.c 
    cairo-unicode.c 
    cairo-user-font.c 
    cairo-version.c 
    cairo-wideint.c 
    cairo-line.c
)

set(_cairo_font_subset_private
    cairo-scaled-font-subsets-private.h
    cairo-truetype-subset-private.h
    cairo-type1-private.h
    cairo-type3-glyph-surface-private.h
)
set(_cairo_font_subset_sources
    cairo-cff-subset.c
    cairo-scaled-font-subsets.c
    cairo-truetype-subset.c
    cairo-type1-fallback.c
    cairo-type1-glyph-names.c
    cairo-type1-subset.c
    cairo-type3-glyph-surface.c
)

list(APPEND cairo_private ${_cairo_font_subset_private})
list(APPEND cairo_sources ${_cairo_font_subset_sources})

set(cairo_egl_sources)
set(cairo_glx_sources)
set(cairo_wgl_sources)

set(_cairo_pdf_operators_private cairo-pdf-operators-private.h cairo-pdf-shading-private.h)
set(_cairo_pdf_operators_sources cairo-pdf-operators.c cairo-pdf-shading.c)
list(APPEND cairo_private ${_cairo_pdf_operators_private})
list(APPEND cairo_sources ${_cairo_pdf_operators_sources})

set(cairo_png_sources cairo-png.c)

set(cairo_ps_headers cairo-ps.h)
set(cairo_ps_private cairo-ps-surface-private.h)
set(cairo_ps_sources cairo-ps-surface.c)

set(_cairo_deflate_stream_sources cairo-deflate-stream.c)
list(APPEND cairo_sources ${_cairo_deflate_stream_sources})

set(cairo_pdf_headers cairo-pdf.h)
set(cairo_pdf_private cairo-pdf-surface-private.h)
set(cairo_pdf_sources cairo-pdf-surface.c)

set(cairo_svg_headers cairo-svg.h)
set(cairo_svg_private cairo-svg-surface-private.h)
set(cairo_svg_sources cairo-svg-surface.c)

set(cairo_ft_headers cairo-ft.h)
set(cairo_ft_private cairo-ft-private.h)
set(cairo_ft_sources cairo-ft-font.c)

# These are private, even though they look like public headers
set(cairo_test_surfaces_private
    test-compositor-surface.h
    test-compositor-surface-private.h
    test-null-compositor-surface.h
    test-paginated-surface.h 
)
set(cairo_test_surfaces_sources
    test-compositor-surface.c
    test-null-compositor-surface.c
    test-base-compositor-surface.c
    test-paginated-surface.c
)

set(cairo_xlib_headers cairo-xlib.h)
set(cairo_xlib_private
    cairo-xlib-private.h
    cairo-xlib-surface-private.h
    cairo-xlib-xrender-private.h
)
set(cairo_xlib_sources
    cairo-xlib-display.c
    cairo-xlib-core-compositor.c
    cairo-xlib-fallback-compositor.c
    cairo-xlib-render-compositor.c
    cairo-xlib-screen.c
    cairo-xlib-source.c
    cairo-xlib-surface.c
    cairo-xlib-surface-shm.c
    cairo-xlib-visual.c
    cairo-xlib-xcb-surface.c
)

set(cairo_xlib_xrender_headers cairo-xlib-xrender.h)

set(cairo_xcb_headers cairo-xcb.h)
set(cairo_xcb_private cairo-xcb-private.h)
set(cairo_xcb_sources
    cairo-xcb-connection.c
    cairo-xcb-connection-core.c
    cairo-xcb-connection-render.c
    cairo-xcb-connection-shm.c
    cairo-xcb-screen.c
    cairo-xcb-shm.c
    cairo-xcb-surface.c
    cairo-xcb-resources.c
    cairo-xcb-surface-core.c
    cairo-xcb-surface-render.c
)

set(cairo_qt_headers cairo-qt.h)
set(cairo_qt_cxx_sources cairo-qt-surface.cpp)

set(cairo_quartz_headers cairo-quartz.h)
set(cairo_quartz_private cairo-quartz-private.h)
set(cairo_quartz_sources cairo-quartz-surface.c)

set(cairo_quartz_image_headers cairo-quartz-image.h)
set(cairo_quartz_image_sources cairo-quartz-image-surface.c)

set(cairo_quartz_font_sources cairo-quartz-font.c)

set(cairo_win32_headers cairo-win32.h)
set(cairo_win32_private win32/cairo-win32-private.h)
set(cairo_win32_sources 
        win32/cairo-win32-debug.c
        win32/cairo-win32-device.c
        win32/cairo-win32-gdi-compositor.c
        win32/cairo-win32-system.c
        win32/cairo-win32-surface.c
        win32/cairo-win32-display-surface.c
        win32/cairo-win32-printing-surface.c
)
set(cairo_win32_font_sources win32/cairo-win32-font.c)

set(cairo_skia_headers cairo-skia.h)
set(cairo_skia_private skia/cairo-skia-private.h)
set(cairo_skia_cxx_sources
    skia/cairo-skia-context.cpp
    skia/cairo-skia-surface.cpp
)

set(cairo_os2_headers cairo-os2.h)
set(cairo_os2_private cairo-os2-private.h)
set(cairo_os2_sources cairo-os2-surface.c)

# automake is stupid enough to always use c++ linker if we enable the
# following lines, even if beos surface is not enabled.  Disable it for now.
set(cairo_beos_headers cairo-beos.h)
set(cairo_beos_cxx_sources cairo-beos-surface.cpp)

set(cairo_gl_headers cairo-gl.h)
set(cairo_gl_private 
        cairo-gl-private.h
        cairo-gl-dispatch-private.h
        cairo-gl-ext-def-private.h
        cairo-gl-gradient-private.h
)
set(cairo_gl_sources 
        cairo-gl-composite.c
        cairo-gl-device.c
        cairo-gl-dispatch.c
        cairo-gl-glyphs.c
        cairo-gl-gradient.c
        cairo-gl-info.c
        cairo-gl-operand.c
        cairo-gl-shaders.c
        cairo-gl-msaa-compositor.c
        cairo-gl-spans-compositor.c
        cairo-gl-traps-compositor.c
        cairo-gl-source.c
        cairo-gl-surface.c
)
set(cairo_glesv2_headers ${cairo_gl_headers})
set(cairo_glesv2_private ${cairo_gl_private})
set(cairo_glesv2_sources ${cairo_gl_sources})

list(APPEND cairo_egl_sources cairo-egl-context.c)
list(APPEND cairo_glx_sources cairo-glx-context.c)
list(APPEND cairo_wgl_sources cairo-wgl-context.c)

set(cairo_directfb_headers cairo-directfb.h)
set(cairo_directfb_sources cairo-directfb-surface.c)

set(cairo_drm_headers cairo-drm.h)
set(cairo_drm_private 
        drm/cairo-drm-private.h
        drm/cairo-drm-ioctl-private.h
        drm/cairo-drm-intel-private.h
        drm/cairo-drm-intel-brw-defines.h
        drm/cairo-drm-intel-brw-structs.h
        drm/cairo-drm-intel-brw-eu.h
        drm/cairo-drm-intel-command-private.h
        drm/cairo-drm-intel-ioctl-private.h
        drm/cairo-drm-i915-private.h
        drm/cairo-drm-i965-private.h
        drm/cairo-drm-radeon-private.h
)

set(cairo_drm_sources drm/cairo-drm.c
            drm/cairo-drm-bo.c
            drm/cairo-drm-surface.c
            drm/cairo-drm-intel.c
            drm/cairo-drm-intel-debug.c
            drm/cairo-drm-intel-surface.c
            drm/cairo-drm-i915-surface.c
            drm/cairo-drm-i915-glyphs.c
            drm/cairo-drm-i915-shader.c
            drm/cairo-drm-i915-spans.c
            drm/cairo-drm-i965-surface.c
            drm/cairo-drm-i965-glyphs.c
            drm/cairo-drm-i965-shader.c
            drm/cairo-drm-i965-spans.c
            drm/cairo-drm-intel-brw-eu.c
            drm/cairo-drm-intel-brw-eu-emit.c
            drm/cairo-drm-intel-brw-eu-util.c
            drm/cairo-drm-radeon.c
            drm/cairo-drm-radeon-surface.c
)
set(cairo_gallium_sources drm/cairo-drm-gallium-surface.c)

set(cairo_script_headers cairo-script.h)
set(cairo_script_private cairo-script-private.h)
set(cairo_script_sources cairo-script-surface.c)

set(cairo_tee_headers cairo-tee.h)
set(cairo_tee_private cairo-tee-surface-private.h)
set(cairo_tee_sources cairo-tee-surface.c)

set(cairo_xml_headers cairo-xml.h)
set(cairo_xml_sources cairo-xml-surface.c)

set(cairo_vg_headers cairo-vg.h)
set(cairo_vg_sources cairo-vg-surface.c)

set(cairo_cogl_headers cairo-cogl.h)
set(cairo_cogl_private 
        cairo-cogl-private.h
        cairo-cogl-gradient-private.h
        cairo-cogl-context-private.h
        cairo-cogl-utils-private.h
)
set(cairo_cogl_sources 
        cairo-cogl-surface.c
        cairo-cogl-gradient.c
        cairo-cogl-context.c
        cairo-cogl-utils.c
)