#include "SClipboard.h"
#include <windows.h>
#include "SUnkImpl.h"
#include "enumformatetc.h"
#include <winerror.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

static std::recursive_mutex s_clipFmtMutex;
static std::map<std::string, UINT> s_clipFmtMap;
static UINT s_nextClipFmt = 0xC000;

class SOhosClipboardDataObject : public SUnkImpl<IDataObject> {
    struct DataEntry {
        CLIPFORMAT format;
        HGLOBAL data;

        DataEntry(CLIPFORMAT fmt, HGLOBAL hData)
            : format(fmt)
            , data(hData)
        {
        }

        ~DataEntry()
        {
            if (data)
                GlobalFree(data);
        }
    };

  public:
    SOhosClipboardDataObject()
    {
    }

    ~SOhosClipboardDataObject()
    {
        clear();
    }

    void clear()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (DataEntry *entry : m_entries)
            delete entry;
        m_entries.clear();
    }

    bool empty() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_entries.empty();
    }

    HGLOBAL dataHandle(CLIPFORMAT format) const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const DataEntry *entry = findEntry(format);
        return entry ? entry->data : nullptr;
    }

    HRESULT WINAPI GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override
    {
        if (!pformatetcIn || !pmedium)
            return E_INVALIDARG;
        if (!(pformatetcIn->tymed & TYMED_HGLOBAL))
            return DV_E_TYMED;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const DataEntry *entry = findEntry(pformatetcIn->cfFormat);
        if (!entry)
            return DV_E_FORMATETC;

        HGLOBAL copy = duplicateHGlobal(entry->data, pformatetcIn->cfFormat);
        if (!copy)
            return STG_E_MEDIUMFULL;

        memset(pmedium, 0, sizeof(*pmedium));
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = copy;
        pmedium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT WINAPI GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) override
    {
        if (!pformatetc || !pmedium)
            return E_INVALIDARG;
        if (!(pformatetc->tymed & TYMED_HGLOBAL) || pmedium->tymed != TYMED_HGLOBAL)
            return DV_E_TYMED;
        if (!pmedium->hGlobal)
            return E_INVALIDARG;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const DataEntry *entry = findEntry(pformatetc->cfFormat);
        if (!entry)
            return DV_E_FORMATETC;

        const SIZE_T srcSize = clipboardDataSize(entry->data, pformatetc->cfFormat);
        const SIZE_T dstSize = GlobalSize(pmedium->hGlobal);
        if (!srcSize || !dstSize)
            return STG_E_MEDIUMFULL;

        const void *src = GlobalLock(entry->data);
        void *dst = GlobalLock(pmedium->hGlobal);
        if (!src || !dst)
        {
            if (dst)
                GlobalUnlock(pmedium->hGlobal);
            if (src)
                GlobalUnlock(entry->data);
            return STG_E_MEDIUMFULL;
        }

        memcpy(dst, src, std::min(srcSize, dstSize));
        GlobalUnlock(pmedium->hGlobal);
        GlobalUnlock(entry->data);
        return S_OK;
    }

    HRESULT WINAPI QueryGetData(FORMATETC *pformatetc) override
    {
        if (!pformatetc)
            return E_INVALIDARG;
        if (!(pformatetc->tymed & TYMED_HGLOBAL))
            return DV_E_TYMED;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return findEntry(pformatetc->cfFormat) ? S_OK : DV_E_FORMATETC;
    }

    HRESULT WINAPI SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override
    {
        if (!pformatetc || !pmedium)
            return E_INVALIDARG;
        if (!(pformatetc->tymed & TYMED_HGLOBAL) || pmedium->tymed != TYMED_HGLOBAL)
            return DV_E_TYMED;
        if (!pmedium->hGlobal)
            return E_INVALIDARG;

        HGLOBAL hData = fRelease ? pmedium->hGlobal : duplicateHGlobal(pmedium->hGlobal, pformatetc->cfFormat);
        if (!hData)
            return STG_E_MEDIUMFULL;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        removeEntry(pformatetc->cfFormat);
        m_entries.push_back(new DataEntry(pformatetc->cfFormat, hData));
        if (fRelease)
            pmedium->hGlobal = nullptr;
        return S_OK;
    }

    HRESULT WINAPI EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override
    {
        if (!ppenumFormatEtc)
            return E_INVALIDARG;
        *ppenumFormatEtc = nullptr;
        if (dwDirection != DATADIR_GET)
            return E_NOTIMPL;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        std::vector<FORMATETC> formats;
        formats.reserve(m_entries.size());
        for (const DataEntry *entry : m_entries)
        {
            FORMATETC fmt = { entry->format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            formats.push_back(fmt);
        }
        *ppenumFormatEtc = new SEnumFormatEtc((UINT)formats.size(), formats.empty() ? nullptr : formats.data());
        return *ppenumFormatEtc ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT WINAPI GetCanonicalFormatEtc(FORMATETC *, FORMATETC *) override
    {
        return E_NOTIMPL;
    }

    HRESULT WINAPI DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override
    {
        return E_NOTIMPL;
    }

    HRESULT WINAPI DUnadvise(DWORD) override
    {
        return E_NOTIMPL;
    }

    HRESULT WINAPI EnumDAdvise(IEnumSTATDATA **) override
    {
        return E_NOTIMPL;
    }

    IUNKNOWN_BEGIN(IDataObject)
    IUNKNOWN_END()

  private:
    const DataEntry *findEntry(CLIPFORMAT format) const
    {
        for (const DataEntry *entry : m_entries)
        {
            if (entry->format == format)
                return entry;
        }
        return nullptr;
    }

    void removeEntry(CLIPFORMAT format)
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            if ((*it)->format == format)
            {
                delete *it;
                m_entries.erase(it);
                return;
            }
        }
    }

    static SIZE_T boundedTextSize(HGLOBAL data, CLIPFORMAT format, SIZE_T fallbackSize)
    {
        const void *src = GlobalLock(data);
        if (!src)
            return fallbackSize;

        SIZE_T result = fallbackSize;
        if (format == CF_UNICODETEXT)
        {
            const wchar_t *text = static_cast<const wchar_t *>(src);
            const SIZE_T count = fallbackSize / sizeof(wchar_t);
            for (SIZE_T i = 0; i < count; ++i)
            {
                if (text[i] == L'\0')
                {
                    result = (i + 1) * sizeof(wchar_t);
                    break;
                }
            }
        }
        else if (format == CF_TEXT || format == CF_OEMTEXT)
        {
            const char *text = static_cast<const char *>(src);
            for (SIZE_T i = 0; i < fallbackSize; ++i)
            {
                if (text[i] == '\0')
                {
                    result = i + 1;
                    break;
                }
            }
        }
        GlobalUnlock(data);
        return result;
    }

    static SIZE_T clipboardDataSize(HGLOBAL data, CLIPFORMAT format)
    {
        const SIZE_T size = GlobalSize(data);
        if (!size)
            return 0;
        if (format == CF_UNICODETEXT || format == CF_TEXT || format == CF_OEMTEXT)
            return boundedTextSize(data, format, size);
        return size;
    }

    static HGLOBAL duplicateHGlobal(HGLOBAL src, CLIPFORMAT format)
    {
        const SIZE_T size = clipboardDataSize(src, format);
        if (!size)
            return nullptr;

        HGLOBAL dstHandle = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!dstHandle)
            return nullptr;

        const void *srcData = GlobalLock(src);
        void *dstData = GlobalLock(dstHandle);
        if (!srcData || !dstData)
        {
            if (dstData)
                GlobalUnlock(dstHandle);
            if (srcData)
                GlobalUnlock(src);
            GlobalFree(dstHandle);
            return nullptr;
        }

        memcpy(dstData, srcData, size);
        GlobalUnlock(dstHandle);
        GlobalUnlock(src);
        return dstHandle;
    }

    mutable std::recursive_mutex m_mutex;
    std::vector<DataEntry *> m_entries;
};

SClipboard::SClipboard()
    : m_clipDataObject(new SOhosClipboardDataObject())
    , m_externalDataObject(nullptr)
    , m_owner(0)
    , m_open(FALSE)
{
}

SClipboard::~SClipboard()
{
    if (m_externalDataObject)
    {
        m_externalDataObject->Release();
        m_externalDataObject = nullptr;
    }
    if (m_clipDataObject)
    {
        m_clipDataObject->Release();
        m_clipDataObject = nullptr;
    }
}

UINT SClipboard::RegisterClipboardFormatA(LPCSTR pszName)
{
    if (!pszName || !*pszName)
        return 0;
    std::lock_guard<std::recursive_mutex> lock(s_clipFmtMutex);
    std::string name(pszName);
    auto it = s_clipFmtMap.find(name);
    if (it != s_clipFmtMap.end())
        return it->second;
    UINT fmt = s_nextClipFmt++;
    s_clipFmtMap[name] = fmt;
    return fmt;
}

bool SClipboard::hasFormat(UINT fmtAtom)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    FORMATETC fmt = { (CLIPFORMAT)fmtAtom, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (m_externalDataObject && m_externalDataObject->QueryGetData(&fmt) == S_OK)
        return true;
    return m_clipDataObject && m_clipDataObject->QueryGetData(&fmt) == S_OK;
}

HANDLE SClipboard::getClipboardData(UINT fmtAtom)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_clipDataObject)
    {
        HGLOBAL hData = m_clipDataObject->dataHandle((CLIPFORMAT)fmtAtom);
        if (hData)
            return hData;
    }
    if (!m_externalDataObject)
        return 0;

    FORMATETC fmt = { (CLIPFORMAT)fmtAtom, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = { 0 };
    if (m_externalDataObject->GetData(&fmt, &medium) != S_OK)
        return 0;
    if (m_clipDataObject->SetData(&fmt, &medium, FALSE) != S_OK)
    {
        ReleaseStgMedium(&medium);
        return 0;
    }
    ReleaseStgMedium(&medium);
    return m_clipDataObject->dataHandle((CLIPFORMAT)fmtAtom);
}

HANDLE SClipboard::setClipboardData(UINT fmtAtom, HANDLE hMem)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_open)
    {
        if (hMem)
            GlobalFree((HGLOBAL)hMem);
        return 0;
    }
    if (m_externalDataObject)
    {
        m_externalDataObject->Release();
        m_externalDataObject = nullptr;
    }
    if (!hMem)
        return 0;

    FORMATETC fmt = { (CLIPFORMAT)fmtAtom, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium = { 0 };
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = (HGLOBAL)hMem;
    if (m_clipDataObject->SetData(&fmt, &medium, TRUE) != S_OK)
    {
        if (medium.hGlobal)
            GlobalFree(medium.hGlobal);
        return 0;
    }
    return hMem;
}

BOOL SClipboard::openClipboard(HWND hWndNewOwner)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_open = TRUE;
    m_owner = hWndNewOwner;
    return TRUE;
}

BOOL SClipboard::closeClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_open = FALSE;
    return TRUE;
}

HWND SClipboard::getClipboardOwner() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_owner;
}

BOOL SClipboard::emptyClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_open)
        return FALSE;
    if (m_clipDataObject)
    {
        m_clipDataObject->clear();
    }
    if (m_externalDataObject)
    {
        m_externalDataObject->Release();
        m_externalDataObject = nullptr;
    }
    return TRUE;
}

IDataObject *SClipboard::getDataObject()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    IDataObject *ret = m_externalDataObject;
    if (!ret && m_clipDataObject && !m_clipDataObject->empty())
        ret = m_clipDataObject;
    if (!ret)
        return nullptr;
    ret->AddRef();
    return ret;
}

BOOL SClipboard::isCurrentClipboard(IDataObject *pDo)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_externalDataObject)
        return pDo == m_externalDataObject;
    return pDo == m_clipDataObject;
}

BOOL SClipboard::setDataObject(IDataObject *pDo)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_open)
        return FALSE;
    if (m_externalDataObject)
    {
        m_externalDataObject->Release();
        m_externalDataObject = nullptr;
    }
    m_externalDataObject = pDo;
    if (m_externalDataObject)
        m_externalDataObject->AddRef();
    return TRUE;
}

void SClipboard::flushClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_externalDataObject)
        return;

    IEnumFORMATETC *enumFmt = nullptr;
    if (m_externalDataObject->EnumFormatEtc(DATADIR_GET, &enumFmt) != S_OK)
        return;

    m_clipDataObject->clear();
    FORMATETC fmt = { 0 };
    while (enumFmt->Next(1, &fmt, nullptr) == S_OK)
    {
        if (!(fmt.tymed & TYMED_HGLOBAL))
            continue;
        STGMEDIUM medium = { 0 };
        if (m_externalDataObject->GetData(&fmt, &medium) == S_OK)
        {
            m_clipDataObject->SetData(&fmt, &medium, FALSE);
            ReleaseStgMedium(&medium);
        }
    }
    enumFmt->Release();
    m_externalDataObject->Release();
    m_externalDataObject = nullptr;
}
