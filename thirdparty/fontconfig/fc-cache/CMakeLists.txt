# fc-cache tool

add_executable(fc-cache fc-cache.c)

target_include_directories(fc-cache PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-cache PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-cache PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-cache PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-cache fccase_h fclang_h)

install(TARGETS fc-cache
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)

# Run fc-cache on install if enabled and not cross-compiling
if(ENABLE_CACHE_BUILD AND NOT CMAKE_CROSSCOMPILING)
    install(CODE "
        message(STATUS \"Running fc-cache to build font cache...\")
        execute_process(
            COMMAND \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/fc-cache -s -f -v
            RESULT_VARIABLE FC_CACHE_RESULT
        )
        if(NOT FC_CACHE_RESULT EQUAL 0)
            message(WARNING \"fc-cache failed with result: \${FC_CACHE_RESULT}\")
        endif()
    " COMPONENT Tools)
endif()