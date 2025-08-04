# Generate configuration files for fontconfig

# Generate font paths configuration
set(FC_DEFAULT_FONTS_XML "")
set(FC_DEFAULT_FONTS_ESCAPED "")
foreach(path ${FC_FONTS_PATHS})
    string(APPEND FC_DEFAULT_FONTS_XML "\t<dir>${path}</dir>\n")
    string(APPEND FC_DEFAULT_FONTS_ESCAPED "\\t<dir>${path}</dir>\\n")
endforeach()

set(FC_FONTPATH_XML "")
set(FC_FONTPATH_ESCAPED "")
foreach(path ${FC_ADD_FONTS})
    string(APPEND FC_FONTPATH_XML "\t<dir>${path}</dir>\n")
    string(APPEND FC_FONTPATH_ESCAPED "\\t<dir>${path}</dir>\\n")
endforeach()

# Configure config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    @ONLY
)

# Configure fonts.conf
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts.conf.in
    ${CMAKE_CURRENT_BINARY_DIR}/fonts.conf
    @ONLY
)

# Generate pkg-config file
set(PKG_CONFIG_REQUIRES_PRIVATE "freetype2 >= 21.0.15")
set(PKG_CONFIG_LIBS_PRIVATE "")
if(MATH_LIBRARY)
    string(APPEND PKG_CONFIG_LIBS_PRIVATE " -l${MATH_LIBRARY}")
endif()
if(Threads_FOUND)
    string(APPEND PKG_CONFIG_LIBS_PRIVATE " ${CMAKE_THREAD_LIBS_INIT}")
endif()
