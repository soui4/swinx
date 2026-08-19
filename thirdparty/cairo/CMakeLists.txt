project(cairo)
cmake_minimum_required(VERSION 3.16)

add_compile_options(-Wno-attributes)
# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Use internal dependencies instead of system packages
# Ensure dependencies are built first
if(NOT TARGET pixman-1)
    message(FATAL_ERROR "pixman-1 target not found. Make sure pixman is built before cairo.")
endif()

if(NOT TARGET freetype)
    message(FATAL_ERROR "freetype target not found. Make sure freetype is built before cairo.")
endif()

if(NOT TARGET fontconfig)
    message(FATAL_ERROR "fontconfig target not found. Make sure fontconfig is built before cairo.")
endif()

add_compile_options(-Wno-enum-conversion)
# Use internal PNG and ZLIB libraries instead of system packages
# Ensure dependencies are built first
if(NOT TARGET png_static)
    message(FATAL_ERROR "png_static target not found. Make sure libpng is built before cairo.")
endif()

if(NOT TARGET zlib_static)
    message(FATAL_ERROR "zlib_static target not found. Make sure zlib is built before cairo.")
endif()

# Set PNG variables for cairo configuration
set(PNG_FOUND TRUE)
set(PNG_LIBRARY png_static)
set(PNG_LIBRARIES png_static)
get_target_property(PNG_INCLUDE_DIR png_static INTERFACE_INCLUDE_DIRECTORIES)
if(NOT PNG_INCLUDE_DIR)
    set(PNG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libpng")
endif()
find_package(Threads)

# Quartz backend option (mirrors meson option('quartz'))
# Meson only enables quartz on darwin (macOS); iOS lacks the ApplicationServices
# umbrella framework, so quartz stays disabled there — matching meson behavior.
# When enabled on macOS, checks for CoreFoundation and ApplicationServices
# frameworks (quartz_deps). Surface/image backends are enabled when the deps
# are found; the Quartz font backend is enabled only when CTFontDrawGlyphs is
# available, matching meson's behavior.
option(ENABLE_QUARTZ "Enable Quartz surface backend" ON)

if(ENABLE_QUARTZ AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(APPLICATION_SERVICES ApplicationServices)
    if(CORE_FOUNDATION AND APPLICATION_SERVICES)
        set(CAIRO_QUARTZ_DEPS_FOUND TRUE)
        list(APPEND CAIRO_LIBS ${CORE_FOUNDATION} ${APPLICATION_SERVICES})
        include(CheckCSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES ${CORE_FOUNDATION} ${APPLICATION_SERVICES})
        check_c_source_compiles("
#include <ApplicationServices/ApplicationServices.h>
int main(void) { CTFontDrawGlyphs(0, 0, 0, 0, 0); return 0; }
" HAVE_CTFontDrawGlyphs)
        unset(CMAKE_REQUIRED_LIBRARIES)
    else()
        set(CAIRO_QUARTZ_DEPS_FOUND FALSE)
        message(STATUS "Quartz enabled but CoreFoundation/ApplicationServices not found; disabling Quartz.")
    endif()
endif()

include(Configure_config.cmake)
include(Configure_features.cmake)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(CAIRO_HAS_PTHREAD)
	list(APPEND CAIRO_LIBS -lpthread)
endif()

if (WIN32)
    set(CAIRO_LIBS gdi32 msimg32 user32 winmm)
endif()

# Configure include directories for internal dependencies
# Get include directories from targets
get_target_property(PIXMAN_INCLUDE_DIRS pixman-1 INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(FONTCONFIG_INCLUDE_DIRS fontconfig INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(PNG_INCLUDE_DIRS png_static INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(ZLIB_INCLUDE_DIRS zlib_static INTERFACE_INCLUDE_DIRECTORIES)

# Include directories
include_directories(${PIXMAN_INCLUDE_DIRS})
include_directories(${FREETYPE_INCLUDE_DIRS})
include_directories(${FONTCONFIG_INCLUDE_DIRS})
include_directories(${PNG_INCLUDE_DIRS})
include_directories(${ZLIB_INCLUDE_DIRS})

# Link libraries - use target names for internal dependencies
list(APPEND CAIRO_LIBS pixman-1 freetype fontconfig png_static zlib_static)

if (APPLE AND CAIRO_QUARTZ_DEPS_FOUND)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(APPLICATION_SERVICES ApplicationServices)
    if (CORE_FOUNDATION AND APPLICATION_SERVICES)
        list(APPEND CAIRO_LIBS ${CORE_FOUNDATION} ${APPLICATION_SERVICES})
    endif()
endif()
include_directories(src)
add_subdirectory(src)


