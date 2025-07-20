#ifndef _SNSDATAOBJECTPROXY_H_
#define _SNSDATAOBJECTPROXY_H_
#import <Cocoa/Cocoa.h>
#include <windows.h>
#include <SUnkImpl.h>

class SNsDataObjectProxy : public SUnkImpl<IDataObject>{
public:
    SNsDataObjectProxy(NSPasteboard * nspasteboard);
    ~SNsDataObjectProxy();
public:
    virtual /* [local] */ HRESULT WINAPI GetData(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetcIn,
        /* [annotation][out] */
        _Out_  STGMEDIUM* pmedium) override;

    virtual /* [local] */ HRESULT WINAPI GetDataHere(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetc,
        /* [annotation][out][in] */
        _Inout_  STGMEDIUM* pmedium) override{
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI QueryGetData(
        /* [unique][in] */ __RPC__in_opt FORMATETC *pformatetc) override;

    virtual HRESULT WINAPI GetCanonicalFormatEtc(
        /* [unique][in] */ __RPC__in_opt FORMATETC* pformatectIn,
        /* [out] */ __RPC__out FORMATETC* pformatetcOut) override{
        return E_NOTIMPL;
    }

    virtual /* [local] */ HRESULT WINAPI SetData(
        /* [annotation][unique][in] */
        _In_  FORMATETC* pformatetc,
        /* [annotation][unique][in] */
        _In_  STGMEDIUM* pmedium,
        /* [in] */ BOOL fRelease) override{
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI EnumFormatEtc(
        /* [in] */ DWORD dwDirection,
        /* [out] */ __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc) override;
    virtual HRESULT WINAPI DAdvise(
        /* [in] */ __RPC__in FORMATETC* pformatetc,
        /* [in] */ DWORD advf,
        /* [unique][in] */ __RPC__in_opt IAdviseSink* pAdvSink,
        /* [out] */ __RPC__out DWORD* pdwConnection)override {
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI DUnadvise(
        /* [in] */ DWORD dwConnection)override {
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI EnumDAdvise(
        /* [out] */ __RPC__deref_out_opt IEnumSTATDATA** ppenumAdvise)override {
        return E_NOTIMPL;
    }

    static NSString* getPasteboardType(UINT uFormat);
public:
	IUNKNOWN_BEGIN(IDataObject)
		IUNKNOWN_END()
private:
    NSPasteboard * m_nspasteboard;
};

#endif//_SNSDATAOBJECTPROXY_H_