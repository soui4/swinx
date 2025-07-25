# fc-conflist tool

add_executable(fc-conflist fc-conflist.c)

target_include_directories(fc-conflist PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-conflist PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-conflist PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-conflist PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-conflist fccase_h fclang_h)

install(TARGETS fc-conflist
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
