#include "SDragdrop.h"
#include "SConnection.h"
#include "SClipboard.h"
#include <vector>
#include "log.h"
#define kLogTag "sdragdrop"

/***
 * OLEDD_GetButtonState()
 *
 * This method will use the current state of the keyboard to build
 * a button state mask equivalent to the one passed in the
 * WM_MOUSEMOVE wParam.
 */
static DWORD OLEDD_GetButtonState(void)
{
    BYTE  keyboardState[256];
    DWORD keyMask = 0;

    GetKeyboardState(keyboardState);

    if ((keyboardState[VK_SHIFT] & 0x80) != 0)
        keyMask |= MK_SHIFT;

    if ((keyboardState[VK_CONTROL] & 0x80) != 0)
        keyMask |= MK_CONTROL;

    if ((keyboardState[VK_MENU] & 0x80) != 0)
        keyMask |= MK_ALT;

    if ((keyboardState[VK_LBUTTON] & 0x80) != 0)
        keyMask |= MK_LBUTTON;

    if ((keyboardState[VK_RBUTTON] & 0x80) != 0)
        keyMask |= MK_RBUTTON;

    if ((keyboardState[VK_MBUTTON] & 0x80) != 0)
        keyMask |= MK_MBUTTON;

    return keyMask;
}

static inline BOOL is_droptarget(HWND hwnd)
{
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->IsDropTarget(hwnd);
}

/***
 * OLEDD_TrackStateChange()
 *
 * This method is invoked while a drag and drop operation is in effect.
 *
 * params:
 *    trackerInfo - Pointer to the structure identifying the
 *                  drag & drop operation that is currently
 *                  active.
 */
LRESULT SDragDrop::TrackStateChange(UINT uMsg, WPARAM wp, LPARAM lp)
{
    HWND   hwndNewTarget = 0;
    POINT pt;

    /*
     * This method may be called from QueryContinueDrag again,
     * (i.e. by running message loop) so avoid recursive call chain.
     */
    if (inTrackCall) return 0;
    inTrackCall = TRUE;

    /*
     * Get the handle of the window under the mouse
     */
    pt.x = curMousePos.x;
    pt.y = curMousePos.y;
    hwndNewTarget = WindowFromPoint(pt);
    if (!is_droptarget(hwndNewTarget))
        hwndNewTarget = 0;
    returnValue = dropSource->QueryContinueDrag(escPressed, dwKeyState);

    if (curTargetHWND != hwndNewTarget && (returnValue == S_OK || returnValue == DRAGDROP_S_DROP))
    {
        if (curTargetHWND)
        {
            drag_leave(curTargetHWND);
            curTargetHWND = 0;
        }

        if (hwndNewTarget)
        {
            curTargetHWND = hwndNewTarget;
            drag_enter(hwndNewTarget);
        }
    }

    if (returnValue == S_OK)
    {
        if (curTargetHWND)
        {
            drag_over(curTargetHWND, curMousePos.x, curMousePos.y);
        }
        else {
            give_feedback();
        }
    }
    else
        drag_end();

    inTrackCall = FALSE;
    return 0;
}

LRESULT SDragDrop::OnXdndStatus(UINT uMsg, WPARAM wp, LPARAM lp) {
    if (!wp)
        *pdwEffect = DROPEFFECT_NONE;
    else
        *pdwEffect = lp;
    give_feedback();
    return 0;
}

LRESULT SDragDrop::OnXdndFinish(UINT uMsg, WPARAM wp, LPARAM lp) {
    trackingDone = TRUE;
    if (!wp)
        *pdwEffect = DROPEFFECT_NONE, returnValue = DRAGDROP_S_CANCEL;
    else
        *pdwEffect = lp, returnValue = DRAGDROP_S_DROP;
    SLOG_STMI() << "OnXdndFinish, returnValue=" << returnValue;
    return 0;
}

void SDragDrop::drag_leave(HWND target)
{
    xcb_client_message_event_t leave;
    leave.response_type = XCB_CLIENT_MESSAGE;
    leave.sequence = 0;
    leave.window = target;
    leave.format = 32;
    leave.type = conn->atoms.XdndLeave;
    leave.data.data32[0] = m_hWnd;
    xcb_send_event(conn->connection, false, target, XCB_EVENT_MASK_NO_EVENT, (const char*)&leave);
}

void SDragDrop::drag_over(HWND target,int x,int y)
{
    xcb_client_message_event_t position;
    position.response_type = XCB_CLIENT_MESSAGE;
    position.sequence = 0;
    position.window = target;
    position.format = 32;
    position.type = conn->atoms.XdndPosition;
    position.data.data32[0] = m_hWnd;
    position.data.data32[1] = dwKeyState; // reserved, we use it to transfer the key state.
    position.data.data32[2] = MAKELONG(x, y);
    position.data.data32[3] = 0;//time
    uint32_t action = 0;
    if (dwOKEffect & DROPEFFECT_MOVE)
        action = conn->atoms.XdndActionMove;
    else if (dwOKEffect & DROPEFFECT_LINK)
        action = conn->atoms.XdndActionLink;
    else if (dwOKEffect & DROPEFFECT_COPY)
        action = conn->atoms.XdndActionCopy;
    else
        action = XCB_NONE;
    position.data.data32[4] = action;//suggested action.
    xcb_send_event(conn->connection, false, target, XCB_EVENT_MASK_NO_EVENT, (const char*)&position);
}

void SDragDrop::drag_enter(HWND target)
{
    assert(dataObject);
    std::vector<xcb_atom_t> types;
    IEnumFORMATETC* fmt = NULL;
    if (dataObject->EnumFormatEtc(1, &fmt) == S_OK) {
        FORMATETC fmtEtc;
        while (fmt->Next(1, &fmtEtc, NULL) == S_OK)
        {
            types.push_back(conn->clipFormat2Atom(fmtEtc.cfFormat));
        }
        fmt->Release();
        if (types.size() > xdnd_max_type)
            types.resize(xdnd_max_type);
    }

    xcb_client_message_event_t enter;
    enter.response_type = XCB_CLIENT_MESSAGE;
    enter.sequence = 0;
    enter.window = target;
    enter.format = 32;
    enter.type = conn->atoms.XdndEnter;
    enter.data.data32[0] = m_hWnd;
    enter.data.data32[1] = xdnd_version <<24;
    if (types.size() > 3) {
        enter.data.data32[1] |= 1;
        xcb_change_property(conn->connection, XCB_PROP_MODE_REPLACE, m_hWnd,
            conn->atoms.XdndTypelist,
            XCB_ATOM_ATOM, 32, types.size(), (const void*)types.data());
    }
    else {
        enter.data.data32[2] = types.size() > 0 ? types.at(0) : 0;
        enter.data.data32[3] = types.size() > 1 ? types.at(1) : 0;
        enter.data.data32[4] = types.size() > 2 ? types.at(2) : 0;
    }
    xcb_send_event(conn->connection, false, target, XCB_EVENT_MASK_NO_EVENT, (const char*)&enter);
    drag_over(target, curMousePos.x, curMousePos.y);
}

void SDragDrop::drag_drop(HWND target, DWORD dwEffect)
{
    xcb_client_message_event_t drop;
    drop.response_type = XCB_CLIENT_MESSAGE;
    drop.sequence = 0;
    drop.window = target;
    drop.format = 32;
    drop.type = conn->atoms.XdndDrop;
    drop.data.data32[0] = m_hWnd;
    drop.data.data32[1] = 0;
    drop.data.data32[2] = XCB_TIME_CURRENT_TIME;
    drop.data.data32[3] = 0;
    uint32_t action = 0;
    if (dwOKEffect & DROPEFFECT_MOVE)
        action = conn->atoms.XdndActionMove;
    else if (dwOKEffect & DROPEFFECT_LINK)
        action = conn->atoms.XdndActionLink;
    else if (dwOKEffect & DROPEFFECT_COPY)
        action = conn->atoms.XdndActionCopy;
    else
        action = XCB_NONE;

    drop.data.data32[4] = action;
    xcb_send_event(conn->connection, false, target, XCB_EVENT_MASK_NO_EVENT, (const char*)&drop);
}

void SDragDrop::drag_end()
{
    if (this->returnValue == DRAGDROP_S_DROP) {
        drag_drop(curTargetHWND, *pdwEffect);
    }
    else {
        trackingDone = TRUE;
        *pdwEffect = DROPEFFECT_NONE;
    }
    dragEnded = TRUE;
}

HRESULT SDragDrop::give_feedback()
{
    HRESULT hr;
    LPCSTR res = NULL;
    HCURSOR cur;
    if(!curTargetHWND)
        *this->pdwEffect = DROPEFFECT_NONE;
    hr = this->dropSource->GiveFeedback(*this->pdwEffect);

    if (hr == DRAGDROP_S_USEDEFAULTCURSORS)
    {
        if (*this->pdwEffect & DROPEFFECT_MOVE)
            res = IDC_MOVE;
        else if (*this->pdwEffect & DROPEFFECT_LINK)
            res = IDC_LINK;
        else if (*this->pdwEffect & DROPEFFECT_COPY)
            res = IDC_COPY;
        else
            res = IDC_NODROP;

        cur = LoadCursor(0, res);
        SetCursor(cur);
    }

    return hr;
}

HRESULT SDragDrop::DoDragDrop(IDataObject* pDataObject, IDropSource* pDropSource, DWORD dwOKEffect, DWORD* pdwEffect)
{
    MSG             msg;
    SLOG_FMTD("(%p, %p, %08x, %p)", pDataObject, pDropSource, dwOKEffect, pdwEffect);

    if (!pDataObject || !pDropSource || !pdwEffect)
        return E_INVALIDARG;
    SConnection* conn = SConnMgr::instance()->getConnection();
    /*
     * Setup the drag n drop tracking window.
     */
    SDragDrop trackerInfo;
    trackerInfo.dataObject = pDataObject;
    trackerInfo.dropSource = pDropSource;
    trackerInfo.dwOKEffect = dwOKEffect;
    trackerInfo.pdwEffect = pdwEffect;
    trackerInfo.trackingDone = FALSE;
    trackerInfo.inTrackCall = FALSE;
    trackerInfo.escPressed = FALSE;
    trackerInfo.dragEnded = FALSE;
    trackerInfo.curTargetHWND = 0;
    trackerInfo.conn = conn;
    if (!trackerInfo.CreateWindowA(0, CLS_WINDOW, "TrackerWindow", WS_POPUP, 0, 0, 1, 1, 0, 0, 0))
        return E_OUTOFMEMORY;

    {
        conn->getClipboard()->setSelDataObject(pDataObject);
        /*
         * Capture the mouse input
         */
        HWND oldCapture = trackerInfo.SetCapture();
        SLOG_STMI() << "DoDragDrop start";

        msg.message = 0;

        /*
         * Pump messages. All mouse input should go to the capture window.
         */
        while (!trackerInfo.trackingDone && GetMessage(&msg, 0, 0, 0))
        {
            trackerInfo.curMousePos.x = msg.pt.x;
            trackerInfo.curMousePos.y = msg.pt.y;
            trackerInfo.dwKeyState = OLEDD_GetButtonState();

            if (!trackerInfo.dragEnded &&((msg.message >= WM_KEYFIRST) &&(msg.message <= WM_KEYLAST)
                || (msg.message >= WM_MOUSEFIRST) && (msg.message <= WM_MOUSELAST)))
            {
                /*
                 * When keyboard messages are sent to windows on this thread, we
                 * want to ignore notify the drop source that the state changed.
                 * in the case of the Escape key, we also notify the drop source
                 * we give it a special meaning.
                 */
                if ((msg.message == WM_KEYDOWN) &&
                    (msg.wParam == VK_ESCAPE))
                {
                    trackerInfo.escPressed = TRUE;
                }

                /*
                 * Notify the drop source.
                 */
                trackerInfo.TrackStateChange(msg.message,msg.wParam,msg.lParam);
            }
            else
            {
                /*
                 * Dispatch the messages only when it's not a keyboard message.
                 */
                DispatchMessage(&msg);
            }
        }
        conn->getClipboard()->setSelDataObject(NULL);
        ::SetCapture(oldCapture);
        /* re-post the quit message to outer message loop */
        if (msg.message == WM_QUIT)
            PostQuitMessage(msg.wParam);
        /*
         * Destroy the temporary window.
         */
        SLOG_STMI() << "DoDragDrop Quit, ret=" << trackerInfo.returnValue;
        trackerInfo.DestroyWindow();
        return trackerInfo.returnValue;
    }

    return E_FAIL;
}


//-------------------------------------------------------
SDataObjectProxy::SDataObjectProxy(SConnection* conn, HWND hWnd) :m_conn(conn), m_hSource(hWnd) {
    m_targetTime = XCB_CURRENT_TIME;
    m_dwEffect = 0;
    m_dwKeyState = 0;
}

HRESULT  SDataObjectProxy::GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) {
    if (QueryGetData(pformatetcIn) != S_OK) {
        return DV_E_FORMATETC;
    }
    std::shared_ptr<std::vector<char>> buf = m_conn->readXdndSelection(pformatetcIn->cfFormat);
    if (!buf)
        return DV_E_DVASPECT;
    size_t bufLen = buf->size();
    pmedium->hGlobal = GlobalAlloc(0, bufLen);
    if (!pmedium->hGlobal)
        return STG_E_MEDIUMFULL;
    void* dst = GlobalLock(pmedium->hGlobal);
    memcpy(dst, buf->data(), bufLen);
    GlobalUnlock(pmedium->hGlobal);
    return S_OK;
}