cmake_minimum_required(VERSION 3.16)
project(xcb-imdkit VERSION 1.0.9)

file(GLOB_RECURSE HEADEERS
    include/*.h 
    )

file(GLOB_RECURSE SRCS
    src/*.c 
    src/*.h
    )

add_library(xcb-imdkit STATIC ${SRCS} ${HEADEERS})
target_compile_options(xcb-imdkit PRIVATE -fPIC)
if(SOUI_ENABLE_CORE_LIB)
    set(CORE_LIBS ${CORE_LIBS} xcb-imdkit CACHE INTERNAL "core_lib")
endif()

target_include_directories(xcb-imdkit
                           PUBLIC ..
                           include
                           uthash
                           )

install(TARGETS xcb-imdkit
    CONFIGURATIONS Release
    RUNTIME DESTINATION bin/Release
    LIBRARY DESTINATION lib/Release
    ARCHIVE DESTINATION lib/Release
)

install(TARGETS xcb-imdkit
    CONFIGURATIONS Debug
    RUNTIME DESTINATION bin/Debug
    LIBRARY DESTINATION lib/Debug
    ARCHIVE DESTINATION lib/Debug
)



