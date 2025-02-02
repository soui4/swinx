cmake_minimum_required(VERSION 3.1)

project(swinx)

#add gtk demo
find_package(PkgConfig REQUIRED)


add_subdirectory(thirdparty/xkbcommon)
add_subdirectory(thirdparty/libxcb)

add_definitions(-DPLATFORM_EXPORTS)
add_compile_options(-Wno-format-truncation)

file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB_RECURSE SRCS
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

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})
 
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    target_link_libraries(
        swinx
        libxcb  cairo uuid xkbcommon
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xcb
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xkbcommon
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



