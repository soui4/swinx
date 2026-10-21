#include <windows.h>
#include <objbase.h>

class CMalloc : public IMalloc {
    LONG nRef;

  public:
    CMalloc()
        : nRef(1)
    {
    }

    virtual ~CMalloc()
    {
    }

  public:
    STDMETHOD_(HRESULT, QueryInterface)(THIS_ REFGUID riid, void **ppvObject) override;
    STDMETHOD_(ULONG, AddRef)(THIS) override;
    STDMETHOD_(ULONG, Release)(THIS) override;

  public:
    virtual void *STDMETHODCALLTYPE Alloc(
        /* [annotation][in] */
        _In_ SIZE_T cb) override;

    virtual void *STDMETHODCALLTYPE Realloc(
        /* [annotation][in] */
        _In_opt_ void *pv,
        /* [annotation][in] */
        _In_ SIZE_T cb) override;

    virtual void STDMETHODCALLTYPE Free(
        /* [annotation][in] */
        _In_opt_ void *pv) override;

    virtual SIZE_T STDMETHODCALLTYPE GetSize(
        /* [annotation][in] */
        _In_opt_ _Post_writable_byte_size_(return ) void *pv) override;

    virtual int STDMETHODCALLTYPE DidAlloc(
        /* [annotation][in] */
        _In_opt_ void *pv) override;

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
    IMalloc *IMalloc_iface;
    IMallocSpy *spy;
    DWORD spyed_allocations;
    BOOL spy_release_pending; /* CoRevokeMallocSpy called with spyed allocations left */
    void **blocks;
    DWORD blocks_length;
};

// Function-local static, NOT a namespace-scope object: bare metal (FreeRTOS)
// startup.c never runs __libc_init_array, so a namespace-scope CMalloc would
// keep its vtable pointer unset.  Local statics construct on first use.
static CMalloc &sys_malloc()
{
    static CMalloc *s = new CMalloc();
    return *s;
}

static struct allocator &sys_allocator()
{
    // function-local static: see sys_malloc() note -- bare metal never runs
    // global dynamic initializers, so this must construct on first use.
    static allocator *s = new allocator{ &sys_malloc(), NULL, 0, FALSE, NULL, 0 };
    return *s;
}

static CRITICAL_SECTION allocspy_cs;

static BOOL mallocspy_grow(DWORD length)
{
    void **blocks;

    if (!sys_allocator().blocks)
        blocks = (void **)LocalAlloc(LMEM_ZEROINIT, length * sizeof(void *));
    else
        blocks = (void **)LocalReAlloc((HLOCAL)sys_allocator().blocks, length * sizeof(void *), LMEM_ZEROINIT | LMEM_MOVEABLE);
    if (blocks)
    {
        sys_allocator().blocks = blocks;
        sys_allocator().blocks_length = length;
    }

    return blocks != NULL;
}

static void mallocspy_add_mem(void *mem)
{
    void **current;

    if (!mem || (!sys_allocator().blocks_length && !mallocspy_grow(0x1000)))
        return;

    /* Find a free location */
    current = sys_allocator().blocks;
    while (*current)
    {
        current++;
        if (current >= sys_allocator().blocks + sys_allocator().blocks_length)
        {
            DWORD old_length = sys_allocator().blocks_length;
            if (!mallocspy_grow(sys_allocator().blocks_length + 0x1000))
                return;
            current = sys_allocator().blocks + old_length;
        }
    }

    *current = mem;
    sys_allocator().spyed_allocations++;
}

static void **mallocspy_is_allocation_spyed(const void *mem)
{
    void **current = sys_allocator().blocks;

    while (*current != mem)
    {
        current++;
        if (current >= sys_allocator().blocks + sys_allocator().blocks_length)
            return NULL;
    }

    return current;
}

static BOOL mallocspy_remove_spyed_memory(const void *mem)
{
    void **current;

    if (!sys_allocator().blocks_length)
        return FALSE;

    if (!(current = mallocspy_is_allocation_spyed(mem)))
        return FALSE;

    sys_allocator().spyed_allocations--;
    *current = NULL;
    return TRUE;
}

HRESULT CMalloc::QueryInterface(REFIID riid, void **obj)
{
    if (IsEqualIID(IID_IUnknown, riid) || IsEqualIID(IID_IMalloc, riid))
    {
        *obj = &sys_allocator().IMalloc_iface;
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
    if (ret == 0)
    {
        delete this;
    }
    return ret;
}

void *CMalloc::Alloc(SIZE_T cb)
{
    void *addr;

    if (sys_allocator().spy)
    {
        SIZE_T preAllocResult;

        EnterCriticalSection(&allocspy_cs);
        preAllocResult = sys_allocator().spy->PreAlloc(cb);
        if (cb && !preAllocResult)
        {
            /* PreAlloc can force Alloc to fail, but not if cb == 0 */
            LeaveCriticalSection(&allocspy_cs);
            return NULL;
        }
    }

    addr = HeapAlloc(GetProcessHeap(), 0, cb);

    if (sys_allocator().spy)
    {
        addr = sys_allocator().spy->PostAlloc(addr);
        mallocspy_add_mem(addr);
        LeaveCriticalSection(&allocspy_cs);
    }

    return addr;
}

void *CMalloc::Realloc(void *pv, SIZE_T cb)
{
    void *addr;

    if (sys_allocator().spy)
    {
        void *real_mem;
        BOOL spyed;

        EnterCriticalSection(&allocspy_cs);
        spyed = mallocspy_remove_spyed_memory(pv);
        cb = sys_allocator().spy->PreRealloc(pv, cb, &real_mem, spyed);

        /* check if can release the spy */
        if (sys_allocator().spy_release_pending && !sys_allocator().spyed_allocations)
        {
            sys_allocator().spy->Release();
            sys_allocator().spy_release_pending = FALSE;
            sys_allocator().spy = NULL;
            LeaveCriticalSection(&allocspy_cs);
        }

        if (!cb)
        {
            /* PreRealloc can force Realloc to fail */
            if (sys_allocator().spy)
                LeaveCriticalSection(&allocspy_cs);
            return NULL;
        }

        pv = real_mem;
    }

    if (!pv)
        addr = HeapAlloc(GetProcessHeap(), 0, cb);
    else if (cb)
        addr = HeapReAlloc(GetProcessHeap(), 0, pv, cb);
    else
    {
        HeapFree(GetProcessHeap(), 0, pv);
        addr = NULL;
    }

    if (sys_allocator().spy)
    {
        addr = sys_allocator().spy->PostRealloc(addr, TRUE);
        mallocspy_add_mem(addr);
        LeaveCriticalSection(&allocspy_cs);
    }

    return addr;
}

void CMalloc::Free(void *mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;

    if (!mem)
        return;

    if (sys_allocator().spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = mallocspy_remove_spyed_memory(mem);
        spy_active = TRUE;
        mem = sys_allocator().spy->PreFree(mem, spyed_block);
    }

    HeapFree(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        sys_allocator().spy->PostFree(spyed_block);

        /* check if can release the spy */
        if (sys_allocator().spy_release_pending && !sys_allocator().spyed_allocations)
        {
            sys_allocator().spy->Release();
            sys_allocator().spy_release_pending = FALSE;
            sys_allocator().spy = NULL;
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
SIZE_T CMalloc::GetSize(void *mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;
    SIZE_T size;

    if (!mem)
        return (SIZE_T)-1;

    if (sys_allocator().spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = !!mallocspy_is_allocation_spyed(mem);
        spy_active = TRUE;
        mem = sys_allocator().spy->PreGetSize(mem, spyed_block);
    }

    size = HeapSize(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        size = sys_allocator().spy->PostGetSize(size, spyed_block);
        LeaveCriticalSection(&allocspy_cs);
    }

    return size;
}

INT CMalloc::DidAlloc(void *mem)
{
    BOOL spyed_block = FALSE, spy_active = FALSE;
    int did_alloc;

    if (!mem)
        return -1;

    if (sys_allocator().spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spyed_block = !!mallocspy_is_allocation_spyed(mem);
        spy_active = TRUE;
        mem = sys_allocator().spy->PreDidAlloc(mem, spyed_block);
    }

    did_alloc = HeapValidate(GetProcessHeap(), 0, mem);

    if (spy_active)
    {
        did_alloc = sys_allocator().spy->PostDidAlloc(mem, spyed_block, did_alloc);
        LeaveCriticalSection(&allocspy_cs);
    }

    return did_alloc;
}

void CMalloc::HeapMinimize()
{
    BOOL spy_active = FALSE;

    if (sys_allocator().spy)
    {
        EnterCriticalSection(&allocspy_cs);
        spy_active = TRUE;
        sys_allocator().spy->PreHeapMinimize();
    }

    if (spy_active)
    {
        sys_allocator().spy->PostHeapMinimize();
        LeaveCriticalSection(&allocspy_cs);
    }
}

/******************************************************************************
 *                CoGetMalloc        (combase.@)
 */
HRESULT WINAPI CoGetMalloc(DWORD context, IMalloc **imalloc)
{
    if (context != MEMCTX_TASK)
    {
        *imalloc = NULL;
        return E_INVALIDARG;
    }

    *imalloc = sys_allocator().IMalloc_iface;

    return S_OK;
}

/***********************************************************************
 *           CoTaskMemAlloc         (combase.@)
 */
void *WINAPI CoTaskMemAlloc(SIZE_T size)
{
    return sys_allocator().IMalloc_iface->Alloc(size);
}

/***********************************************************************
 *           CoTaskMemFree          (combase.@)
 */
void WINAPI CoTaskMemFree(void *ptr)
{
    sys_allocator().IMalloc_iface->Free(ptr);
}

/***********************************************************************
 *           CoTaskMemRealloc        (combase.@)
 */
void *WINAPI CoTaskMemRealloc(void *ptr, SIZE_T size)
{
    return sys_allocator().IMalloc_iface->Realloc(ptr, size);
}

/***********************************************************************
 *           CoRegisterMallocSpy        (combase.@)
 */
HRESULT WINAPI CoRegisterMallocSpy(IMallocSpy *spy)
{
    HRESULT hr = E_INVALIDARG;

    if (!spy)
        return E_INVALIDARG;

    EnterCriticalSection(&allocspy_cs);

    if (sys_allocator().spy)
        hr = CO_E_OBJISREG;
    else if (SUCCEEDED(spy->QueryInterface(IID_IMallocSpy, (void **)&spy)))
    {
        sys_allocator().spy = spy;
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

    if (!sys_allocator().spy)
        hr = CO_E_OBJNOTREG;
    else if (sys_allocator().spyed_allocations)
    {
        sys_allocator().spy_release_pending = TRUE;
        hr = E_ACCESSDENIED;
    }
    else
    {
        sys_allocator().spy->Release();
        sys_allocator().spy = NULL;
    }

    LeaveCriticalSection(&allocspy_cs);

    return hr;
}
