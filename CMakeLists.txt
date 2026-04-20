cmake_minimum_required(VERSION 3.16)

project(swinx)
set(CMAKE_CXX_STANDARD 11)

add_compile_options(-Wno-deprecated-declarations)
 
add_subdirectory(thirdparty)
if(CMAKE_SYSTEM_NAME MATCHES Darwin)
include(macos.cmake)
else()
include(linux.cmake)
endif()

if(SOUI_ENABLE_CORE_LIB)
target_compile_options(swinx PRIVATE -fPIC)
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



