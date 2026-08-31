# ============================================================
# swinx / mobile.cmake — 统一 Android 与 OHOS 的平台适配器构建。
# 源码目录为 src/platform/mobile/，通过 ANDROID / CMAKE_SYSTEM_NAME STREQUAL "OHOS"
# 区分平台特有配置，最终生成同一个 libswinx（shared/static 取决于 SOUI_ENABLE_CORE_LIB）。
# ============================================================

add_compile_options(-Wno-format-truncation -Wno-attributes
                    -Wno-missing-field-initializers
                    -Wno-deprecated-declarations
                    -Wno-unused-result -Wno-format)
add_definitions(-DENABLE_VIRTUAL_HWND)

if(CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS")
    add_definitions(-D__OHOS__)
endif()

get_target_property(cairo_inc cairo INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(png_inc png_static INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${cairo_inc} ${png_inc})

file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)
file(GLOB SRCS
    src/*.cpp
    src/cmnctl32/*.cpp
    src/cmnctl32/*.c
    src/platform/mobile/*.cpp
    src/gdi/cairo/*.cpp
)

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})

if(NOT TARGET cairo)
    message(FATAL_ERROR "cairo target not found.")
endif()

if(NOT SOUI_ENABLE_CORE_LIB)
    add_library(swinx SHARED ${SRCS} ${HEADERS})
else()
    add_library(swinx STATIC ${SRCS} ${HEADERS})
endif()

add_dependencies(swinx cairo fontconfig freetype pixman-1)

# ---------------- 平台特有链接库 ----------------
set(SWINX_LIBS dl m)
if(ANDROID)
    list(APPEND SWINX_LIBS stdc++ log android c++_shared)
elseif(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_library(OHOS_ACE_NAPI ace_napi.z)
    find_library(OHOS_ACE_NDK  ace_ndk.z)
    find_library(OHOS_NATIVE_WINDOW native_window)
    find_library(OHOS_HILOG     hilog_ndk.z)
    foreach(_ohos_lib IN ITEMS OHOS_ACE_NAPI OHOS_ACE_NDK OHOS_NATIVE_WINDOW OHOS_HILOG)
        if(${_ohos_lib})
            list(APPEND SWINX_LIBS ${${_ohos_lib}})
        endif()
    endforeach()
endif()

target_link_libraries(swinx
    cairo fontconfig freetype pixman-1
    ${SWINX_LIBS}
)

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} ${SWINX_LIBS} CACHE INTERNAL "swinx_dep_libs")
endif()

target_include_directories(swinx
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mobile
)

# OHOS 需要额外的 thirdparty 子目录头文件搜索路径
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    target_include_directories(swinx
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sysinclude
    )
endif()
