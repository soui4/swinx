# Install gettext ITS files

set(GETTEXT_FILES
    fontconfig.its
    fontconfig.loc
)

install(FILES ${GETTEXT_FILES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gettext/its
    COMPONENT Development
)