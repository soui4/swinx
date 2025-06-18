#ifndef _SCLIPBOARD_H_
#define _SCLIPBOARD_H_

#include <xcb/xproto.h>
#include <sstream>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <memory>
#include <vector>
#include "SUnkImpl.h"
#include "nativewnd.h"
#include "uimsg.h"
#include "SConnection.h"

class FormatedData {
public:
	UINT fmt;
	HGLOBAL data;
	BOOL bRelease;
	FormatedData() :fmt(0), data(nullptr), bRelease(TRUE){}
	~FormatedData() {
		if (data && bRelease) 
			GlobalFree(data);
	}
};

class SMimeEnumFORMATETC;
class SMimeData : public SUnkImpl<IDataObject> {
	SMimeEnumFORMATETC* m_fmtEnum;
public:
	SMimeData();
	~SMimeData();

	void clear();
	void set(FormatedData* data);

	std::list<FormatedData*> m_lstData;
	std::recursive_mutex m_mutex;
public:
	HRESULT WINAPI GetData(FORMATETC* pformatetcIn,STGMEDIUM* pmedium)override;

	HRESULT WINAPI GetDataHere(FORMATETC* pformatetc,STGMEDIUM* pmedium)override;

	HRESULT WINAPI QueryGetData(FORMATETC* pformatetc)override;

	HRESULT WINAPI SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease)override;

	HRESULT WINAPI EnumFormatEtc(DWORD dwDirection,IEnumFORMATETC** ppenumFormatEtc)override;

	HRESULT WINAPI GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) override{
		return E_NOTIMPL;
	}

	HRESULT WINAPI DAdvise(
		FORMATETC* pformatetc,
		DWORD advf,
		IAdviseSink* pAdvSink,
		DWORD* pdwConnection)override {
		return E_NOTIMPL;
	}

	HRESULT WINAPI DUnadvise(DWORD dwConnection) override{
		return E_NOTIMPL;
	}

	HRESULT WINAPI EnumDAdvise(
		IEnumSTATDATA** ppenumAdvise) override{
		return E_NOTIMPL;
	}
	IUNKNOWN_BEGIN(IDataObject)
		IUNKNOWN_END()
};

class SDataObjectProxy : public SUnkImpl<IDataObject>{
public:
    SDataObjectProxy(SConnection* conn, HWND hWnd);
	~SDataObjectProxy();

    HWND getSource() const {
        return m_hSource;
    }

	void fetchDataTypeList();

	virtual bool isXdnd() const{ return false;}
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
        /* [unique][in] */ __RPC__in_opt FORMATETC* pformatetc) override{
        for (auto it : m_lstTypes) {
            if (it == pformatetc->cfFormat)
            {
                if (pformatetc->tymed != TYMED_HGLOBAL)
                    return DV_E_TYMED;
                else
                    return S_OK;
            }
        }
        return DV_E_FORMATETC;
    }

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
        /* [out] */ __RPC__deref_out_opt IEnumFORMATETC** ppenumFormatEtc) override{
        return E_NOTIMPL;
    }

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
public:
    IUNKNOWN_BEGIN(IDataObject)
        IUNKNOWN_END()
protected:
    SConnection* m_conn;
    HWND m_hSource;
    std::vector<uint32_t> m_lstTypes;
};

class SClipboard {
	enum {
		kWaitTimeout = 5000,
		kIncrTimeout = 50000,
	};
public:
	SClipboard(SConnection *conn);
	~SClipboard();

public:
	bool hasFormat(UINT fmtAtom);
	HANDLE getClipboardData(UINT fmtAtom);
	HANDLE setClipboardData(UINT fmtAtom, HANDLE hMem);
	BOOL openClipboard(HWND hWndNewOwner);
	BOOL closeClipboard();
	xcb_window_t getClipboardOwner() const;
	BOOL emptyClipboard();
	IDataObject* getDataObject(BOOL bSel = FALSE);
	BOOL isCurrentClipboard(IDataObject * pDo);
	BOOL setDataObject(IDataObject* pDo,BOOL bSel);
    void flushClipboard();

    void setProcessIncr(BOOL process) { m_incr_active = process; }
    BOOL processIncr() { return m_incr_active; }
public:
	void handleSelectionRequest(xcb_selection_request_event_t* e);
	void handleSelectionClear(xcb_selection_clear_event_t *e);
    void incrTransactionPeeker(xcb_generic_event_t *ge, bool &accepted);

	std::shared_ptr<std::vector<char>> getDataInFormat(xcb_atom_t modeAtom, xcb_atom_t fmtAtom);
	std::shared_ptr<std::vector<char>> getSelection(xcb_atom_t selection, xcb_atom_t fmtAtom, xcb_atom_t property, xcb_timestamp_t time);
protected:
	  
	xcb_generic_event_t* waitForClipboardEvent(xcb_window_t win, int type, int timeout, xcb_atom_t selAtom,bool checkManager = false);
	void clipboardReadIncrementalProperty(xcb_window_t win, xcb_atom_t property, xcb_atom_t selection, int nbytes, bool nullterm, std::shared_ptr<std::vector<char>> buf);
	bool clipboardReadProperty(xcb_window_t win, xcb_atom_t property, bool deleteProperty, std::vector<char>* buffer, int* size, xcb_atom_t* type, int* format);
	xcb_atom_t sendTargetsSelection(IDataObject* d, xcb_window_t window, xcb_atom_t property);
	xcb_atom_t sendSelection(IDataObject* d, xcb_atom_t target, xcb_window_t window, xcb_atom_t property);

	xcb_connection_t* xcb_connection() const;
	xcb_window_t getClipboardMgrOwner();
private:
	SConnection* m_conn;
	xcb_timestamp_t m_ts;
	xcb_timestamp_t m_incr_receive_time;

	xcb_window_t m_requestor;
	xcb_window_t m_owner;
	BOOL m_bOpen;

	std::recursive_mutex m_mutex;
	SMimeData* m_doClip;
	IDataObject* m_doExClip;
	IDataObject* m_doSel;

	BOOL m_incr_active;
};

#endif//_SCLIPBOARD_H_