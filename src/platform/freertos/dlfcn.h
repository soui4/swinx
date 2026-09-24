/*
 * dlfcn.h -- FreeRTOS bridge for the <dlfcn.h> include pulled in by
 * swinx/include/windows.h.  A bare-metal FreeRTOS target has no dynamic
 * loader, so the entry points are declared here for API-compatibility and
 * implemented as no-op stubs (see dlfreertos.cpp).  Only reachable on the
 * FreeRTOS platform: on Linux/macOS/Android/OHOS the toolchain's own
 * <dlfcn.h> is used instead (this directory is not on their include path).
 */
#ifndef SWINX_FREERTOS_DLFCN_H_
#define SWINX_FREERTOS_DLFCN_H_

#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0000

#define RTLD_NEXT      ((void *)-1)
#define RTLD_DEFAULT   ((void *)0)

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *filename, int flag);
void *dlsym(void *handle, const char *symbol);
int   dlclose(void *handle);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif // SWINX_FREERTOS_DLFCN_H_
