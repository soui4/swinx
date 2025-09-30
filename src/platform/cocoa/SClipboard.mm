#import <Cocoa/Cocoa.h>
#include "SClipboard.h"
#include <windows.h>
#include <vector>
#include <log.h>
#include "atoms.h"
#include "ctypes.h"
#include "sysapi.h"
#define kLogTag "SClipboard"
#include "tostring.hpp"
using namespace swinx;


static NSPasteboardType clipboardFormat2PasteboardType(UINT uFormat) {
  switch (uFormat) {
  case CF_TEXT:
    return NSStringPboardType;
  case CF_BITMAP:
    return NSPasteboardTypePNG;
  default:
    if (uFormat > CF_MAX) {
      char szName[1024];
      int len = SAtoms::getAtomName(uFormat - CF_MAX, szName, 1024);
      if (len < 0)
        return nil;
      return [NSString stringWithUTF8String:szName];
    } else {
      return nil;
    }
  }
}

class SMimeEnumFORMATETC : public SUnkImpl<IEnumFORMATETC> {
  SMimeData *m_mimeData;
  ULONG m_iFmt;

public:
  SMimeEnumFORMATETC(SMimeData *mimeData) : m_mimeData(mimeData), m_iFmt(0) {
    mimeData->lock();
  }
  ~SMimeEnumFORMATETC(){
    m_mimeData->unlock();
  }

  HRESULT STDMETHODCALLTYPE Next(
      /* [in] */ ULONG celt,
      /* [annotation] */
      _Out_writes_to_(celt, *pceltFetched) FORMATETC *rgelt,
      /* [annotation] */
      _Out_opt_ ULONG *pceltFetched) override {
        auto data = m_mimeData->formatedData();
    if (m_iFmt + celt > data.size())
      return E_UNEXPECTED;
    if (pceltFetched)
      *pceltFetched = m_iFmt;
    auto it = data.begin();
    ULONG i = 0;
    while (i < m_iFmt) {
      it++;
      i++;
    }
    for (ULONG j = 0; j < celt; j++) {
      rgelt[j].cfFormat = (*it)->fmt;
      rgelt[j].tymed = TYMED_HGLOBAL;
      it++;
    }
    m_iFmt += celt;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE Skip(/* [in] */ ULONG celt) override {
    m_iFmt += celt;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE Reset(void) override {
    m_iFmt = 0;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE
  Clone(/* [out] */ __RPC__deref_out_opt IEnumFORMATETC **ppenum) override {
    return E_NOTIMPL;
  }

  IUNKNOWN_BEGIN(IEnumFORMATETC)
  IUNKNOWN_END()
};

//-----------------------------------------------------------------------------
SMimeData::SMimeData() { m_fmtEnum = new SMimeEnumFORMATETC(this); }

SMimeData::~SMimeData() {
  clear();
  m_fmtEnum->Release();
}

void SMimeData::clear() {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  for (auto it : m_lstData) {
    delete it;
  }
  m_lstData.clear();
  SLOG_STMD() << "clear mimedata";
}

bool SMimeData::isEmpty() const{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    return m_lstData.empty();
}



HRESULT SMimeData::QueryGetData(FORMATETC *pformatetc) {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  @autoreleasepool {
    NSPasteboardType type =
        clipboardFormat2PasteboardType(pformatetc->cfFormat);
    if (!type) {
      return DV_E_FORMATETC;
    }
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    if ([[pasteboard types] containsObject:type]) {
      return S_OK;
    }
    return DV_E_FORMATETC;
  }
}

HRESULT SMimeData::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);

  if (pformatetcIn->cfFormat == CF_UNICODETEXT) {
    pformatetcIn->cfFormat = CF_TEXT;
    HRESULT hr = GetData(pformatetcIn, pmedium);
    if (hr != S_OK)
      return hr;

    const char *pData = (const char *)GlobalLock(pmedium->hGlobal);
    size_t len = strlen(pData);
    GlobalUnlock(pmedium->hGlobal);
    std::wstring wstr;
    towstring(pData, len, wstr);
    pmedium->hGlobal = GlobalReAlloc(pmedium->hGlobal, GMEM_MOVEABLE,
                                     (wstr.length() + 1) * sizeof(wchar_t));
    void *dst = GlobalLock(pmedium->hGlobal);
    memcpy(dst, wstr.c_str(), (wstr.length() + 1) * sizeof(wchar_t));
    GlobalUnlock(pmedium->hGlobal);
    return hr;
  } else {
    if (pformatetcIn->tymed != TYMED_HGLOBAL)
      return DV_E_TYMED;
    @autoreleasepool {
      NSPasteboardType type =
          clipboardFormat2PasteboardType(pformatetcIn->cfFormat);
      if (!type) {
        return DV_E_FORMATETC;
      }
      NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
      if ([[pasteboard types] containsObject:type]) {
        NSData *data = [pasteboard dataForType:type];
        if (data) {
          pmedium->hGlobal = GlobalAlloc(GMEM_MOVEABLE, [data length]);
          void *buf = GlobalLock(pmedium->hGlobal);
          memcpy(buf, [data bytes], [data length]);
          GlobalUnlock(pmedium->hGlobal);
          pmedium->tymed = TYMED_HGLOBAL;
          return S_OK;
        }
      }
      return DV_E_FORMATETC;
    }
  }
}

HRESULT SMimeData::GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) {
  for (auto it : m_lstData) {
    if (it->fmt == pformatetc->cfFormat && pformatetc->tymed == TYMED_HGLOBAL) {
      assert(pmedium->hGlobal);
      const void *src = GlobalLock(it->data);
      void *dst = GlobalLock(pmedium->hGlobal);
      memcpy(dst, src,
             std::min(GlobalSize(it->data), GlobalSize(pmedium->hGlobal)));
      GlobalUnlock(it->data);
      GlobalUnlock(pmedium->hGlobal);
      pmedium->tymed = TYMED_HGLOBAL;
      return S_OK;
    }
  }
  return DV_E_FORMATETC;
}

void SMimeData::flush() {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    for (auto it : m_lstData) {
      NSPasteboardType type = clipboardFormat2PasteboardType(it->fmt);
      if (!type) {
        continue;
      }
      NSData *data = [NSData dataWithBytes:GlobalLock(it->data)
                                    length:GlobalSize(it->data)];
      GlobalUnlock(it->data);
      [pasteboard setData:data forType:type];
    }
    clear();
  }
}

void SMimeData::set(FormatedData *data) {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  for (auto it = m_lstData.begin(); it != m_lstData.end(); it++) {
    if ((*it)->fmt == data->fmt) {
      delete *it;
      m_lstData.erase(it);
      break;
    }
  }
  m_lstData.push_back(data);
  SLOG_STMI() << "SMimeData::set,format=" << data->fmt
              << " format size=" << m_lstData.size();
}

HRESULT SMimeData::SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium,
                           BOOL fRelease) {
  if (pmedium->tymed != TYMED_HGLOBAL)
    return DV_E_TYMED;
  FormatedData *pdata = new FormatedData;
  pdata->fmt = pformatetc->cfFormat;
  if (fRelease) {
    pdata->data = pmedium->hGlobal;
  } else {
    pdata->data = GlobalAlloc(0, GlobalSize(pmedium->hGlobal));
    void *dst = GlobalLock(pdata->data);
    const void *src = GlobalLock(pmedium->hGlobal);
    memcpy(dst, src, GlobalSize(pdata->data));
    GlobalUnlock(pdata->data);
    GlobalUnlock(pmedium->hGlobal);
  }
  set(pdata);
  return S_OK;
}

HRESULT SMimeData::EnumFormatEtc(DWORD dwDirection,
                                 IEnumFORMATETC **ppenumFormatEtc) {
  if (dwDirection == DATADIR_GET) {
    *ppenumFormatEtc = m_fmtEnum;
    m_fmtEnum->Reset();
    m_fmtEnum->AddRef();
    return S_OK;
  }
  return E_NOTIMPL;
}

SClipboard::SClipboard()
    : m_owner(0), m_bOpen(FALSE), m_bModified(FALSE), m_doClip(NULL),
      m_doExClip(NULL) {
  m_doClip = new SMimeData();
}

SClipboard::~SClipboard() {
  if (getClipboardOwner() == m_owner) {
    emptyClipboard();
  }
  m_doClip->Release();
  m_doClip = NULL;
  if (m_doExClip) {
    m_doExClip->Release();
    m_doExClip = NULL;
  }
  m_owner = 0;
}

HWND SClipboard::getClipboardOwner() const { return m_owner; }

BOOL SClipboard::emptyClipboard() {
  if (!m_bOpen)
    return FALSE;
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  if (m_doExClip) {
    m_doExClip->Release();
    m_doExClip = NULL;
  }
  m_doClip->clear();
  m_bModified = TRUE;
  return TRUE;
}

BOOL SClipboard::isCurrentClipboard(IDataObject *pDo) {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);

  if (m_doExClip)
    return pDo == m_doExClip;
  return pDo == m_doClip;
}

IDataObject *SClipboard::getDataObject() {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  IDataObject *ret = m_doExClip ? m_doExClip : m_doClip;
  ret->AddRef();
  return ret;
}

BOOL SClipboard::setDataObject(IDataObject *pDo) {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  if (!m_bOpen)
    return FALSE;
  if (m_doExClip) {
    m_doExClip->Release();
    m_doExClip = NULL;
  }
  m_doExClip = pDo;
  if (m_doExClip) {
    m_doExClip->AddRef();
  }
  m_bModified = TRUE;
  return TRUE;
}

void SClipboard::flushClipboard() {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  if (m_doExClip) {
    // copy extenal dataobject to internal dataobject
    m_doClip->clear();
    IEnumFORMATETC *enum_fmt;
    HRESULT hr = m_doExClip->EnumFormatEtc(DATADIR_GET, &enum_fmt);
    if (FAILED(hr))
      return;
    FORMATETC fmt;
    while (enum_fmt->Next(1, &fmt, NULL) == S_OK) {
      STGMEDIUM storage = {0};
      hr = m_doExClip->GetData(&fmt, &storage);
      if (hr == S_OK) {
        m_doClip->SetData(&fmt, &storage, FALSE);
      }
    }
    enum_fmt->Release();

    m_doExClip->Release();
    m_doExClip = NULL;
  }
  m_doClip->flush();
}

bool SClipboard::hasFormat(UINT format) {
  @autoreleasepool {
    NSPasteboardType type = clipboardFormat2PasteboardType(format);
    if (!type)
      return FALSE;
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    return [[pasteboard types] containsObject:type];
  }
}

HANDLE SClipboard::getClipboardData(UINT uFormat) {
  FORMATETC formatetcIn;
  STGMEDIUM medium = {0};
  formatetcIn.cfFormat = uFormat;
  formatetcIn.tymed = TYMED_HGLOBAL;
  HRESULT hr = m_doClip->GetData(&formatetcIn, &medium);
  if (hr != S_OK)
    return NULL;
  return medium.hGlobal;
}

HANDLE SClipboard::setClipboardData(UINT uFormat, HANDLE hMem) {
  if (!m_bOpen) {
    GlobalFree(hMem);
    return 0;
  }
  if (uFormat == CF_UNICODETEXT) {
    // convert to utf8
    const wchar_t *src = (const wchar_t *)GlobalLock(hMem);
    size_t len = GlobalSize(hMem) / sizeof(wchar_t);
    std::string str;
    tostring(src, len, str);
    GlobalUnlock(hMem);
    hMem = GlobalReAlloc(hMem, str.length(), 0);
    void *buf = GlobalLock(hMem);
    memcpy(buf, str.c_str(), str.length());
    GlobalUnlock(hMem);
    uFormat = CF_TEXT;
  }
  FORMATETC formatetc = {(CLIPFORMAT)uFormat, nullptr, 0, 0,
                         TYMED_HGLOBAL}; // Pointer to the FORMATETC structure
  STGMEDIUM medium={0};
  medium.hGlobal = hMem;
  medium.tymed = TYMED_HGLOBAL;
  m_doClip->SetData(&formatetc, &medium, TRUE);
  m_bModified = TRUE;
  return hMem;
}

BOOL SClipboard::openClipboard(HWND hWndNewOwner) {
  m_mutex.lock();
  if (m_bOpen)
    return FALSE;
  m_bOpen = TRUE;
  m_bModified = FALSE;
  m_owner = hWndNewOwner;
  return TRUE;
}

BOOL SClipboard::closeClipboard() {
  m_mutex.unlock();
  if (!m_bOpen)
    return FALSE;
  if(m_bModified)
  {
    flushClipboard();
  }
  m_bOpen = FALSE;
  m_bModified = FALSE;
  m_owner = NULL;
  return TRUE;
}

UINT SClipboard::RegisterClipboardFormatA(LPCSTR pszName) {
  return SAtoms::registerAtom(pszName) + CF_MAX;
}
