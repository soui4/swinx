project(cairo)
cmake_minimum_required(VERSION 3.16)

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

if(NOT TARGET swinx_zlibstatic)
    message(FATAL_ERROR "swinx_zlibstatic target not found. Make sure zlib is built before cairo.")
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
get_target_property(ZLIB_INCLUDE_DIRS swinx_zlibstatic INTERFACE_INCLUDE_DIRECTORIES)

# Include directories
include_directories(${PIXMAN_INCLUDE_DIRS})
include_directories(${FREETYPE_INCLUDE_DIRS})
include_directories(${FONTCONFIG_INCLUDE_DIRS})
include_directories(${PNG_INCLUDE_DIRS})
include_directories(${ZLIB_INCLUDE_DIRS})

# Link libraries - use target names for internal dependencies
list(APPEND CAIRO_LIBS pixman-1 freetype fontconfig png_static swinx_zlibstatic)

if (APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(APPLICATION_SERVICES ApplicationServices)
    if (CORE_FOUNDATION AND APPLICATION_SERVICES)
        list(APPEND CAIRO_LIBS ${CORE_FOUNDATION} ${APPLICATION_SERVICES})
    endif()
endif()
include_directories(src)
add_subdirectory(src)