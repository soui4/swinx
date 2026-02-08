#ifndef _UIMSG_H_
#define _UIMSG_H_
#include <mutex>
#include <string>
#include <uuid/uuid.h>
#include <hook.h>
#include "handle.h"
#include "sharedmem.h"

enum _MsgType
{
    MT_POST = 0,
    MT_SEND,
    MT_CALLBACK,
};

#pragma pack(push, 1)
struct MsgLayout
{
    uint64_t ret;
    uint64_t wp;
    uint64_t lp; // lp or COPYDATASTRUCT.dwData
};
#pragma pack(pop)

struct MsgReply
{
    std::mutex mutex;
    int nRef;
    int type;
    MsgReply()
        : nRef(1)
        , type(MT_POST)
    {
    }
    virtual ~MsgReply()
    {
    }

    virtual void AddRef()
    {
        std::unique_lock<std::mutex> lock(mutex);
        nRef++;
    }
    virtual void Release()
    {
        mutex.lock();
        BOOL bFree = --nRef == 0;
        mutex.unlock();
        if (bFree)
        {
            delete this;
        }
    }

    virtual void SetResult(LRESULT res)
    {
    }

    int GetType() const
    {
        return type;
    }
};

struct CbTask
{
    std::mutex mutex;
    int nRef;

    HANDLE synEvent;
    HWND hwnd;
    UINT uMsg;
    ULONG_PTR dwData;
    SENDASYNCPROC asyncProc;

    CbTask(HANDLE evt)
        : nRef(1)
    {
        synEvent = AddHandleRef(evt);
    }
    virtual ~CbTask()
    {
        CloseHandle(synEvent);
    }

    virtual void AddRef()
    {
        std::unique_lock<std::mutex> lock(mutex);
        nRef++;
    }
    virtual void Release()
    {
        mutex.lock();
        BOOL bFree = --nRef == 0;
        mutex.unlock();
        if (bFree)
        {
            delete this;
        }
    }
    virtual BOOL TestEvent() = 0;
};

struct CallbackTask : CbTask
{

    LRESULT lResult;

    CallbackTask(HANDLE evt)
        : CbTask(evt)
    {
    }

    ~CallbackTask()
    {
    }

    BOOL TestEvent()
    {
        if (WaitForSingleObject(synEvent, 0) != WAIT_OBJECT_0)
            return FALSE;
        asyncProc(hwnd, uMsg, dwData, lResult);
        return TRUE;
    }
};

struct CallbackTaskIpc : CbTask
{
    swinx::SharedMemory *pSharedMem;
    CallbackTaskIpc(HANDLE evt, swinx::SharedMemory *shareMem)
        : CbTask(evt)
        , pSharedMem(shareMem)
    {
    }

    ~CallbackTaskIpc()
    {
        delete pSharedMem;
    }

    BOOL TestEvent()
    {
        if (WaitForSingleObject(synEvent, 0) != WAIT_OBJECT_0)
            return FALSE;
        MsgLayout *msgLayout = (MsgLayout *)pSharedMem->buffer();
        asyncProc(hwnd, uMsg, dwData, msgLayout->ret);
        return TRUE;
    }
};

struct CallbackReply : MsgReply
{
    HANDLE synEvent;
    LRESULT *lResult;
    bool hasResult;

    CallbackReply(HANDLE evt, LRESULT *pret)
        : hasResult(false)
    {
        type = MT_CALLBACK;
        synEvent = AddHandleRef(evt);
        lResult = pret;
    }

    ~CallbackReply()
    {
        if (!hasResult)
        {
            SetEvent(synEvent);
        }
        CloseHandle(synEvent);
    }

    void SetResult(LRESULT res) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        hasResult = true;
        if (lResult)
            *lResult = res;
        SetEvent(synEvent);
    }
};

struct SendReply : MsgReply
{
    HANDLE synEvent;
    LRESULT ret;
    bool hasResult;
    SendReply(HANDLE hEvt, LRESULT ret_ = -1)
        : synEvent(AddHandleRef(hEvt))
        , ret(ret_)
        , hasResult(false)
    {
        type = MT_SEND;
    }

    ~SendReply()
    {
        if (!hasResult)
            SetEvent(synEvent);
        CloseHandle(synEvent);
    }

    void SetResult(LRESULT res) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ret = res;
        hasResult = true;
        SetEvent(synEvent);
    }
};


struct Msg : MSG
{
    MsgReply *msgReply;
    Msg(MsgReply *reply = nullptr)
    {
        msgReply = reply;//move ower to this, not add ref
        time = GetTickCount();
    }
    virtual ~Msg()
    {
        if (msgReply)
        {
            msgReply->Release();
        }
    }
    virtual void SetResult(LRESULT res)
    {
        if (msgReply)
        {
            msgReply->SetResult(res);
        }
    }
};

struct MsgW2A : Msg{
    MsgW2A(MsgReply *reply = nullptr): Msg(reply){
        message = WM_MSG_W2A;
        wParam = 0;
        lParam = (LPARAM)&orgMsg;
    }

    MSG orgMsg;
};

struct MsgPaint : Msg
{
    HRGN rgn;
    MsgPaint(HRGN src = nullptr)
        : rgn(src)
    {
    }
    ~MsgPaint();
};

struct MsgWndPosChanged : Msg
{
    WINDOWPOS pos;
};

typedef unsigned char   suid_t[8];

struct IpcMsg : Msg
{
    static void gen_suid(suid_t * uid);
    static void suid2string(const suid_t id, char *buf);
    static std::string get_share_mem_name(const suid_t id);
    static std::string get_ipc_event_name(const suid_t id);

    swinx::SharedMemory *shareMem;
    HANDLE synEvt;
    COPYDATASTRUCT cds;
    bool hasResult;
    IpcMsg(HWND hWnd, const uint32_t data[5]);
    ~IpcMsg();
    void SetResult(LRESULT res) override;
};

struct CallHookData
{
    HOOKPROC proc;
    UINT code;
    WPARAM wp;
    LPARAM lp;
};

enum {
    UM_STATE = (WM_INTERNAL + 1),
    UM_SETTINGS,
    UM_MAPNOTIFY,
    UM_CALLHOOK,
    UM_XDND_DRAG_ENTER=0x400,
    UM_XDND_DRAG_OVER,
    UM_XDND_DRAG_LEAVE,
    UM_XDND_DRAG_DROP,
    UM_XDND_FINISH,
    UM_XDND_STATUS,
};
enum
{
    kMapped = 1,
};

enum{
    SETTINGS_DPI=1,
};

#ifdef __linux__
struct DragBase {
    HWND hFrom;
};
class XDndDataObjectProxy;
struct DragEnterData : DragBase {
    POINTL pt;
    XDndDataObjectProxy *pData;
    DragEnterData(XDndDataObjectProxy *_pData);
    ~DragEnterData();
};

struct DragEnterMsg : Msg , DragEnterData {
    DragEnterMsg(XDndDataObjectProxy *_pData): DragEnterData(_pData){
        lParam = (LPARAM)(DragEnterData*)this;
    }

    ~DragEnterMsg() {
    }
};

struct DragDropData : DragBase{};

struct DragDropMsg : Msg, DragDropData{
    DragDropMsg(){
        lParam = (LPARAM)(DragDropData*)this;
    }
};

struct DragOverData : DragBase {
    POINTL pt;
    DWORD dwKeyState;
    uint32_t supported_actions;
};

struct DragOverMsg : Msg, DragOverData {
    DragOverMsg() {
        lParam = (LPARAM)(DragOverData*)this;
    }
};

#endif//__linux__

#endif //_UIMSG_H_