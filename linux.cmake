find_package(PkgConfig REQUIRED)

pkg_search_module(CAIRO REQUIRED cairo)
if (NOT CAIRO_FOUND)
    message(FATAL_ERROR "Cairo not found")
endif()

include_directories(${CAIRO_INCLUDE_DIRS})
link_directories(${CAIRO_LIBRARY_DIRS} )

option(USING_GTK3_DLG "Using GTK3 to show common dialog" ON)

#set(USING_GTK3_DLG OFF)
if(USING_GTK3_DLG)

pkg_check_modules(GTK3 QUIET gtk+-3.0)  # 检查 GTK 3.0 版本
# 如果找到 GTK，则设置相关变量
if(GTK3_FOUND)
    message(STATUS "GTK version ${GTK3_VERSION} found. Enabling GTK support.  ")
    add_definitions(-DENABLE_GTK) 
    # 设置包含目录和链接目录
    include_directories(${GTK3_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS})
else()
    message(STATUS "GTK not found. Disabling GTK support.")
endif(GTK3_FOUND)
else()
set(GTK3_FOUND OFF)
endif(USING_GTK3_DLG)

add_subdirectory(thirdparty/xkbcommon)
add_subdirectory(thirdparty/libxcb)
add_subdirectory(thirdparty/xcb-imdkit)

file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    thirdparty/xcb-util-image/*.c 
    thirdparty/xcb-util-renderutil/*.c  
    thirdparty/xcb-util-keysyms/*.c
    thirdparty/xcb-util-wm/*.c 
    thirdparty/xcb-util/*.c 
    thirdparty/libxcb/*.c 
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/linux/*.cpp
    src/platform/linux/gtk/*.cpp
    )

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})
 
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    target_link_libraries(swinx
        xcb  cairo  xkbcommon xcb-imdkit freetype fontconfig png uuid
    )
    if(GTK3_FOUND)
        target_link_libraries(swinx ${GTK3_LIBRARIES})
    endif()
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
    add_dependencies(swinx xcb-imdkit)
endif()

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xkbcommon
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb-imdkit/include
)




