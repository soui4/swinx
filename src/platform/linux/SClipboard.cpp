#include "SConnection.h"
#include "SClipboard.h"
#include "debug.h"
#define kLogTag "SClipboard"
#include <vector>
#include "tostring.hpp"

class SMimeEnumFORMATETC : public SUnkImpl<IEnumFORMATETC> {
    SMimeData *m_mimeData;
    ULONG m_iFmt;

  public:
    SMimeEnumFORMATETC(SMimeData *mimeData)
        : m_mimeData(mimeData)
        , m_iFmt(0)
    {
    }

    HRESULT STDMETHODCALLTYPE Next(
        /* [in] */ ULONG celt,
        /* [annotation] */
        _Out_writes_to_(celt, *pceltFetched) FORMATETC *rgelt,
        /* [annotation] */
        _Out_opt_ ULONG *pceltFetched) override
    {
        if (m_iFmt + celt > m_mimeData->m_lstData.size())
            return E_UNEXPECTED;
        if (pceltFetched)
            *pceltFetched = m_iFmt;
        auto it = m_mimeData->m_lstData.begin();
        ULONG i = 0;
        while (i < m_iFmt)
        {
            it++;
            i++;
        }
        for (ULONG j = 0; j < celt; j++)
        {
            rgelt[j].cfFormat = (*it)->fmt;
            rgelt[j].tymed = TYMED_HGLOBAL;
            it++;
        }
        m_iFmt += celt;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Skip(/* [in] */ ULONG celt) override
    {
        m_iFmt += celt;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Reset(void) override
    {
        m_iFmt = 0;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Clone(/* [out] */ __RPC__deref_out_opt IEnumFORMATETC **ppenum) override
    {
        return E_NOTIMPL;
    }

    IUNKNOWN_BEGIN(IEnumFORMATETC)
    IUNKNOWN_END()
};

SMimeData::SMimeData()
{
    m_fmtEnum = new SMimeEnumFORMATETC(this);
}

SMimeData::~SMimeData()
{
    clear();
    m_fmtEnum->Release();
}

void SMimeData::clear()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it : m_lstData)
    {
        delete it;
    }
    m_lstData.clear();
    SLOG_STMD() << "clear mimedata";
}

void SMimeData::set(FormatedData *data)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_lstData.begin(); it != m_lstData.end(); it++)
    {
        if ((*it)->fmt == data->fmt)
        {
            delete *it;
            m_lstData.erase(it);
            break;
        }
    }
    m_lstData.push_back(data);
//    SLOG_STMI() << "SMimeData::set,format=" << data->fmt << " format size=" << m_lstData.size();
}

HRESULT SMimeData::QueryGetData(FORMATETC *pformatetc)
{
    for (auto it : m_lstData)
    {
        if (it->fmt == pformatetc->cfFormat && pformatetc->tymed == TYMED_HGLOBAL)
            return S_OK;
    }
    return S_OK;
}

HRESULT SMimeData::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    for (auto it : m_lstData)
    {
        if (it->fmt == pformatetcIn->cfFormat && pformatetcIn->tymed == TYMED_HGLOBAL)
        {
            assert(it->data);
            memset(pmedium, 0, sizeof(STGMEDIUM));
            const void *src = GlobalLock(it->data);
            int srcSize = GlobalSize(it->data);
            if(pformatetcIn->cfFormat == CF_UNICODETEXT)
            {
                srcSize = wcslen((const wchar_t *)src) * sizeof(wchar_t) + sizeof(wchar_t);
            }else if(pformatetcIn->cfFormat == CF_TEXT)
            {
                srcSize = strlen((const char *)src)+1;
            }
            pmedium->hGlobal = GlobalAlloc(0, srcSize);
            void *dst = GlobalLock(pmedium->hGlobal);
            memcpy(dst, src, srcSize);
            GlobalUnlock(it->data);
            GlobalUnlock(pmedium->hGlobal);
            pmedium->tymed = TYMED_HGLOBAL;
            return S_OK;
        }
    }
    return DV_E_FORMATETC;
}

HRESULT SMimeData::GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium)
{
    for (auto it : m_lstData)
    {
        if (it->fmt == pformatetc->cfFormat && pformatetc->tymed == TYMED_HGLOBAL)
        {
            assert(pmedium->hGlobal);
            const void *src = GlobalLock(it->data);
            void *dst = GlobalLock(pmedium->hGlobal);
            memcpy(dst, src, std::min(GlobalSize(it->data), GlobalSize(pmedium->hGlobal)));
            GlobalUnlock(it->data);
            GlobalUnlock(pmedium->hGlobal);
            pmedium->tymed = TYMED_HGLOBAL;
            return S_OK;
        }
    }
    return DV_E_FORMATETC;
}

HRESULT SMimeData::SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease)
{
    if (pmedium->tymed != TYMED_HGLOBAL)
        return DV_E_TYMED;
    FormatedData *pdata = new FormatedData;
    pdata->fmt = pformatetc->cfFormat;
    if (fRelease)
    {
        pdata->data = pmedium->hGlobal;
    }
    else
    {
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

HRESULT SMimeData::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc)
{
    if (dwDirection == DATADIR_GET)
    {
        *ppenumFormatEtc = m_fmtEnum;
        m_fmtEnum->Reset();
        m_fmtEnum->AddRef();
        return S_OK;
    }
    return E_NOTIMPL;
}

//-------------------------------------------------------
SDataObjectProxy::SDataObjectProxy(SConnection *conn, HWND hWnd)
    : m_conn(conn)
    , m_hSource(hWnd)
{
}

SDataObjectProxy::~SDataObjectProxy()
{
}

void SDataObjectProxy::fetchDataTypeList()
{
    m_lstTypes.clear();
    std::shared_ptr<std::vector<char>> data = m_conn->getClipboard()->getDataInFormat(m_conn->atoms.CLIPBOARD, m_conn->atoms.TARGETS,SClipboard::kReadFormatTimeout);
    if (data)
    {
        const char *buf = data->data();
        size_t len = data->size();
        for (size_t i = 0; i < len; i += sizeof(xcb_atom_t))
        {
            xcb_atom_t atom = *(xcb_atom_t *)buf;
            // char buf2[200]={0};
            // SAtoms::getAtomName(atom,buf2,200);
            // SLOG_STMI()<<"clipboard atom="<<atom<<" name="<<buf2;
            m_lstTypes.push_back(m_conn->atom2ClipFormat(atom));
            buf += sizeof(xcb_atom_t);
        }
    }
}

HRESULT SDataObjectProxy::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    if (QueryGetData(pformatetcIn) != S_OK)
    {
        return DV_E_FORMATETC;
    }
    std::shared_ptr<std::vector<char>> buf = m_conn->readSelection(isXdnd(), pformatetcIn->cfFormat);
    if (!buf)
        return DV_E_DVASPECT;
    size_t bufLen = buf->size();
    pmedium->hGlobal = GlobalAlloc(0, bufLen+1);
    if (!pmedium->hGlobal)
        return STG_E_MEDIUMFULL;
    char *dst = (char*)GlobalLock(pmedium->hGlobal);
    memcpy(dst, buf->data(), bufLen);
    dst[bufLen]=0;//append a null
    GlobalUnlock(pmedium->hGlobal);
    return S_OK;
}

//---------------------------------------------------------------------------
class INCRTransaction;
typedef std::map<xcb_window_t, INCRTransaction *> TransactionMap;
static __thread TransactionMap *transactions = 0;
static VOID CALLBACK OnClipboardTimeout(HWND hWnd, UINT msg, UINT_PTR timerId, DWORD ts);

#define INCR_DEBUG 0
class INCRTransaction {
  public:
    INCRTransaction(SConnection *c, xcb_window_t w, xcb_atom_t p, std::shared_ptr<std::vector<char>> d, uint32_t inc, xcb_atom_t t, int to)
        : conn(c)
        , win(w)
        , property(p)
        , data(d)
        , increment(inc)
        , target(t)
        , timeout(to)
        , offset(0)
    {
        const uint32_t values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
        xcb_change_window_attributes(conn->connection, win, XCB_CW_EVENT_MASK, values);
        if (!transactions)
        {
#if INCR_DEBUG
            SLOG_STMI() << "INCRTransaction: creating the TransactionMap";
#endif
            transactions = new TransactionMap;
            conn->getClipboard()->setProcessIncr(true);
        }
        transactions->insert(std::make_pair(win, this));
        abort_timer = SetTimer(0, 0, timeout, OnClipboardTimeout);
    }

    ~INCRTransaction()
    {
        if (abort_timer)
            KillTimer(0, abort_timer);
        abort_timer = 0;
        transactions->erase(win);
        if (transactions->empty())
        {
#if INCR_DEBUG
            SLOG_STMI() << "INCRTransaction: no more INCR transactions left in the TransactionMap";
#endif
            delete transactions;
            transactions = 0;
            conn->getClipboard()->setProcessIncr(false);
        }
    }

    void updateIncrProperty(xcb_property_notify_event_t *event, bool &accepted)
    {
        xcb_connection_t *c = conn->connection;
        if (event->atom == property && event->state == XCB_PROPERTY_DELETE)
        {
            accepted = true;
            // restart the timer
            if (abort_timer)
                KillTimer(0, abort_timer);
            abort_timer = SetTimer(0, 0, timeout, OnClipboardTimeout);

            uint32_t bytes_left = data->size() - offset;
            if (bytes_left > 0)
            {
                uint32_t bytes_to_send = std::min(increment, bytes_left);
#if INCR_DEBUG
                SLOG_FMTI("INCRTransaction: sending %d bytes, %d remaining (INCR transaction %p)", bytes_to_send, bytes_left - bytes_to_send, this);
#endif
                int dataSize = bytes_to_send;
                xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, property, target, 8, dataSize, data->data() + offset);
                offset += bytes_to_send;
            }
            else
            {
#if INCR_DEBUG
                SLOG_FMTI("INCRTransaction: INCR transaction %p completed", this);
#endif
                xcb_change_property(c, XCB_PROP_MODE_REPLACE, win, property, target, 8, 0, (const void *)0);
                const uint32_t values[] = { XCB_EVENT_MASK_NO_EVENT };
                xcb_change_window_attributes(conn->connection, win, XCB_CW_EVENT_MASK, values);
                // self destroy
                delete this;
            }
        }
    }

    BOOL onTimer(UINT_PTR timerID)
    {
        if (timerID != abort_timer)
            return FALSE;
            // this can happen when the X client we are sending data
            // to decides to exit (normally or abnormally)
#ifdef INCR_DEBUG
        SLOG_STMI() << "INCRTransaction: Timed out while sending data to " << this;
#endif
        delete this;
        return TRUE;
    }

  private:
    SConnection *conn;
    xcb_window_t win;
    xcb_atom_t property;
    std::shared_ptr<std::vector<char>> data;
    uint32_t increment;
    xcb_atom_t target;
    int timeout;
    uint32_t offset;
    UINT_PTR abort_timer;
};

static VOID CALLBACK OnClipboardTimeout(HWND hWnd, UINT msg, UINT_PTR timerId, DWORD ts)
{
    for (auto it : (*transactions))
    {
        if (it.second->onTimer(timerId))
            break;
    }
}
//-------------------------------------------------------------------------

SClipboard::SClipboard(SConnection *conn)
    : m_conn(conn)
    , m_requestor(XCB_NONE)
    , m_owner(XCB_NONE)
    , m_bOpen(FALSE)
    , m_ts(XCB_CURRENT_TIME)
    , m_doSel(NULL)
    , m_incr_active(FALSE)
    , m_doExClip(NULL)
{
    m_doClip = new SMimeData();
    m_owner = xcb_generate_id(xcb_connection());
    xcb_create_window(xcb_connection(),
                      XCB_COPY_FROM_PARENT, // depth -- same as root
                      m_owner,              // window id
                      conn->screen->root,   // parent window id
                      0, 0, 3, 3,
                      0,                           // border width
                      XCB_WINDOW_CLASS_INPUT_ONLY, // window class
                      conn->screen->root_visual,   // visual
                      0,                           // value mask
                      0);

    m_requestor = xcb_generate_id(xcb_connection());
    xcb_create_window(xcb_connection(),
                      XCB_COPY_FROM_PARENT, // depth -- same as root
                      m_requestor,          // window id
                      conn->screen->root,   // parent window id
                      0, 0, 3, 3,
                      0,                           // border width
                      XCB_WINDOW_CLASS_INPUT_ONLY, // window class
                      conn->screen->root_visual,   // visual
                      0,                           // value mask
                      0);                          // value list
    m_conn->flush();
}

SClipboard::~SClipboard()
{
    if (getClipboardOwner() == m_owner)
    {
        emptyClipboard();
    }
    m_doClip->Release();
    m_doClip = NULL;
    xcb_destroy_window(xcb_connection(), m_requestor);
    m_requestor = XCB_NONE;
    xcb_destroy_window(xcb_connection(), m_owner);
    m_owner = XCB_NONE;
    m_conn->flush();
}

xcb_connection_t *SClipboard::xcb_connection() const
{
    return m_conn->connection;
}

static inline int maxSelectionIncr(xcb_connection_t *c)
{
    int l = xcb_get_maximum_request_length(c);
    return (l > 65536 ? 65536 * 4 : l * 4) - 100;
}

bool SClipboard::clipboardReadProperty(xcb_window_t win, xcb_atom_t property, bool deleteProperty, std::vector<char> *buffer, int *size, xcb_atom_t *type, int *format)
{
    int maxsize = maxSelectionIncr(xcb_connection());
    unsigned long bytes_left; // bytes_after
    xcb_atom_t dummy_type;
    int dummy_format;

    if (!type) // allow null args
        type = &dummy_type;
    if (!format)
        format = &dummy_format;

    // Don't read anything, just get the size of the property data
    xcb_get_property_cookie_t cookie = xcb_get_property(xcb_connection(), false, win, property, XCB_GET_PROPERTY_TYPE_ANY, 0, 0);
    xcb_get_property_reply_t *reply = xcb_get_property_reply(xcb_connection(), cookie, 0);
    if (!reply || reply->type == XCB_NONE)
    {
        free(reply);
        return false;
    }
    *type = reply->type;
    *format = reply->format;
    bytes_left = reply->bytes_after;
    free(reply);

    int offset = 0, buffer_offset = 0;

    int newSize = bytes_left;
    buffer->resize(newSize);

    bool ok = (buffer->size() == newSize);

    if (ok && newSize)
    {
        // could allocate buffer

        while (bytes_left)
        {
            // more to read...

            xcb_get_property_cookie_t cookie = xcb_get_property(xcb_connection(), false, win, property, XCB_GET_PROPERTY_TYPE_ANY, offset, maxsize / 4);
            reply = xcb_get_property_reply(xcb_connection(), cookie, 0);
            if (!reply || reply->type == XCB_NONE)
            {
                free(reply);
                break;
            }
            *type = reply->type;
            *format = reply->format;
            bytes_left = reply->bytes_after;
            char *data = (char *)xcb_get_property_value(reply);
            int length = xcb_get_property_value_length(reply);

            // Here we check if we get a buffer overflow and tries to
            // recover -- this shouldn't normally happen, but it doesn't
            // hurt to be defensive
            if ((int)(buffer_offset + length) > buffer->size())
            {
                SLOG_STMW() << "SClipboard: buffer overflow";
                length = buffer->size() - buffer_offset;

                // escape loop
                bytes_left = 0;
            }

            memcpy(buffer->data() + buffer_offset, data, length);
            buffer_offset += length;

            if (bytes_left)
            {
                // offset is specified in 32-bit multiples
                offset += length / 4;
            }
            free(reply);
        }
    }

    // correct size, not 0-term.
    if (size)
        *size = buffer_offset;
    if (*type == m_conn->atoms.INCR)
        m_incr_receive_time = m_conn->getSectionTs();
    if (deleteProperty)
        xcb_delete_property(xcb_connection(), win, property);

    m_conn->flush();

    return ok;
}

xcb_atom_t SClipboard::sendTargetsSelection(IDataObject *d, xcb_window_t window, xcb_atom_t property)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    std::vector<xcb_atom_t> types;
    IEnumFORMATETC *enumFmt;
    if (d->EnumFormatEtc(DATADIR_GET, &enumFmt) == S_OK)
    {
        FORMATETC fmt;
        while (enumFmt->Next(1, &fmt, NULL) == S_OK)
        {
            if (fmt.tymed == TYMED_HGLOBAL)
            {
                types.push_back(m_conn->clipFormat2Atom(fmt.cfFormat));
            }
        }
        enumFmt->Release();
    }
    types.push_back(m_conn->atoms.TARGETS);
    types.push_back(m_conn->atoms.MULTIPLE);
    types.push_back(m_conn->atoms.TIMESTAMP);
    types.push_back(m_conn->atoms.SAVE_TARGETS);

    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property, XCB_ATOM_ATOM, 32, types.size(), (const void *)types.data());
    return property;
}

xcb_atom_t SClipboard::sendSelection(IDataObject *d, xcb_atom_t target, xcb_window_t window, xcb_atom_t property)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    IEnumFORMATETC *enumFmt;
    HGLOBAL hData = NULL;
    int dataFormat = 0;
    if (d->EnumFormatEtc(DATADIR_GET, &enumFmt) == S_OK)
    {
        FORMATETC fmt;
        while (enumFmt->Next(1, &fmt, NULL) == S_OK)
        {
            if (fmt.tymed == TYMED_HGLOBAL && m_conn->clipFormat2Atom(fmt.cfFormat) == target)
            {
                STGMEDIUM medium = { 0 };
                d->GetData(&fmt, &medium);
                hData = medium.hGlobal;
                dataFormat = fmt.cfFormat;
                break;
            }
        }
        enumFmt->Release();
    }
    if (!hData)
    {
        return XCB_NONE;
    }
    xcb_atom_t atomFormat = target;
    // don't allow INCR transfers when using MULTIPLE or to
    // Motif clients (since Motif doesn't support INCR)
    bool allow_incr = property != m_conn->atoms.CLIP_TEMPORARY;

    // X_ChangeProperty protocol request is 24 bytes
    const int increment = (xcb_get_maximum_request_length(xcb_connection()) * 4) - 24;
    size_t len = GlobalSize(hData);
    if (len > increment && allow_incr)
    {
        uint32_t bytes = increment;
        std::shared_ptr<std::vector<char>> data = std::make_shared<std::vector<char>>(bytes);
        const char *src = (const char *)GlobalLock(hData);
        memcpy(data->data(), src, bytes);
        GlobalUnlock(hData);
        GlobalFree(hData);
        xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property, m_conn->atoms.INCR, 32, 1, (const void *)&bytes);
        new INCRTransaction(m_conn, window, property, data, increment, atomFormat, kIncrTimeout);
        return property;
    }

    // make sure we can perform the XChangeProperty in a single request
    if (len > increment)
    {
        GlobalFree(hData);
        return XCB_NONE; // ### perhaps use several XChangeProperty calls w/ PropModeAppend?
    }
    // use a single request to transfer data
    const char *buf = (const char *)GlobalLock(hData);
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property, target, 8, len, buf);
    GlobalUnlock(hData);
    GlobalFree(hData);

    return property;
}

void SClipboard::handleSelectionRequest(xcb_selection_request_event_t *req)
{
    xcb_selection_notify_event_t event;
    event.response_type = XCB_SELECTION_NOTIFY;
    event.requestor = req->requestor;
    event.selection = req->selection;
    event.target = req->target;
    event.property = XCB_NONE;
    event.time = req->time;
    if (req->selection != m_conn->atoms.CLIPBOARD && req->selection != m_conn->atoms.XdndSelection)
    {
        xcb_send_event(m_conn->connection, false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }
    if (req->selection == m_conn->atoms.XdndSelection && !m_doSel)
    {
        xcb_send_event(m_conn->connection, false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }
    if (req->time != XCB_CURRENT_TIME && m_ts != XCB_CURRENT_TIME && req->time < m_ts)
    {
        xcb_send_event(m_conn->connection, false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }

    struct AtomPair
    {
        xcb_atom_t target;
        xcb_atom_t property;
    };
    AtomPair *multi = nullptr;
    xcb_atom_t multi_type = XCB_NONE;
    int multi_format = 0;
    int nmulti = 0;
    int imulti = -1;
    bool multi_writeback = false;

    if (req->target == m_conn->atoms.MULTIPLE)
    {
        std::vector<char> multi_data;
        if (req->property == XCB_NONE || !clipboardReadProperty(req->requestor, req->property, false, &multi_data, 0, &multi_type, &multi_format) || multi_format != 32)
        {
            // MULTIPLE property not formatted correctly
            xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
            return;
        }
        nmulti = multi_data.size() / sizeof(AtomPair);
        multi = new AtomPair[nmulti];
        memcpy(multi, multi_data.data(), multi_data.size());
        imulti = 0;
    }

    IDataObject *pDo = req->selection != m_conn->atoms.CLIPBOARD ? m_doSel : m_doClip;
    for (; imulti < nmulti; ++imulti)
    {
        xcb_atom_t target;
        xcb_atom_t property;

        if (multi)
        {
            target = multi[imulti].target;
            property = multi[imulti].property;
        }
        else
        {
            target = req->target;
            property = req->property;
            if (property == XCB_NONE) // obsolete client
                property = target;
        }

        xcb_atom_t ret = XCB_NONE;
        if (target == XCB_NONE || property == XCB_NONE)
        {
            SLOG_STMW() << "SClipboard: Invalid target or property timestamp";
        }
        else if (target == m_conn->atoms.TIMESTAMP)
        {
            if (m_ts != XCB_CURRENT_TIME)
            {
                xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, req->requestor, property, XCB_ATOM_INTEGER, 32, 1, &m_ts);
                ret = property;
            }
            else
            {
                SLOG_STMW() << "SClipboard: Invalid data timestamp";
            }
        }
        else if (target == m_conn->atoms.TARGETS)
        {
            ret = sendTargetsSelection(pDo, req->requestor, property);
        }
        else
        {
            ret = sendSelection(pDo, target, req->requestor, property);
        }

        if (nmulti > 0)
        {
            if (ret == XCB_NONE)
            {
                multi[imulti].property = XCB_NONE;
                multi_writeback = true;
            }
        }
        else
        {
            event.property = ret;
            break;
        }
    }

    if (nmulti > 0)
    {
        if (multi_writeback)
        {
            // according to ICCCM 2.6.2 says to put None back
            // into the original property on the requestor window
            xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, req->requestor, req->property, multi_type, 32, nmulti * 2, (const void *)multi);
        }

        delete[] multi;
        event.property = req->property;
    }
    // send selection notify to requestor
    xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
}

void SClipboard::handleSelectionClear(xcb_selection_clear_event_t *e)
{
    if (e->selection != m_conn->atoms.CLIPBOARD)
        return;
    if (m_ts != XCB_CURRENT_TIME && e->time < m_ts)
        return;
    xcb_window_t owner = getClipboardOwner();
    if (owner != m_owner)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        if (m_doExClip)
        {
            m_doExClip->Release();
            m_doExClip = NULL;
        }
        m_doClip->clear();
        m_ts = XCB_CURRENT_TIME;
    }
}

void SClipboard::incrTransactionPeeker(xcb_generic_event_t *ge, bool &accepted)
{
    uint response_type = ge->response_type & ~0x80;
    if (response_type == XCB_PROPERTY_NOTIFY)
    {
        xcb_property_notify_event_t *event = (xcb_property_notify_event_t *)ge;
        auto it = transactions->find(event->window);
        if (it != transactions->end())
        {
            it->second->updateIncrProperty(event, accepted);
        }
    }
}

xcb_window_t SClipboard::getClipboardOwner() const
{
    xcb_get_selection_owner_cookie_t owner_cookie = xcb_get_selection_owner(xcb_connection(), m_conn->atoms.CLIPBOARD);
    xcb_get_selection_owner_reply_t *owner_reply = xcb_get_selection_owner_reply(xcb_connection(), owner_cookie, NULL);
    xcb_window_t win = owner_reply->owner;
    free(owner_reply);
    return win;
}

BOOL SClipboard::emptyClipboard()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (m_doExClip)
    {
        m_doExClip->Release();
        m_doExClip = NULL;
    }
    m_doClip->clear();
    xcb_set_selection_owner(m_conn->connection, XCB_NONE, m_conn->atoms.CLIPBOARD, XCB_TIME_CURRENT_TIME);
    m_conn->flush();
    return TRUE;
}

BOOL SClipboard::isCurrentClipboard(IDataObject *pDo)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);

    if (m_doExClip)
        return pDo == m_doExClip;
    return pDo == m_doClip;
}

IDataObject *SClipboard::getDataObject(BOOL bSel)
{
    if (bSel)
    {
        if (!m_doSel)
            return nullptr;
        m_doSel->AddRef();
        return m_doSel;
    }
    xcb_window_t owner = GetClipboardOwner();
    if (owner == m_owner)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        IDataObject *ret = m_doExClip ? m_doExClip : m_doClip;
        ret->AddRef();
        return ret;
    }
    else
    {
        SDataObjectProxy *ret = new SDataObjectProxy(m_conn, owner);
        ret->fetchDataTypeList();
        return ret;
    }
}

BOOL SClipboard::setDataObject(IDataObject *pDo, BOOL bSel)
{
    if (bSel)
    { // used for xdnd
        if (m_doSel)
        {
            m_doSel->Release();
            m_doSel = NULL;
        }
        m_doSel = pDo;
        if (m_doSel)
        {
            m_doSel->AddRef();
            xcb_set_selection_owner(m_conn->connection, m_owner, m_conn->atoms.XdndSelection, XCB_CURRENT_TIME);
        }
        else
        {
            xcb_set_selection_owner(m_conn->connection, XCB_NONE, m_conn->atoms.XdndSelection, XCB_CURRENT_TIME);
        }
        return TRUE;
    }
    else
    { // used for clipboard
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        if (!m_bOpen)
            return FALSE;
        if (m_doExClip)
        {
            m_doExClip->Release();
            m_doExClip = NULL;
        }
        m_doExClip = pDo;
        if (m_doExClip)
        {
            m_doExClip->AddRef();
        }
        return TRUE;
    }
}

void SClipboard::flushClipboard()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (m_doExClip)
    {
        // copy extenal dataobject to internal dataobject
        m_doClip->clear();
        IEnumFORMATETC *enum_fmt;
        HRESULT hr = m_doExClip->EnumFormatEtc(DATADIR_GET, &enum_fmt);
        if (FAILED(hr))
            return;
        FORMATETC fmt;
        while (enum_fmt->Next(1, &fmt, NULL) == S_OK)
        {
            STGMEDIUM storage = { 0 };
            hr = m_doExClip->GetData(&fmt, &storage);
            if (hr == S_OK)
            {
                m_doClip->SetData(&fmt, &storage, FALSE);
            }
        }
        enum_fmt->Release();

        m_doExClip->Release();
        m_doExClip = NULL;
    }
    xcb_set_selection_owner(m_conn->connection, m_owner, m_conn->atoms.CLIPBOARD, XCB_CURRENT_TIME);
    m_conn->flush();
}

bool SClipboard::hasFormat(UINT fmt)
{
    xcb_atom_t fmtAtom = m_conn->clipFormat2Atom(fmt);
    // fatch formats
    std::shared_ptr<std::vector<char>> data = getDataInFormat(m_conn->atoms.CLIPBOARD, m_conn->atoms.TARGETS,kReadFormatTimeout);
    if (data)
    {
        const char *buf = data->data();
        size_t len = data->size();
        for (size_t i = 0; i < len; i += sizeof(xcb_atom_t))
        {
            xcb_atom_t atom = *(xcb_atom_t *)buf;
            if (atom == fmtAtom)
            {
                return true;
            }    
            buf += sizeof(xcb_atom_t);
        }
    }
    return false;
}

HANDLE SClipboard::getClipboardData(UINT fmt)
{
    xcb_atom_t fmtAtom = m_conn->clipFormat2Atom(fmt);
    std::shared_ptr<std::vector<char>> buf = getDataInFormat(m_conn->atoms.CLIPBOARD, fmtAtom, kWaitTimeout);
    if (!buf)
    {
        return nullptr;
    }
    if (fmt == CF_UNICODETEXT)
    {
        std::wstring str;
        towstring(buf->data(), buf->size(), str);
        HGLOBAL ret = GlobalAlloc(GMEM_MOVEABLE, (str.length() + 1) * sizeof(wchar_t));
        void *dst = GlobalLock(ret);
        memcpy(dst, str.data(), (str.length() + 1) * sizeof(wchar_t));
        GlobalUnlock(ret);
        return ret;
    }
    else
    {
        HGLOBAL ret = GlobalAlloc(GMEM_MOVEABLE, buf->size());
        void *dst = GlobalLock(ret);
        memcpy(dst, buf->data(), buf->size());
        GlobalUnlock(ret);
        return ret;
    }
}

HANDLE SClipboard::setClipboardData(UINT uFormat, HANDLE hMem)
{
    if (!m_bOpen)
    {
        GlobalFree(hMem);
        return 0;
    }
    if (uFormat == CF_UNICODETEXT)
    {
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
    IDataObject *pDo = getDataObject(FALSE);
    FORMATETC formatetc = { (CLIPFORMAT)uFormat, nullptr, 0, 0, TYMED_HGLOBAL }; // Pointer to the FORMATETC structure
    STGMEDIUM medium;
    pDo->SetData(&formatetc, &medium, TRUE);
    pDo->Release();
    m_ts = m_conn->getSectionTs();
    return hMem;
}

BOOL SClipboard::openClipboard(HWND hWndNewOwner)
{
    if (hWndNewOwner)
    {
        tid_t tid = GetWindowThreadProcessId(hWndNewOwner, NULL);
        if (tid != GetCurrentThreadId())
            return FALSE;
    }
    m_mutex.lock();
    if (m_bOpen)
        return FALSE;
    m_bOpen = TRUE;
    return TRUE;
}

BOOL SClipboard::closeClipboard()
{
    m_mutex.unlock();
    if (!m_bOpen)
        return FALSE;
    m_bOpen = FALSE;
    flushClipboard();
    return TRUE;
}

std::shared_ptr<std::vector<char>> SClipboard::getDataInFormat(xcb_atom_t modeAtom, xcb_atom_t fmtAtom, int timeout)
{
    return getSelection(modeAtom, fmtAtom, m_conn->atoms.SO_SELECTION,timeout, 0);
}

std::shared_ptr<std::vector<char>> SClipboard::getSelection(xcb_atom_t selection, xcb_atom_t fmtAtom, xcb_atom_t property, int timeout, xcb_timestamp_t time)
{
    std::shared_ptr<std::vector<char>> buf = std::make_shared<std::vector<char>>();
    if(selection == m_conn->atoms.CLIPBOARD){
        xcb_window_t owner = getClipboardOwner();
        if(owner == m_owner)
        {
            if(fmtAtom == m_conn->atoms.TARGETS){
                std::vector<xcb_atom_t> types;
                IEnumFORMATETC *enumFmt;
                if (m_doClip->EnumFormatEtc(DATADIR_GET, &enumFmt) == S_OK)
                {
                    FORMATETC fmt;
                    while (enumFmt->Next(1, &fmt, NULL) == S_OK)
                    {
                        if (fmt.tymed == TYMED_HGLOBAL)
                        {
                            types.push_back(m_conn->clipFormat2Atom(fmt.cfFormat));
                        }
                    }
                    enumFmt->Release();
                }
                buf->assign((char *)types.data(), (char *)types.data() + types.size() * sizeof(xcb_atom_t));
            }else{
                //read data from this process
                FORMATETC fmt = { (CLIPFORMAT)m_conn->atom2ClipFormat(fmtAtom), nullptr, 0, 0, TYMED_HGLOBAL };
                STGMEDIUM medium = { 0 };
                if(S_OK==m_doClip->GetData(&fmt, &medium)){
                    const char * src = (const char *)GlobalLock(medium.hGlobal);
                    buf->assign(src, src + GlobalSize(medium.hGlobal));
                    GlobalUnlock(medium.hGlobal);
                    ReleaseStgMedium(&medium);
                }
            }
            return buf;
        }
    }
    xcb_delete_property(xcb_connection(), m_requestor, property);
    xcb_convert_selection(xcb_connection(), m_requestor, selection, fmtAtom, property, time);

    m_conn->sync();

    xcb_generic_event_t *ge = waitForClipboardEvent(m_requestor, XCB_SELECTION_NOTIFY, timeout, selection);
    bool no_selection = !ge || ((xcb_selection_notify_event_t *)ge)->property == XCB_NONE;
    free(ge);

    if (no_selection)
        return buf;

    xcb_atom_t type;
    std::vector<char> buf2;
    if (clipboardReadProperty(m_requestor, property, true, &buf2, 0, &type, 0))
    {
        if (type == m_conn->atoms.INCR)
        {
            int nbytes = buf->size() >= 4 ? *((int *)buf->data()) : 0;
            clipboardReadIncrementalProperty(m_requestor, property, selection, nbytes, false, buf);
        }
        else
        {
            size_t oldLen = buf->size();
            buf->resize(buf->size() + buf2.size());
            memcpy(buf->data() + oldLen, buf2.data(), buf2.size());
        }
    }

    return buf;
}

class Notify : public IEventChecker {
  public:
    Notify(xcb_window_t win, int t)
        : window(win)
        , type(t)
    {
    }
    xcb_window_t window;
    int type;
    bool checkEvent(xcb_generic_event_t *event) const
    {
        if (!event)
            return false;
        int t = event->response_type & 0x7f;
        if (t != type)
            return false;
        if (t == XCB_PROPERTY_NOTIFY)
        {
            xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)event;
            if (pn->window == window)
                return true;
        }
        else if (t == XCB_SELECTION_NOTIFY)
        {
            xcb_selection_notify_event_t *sn = (xcb_selection_notify_event_t *)event;
            if (sn->requestor == window)
                return true;
        }
        return false;
    }
};
class ClipboardEvent : public IEventChecker {
  public:
    ClipboardEvent(xcb_atom_t atomSel)
    {
        m_atomSel = atomSel;
    }
    xcb_atom_t m_atomSel;
    bool checkEvent(xcb_generic_event_t *e) const
    {
        if (!e)
            return false;
        int type = e->response_type & 0x7f;
        if (type == XCB_SELECTION_REQUEST)
        {
            xcb_selection_request_event_t *sr = (xcb_selection_request_event_t *)e;
            return sr->selection == XCB_ATOM_PRIMARY || sr->selection == m_atomSel;
        }
        else if (type == XCB_SELECTION_CLEAR)
        {
            xcb_selection_clear_event_t *sc = (xcb_selection_clear_event_t *)e;
            return sc->selection == XCB_ATOM_PRIMARY || sc->selection == m_atomSel;
        }
        return false;
    }
};

xcb_window_t SClipboard::getClipboardMgrOwner()
{
    xcb_window_t ret = XCB_NONE;
    xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(xcb_connection(), m_conn->atoms.CLIPBOARD_MANAGER);
    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(xcb_connection(), cookie, 0);
    if (reply)
        ret = reply->owner;
    free(reply);
    return ret;
}

xcb_generic_event_t *SClipboard::waitForClipboardEvent(xcb_window_t win, int type, int timeout, xcb_atom_t selAtom, bool checkManager)
{
    uint64_t ts1 = GetTickCount64();
    for(;;)
    {
        Notify notify(win, type);
        xcb_generic_event_t *e = m_conn->checkEvent(&notify);
        if (e)
            return e;

        if (checkManager)
        {
            if (getClipboardMgrOwner() == XCB_NONE)
                return nullptr;
        }

        // process other clipboard events, since someone is probably requesting data from us
        ClipboardEvent clipboard(selAtom);
        e = m_conn->checkEvent(&clipboard);
        if (e)
        {
            m_conn->pushEvent(e);
            free(e);
            MSG msg;
            while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        m_conn->flush();
        uint64_t ts2 = GetTickCount64();
        uint64_t cost = ts2 - ts1;
        if (cost >= timeout)
            break;
        Sleep(std::min(50, timeout - (int)cost));
    }

    return nullptr;
}

void SClipboard::clipboardReadIncrementalProperty(xcb_window_t win, xcb_atom_t property, xcb_atom_t selection, int nbytes, bool nullterm, std::shared_ptr<std::vector<char>> bufOut)
{
    std::vector<char> tmp_buf;
    xcb_timestamp_t prev_time = m_incr_receive_time;

    for (;;)
    {
        m_conn->flush();
        xcb_generic_event_t *ge = waitForClipboardEvent(win, XCB_PROPERTY_NOTIFY, kWaitTimeout, selection);
        if (!ge)
            break;
        xcb_property_notify_event_t *event = (xcb_property_notify_event_t *)ge;

        if (event->atom != property || event->state != XCB_PROPERTY_NEW_VALUE || event->time < prev_time)
            continue;
        prev_time = event->time;
        int length = 0;
        if (clipboardReadProperty(win, property, true, &tmp_buf, &length, 0, 0))
        {
            if (length == 0)
            { // no more data, we're done
                break;
            }
            else
            {
                size_t oldLen = bufOut->size();
                bufOut->resize(bufOut->size() + tmp_buf.size());
                memcpy(bufOut->data() + oldLen, tmp_buf.data(), tmp_buf.size());
            }
        }
        else
        {
            break;
        }

        free(ge);
    }
}
