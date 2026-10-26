#ifndef _SWINX_MALLOC_H_
#define _SWINX_MALLOC_H_

/*
 * MSVC CRT <malloc.h> shim.
 *
 * WebKit's OS(WINDOWS) code paths (PageBlock.cpp, PageAllocation.h,
 * MachineStackMarker.cpp, FastMalloc.cpp) include <malloc.h> and use the
 * MSVC-only heap helpers.  On the swinx platform swinx/include comes first on
 * the include path, so this header provides the subset WebKit needs on top of
 * the C library exports that POSIX already has.
 */

#ifdef _WIN32

/* swinx built on a real Windows host: use the CRT header we would otherwise
 * shadow. */
#include_next <malloc.h>

#else /* !_WIN32 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
/* glibc/BSD: malloc_usable_size lives in the real <malloc.h>.  include_next
 * skips this shim and pulls the system header. */
#include_next <malloc.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /* MSVC: _msize/_aligned_msize return the usable size of an allocation. */
    static inline size_t _msize(void *ptr)
    {
#if defined(__APPLE__)
        return malloc_size(ptr);
#else
        return malloc_usable_size(ptr);
#endif
    }

    static inline size_t _aligned_msize(void *ptr, size_t alignment, size_t offset)
    {
        (void)alignment;
        (void)offset;
        return _msize(ptr);
    }

    /* MSVC: _aligned_malloc(size, alignment). */
    static inline void *_aligned_malloc(size_t size, size_t alignment)
    {
        void *p = NULL;
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return NULL;
        if (posix_memalign(&p, alignment, size) != 0)
            return NULL;
        return p;
    }

    static inline void _aligned_free(void *ptr)
    {
        free(ptr);
    }

    static inline void *_aligned_realloc(void *ptr, size_t size, size_t alignment)
    {
        void *p = _aligned_malloc(size, alignment);
        if (!p)
            return NULL;
        if (ptr)
        {
            size_t oldSize = _msize(ptr);
            memcpy(p, ptr, oldSize < size ? oldSize : size);
            _aligned_free(ptr);
        }
        return p;
    }

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* _SWINX_MALLOC_H_ */
