#ifndef _SUIDATAOBJECTPROXY_H_
#define _SUIDataOBJECTPROXY_H_
#import <UIKit/UIKit.h>
#include <windows.h>
#include <SUnkImpl.h>

// UIPasteboard 的 IDataObject 适配，供拖放目标读取系统剪贴板/拖拽数据。
class SUIDataObjectProxy : public SUnkImpl<IDataObject>{
public:
    SUIDataObjectProxy(UIPasteboard *pasteboard);
    ~SUIDataObjectProxy();
public:
    virtual /* [local] */ HRESULT WINAPI GetData(
        _In_  FORMATETC* pformatetcIn,
        _Out_  STGMEDIUM* pmedium) override;

    virtual /* [local] */ HRESULT WINAPI GetDataHere(
        _In_  FORMATETC* pformatetc,
        _Inout_  STGMEDIUM* pmedium) override{
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI QueryGetData(
        __RPC__in_opt FORMATETC *pformatetc) override;

    virtual HRESULT WINAPI GetCanonicalFormatEtc(
        __RPC__in_opt FORMATETC* pformatectIn,
        __RPC__out FORMATETC* pformatetcOut) override{
        return E_NOTIMPL;
    }

    virtual /* [local] */ HRESULT WINAPI SetData(
        _In_  FORMATETC* pformatetc,
        _In_  STGMEDIUM* pmedium,
        BOOL fRelease) override{
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI EnumFormatEtc(
        DWORD dwDirection,
        __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc) override;
    virtual HRESULT WINAPI DAdvise(
        __RPC__in FORMATETC* pformatetc,
        DWORD advf,
        __RPC__in_opt IAdviseSink* pAdvSink,
        __RPC__out DWORD* pdwConnection)override {
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI DUnadvise(
        DWORD dwConnection)override {
        return E_NOTIMPL;
    }

    virtual HRESULT WINAPI EnumDAdvise(
        __RPC__deref_out_opt IEnumSTATDATA** ppenumAdvise)override {
        return E_NOTIMPL;
    }

    static NSString* getPasteboardType(UINT uFormat);
public:
	IUNKNOWN_BEGIN(IDataObject)
		IUNKNOWN_END()
private:
    UIPasteboard * m_pasteboard;
};

#endif//_SUIDATAOBJECTPROXY_H_
