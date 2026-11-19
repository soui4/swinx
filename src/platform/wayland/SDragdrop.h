#ifndef _SWINX_WAYLAND_SDRAGDROP_H_
#define _SWINX_WAYLAND_SDRAGDROP_H_

#include <windows.h>
#include <ole2.h>

// 骨架阶段只提供编译所需的声明；实现阶段改为 wl_data_device 拖放。
class SDragDrop {
public:
    static HRESULT DoDragDrop(IDataObject *, IDropSource *, DWORD, DWORD *)
    {
        return E_NOTIMPL;
    }
};

#endif // _SWINX_WAYLAND_SDRAGDROP_H_
