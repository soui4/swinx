#ifndef _HANDLE_H_
#define _HANDLE_H_

#include <mutex>

typedef void (*FreeHandlePtr)(void *ptr);
typedef struct _Handle
{
    std::recursive_mutex mutex;
    int type;
    int nRef;
    void *ptr;
    FreeHandlePtr cbFree;

    _Handle(int _type, void *_ptr, FreeHandlePtr _cbFree);
    virtual ~_Handle();
} * HANDLE;

enum
{
    GDI_OBJ = 1,

    FILE_OBJ = 20,
    HEAP_OBJ = 21,
    SYN_OBJ = 100,
    FILEMAP_OBJ = 200,
};

HANDLE WINAPI AddHandleRef(HANDLE hHandle);

BOOL WINAPI CloseHandle(HANDLE h);

HANDLE InitHandle(int type, void *ptr, FreeHandlePtr cbFree);

#endif //_HANDLE_H_