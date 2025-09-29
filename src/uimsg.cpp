#include <windows.h>
#include <atomic>
#include "uimsg.h"
#ifdef __linux__
#include "SDragdrop.h"
#endif //__linux__
#include "log.h"
#define kLogTag "uimsg"

MsgPaint::~MsgPaint()
{
    if (rgn)
    {
        DeleteObject(rgn);
    }
}
void IpcMsg::gen_suid(suid_t *uid)
{
    static std::atomic<uint32_t> s_uid_index(0);
    uint32_t *p = (uint32_t *)uid;
    *p++ = getpid();
    *p++ = s_uid_index++;
}

void IpcMsg::suid2string(const suid_t id, char *buf)
{
    for (int i = 0; i < sizeof(suid_t); i++)
    {
        sprintf(buf, "%02x", id[i]);
        buf += 2;
    }
    *buf = 0;
}

std::string IpcMsg::get_share_mem_name(const suid_t id)
{
    char buf[25];
    suid2string(id, buf);
    std::string mem_name = "/smsg_";
    mem_name += buf;
    return mem_name;
}

std::string IpcMsg::get_ipc_event_name(const suid_t id)
{
    char buf[25];
    suid2string(id, buf);
    std::string event_name = "/sevt_";
    event_name += buf;
    return event_name;
}

IpcMsg::IpcMsg(HWND hWnd, const uint32_t data[5])
    : shareMem(nullptr)
    , synEvt(INVALID_HANDLE_VALUE)
    , hasResult(false)
{
    hwnd = hWnd;
    message = data[0];
    suid_t id;
    memcpy(id, data + 1, sizeof(id));
    std::string strEvt = get_ipc_event_name(id);
    // SLOG_STMI()<<"handle ipcmsg,event name="<<strEvt.c_str();
    synEvt = CreateEventA(nullptr, FALSE, FALSE, strEvt.c_str());

    std::string strMem = get_share_mem_name(id);
    // SLOG_STMI()<<"handle ipcmsg,share memory name="<<strMem.c_str();
    shareMem = new swinx::SharedMemory;
    shareMem->init(strMem.c_str(), 0); // open exist share mem.
    MsgLayout *msgLayout = (MsgLayout *)shareMem->buffer();
    if (message == WM_COPYDATA)
    {
        wParam = msgLayout->wp;
        cds.dwData = msgLayout->lp;
        LPBYTE extra = (LPBYTE)(msgLayout + 1);
        cds.cbData = *(DWORD *)extra;
        cds.lpData = extra + sizeof(DWORD);
        lParam = (LPARAM)&cds;
        // LOG_STMI("msg")<<"recv copydata msg, dwData="<<cds.dwData<<" cbData="<<cds.cbData;
    }
    else
    {
        wParam = msgLayout->wp;
        lParam = msgLayout->lp;
        // LOG_STMI("msg")<<"recv ipc msg="<<message<<" wp="<<wParam<<" lp="<<lParam;
    }
}

IpcMsg::~IpcMsg()
{
    // LOG_STMI("msg")<<"ipc msg release";
    if (!hasResult)
    {
        SetEvent(synEvt);
    }
    CloseHandle(synEvt);
    delete shareMem;
}

void IpcMsg::SetResult(LRESULT res)
{
    // LOG_STMI("msg")<<"ipc msg SetResult "<<res;
    hasResult = true;
    MsgLayout *msgLayout = (MsgLayout *)shareMem->buffer();
    msgLayout->ret = res;
    SetEvent(synEvt);
}
//-----------------------------------------------------------
#ifdef __linux__
DragEnterData::DragEnterData(XDndDataObjectProxy *_pData)
{
    pData = _pData;
    if (pData)
        pData->AddRef();
}
DragEnterData::~DragEnterData()
{
    if (pData)
    {
        pData->Release();
        pData = NULL;
    }
}
#endif //__linux__