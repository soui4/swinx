#ifndef _OHOS_SDRAGDROP_H_
#define _OHOS_SDRAGDROP_H_

#include <windows.h>
#include <ole2.h>

class SDragDrop {
  public:
    static HRESULT DoDragDrop(IDataObject *, IDropSource *, DWORD, DWORD *)
    {
        return E_NOTIMPL;
    }
};

#endif // _OHOS_SDRAGDROP_H_
