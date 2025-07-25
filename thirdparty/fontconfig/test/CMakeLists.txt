# Test configuration

# Fetch test fonts target
add_custom_target(fetch_test_fonts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../build-aux/fetch-testfonts.py
            --target-dir ${CMAKE_CURRENT_BINARY_DIR}/../testfonts
            --try-symlink
    COMMENT "Fetching test fonts"
)

# List of tests
set(TESTS
    test-bz89617.c
    test-bz131804.c
    test-bz96676.c
    test-name-parse.c
    test-bz106618.c
    test-bz1744377.c
    test-issue180.c
    test-family-matching.c
    test-ptrlist.c
)

set(TESTS_NOT_PARALLEL "")

# Platform-specific tests
if(NOT WIN32)
    list(APPEND TESTS
        test-bz106632.c
        test-issue107.c
    )

    list(APPEND TESTS_NOT_PARALLEL
        test-crbug1004254.c
        test-mt-fccfg.c
    )

    # Static library specific tests
    if(BUILD_SHARED_LIBS STREQUAL "OFF")
        list(APPEND TESTS
            test-issue110.c
            test-d1f48f11.c
        )
    endif()
endif()

# Create test executables
foreach(test_file ${TESTS})
    string(REPLACE ".c" "" test_name ${test_file})
    string(REPLACE "-" "_" test_target ${test_name})

    add_executable(${test_target}
        ${test_file}
        ${CMAKE_CURRENT_BINARY_DIR}/../src/fcstdint.h
    )

    target_include_directories(${test_target} PRIVATE
        ${INCBASE}
        ${CMAKE_CURRENT_BINARY_DIR}/../src
    )

    target_link_libraries(${test_target} PRIVATE fontconfig_internal)

    # Special handling for specific tests
    if(test_file STREQUAL "test-bz89617.c")
        target_compile_definitions(${test_target} PRIVATE
            SRCDIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
    elseif(test_file STREQUAL "test-bz106632.c")
        target_compile_definitions(${test_target} PRIVATE
            FONTFILE="${CMAKE_CURRENT_SOURCE_DIR}/4x6.pcf"
        )
    elseif(test_file STREQUAL "test-ptrlist.c")
        target_include_directories(${test_target} PRIVATE ${INCSRC})
        if(Intl_FOUND)
            target_link_libraries(${test_target} PRIVATE ${Intl_LIBRARIES})
            target_include_directories(${test_target} PRIVATE ${Intl_INCLUDE_DIRS})
        endif()
    endif()

    # Add dependencies
    add_dependencies(${test_target} fccase_h fclang_h)

    # Add test
    add_test(NAME ${test_name} COMMAND ${test_target})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 600)
endforeach()

# Non-parallel tests
foreach(test_file ${TESTS_NOT_PARALLEL})
    string(REPLACE ".c" "" test_name ${test_file})
    string(REPLACE "-" "_" test_target ${test_name})

    add_executable(${test_target}
        ${test_file}
        ${CMAKE_CURRENT_BINARY_DIR}/../src/fcstdint.h
    )

    target_include_directories(${test_target} PRIVATE
        ${INCBASE}
        ${CMAKE_CURRENT_BINARY_DIR}/../src
    )

    target_link_libraries(${test_target} PRIVATE fontconfig_internal)

    # Special handling
    if(test_file STREQUAL "test-crbug1004254.c" OR test_file STREQUAL "test-mt-fccfg.c")
        target_link_libraries(${test_target} PRIVATE Threads::Threads)
    endif()

    if(test_file STREQUAL "test-mt-fccfg.c")
        target_include_directories(${test_target} PRIVATE ${INCSRC})
    endif()

    # Add dependencies
    add_dependencies(${test_target} fccase_h fclang_h)

    # Add test (non-parallel)
    add_test(NAME ${test_name} COMMAND ${test_target})
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 600
        RUN_SERIAL TRUE
    )
endforeach()

# Shell script tests (Unix only)
if(NOT WIN32)
    # Determine expected output file
    if(FREETYPE_PCF_LONG_FAMILY_NAMES)
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/out.expected-long-family-names
            ${CMAKE_CURRENT_BINARY_DIR}/out.expected
            COPYONLY
        )
    else()
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/out.expected-no-long-family-names
            ${CMAKE_CURRENT_BINARY_DIR}/out.expected
            COPYONLY
        )
    endif()

    # Add shell script test
    add_test(NAME run_test_sh
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-test.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(run_test_sh PROPERTIES
        TIMEOUT 600
        ENVIRONMENT "srcdir=${CMAKE_CURRENT_SOURCE_DIR};builddir=${CMAKE_CURRENT_BINARY_DIR};EXEEXT=${EXEEXT};VERBOSE=1"
    )

    # Python tests with pytest
    find_program(PYTEST_EXECUTABLE pytest)
    if(PYTEST_EXECUTABLE)
        add_test(NAME pytest
            COMMAND ${PYTEST_EXECUTABLE} --tap
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        set_tests_properties(pytest PROPERTIES
            TIMEOUT 600
            ENVIRONMENT "builddir=${CMAKE_CURRENT_BINARY_DIR}/.."
        )
        add_dependencies(pytest fetch_test_fonts)
    endif()
endif()

# JSON-C tests
pkg_check_modules(JSONC json-c)
if(JSONC_FOUND)
    add_executable(test_conf test-conf.c)
    target_link_libraries(test_conf PRIVATE fontconfig ${JSONC_LIBRARIES})
    target_include_directories(test_conf PRIVATE ${JSONC_INCLUDE_DIRS})

    add_test(NAME run_test_conf_sh
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-test-conf.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(run_test_conf_sh PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "srcdir=${CMAKE_CURRENT_SOURCE_DIR};builddir=${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_dependencies(run_test_conf_sh test_conf)
endif()