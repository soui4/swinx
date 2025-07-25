# Build dependencies in correct order
# pixman has no dependencies, build it first
add_subdirectory(pixman)

# freetype depends on some system libraries but not on our internal libs
add_subdirectory(freetype)

add_subdirectory(expat)

# fontconfig depends on freetype
add_subdirectory(fontconfig)

# cairo depends on pixman, freetype, and fontconfig
add_subdirectory(cairo)