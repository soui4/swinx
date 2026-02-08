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
    STDMETHOD(InitializeFromBitmap)(LPSHDRAGIMAGE pshdi, IDataObject *pDataObject) override;
    STDMETHOD(InitializeFromWindow)(HWND hwnd, POINT *ppt, IDataObject *pDataObject) override;

public:
	IUNKNOWN_BEGIN(IDragSourceHelper)
	IUNKNOWN_END()
};

#endif // __SDRAGSOURCEHELPER__H__