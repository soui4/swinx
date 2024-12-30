#include <windows.h>
#include <ole2.h>
#include "SConnection.h"
#include "SDragdrop.h"
#include "SClipboard.h"
#include "wndobj.h"
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


HRESULT WINAPI RevokeDragDrop( HWND hwnd){
    WndObj wndObj = WndMgr::fromHwnd(hwnd);
    if (!wndObj)
        return DRAGDROP_E_INVALIDHWND;
    if (!wndObj->dropTarget)
        return DRAGDROP_E_NOTREGISTERED;
    wndObj->dropTarget->Release();
    wndObj->dropTarget = NULL;
    wndObj->mConnection->EnableDragDrop(hwnd,FALSE);
    return S_OK;
}

HRESULT WINAPI RegisterDragDrop(HWND hwnd, LPDROPTARGET dropTarget)
{
    if (!dropTarget)
        return E_INVALIDARG;
    WndObj wndObj = WndMgr::fromHwnd(hwnd);
    if (!wndObj)
        return DRAGDROP_E_INVALIDHWND;
    if (wndObj->dropTarget)
        return DRAGDROP_E_ALREADYREGISTERED;
    wndObj->dropTarget = dropTarget;
    dropTarget->AddRef();
    wndObj->mConnection->EnableDragDrop(hwnd, TRUE);
    return S_OK;
}

HRESULT WINAPI DoDragDrop(
    IDataObject* pDataObject,  /* [in] ptr to the data obj           */
    IDropSource* pDropSource,  /* [in] ptr to the source obj         */
    DWORD       dwOKEffect,    /* [in] effects allowed by the source */
    DWORD* pdwEffect)    /* [out] ptr to effects of the source */
{
    return SDragDrop::DoDragDrop(pDataObject, pDropSource, dwOKEffect, pdwEffect);
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
    if (pdo == pconn->getClipboard()->getDataObject(FALSE))
        return S_OK;
    return E_UNEXPECTED;
}

HRESULT WINAPI OleGetClipboard(IDataObject** ppdo) {
    SConnection* pconn = SConnMgr::instance()->getConnection();
    *ppdo = pconn->getClipboard()->getDataObject(FALSE);
    (*ppdo)->AddRef();
    return S_OK;
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
