#ifndef __SDRAGSOURCEHELPER__H__
#define __SDRAGSOURCEHELPER__H__

#include <shlobj.h>
#include <SUnkImpl.h>

class SDragSourceHelper : public SUnkImpl<IDragSourceHelper>
{
public:
    SDragSourceHelper();
    virtual ~SDragSourceHelper();

    static HRESULT GetDragImage(IDataObject *pDataObject, SHDRAGIMAGE *pshdi);

public:
    // IDragSourceHelper
    virtual HRESULT STDMETHODCALLTYPE InitializeFromBitmap(LPSHDRAGIMAGE pshdi, IDataObject *pDataObject);
    virtual HRESULT STDMETHODCALLTYPE InitializeFromWindow(HWND hwnd, POINT *ppt, IDataObject *pDataObject);

public:
	IUNKNOWN_BEGIN(IDragSourceHelper)
	IUNKNOWN_END()
};

#endif // __SDRAGSOURCEHELPER__H__