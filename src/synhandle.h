
#ifndef _SYN_HANDLE_H_
#define _SYN_HANDLE_H_
#include <mutex>

enum
{
    HUnknown = 0,
    HEvent = 1,
    HNamedEvent,
    HSemaphore,
    HNamedSemaphore,
    HMutex,
    HNamedMutex,
    HFileMap,
    HFdHandle,
};

struct _SynHandle
{
    int type;
    _SynHandle()
        : type(HUnknown)
    {
    }
    virtual ~_SynHandle()
    {
    }

    virtual int getType() const
    {
        return type;
    }
    virtual bool init(LPCSTR pszName, void *initData) = 0;
    virtual int getReadFd() = 0;
    virtual int getWriteFd() = 0;
    virtual bool onWaitDone()
    {
        return true;
    }
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual void *getData() = 0;

    bool readSignal(bool bLock = true)
    {
        if (bLock)
            lock();
        fd_set readfds;
        int fd = getReadFd();
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        select(fd + 1, &readfds, NULL, NULL, &timeout);
        bool bSignal = FD_ISSET(fd, &readfds);
        if (bSignal)
        {
            char buffer[2] = { 0 };
            ssize_t len = read(fd, buffer, 1);
        }
        if (bLock)
            unlock();
        return bSignal;
    }
    bool writeSignal()
    {
        int fd = getWriteFd();
        return write(fd, "a", 1) == 1;
    }
};

static void FreeSynObj(void *ptr)
{
    _SynHandle *sysHandle = (_SynHandle *)ptr;
    delete sysHandle;
}

static HANDLE NewSynHandle(_SynHandle *synObj)
{
    return new _Handle(SYN_OBJ, synObj, FreeSynObj);
}

static _SynHandle *GetSynHandle(HANDLE h)
{
    if (h->type != SYN_OBJ)
        return nullptr;
    return (_SynHandle *)h->ptr;
}
#endif//_SYN_HANDLE_H_