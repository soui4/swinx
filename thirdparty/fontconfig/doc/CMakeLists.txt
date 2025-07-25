# Documentation build configuration

set(DOC_TARGETS "")

# Find documentation tools
find_program(DOCBOOK2MAN_EXECUTABLE docbook2man)
find_program(DOCBOOK2TXT_EXECUTABLE docbook2txt)
find_program(DOCBOOK2PDF_EXECUTABLE docbook2pdf)
find_program(DOCBOOK2HTML_EXECUTABLE docbook2html)

# Function documentation files
set(DOC_FUNCS_FNCS
    fcatomic fcblanks fccache fccharset fcconfig fcconstant fcdircache
    fcfile fcfontset fcfontations fcformat fcfreetype fcinit fclangset
    fcmatrix fcobjectset fcobjecttype fcpattern fcrange fcstring
    fcstrset fcvalue fcweight
)

# Generate configuration files
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cache-version.sgml.in
    ${CMAKE_CURRENT_BINARY_DIR}/cache-version.sgml
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.sgml.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.sgml
    @ONLY
)

set(BASECONFIGDIR ${FC_CONFIGDIR})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/confdir.sgml.in
    ${CMAKE_CURRENT_BINARY_DIR}/confdir.sgml
    @ONLY
)

# Copy SGML files to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fontconfig-devel.sgml
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fontconfig-user.sgml
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
    COPYONLY
)

# Generate function SGML files
set(FUNCS_SGML "")
foreach(func ${DOC_FUNCS_FNCS})
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${func}.sgml
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/edit-sgml.py
                ${CMAKE_CURRENT_SOURCE_DIR}/func.sgml
                ${CMAKE_CURRENT_SOURCE_DIR}/${func}.fncs
                ${CMAKE_CURRENT_BINARY_DIR}/${func}.sgml
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/func.sgml
                ${CMAKE_CURRENT_SOURCE_DIR}/${func}.fncs
                ${CMAKE_CURRENT_SOURCE_DIR}/edit-sgml.py
        COMMENT "Generating ${func}.sgml"
    )
    list(APPEND FUNCS_SGML ${CMAKE_CURRENT_BINARY_DIR}/${func}.sgml)
endforeach()

# Extract man page list
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract-man-list.py
            ${CMAKE_CURRENT_SOURCE_DIR}/fcatomic.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcblanks.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fccache.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fccharset.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcconfig.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcconstant.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcdircache.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcfile.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcfontset.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcfontations.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcformat.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcfreetype.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcinit.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fclangset.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcmatrix.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcobjectset.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcobjecttype.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcpattern.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcrange.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcstring.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcstrset.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcvalue.fncs
            ${CMAKE_CURRENT_SOURCE_DIR}/fcweight.fncs
    OUTPUT_VARIABLE MAN_LIST_OUTPUT
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

string(REPLACE "\n" ";" MAN_LIST ${MAN_LIST_OUTPUT})
set(MAN_PAGES "")
foreach(man ${MAN_LIST})
    if(man)
        list(APPEND MAN_PAGES ${man}.3)
    endif()
endforeach()

# Man page generation
if(DOCBOOK2MAN_EXECUTABLE)
    list(APPEND DOC_TARGETS "man")

    add_custom_command(
        OUTPUT ${MAN_PAGES}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2MAN_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml ${FUNCS_SGML}
        COMMENT "Generating man pages"
    )

    add_custom_target(devel-man ALL DEPENDS ${MAN_PAGES})

    install(FILES ${MAN_PAGES}
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man3
        COMPONENT Documentation
    )

    # fonts-conf(5) man page
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fonts-conf.5
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2MAN_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
        COMMENT "Generating fonts-conf.5 man page"
    )

    add_custom_target(fonts-conf-5-man-page ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fonts-conf.5
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fonts-conf.5
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man5
        COMPONENT Documentation
    )

    # Tool man pages
    set(TOOLS_MAN_PAGES fc-cache fc-cat fc-conflist fc-list fc-match fc-pattern fc-query fc-scan fc-validate)
    foreach(tool ${TOOLS_MAN_PAGES})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${tool}.1
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                    ${DOCBOOK2MAN_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../${tool}/${tool}.sgml
                    --output ${CMAKE_CURRENT_BINARY_DIR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../${tool}/${tool}.sgml
            COMMENT "Generating ${tool}.1 man page"
        )

        add_custom_target(${tool}-man-page ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${tool}.1
        )

        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${tool}.1
            DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
            COMPONENT Documentation
        )
    endforeach()
endif()

# PDF generation
if(DOCBOOK2PDF_EXECUTABLE)
    list(APPEND DOC_TARGETS "PDF")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.pdf
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2PDF_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-devel.pdf"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.pdf
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2PDF_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-user.pdf"
    )

    add_custom_target(devel-pdf ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.pdf
    )

    add_custom_target(user-pdf ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.pdf
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.pdf
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.pdf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fontconfig
        COMPONENT Documentation
    )
endif()

# Text generation
if(DOCBOOK2TXT_EXECUTABLE)
    list(APPEND DOC_TARGETS "Text")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.txt
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2TXT_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-devel.txt"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.txt
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2TXT_EXECUTABLE}
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-user.txt"
    )

    add_custom_target(devel-txt ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.txt
    )

    add_custom_target(user-txt ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.txt
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.txt
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fontconfig
        COMPONENT Documentation
    )
endif()

# HTML generation
if(DOCBOOK2HTML_EXECUTABLE)
    list(APPEND DOC_TARGETS "HTML")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.html
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2HTML_EXECUTABLE} --nochunks
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-devel.html"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.html
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run-quiet.py
                ${DOCBOOK2HTML_EXECUTABLE} --nochunks
                ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml
                --output ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.sgml ${FUNCS_SGML}
        COMMENT "Generating fontconfig-user.html"
    )

    add_custom_target(devel-html ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.html
    )

    add_custom_target(user-html ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.html
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-devel.html
        ${CMAKE_CURRENT_BINARY_DIR}/fontconfig-user.html
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fontconfig
        COMPONENT Documentation
    )
endif()

# Documentation tests (Unix only)
if(NOT WIN32)
    set(DOC_TESTS check-missing-doc.py)

    foreach(test_script ${DOC_TESTS})
        add_test(NAME ${test_script}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${test_script}
                    ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endforeach()
endif()

# Set global variable for summary
set(DOC_TARGETS ${DOC_TARGETS} PARENT_SCOPE)