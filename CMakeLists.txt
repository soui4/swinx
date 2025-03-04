cmake_minimum_required(VERSION 3.1)

project(swinx)

option(USING_GTK3_DLG "Using GTK3 to show common dialog" ON)
set(USING_GTK3_DLG OFF)
if(USING_GTK3_DLG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)  # 检查 GTK 3.0 版本
# 如果找到 GTK，则设置相关变量
if(GTK_FOUND)
    message(STATUS "GTK version ${GTK_VERSION} found. Enabling GTK support. ")
    add_definitions(-DENABLE_GTK) 
    # 设置包含目录和链接目录
    include_directories(${GTK3_INCLUDE_DIRS})
    link_directories(${GTK3_LIBRARY_DIRS})
else()
    message(STATUS "GTK not found. Disabling GTK support.")
endif(GTK_FOUND)
else()
set(GTK_FOUND OFF)
endif(USING_GTK3_DLG)

add_subdirectory(thirdparty/xkbcommon)
add_subdirectory(thirdparty/libxcb)
add_subdirectory(thirdparty/xcb-imdkit)
add_definitions(-DPLATFORM_EXPORTS)
add_compile_options(-Wno-format-truncation)

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
    )
file(GLOB GTK_DEPS src/gtk/*.*)
if(GTK_FOUND)
set(SRCS ${SRCS} ${GTK_DEPS})
endif(GTK_FOUND)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})
 
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    
    if(GTK_FOUND)
        target_link_libraries(swinx 
        cairo uuid xkbcommon xcb-imdkit ${GTK3_LIBRARIES})
    else()
    target_link_libraries(swinx
        libxcb  cairo uuid xkbcommon xcb-imdkit
    )
    endif()
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
    add_dependencies(swinx xcb-imdkit)
endif()

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xkbcommon
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb-imdkit/include
)

set_target_properties (swinx PROPERTIES
    FOLDER swinx
)

install(TARGETS swinx
    CONFIGURATIONS Release
    RUNTIME DESTINATION bin/Release
    LIBRARY DESTINATION lib/Release
    ARCHIVE DESTINATION lib/Release
)

install(TARGETS swinx
    CONFIGURATIONS Debug
    RUNTIME DESTINATION bin/Debug
    LIBRARY DESTINATION lib/Debug
    ARCHIVE DESTINATION lib/Debug
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION swinx)



