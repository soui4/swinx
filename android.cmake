# swinx Android configuration
# This file is included from the main CMakeLists.txt for Android builds

add_compile_options(-Wno-format-truncation)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-missing-field-initializers)

# Enable virtual hwnd
add_definitions(-DENABLE_VIRTUAL_HWND)

get_target_property(cairo_inc cairo INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(png_inc png_static INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${cairo_inc} ${png_inc})
# Android-specific includes
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/android/*.cpp
    src/gdi/cairo/*.cpp
)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found. Make sure thirdparty is built first.")
endif()

if (NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()

# Add dependencies to ensure proper build order for all internal libraries
add_dependencies(swinx cairo fontconfig freetype pixman-1)

# Android-specific dependencies (no cairo/fontconfig/freetype on Android)
set(SWINX_LIBS dl m stdc++ log android c++_shared)
if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_link_libraries(swinx
    cairo fontconfig freetype pixman-1 
    ${SWINX_LIBS}
)

target_include_directories(swinx
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android
)