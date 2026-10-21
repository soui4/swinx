
# 最低部署版本约束（取各项要求的最高值）：
#   CGShadingCreateAxial 等渐变 API：macOS 10.2+
#   -fobjc-arc（项目使用 ARC）：macOS 10.6+
#   libc++（现代 Xcode 不再提供 libstdc++ 头文件）：macOS 10.7+
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "10.13")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
endif()

add_compile_options(-Wno-extern-c-compat)
add_compile_options(-Wno-unknown-warning-option)
add_compile_options(-Wno-constant-conversion)
add_compile_options(-Wno-comment)
file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/posix/*.cpp
    src/platform/cocoa/*.mm
    src/gdi/apple/*.cpp
    )

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

get_target_property(PNG_INCLUDE_DIRS png_static INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${PNG_INCLUDE_DIRS})

# Still need Iconv from system
find_package(Iconv REQUIRED)

find_library(COCOA_LIBRARY Cocoa)
find_library(QUARTZ_LIBRARY QuartzCore)
find_library(IOKit_LIBRARY IOKit)
find_library(Carbon_LIBRARY Carbon)
find_library(Audio_LIBRARY AudioToolbox)
find_library(CoreFoundation_LIBRARY CoreFoundation)
find_library(ImageIO_LIBRARY ImageIO)
find_library(OPENGL_LIBRARY OpenGL) # wgl.h 兼容层的 NSOpenGLContext 落地（SGLBridge.mm）
# Use internal compiled libraries
set(SWINX_LIBS
    Iconv::Iconv       # System Iconv library
    ${COCOA_LIBRARY}
    ${QUARTZ_LIBRARY}
    ${IOKit_LIBRARY}
    ${Carbon_LIBRARY}
    ${Audio_LIBRARY}
    ${CoreFoundation_LIBRARY}
    ${ImageIO_LIBRARY}
    ${OPENGL_LIBRARY}
    m
    stdc++
    )

if (NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    # 确保导出所有符号，包括fontconfig和freetype的符号
    set_target_properties(swinx PROPERTIES
        LINK_FLAGS "-Wl,-all_load"
        MACOSX_RPATH TRUE
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()
add_dependencies(swinx fontconfig freetype pixman-1)
target_link_libraries(swinx PRIVATE ${SWINX_LIBS} fontconfig freetype pixman-1)

# Add dependencies to ensure proper build order for all internal libraries

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()


target_compile_options(swinx PRIVATE "-fobjc-arc")
target_compile_options(swinx PRIVATE -stdlib=libc++)
target_link_options(swinx PRIVATE -stdlib=libc++)

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/cocoa
)




