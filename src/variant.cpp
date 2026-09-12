#include <windows.h>
#include <oaidl.h>
#include "bstr_internal.h"

/*
 * VARIANT lifecycle following standard OLEAUT32 semantics.
 *
 * The COM surface SOUI actually depends on (see SwndAccessible.cpp, compiled
 * when SOUI_ENABLE_ACC is on) requires:
 *  - VariantCopy with VT_VARIANT|VT_BYREF source dereferences the pointer and
 *    copies the referenced variant (accValidateNavStart loops on it);
 *  - VariantClear must release interface references (VT_DISPATCH/VT_UNKNOWN)
 *    and destroy SAFEARRAYs, so VARIANT-held COM references are balanced;
 *  - VT_BYREF payloads are pointers owned by the caller: neither Clear nor
 *    Copy may free/reallocate the pointee.
 */

void VariantInit(VARIANTARG *pvarg)
{
    if (!pvarg)
        return;
    memset(pvarg, 0, sizeof(VARIANTARG));
    pvarg->vt = VT_EMPTY;
}

HRESULT VariantClear(VARIANTARG *prop)
{
    if (!prop)
        return E_POINTER;

    /* VT_BYREF: the pointee belongs to the caller, never touch it */
    if (!(prop->vt & VT_BYREF))
    {
        /* VT_ARRAY is a modifier bit (0x2000) above VT_TYPEMASK (0x0fff):
         * it never survives "& VT_TYPEMASK", so test it explicitly. */
        if (prop->vt & VT_ARRAY)
        {
            SafeArrayDestroy(prop->parray);
        }
        else
        {
            switch (prop->vt & VT_TYPEMASK)
            {
            case VT_BSTR:
                SysFreeString(prop->bstrVal);
                break;
            case VT_DISPATCH:
                if (prop->pdispVal)
                    prop->pdispVal->Release();
                break;
            case VT_UNKNOWN:
                if (prop->punkVal)
                    prop->punkVal->Release();
                break;
            default:
                break; /* scalar / empty types own no resources */
            }
        }
    }

    memset(prop, 0, sizeof(VARIANTARG));
    prop->vt = VT_EMPTY;
    return S_OK;
}

HRESULT VariantCopy(VARIANTARG *dest, const VARIANTARG *src)
{
    if (!dest || !src)
        return E_POINTER;

    HRESULT res = ::VariantClear(dest);
    if (res != S_OK)
        return res;

    /* VT_VARIANT|VT_BYREF: dereference and copy the referenced variant
     * (SOUI accValidateNavStart relies on this). */
    if ((src->vt & VT_TYPEMASK) == VT_VARIANT && (src->vt & VT_BYREF))
    {
        if (!src->pvarVal)
        {
            dest->vt = VT_EMPTY;
            return S_OK;
        }
        return VariantCopy(dest, src->pvarVal);
    }

    /* Any other VT_BYREF type: copy the pointer as-is, shallow */
    if (src->vt & VT_BYREF)
    {
        *dest = *src;
        return S_OK;
    }

    /* VT_ARRAY is a modifier bit (0x2000) above VT_TYPEMASK (0x0fff):
     * it never survives "& VT_TYPEMASK", so test it explicitly.
     * (The old "case VT_ARRAY" inside the switch below was dead code and a
     * plain VT_ARRAY fell through to the shallow default copy.) */
    if (src->vt & VT_ARRAY)
    {
        dest->vt = VT_EMPTY;
        res = SafeArrayCopy(src->parray, &dest->parray);
        if (res != S_OK)
            return res;
        dest->vt = src->vt;
        return S_OK;
    }

    switch (src->vt & VT_TYPEMASK)
    {
    case VT_BSTR:
        dest->bstrVal = SysAllocStringCopy(src->bstrVal);
        if (!dest->bstrVal && src->bstrVal)
            return E_OUTOFMEMORY;
        dest->vt = VT_BSTR;
        break;
    case VT_DISPATCH:
        dest->pdispVal = src->pdispVal;
        if (dest->pdispVal)
            dest->pdispVal->AddRef();
        dest->vt = VT_DISPATCH;
        break;
    case VT_UNKNOWN:
        dest->punkVal = src->punkVal;
        if (dest->punkVal)
            dest->punkVal->AddRef();
        dest->vt = VT_UNKNOWN;
        break;
    default:
        *dest = *src;
        break;
    }
    return S_OK;
}
