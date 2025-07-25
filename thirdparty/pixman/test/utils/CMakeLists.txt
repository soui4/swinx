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

# Test utilities library
add_library(testutils STATIC
    utils.c
    utils-prng.c
)

# Set include directories
target_include_directories(testutils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_BINARY_DIR}
)

# Link with pixman and dependencies
target_link_libraries(testutils
    PUBLIC
        pixman::pixman-1
    PRIVATE
        ${CMAKE_THREAD_LIBS_INIT}
)

# Link with math library if available
if(MATH_LIBRARY)
    target_link_libraries(testutils PRIVATE ${MATH_LIBRARY})
endif()

# Link with PNG if available
if(PNG_FOUND)
    target_link_libraries(testutils PRIVATE PNG::PNG)
endif()

# Link with OpenMP if available
if(OpenMP_C_FOUND)
    target_link_libraries(testutils PRIVATE OpenMP::OpenMP_C)
endif()

# Create an alias for easier use
add_library(pixman::testutils ALIAS testutils)
