# swinx iOS configuration
# This file is included from the main CMakeLists.txt for iOS builds

# 最低部署版本约束（取各项要求的最高值）：
#   CGShadingCreateAxial 等渐变 API：iOS 2.0+
#   -fobjc-arc（项目使用 ARC）：iOS 4.0+
#   libc++（现代 Xcode 不再提供 libstdc++ 头文件）：iOS 5.0+
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "5.0")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "5.0")
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
    src/platform/ios/*.mm
    src/gdi/apple/*.cpp
    )


source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

# Manually add include directories for internal libraries
get_target_property(PNG_INCLUDE_DIRS png_static INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${PNG_INCLUDE_DIRS})

# iOS frameworks
# 用 -framework 标志而非 find_library：find_library 会在 CMake 配置时缓存
# 绝对路径（配置用 iphoneos SDK 时会缓存设备 SDK 的 .framework），模拟器构建时
# 该路径缺 x86_64/arm64-simulator 切片导致 undefined symbol。
# -framework 由链接器在当前活跃 SDK 中查找，设备/模拟器通用。
set(SWINX_LIBS
    "-liconv"
    "-framework Foundation"
    "-framework UIKit"
    "-framework CoreFoundation"
    "-framework CoreGraphics"
    "-framework CoreText"
    "-framework QuartzCore"
    "-framework AudioToolbox"
    "-framework ImageIO"
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

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_compile_options(swinx PRIVATE "-fobjc-arc")

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios
)
