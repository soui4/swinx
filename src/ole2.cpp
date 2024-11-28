#include <windows.h>
#include <ole2.h>
#include "SConnection.h"

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

HRESULT RevokeDragDrop(
  HWND hwnd
){
    //todo:hjx
    return E_NOTIMPL;
}

HRESULT DoDragDrop(
  LPDATAOBJECT pDataObj,
  LPDROPSOURCE pDropSource,
  DWORD        dwOKEffects,
  LPDWORD      pdwEffect
){
    return E_NOTIMPL;
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
