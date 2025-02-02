#include <windows.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <list>
#include "handle.h"
#include "SConnection.h"
#include "debug.h"
#define kLogTag "memory"

struct MemBlock
{
    size_t len;
    size_t flushLen;
    size_t allocSize;
    void *ptr;
    DWORD flags;
};

struct HeapInfo
{
    HeapInfo(DWORD opt_, size_t dwInitialSize_, size_t dwMaximumSize_)
        : option(opt_)
        , dwInitialSize(dwInitialSize_)
        , dwMaximumSize(dwMaximumSize_)
    {
    }
    DWORD option;
    size_t dwInitialSize;
    size_t dwMaximumSize;
    std::list<MemBlock> lstMem;
};

HANDLE GetProcessHeap()
{
    return SConnMgr::instance()->getProcessHeap();
}

static void FreeHeapInfo(void *ptr)
{
    HeapInfo *info = (HeapInfo *)ptr;
    for (auto it : info->lstMem)
    {
        if ((it.flags & HEAP_CREATE_ENABLE_EXECUTE) && (it.flushLen > 0))
            mprotect((char *)it.ptr - sizeof(HANDLE), it.flushLen + sizeof(HANDLE), PROT_WRITE | PROT_READ); // restore to write.

        free((char *)it.ptr - sizeof(HANDLE));
    }
    delete info;
}

HANDLE
HeapCreate(DWORD flOptions, size_t dwInitialSize, size_t dwMaximumSize)
{
    return InitHandle(HEAP_OBJ, new HeapInfo(flOptions, dwInitialSize, dwMaximumSize), FreeHeapInfo);
}

BOOL WINAPI HeapDestroy(HANDLE hHeap)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return FALSE;
    return CloseHandle(hHeap);
}

BOOL WINAPI HeapLock(HANDLE hHeap)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return FALSE;
    hHeap->mutex.lock();
    return TRUE;
}

BOOL WINAPI HeapUnlock(HANDLE hHeap)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return FALSE;
    hHeap->mutex.unlock();
    return TRUE;
}

LPVOID
HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(hHeap->mutex);
    HeapInfo *info = (HeapInfo *)hHeap->ptr;
    assert(info);
    MemBlock block;
    block.len = dwBytes;
    block.ptr = nullptr;
    block.flags = dwFlags;
    block.flushLen = 0;
    block.allocSize = 0;
    int pageSize = sysconf(_SC_PAGE_SIZE);

    size_t bufLen = (dwBytes + sizeof(HANDLE) + pageSize - 1) / pageSize * pageSize;
    void *ptr = nullptr;
    int ret = posix_memalign(&ptr, pageSize, bufLen);
    if (ret != 0)
    {
        return nullptr;
    }
    memcpy(ptr, &hHeap, sizeof(HANDLE));
    block.allocSize = bufLen - sizeof(HANDLE);
    block.ptr = (char *)ptr + sizeof(HANDLE);
    if (dwFlags & HEAP_ZERO_MEMORY)
    {
        memset(block.ptr, 0, dwBytes);
    }
    // for test, set the next memory to 0
    memset((char *)block.ptr + dwBytes, 0, block.allocSize - dwBytes);
    info->lstMem.push_back(block);
    return block.ptr;
}

BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return FALSE;

    std::lock_guard<std::recursive_mutex> lock(hHeap->mutex);
    HeapInfo *info = (HeapInfo *)hHeap->ptr;
    if (!info)
        return FALSE;
    for (auto it = info->lstMem.begin(); it != info->lstMem.end(); it++)
    {
        if (it->ptr == lpMem)
        {
            if ((it->flags & HEAP_CREATE_ENABLE_EXECUTE) && (it->flushLen > 0))
            {
                int ret = mprotect((char *)it->ptr - sizeof(HANDLE), it->flushLen + sizeof(HANDLE), PROT_WRITE | PROT_READ); // restore to write.
                if (ret != 0)
                {
                    SLOG_FMTI("warn,mprotect ret %d", ret);
                }
            }

            free((char *)lpMem - sizeof(HANDLE));
            info->lstMem.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

LPVOID WINAPI HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(hHeap->mutex);
    HeapInfo *info = (HeapInfo *)hHeap->ptr;
    if (!info)
        return FALSE;
    for (auto it = info->lstMem.begin(); it != info->lstMem.end(); it++)
    {
        if (it->ptr == lpMem)
        {
            if (it->allocSize >= dwBytes)
            {
                if ((dwBytes > it->len) && (dwFlags & HEAP_ZERO_MEMORY))
                {
                    memset((char *)it->ptr + it->len, 0, dwBytes - it->len);
                }
                it->len = dwBytes;
                return it->ptr;
            }
            else
            {

                int pageSize = sysconf(_SC_PAGE_SIZE);
                size_t bufLen = (dwBytes + sizeof(HANDLE) + pageSize - 1) / pageSize * pageSize;
                void *ptr = nullptr;
                int ret = posix_memalign(&ptr, pageSize, bufLen);
                if (ret != 0)
                {
                    return nullptr;
                }
                memcpy(ptr, &hHeap, sizeof(HANDLE));
                ptr = (char *)ptr + sizeof(HANDLE);
                if (dwFlags & HEAP_ZERO_MEMORY)
                {
                    memset(ptr, 0, dwBytes);
                }
                memcpy(ptr, it->ptr, std::min(it->len, dwBytes));
                if ((it->flags & HEAP_CREATE_ENABLE_EXECUTE) && (it->flushLen > 0))
                {
                    mprotect((char *)it->ptr - sizeof(HANDLE), it->flushLen + sizeof(HANDLE), PROT_WRITE | PROT_READ); // restore to write.
                    it->flushLen = 0;
                }
                free((char *)it->ptr - sizeof(HANDLE));
                it->ptr = ptr;
                it->len = dwBytes;
                it->allocSize = bufLen - sizeof(HANDLE);
                return it->ptr;
            }
        }
    }
    return nullptr;
}

SIZE_T WINAPI HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    if (!hHeap || hHeap->type != HEAP_OBJ)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(hHeap->mutex);
    HeapInfo *info = (HeapInfo *)hHeap->ptr;
    if (!info)
        return FALSE;
    for (auto it = info->lstMem.begin(); it != info->lstMem.end(); it++)
    {
        if (it->ptr == lpMem)
        {
            return it->len;
        }
    }
    return 0;
}

BOOL WINAPI HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    const char *pBuf = (const char *)lpMem;
    pBuf -= sizeof(HANDLE);
    if (IsBadWritePtr(pBuf, sizeof(HANDLE)))
        return FALSE;
    HANDLE hHeap2;
    memcpy(&hHeap2, pBuf, sizeof(HANDLE));
    return hHeap2 == hHeap;
}

BOOL FlushInstructionCache(HANDLE hProcess, LPCVOID lpMem, size_t dwSize)
{
    const char *pBuf = (const char *)lpMem;
    pBuf -= sizeof(HANDLE);
    if (IsBadWritePtr(pBuf, sizeof(HANDLE)))
        return FALSE;
    HANDLE hHeap;
    memcpy(&hHeap, pBuf, sizeof(HANDLE));

    std::lock_guard<std::recursive_mutex> lock(hHeap->mutex);
    HeapInfo *info = (HeapInfo *)hHeap->ptr;
    if (!info)
        return FALSE;
    for (auto it = info->lstMem.begin(); it != info->lstMem.end(); it++)
    {
        if (it->ptr == lpMem && (it->flags & HEAP_CREATE_ENABLE_EXECUTE))
        {
            if (dwSize > it->len)
                return FALSE;
            int pageSize = sysconf(_SC_PAGE_SIZE);
            DWORD dwMapSize = (dwSize + sizeof(HANDLE) + pageSize - 1) / pageSize * pageSize;
            int ret = mprotect((char *)lpMem - sizeof(HANDLE), dwMapSize, PROT_EXEC | PROT_READ | PROT_WRITE);
            if (ret == 0)
            {
                it->flushLen = dwMapSize - sizeof(HANDLE);
            }
            else
            {
                SLOG_FMTW("mprotect ret %d,error=%d", ret, errno);
            }
            return ret == 0;
        }
    }
    return FALSE;
}

//-------------------------------------------------------------------
#pragma pack(push, 1)
struct local_header
{
    WORD magic;
    void *ptr;
    BYTE flags;
    BYTE lock;
};
#pragma pack(pop)

#define MAGIC_LOCAL_USED 0x5342
/* align the storage needed for the HLOCAL on an 8-byte boundary thus
 * LocalAlloc/LocalReAlloc'ing with LMEM_MOVEABLE of memory with
 * size = 8*k, where k=1,2,3,... allocs exactly the given size.
 * The Minolta DiMAGE Image Viewer heavily relies on this, corrupting
 * the output jpeg's > 1 MB if not */
#define HLOCAL_STORAGE (sizeof(HLOCAL) * 2)

static inline struct local_header *get_header(HLOCAL hmem)
{
    return (struct local_header *)((char *)hmem - 2);
}

static inline HLOCAL get_handle(struct local_header *header)
{
    return (HLOCAL)&header->ptr;
}

static inline BOOL is_pointer(HLOCAL hmem)
{
    return !((ULONG_PTR)hmem & 2);
}

/***********************************************************************
 *           GlobalAlloc   (kernelbase.@)
 */
HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size)
{
    /* mask out obsolete flags */
    flags &= ~(GMEM_NOCOMPACT | GMEM_NOT_BANKED | GMEM_NOTIFY);

    /* LocalAlloc allows a 0-size fixed block, but GlobalAlloc doesn't */
    if (!(flags & GMEM_MOVEABLE) && !size)
        size = 1;

    return LocalAlloc(flags, size);
}

/***********************************************************************
 *           GlobalFree   (kernelbase.@)
 */
HGLOBAL WINAPI GlobalFree(HLOCAL hmem)
{
    return LocalFree(hmem);
}

/***********************************************************************
 *           LocalAlloc   (kernelbase.@)
 */
HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T size)
{
    struct local_header *header;
    DWORD heap_flags = 0;
    void *ptr;

    if (flags & LMEM_ZEROINIT)
        heap_flags = HEAP_ZERO_MEMORY;

    if (!(flags & LMEM_MOVEABLE)) /* pointer */
    {
        ptr = HeapAlloc(GetProcessHeap(), heap_flags, size);
        return (HLOCAL)ptr;
    }

    if (size > INT_MAX - HLOCAL_STORAGE)
    {
        SetLastError(ERROR_OUTOFMEMORY);
        return 0;
    }
    if (!(header = (local_header *)HeapAlloc(GetProcessHeap(), 0, sizeof(*header))))
        return 0;

    header->magic = MAGIC_LOCAL_USED;
    header->flags = flags >> 8;
    header->lock = 0;

    if (size)
    {
        if (!(ptr = HeapAlloc(GetProcessHeap(), heap_flags, size + HLOCAL_STORAGE)))
        {
            HeapFree(GetProcessHeap(), 0, header);
            return 0;
        }
        *(HLOCAL *)ptr = get_handle(header);
        header->ptr = (char *)ptr + HLOCAL_STORAGE;
    }
    else
        header->ptr = NULL;

    return get_handle(header);
}

/***********************************************************************
 *           LocalFree   (kernelbase.@)
 */
HLOCAL WINAPI LocalFree(HLOCAL hmem)
{
    struct local_header *header;
    HLOCAL ret;

    HeapLock(GetProcessHeap());
    ret = 0;
    if (is_pointer(hmem)) /* POINTER */
    {
        if (!HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, hmem))
        {
            SetLastError(ERROR_INVALID_HANDLE);
            ret = hmem;
        }
    }
    else /* HANDLE */
    {
        header = get_header(hmem);
        if (header->magic == MAGIC_LOCAL_USED)
        {
            header->magic = 0xdead;
            if (header->ptr)
            {
                if (!HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, (char *)header->ptr - HLOCAL_STORAGE))
                    ret = hmem;
            }
            if (!HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, header))
                ret = hmem;
        }
        else
        {
            SetLastError(ERROR_INVALID_HANDLE);
            ret = hmem;
        }
    }
    HeapUnlock(GetProcessHeap());
    return ret;
}

/***********************************************************************
 *           LocalLock   (kernelbase.@)
 */
LPVOID WINAPI LocalLock(HLOCAL hmem)
{
    void *ret = NULL;

    if (is_pointer(hmem))
    {
        volatile char *p = (char *)hmem;
        *p |= 0;
        return hmem;
    }

    HeapLock(GetProcessHeap());
    {
        struct local_header *header = get_header(hmem);
        if (header->magic == MAGIC_LOCAL_USED)
        {
            ret = header->ptr;
            if (!header->ptr)
                SetLastError(ERROR_DISCARDED);
            else if (header->lock < LMEM_LOCKCOUNT)
                header->lock++;
        }
        else
        {
            SetLastError(ERROR_INVALID_HANDLE);
        }
    }
    HeapUnlock(GetProcessHeap());
    return ret;
}

/***********************************************************************
 *           LocalReAlloc   (kernelbase.@)
 */
HLOCAL WINAPI LocalReAlloc(HLOCAL hmem, SIZE_T size, UINT flags)
{
    struct local_header *header;
    void *ptr;
    HLOCAL ret = 0;
    DWORD heap_flags = (flags & LMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0;

    HeapLock(GetProcessHeap());
    if (flags & LMEM_MODIFY) /* modify flags */
    {
        if (is_pointer(hmem) && (flags & LMEM_MOVEABLE))
        {
            /* make a fixed block moveable
             * actually only NT is able to do this. But it's soo simple
             */
            if (hmem == 0)
            {
                SetLastError(ERROR_NOACCESS);
            }
            else
            {
                size = HeapSize(GetProcessHeap(), 0, hmem);
                ret = LocalAlloc(flags, size);
                ptr = LocalLock(ret);
                memcpy(ptr, hmem, size);
                LocalUnlock(ret);
                LocalFree(hmem);
            }
        }
        else if (!is_pointer(hmem) && (flags & LMEM_DISCARDABLE))
        {
            /* change the flags to make our block "discardable" */
            header = get_header(hmem);
            header->flags |= LMEM_DISCARDABLE >> 8;
            ret = hmem;
        }
        else
            SetLastError(ERROR_INVALID_PARAMETER);
    }
    else
    {
        if (is_pointer(hmem))
        {
            /* reallocate fixed memory */
            if (!(flags & LMEM_MOVEABLE))
            {
                heap_flags |= HEAP_REALLOC_IN_PLACE_ONLY;
                ret = (HLOCAL)HeapReAlloc(GetProcessHeap(), heap_flags, hmem, size);
            }
            else
            {
                ret = LocalAlloc(flags, size);
                void *dst = LocalLock(ret);
                void *src = (void *)hmem;
                size_t sz_src = HeapSize(GetProcessHeap(), 0, src);
                memcpy(dst, src, std::min(sz_src, size));
                LocalUnlock(ret);
                HeapFree(GetProcessHeap(), 0, src);
            }
        }
        else
        {
            /* reallocate a moveable block */
            header = get_header(hmem);
            if (size != 0)
            {
                if (size <= INT_MAX - HLOCAL_STORAGE)
                {
                    if (header->ptr)
                    {
                        if ((ptr = HeapReAlloc(GetProcessHeap(), heap_flags, (char *)header->ptr - HLOCAL_STORAGE, size + HLOCAL_STORAGE)))
                        {
                            header->ptr = (char *)ptr + HLOCAL_STORAGE;
                            ret = hmem;
                        }
                    }
                    else
                    {
                        if ((ptr = HeapAlloc(GetProcessHeap(), heap_flags, size + HLOCAL_STORAGE)))
                        {
                            *(HLOCAL *)ptr = hmem;
                            header->ptr = (char *)ptr + HLOCAL_STORAGE;
                            ret = hmem;
                        }
                    }
                }
                else
                    SetLastError(ERROR_OUTOFMEMORY);
            }
            else
            {
                if (header->lock == 0)
                {
                    if (header->ptr)
                    {
                        HeapFree(GetProcessHeap(), 0, (char *)header->ptr - HLOCAL_STORAGE);
                        header->ptr = NULL;
                    }
                    ret = hmem;
                }
                // else WARN("not freeing memory associated with locked handle\n");
            }
        }
    }
    HeapUnlock(GetProcessHeap());
    return ret;
}

/***********************************************************************
 *           LocalUnlock   (kernelbase.@)
 */
BOOL WINAPI LocalUnlock(HLOCAL hmem)
{
    BOOL ret = FALSE;

    if (is_pointer(hmem))
    {
        SetLastError(ERROR_NOT_LOCKED);
        return TRUE;
    }

    HeapLock(GetProcessHeap());
    {
        struct local_header *header = get_header(hmem);
        if (header->magic == MAGIC_LOCAL_USED)
        {
            if (header->lock)
            {
                header->lock--;
                ret = (header->lock != 0);
                if (!ret)
                    SetLastError(NO_ERROR);
            }
            else
            {
                SetLastError(ERROR_NOT_LOCKED);
            }
        }
        else
        {
            SetLastError(ERROR_INVALID_HANDLE);
        }
    }
    HeapUnlock(GetProcessHeap());
    return ret;
}

UINT WINAPI LocalSize(HLOCAL hMem)
{
    UINT ret = 0;
    HeapLock(GetProcessHeap());
    if (is_pointer(hMem))
    {
        ret = HeapSize(GetProcessHeap(), 0, hMem);
    }
    else
    {
        struct local_header *header = get_header(hMem);
        if (header->magic == MAGIC_LOCAL_USED)
        {
            if (header->ptr)
                ret = HeapSize(GetProcessHeap(), 0, (char *)header->ptr - HLOCAL_STORAGE) - HLOCAL_STORAGE;
            else
                ret = 0;
        }
        else
        {
            SetLastError(ERROR_INVALID_HANDLE);
        }
    }
    HeapUnlock(GetProcessHeap());
    return ret;
}

UINT WINAPI LocalFlags(_In_ HLOCAL hMem)
{
    HeapLock(GetProcessHeap());
    struct local_header *header = get_header(hMem);
    UINT ret = header->flags;
    HeapUnlock(GetProcessHeap());
    return ret;
}

HLOCAL LocalHandle(LPCVOID pmem)
{
    HLOCAL handle = 0;
    if (!pmem)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    HeapUnlock(GetProcessHeap());
    do
    {
        /* note that if pmem is a pointer to a block allocated by        */
        /* GlobalAlloc with GMEM_MOVEABLE then magic test in HeapValidate  */
        /* will fail.                                                      */
        if (is_pointer((HLOCAL)pmem))
        {
            if (HeapValidate(GetProcessHeap(), HEAP_NO_SERIALIZE, pmem))
            {
                handle = (HLOCAL)pmem; /* valid fixed block */
                break;
            }
        }
        if (IsBadReadPtr((char *)pmem - HLOCAL_STORAGE, HLOCAL_STORAGE))
        {
            SetLastError(ERROR_INVALID_HANDLE);
            break;
        }
        handle = *(HLOCAL *)((char *)pmem - HLOCAL_STORAGE);
        local_header *header = get_header(handle);
        if (header->magic != MAGIC_LOCAL_USED)
        {
            handle = 0;
            SetLastError(ERROR_INVALID_HANDLE);
        }
    } while (0);
    HeapUnlock(GetProcessHeap());
    return handle;
}

BOOL WINAPI GlobalUnlock(_In_ HGLOBAL hMem)
{
    return LocalUnlock(hMem);
}

LPVOID
WINAPI
GlobalLock(_In_ HGLOBAL hMem)
{
    return LocalLock(hMem);
}

SIZE_T
WINAPI
GlobalSize(_In_ HGLOBAL hMem)
{
    return LocalSize(hMem);
}

HGLOBAL WINAPI GlobalReAlloc(_In_ HGLOBAL hMem, _In_ SIZE_T dwBytes, _In_ UINT uFlags)
{
    return LocalReAlloc(hMem, dwBytes, uFlags);
}

UINT WINAPI GlobalFlags(_In_ HGLOBAL hMem)
{
    return LocalFlags(hMem);
}

HLOCAL GlobalHandle(LPCVOID pMem)
{
    return LocalHandle(pMem);
}
