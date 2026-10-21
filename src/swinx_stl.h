/*
 * swinx_stl.h - force-include preamble for every swinx C++ TU
 *
 * Responsibilities (2026-10-20: the swinx_stl namespace indirection is gone,
 * swinx code uses std:: directly):
 *
 *   1. Exception handling disabled (FreeRTOS target built with
 *      -fno-exceptions).  Neutralize `throw EXPR;` so swinx's few throw
 *      sites still compile: the macro rewrites them into an unconditional
 *      abort.  The FreeRTOS build does NOT compile any swinx file that uses
 *      try/catch (those live only in the linux/cocoa platform sources), so
 *      only `throw` needs handling.  This block is a no-op on every other
 *      platform (SWINX_NO_EXCEPTIONS is only set by freeRTOS.cmake).
 *
 *   2. Pull in the standard <string>/<vector>/... headers.  On the FreeRTOS
 *      platform the concurrency primitives (<thread>/<mutex>/
 *      <condition_variable>) are shadowed by src/platform/freertos/stl/
 *      override headers (include path ordered BEFORE the toolchain), which
 *      define FreeRTOS-backed types directly in namespace std.
 *
 */

#pragma once

// ---------------------------------------------------------------------------
// 1. throw neutralization for -fno-exceptions builds (FreeRTOS only)
// ---------------------------------------------------------------------------
#if defined(SWINX_NO_EXCEPTIONS)
    #include <cstdlib>
    namespace std { [[noreturn]] inline void __fr_abort() noexcept { std::abort(); } }
    #define throw if (true) { ::std::__fr_abort(); } else (void)
#endif

// ---------------------------------------------------------------------------
// 2. standard container/string headers (used via std:: everywhere)
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <deque>
#include <unordered_map>
#include <utility>
