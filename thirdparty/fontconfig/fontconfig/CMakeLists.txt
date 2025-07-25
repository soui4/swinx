# Generate fontconfig.h from template
set(CACHE_VERSION ${CACHEVERSION})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fontconfig.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfig.h
    @ONLY
)

# Install headers
set(FC_HEADERS
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfig.h
    fcfreetype.h
    fcprivate.h
)

install(FILES ${FC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fontconfig
    COMPONENT Development
)