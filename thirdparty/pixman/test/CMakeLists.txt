# Copyright © 2018 Intel Corporation
# Copyright © 2024 Pixman Contributors

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Test programs
set(TESTS
    oob-test
    infinite-loop
    trap-crasher
    fence-image-self-test
    region-translate-test
    fetch-test
    a1-trap-test
    prng-test
    radial-invalid
    pdf-op-test
    region-test
    region-fractional-test
    combiner-test
    scaling-crash-test
    alpha-loop
    scaling-helpers-test
    rotate-test
    alphamap
    gradient-crash-test
    pixel-test
    matrix-test
    filter-reduction-test
    composite-traps-test
    region-contains-test
    glyph-test
    solid-test
    stress-test
    cover-test
    blitters-test
    affine-test
    scaling-test
    composite
    tolerance-test
    neg-stride-test
)

# Add thread-test only if pthreads or Windows threads are available
if(HAVE_PTHREADS OR WIN32)
    list(APPEND TESTS thread-test)
endif()

# Benchmark programs
set(PROGS
    lowlevel-blt-bench
    radial-perf-test
    check-formats
    scaling-bench
    affine-bench
)

# Create test executables and register them with CTest
foreach(test_name ${TESTS})
    add_executable(${test_name} ${test_name}.c)
    
    # Link with required libraries
    target_link_libraries(${test_name}
        PRIVATE
            pixman::pixman-1
            pixman::testutils
            ${CMAKE_THREAD_LIBS_INIT}
    )
    
    # Link with OpenMP if available
    if(OpenMP_C_FOUND)
        target_link_libraries(${test_name} PRIVATE OpenMP::OpenMP_C)
    endif()
    
    # Link with PNG if available
    if(PNG_FOUND)
        if(TARGET png_static)
            target_link_libraries(${test_name} PRIVATE png_static)
            # Manually add include directories for internal PNG
            get_target_property(PNG_INCLUDE_DIRS png_static INTERFACE_INCLUDE_DIRECTORIES)
            if(PNG_INCLUDE_DIRS)
                target_include_directories(${test_name} PRIVATE ${PNG_INCLUDE_DIRS})
            endif()
        else()
            target_link_libraries(${test_name} PRIVATE PNG::PNG)
        endif()
    endif()
    
    # Set include directories
    target_include_directories(${test_name} PRIVATE ${CMAKE_BINARY_DIR})
    
    # Register with CTest
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        TIMEOUT 120
    )
endforeach()

# Create benchmark executables (not registered as tests)
foreach(prog_name ${PROGS})
    add_executable(${prog_name} ${prog_name}.c)
    
    # Link with required libraries
    target_link_libraries(${prog_name}
        PRIVATE
            pixman::pixman-1
            pixman::testutils
    )
    
    # Link with OpenMP if available
    if(OpenMP_C_FOUND)
        target_link_libraries(${prog_name} PRIVATE OpenMP::OpenMP_C)
    endif()
    
    # Set include directories
    target_include_directories(${prog_name} PRIVATE ${CMAKE_BINARY_DIR})
endforeach()
