
add_compile_options(-Wno-extern-c-compat)
add_compile_options(-Wno-unknown-warning-option)
add_compile_options(-Wno-constant-conversion)
add_compile_options(-Wno-comment)
file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/cocoa/*.mm
    )
 
source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

# Use internal compiled libraries instead of system packages
# Ensure our thirdparty libraries are available
if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found. Make sure thirdparty is built first.")
endif()

# Still need Iconv from system
find_package(Iconv REQUIRED)

# Manually add include directories for internal libraries
# This ensures headers can be found during compilation
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cairo/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/thirdparty/cairo/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pixman/pixman)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/freetype/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fontconfig)


find_library(COCOA_LIBRARY Cocoa)
find_library(QUARTZ_LIBRARY QuartzCore)
find_library(IOKit_LIBRARY IOKit)
find_library(Carbon_LIBRARY Carbon)

# Use internal compiled libraries
set(LIBS
    cairo              # Our internal cairo target
    Iconv::Iconv       # System Iconv library
    ${COCOA_LIBRARY}
    ${QUARTZ_LIBRARY}
    ${IOKit_LIBRARY}
    ${Carbon_LIBRARY}
)
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    target_link_libraries(swinx PRIVATE ${LIBS})

    # 确保导出所有符号，包括fontconfig和freetype的符号
    set_target_properties(swinx PROPERTIES
        LINK_FLAGS "-Wl,-all_load"
        MACOSX_RPATH TRUE
    )
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS} ${LIBS})
endif()
# Add dependencies to ensure proper build order for all internal libraries
add_dependencies(swinx cairo fontconfig freetype pixman-1)
target_compile_options(swinx PRIVATE "-fobjc-arc")

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/cocoa
)




