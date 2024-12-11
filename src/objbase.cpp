#include <windows.h>
#include <objbase.h>
#include <oaidl.h>
#include <uuid/uuid.h>

static inline void* AllocateForBSTR(size_t cb) { return ::malloc(cb); }
static inline void FreeForBSTR(void* pv) { ::free(pv); }

/* Win32 uses DWORD (32-bit) type to store size of string before (OLECHAR *) string.
  We must select CBstrSizeType for another systems (not Win32):

    if (CBstrSizeType is UINT32),
          then we support only strings smaller than 4 GB.
          Win32 version always has that limitation.

    if (CBstrSizeType is UINT),
          (UINT can be 16/32/64-bit)
          We can support strings larger than 4 GB (if UINT is 64-bit),
          but sizeof(UINT) can be different in parts compiled by
          different compilers/settings,
          and we can't send such BSTR strings between such parts.
*/

typedef uint32_t CBstrSizeType;

#define k_BstrSize_Max 0xFFFFFFFF
// #define k_BstrSize_Max UINT_MAX
// #define k_BstrSize_Max ((UINT)(INT)-1)

BSTR SysAllocStringByteLen(LPCSTR s, UINT len)
{
    /* Original SysAllocStringByteLen in Win32 maybe fills only unaligned null OLECHAR at the end.
       We provide also aligned null OLECHAR at the end. */

    if (len >= (k_BstrSize_Max - sizeof(OLECHAR) - sizeof(OLECHAR) - sizeof(CBstrSizeType)))
        return NULL;

    UINT size = (len + sizeof(OLECHAR) + sizeof(OLECHAR) - 1) & ~(sizeof(OLECHAR) - 1);
    void* p = AllocateForBSTR(size + sizeof(CBstrSizeType));
    if (!p)
        return NULL;
    *(CBstrSizeType*)p = (CBstrSizeType)len;
    BSTR bstr = (BSTR)((CBstrSizeType*)p + 1);
    if (s)
        memcpy(bstr, s, len);
    for (; len < size; len++)
        ((BYTE*)bstr)[len] = 0;
    return bstr;
}

BSTR SysAllocStringLen(const OLECHAR* s, UINT len)
{
    if (len >= (k_BstrSize_Max - sizeof(OLECHAR) - sizeof(CBstrSizeType)) / sizeof(OLECHAR))
        return NULL;

    UINT size = len * sizeof(OLECHAR);
    void* p = AllocateForBSTR(size + sizeof(CBstrSizeType) + sizeof(OLECHAR));
    if (!p)
        return NULL;
    *(CBstrSizeType*)p = (CBstrSizeType)size;
    BSTR bstr = (BSTR)((CBstrSizeType*)p + 1);
    if (s)
        memcpy(bstr, s, size);
    bstr[len] = 0;
    return bstr;
}

BSTR SysAllocString(const OLECHAR* s)
{
    if (!s)
        return 0;
    const OLECHAR* s2 = s;
    while (*s2 != 0)
        s2++;
    return SysAllocStringLen(s, (UINT)(s2 - s));
}

void SysFreeString(BSTR bstr)
{
    if (bstr)
        FreeForBSTR((CBstrSizeType*)bstr - 1);
}

UINT SysStringByteLen(BSTR bstr)
{
    if (!bstr)
        return 0;
    return *((CBstrSizeType*)bstr - 1);
}

UINT SysStringLen(BSTR bstr)
{
    if (!bstr)
        return 0;
    return *((CBstrSizeType*)bstr - 1) / sizeof(OLECHAR);
}

static inline BOOL is_valid_hex(WCHAR c)
{
    if (!(((c >= '0') && (c <= '9')) ||
        ((c >= 'a') && (c <= 'f')) ||
        ((c >= 'A') && (c <= 'F'))))
        return FALSE;
    return TRUE;
}

static const BYTE guid_conv_table[256] =
{
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 */
    0,   1,   2,   3,   4,   5,   6, 7, 8, 9, 0, 0, 0, 0, 0, 0, /* 0x30 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf                             /* 0x60 */
};

static BOOL guid_from_string(LPCWSTR s, GUID* id)
{
    int i;

    if (!s || s[0] != '{')
    {
        memset(id, 0, sizeof(*id));
        if (!s) return TRUE;
        return FALSE;
    }

    /* In form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} */

    id->Data1 = 0;
    for (i = 1; i < 9; ++i)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data1 = (id->Data1 << 4) | guid_conv_table[s[i]];
    }
    if (s[9] != '-') return FALSE;

    id->Data2 = 0;
    for (i = 10; i < 14; ++i)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data2 = (id->Data2 << 4) | guid_conv_table[s[i]];
    }
    if (s[14] != '-') return FALSE;

    id->Data3 = 0;
    for (i = 15; i < 19; ++i)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data3 = (id->Data3 << 4) | guid_conv_table[s[i]];
    }
    if (s[19] != '-') return FALSE;

    for (i = 20; i < 37; i += 2)
    {
        if (i == 24)
        {
            if (s[i] != '-') return FALSE;
            i++;
        }
        if (!is_valid_hex(s[i]) || !is_valid_hex(s[i + 1])) return FALSE;
        id->Data4[(i - 20) / 2] = guid_conv_table[s[i]] << 4 | guid_conv_table[s[i + 1]];
    }

    if (s[37] == '}' && s[38] == '\0')
        return TRUE;

    return FALSE;
}
/******************************************************************************
 *                CLSIDFromProgID        (combase.@)
 */
HRESULT WINAPI CLSIDFromProgID(LPCOLESTR progid, CLSID* clsid)
{
    return guid_from_string(progid, clsid);
}

HRESULT WINAPI CoCreateInstance(REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID FAR* ppv) {
    return E_NOTIMPL;
}


HRESULT WINAPI OleInitialize(LPVOID reserved) {
    //todo:hjx
    return S_OK;
}

void WINAPI OleUninitialize(void) {
    //todo:hjx
}

HRESULT WINAPI CoCreateGuid(GUID* pguid) {
    uuid_t id;
    uuid_generate(id);
    memcpy(pguid, &id, sizeof(id));
    return S_OK;
}