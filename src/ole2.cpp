#include <windows.h>
#include <ole2.h>
#include "SConnection.h"
#include "log.h"
#define kLogTag "ole2"

HRESULT CoInitialize(
    LPVOID pvReserved
) {
    return CoInitializeEx(pvReserved, 0);
}

HRESULT CoInitializeEx(
    void* pvReserved,
    DWORD dwCoInit
) {
    return S_OK;
}

void CoUninitialize(void) {

}


//--------------------------------------------------------
/******************************************************************************
 * These are static/global variables and internal data structures that the
 * OLE module uses to maintain its state.
 */
typedef struct tagTrackerWindowInfo
{
    IDataObject* dataObject;
    IDropSource* dropSource;
    DWORD        dwOKEffect;
    DWORD* pdwEffect;
    BOOL       trackingDone;
    BOOL         inTrackCall;
    HRESULT      returnValue;

    BOOL       escPressed;
    HWND       curTargetHWND;	/* window the mouse is hovering over */
    IDropTarget* curDragTarget;
    POINTL     curMousePos;       /* current position of the mouse in screen coordinates */
    DWORD      dwKeyState;        /* current state of the shift and ctrl keys and the mouse buttons */
} TrackerWindowInfo;


static const char OLEDD_DRAGTRACKERCLASS[] = "SWinXDragDropTracker32";
static LRESULT WINAPI OLEDD_DragTrackerWindowProc(
    HWND   hwnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM   lParam);
static void OLEDD_TrackStateChange(TrackerWindowInfo* trackerInfo);

/***
 * OLEDD_Initialize()
 *
 * Initializes the OLE drag and drop data structures.
 */
static void OLEDD_Initialize(void)
{
    WNDCLASSA wndClass;

    ZeroMemory(&wndClass, sizeof(WNDCLASSA));
    wndClass.style = CS_GLOBALCLASS;
    wndClass.lpfnWndProc = OLEDD_DragTrackerWindowProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = sizeof(TrackerWindowInfo*);
    wndClass.hCursor = 0;
    wndClass.hbrBackground = 0;
    wndClass.lpszClassName = OLEDD_DRAGTRACKERCLASS;

    RegisterClassA(&wndClass);
}

/***
 * OLEDD_DragTrackerWindowProc()
 *
 * This method is the WindowProcedure of the drag n drop tracking
 * window. During a drag n Drop operation, an invisible window is created
 * to receive the user input and act upon it. This procedure is in charge
 * of this behavior.
 */

#define DRAG_TIMER_ID 1

static LRESULT WINAPI OLEDD_DragTrackerWindowProc(
    HWND   hwnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM   lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        LPCREATESTRUCT createStruct = (LPCREATESTRUCT)lParam;
        SetWindowLongPtrA(hwnd, GWLP_OPAQUE, (LONG_PTR)createStruct->lpCreateParams);
        SetTimer(hwnd, DRAG_TIMER_ID, 50, NULL);

        break;
    }
    case WM_TIMER:
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        TrackerWindowInfo* trackerInfo = (TrackerWindowInfo*)GetWindowLongPtrA(hwnd, 0);
        if (trackerInfo->trackingDone) break;
        OLEDD_TrackStateChange(trackerInfo);
        break;
    }
    case WM_DESTROY:
    {
        KillTimer(hwnd, DRAG_TIMER_ID);
        break;
    }
    }

    /*
     * This is a window proc after all. Let's call the default.
     */
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void drag_enter(TrackerWindowInfo* info, HWND new_target)
{
    HRESULT hr;

    info->curTargetHWND = new_target;

    //while (new_target && !is_droptarget(new_target))
    //    new_target = GetParent(new_target);

    //info->curDragTarget = get_droptarget_pointer(new_target);

    if (info->curDragTarget)
    {
        *info->pdwEffect = info->dwOKEffect;
        hr = info->curDragTarget->DragEnter(info->dataObject,
            info->dwKeyState, info->curMousePos,
            info->pdwEffect);
        *info->pdwEffect &= info->dwOKEffect;

        /* failed DragEnter() means invalid target */
        if (hr != S_OK)
        {
            info->curDragTarget->Release();
            info->curDragTarget = 0;
            info->curTargetHWND = 0;
        }
    }
}

static void drag_end(TrackerWindowInfo* info)
{
    HRESULT hr;

    info->trackingDone = TRUE;
    ReleaseCapture();

    if (info->curDragTarget)
    {
        if (info->returnValue == DRAGDROP_S_DROP &&
            *info->pdwEffect != DROPEFFECT_NONE)
        {
            *info->pdwEffect = info->dwOKEffect;
            hr = info->curDragTarget->Drop(info->dataObject, info->dwKeyState,
                info->curMousePos, info->pdwEffect);
            *info->pdwEffect &= info->dwOKEffect;

            if (FAILED(hr))
                info->returnValue = hr;
        }
        else
        {
            info->curDragTarget->DragLeave();
            *info->pdwEffect = DROPEFFECT_NONE;
        }
        info->curDragTarget->Release();
        info->curDragTarget = NULL;
    }
    else
        *info->pdwEffect = DROPEFFECT_NONE;
}

static HRESULT give_feedback(TrackerWindowInfo* info)
{
    HRESULT hr;
    LPCSTR res = NULL;
    HCURSOR cur;

    if (info->curDragTarget == NULL)
        *info->pdwEffect = DROPEFFECT_NONE;

    hr = info->dropSource->GiveFeedback(*info->pdwEffect);

    if (hr == DRAGDROP_S_USEDEFAULTCURSORS)
    {
        if (*info->pdwEffect & DROPEFFECT_MOVE)
            res = IDC_MOVE;
        else if (*info->pdwEffect & DROPEFFECT_COPY)
            res = IDC_COPY;
        else if (*info->pdwEffect & DROPEFFECT_LINK)
            res = IDC_LINK;
        else
            res = IDC_NODROP;

        cur = LoadCursor(0, res);
        SetCursor(cur);
    }

    return hr;
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
static void OLEDD_TrackStateChange(TrackerWindowInfo* trackerInfo)
{
    HWND   hwndNewTarget = 0;
    POINT pt;

    /*
     * This method may be called from QueryContinueDrag again,
     * (i.e. by running message loop) so avoid recursive call chain.
     */
    if (trackerInfo->inTrackCall) return;
    trackerInfo->inTrackCall = TRUE;

    /*
     * Get the handle of the window under the mouse
     */
    pt.x = trackerInfo->curMousePos.x;
    pt.y = trackerInfo->curMousePos.y;
    hwndNewTarget = WindowFromPoint(pt);

    trackerInfo->returnValue = trackerInfo->dropSource->QueryContinueDrag(
        trackerInfo->escPressed,
        trackerInfo->dwKeyState);

    if (trackerInfo->curTargetHWND != hwndNewTarget &&
        (trackerInfo->returnValue == S_OK ||
            trackerInfo->returnValue == DRAGDROP_S_DROP))
    {
        if (trackerInfo->curDragTarget)
        {
            trackerInfo->curDragTarget->DragLeave();
            trackerInfo->curDragTarget->Release();
            trackerInfo->curDragTarget = 0;
            trackerInfo->curTargetHWND = 0;
        }

        if (hwndNewTarget)
            drag_enter(trackerInfo, hwndNewTarget);

        give_feedback(trackerInfo);

    }

    if (trackerInfo->returnValue == S_OK)
    {
        if (trackerInfo->curDragTarget)
        {
            *trackerInfo->pdwEffect = trackerInfo->dwOKEffect;
            trackerInfo->curDragTarget->DragOver(
                trackerInfo->dwKeyState,
                trackerInfo->curMousePos,
                trackerInfo->pdwEffect);
            *trackerInfo->pdwEffect &= trackerInfo->dwOKEffect;
        }
        give_feedback(trackerInfo);
    }
    else
        drag_end(trackerInfo);

    trackerInfo->inTrackCall = FALSE;
}

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


HRESULT WINAPI RevokeDragDrop( HWND hwnd){
    //todo:hjx
    return E_NOTIMPL;
}

HRESULT WINAPI RegisterDragDrop(HWND, LPDROPTARGET)
{
    return E_NOTIMPL;
}
/***********************************************************************
 * DoDragDrop [OLE32.@]
 */
HRESULT WINAPI DoDragDrop(
    IDataObject* pDataObject,  /* [in] ptr to the data obj           */
    IDropSource* pDropSource,  /* [in] ptr to the source obj         */
    DWORD       dwOKEffect,    /* [in] effects allowed by the source */
    DWORD* pdwEffect)    /* [out] ptr to effects of the source */
{
    TrackerWindowInfo trackerInfo;
    HWND            hwndTrackWindow;
    MSG             msg;

    SLOG_FMTD("(%p, %p, %08x, %p)", pDataObject, pDropSource, dwOKEffect, pdwEffect);

    if (!pDataObject || !pDropSource || !pdwEffect)
        return E_INVALIDARG;

    /*
     * Setup the drag n drop tracking window.
     */

    trackerInfo.dataObject = pDataObject;
    trackerInfo.dropSource = pDropSource;
    trackerInfo.dwOKEffect = dwOKEffect;
    trackerInfo.pdwEffect = pdwEffect;
    trackerInfo.trackingDone = FALSE;
    trackerInfo.inTrackCall = FALSE;
    trackerInfo.escPressed = FALSE;
    trackerInfo.curTargetHWND = 0;
    trackerInfo.curDragTarget = 0;

    hwndTrackWindow = CreateWindowA(OLEDD_DRAGTRACKERCLASS, "TrackerWindow",
        WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, 0,
        &trackerInfo);

    if (hwndTrackWindow)
    {
        /*
         * Capture the mouse input
         */
        SetCapture(hwndTrackWindow);

        msg.message = 0;

        /*
         * Pump messages. All mouse input should go to the capture window.
         */
        while (!trackerInfo.trackingDone && GetMessage(&msg, 0, 0, 0))
        {
            trackerInfo.curMousePos.x = msg.pt.x;
            trackerInfo.curMousePos.y = msg.pt.y;
            trackerInfo.dwKeyState = OLEDD_GetButtonState();

            if ((msg.message >= WM_KEYFIRST) &&
                (msg.message <= WM_KEYLAST))
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
                OLEDD_TrackStateChange(&trackerInfo);
            }
            else
            {
                /*
                 * Dispatch the messages only when it's not a keyboard message.
                 */
                DispatchMessage(&msg);
            }
        }

        /* re-post the quit message to outer message loop */
        if (msg.message == WM_QUIT)
            PostQuitMessage(msg.wParam);
        /*
         * Destroy the temporary window.
         */
        DestroyWindow(hwndTrackWindow);

        return trackerInfo.returnValue;
    }

    return E_FAIL;
}

HRESULT WINAPI OleSetClipboard(IDataObject* pdo) {
    if (!OpenClipboard(0))
        return E_UNEXPECTED;
    EmptyClipboard();
    IEnumFORMATETC * enum_fmt;
    HRESULT hr = pdo->EnumFormatEtc(DATADIR_GET, &enum_fmt);
    if (FAILED(hr))
        return hr;
    FORMATETC fmt;
    while (enum_fmt->Next(1, &fmt, NULL) == S_OK) {
        STGMEDIUM storage;
        hr = pdo->GetData(&fmt, &storage);
        if (hr != S_OK)
            break;
        if (storage.tymed == TYMED_HGLOBAL) {
            SetClipboardData(fmt.cfFormat, storage.hGlobal);
        }
    }
    enum_fmt->Release();
    CloseClipboard();
    return hr;
}

HRESULT WINAPI OleFlushClipboard(void) {
    return E_NOTIMPL;
}

HRESULT WINAPI OleIsCurrentClipboard(IDataObject* pdo) {
    SConnection* pconn = SConnMgr::instance()->getConnection();
    if (pdo == pconn->getClipboard())
        return S_OK;
    return E_UNEXPECTED;
}

HRESULT WINAPI OleGetClipboard(IDataObject** ppdo) {
    SConnection* pconn = SConnMgr::instance()->getConnection();
    return pconn->getClipboard()->QueryInterface(IID_IDataObject, (void**)ppdo);
}

void WINAPI ReleaseStgMedium(STGMEDIUM* pmedium)
{
    if (!pmedium) return;

    switch (pmedium->tymed)
    {
    case TYMED_HGLOBAL:
    {
        if ((pmedium->pUnkForRelease == 0) && (pmedium->hGlobal != 0))
            GlobalFree(pmedium->hGlobal);
        break;
    }
    case TYMED_FILE:
    {
        if (pmedium->lpszFileName != 0)
        {
            if (pmedium->pUnkForRelease == 0)
            {
                DeleteFileW(pmedium->lpszFileName);
            }
            CoTaskMemFree(pmedium->lpszFileName);
        }
        break;
    }
    case TYMED_ISTREAM:
    {
        if (pmedium->pstm != 0)
        {
            pmedium->pstm->Release();
        }
        break;
    }
    case TYMED_ISTORAGE:
    {
        if (pmedium->pstg != 0)
        {
            pmedium->pstg->Release();
        }
        break;
    }
    case TYMED_GDI:
    {
        if ((pmedium->pUnkForRelease == 0) &&
            (pmedium->hBitmap != 0))
            DeleteObject(pmedium->hBitmap);
        break;
    }
    case TYMED_NULL:
    default:
        break;
    }
    pmedium->tymed = TYMED_NULL;

    /*
     * After cleaning up, the unknown is released
     */
    if (pmedium->pUnkForRelease != 0)
    {
        pmedium->pUnkForRelease->Release();
        pmedium->pUnkForRelease = 0;
    }
}
