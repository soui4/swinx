
# OUR SOURCES
set(xcb_sources
xkb.c
shape.c
randr.c
)

source_group("Source Files" FILES ${xcb_sources})

add_library(libxcb STATIC ${xcb_sources} )

if(SOUI_ENABLE_CORE_LIB)
    set(SWINX_DEP_LIBS ${SWINX_DEP_LIBS} libxcb CACHE INTERNAL "swinx_dep_libs")
endif()
target_include_directories(libxcb PRIVATE .)
target_include_directories(libxcb PRIVATE ..)
target_include_directories(libxcb PRIVATE ../xcb)
target_include_directories(libxcb PRIVATE ../sysinclude)
set_target_properties(libxcb PROPERTIES POSITION_INDEPENDENT_CODE ON)

install(TARGETS libxcb
    CONFIGURATIONS Release
    RUNTIME DESTINATION bin/Release
    LIBRARY DESTINATION lib/Release
    ARCHIVE DESTINATION lib/Release
)

install(TARGETS libxcb
    CONFIGURATIONS Debug
    RUNTIME DESTINATION bin/Debug
    LIBRARY DESTINATION lib/Debug
    ARCHIVE DESTINATION lib/Debug
)

set_target_properties (libxcb PROPERTIES
    FOLDER swinx
)

