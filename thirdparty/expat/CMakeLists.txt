#                          __  __            _
#                       ___\ \/ /_ __   __ _| |_
#                      / _ \\  /| '_ \ / _` | __|
#                     |  __//  \| |_) | (_| | |_
#                      \___/_/\_\ .__/ \__,_|\__|
#                               |_| XML parser
#
# Copyright (c) 2010      Patrick Spendrin <ps_ml@gmx.de>
# Copyright (c) 2012      Karl Waclawek <karl@waclawek.net>
# Copyright (c) 2016-2025 Sebastian Pipping <sebastian@pipping.org>
# Copyright (c) 2016      Sergei Nikulov <sergey.nikulov@gmail.com>
# Copyright (c) 2016      Björn Lindahl <bjorn.lindahl@foi.se>
# Copyright (c) 2016      Tobias Taschner <github@tc84.de>
# Copyright (c) 2016      Ben Boeckel <ben.boeckel@kitware.com>
# Copyright (c) 2017-2022 Rhodri James <rhodri@wildebeest.org.uk>
# Copyright (c) 2017      Rolf Eike Beer <eike@sf-mail.de>
# Copyright (c) 2017      Stephen Groat <stephen@groat.us>
# Copyright (c) 2017      Franek Korta <fkorta@gmail.com>
# Copyright (c) 2018      pedro-vicente <pedro.vicente@space-research.org>
# Copyright (c) 2018      Frank Rast <frank.rast@gefeg.com>
# Copyright (c) 2018      userwithuid <userwithuid@gmail.com>
# Copyright (c) 2018      Yury Gribov <tetra2005@gmail.com>
# Copyright (c) 2019      Kishore Kunche <kishore.kunche@intel.com>
# Copyright (c) 2019      xantares <xantares09@hotmail.com>
# Copyright (c) 2019      Mohammed Khajapasha <mohammed.khajapasha@intel.com>
# Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
# Copyright (c) 2019      Bhargava Shastry <bhargava.shastry@ethereum.org>
# Copyright (c) 2020      Maciej Sroczyński <macieksroczynski@poczta.fm>
# Copyright (c) 2020      Gulliver <gulliver@traumkristalle.net>
# Copyright (c) 2020      Thomas Beutlich <tc@tbeu.de>
# Copyright (c) 2021      Alex Richardson <Alexander.Richardson@cl.cam.ac.uk>
# Copyright (c) 2022      Johnny Jazeix <jazeix@gmail.com>
# Copyright (c) 2022      Mark Brand <markbrand@google.com>
# Copyright (c) 2022      David Faure <david.faure@kdab.com>
# Unlike most of Expat,
# this file is copyrighted under the BSD-license for buildsystem files of KDE.

cmake_minimum_required(VERSION 3.13.0)

project(expat
    VERSION
        2.7.1
    LANGUAGES
        C
)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # i.e. -std=c99 rather than default -std=gnu99

set(PACKAGE_BUGREPORT "https://github.com/libexpat/libexpat/issues")
set(PACKAGE_NAME "expat")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE_NAME}")

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

#
# Configuration defaults
#
if(WINCE)
    set(_EXPAT_BUILD_TOOLS_DEFAULT OFF)
else()
    set(_EXPAT_BUILD_TOOLS_DEFAULT ON)
endif()
if(MSVC OR NOT _EXPAT_BUILD_TOOLS_DEFAULT)
    set(_EXPAT_BUILD_DOCS_DEFAULT OFF)
else()
    find_program(DOCBOOK_TO_MAN NAMES docbook2x-man db2x_docbook2man docbook2man docbook-to-man)
    if(DOCBOOK_TO_MAN)
        set(_EXPAT_BUILD_DOCS_DEFAULT ON)
    else()
        set(_EXPAT_BUILD_DOCS_DEFAULT OFF)
    endif()
endif()
if(MSVC)
    set(_EXPAT_BUILD_PKGCONFIG_DEFAULT OFF)
else()
    set(_EXPAT_BUILD_PKGCONFIG_DEFAULT ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE NoConfig)  # so that accessing EXPAT_*_POSTFIX will be waterproof
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" _EXPAT_BUILD_TYPE_UPPER)

#
# Configuration
#

macro(expat_shy_set var default cache type desc)
    # Macro expat_shy_set came into life because:
    # - Expat was previously using an inconsistent mix of CMake's native set()
    #   and option() to define public build time options.
    # - option() is more friendly than set() with regard to configuring an
    #   external project that is pulled in by means of add_subdirectory() --
    #   see comments in issue #597 -- so we wanted to get away from set().
    # - option() auto-converts non-bool values to bool when writing to the CMake
    #   cache, so we needed something that supports non-bool better and hence
    #   wanted to get away from plain option(), too.
    #
    # As a result, this function serves as a hybrid between CMake's regular set()
    # and option(): from set() it takes support for non-bool types and the function
    # name and signature whereas from option() (with policy CMP0077 mode NEW) it
    # takes being shy when a value has previously been defined for that variable.
    #
    # So that resolves all need for set(.. FORCE) when pulling in Expat by means of
    # add_subdirectory().
    #
    if(NOT ${cache} STREQUAL "CACHE")
        message(SEND_ERROR "Macro usage is: expat_shy_set(var default CACHE type desc)")
    endif()

    if(DEFINED ${var})
        # NOTE: The idea is to (ideally) only add to the cache if
        #       there is no cache entry, yet.  "if(DEFINED CACHE{var})"
        #       requires CMake >=3.14.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.14" AND NOT DEFINED "CACHE{${var}}")
            set("${var}" "${${var}}" CACHE "${type}" "${desc}")
        endif()
    else()
        set("${var}" "${default}" CACHE "${type}" "${desc}")
    endif()
endmacro()

expat_shy_set(EXPAT_BUILD_TOOLS ${_EXPAT_BUILD_TOOLS_DEFAULT} CACHE BOOL "Build the xmlwf tool for expat library")
expat_shy_set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "Build the examples for expat library")
expat_shy_set(EXPAT_BUILD_TESTS OFF CACHE BOOL "Build the tests for expat library")
expat_shy_set(EXPAT_BUILD_DOCS OFF CACHE BOOL "Build man page for xmlwf")
expat_shy_set(EXPAT_BUILD_FUZZERS OFF CACHE BOOL "Build fuzzers for the expat library")
expat_shy_set(EXPAT_BUILD_PKGCONFIG ${_EXPAT_BUILD_PKGCONFIG_DEFAULT} CACHE BOOL "Build pkg-config file")
expat_shy_set(EXPAT_OSSFUZZ_BUILD OFF CACHE BOOL "Build fuzzers via OSS-Fuzz for the expat library")
if(UNIX OR _EXPAT_HELP)
    expat_shy_set(EXPAT_WITH_LIBBSD OFF CACHE BOOL "Utilize libbsd (for arc4random_buf)")
endif()
expat_shy_set(EXPAT_ENABLE_INSTALL ON CACHE BOOL "Install expat files in cmake install target")
expat_shy_set(EXPAT_CONTEXT_BYTES 1024 CACHE STRING "Define to specify how much context to retain around the current parse point, 0 to disable")
mark_as_advanced(EXPAT_CONTEXT_BYTES)
expat_shy_set(EXPAT_DTD ON CACHE BOOL "Define to make parameter entity parsing functionality available")
mark_as_advanced(EXPAT_DTD)
expat_shy_set(EXPAT_GE ON CACHE BOOL "Define to make general entity parsing functionality available")
mark_as_advanced(EXPAT_GE)
expat_shy_set(EXPAT_NS ON CACHE BOOL "Define to make XML Namespaces functionality available")
mark_as_advanced(EXPAT_NS)
expat_shy_set(EXPAT_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat all compiler warnings as errors")
if(UNIX OR _EXPAT_HELP)
    expat_shy_set(EXPAT_DEV_URANDOM ON CACHE BOOL "Define to include code reading entropy from `/dev/urandom'.")
    expat_shy_set(EXPAT_WITH_GETRANDOM "AUTO" CACHE STRING "Make use of getrandom function (ON|OFF|AUTO) [default=AUTO]")
    expat_shy_set(EXPAT_WITH_SYS_GETRANDOM "AUTO" CACHE STRING "Make use of syscall SYS_getrandom (ON|OFF|AUTO) [default=AUTO]")
    mark_as_advanced(EXPAT_DEV_URANDOM)
endif()
expat_shy_set(EXPAT_CHAR_TYPE "char" CACHE STRING "Character type to use (char|ushort|wchar_t) [default=char]")
expat_shy_set(EXPAT_ATTR_INFO OFF CACHE BOOL "Define to allow retrieving the byte offsets for attribute names and values")
mark_as_advanced(EXPAT_ATTR_INFO)
expat_shy_set(EXPAT_LARGE_SIZE OFF CACHE BOOL "Make XML_GetCurrent* functions return <(unsigned) long long> rather than <(unsigned) long>")
mark_as_advanced(EXPAT_LARGE_SIZE)
expat_shy_set(EXPAT_MIN_SIZE OFF CACHE BOOL "Get a smaller (but slower) parser (in particular avoid multiple copies of the tokenizer)")
mark_as_advanced(EXPAT_MIN_SIZE)
if(MSVC OR _EXPAT_HELP)
    expat_shy_set(EXPAT_MSVC_STATIC_CRT OFF CACHE BOOL "Use /MT flag (static CRT) when compiling in MSVC")
endif()
if(NOT _EXPAT_HELP)
    expat_shy_set(_EXPAT_M32 OFF CACHE BOOL "(Unofficial!) Produce 32bit code with -m32")
    mark_as_advanced(_EXPAT_M32)
endif()

if(EXPAT_BUILD_TESTS OR EXPAT_BUILD_FUZZERS)
    # We have to call enable_language() before modifying any CMAKE_CXX_* variables
    enable_language(CXX)

    if (EXPAT_BUILD_FUZZERS)
        set(CMAKE_CXX_STANDARD 17)  # for std::string_view for Abseil for libprotobuf-mutator
    else()
        set(CMAKE_CXX_STANDARD 11)
    endif()
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)  # i.e. -std=c++11 rather than default -std=gnu++11
endif()

#
# Environment checks
#
if(EXPAT_DTD AND NOT EXPAT_GE)
    message(SEND_ERROR "Option EXPAT_DTD requires that EXPAT_GE is also enabled.")
    message(SEND_ERROR "Please either enable option EXPAT_GE (recommended) or disable EXPAT_DTD also.")
endif()

if(EXPAT_WITH_LIBBSD)
    find_library(LIB_BSD NAMES bsd)
    if(NOT LIB_BSD)
        message(SEND_ERROR "EXPAT_WITH_LIBBSD option is enabled, but libbsd was not found")
    else()
        set(HAVE_LIBBSD TRUE)
    endif()
endif()

if(MSVC)
    # For the three types of MSVC version values, please see:
    # - https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html
    # - https://sourceforge.net/p/predef/wiki/Compilers/
    # - https://en.wikipedia.org/wiki/Microsoft_Visual_Studio#History
    set(_EXPAT_MSVC_REQUIRED_INT 1800)  # i.e. 12.0/2013/1800; see PR #426
    set(_EXPAT_MSVC_SUPPORTED_INT 1920)
    set(_EXPAT_MSVC_SUPPORTED_DISPLAY "Visual Studio 16.0/2019/${_EXPAT_MSVC_SUPPORTED_INT}")

    if(MSVC_VERSION VERSION_LESS ${_EXPAT_MSVC_SUPPORTED_INT})
        if(MSVC_VERSION VERSION_LESS ${_EXPAT_MSVC_REQUIRED_INT})
            message(SEND_ERROR "MSVC_VERSION ${MSVC_VERSION} is TOO OLD to compile Expat without errors.")
            message(SEND_ERROR "Please use officially supported ${_EXPAT_MSVC_SUPPORTED_DISPLAY} or later.  Thank you!")
        else()
            message(WARNING "MSVC_VERSION ${MSVC_VERSION} is NOT OFFICIALLY SUPPORTED by Expat.")
            message(WARNING "Please use ${_EXPAT_MSVC_SUPPORTED_DISPLAY} or later.  Thank you!")
        endif()
    endif()
endif()

macro(_expat_copy_bool_int source_ref dest_ref)
    if(${source_ref})
        set(${dest_ref} 1)
    else()
        set(${dest_ref} 0)
    endif()
endmacro()

if(EXPAT_LARGE_SIZE)
    add_definitions(-DXML_LARGE_SIZE)
endif()

if(EXPAT_MIN_SIZE)
    add_definitions(-DXML_MIN_SIZE)
endif()

if(EXPAT_CHAR_TYPE STREQUAL "char")
    set(_EXPAT_UNICODE OFF)
    set(_EXPAT_UNICODE_WCHAR_T OFF)
elseif(EXPAT_CHAR_TYPE STREQUAL "ushort")
    set(_EXPAT_UNICODE ON)
    set(_EXPAT_UNICODE_WCHAR_T OFF)
    if(EXPAT_BUILD_EXAMPLES)
        message(SEND_ERROR "Examples can not be built with option -DEXPAT_CHAR_TYPE=ushort. Please pass -DEXPAT_CHAR_TYPE=(char|wchar_t) or -DEXPAT_BUILD_EXAMPLES=OFF.")
    endif()
    if(EXPAT_BUILD_TESTS)
        message(SEND_ERROR "The testsuite can not be built with option -DEXPAT_CHAR_TYPE=ushort. Please pass -DEXPAT_CHAR_TYPE=(char|wchar_t) or -DEXPAT_BUILD_TESTS=OFF.")
    endif()
    if(EXPAT_BUILD_TOOLS)
        message(SEND_ERROR "The xmlwf tool can not be built with option -DEXPAT_CHAR_TYPE=ushort. Please pass -DEXPAT_CHAR_TYPE=(char|wchar_t) or -DEXPAT_BUILD_TOOLS=OFF.")
    endif()
elseif(EXPAT_CHAR_TYPE STREQUAL "wchar_t")
    set(_EXPAT_UNICODE ON)
    set(_EXPAT_UNICODE_WCHAR_T ON)
    if(NOT WIN32)
        string(FIND "${CMAKE_C_FLAGS}" "-fshort-wchar" _expat_short_wchar_found)
        if(${_expat_short_wchar_found} EQUAL "-1")
            message(SEND_ERROR "Configuration -DEXPAT_CHAR_TYPE=wchar_t requires -DCMAKE_{C,CXX}_FLAGS=-fshort-wchar (which was not found) and libc compiled with -fshort-wchar, too.")
        endif()
        if (EXPAT_BUILD_TOOLS)
            message(SEND_ERROR "The xmlwf tool can not be built with option -DEXPAT_CHAR_TYPE=wchar_t outside of Windows. Please pass -DEXPAT_CHAR_TYPE=char or -DEXPAT_BUILD_TOOLS=OFF.")
        endif()
    endif()
else()
    message(SEND_ERROR "Option -DEXPAT_CHAR_TYPE=(char|ushort|wchar_t) cannot be \"${EXPAT_CHAR_TYPE}\".")
endif()

if(_EXPAT_UNICODE)
    add_definitions(-DXML_UNICODE)              # for unsigned short
    if(_EXPAT_UNICODE_WCHAR_T)
        add_definitions(-DXML_UNICODE_WCHAR_T)  # for wchar_t
    endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/ConfigureChecks.cmake)

macro(evaluate_detection_results use_ref have_ref thing_lower thing_title)
    if(${use_ref} AND NOT (${use_ref} STREQUAL "AUTO") AND NOT ${have_ref})
        message(SEND_ERROR
                "Use of ${thing_lower} was enforced by ${use_ref}=ON but it could not be found.")
    elseif(NOT ${use_ref} AND ${have_ref})
        message("${thing_title} was found but it will not be used due to ${use_ref}=OFF.")
        set(${have_ref} 0)
    endif()
endmacro()

if(NOT WIN32)
    evaluate_detection_results(EXPAT_WITH_GETRANDOM HAVE_GETRANDOM "function getrandom" "Function getrandom")
    evaluate_detection_results(EXPAT_WITH_SYS_GETRANDOM HAVE_SYSCALL_GETRANDOM "syscall SYS_getrandom" "Syscall SYS_getrandom")
endif()

_expat_copy_bool_int(EXPAT_ATTR_INFO        XML_ATTR_INFO)
_expat_copy_bool_int(EXPAT_DTD              XML_DTD)
_expat_copy_bool_int(EXPAT_GE               XML_GE)
_expat_copy_bool_int(EXPAT_LARGE_SIZE       XML_LARGE_SIZE)
_expat_copy_bool_int(EXPAT_MIN_SIZE         XML_MIN_SIZE)
_expat_copy_bool_int(EXPAT_NS               XML_NS)
if(NOT WIN32)
    _expat_copy_bool_int(EXPAT_DEV_URANDOM  XML_DEV_URANDOM)
endif()
if(NOT EXPAT_CONTEXT_BYTES GREATER 0)  # in particular with -DEXPAT_CONTEXT_BYTES=OFF
    set(EXPAT_CONTEXT_BYTES 0)
endif()
set(XML_CONTEXT_BYTES ${EXPAT_CONTEXT_BYTES})

macro(expat_install)
    if(EXPAT_ENABLE_INSTALL)
        install(${ARGN})
    endif()
endmacro()

configure_file(expat_config.h.cmake "${CMAKE_CURRENT_BINARY_DIR}/expat_config.h")
expat_install(FILES "${CMAKE_CURRENT_BINARY_DIR}/expat_config.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


set(EXTRA_COMPILE_FLAGS)
if(FLAG_NO_STRICT_ALIASING)
    set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -fno-strict-aliasing")
endif()
if(FLAG_VISIBILITY)
  if(EXPAT_SHARED_LIBS)
     add_definitions(-DXML_ENABLE_VISIBILITY=1)
  endif()
  set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -fvisibility=hidden")
endif()
if(MINGW AND ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"))
    # Without __USE_MINGW_ANSI_STDIO the compiler produces a false positive
    set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -Wno-pedantic-ms-format")
endif()
if (EXPAT_WARNINGS_AS_ERRORS)
    if(MSVC)
        add_definitions(/WX)
    else()
        set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -Werror")
        if(MINGW)
            # To avoid "error: unknown conversion type character ‘l’ in format [-Werror=format=]"
            set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -Wno-format")
        endif()
    endif()
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EXTRA_COMPILE_FLAGS}")

if (MSVC)
    if (EXPAT_MSVC_STATIC_CRT)
        message("-- Using static CRT ${EXPAT_MSVC_STATIC_CRT}")
        foreach(flag_var
                CMAKE_CXX_FLAGS_${_EXPAT_BUILD_TYPE_UPPER}
                CMAKE_CXX_FLAGS_DEBUG
                CMAKE_CXX_FLAGS_RELEASE
                CMAKE_CXX_FLAGS_MINSIZEREL
                CMAKE_CXX_FLAGS_RELWITHDEBINFO
                CMAKE_C_FLAGS_${_EXPAT_BUILD_TYPE_UPPER}
                CMAKE_C_FLAGS_DEBUG
                CMAKE_C_FLAGS_RELEASE
                CMAKE_C_FLAGS_MINSIZEREL
                CMAKE_C_FLAGS_RELWITHDEBINFO
                )
            string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endforeach()
    endif()
endif()

if(_EXPAT_M32 AND NOT MSVC)
    foreach(flag_var
            CMAKE_CXX_FLAGS_${_EXPAT_BUILD_TYPE_UPPER}
            CMAKE_CXX_FLAGS_DEBUG
            CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL
            CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS_${_EXPAT_BUILD_TYPE_UPPER}
            CMAKE_C_FLAGS_DEBUG
            CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL
            CMAKE_C_FLAGS_RELWITHDEBINFO
            )
        set(${flag_var} "${${flag_var}} -m32")
    endforeach()
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -wd4996)
endif()

#
# C library
#
set(_EXPAT_C_SOURCES
    lib/xmlparse.c
    lib/xmlrole.c
    lib/xmltok.c
# NOTE: ISO C forbids an empty translation unit
#   lib/xmltok_impl.c
#   lib/xmltok_ns.c
)

if(EXPAT_SHARED_LIBS)
    set(_SHARED SHARED)
    if(WIN32)
        macro(_expat_def_file_toggle source_var target_var)
            if(${source_var})
                set(${target_var} " ")  # i.e. not commented out, a single space
            else()
                set(${target_var} ";")  # i.e. commented out
            endif()
        endmacro()

        if(EXPAT_DTD OR EXPAT_GE)
            set(_EXPAT_DTD_OR_GE TRUE)
        else()
            set(_EXPAT_DTD_OR_GE FALSE)
        endif()

        _expat_def_file_toggle(_EXPAT_DTD_OR_GE _EXPAT_COMMENT_DTD_OR_GE)
        _expat_def_file_toggle(EXPAT_ATTR_INFO _EXPAT_COMMENT_ATTR_INFO)

        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lib/libexpat.def.cmake" "${CMAKE_CURRENT_BINARY_DIR}/lib/libexpat.def")
        set(_EXPAT_EXTRA_SOURCES ${_EXPAT_EXTRA_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/lib/libexpat.def")

        # Add DLL version
        string(REPLACE "." "," _EXPAT_DLL_VERSION ${PROJECT_VERSION}.0)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/win32/version.rc.cmake" "${CMAKE_CURRENT_BINARY_DIR}/win32/version.rc")
        set(_EXPAT_EXTRA_SOURCES ${_EXPAT_EXTRA_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/win32/version.rc")
    endif()
else()
    set(_SHARED STATIC)
endif()

add_library(expat STATIC ${_EXPAT_C_SOURCES} ${_EXPAT_EXTRA_SOURCES})
add_library(expat::expat ALIAS expat)
target_compile_options(expat PRIVATE -fPIC)

target_include_directories(expat
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
