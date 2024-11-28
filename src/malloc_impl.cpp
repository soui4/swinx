#include <windows.h>
#include <objbase.h>

class CMalloc : public IMalloc
{
    LONG nRef;
public:
    CMalloc() :nRef(1) {}

    ~CMalloc() {}

public:
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFGUID riid, void** ppvObject) override;
    STDMETHOD_(ULONG, AddRef)(THIS) override;
    STDMETHOD_(ULONG, Release)(THIS) override;
public:
    virtual void* STDMETHODCALLTYPE Alloc(
        /* [annotation][in] */
        _In_  SIZE_T cb) override;

    virtual void* STDMETHODCALLTYPE Realloc(
        /* [annotation][in] */
        _In_opt_  void* pv,
        /* [annotation][in] */
        _In_  SIZE_T cb) override;

    virtual void STDMETHODCALLTYPE Free(
        /* [annotation][in] */
        _In_opt_  void* pv) override;

    virtual SIZE_T STDMETHODCALLTYPE GetSize(
        /* [annotation][in] */
        _In_opt_ _Post_writable_byte_size_(return)  void* pv) override;

    virtual int STDMETHODCALLTYPE DidAlloc(
        /* [annotation][in] */
        _In_opt_  void* pv) override;

    virtual void STDMETHODCALLTYPE HeapMinimize(void) override;
};

/*
 * Copyright 1997 Marcus Meissner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
struct allocator
{
    IMalloc * IMalloc_iface;
    IMallocSpy* spy;
    DWORD spyed_allocations;
    BOOL spy_release_pending; /* CoRevokeMallocSpy called with spyed allocations left */
    void** blocks;
    DWORD blocks_length;
};

static CMalloc sys_malloc;
static struct allocator allocator = {&sys_malloc,0};

static CRITICAL_SECTION allocspy_cs;

static BOOL mallocspy_grow(DWORD length)
{
    void** blocks;

    if (!allocator.blocks) blocks = (void**)LocalAlloc(LMEM_ZEROINIT, length * sizeof(void*));
    else blocks = (void**)LocalReAlloc((HLOCAL)allocator.blocks, length * sizeof(void*), LMEM_ZEROINIT | LMEM_MOVEABLE);
    if (blocks)
    {
        allocator.blocks = blocks;
        allocator.blocks_length = length;
    }

    return blocks != NULL;
}

static void mallocspy_add_mem(void* mem)
{
    void** current;

    if (!mem || (!allocator.blocks_length && !mallocspy_grow(0x1000)))
        return;

    /* Find a free location */
    current = allocator.blocks;
    while (*current)
    {
        current++;
        if (current >= allocator.blocks + allocator.blocks_length)
        {
            DWORD old_length = allocator.blocks_length;
            if (!mallocspy_grow(allocator.blocks_length + 0x1000))
                return;
            current = allocator.blocks + old_length;
        }
    }

    *current = mem;
    allocator.spyed_allocations++;
}

static void** mallocspy_is_allocation_spyed(const void* mem)
{
    void** current = allocator.blocks;

    while (*current != mem)
    {
        current++;
        if (current >= allocator.blocks + allocator.blocks_length)
            return NULL;
    }

    return current;
}

static BOOL mallocspy_remove_spyed_memory(const void* mem)
{
    void** current;

    if (!allocator.blocks_length)
        return FALSE;

    if (!(current = mallocspy_is_allocation_spyed(mem)))
        return FALSE;

    allocator.spyed_allocations--;
    *current = NULL;
    return TRUE;
}


HRESULT CMalloc::QueryInterface(REFIID riid, void** obj)
{
    if (IsEqualIID(IID_IUnknown, riid) || IsEqualIID(IID_IMalloc, riid))
    {
        *obj = &allocator.IMalloc_iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

ULONG CMalloc::AddRef()
{
    return InterlockedIncrement(&nRef);
}

ULONG CMalloc::Release()
{
    long ret = InterlockedDecrement(&nRef);
    if (ret == 0) {
        delete this;
    }
    return ret;
}

void* CMalloc::Alloc(SIZE_T cb)
{
    void* addr;

    if (allocator.spy)
    {
        SIZE_T preAllocResult;

        EnterCriticalSection(&allocspy_cs);
        preAllocResult = allocator.spy->PreAlloc(cb);
        if (cb && !preAllocResult)
        {
            /* PreAlloc can force Alloc to fail, but not if cb == 0 */
            LeaveCriticalSection(&allocspy_cs);
            return NULL;
        }
    }

    addr = HeapAlloc(GetProcessHeap(), 0, cb);

    if (allocator.spy)
    {
        addr = allocator.spy->PostAlloc(addr);
        mallocspy_add_mem(addr);
        LeaveCriticalSection(&allocspy_cs);
    }

    return addr;
}

void* CMalloc::Realloc(void* pv, SIZE_T cb)
{
    void* addr;

    if (allocator.spy)
    {
        void* real_mem;
        BOOL spyed;

        EnterCriticalSection(&allocspy_cs);
        spyed = mallocspy_remove_spyed_memory(pv);
        cb = allocator.spy->PreRealloc(pv, cb, &real_mem, spyed);

        /* check if can release the spy */
        if (allocator.spy_release_pending && !allocator.spyed_allocations)
        {
            allocator.spy->Release();
            allocator.spy_release_pending = FALSE;
            allocator.spy = NULL;
            LeaveCriticalSection(&allocspy_cs);
        }

        if (!cb)
        {
            /* PreRealloc can force Realloc to fail */
            if (allocator.spy)
                LeaveCriticalSection(&allocspy_cs);
            return NULL;
        }

        pv = real_mem;
    }

    if (!pv) addr = HeapAlloc(GetProcessHeap(), 0, cb);
    else if (cb) addr = HeapReAlloc(GetProcessHeap(), 0, pv, cb);
    else
    {
        HeapFree(GetProcessHeap(), 0, pv);
        addr = NULL;
    }

    if (allocator.spy)
    {
        addr = allocator.spy->PostRealloc(addr, TRUE);
        mallocspy_add_mem(addr);
        LeaveCriticalSection(&allocspy_cs);
    }

    return addr;
}

void CMalloc::Free(void* mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;

    if (!mem)
        return;

    if (allocator.spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = mallocspy_remove_spyed_memory(mem);
        spy_active = TRUE;
        mem = allocator.spy->PreFree(mem, spyed_block);
    }

    HeapFree(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        allocator.spy->PostFree(spyed_block);

        /* check if can release the spy */
        if (allocator.spy_release_pending && !allocator.spyed_allocations)
        {
            allocator.spy->Release();
            allocator.spy_release_pending = FALSE;
            allocator.spy = NULL;
        }

        LeaveCriticalSection(&allocspy_cs);
    }
}

/******************************************************************************
 * NOTES
 *  FIXME returns:
 *      win95:  size allocated (4 byte boundaries)
 *      win2k:  size originally requested !!! (allocated on 8 byte boundaries)
 */
SIZE_T CMalloc::GetSize(void* mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;
    SIZE_T size;

    if (!mem)
        return (SIZE_T)-1;

    if (allocator.spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = !!mallocspy_is_allocation_spyed(mem);
        spy_active = TRUE;
        mem = allocator.spy->PreGetSize( mem, spyed_block);
    }

    size = HeapSize(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        size = allocator.spy->PostGetSize(size, spyed_block);
        LeaveCriticalSection(&allocspy_cs);
    }

    return size;
}

INT CMalloc::DidAlloc(void* mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;
    int did_alloc;

    if (!mem)
        return -1;

    if (allocator.spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = !!mallocspy_is_allocation_spyed(mem);
        spy_active = TRUE;
        mem = allocator.spy->PreDidAlloc( mem, spyed_block);
    }

    did_alloc = HeapValidate(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        did_alloc = allocator.spy->PostDidAlloc(mem, spyed_block, did_alloc);
        LeaveCriticalSection(&allocspy_cs);
    }

    return did_alloc;
}

void CMalloc::HeapMinimize()
{
    BOOL spy_active = FALSE;

    if (allocator.spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spy_active = TRUE;
        allocator.spy->PreHeapMinimize();
    }

    if (spy_active)
    {
        allocator.spy->PostHeapMinimize();
        LeaveCriticalSection(&allocspy_cs);
    }
}

/******************************************************************************
 *                CoGetMalloc        (combase.@)
 */
HRESULT WINAPI CoGetMalloc(DWORD context, IMalloc** imalloc)
{
    if (context != MEMCTX_TASK)
    {
        *imalloc = NULL;
        return E_INVALIDARG;
    }

    *imalloc = allocator.IMalloc_iface;

    return S_OK;
}

/***********************************************************************
 *           CoTaskMemAlloc         (combase.@)
 */
void* WINAPI CoTaskMemAlloc(SIZE_T size)
{
    return allocator.IMalloc_iface->Alloc(size);
}

/***********************************************************************
 *           CoTaskMemFree          (combase.@)
 */
void WINAPI CoTaskMemFree(void* ptr)
{
    allocator.IMalloc_iface->Free(ptr);
}

/***********************************************************************
 *           CoTaskMemRealloc        (combase.@)
 */
void* WINAPI CoTaskMemRealloc(void* ptr, SIZE_T size)
{
    return allocator.IMalloc_iface->Realloc(ptr, size);
}

/***********************************************************************
 *           CoRegisterMallocSpy        (combase.@)
 */
HRESULT WINAPI CoRegisterMallocSpy(IMallocSpy* spy)
{
    HRESULT hr = E_INVALIDARG;

    if (!spy) return E_INVALIDARG;

    EnterCriticalSection(&allocspy_cs);

    if (allocator.spy)
        hr = CO_E_OBJISREG;
    else if (SUCCEEDED(spy->QueryInterface(IID_IMallocSpy, (void**)&spy)))
    {
        allocator.spy = spy;
        hr = S_OK;
    }

    LeaveCriticalSection(&allocspy_cs);

    return hr;
}

/***********************************************************************
 *           CoRevokeMallocSpy (combase.@)
 */
HRESULT WINAPI CoRevokeMallocSpy(void)
{
    HRESULT hr = S_OK;

    EnterCriticalSection(&allocspy_cs);

    if (!allocator.spy)
        hr = CO_E_OBJNOTREG;
    else if (allocator.spyed_allocations)
    {
        allocator.spy_release_pending = TRUE;
        hr = E_ACCESSDENIED;
    }
    else
    {
        allocator.spy->Release();
        allocator.spy = NULL;
    }

    LeaveCriticalSection(&allocspy_cs);

    return hr;
}
