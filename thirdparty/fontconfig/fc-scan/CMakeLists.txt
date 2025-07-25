# fc-scan tool

add_executable(fc-scan fc-scan.c)

target_include_directories(fc-scan PRIVATE
    ${INCBASE}
    ${INCSRC}
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
    ${FREETYPE2_INCLUDE_DIRS}
)

target_link_libraries(fc-scan PRIVATE fontconfig_internal)

if(Intl_FOUND)
    target_link_libraries(fc-scan PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fc-scan PRIVATE ${Intl_INCLUDE_DIRS})
endif()

# Add dependencies on generated files
add_dependencies(fc-scan fccase_h fclang_h)

install(TARGETS fc-scan
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Tools
)
