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

target_include_directories(xcb-imdkit
                           PUBLIC ..
                           include
                           uthash
                           )




