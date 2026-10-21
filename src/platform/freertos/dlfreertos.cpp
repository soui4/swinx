/*
 * swinx/platform/freertos/dlfreertos.cpp
 *
 * Implementation of the <dlfcn.h> bridge declared in dlfcn.h (this dir).
 * A bare-metal FreeRTOS target has no dynamic loader, so every entry point
 * is a graceful no-op: dlopen returns NULL, dlsym returns NULL, dlerror
 * reports the reason.  On Linux/macOS/Android/OHOS the toolchain's own
 * <dlfcn.h> is used instead and this file is not compiled.
 *
 * Also covers the Win32 module-loading surface of src/sysapi.cpp (excluded
 * on FreeRTOS): GetModuleHandleA / LoadLibraryA / FreeLibrary /
 * GetProcAddress / GetModuleFileNameA all degrade to "not found", which is
 * the correct semantics for SOUI's optional plugin/activex probing paths.
 */
#include "dlfcn.h"

#include <windows.h>

extern "C" {

void *dlopen(const char *filename, int flag)
{
    (void)flag;
    if (filename == NULL)
        return (void *)RTLD_DEFAULT; /* main "program" handle */
    return NULL;
}

void *dlsym(void *handle, const char *symbol)
{
    (void)handle;
    (void)symbol;
    return NULL;
}

int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

char *dlerror(void)
{
    /* permanent state: no error, and dlerror() consumed nothing */
    return NULL;
}

HMODULE GetModuleHandleA(LPCSTR name)
{
    /* single "module" = the static image; SOUI passes NULL for it */
    return name == NULL ? (HMODULE)1 : NULL;
}

HMODULE LoadLibraryA(LPCSTR name)
{
    (void)name;
    return NULL;
}

BOOL FreeLibrary(HMODULE h)
{
    (void)h;
    return FALSE;
}

FARPROC GetProcAddress(HMODULE h, LPCSTR name)
{
    (void)h;
    (void)name;
    return NULL;
}

DWORD GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD len)
{
    (void)h;
    if (buf && len)
        buf[0] = 0;
    return 0;
}

} // extern "C"
