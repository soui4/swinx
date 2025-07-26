# Build dependencies in correct order
add_subdirectory(zlib)

add_subdirectory(libpng)

add_subdirectory(pixman)
# freetype depends on some system libraries but not on our internal libs
add_subdirectory(freetype)

add_subdirectory(expat)

# fontconfig depends on freetype
add_subdirectory(fontconfig)

# cairo depends on pixman, freetype, and fontconfig
add_subdirectory(cairo)