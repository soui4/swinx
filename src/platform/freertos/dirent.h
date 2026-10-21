/*
 * swinx/platform/freertos/dirent.h -- FreeRTOS stub for <dirent.h>.
 *
 * Bare-metal newlib's own <dirent.h> #errors out ("<dirent.h> not
 * supported"); the SOUI res-provider enumerates directories through it on
 * POSIX. This freertos shadow provides the declaration surface only -- no
 * SOUI code path on FreeRTOS actually calls opendir/readdir (the memory
 * ResProvider serves assets), so nothing needs an implementation. If
 * directory scanning is ever wired up, the opendir/readdir/closedir
 * implementations belong in the platform bridge.
 */
#ifndef SWINX_FREERTOS_DIRENT_H_
#define SWINX_FREERTOS_DIRENT_H_

#include <stddef.h>

typedef struct DIR_ DIR;

struct dirent
{
    int d_ino;
    char d_name[256];
};

#ifdef __cplusplus
extern "C" {
#endif

DIR *opendir(const char *name);
int closedir(DIR *dir);
struct dirent *readdir(DIR *dir);
void rewinddir(DIR *dir);
void seekdir(DIR *dir, long pos);
long telldir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif /* SWINX_FREERTOS_DIRENT_H_ */
