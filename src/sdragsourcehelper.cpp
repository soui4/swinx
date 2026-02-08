#include "sdragsourcehelper.h"

#define CF_DRAGSOURCE_INFO    65534
#define CF_DRAGSOURCE_IMAGE   65535

typedef struct DRAGSOURCE_INFO
{
    SIZE sizeDragImage;
    POINT ptOffset;
    COLORREF crColorKey;
} DRAGSOURCE_INFO;

//------------------------------------------------------------------
SDragSourceHelper::SDragSourceHelper()
{
}

SDragSourceHelper::~SDragSourceHelper()
{
}

HRESULT SDragSourceHelper::GetDragImage(IDataObject *pDataObject, SHDRAGIMAGE *pshdi)
{
    FORMATETC fmt = {CF_DRAGSOURCE_INFO, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {TYMED_HGLOBAL};
    HRESULT hr = pDataObject->GetData(&fmt, &medium);
    if(hr != S_OK)
        return hr;
    DRAGSOURCE_INFO *pInfo = (DRAGSOURCE_INFO *)GlobalLock(medium.hGlobal);
    pshdi->sizeDragImage = pInfo->sizeDragImage;
    pshdi->ptOffset = pInfo->ptOffset;
    pshdi->crColorKey = pInfo->crColorKey;
    GlobalUnlock(medium.hGlobal);
    FORMATETC fmt2 = {CF_DRAGSOURCE_IMAGE, NULL, DVASPECT_CONTENT, -1, TYMED_GDI};
    STGMEDIUM medium2 = {TYMED_GDI};
    hr = pDataObject->GetData(&fmt2, &medium2);
    if(hr != S_OK)
        return hr;
    pshdi->hbmpDragImage = medium2.hBitmap;  
    return S_OK;
}

HRESULT SDragSourceHelper::InitializeFromBitmap(LPSHDRAGIMAGE pshdi, IDataObject *pDataObject)
{
    if (!pshdi || !pDataObject || !pshdi->hbmpDragImage)
        return E_INVALIDARG;
    BITMAP bm;
    GetObject(pshdi->hbmpDragImage, sizeof(bm), &bm);
    if (bm.bmBitsPixel != 32)
        return E_INVALIDARG;
    HANDLE hData = GlobalAlloc(GMEM_MOVEABLE, sizeof(DRAGSOURCE_INFO));
    DRAGSOURCE_INFO *pInfo = (DRAGSOURCE_INFO *)GlobalLock(hData);
    pInfo->sizeDragImage = pshdi->sizeDragImage;
    pInfo->ptOffset = pshdi->ptOffset;
    pInfo->crColorKey = pshdi->crColorKey;
    GlobalUnlock(hData);
    FORMATETC fmt = {CF_DRAGSOURCE_INFO, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {TYMED_HGLOBAL};
    medium.hGlobal = hData;
    HRESULT hr = pDataObject->SetData(&fmt,&medium,TRUE);
    if(hr!=S_OK)
    {
        GlobalFree(hData);
        return hr;
    }
    FORMATETC fmt2 = {CF_DRAGSOURCE_IMAGE, NULL, DVASPECT_CONTENT, -1, TYMED_GDI};
    STGMEDIUM medium2 = {TYMED_GDI};
    medium2.hBitmap = RefGdiObj(pshdi->hbmpDragImage);

    hr = pDataObject->SetData(&fmt2,&medium2,TRUE);
    if(hr!=S_OK)
    {
        DeleteObject(medium2.hBitmap);
        return hr;
    }
    return S_OK;
}

HRESULT SDragSourceHelper::InitializeFromWindow(HWND hwnd, POINT *ppt, IDataObject *pDataObject)
{
    return E_NOTIMPL;
}