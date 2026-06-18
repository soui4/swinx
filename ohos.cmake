add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-result)
add_compile_options(-Wno-format)

file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/ohos/*.cpp
)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found. Make sure thirdparty is built first.")
endif()

if (NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()

add_dependencies(swinx cairo fontconfig freetype pixman-1)

set(SWINX_LIBS dl m)

find_library(OHOS_ACE_NAPI ace_napi.z)
find_library(OHOS_ACE_NDK ace_ndk.z)
find_library(OHOS_NATIVE_WINDOW native_window)
find_library(OHOS_HILOG hilog_ndk.z)

foreach(_ohos_lib IN ITEMS OHOS_ACE_NAPI OHOS_ACE_NDK OHOS_NATIVE_WINDOW OHOS_HILOG)
    if(${_ohos_lib})
        list(APPEND SWINX_LIBS ${${_ohos_lib}})
    endif()
endforeach()

target_link_libraries(swinx PRIVATE cairo fontconfig freetype pixman-1 ${SWINX_LIBS})

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_include_directories(swinx
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ohos
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
)
