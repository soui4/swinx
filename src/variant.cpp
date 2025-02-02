#include <windows.h>
#include <oaidl.h>

void VariantInit(VARIANTARG *pvarg)
{
    memset(pvarg, 0, sizeof(VARIANTARG));
    pvarg->vt = VT_EMPTY;
}

HRESULT VariantClear(VARIANTARG *prop)
{
    if (prop->vt == VT_BSTR)
        SysFreeString(prop->bstrVal);
    prop->vt = VT_EMPTY;
    return S_OK;
}

HRESULT VariantCopy(VARIANTARG *dest, const VARIANTARG *src)
{
    HRESULT res = ::VariantClear(dest);
    if (res != S_OK)
        return res;
    switch (src->vt)
    {
    case VT_BSTR:
        dest->bstrVal = SysAllocStringByteLen((LPCSTR)src->bstrVal, SysStringByteLen(src->bstrVal));
        if (!dest->bstrVal)
            return E_OUTOFMEMORY;
        dest->vt = VT_BSTR;
        break;
    case VT_DISPATCH:
    case VT_UNKNOWN:
        dest->punkVal = src->punkVal;
        if (dest->punkVal)
            dest->punkVal->AddRef();
        break;
    case VT_ARRAY:
        return SafeArrayCopy(src->parray, &dest->parray);
    default:
        *dest = *src;
        break;
    }
    return S_OK;
}