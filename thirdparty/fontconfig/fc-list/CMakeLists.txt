# fc-list tool

add_executable(fc-list fc-list.c)

target_include_directories(fc-list PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-list PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-list PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-list PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-list fccase_h fclang_h)

install(TARGETS fc-list
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
