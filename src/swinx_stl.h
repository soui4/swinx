/*
 * swinx_stl.h - drop-in STL substitution shim for swinx
 *
 * Purpose:
 *   Let swinx use uSTL (https://github.com/msharov/ustl) for the
 *   container / string types it relies on, while keeping the rest of the
 *   standard library (threads, mutexes, atomics, chrono, functional,
 *   memory, iostream, exceptions) on the real libstdc++ / libc++.
 *
 *   uSTL does NOT implement <thread>, <mutex>, <condition_variable>,
 *   <atomic>, <chrono>, <functional>, <memory>(shared_ptr is present in
 *   some forks but not in upstream msharov/ustl; we keep std anyway),
 *   <fstream>, <sstream>, <exception>. Those are intentionally left as
 *   real std below and are NOT rewritten by the substitution script.
 *
 *   uSTL also has NO hash / unordered associative container (no
 *   unordered_map / unordered_set) and NO deque. Those two types are kept
 *   on the real standard library in both modes (see the shim body).
 *
 *   NOTE on semantics: upstream uSTL's map/set/list/multimap/multiset are
 *   implemented on top of ustl::vector (a sorted vector), NOT a red-black
 *   tree / linked list. The API is close enough to compile, but insert /
 *   erase are O(n) and iteration/ordering differ from std. Keep this in
 *   mind if swinx relies on logarithmic associative performance.
 *
 * How it works:
 *   - When SOUI_USE_USTL is defined, the types below resolve to ustl::*.
 *   - Otherwise (default) they resolve to std::* verbatim, so the build
 *     behaves exactly as before. This makes the mechanical replacement in
 *     swinx source fully reversible and zero-risk when the option is off.
 *
 * This header is force-included into every swinx C++ TU only (see swinx
 * CMakeLists.txt, gated to COMPILE_LANGUAGE:CXX). swinx public headers in
 * include/ stay on std:: and do NOT include it, so the external API is
 * unaffected. The substitution therefore lives entirely inside swinx.
 */

#pragma once

#if defined(SOUI_USE_USTL)
    // Pull in uSTL. Try both the installed (<ustl/ustl.h>) and the
    // in-tree (<ustl.h>) layout; adjust USTL_INCLUDE_DIR if neither
    // matches your vendored copy.
    #if __has_include(<ustl/ustl.h>)
        #include <ustl/ustl.h>
    #elif __has_include(<ustl.h>)
        #include <ustl.h>
    #else
        #error "SOUI_USE_USTL is ON but <ustl/ustl.h> / <ustl.h> were not found. " \
               "Set -DUSTL_INCLUDE_DIR=<path> to your uSTL checkout (github.com/msharov/ustl, v2.5)."
    #endif

    // uSTL's ulist.h unconditionally does "#define deque list" at global scope
    // (uSTL's list is just a vector alias, not a real deque). That macro poisons
    // the bare token "deque" everywhere, so libstdc++'s std::deque is rewritten
    // to std::list and <deque> fails to compile. swinx keeps a real std::deque,
    // so undo the macro immediately after pulling in the umbrella.
    #undef deque

    // uSTL has no unordered_map/unordered_set and no deque, so those two stay
    // on the real standard library. Their headers must be pulled in here
    // (the ustl umbrella does not include them) before the aliases below.
    #include <unordered_map>
    #include <deque>

    namespace swinx_stl {
        using ustl::string;
        using ustl::vector;
        using ustl::list;
        using ustl::map;
        using ustl::set;
        using ustl::pair;
        using ustl::make_pair;
        // uSTL provides no hash/unordered container and no deque, so these
        // two stay on the real standard library in both modes.
        using std::unordered_map;
        using std::deque;
    }
#else
    #include <string>
    #include <vector>
    #include <list>
    #include <map>
    #include <set>
    #include <deque>
    #include <unordered_map>
    #include <utility>

    namespace swinx_stl {
        using std::string;
        using std::vector;
        using std::list;
        using std::map;
        using std::set;
        using std::deque;
        using std::pair;
        using std::unordered_map;
        using std::make_pair;
    }
#endif
