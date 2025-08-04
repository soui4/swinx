# This file is copyrighted under the BSD-license for buildsystem files of KDE
# copyright 2010, Patrick Spendrin <ps_ml@gmx.de>

project(expat)

cmake_minimum_required(VERSION 3.15)
set(PACKAGE_BUGREPORT "expat-bugs@libexpat.org")
set(PACKAGE_NAME "expat")
set(PACKAGE_VERSION "2.2.4")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "${PACKAGE_NAME}")

if(WINCE)
    set(BUILD_tools_default OFF)
else()
    set(BUILD_tools_default ON)
endif()
if(MSVC OR NOT BUILD_tools_default)
    set(BUILD_doc_default OFF)
else()
    find_program(DOCBOOK_TO_MAN NAMES docbook2x-man)
    if(DOCBOOK_TO_MAN)
        set(BUILD_doc_default ON)
    else()
        set(BUILD_doc_default OFF)
    endif()
endif()

option(INSTALL "install expat files in cmake install target" ON)

# configuration options
set(XML_CONTEXT_BYTES 1024 CACHE STRING "Define to specify how much context to retain around the current parse point")
option(XML_DTD "Define to make parameter entity parsing functionality available" ON)
option(XML_NS "Define to make XML Namespaces functionality available" ON)
if(NOT WIN32)
    option(XML_DEV_URANDOM "Define to include code reading entropy from `/dev/urandom'." ON)
    if(XML_DEV_URANDOM)
        add_definitions(-DXML_DEV_URANDOM)
    endif()
endif()

if(XML_DTD)
    set(XML_DTD 1)
else(XML_DTD)
    set(XML_DTD 0)
endif(XML_DTD)
if(XML_NS)
    set(XML_NS 1)
else(XML_NS)
    set(XML_NS 0)
endif(XML_NS)

include(${CMAKE_CURRENT_LIST_DIR}/ConfigureChecks.cmake)

set(EXTRA_COMPILE_FLAGS)
if(FLAG_NO_STRICT_ALIASING)
    set(EXTRA_COMPILE_FLAGS "${EXTRA_COMPILE_FLAGS} -fno-strict-aliasing")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EXTRA_COMPILE_FLAGS}")

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/lib)
if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -wd4996)
endif(MSVC)

set(expat_SRCS
    lib/loadlibrary.c
    lib/xmlparse.c
    lib/xmlrole.c
    lib/xmltok.c
    lib/xmltok_impl.c
    lib/xmltok_ns.c
)

if(WIN32)
    add_definitions(-DXML_STATIC)
endif(WIN32)

add_library(expat STATIC ${expat_SRCS})
target_compile_options(expat PRIVATE -fPIC)

set(LIBCURRENT 7)   # sync
set(LIBREVISION 6)  # with
set(LIBAGE 6)       # configure.ac!
math(EXPR LIBCURRENT_MINUS_AGE "${LIBCURRENT} - ${LIBAGE}")

if(NOT WIN32)
    set_property(TARGET expat PROPERTY VERSION ${LIBCURRENT_MINUS_AGE}.${LIBAGE}.${LIBREVISION})
    set_property(TARGET expat PROPERTY SOVERSION ${LIBCURRENT_MINUS_AGE})
    set_property(TARGET expat PROPERTY NO_SONAME ${NO_SONAME})
endif(NOT WIN32)


target_include_directories(expat
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
)

