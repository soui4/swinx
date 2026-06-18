#include <windows.h>
#include <ole2.h>
#include "SConnection.h"
#include <string.h>

#ifndef CF_PRIVATEFIRST
#define CF_PRIVATEFIRST 0x0200
#endif

HRESULT CoInitialize(LPVOID pvReserved)
{
    return CoInitializeEx(pvReserved, 0);
}

HRESULT CoInitializeEx(void *, DWORD)
{
    return S_OK;
}

void CoUninitialize(void)
{
}

HRESULT WINAPI RevokeDragDrop(HWND)
{
    return S_OK;
}

HRESULT WINAPI RegisterDragDrop(HWND, LPDROPTARGET dropTarget)
{
    if (!dropTarget)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI DoDragDrop(IDataObject *, IDropSource *, DWORD, DWORD *pdwEffect)
{
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    return E_NOTIMPL;
}

HRESULT WINAPI OleSetClipboard(IDataObject *pdo)
{
    if (!OpenClipboard(0))
        return CLIPBRD_E_CANT_OPEN;
    SConnection *pconn = SConnMgr::instance()->getConnection();
    BOOL ret = pconn->getClipboard()->setDataObject(pdo);
    CloseClipboard();
    return ret ? S_OK : CLIPBRD_E_CANT_SET;
}

HRESULT WINAPI OleFlushClipboard(void)
{
    SConnection *pconn = SConnMgr::instance()->getConnection();
    pconn->getClipboard()->flushClipboard();
    return S_OK;
}

HRESULT WINAPI OleIsCurrentClipboard(IDataObject *pdo)
{
    SConnection *pconn = SConnMgr::instance()->getConnection();
    return pconn->getClipboard()->isCurrentClipboard(pdo) ? S_OK : S_FALSE;
}

HRESULT WINAPI OleGetClipboard(IDataObject **ppdo)
{
    if (!ppdo)
        return E_POINTER;
    SConnection *pconn = SConnMgr::instance()->getConnection();
    *ppdo = pconn->getClipboard()->getDataObject();
    return *ppdo ? S_OK : S_FALSE;
}

void WINAPI ReleaseStgMedium(STGMEDIUM *pmedium)
{
    if (!pmedium)
        return;

    switch (pmedium->tymed)
    {
    case TYMED_HGLOBAL:
        if (!pmedium->pUnkForRelease && pmedium->hGlobal)
            GlobalFree(pmedium->hGlobal);
        break;
    case TYMED_FILE:
        if (pmedium->lpszFileName)
        {
            if (!pmedium->pUnkForRelease)
                DeleteFileW(pmedium->lpszFileName);
            CoTaskMemFree(pmedium->lpszFileName);
        }
        break;
    case TYMED_ISTREAM:
        if (pmedium->pstm)
            pmedium->pstm->Release();
        break;
    case TYMED_ISTORAGE:
        if (pmedium->pstg)
            pmedium->pstg->Release();
        break;
    default:
        break;
    }

    if (pmedium->pUnkForRelease)
        pmedium->pUnkForRelease->Release();

    pmedium->tymed = TYMED_NULL;
    pmedium->pUnkForRelease = 0;
}

static HANDLE duplicateGlobalData(HANDLE hSrc, UINT uiFlags)
{
    HGLOBAL hSrcGlobal = (HGLOBAL)hSrc;
    SIZE_T szSrc = GlobalSize(hSrcGlobal);
    if (szSrc == 0)
        return nullptr;

    HGLOBAL ret = GlobalAlloc(uiFlags, szSrc);
    if (!ret)
        return nullptr;

    const void *psrc = GlobalLock(hSrcGlobal);
    void *pdst = GlobalLock(ret);
    if (!psrc || !pdst)
    {
        if (pdst)
            GlobalUnlock(ret);
        if (psrc)
            GlobalUnlock(hSrcGlobal);
        GlobalFree(ret);
        return nullptr;
    }

    memcpy(pdst, psrc, szSrc);
    GlobalUnlock(ret);
    GlobalUnlock(hSrcGlobal);
    return ret;
}

static bool isHGlobalClipboardFormat(CLIPFORMAT cfFormat)
{
    switch (cfFormat)
    {
    case CF_TEXT:
    case CF_OEMTEXT:
    case CF_UNICODETEXT:
    case CF_HDROP:
    case CF_LOCALE:
    case CF_DIB:
    case CF_DIBV5:
    case CF_SYLK:
    case CF_DIF:
    case CF_TIFF:
    case CF_PENDATA:
    case CF_RIFF:
    case CF_WAVE:
        return true;
    default:
        return cfFormat >= CF_PRIVATEFIRST;
    }
}

HANDLE WINAPI OleDuplicateData(HANDLE hSrc, CLIPFORMAT cfFormat, UINT uiFlags)
{
    if (!hSrc)
        return nullptr;
    if (cfFormat == CF_BITMAP)
        return RefGdiObj(hSrc);
    if (isHGlobalClipboardFormat(cfFormat))
        return duplicateGlobalData(hSrc, uiFlags);
    return nullptr;
}
