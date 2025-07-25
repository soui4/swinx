# Source library build configuration

# Windows export arguments
set(WIN_EXPORT_ARGS "")
if(WIN32)
    set(WIN_EXPORT_ARGS "-DFcPublic=__declspec(dllexport)" "-DDLL_EXPORT")
endif()

# Copy fcstdint.h to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/fcstdint.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/fcstdint.h
    COPYONLY
)

# Pattern library sources (to break dependency cycle)
set(PATTERN_SOURCES
    fcpat.c
    ${CMAKE_CURRENT_BINARY_DIR}/fcstdint.h
)

# Create pattern library as OBJECT library
add_library(patternlib_internal OBJECT ${PATTERN_SOURCES})
target_compile_options(patternlib_internal PRIVATE -fPIC)

target_include_directories(patternlib_internal PRIVATE
    ${INCBASE}
    ${CMAKE_CURRENT_BINARY_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}
)
# Use internal freetype target instead of system freetype
if(TARGET freetype)
    target_link_libraries(patternlib_internal PRIVATE freetype ${XML_LIBRARIES})
    get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(patternlib_internal PRIVATE ${FREETYPE_INCLUDE_DIRS} ${XML_INCLUDE_DIRS})
else()
    target_link_libraries(patternlib_internal PRIVATE ${FREETYPE2_LIBRARIES} ${XML_LIBRARIES})
    target_include_directories(patternlib_internal PRIVATE ${FREETYPE2_INCLUDE_DIRS} ${XML_INCLUDE_DIRS})
endif()
if(WIN32)
    target_compile_definitions(patternlib_internal PRIVATE ${WIN_EXPORT_ARGS})
endif()

# Add dependencies on generated files for pattern library
add_dependencies(patternlib_internal generated_headers fccase_h fclang_h)

# Main fontconfig sources
set(FC_SOURCES
    fcatomic.c
    fccache.c
    fccfg.c
    fccharset.c
    fccompat.c
    fcdbg.c
    fcdefault.c
    fcdir.c
    fcformat.c
    fcfreetype.c
    fcfs.c
    fcptrlist.c
    fchash.c
    fcinit.c
    fclang.c
    fclist.c
    fcmatch.c
    fcmatrix.c
    fcname.c
    fcobjs.c
    fcrange.c
    fcserialize.c
    fcstat.c
    fcstr.c
    fcweight.c
    fcxml.c
    ftglue.c
)

# Generate fcobjshash.h using gperf (Meson-compatible format)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.gperf
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate-gperf-meson-format.py
            ${CMAKE_CURRENT_SOURCE_DIR}/fcobjshash.gperf.h
            ${CMAKE_CURRENT_SOURCE_DIR}/fcobjs.h
            ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.gperf
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fcobjshash.gperf.h
            ${CMAKE_CURRENT_SOURCE_DIR}/fcobjs.h
            ${CMAKE_CURRENT_SOURCE_DIR}/generate-gperf-meson-format.py
    COMMENT "Generating fcobjshash.gperf (Meson-compatible format)"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.h
    COMMAND ${GPERF_EXECUTABLE} --pic -m 100
            ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.gperf
            --output-file ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.h
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.gperf
    COMMENT "Generating fcobjshash.h with gperf"
)

# Generate alias headers
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fcalias.h
           ${CMAKE_CURRENT_BINARY_DIR}/fcaliastail.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/makealias.py
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/fcalias.h
            ${CMAKE_CURRENT_BINARY_DIR}/fcaliastail.h
            ${CMAKE_CURRENT_BINARY_DIR}/../fontconfig/fontconfig.h
            ${CMAKE_CURRENT_SOURCE_DIR}/fcdeprecate.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../fontconfig/fcprivate.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/makealias.py
            ${CMAKE_CURRENT_BINARY_DIR}/../fontconfig/fontconfig.h
            ${CMAKE_CURRENT_SOURCE_DIR}/fcdeprecate.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../fontconfig/fcprivate.h
    COMMENT "Generating alias headers"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fcftalias.h
           ${CMAKE_CURRENT_BINARY_DIR}/fcftaliastail.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/makealias.py
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/fcftalias.h
            ${CMAKE_CURRENT_BINARY_DIR}/fcftaliastail.h
            ${CMAKE_CURRENT_SOURCE_DIR}/../fontconfig/fcfreetype.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/makealias.py
            ${CMAKE_CURRENT_SOURCE_DIR}/../fontconfig/fcfreetype.h
    COMMENT "Generating FreeType alias headers"
)

# Create custom targets for generated headers first
add_custom_target(generated_headers
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/fcalias.h
        ${CMAKE_CURRENT_BINARY_DIR}/fcaliastail.h
        ${CMAKE_CURRENT_BINARY_DIR}/fcftalias.h
        ${CMAKE_CURRENT_BINARY_DIR}/fcftaliastail.h
        ${CMAKE_CURRENT_BINARY_DIR}/fcobjshash.h
        ${CMAKE_CURRENT_BINARY_DIR}/fcstdint.h
)

# All library sources including generated files
set(LIB_FONTCONFIG_SOURCES
    ${FC_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/fcstdint.h
)

# Create the main fontconfig library
add_library(fontconfig STATIC ${LIB_FONTCONFIG_SOURCES})
target_compile_options(fontconfig PRIVATE -fPIC)

# Add dependencies on generated files
add_dependencies(fontconfig generated_headers fccase_h fclang_h)

# Set library properties
set_target_properties(fontconfig PROPERTIES
    VERSION ${LIBVERSION}
    SOVERSION ${SOVERSION}
)

if(APPLE)
    set_target_properties(fontconfig PROPERTIES
        MACOSX_COMPATIBILITY_VERSION ${OSXVERSION}
        MACOSX_CURRENT_VERSION ${OSXVERSION}
    )
endif()

# Include directories
target_include_directories(fontconfig
    PUBLIC
        $<BUILD_INTERFACE:${INCBASE}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/..
        ${FREETYPE2_INCLUDE_DIRS}
        ${XML_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}/../fc-lang
        ${CMAKE_CURRENT_BINARY_DIR}/../fc-case
)

# Use internal freetype target if available
if(TARGET freetype)
    get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(fontconfig PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(fontconfig PRIVATE freetype ${XML_LIBRARIES})
    # Add dependency to ensure proper build order
    add_dependencies(fontconfig freetype)
else()
    target_include_directories(fontconfig PRIVATE ${FREETYPE2_INCLUDE_DIRS})
    target_link_libraries(fontconfig PRIVATE ${FREETYPE2_LIBRARIES} ${XML_LIBRARIES})
endif()

# Link pattern library objects directly instead of linking the library
target_sources(fontconfig PRIVATE $<TARGET_OBJECTS:patternlib_internal>)

if(MATH_LIBRARY)
    target_link_libraries(fontconfig PRIVATE ${MATH_LIBRARY})
endif()

if(Threads_FOUND)
    target_link_libraries(fontconfig PRIVATE Threads::Threads)
endif()

if(Intl_FOUND)
    target_link_libraries(fontconfig PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fontconfig PRIVATE ${Intl_INCLUDE_DIRS})
endif()

if(Iconv_FOUND)
    target_link_libraries(fontconfig PRIVATE Iconv::Iconv)
endif()

# Compile definitions
if(WIN32)
    target_compile_definitions(fontconfig PRIVATE ${WIN_EXPORT_ARGS})
endif()

# Install the library
# 当使用内部目标时，不导出目标以避免依赖问题
if(TARGET expat AND TARGET freetype)
    # 使用内部目标时，只安装库文件，不导出目标
    install(TARGETS fontconfig
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime
            NAMELINK_COMPONENT Development
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Development
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
    )
else()
    # 使用系统库时，可以安全地导出目标
    install(TARGETS fontconfig
        EXPORT fontconfigTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime
            NAMELINK_COMPONENT Development
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Development
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
    )
endif()

# Create static library for internal use
add_library(fontconfig_internal STATIC ${LIB_FONTCONFIG_SOURCES})
target_compile_options(fontconfig_internal PRIVATE -fPIC)

# Add dependencies on generated files
add_dependencies(fontconfig_internal generated_headers fccase_h fclang_h)
target_include_directories(fontconfig_internal
    PUBLIC
        ${INCBASE}
        ${CMAKE_CURRENT_BINARY_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/..
        ${XML_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}/../fc-lang
        ${CMAKE_CURRENT_BINARY_DIR}/../fc-case
)

# Use internal freetype target if available
if(TARGET freetype)
    get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(fontconfig_internal PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(fontconfig_internal PRIVATE freetype ${XML_LIBRARIES})
    # Add dependency to ensure proper build order
    add_dependencies(fontconfig_internal freetype)
else()
    target_include_directories(fontconfig_internal PRIVATE ${FREETYPE2_INCLUDE_DIRS})
    target_link_libraries(fontconfig_internal PRIVATE ${FREETYPE2_LIBRARIES} ${XML_LIBRARIES})
endif()

# Link pattern library objects directly
target_sources(fontconfig_internal PRIVATE $<TARGET_OBJECTS:patternlib_internal>)

if(MATH_LIBRARY)
    target_link_libraries(fontconfig_internal PRIVATE ${MATH_LIBRARY})
endif()

if(Threads_FOUND)
    target_link_libraries(fontconfig_internal PRIVATE Threads::Threads)
endif()

if(Intl_FOUND)
    target_link_libraries(fontconfig_internal PRIVATE ${Intl_LIBRARIES})
    target_include_directories(fontconfig_internal PRIVATE ${Intl_INCLUDE_DIRS})
endif()

if(Iconv_FOUND)
    target_link_libraries(fontconfig_internal PRIVATE Iconv::Iconv)
endif()

if(WIN32)
    target_compile_definitions(fontconfig_internal PRIVATE ${WIN_EXPORT_ARGS})
endif()

# Export targets for find_package (only when using system libraries)
if(NOT (TARGET expat AND TARGET freetype))
    install(EXPORT fontconfigTargets
        FILE fontconfigTargets.cmake
        NAMESPACE fontconfig::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fontconfig
        COMPONENT Development
    )
endif()

# Create config file for find_package
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    fontconfigConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/fontconfigConfig.cmake.in
    fontconfigConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fontconfig
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfigConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/fontconfigConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fontconfig
    COMPONENT Development
)