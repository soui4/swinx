#include <windows.h>
#include "uimsg.h"
#include "SDragdrop.h"

void IpcMsg::uuid2string(const uuid_t id, char *buf)
{
    for (int i = 0; i < 16; i++)
    {
        sprintf(buf, "%02x", id[i]);
        buf += 2;
    }
    *buf = 0;
}

std::string IpcMsg::get_share_mem_name(const uuid_t id)
{
    char buf[33];
    uuid2string(id, buf);
    std::string mem_name = "/soui-sendmsg-sharememory-";
    mem_name += buf;
    return mem_name;
}

std::string IpcMsg::get_ipc_event_name(const uuid_t id)
{
    char buf[33];
    uuid2string(id, buf);
    std::string event_name = "/soui-sendmsg-event-";
    event_name += buf;
    return event_name;
}


DragEnterData::DragEnterData(XDndDataObjectProxy *_pData){
    pData=_pData;
    if(pData)
        pData->AddRef();
}
DragEnterData::~DragEnterData(){
    if(pData){
        pData->Release();
        pData=NULL;
    }
}