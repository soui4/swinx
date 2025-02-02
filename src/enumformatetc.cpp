#include "enumformatetc.h"

SEnumFormatEtc::SEnumFormatEtc(UINT cfmt, const FORMATETC afmt[])
{
    posFmt = 0;
    countFmt = 0;
    pFmt = new FORMATETC[cfmt];
    if (pFmt)
    {
        memcpy(pFmt, afmt, cfmt * sizeof(FORMATETC));
        countFmt = cfmt;
    }
}

SEnumFormatEtc::~SEnumFormatEtc()
{
    if (pFmt)
        delete[] pFmt;
}

HRESULT SEnumFormatEtc::Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFethed)
{
    UINT i;

    if (!pFmt)
        return S_FALSE;
    if (!rgelt)
        return E_INVALIDARG;
    if (pceltFethed)
        *pceltFethed = 0;

    for (i = 0; posFmt < countFmt && celt > i; i++)
    {
        *rgelt++ = pFmt[posFmt++];
    }

    if (pceltFethed)
        *pceltFethed = i;

    return ((i == celt) ? S_OK : S_FALSE);
}

HRESULT SEnumFormatEtc::Skip(ULONG celt)
{
    if ((posFmt + celt) >= countFmt)
        return S_FALSE;
    posFmt += celt;
    return S_OK;
}

HRESULT SEnumFormatEtc::Reset()
{
    posFmt = 0;
    return S_OK;
}

HRESULT SEnumFormatEtc::Clone(IEnumFORMATETC **ppenum)
{
    if (!ppenum)
        return E_INVALIDARG;

    *ppenum = new SEnumFormatEtc(countFmt, pFmt);
    if (*ppenum)
        (*ppenum)->Skip(posFmt);
    return S_OK;
}
