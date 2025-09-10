cmake_minimum_required(VERSION 3.16)

project(fontconfig
    VERSION 2.17.1
    LANGUAGES C
    DESCRIPTION "Font configuration and customization library"
)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include necessary modules
include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CMakePackageConfigHelpers)
include(FindPkgConfig)

# Project version variables
set(FC_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(FC_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(FC_VERSION_MICRO ${PROJECT_VERSION_PATCH})

# Library versioning (compatible with libtool versioning)
set(SOVERSION ${FC_VERSION_MAJOR})
math(EXPR CURVERSION "${FC_VERSION_MINOR} - 1")
set(LIBVERSION "${SOVERSION}.${CURVERSION}.0")
set(DEFVERSION "${CURVERSION}.${FC_VERSION_MICRO}")
math(EXPR OSXVERSION "${CURVERSION} + 1")
set(CACHEVERSION "9")

# Build options
option(ENABLE_NLS "Enable native language support" OFF)
option(ENABLE_CACHE_BUILD "Run fc-cache on install" OFF)
option(ENABLE_ICONV "Enable iconv support" OFF)
option(ENABLE_FONTATIONS "Use Fontations for indexing" OFF)
option(ENABLE_PREBUILT_GPERF "Use prebuilt gperf files instead of generating them" ON)

# Default configuration options
set(DEFAULT_HINTING "slight" CACHE STRING "Default hinting configuration")
set_property(CACHE DEFAULT_HINTING PROPERTY STRINGS none slight medium full)

set(DEFAULT_SUB_PIXEL_RENDERING "none" CACHE STRING "Default sub-pixel rendering")
set_property(CACHE DEFAULT_SUB_PIXEL_RENDERING PROPERTY STRINGS none bgr rgb vbgr vrgb)

set(BITMAP_CONF "no-except-emoji" CACHE STRING "Default bitmap font configuration")
set_property(CACHE BITMAP_CONF PROPERTY STRINGS yes no no-except-emoji)

# Path configuration
set(FC_CACHEDIR "/var/cache/fontconfig" CACHE PATH "Cache directory")
set(FC_TEMPLATEDIR "/usr/share/fontconfig/conf.avail" CACHE PATH "Template directory")
set(FC_BASECONFIGDIR "/etc/fonts" CACHE PATH "Base config directory")
set(FC_CONFIGDIR "${FC_BASECONFIGDIR}/conf.d" CACHE PATH "Config directory")
set(FC_XMLDIR "/usr/share/xml/fontconfig" CACHE PATH "XML directory")

# Font directories
set(DEFAULT_FONTS_DIRS "" CACHE STRING "Default font directories (semicolon separated)")
set(ADDITIONAL_FONTS_DIRS "" CACHE STRING "Additional font directories (semicolon separated)")

# Find required dependencies
find_package(PkgConfig REQUIRED)

# Find FreeType2
if(TARGET freetype)
    #message(STATUS "Using internal FreeType target")
    get_target_property(FREETYPE2_LIBRARIES freetype IMPORTED_LOCATION)
    get_target_property(FREETYPE2_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
else()
    find_package(Freetype REQUIRED)
    if(NOT Freetype_FOUND)
        message(FATAL_ERROR "FreeType2 not found")
    endif()
    set(FREETYPE2_LIBRARIES ${FREETYPE_LIBRARIES})
    set(FREETYPE2_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS})
endif()

# Find XML library
if(TARGET expat)
    #message(STATUS "Using internal Expat target")
    set(XML_LIBRARIES expat)
    get_target_property(XML_INCLUDE_DIRS expat INTERFACE_INCLUDE_DIRECTORIES)
else()
    pkg_check_modules(EXPAT expat)
    if(NOT EXPAT_FOUND)
        message(FATAL_ERROR "Expat not found")
    endif()
    set(XML_LIBRARIES ${EXPAT_LIBRARIES})
    set(XML_INCLUDE_DIRS ${EXPAT_INCLUDE_DIRS})
endif()

# Find optional dependencies
find_library(MATH_LIBRARY m)

# Find libintl for NLS support
if(ENABLE_NLS)
    find_package(Intl)
    if(Intl_FOUND)
        set(ENABLE_NLS_ACTUAL 1)
    else()
        set(ENABLE_NLS OFF)
        set(ENABLE_NLS_ACTUAL 0)
    endif()
endif()

# Find iconv if enabled
if(ENABLE_ICONV)
    find_package(Iconv)
    if(Iconv_FOUND)
        set(USE_ICONV 1)
    else()
        set(ENABLE_ICONV OFF)
        set(USE_ICONV 0)
    endif()
endif()

# Find threads (not on Windows)
if(NOT WIN32)
    find_package(Threads)
    if(Threads_FOUND)
        set(HAVE_PTHREAD 1)
    endif()
endif()


# Set up default font directories
if(NOT DEFAULT_FONTS_DIRS)
    if(WIN32)
        set(FC_FONTS_PATHS "WINDOWSFONTDIR;WINDOWSUSERFONTDIR")
    elseif(APPLE)
        set(FC_FONTS_PATHS "/System/Library/Fonts;/Library/Fonts;~/Library/Fonts;/System/Library/Assets/com_apple_MobileAsset_Font3;/System/Library/Assets/com_apple_MobileAsset_Font4")
    elseif(ANDROID)
        set(FC_FONTS_PATHS "/system/fonts/;/product/fonts/")
    else()
        set(FC_FONTS_PATHS "/usr/share/fonts;/usr/local/share/fonts")
    endif()
else()
    string(REPLACE ";" ";" FC_FONTS_PATHS "${DEFAULT_FONTS_DIRS}")
endif()

# Set up additional font directories
set(FC_ADD_FONTS "")
if(NOT ADDITIONAL_FONTS_DIRS)
    # Check for X11 font directories
    foreach(dir "/usr/X11R6/lib/X11" "/usr/X11/lib/X11" "/usr/lib/X11")
        if(EXISTS "${dir}/fonts")
            list(APPEND FC_ADD_FONTS "${dir}/fonts")
        endif()
    endforeach()
else()
    string(REPLACE ";" ";" FC_ADD_FONTS "${ADDITIONAL_FONTS_DIRS}")
endif()

# Configuration data
set(PACKAGE_NAME "${PROJECT_NAME}")
set(PACKAGE_TARNAME "${PROJECT_NAME}")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
set(PACKAGE_STRING "${PROJECT_NAME} ${PROJECT_VERSION}")
set(PACKAGE_BUGREPORT "https://gitlab.freedesktop.org/fontconfig/fontconfig/issues/new")
set(PACKAGE_URL "")

# Endianness check
include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

# Add compile definitions
add_compile_definitions(HAVE_CONFIG_H)
if(WIN32)
    add_compile_definitions(${WIN_EXPORT_ARGS})
endif()

# Set up include directories
set(INCBASE ${CMAKE_CURRENT_SOURCE_DIR})
set(INCSRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

#message(STATUS "fontconfig binary dir: ${CMAKE_CURRENT_BINARY_DIR}")
# Add build directory to include path for generated files
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR}/fc-lang)
add_compile_options(-Wno-excess-initializers)
# Add subdirectories
add_subdirectory(fontconfig)
add_subdirectory(fc-case)
add_subdirectory(fc-lang)
add_subdirectory(src)

if(ENABLE_FONTATIONS)
    message(WARNING "Fontations support requires Rust and is not yet implemented in this CMake build")
endif()

if(ENABLE_NLS)
    add_subdirectory(po)
    add_subdirectory(po-conf)
endif()


# Generate configuration files
include(cmake/ConfigureChecks.cmake)
include(cmake/GenerateConfigFiles.cmake)

# Create meson-config.h.in template if it doesn't exist
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/meson-config.h.in)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config-fixups.h
        ${CMAKE_CURRENT_BINARY_DIR}/meson-config.h.in
        COPYONLY
    )
endif()

# Summary
message(STATUS "")
message(STATUS "fontconfig ${PROJECT_VERSION} configuration summary:")
message(STATUS "  Documentation: ${ENABLE_DOCS}")
message(STATUS "  NLS: ${ENABLE_NLS}")
message(STATUS "  Tests: ${ENABLE_TESTS}")
message(STATUS "  Tools: ${ENABLE_TOOLS}")
message(STATUS "  iconv: ${ENABLE_ICONV}")
message(STATUS "  Fontations support: ${ENABLE_FONTATIONS}")
message(STATUS "")
message(STATUS "Paths:")
message(STATUS "  Cache directory: ${FC_CACHEDIR}")
message(STATUS "  Template directory: ${FC_TEMPLATEDIR}")
message(STATUS "  Base config directory: ${FC_BASECONFIGDIR}")
message(STATUS "  Config directory: ${FC_CONFIGDIR}")
message(STATUS "  XML directory: ${FC_XMLDIR}")
message(STATUS "")