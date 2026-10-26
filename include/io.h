#ifndef _IO_H_
#define _IO_H_
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    /* MSVC's low-level I/O layer (<io.h>) over the POSIX descriptor calls.
     * Only what the ported code actually reaches for is mapped; anything else
     * should be spelled with the POSIX name directly. */

    /* The MSVC secure CRT reports failures as errno_t; POSIX returns -1 and
     * leaves the code in errno. Callers compare these results, so the type has
     * to be an integer, not void. */
    typedef int errno_t;

    /* <io.h> names a stream's descriptor _fileno(); POSIX names it fileno(). */
#define _fileno(stream) fileno(stream)

    /* Resizes the open file to `size` bytes. Returns 0 on success, otherwise
     * the errno value describing the failure (EINVAL, EBADF, ...). */
    errno_t _chsize_s(int fd, long long size);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_IO_H_
