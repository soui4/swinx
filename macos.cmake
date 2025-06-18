

file(GLOB_RECURSE HEADERS  include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/cocoa/*.mm
    )
 
source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

find_package(PkgConfig REQUIRED)
pkg_search_module(CAIRO REQUIRED cairo)
if (NOT CAIRO_FOUND)
    message(FATAL_ERROR "Cairo not found")
endif()

include_directories(${CAIRO_INCLUDE_DIRS})
link_directories(${CAIRO_LIBRARY_DIRS} )

find_package(Freetype REQUIRED)
find_package(PNG REQUIRED)
find_package(Fontconfig REQUIRED)
find_package(Iconv REQUIRED)



find_library(COCOA_LIBRARY Cocoa)
find_library(QUARTZ_LIBRARY QuartzCore)
find_library(IOKit_LIBRARY IOKit)
find_library(Carbon_LIBRARY Carbon)
set(LIBS ${CAIRO_LIBRARIES}  
    Freetype::Freetype 
    PNG::PNG
    Fontconfig::Fontconfig  
    Iconv::Iconv
    ${COCOA_LIBRARY}
    ${QUARTZ_LIBRARY}
    ${IOKit_LIBRARY}
    ${Carbon_LIBRARY}
)
if (NOT ENABLE_SOUI_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
    target_link_libraries(swinx PRIVATE ${LIBS})
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS} ${LIBS})
endif()

target_compile_options(swinx PRIVATE "-fobjc-arc")

target_include_directories(swinx
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/cocoa
)




