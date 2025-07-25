# Internationalization support for fontconfig configuration files

find_package(Gettext)

if(GETTEXT_FOUND)
    set(GETTEXT_PACKAGE "${PROJECT_NAME}-conf")
    set(GETTEXT_ARGS
        --msgid-bugs-address=${PACKAGE_BUGREPORT}
        --directory=${CMAKE_SOURCE_DIR}
    )

    # Get list of po files
    file(GLOB PO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.po")

    # Create translations
    foreach(po_file ${PO_FILES})
        get_filename_component(lang ${po_file} NAME_WE)
        gettext_process_po_files(${lang} ALL PO_FILES ${po_file})

        # Install mo files
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${lang}.gmo
            DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES
            RENAME ${GETTEXT_PACKAGE}.mo
            COMPONENT Runtime
        )
    endforeach()

    # Create pot file
    add_custom_target(update-po-conf
        COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE}
                --default-domain=${GETTEXT_PACKAGE}
                --add-comments=TRANSLATORS:
                --keyword=_
                --keyword=N_
                --from-code=UTF-8
                ${GETTEXT_ARGS}
                --output=${CMAKE_CURRENT_SOURCE_DIR}/${GETTEXT_PACKAGE}.pot
                ${CMAKE_SOURCE_DIR}/conf.d/*.conf
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Updating ${GETTEXT_PACKAGE}.pot"
    )
endif()