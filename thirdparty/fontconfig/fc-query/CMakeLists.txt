# fc-query tool

add_executable(fc-query fc-query.c)

target_include_directories(fc-query PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
    ${FREETYPE2_INCLUDE_DIRS}
)

target_link_libraries(fc-query PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-query PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-query PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-query fccase_h fclang_h)

install(TARGETS fc-query
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
