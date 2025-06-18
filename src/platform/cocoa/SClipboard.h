#ifndef _SCLIPBOARD_H_
#define _SCLIPBOARD_H_

#include <sstream>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <memory>
#include <vector>
#include <ctypes.h>
#include <basetyps.h>
#include <sysapi.h>
#include <winerror.h>
#include <SUnkImpl.h>
#include <objidl.h>

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

	std::recursive_mutex m_mutex;
	std::list<FormatedData*> m_lstData;

	void set(FormatedData* data);
	void flush();
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

class SClipboard {

public:
	SClipboard();
	~SClipboard();

	static UINT RegisterClipboardFormatA(LPCSTR pszName);
public:
	bool hasFormat(UINT fmtAtom);
	HANDLE getClipboardData(UINT fmtAtom);
	HANDLE setClipboardData(UINT fmtAtom, HANDLE hMem);
	BOOL openClipboard(HWND hWndNewOwner);
	BOOL closeClipboard();
	HWND getClipboardOwner() const;
	BOOL emptyClipboard();
	IDataObject* getDataObject();
	BOOL isCurrentClipboard(IDataObject * pDo);
	BOOL setDataObject(IDataObject* pDo);
    void flushClipboard();

private:
	HWND m_owner;
	BOOL m_bOpen;

	std::recursive_mutex m_mutex;
	SMimeData* m_doClip;
	IDataObject* m_doExClip;
};

#endif//_SCLIPBOARD_H_