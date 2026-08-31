#ifndef _SWINX_MOBILE_SDRAGDROP_H_
#define _SWINX_MOBILE_SDRAGDROP_H_

#include <windows.h>
#include <ole2.h>

class SDragDrop {
  public:
    static HRESULT DoDragDrop(IDataObject *, IDropSource *, DWORD, DWORD *)
    {
        return E_NOTIMPL;
    }
};

#endif // _SWINX_MOBILE_SDRAGDROP_H_
