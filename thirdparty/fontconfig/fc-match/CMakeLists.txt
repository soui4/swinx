# fc-match tool

add_executable(fc-match fc-match.c)

target_include_directories(fc-match PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-match PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-match PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-match PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-match fccase_h fclang_h)

install(TARGETS fc-match
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
