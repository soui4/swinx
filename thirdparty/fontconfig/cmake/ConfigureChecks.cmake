# Configuration checks for fontconfig

# Check headers
set(CHECK_HEADERS
    dirent.h
    dlfcn.h
    fcntl.h
    inttypes.h
    stdint.h
    stdio.h
    stdlib.h
    strings.h
    string.h
    unistd.h
    sys/statvfs.h
    sys/vfs.h
    sys/statfs.h
    sys/stat.h
    sys/types.h
    sys/param.h
    sys/mount.h
    time.h
    wchar.h
)

foreach(header ${CHECK_HEADERS})
    string(TOUPPER ${header} header_upper)
    string(REPLACE "/" "_" header_upper ${header_upper})
    string(REPLACE "." "_" header_upper ${header_upper})
    check_include_file(${header} HAVE_${header_upper})
endforeach()

# Check functions
set(CHECK_FUNCTIONS
    link
    mkstemp
    mkostemp
    _mktemp_s
    mkdtemp
    getopt
    getopt_long
    getprogname
    getexecname
    rand
    random
    lrand48
    random_r
    rand_r
    readlink
    fstatvfs
    fstatfs
    lstat
    strerror
    strerror_r
    mmap
    vprintf
    vsnprintf
    vsprintf
    getpagesize
    getpid
    dcgettext
    gettext
    localtime_r
)

foreach(func ${CHECK_FUNCTIONS})
    string(TOUPPER ${func} func_upper)
    check_function_exists(${func} HAVE_${func_upper})
endforeach()

# Check FreeType functions
if(FREETYPE2_FOUND)
    set(CMAKE_REQUIRED_LIBRARIES ${FREETYPE2_LIBRARIES})
    set(CMAKE_REQUIRED_INCLUDES ${FREETYPE2_INCLUDE_DIRS})

    check_function_exists(FT_Get_BDF_Property HAVE_FT_GET_BDF_PROPERTY)
    check_function_exists(FT_Get_PS_Font_Info HAVE_FT_GET_PS_FONT_INFO)
    check_function_exists(FT_Has_PS_Glyph_Names HAVE_FT_HAS_PS_GLYPH_NAMES)
    check_function_exists(FT_Get_X11_Font_Format HAVE_FT_GET_X11_FONT_FORMAT)
    check_function_exists(FT_Done_MM_Var HAVE_FT_DONE_MM_VAR)

    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_INCLUDES)
endif()

# Check header symbols
check_symbol_exists(posix_fadvise fcntl.h HAVE_POSIX_FADVISE)

# Check struct members
check_struct_has_member("struct statvfs" f_basetype "sys/statvfs.h" HAVE_STRUCT_STATVFS_F_BASETYPE)
check_struct_has_member("struct statvfs" f_fstypename "sys/statvfs.h" HAVE_STRUCT_STATVFS_F_FSTYPENAME)
check_struct_has_member("struct statfs" f_flags "sys/vfs.h;sys/statfs.h;sys/param.h;sys/mount.h" HAVE_STRUCT_STATFS_F_FLAGS)
check_struct_has_member("struct statfs" f_fstypename "sys/vfs.h;sys/statfs.h;sys/param.h;sys/mount.h" HAVE_STRUCT_STATFS_F_FSTYPENAME)
check_struct_has_member("struct stat" st_mtim "sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM)
check_struct_has_member("struct dirent" d_type "sys/types.h;dirent.h" HAVE_STRUCT_DIRENT_D_TYPE)

# Check type sizes
check_type_size("void *" SIZEOF_VOID_P)

# Check alignments
# Use fallback since CheckTypeAlignment is not widely available
set(ALIGNOF_VOID_P ${CMAKE_SIZEOF_VOID_P})
set(ALIGNOF_DOUBLE 8)

# Check for flexible array member support
check_c_source_compiles("
struct test {
    int len;
    char data[];
};
int main() { return 0; }
" HAVE_FLEXIBLE_ARRAY_MEMBER)

if(HAVE_FLEXIBLE_ARRAY_MEMBER)
    set(FLEXIBLE_ARRAY_MEMBER "")
else()
    set(FLEXIBLE_ARRAY_MEMBER "1")
endif()

# Check for pthread priority inheritance
if(Threads_FOUND)
    check_c_source_compiles("
#include <pthread.h>
int main() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    return 0;
}
" HAVE_PTHREAD_PRIO_INHERIT)
endif()

# Check for atomic primitives
check_c_source_compiles("
#include <stdatomic.h>
int main() {
    atomic_int x = ATOMIC_VAR_INIT(0);
    atomic_fetch_add(&x, 1);
    return 0;
}
" HAVE_STDATOMIC_PRIMITIVES)

check_c_source_compiles("
int main() {
    int x = 0;
    __sync_fetch_and_add(&x, 1);
    return 0;
}
" HAVE_INTEL_ATOMIC_PRIMITIVES)

check_c_source_compiles("
#include <atomic.h>
int main() {
    uint_t x = 0;
    atomic_add_int(&x, 1);
    return 0;
}
" HAVE_SOLARIS_ATOMIC_OPS)

# Check FreeType PCF long family names support
if(FREETYPE2_FOUND)
    set(CMAKE_REQUIRED_LIBRARIES ${FREETYPE2_LIBRARIES})
    set(CMAKE_REQUIRED_INCLUDES ${FREETYPE2_INCLUDE_DIRS})

    check_c_source_compiles("
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BDF_H
#include FT_TRUETYPE_IDS_H
int main() {
    FT_Library library;
    FT_Face face;
    BDF_PropertyRec prop;
    FT_Init_FreeType(&library);
    if (FT_Get_BDF_Property(face, \"FAMILY_NAME\", &prop) == 0)
        return 0;
    return 1;
}
" FREETYPE_PCF_LONG_FAMILY_NAMES)

    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_INCLUDES)
endif()

if(ENABLE_PREBUILT_GPERF)
    message(STATUS "Using prebuilt gperf files, set FC_GPERF_SIZE_T to size_t")
    set(FC_GPERF_SIZE_T "size_t")
else(ENABLE_PREBUILT_GPERF)
# Check gperf length type
find_program(GPERF_EXECUTABLE gperf REQUIRED)
execute_process(
    COMMAND ${GPERF_EXECUTABLE} -L ANSI-C ${CMAKE_CURRENT_SOURCE_DIR}/meson-cc-tests/gperf.txt
    OUTPUT_VARIABLE GPERF_OUTPUT
    ERROR_QUIET
    )
    
if(GPERF_OUTPUT MATCHES "size_t")
    set(FC_GPERF_SIZE_T "size_t")
else()
    set(FC_GPERF_SIZE_T "unsigned")
endif()
endif(ENABLE_PREBUILT_GPERF)

# Set additional defines
set(_GNU_SOURCE 1)
set(GETTEXT_PACKAGE "${PROJECT_NAME}")

# Sanity check for required types
check_type_size("uint64_t" UINT64_T)
check_type_size("int32_t" INT32_T)
check_type_size("uintptr_t" UINTPTR_T)
check_type_size("intptr_t" INTPTR_T)

if(NOT HAVE_UINT64_T OR NOT HAVE_INT32_T OR NOT HAVE_UINTPTR_T OR NOT HAVE_INTPTR_T)
    message(FATAL_ERROR "Required integer types not found")
endif()