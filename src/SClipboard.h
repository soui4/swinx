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
	HRESULT WINAPI GetData(FORMATETC* pformatetcIn,STGMEDIUM* pmedium);

	HRESULT WINAPI GetDataHere(FORMATETC* pformatetc,STGMEDIUM* pmedium);

	HRESULT WINAPI QueryGetData(FORMATETC* pformatetc);

	HRESULT WINAPI SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease);

	HRESULT WINAPI EnumFormatEtc(DWORD dwDirection,IEnumFORMATETC** ppenumFormatEtc);

	HRESULT WINAPI GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) {
		return E_NOTIMPL;
	}

	HRESULT WINAPI DAdvise(
		FORMATETC* pformatetc,
		DWORD advf,
		IAdviseSink* pAdvSink,
		DWORD* pdwConnection) {
		return E_NOTIMPL;
	}

	HRESULT WINAPI DUnadvise(DWORD dwConnection) {
		return E_NOTIMPL;
	}

	HRESULT WINAPI EnumDAdvise(
		IEnumSTATDATA** ppenumAdvise) {
		return E_NOTIMPL;
	}
	IUNKNOWN_BEGIN(IDataObject)
		IUNKNOWN_END()
};


class SClipboard : public ISelectionListener {
	enum {
		kWaitTimeout = 5000,
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
	void setSelDataObject(IDataObject* pDo);
public:
	void handleSelectionRequest(xcb_selection_request_event_t* e);
	void handleSelectionClear(xcb_selection_clear_event_t *e);

	std::shared_ptr<std::vector<char>> getDataInFormat(xcb_atom_t modeAtom, xcb_atom_t fmtAtom);
	std::shared_ptr<std::vector<char>> getSelection(xcb_atom_t selection, xcb_atom_t fmtAtom, xcb_atom_t property, xcb_timestamp_t time);
protected:
	  
	xcb_generic_event_t* waitForClipboardEvent(xcb_window_t win, int type, int timeout, xcb_atom_t selAtom,bool checkManager = false);
	void clipboardReadIncrementalProperty(xcb_window_t win, xcb_atom_t property, xcb_atom_t selection, int nbytes, bool nullterm, std::shared_ptr<std::vector<char>> buf);
	bool clipboardReadProperty(xcb_window_t win, xcb_atom_t property, bool deleteProperty, std::vector<char>* buffer, int* size, xcb_atom_t* type, int* format);
	xcb_atom_t sendTargetsSelection(IDataObject* d, xcb_window_t window, xcb_atom_t property);
	xcb_atom_t sendSelection(IDataObject* d, xcb_atom_t target, xcb_window_t window, xcb_atom_t property);

	xcb_connection_t* xcb_connection() const;
private:
	SConnection* m_conn;
	xcb_timestamp_t m_ts;
	xcb_timestamp_t m_incr_receive_time;

	xcb_window_t m_requestor;
	xcb_window_t m_owner;
	BOOL m_bOpen;

	std::recursive_mutex m_mutex;
	SMimeData* m_doClip;
	IDataObject* m_doSel;
};

#endif//_SCLIPBOARD_H_