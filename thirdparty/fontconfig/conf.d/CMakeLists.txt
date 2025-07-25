# Configuration files installation and linking

# List of configuration files
set(CONF_FILES
    05-reset-dirs-sample.conf
    09-autohint-if-no-hinting.conf
    10-autohint.conf
    10-hinting-full.conf
    10-hinting-medium.conf
    10-hinting-none.conf
    10-hinting-slight.conf
    10-no-antialias.conf
    10-scale-bitmap-fonts.conf
    10-sub-pixel-bgr.conf
    10-sub-pixel-none.conf
    10-sub-pixel-rgb.conf
    10-sub-pixel-vbgr.conf
    10-sub-pixel-vrgb.conf
    10-unhinted.conf
    10-yes-antialias.conf
    11-lcdfilter-default.conf
    11-lcdfilter-legacy.conf
    11-lcdfilter-light.conf
    11-lcdfilter-none.conf
    20-unhint-small-vera.conf
    25-unhint-nonlatin.conf
    30-metric-aliases.conf
    40-nonlatin.conf
    45-generic.conf
    45-latin.conf
    48-guessfamily.conf
    48-spacing.conf
    49-sansserif.conf
    50-user.conf
    51-local.conf
    60-generic.conf
    60-latin.conf
    65-fonts-persian.conf
    65-khmer.conf
    65-nonlatin.conf
    69-unifont.conf
    70-no-bitmaps.conf
    70-no-bitmaps-and-emoji.conf
    70-no-bitmaps-except-emoji.conf
    70-yes-bitmaps.conf
    80-delicious.conf
    90-synthetic.conf
)

# Configuration links based on options
set(CONF_LINKS
    10-hinting-${DEFAULT_HINTING}.conf
    10-scale-bitmap-fonts.conf
    10-sub-pixel-${DEFAULT_SUB_PIXEL_RENDERING}.conf
    10-yes-antialias.conf
    11-lcdfilter-default.conf
    20-unhint-small-vera.conf
    30-metric-aliases.conf
    40-nonlatin.conf
    45-generic.conf
    45-latin.conf
    48-spacing.conf
    49-sansserif.conf
    50-user.conf
    51-local.conf
    60-generic.conf
    60-latin.conf
    65-fonts-persian.conf
    65-nonlatin.conf
    69-unifont.conf
    80-delicious.conf
    90-synthetic.conf
)

# Add bitmap configuration based on option
if(BITMAP_CONF STREQUAL "yes")
    list(APPEND CONF_LINKS 70-yes-bitmaps.conf)
elseif(BITMAP_CONF STREQUAL "no")
    list(APPEND CONF_LINKS 70-no-bitmaps-and-emoji.conf)
elseif(BITMAP_CONF STREQUAL "no-except-emoji")
    list(APPEND CONF_LINKS 70-no-bitmaps-except-emoji.conf)
endif()

# Install configuration files to template directory
install(FILES ${CONF_FILES}
    DESTINATION ${FC_TEMPLATEDIR}
    COMPONENT Runtime
)

# Create symbolic links in config directory
install(CODE "
    file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/${FC_CONFIGDIR}\")
    foreach(conf_file ${CONF_LINKS})
        set(source_file \"\${CMAKE_INSTALL_PREFIX}/${FC_TEMPLATEDIR}/\${conf_file}\")
        set(target_file \"\${CMAKE_INSTALL_PREFIX}/${FC_CONFIGDIR}/\${conf_file}\")

        # Remove existing link/file if it exists
        if(EXISTS \"\${target_file}\" OR IS_SYMLINK \"\${target_file}\")
            file(REMOVE \"\${target_file}\")
        endif()

        # Create symbolic link
        execute_process(
            COMMAND \${CMAKE_COMMAND} -E create_symlink
                    \"../conf.avail/\${conf_file}\"
                    \"\${target_file}\"
            RESULT_VARIABLE link_result
        )

        if(NOT link_result EQUAL 0)
            message(WARNING \"Failed to create symbolic link for \${conf_file}\")
        endif()
    endforeach()
" COMPONENT Runtime)

# Generate 35-lang-normalize.conf
# First, get list of orthography files without underscores from fc-lang
file(GLOB ORTH_FILES_GLOB "${CMAKE_CURRENT_SOURCE_DIR}/../fc-lang/*.orth")
set(ORTH_LANGS "")
foreach(orth_file_path ${ORTH_FILES_GLOB})
    get_filename_component(orth_file ${orth_file_path} NAME)
    string(REPLACE ".orth" "" lang ${orth_file})
    if(NOT lang MATCHES "_")
        list(APPEND ORTH_LANGS ${lang})
    endif()
endforeach()

string(REPLACE ";" " " ORTH_LANGS_STR "${ORTH_LANGS}")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/35-lang-normalize.conf
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/write-35-lang-normalize-conf.py
            ${ORTH_LANGS_STR}
            ${CMAKE_CURRENT_BINARY_DIR}/35-lang-normalize.conf
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/write-35-lang-normalize-conf.py
    COMMENT "Generating 35-lang-normalize.conf"
)

add_custom_target(lang_normalize_conf ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/35-lang-normalize.conf
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/35-lang-normalize.conf
    DESTINATION ${FC_TEMPLATEDIR}
    COMPONENT Runtime
)

# Generate README
set(TEMPLATEDIR ${FC_TEMPLATEDIR})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/README.in
    ${CMAKE_CURRENT_BINARY_DIR}/README
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/README
    DESTINATION ${FC_CONFIGDIR}
    COMPONENT Runtime
)