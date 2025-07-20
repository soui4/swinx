#include "sdragsourcehelper.h"
#include <map>
#include <memory>


class SDragImage : public SHDRAGIMAGE
{ 
    public:
        SDragImage(LPSHDRAGIMAGE pshdi)
        {
            memcpy(this, pshdi, sizeof(SHDRAGIMAGE));
            hbmpDragImage = (HBITMAP)RefGdiObj(pshdi->hbmpDragImage);
        }

        ~SDragImage()
        {
            DeleteObject(hbmpDragImage);
        }
};

static std::map<IDataObject*, std::shared_ptr<SHDRAGIMAGE>> s_dragSourceImage;

//------------------------------------------------------------------
SDragSourceHelper::SDragSourceHelper()
{
}


SDragSourceHelper::~SDragSourceHelper()
{
}

HRESULT SDragSourceHelper::GetDragImage(IDataObject *pDataObject, SHDRAGIMAGE *pshdi)
{
    auto it = s_dragSourceImage.find(pDataObject);
    if(it != s_dragSourceImage.end())
    {
        memcpy(pshdi, it->second.get(), sizeof(SHDRAGIMAGE));
        pshdi->hbmpDragImage = (HBITMAP)RefGdiObj(pshdi->hbmpDragImage);
        s_dragSourceImage.erase(it);
        return S_OK;
    }   
    return E_FAIL;
}

HRESULT SDragSourceHelper::InitializeFromBitmap(LPSHDRAGIMAGE pshdi, IDataObject *pDataObject)
{
    if(!pshdi || !pDataObject || !pshdi->hbmpDragImage)
        return E_INVALIDARG;
    BITMAP bm;
    GetObject(pshdi->hbmpDragImage, sizeof(bm), &bm);
    if(bm.bmBitsPixel != 32)
        return E_INVALIDARG;
    s_dragSourceImage[pDataObject] = std::make_shared<SDragImage>(pshdi);
    return S_OK;    
}

HRESULT SDragSourceHelper::InitializeFromWindow(HWND hwnd, POINT *ppt, IDataObject *pDataObject)
{
    return E_NOTIMPL;
}