# fc-validate tool

add_executable(fc-validate fc-validate.c)

target_include_directories(fc-validate PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
    ${FREETYPE2_INCLUDE_DIRS}
)

target_link_libraries(fc-validate PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-validate PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-validate PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-validate fccase_h fclang_h)

install(TARGETS fc-validate
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
