# fc-pattern tool

add_executable(fc-pattern fc-pattern.c)

target_include_directories(fc-pattern PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-pattern PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-pattern PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-pattern PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-pattern fccase_h fclang_h)

install(TARGETS fc-pattern
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
