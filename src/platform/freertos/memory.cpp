/*
 * memory.cpp -- 进程堆 / 全局局部内存 / 共享内存（KERNEL32 内存族）
 *
 * 对应 posix 侧的 src/memory.cpp + src/sharedmem.cpp。裸机上没有 brk 后台堆、
 * 没有 shm_open，所以堆就是一个 malloc 记账表，SharedMemory 退化成进程内堆块。
 *
 * 记账表（HeapInfoFr::blocks）不是调试设施，而是语义要求：SOUI 的 CMalloc 用
 * HeapSize/HeapValidate 实现 GetSize/DidAlloc，全局/局部内存也用同一张表登记，
 * 否则这两类查询对 GlobalAlloc 出来的块恒失败。
 *
 * 局部/全局内存是「指针即句柄」语义：没有可移动句柄表，GlobalLock 直接返回入参，
 * GlobalUnlock 恒 FALSE（Win32 对固定块本来也如此）。所有使用点要么用固定块，要么
 * 把值透传过 GlobalLock —— 本端口唯一的调用方 SOUI 两者都满足。
 */
#include <sfr_config.h>
#include <windows.h>
#include "sysapi.h"
#include "sharedmem.h"
#include <log.h>
#define kLogTag "memory"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include <mutex>
#include <string>
#include <vector>

//=============================================================================
// Process heap (malloc-backed replacement for src/memory.cpp's POSIX heap)
//=============================================================================

struct HeapBlockFr
{
    void *ptr;
    size_t len;
};

struct HeapInfoFr
{
    std::recursive_mutex mutex;
    std::vector<HeapBlockFr> blocks;
};

HANDLE GetProcessHeap()
{
    // leak-on-purpose: the default heap lives for the process lifetime
    static HeapInfoFr *s_heap = new HeapInfoFr();
    return (HANDLE)s_heap;
}

LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, size_t dwBytes)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info)
        return nullptr;
    void *ptr = malloc(dwBytes);
    if (!ptr)
        return nullptr;
    if (dwFlags & HEAP_ZERO_MEMORY)
        memset(ptr, 0, dwBytes);
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    HeapBlockFr block = {ptr, dwBytes};
    info->blocks.push_back(block);
    return ptr;
}

BOOL HeapFree(HANDLE hHeap, DWORD dwFlags __attribute__((unused)), LPVOID lpMem)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            info->blocks.erase(it);
            free(lpMem);
            return TRUE;
        }
    }
    return FALSE;
}

LPVOID HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, size_t dwBytes)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return nullptr;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    size_t oldLen = 0;
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            oldLen = it->len;
            break;
        }
    }
    void *ptr = malloc(dwBytes);
    if (!ptr)
        return nullptr;
    memcpy(ptr, lpMem, oldLen < dwBytes ? oldLen : dwBytes);
    if ((dwFlags & HEAP_ZERO_MEMORY) && dwBytes > oldLen)
        memset((char *)ptr + oldLen, 0, dwBytes - oldLen);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == lpMem)
        {
            it->ptr = ptr;
            it->len = dwBytes;
            break;
        }
    }
    free(lpMem);
    return ptr;
}

//=============================================================================
// SharedMemory -- single-process heap fallback (mirrors the mobile heap
// fallback in src/sharedmem.cpp; bare metal has no shm_open / temp files)
//=============================================================================

namespace swinx
{

    SharedMemory::~SharedMemory()
    {
        if (m_pBuf == (LPBYTE)-1)
            return;
        if (m_bHeap)
        {
            // the refcount header word sits in front of the buffer
            delete[](m_pBuf - sizeof(uint32_t));
        }
        delete m_rwlock;
        m_pBuf = (LPBYTE)-1;
    }

    SharedMemory::InitStat SharedMemory::init(const char *name, uint32_t size)
    {
        assert(m_rwlock == nullptr);
        TSemRwLock<kSharedNumber> *rwlock = new TSemRwLock<kSharedNumber>();
        if (!rwlock->init(name))
        {
            delete rwlock;
            return Failed;
        }
        m_rwlock = rwlock;

        const uint32_t memSize = size + sizeof(uint32_t);
        LPBYTE ptr = new (std::nothrow) uint8_t[memSize];
        if (ptr == nullptr)
        {
            delete rwlock;
            m_rwlock = nullptr;
            return Failed;
        }
        memset(ptr, 0, memSize);
        m_bHeap = true;

        nRef = 1;
        m_pBuf = ptr + sizeof(uint32_t);
        shmid = -1;
        m_dwSize = size;
        m_name = name ? name : "";
        m_bDetached = false;
        SLOG_FMTD("open share memory (freertos, heap fallback), name=%s, size=%u\n", m_name.c_str(), size);
        return Created;
    }

    void SharedMemory::detach()
    {
        m_bDetached = true;
    }

} // namespace swinx

extern "C" {

// ---- heap extras (Linux: memory.cpp; HeapAlloc/HeapReAlloc/HeapFree above) -
HANDLE HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize)
{
    (void)flOptions;
    (void)dwInitialSize;
    (void)dwMaximumSize;
    /* leak-on-purpose: heaps are never destroyed on bare metal */
    return (HANDLE)new HeapInfoFr();
}

BOOL HeapDestroy(HANDLE hHeap)
{
    (void)hHeap; /* deliberately not freed: see HeapCreate note */
    return TRUE;
}

SIZE_T HeapSize(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
        if (it->ptr == lpMem)
            return it->len;
    return 0;
}

BOOL HeapValidate(HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    /* CMalloc::DidAlloc() uses the result as "is in this heap" */
    HeapInfoFr *info = (HeapInfoFr *)hHeap;
    if (!info || !lpMem)
        return FALSE;
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
        if (it->ptr == lpMem)
            return TRUE;
    return FALSE;
}

// ---- global/local alloc (Linux: memory.cpp, Wine-derived) ------------------
// Pointer-only semantics; GMEM/LMEM_ZEROINIT honored, MOVEABLE ignored (the
// block stays a plain pointer, which GlobalLock passes straight through).
// Blocks are registered in the process heap's block list so HeapSize /
// HeapValidate (CMalloc::GetSize / CMalloc::DidAlloc) see them too.
static HLOCAL FrAlloc(UINT flags, SIZE_T size)
{
    if (size == 0)
        size = 1; /* 0-size fixed blocks are allowed by LocalAlloc */
    void *ptr = malloc(size);
    if (!ptr)
        return NULL;
    if (flags & LMEM_ZEROINIT)
        memset(ptr, 0, size);
    HeapInfoFr *info = (HeapInfoFr *)GetProcessHeap();
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    HeapBlockFr block = {ptr, size};
    info->blocks.push_back(block);
    return (HLOCAL)ptr;
}

static void FrUnregister(LPVOID ptr)
{
    HeapInfoFr *info = (HeapInfoFr *)GetProcessHeap();
    std::lock_guard<std::recursive_mutex> lock(info->mutex);
    for (auto it = info->blocks.begin(); it != info->blocks.end(); ++it)
    {
        if (it->ptr == ptr)
        {
            info->blocks.erase(it);
            return;
        }
    }
}

HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes) { return FrAlloc(uFlags, dwBytes); }

HGLOBAL GlobalFree(HGLOBAL hMem)
{
    if (hMem)
    {
        FrUnregister(hMem);
        free(hMem);
    }
    return NULL;
}

LPVOID GlobalLock(HGLOBAL hMem)
{
    return hMem; /* fixed block: the handle IS the pointer */
}

BOOL GlobalUnlock(HGLOBAL hMem)
{
    (void)hMem;
    return FALSE; /* Win32: fixed blocks stay "locked" */
}

HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes) { return FrAlloc(uFlags, uBytes); }

HLOCAL LocalFree(HLOCAL hMem)
{
    if (hMem)
    {
        FrUnregister(hMem);
        free(hMem);
    }
    return NULL;
}

LPVOID LocalLock(HLOCAL hMem) { return hMem; }

BOOL LocalUnlock(HLOCAL hMem)
{
    (void)hMem;
    return FALSE;
}

HLOCAL LocalReAlloc(HLOCAL hMem, SIZE_T uBytes, UINT uFlags)
{
    if (!hMem)
        return FrAlloc(uFlags, uBytes);
    size_t oldLen = HeapSize(GetProcessHeap(), 0, hMem);
    HLOCAL ret = FrAlloc(uFlags, uBytes); /* zero-init covers the grown tail */
    if (!ret)
        return NULL;
    if (!(uFlags & LMEM_ZEROINIT) && oldLen)
        memcpy(ret, hMem, oldLen < uBytes ? oldLen : uBytes);
    FrUnregister(hMem);
    free(hMem);
    return ret;
}

} /* extern "C" */
