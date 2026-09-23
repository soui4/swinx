cmake_minimum_required(VERSION 3.16)

project(swinx)
set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wno-deprecated-declarations)
add_compile_options(-Wno-unused-result)

add_subdirectory(thirdparty)

if(SOUI_ENABLE_ACC)
    add_definitions(-DSOUI_ENABLE_ACC)
endif()

# Explicit platform override. Empty => auto-select by CMAKE_SYSTEM_NAME (unchanged
# Linux/macOS/OHOS/Android/iOS behaviour). Set to "freertos" to build the swinx
# FreeRTOS port (currently a Linux-host compile test based on linux.cmake + uSTL).
set(SOUI_PLATFORM "" CACHE STRING "swinx platform override: linux|freertos (empty = auto)")

if(SOUI_PLATFORM STREQUAL "freertos")
    include(freeRTOS.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "OHOS|OpenHarmony|HarmonyOS|Android")
include(mobile.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES "iOS")
include(ios.cmake)
elseif(CMAKE_SYSTEM_NAME MATCHES Darwin)
include(macos.cmake)
else()
include(linux.cmake)
endif()

if(SOUI_ENABLE_CORE_LIB)
	target_compile_options(swinx PRIVATE -fPIC)
endif()

# Force-include the swinx_stl shim into every swinx C++ TU so the
# substituted swinx_stl:: types resolve in both SOUI_USE_USTL ON and OFF
# modes. Restricted to CXX so the .c sources compiled into this target are
# untouched. swinx is built with GCC/Clang only, so -include is valid.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(swinx PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/swinx_stl.h>)
endif()

# In uSTL mode swinx uses ustl containers, so it must link libustl.
if(SOUI_USE_USTL)
    target_link_libraries(swinx ${SOUI_USTL_LIB})
endif()

# Stricter warnings for the swinx module itself (PRIVATE scope keeps
# thirdparty unaffected). GCC/Clang only; swinx is not built with MSVC.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(swinx PRIVATE -Wall -Wextra)
endif()
set_target_properties (swinx PROPERTIES
    FOLDER swinx
)

set(SOUI_SWINX swinx CACHE INTERNAL "swinx_lib")

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

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" DESTINATION include/swinx)



