# fc-cat tool

add_executable(fc-cat fc-cat.c)

target_include_directories(fc-cat PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

target_link_libraries(fc-cat PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-cat PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-cat PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-cat fccase_h fclang_h)

install(TARGETS fc-cat
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)