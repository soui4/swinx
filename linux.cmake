add_compile_options(-Wno-format-truncation)
add_compile_options(-Wno-attributes)
find_package(PkgConfig REQUIRED)

# Use internal compiled libraries instead of system packages
# Ensure our thirdparty libraries are available
if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found. Make sure thirdparty is built first.")
endif()


add_subdirectory(thirdparty/xkbcommon)
add_subdirectory(thirdparty/libxcb)
add_subdirectory(thirdparty/xcb-imdkit)
add_subdirectory(thirdparty/dbus-1.14.10)
 

get_target_property(CAIRO_INCLUDE_DIRS cairo INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${CAIRO_INCLUDE_DIRS})
get_target_property(DBUS_INCLUDE_DIRS dbus-1 INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${DBUS_INCLUDE_DIRS})

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
    )

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})
 
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    target_link_libraries(swinx
        dl
        xcb                # System XCB library
        cairo              # Our internal cairo target
        xkbcommon          # Our internal xkbcommon target
        xcb-imdkit         # Our internal xcb-imdkit target
        uuid               # System UUID library
        dbus-1             # Our internal D-Bus library
    )
    # Add dependencies to ensure proper build order for all internal libraries
    add_dependencies(swinx cairo fontconfig freetype pixman-1 xcb-imdkit xkbcommon dbus-1)

    # 确保导出所有符号，包括fontconfig和freetype的符号
    set_target_properties(swinx PROPERTIES
        LINK_FLAGS "-Wl,--export-dynamic"
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
    add_dependencies(swinx cairo fontconfig freetype pixman-1 xcb-imdkit xkbcommon dbus-1)
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




