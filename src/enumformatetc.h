#ifndef __ENUM_FORMATETC_H__
#define __ENUM_FORMATETC_H__
#include <windows.h>
#include <objidl.h>
#include "SUnkImpl.h"

class SEnumFormatEtc : public SUnkImpl <IEnumFORMATETC>
{
  public:
    SEnumFormatEtc(UINT cfmt, const FORMATETC afmt[]);
    ~SEnumFormatEtc();
  public:
    virtual /* [local] */ HRESULT STDMETHODCALLTYPE Next(
        /* [in] */ ULONG celt,
        /* [annotation] */
        _Out_writes_to_(celt, *pceltFetched) FORMATETC *rgelt,
        /* [annotation] */
        _Out_opt_ ULONG *pceltFetched) override;

    virtual HRESULT STDMETHODCALLTYPE Skip(
        /* [in] */ ULONG celt) override;

    virtual HRESULT STDMETHODCALLTYPE Reset(void) override;

    virtual HRESULT STDMETHODCALLTYPE Clone(
        /* [out] */ __RPC__deref_out_opt IEnumFORMATETC **ppenum) override;

  protected:
    IUNKNOWN_BEGIN(IEnumFORMATETC)
    IUNKNOWN_END()

    UINT posFmt;
    UINT countFmt;
    LPFORMATETC pFmt;
};

#endif //__ENUM_FORMATETC_H__