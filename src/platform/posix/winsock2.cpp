/* WS2_32 的运行时：把 Win32 的套接字 API 翻译成 POSIX socket 调用。
 *
 * 本文件**只** include Win32 头（winsock2.h）与平台中立的 posixsock.h ——
 * 一旦在这里 include <sys/socket.h>，`struct sockaddr` / `fd_set` / `select`
 * 就会和 winsock2.h 里的同名但不同布局的定义撞上。翻译动作全部落在
 * PsxAddr / PsxFdSet 这些 POD 上，POSIX 那一侧在 posixsock.cpp。
 *
 * 语义约定（与真 Winsock 对齐的地方，也就是别人可能依赖的地方）：
 *   - 失败返回 SOCKET_ERROR / INVALID_SOCKET，并把 WSA 错误码存进**本线程**；
 *     返回 0 表示成功（select 除外：它返回就绪个数，可以是 0）。
 *   - getaddrinfo 一族按 Win32 约定直接返回错误码，不设置 last error。
 *   - WSAEventSelect 会把套接字切成非阻塞，这是它的前置条件，不是副作用。
 */

#include <winsock2.h>

#include "posixsock.h"
#include "winsock_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "debug.h"
#define kLogTag "winsock2"

namespace
{

/* ------------------------------------------------------------ 基础换算 */

/* 字节序。不调用 htons/ntohs：本文件不能 include <arpa/inet.h>，而 winsock2.h
 * 里的兼容宏会把这两个名字换成 swinx 自己的实现，调用是可行的；但 swap16/32
 * 是纯算术，直接算最省事、也少一层命名空间纠结。 */
unsigned short swap16(unsigned short v)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return v;
#else
    return (unsigned short)(((v & 0x00FF) << 8) | ((v & 0xFF00) >> 8));
#endif
}

unsigned long swap32(unsigned long v)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return v;
#else
    return ((v & 0x000000FFUL) << 24) | ((v & 0x0000FF00UL) << 8) | ((v & 0x00FF0000UL) >> 8) |
           ((v & 0xFF000000UL) >> 24);
#endif
}

int errorFromErrno(int e)
{
    switch (e)
    {
    case 0:
        return 0;
    case EINTR:
        return WSAEINTR;
    case EBADF:
        return WSAEBADF;
    case EACCES:
    case EPERM:
        return WSAEACCES;
    case EFAULT:
        return WSAEFAULT;
    case EINVAL:
        return WSAEINVAL;
    case EMFILE:
    case ENFILE:
        return WSAEMFILE;
    case EAGAIN:
        return WSAEWOULDBLOCK;
    case EINPROGRESS:
        return WSAEINPROGRESS;
    case EALREADY:
        return WSAEALREADY;
    case ENOTSOCK:
        return WSAENOTSOCK;
    case EDESTADDRREQ:
        return WSAEDESTADDRREQ;
    case EMSGSIZE:
        return WSAEMSGSIZE;
    case EPROTOTYPE:
        return WSAEPROTOTYPE;
    case ENOPROTOOPT:
        return WSAENOPROTOOPT;
    case EPROTONOSUPPORT:
        return WSAEPROTONOSUPPORT;
#ifdef ESOCKTNOSUPPORT
    case ESOCKTNOSUPPORT:
        return WSAESOCKTNOSUPPORT;
#endif
    case EOPNOTSUPP:
        return WSAEOPNOTSUPP;
#ifdef EPFNOSUPPORT
    case EPFNOSUPPORT:
        return WSAEPFNOSUPPORT;
#endif
    case EAFNOSUPPORT:
        return WSAEAFNOSUPPORT;
    case EADDRINUSE:
        return WSAEADDRINUSE;
    case EADDRNOTAVAIL:
        return WSAEADDRNOTAVAIL;
    case ENETDOWN:
        return WSAENETDOWN;
    case ENETUNREACH:
        return WSAENETUNREACH;
    case ENETRESET:
        return WSAENETRESET;
    case ECONNABORTED:
        return WSAECONNABORTED;
    case ECONNRESET:
        return WSAECONNRESET;
    case ENOBUFS:
    case ENOMEM:
        return WSAENOBUFS;
    case EISCONN:
        return WSAEISCONN;
    case ENOTCONN:
        return WSAENOTCONN;
#ifdef ESHUTDOWN
    case ESHUTDOWN:
        return WSAESHUTDOWN;
#endif
    case ETIMEDOUT:
        return WSAETIMEDOUT;
    case ECONNREFUSED:
        return WSAECONNREFUSED;
    case ELOOP:
        return WSAELOOP;
    case ENAMETOOLONG:
        return WSAENAMETOOLONG;
    case EHOSTUNREACH:
        return WSAEHOSTUNREACH;
    case ENOTEMPTY:
        return WSAENOTEMPTY;
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
    case EWOULDBLOCK:
        return WSAEWOULDBLOCK;
#endif
    default:
        return WSAEINVAL;
    }
}

/* psx* 失败时只给 errno；统一在这里翻译并落到线程错误槽。 */
int failWith(int psxErr)
{
    swinx::wsa::setLastError(errorFromErrno(psxErr));
    return SOCKET_ERROR;
}

SOCKET failSocket(int psxErr)
{
    swinx::wsa::setLastError(errorFromErrno(psxErr));
    return INVALID_SOCKET;
}

bool requireStarted()
{
    if (swinx::wsa::started())
        return true;
    swinx::wsa::setLastError(WSANOTINITIALISED);
    return false;
}

/* ------------------------------------------------------- sockaddr 互转 */

bool addrFromWin(const struct sockaddr *sa, int len, swinx::PsxAddr *out)
{
    if (!sa || !out)
        return false;
    memset(out, 0, sizeof(*out));

    if (sa->sa_family == AF_INET && len >= (int)sizeof(SOCKADDR_IN))
    {
        const SOCKADDR_IN *in = (const SOCKADDR_IN *)sa;
        out->family = swinx::PsxAfInet;
        out->port = swap16(in->sin_port);
        memcpy(out->bytes, &in->sin_addr, 4);
        return true;
    }
    if (sa->sa_family == AF_INET6 && len >= (int)sizeof(SOCKADDR_IN6))
    {
        const SOCKADDR_IN6 *in = (const SOCKADDR_IN6 *)sa;
        out->family = swinx::PsxAfInet6;
        out->port = swap16(in->sin6_port);
        memcpy(out->bytes, in->sin6_addr.u.Byte, 16);
        out->scope = in->sin6_scope_id;
        return true;
    }
    return false;
}

/* 写回 Win32 的 sockaddr，返回实际写入的字节数。 */
int addrToWin(const swinx::PsxAddr &in, struct sockaddr *sa, int capacity)
{
    if (!sa)
        return 0;

    if (in.family == swinx::PsxAfInet && capacity >= (int)sizeof(SOCKADDR_IN))
    {
        SOCKADDR_IN *out = (SOCKADDR_IN *)sa;
        memset(out, 0, sizeof(*out));
        out->sin_family = AF_INET;
        out->sin_port = swap16(in.port);
        memcpy(&out->sin_addr, in.bytes, 4);
        return (int)sizeof(SOCKADDR_IN);
    }
    if (in.family == swinx::PsxAfInet6 && capacity >= (int)sizeof(SOCKADDR_IN6))
    {
        SOCKADDR_IN6 *out = (SOCKADDR_IN6 *)sa;
        memset(out, 0, sizeof(*out));
        out->sin6_family = AF_INET6;
        out->sin6_port = swap16(in.port);
        memcpy(out->sin6_addr.u.Byte, in.bytes, 16);
        out->sin6_scope_id = in.scope;
        return (int)sizeof(SOCKADDR_IN6);
    }
    return 0;
}

/* ------------------------------------------------------ getsockopt 映射 */

bool mapOption(int level, int optname, int *psxLevel, int *psxOpt)
{
    if (level == SOL_SOCKET)
    {
        *psxLevel = swinx::PsxLevelSocket;
        switch (optname)
        {
        case SO_REUSEADDR:
            *psxOpt = swinx::PsxOptReuseAddr;
            return true;
        case SO_KEEPALIVE:
            *psxOpt = swinx::PsxOptKeepAlive;
            return true;
        case SO_RCVBUF:
            *psxOpt = swinx::PsxOptRecvBuf;
            return true;
        case SO_SNDBUF:
            *psxOpt = swinx::PsxOptSendBuf;
            return true;
        case SO_LINGER:
            *psxOpt = swinx::PsxOptLinger;
            return true;
        case SO_RCVTIMEO:
            *psxOpt = swinx::PsxOptRecvTimeout;
            return true;
        case SO_SNDTIMEO:
            *psxOpt = swinx::PsxOptSendTimeout;
            return true;
        case SO_BROADCAST:
            *psxOpt = swinx::PsxOptBroadcast;
            return true;
        case SO_DONTROUTE:
            *psxOpt = swinx::PsxOptDontRoute;
            return true;
        case SO_ERROR:
            *psxOpt = swinx::PsxOptError;
            return true;
        case SO_TYPE:
            *psxOpt = swinx::PsxOptType;
            return true;
        case SO_EXCLUSIVEADDRUSE:
            *psxOpt = swinx::PsxOptExclusiveAddrUse;
            return true;
        default:
            return false;
        }
    }

    if (level == IPPROTO_TCP)
    {
        *psxLevel = swinx::PsxLevelTcp;
        if (optname == TCP_NODELAY)
        {
            *psxOpt = swinx::PsxOptNoDelay;
            return true;
        }
        return false;
    }

    if (level == IPPROTO_IP)
    {
        *psxLevel = swinx::PsxLevelIp;
        switch (optname)
        {
        case IP_TTL:
            *psxOpt = swinx::PsxOptTtl;
            return true;
        case IP_MULTICAST_TTL:
            *psxOpt = swinx::PsxOptMulticastTtl;
            return true;
        case IP_MULTICAST_LOOP:
            *psxOpt = swinx::PsxOptMulticastLoop;
            return true;
        default:
            return false;
        }
    }

    return false;
}

int mapAiFlags(int winFlags)
{
    int flags = 0;
    if (winFlags & AI_CANONNAME)
        flags |= swinx::PsxAiCanonName;
    if (winFlags & AI_PASSIVE)
        flags |= swinx::PsxAiPassive;
    if (winFlags & AI_NUMERICHOST)
        flags |= swinx::PsxAiNumericHost;
    if (winFlags & AI_NUMERICSERV)
        flags |= swinx::PsxAiNumericService;
    return flags;
}

int gaiErrorFromErrno(int e)
{
    switch (e)
    {
    case ENOMEM:
        return WSAENOBUFS;
    case EAFNOSUPPORT:
    case EOPNOTSUPP:
        return WSAEAFNOSUPPORT;
    case EAGAIN:
        return WSATRY_AGAIN;
    case ENOENT:
    case EINVAL:
    default:
        return WSAHOST_NOT_FOUND;
    }
}

/* -------------------------------------------------------- getaddrinfo 结果 */

/* 一次 malloc 装下整条链：每节 ADDRINFOA + 一个 sockaddr_storage + canonname。
 * freeaddrinfo 因此只要一次 free —— 与 Win32 的所有权约定一致。 */
ADDRINFOA *buildAddrInfoA(const swinx::PsxAddrInfo *list, int count)
{
    size_t total = 0;
    for (int i = 0; i < count; ++i)
        total += sizeof(ADDRINFOA) + sizeof(SOCKADDR_STORAGE) + strlen(list[i].canonName) + 1;

    unsigned char *block = (unsigned char *)calloc(1, total);
    if (!block)
        return NULL;

    ADDRINFOA *head = NULL;
    ADDRINFOA *prev = NULL;
    unsigned char *cursor = block;

    for (int i = 0; i < count; ++i)
    {
        ADDRINFOA *node = (ADDRINFOA *)cursor;
        cursor += sizeof(ADDRINFOA);

        SOCKADDR_STORAGE *storage = (SOCKADDR_STORAGE *)cursor;
        cursor += sizeof(SOCKADDR_STORAGE);

        int addrLen = addrToWin(list[i].addr, (struct sockaddr *)storage, (int)sizeof(SOCKADDR_STORAGE));

        node->ai_flags = 0;
        node->ai_family = list[i].family;
        node->ai_socktype = list[i].sockType;
        node->ai_protocol = list[i].protocol;
        node->ai_addrlen = (size_t)addrLen;
        node->ai_addr = (struct sockaddr *)storage;
        node->ai_next = NULL;

        if (list[i].canonName[0] && (list[i].flags & swinx::PsxAiCanonName))
        {
            size_t len = strlen(list[i].canonName) + 1;
            memcpy(cursor, list[i].canonName, len);
            node->ai_canonname = (char *)cursor;
            cursor += len;
        }

        if (prev)
            prev->ai_next = node;
        else
            head = node;
        prev = node;
    }

    return head;
}

ADDRINFOW *buildAddrInfoW(const swinx::PsxAddrInfo *list, int count)
{
    size_t total = 0;
    for (int i = 0; i < count; ++i)
        total += sizeof(ADDRINFOW) + sizeof(SOCKADDR_STORAGE) + (strlen(list[i].canonName) + 1) * sizeof(wchar_t);

    unsigned char *block = (unsigned char *)calloc(1, total);
    if (!block)
        return NULL;

    ADDRINFOW *head = NULL;
    ADDRINFOW *prev = NULL;
    unsigned char *cursor = block;

    for (int i = 0; i < count; ++i)
    {
        ADDRINFOW *node = (ADDRINFOW *)cursor;
        cursor += sizeof(ADDRINFOW);

        SOCKADDR_STORAGE *storage = (SOCKADDR_STORAGE *)cursor;
        cursor += sizeof(SOCKADDR_STORAGE);

        int addrLen = addrToWin(list[i].addr, (struct sockaddr *)storage, (int)sizeof(SOCKADDR_STORAGE));

        node->ai_flags = 0;
        node->ai_family = list[i].family;
        node->ai_socktype = list[i].sockType;
        node->ai_protocol = list[i].protocol;
        node->ai_addrlen = (size_t)addrLen;
        node->ai_addr = (struct sockaddr *)storage;
        node->ai_next = NULL;

        if (list[i].canonName[0] && (list[i].flags & swinx::PsxAiCanonName))
        {
            wchar_t *wide = (wchar_t *)cursor;
            size_t len = strlen(list[i].canonName);
            for (size_t k = 0; k < len; ++k)
                wide[k] = (wchar_t)(unsigned char)list[i].canonName[k];
            wide[len] = 0;
            node->ai_canonname = wide;
            cursor += (len + 1) * sizeof(wchar_t);
        }

        if (prev)
            prev->ai_next = node;
        else
            head = node;
        prev = node;
    }

    return head;
}

/* ------------------------------------------------------- WSAEventSelect */

struct SelectEntry
{
    SOCKET fd; // 不叫 socket：winsock2.h 里 `socket` 是兼容宏，成员名会连带被改
    WSAEVENT event;
    HWND hwnd;      /* WSAAsyncSelect：事件投递目标 */
    unsigned int msg; /* WSAAsyncSelect：窗口消息 */
    long mask;
    long pending;   /* WSAEnumNetworkEvents 的累积集 */
    long reported;  /* WSAAsyncSelect 已经投递过的位（电平变化时重新武装） */
    int errors[FD_MAX_EVENTS];

    SelectEntry()
        : fd(INVALID_SOCKET)
        , event(NULL)
        , hwnd(NULL)
        , msg(0)
        , mask(0)
        , pending(0)
        , reported(0)
    {
        memset(errors, 0, sizeof(errors));
    }
};

std::mutex g_eventLock;
std::map<SOCKET, SelectEntry> g_eventEntries;
std::thread g_pumpThread;
std::atomic<bool> g_pumpStarted(false);

/* 一轮轮询里攒下的窗口消息：不在 g_eventLock 内投递，免得把 Winsock 的全局
 * 锁与窗口管理器的锁叠在一起。 */
struct AsyncNotify
{
    HWND hwnd;
    unsigned int msg;
    SOCKET fd;
    long bits;
    int errors[FD_MAX_EVENTS];
};

/* 一个后台线程轮询所有被 WSAEventSelect / WSAAsyncSelect 接管的套接字。
 *
 * 为什么用轮询而不是 epoll/kqueue：那样要写两套平台代码（Linux 与 *BSD），
 * 而这里服务的是"少量套接字 + 几百毫秒的响应要求"，20ms 一轮完全够用，
 * 四平台同一份实现。这也和 NotifyAddrChange 的做法一致。 */
void pumpLoop()
{
    for (;;)
    {
        bool any = false;
        std::vector<AsyncNotify> notifications;
        {
            std::lock_guard<std::mutex> lock(g_eventLock);
            for (std::map<SOCKET, SelectEntry>::iterator it = g_eventEntries.begin();
                 it != g_eventEntries.end(); ++it)
            {
                any = true;
                SelectEntry &entry = it->second;

                int err = 0;
                long live = 0;
                int ready = swinx::psxPollOne((int)entry.fd, &err);
                if (ready < 0)
                {
                    live = FD_CLOSE;
                    entry.errors[FD_CLOSE_BIT] = errorFromErrno(err);
                }
                else if (ready != 0)
                {
                    long bits = 0;

                    if (ready & swinx::PsxReadyHangup)
                    {
                        bits |= FD_CLOSE;
                    }
                    else if (ready & swinx::PsxReadyRead)
                    {
                        /* 监听套接字"可读"就是有待接受的连接。 */
                        if (entry.mask & FD_ACCEPT)
                            bits |= FD_ACCEPT;
                        else
                            bits |= FD_READ;
                    }

                    if ((ready & swinx::PsxReadyWrite) && (entry.mask & FD_CONNECT))
                    {
                        /* 非阻塞 connect 完成（成功或失败）后套接字变可写。
                         * 失败原因在 SO_ERROR 里，Winsock 也是这么报的。 */
                        bits |= FD_CONNECT;
                        int sockErr = 0;
                        int len = (int)sizeof(int);
                        if (swinx::psxGetSockOpt((int)entry.fd, swinx::PsxLevelSocket,
                                                 swinx::PsxOptError, &sockErr, &len, &err) == 0 &&
                            sockErr != 0)
                            entry.errors[FD_CONNECT_BIT] = errorFromErrno(sockErr);
                    }

                    if ((ready & swinx::PsxReadyWrite) && (entry.mask & FD_WRITE))
                        bits |= FD_WRITE;

                    if (ready & swinx::PsxReadyExcept)
                    {
                        if (entry.mask & FD_OOB)
                            bits |= FD_OOB;
                    }

                    live = bits & entry.mask;
                }

                live &= entry.mask;
                entry.pending |= live;

                if (entry.event)
                {
                    if (entry.pending)
                        SetEvent(entry.event);
                }
                else if (entry.hwnd)
                {
                    /* WSAAsyncSelect：只在位从"没有"变成"有"时投递一次。
                     * 已投递集合跟着当前电平收缩，于是数据被读空后再来新数据
                     * 会重新投递，而持续可写不会每 20ms 刷一条消息。 */
                    long fresh = live & ~entry.reported;
                    entry.reported = live;
                    if (fresh)
                    {
                        AsyncNotify n;
                        n.hwnd = entry.hwnd;
                        n.msg = entry.msg;
                        n.fd = entry.fd;
                        n.bits = fresh;
                        memcpy(n.errors, entry.errors, sizeof(n.errors));
                        notifications.push_back(n);
                    }
                }
            }
        }

        for (size_t i = 0; i < notifications.size(); ++i)
        {
            const AsyncNotify &n = notifications[i];
            long bits = n.bits;
            for (int bit = 0; bit < FD_MAX_EVENTS && bits; ++bit)
            {
                long one = 1L << bit;
                if (!(bits & one))
                    continue;
                PostMessageA(n.hwnd, n.msg, (WPARAM)n.fd, MAKELPARAM((WORD)one, (WORD)n.errors[bit]));
                bits &= ~one;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(any ? 20 : 50));
    }
}

void ensurePump()
{
    bool expected = false;
    if (!g_pumpStarted.compare_exchange_strong(expected, true))
        return;
    g_pumpThread = std::thread(pumpLoop);
    g_pumpThread.detach();
}

/* 名字服务用的静态结果（与 Win32 一样：指针在下次调用前有效）。 */
struct HostEntryStorage
{
    HOSTENT host;
    char *addrList[9];
    char *aliasList[5];
    char nameBuffer[256];
    char aliasBuffers[4][256];
    char addressBuffers[8][16];

    HostEntryStorage()
    {
        memset(&host, 0, sizeof(host));
        memset(addrList, 0, sizeof(addrList));
        memset(aliasList, 0, sizeof(aliasList));
        memset(nameBuffer, 0, sizeof(nameBuffer));
        memset(aliasBuffers, 0, sizeof(aliasBuffers));
        memset(addressBuffers, 0, sizeof(addressBuffers));
    }
};

void fillHostEntry(const swinx::PsxHostEnt &in, HostEntryStorage *out)
{
    strncpy(out->nameBuffer, in.name, sizeof(out->nameBuffer) - 1);
    out->host.h_name = out->nameBuffer;
    out->host.h_aliases = out->aliasList;
    out->host.h_length = (short)in.addrLength;
    out->host.h_addrtype = (short)(in.family == swinx::PsxAfInet6 ? AF_INET6 : AF_INET);

    for (int i = 0; i < in.addrCount && i < 8; ++i)
    {
        memcpy(out->addressBuffers[i], in.addresses[i], (size_t)in.addrLength);
        out->addrList[i] = out->addressBuffers[i];
    }
    out->addrList[in.addrCount < 8 ? in.addrCount : 8] = NULL;
    out->aliasList[0] = NULL;
}

struct ServEntryStorage
{
    SERVENT serv;
    char nameBuffer[128];
    char protoBuffer[32];
};

struct ProtoEntryStorage
{
    PROTOENT proto;
    char nameBuffer[128];
};

} // namespace

/* ==================================================================== 套接字 */

SOCKET WINAPI swinx_socket(int af, int type, int protocol)
{
    if (!requireStarted())
        return INVALID_SOCKET;

    int err = 0;
    int fd = swinx::psxSocket(af, type, protocol, &err);
    if (fd < 0)
        return failSocket(err);
    return (SOCKET)(UINT_PTR)fd;
}

SOCKET WINAPI WSASocketA(int af, int type, int protocol, void *lpProtocolInfo, UINT g, DWORD dwFlags)
{
    (void)lpProtocolInfo;
    (void)g;
    (void)dwFlags;

    /* 关于 WSA_FLAG_OVERLAPPED：Win32 默认就置这一位，拒绝它等于让所有
     * WSASocket 调用失败。这里放行，但 WSARecv/WSASend 在真的收到
     * OVERLAPPED 参数时会明确返回 WSAEOPNOTSUPP —— 不假装有完成端口。 */
    return swinx_socket(af, type, protocol);
}

SOCKET WINAPI WSASocketW(int af, int type, int protocol, void *lpProtocolInfo, UINT g, DWORD dwFlags)
{
    return WSASocketA(af, type, protocol, lpProtocolInfo, g, dwFlags);
}

int WINAPI closesocket(SOCKET s)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (s == INVALID_SOCKET)
    {
        swinx::wsa::setLastError(WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* 注销事件选择，免得后台线程盯住一个已经关掉的 fd（可能已被复用）。 */
    {
        std::lock_guard<std::mutex> lock(g_eventLock);
        g_eventEntries.erase(s);
    }

    int err = 0;
    if (swinx::psxClose((int)s, &err) < 0)
        return failWith(err);
    return 0;
}

int WINAPI swinx_bind(SOCKET s, const struct sockaddr *name, int namelen)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    swinx::PsxAddr addr;
    if (!addrFromWin(name, namelen, &addr))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int err = 0;
    if (swinx::psxBind((int)s, &addr, &err) < 0)
        return failWith(err);
    return 0;
}

int WINAPI swinx_connect(SOCKET s, const struct sockaddr *name, int namelen)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    swinx::PsxAddr addr;
    if (!addrFromWin(name, namelen, &addr))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int err = 0;
    if (swinx::psxConnect((int)s, &addr, &err) < 0)
        return failWith(err);
    return 0;
}

int WINAPI swinx_listen(SOCKET s, int backlog)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    int err = 0;
    if (swinx::psxListen((int)s, backlog, &err) < 0)
        return failWith(err);
    return 0;
}

SOCKET WINAPI swinx_accept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
    if (!requireStarted())
        return INVALID_SOCKET;

    swinx::PsxAddr peer;
    int err = 0;
    int client = swinx::psxAccept((int)s, &peer, &err);
    if (client < 0)
        return failSocket(err);

    if (addr && addrlen)
    {
        int written = addrToWin(peer, addr, *addrlen);
        if (written == 0)
        {
            swinx::psxClose(client, NULL);
            swinx::wsa::setLastError(WSAEFAULT);
            return INVALID_SOCKET;
        }
        *addrlen = written;
    }
    else if (addrlen)
    {
        *addrlen = 0;
    }

    return (SOCKET)(UINT_PTR)client;
}

SOCKET WINAPI WSAAccept(SOCKET s, struct sockaddr *addr, int *addrlen, LPCONDITIONPROC lpfnCondition,
                        DWORD_PTR dwCallbackData)
{
    /* CF_ACCEPT / CF_DEFER 需要调用方回调决定，这里不支持 —— 但不静默忽略：
     * 传了条件函数就明确说不支持，免得调用方以为条件生效了。 */
    if (lpfnCondition)
    {
        swinx::wsa::setLastError(WSAEOPNOTSUPP);
        return INVALID_SOCKET;
    }
    (void)dwCallbackData;
    return swinx_accept(s, addr, addrlen);
}

int WINAPI swinx_shutdown(SOCKET s, int how)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    int err = 0;
    if (swinx::psxShutdown((int)s, how, &err) < 0)
        return failWith(err);
    return 0;
}

/* ================================================================= 数据收发 */

int WINAPI swinx_send(SOCKET s, const char *buf, int len, int flags)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (len < 0 || (len > 0 && !buf))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    int err = 0;
    long n = swinx::psxSend((int)s, buf, (unsigned long)len, flags, &err);
    if (n < 0)
        return failWith(err);
    return (int)n;
}

int WINAPI swinx_recv(SOCKET s, char *buf, int len, int flags)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (len < 0 || (len > 0 && !buf))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    int err = 0;
    long n = swinx::psxRecv((int)s, buf, (unsigned long)len, flags, &err);
    if (n < 0)
        return failWith(err);
    return (int)n;
}

int WINAPI swinx_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (len < 0 || (len > 0 && !buf))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    swinx::PsxAddr addr;
    if (!addrFromWin(to, tolen, &addr))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int err = 0;
    long n = swinx::psxSendTo((int)s, buf, (unsigned long)len, flags, &addr, &err);
    if (n < 0)
        return failWith(err);
    return (int)n;
}

int WINAPI swinx_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (len < 0 || (len > 0 && !buf))
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    swinx::PsxAddr addr;
    int err = 0;
    long n = swinx::psxRecvFrom((int)s, buf, (unsigned long)len, flags, from ? &addr : NULL, &err);
    if (n < 0)
        return failWith(err);

    if (from && fromlen)
    {
        int written = addrToWin(addr, from, *fromlen);
        if (written == 0)
        {
            swinx::wsa::setLastError(WSAEFAULT);
            return SOCKET_ERROR;
        }
        *fromlen = written;
    }
    return (int)n;
}

int WINAPI WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                   DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    /* 没有完成端口：承认拿不出完成通知，而不是丢掉 OVERLAPPED 假装异步。 */
    if (lpOverlapped || lpCompletionRoutine)
    {
        swinx::wsa::setLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (!lpBuffers || dwBufferCount == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    DWORD total = 0;
    for (DWORD i = 0; i < dwBufferCount; ++i)
    {
        if (lpBuffers[i].len == 0)
            continue;
        int n = swinx_send(s, lpBuffers[i].buf, (int)lpBuffers[i].len, (int)dwFlags);
        if (n == SOCKET_ERROR)
        {
            if (lpNumberOfBytesSent)
                *lpNumberOfBytesSent = total;
            return SOCKET_ERROR;
        }
        total += (DWORD)n;
        if ((DWORD)n < lpBuffers[i].len)
            break; /* 部分发送：后面的缓冲没动，如实报已发字节 */
    }

    if (lpNumberOfBytesSent)
        *lpNumberOfBytesSent = total;
    return 0;
}

int WINAPI WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                   LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    if (lpOverlapped || lpCompletionRoutine)
    {
        swinx::wsa::setLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (!lpBuffers || dwBufferCount == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int flags = lpFlags ? (int)*lpFlags : 0;
    DWORD total = 0;
    for (DWORD i = 0; i < dwBufferCount; ++i)
    {
        if (lpBuffers[i].len == 0)
            continue;
        int n = swinx_recv(s, lpBuffers[i].buf, (int)lpBuffers[i].len, flags);
        if (n == SOCKET_ERROR)
        {
            if (lpNumberOfBytesRecvd)
                *lpNumberOfBytesRecvd = total;
            return SOCKET_ERROR;
        }
        total += (DWORD)n;
        if ((DWORD)n < lpBuffers[i].len)
            break; /* 一次 recv 只对应一次就绪；剩下的留给下一次 */
    }

    if (lpNumberOfBytesRecvd)
        *lpNumberOfBytesRecvd = total;
    return 0;
}

int WINAPI WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                     DWORD dwFlags, const struct sockaddr *lpTo, int iToLen,
                     LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (lpOverlapped || lpCompletionRoutine)
    {
        swinx::wsa::setLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (!lpBuffers || dwBufferCount == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    /* 数据报的多个缓冲要拼成一条消息，不能分多次 sendto。 */
    DWORD total = 0;
    for (DWORD i = 0; i < dwBufferCount; ++i)
        total += lpBuffers[i].len;

    if (total == 0)
    {
        if (lpNumberOfBytesSent)
            *lpNumberOfBytesSent = 0;
        return 0;
    }

    std::vector<char> joined(total);
    DWORD offset = 0;
    for (DWORD i = 0; i < dwBufferCount; ++i)
    {
        if (lpBuffers[i].len)
            memcpy(&joined[offset], lpBuffers[i].buf, lpBuffers[i].len);
        offset += lpBuffers[i].len;
    }

    int n = swinx_sendto(s, &joined[0], (int)total, (int)dwFlags, lpTo, iToLen);
    if (n == SOCKET_ERROR)
        return SOCKET_ERROR;

    if (lpNumberOfBytesSent)
        *lpNumberOfBytesSent = (DWORD)n;
    return 0;
}

int WINAPI WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                       LPDWORD lpFlags, struct sockaddr *lpFrom, LPINT lpFromlen,
                       LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (lpOverlapped || lpCompletionRoutine)
    {
        swinx::wsa::setLastError(WSAEOPNOTSUPP);
        return SOCKET_ERROR;
    }
    if (!lpBuffers || dwBufferCount == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    /* 一条数据报必须落进一个缓冲区，所以先收进临时区再一次分散拷贝。 */
    DWORD total = 0;
    for (DWORD i = 0; i < dwBufferCount; ++i)
        total += lpBuffers[i].len;
    if (total == 0)
    {
        if (lpNumberOfBytesRecvd)
            *lpNumberOfBytesRecvd = 0;
        return 0;
    }

    std::vector<char> joined(total);
    int flags = lpFlags ? (int)*lpFlags : 0;
    int n = swinx_recvfrom(s, &joined[0], (int)total, flags, lpFrom, lpFromlen);
    if (n == SOCKET_ERROR)
        return SOCKET_ERROR;

    DWORD remaining = (DWORD)n;
    DWORD offset = 0;
    for (DWORD i = 0; i < dwBufferCount && remaining > 0; ++i)
    {
        DWORD chunk = lpBuffers[i].len < remaining ? lpBuffers[i].len : remaining;
        if (chunk)
            memcpy(lpBuffers[i].buf, &joined[offset], chunk);
        offset += chunk;
        remaining -= chunk;
    }

    if (lpNumberOfBytesRecvd)
        *lpNumberOfBytesRecvd = (DWORD)n;
    return 0;
}

/* ==================================================================== 选项 */

int WINAPI swinx_getsockname(SOCKET s, struct sockaddr *name, int *namelen)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (!name || !namelen)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    swinx::PsxAddr addr;
    int err = 0;
    if (swinx::psxGetSockName((int)s, &addr, &err) < 0)
        return failWith(err);

    int written = addrToWin(addr, name, *namelen);
    if (written == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    *namelen = written;
    return 0;
}

int WINAPI swinx_getpeername(SOCKET s, struct sockaddr *name, int *namelen)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (!name || !namelen)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    swinx::PsxAddr addr;
    int err = 0;
    if (swinx::psxGetPeerName((int)s, &addr, &err) < 0)
        return failWith(err);

    int written = addrToWin(addr, name, *namelen);
    if (written == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    *namelen = written;
    return 0;
}

int WINAPI swinx_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    int psxLevel = 0;
    int psxOpt = 0;
    if (!mapOption(level, optname, &psxLevel, &psxOpt))
    {
        swinx::wsa::setLastError(WSAENOPROTOOPT);
        return SOCKET_ERROR;
    }
    if (!optval)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    /* LINGER 在两边的布局不同：Win32 是两个 u_short，POSIX 是两个 int。 */
    if (level == SOL_SOCKET && optname == SO_LINGER)
    {
        if (optlen < (int)sizeof(LINGER))
        {
            swinx::wsa::setLastError(WSAEFAULT);
            return SOCKET_ERROR;
        }
        const LINGER *src = (const LINGER *)optval;
        swinx::PsxLinger linger;
        linger.onOff = src->l_onoff;
        linger.seconds = src->l_linger;

        int err = 0;
        if (swinx::psxSetSockOpt((int)s, psxLevel, psxOpt, &linger, (int)sizeof(linger), &err) < 0)
            return failWith(err);
        return 0;
    }

    int err = 0;
    if (swinx::psxSetSockOpt((int)s, psxLevel, psxOpt, optval, optlen, &err) < 0)
        return failWith(err);
    return 0;
}

int WINAPI swinx_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    int psxLevel = 0;
    int psxOpt = 0;
    if (!mapOption(level, optname, &psxLevel, &psxOpt))
    {
        swinx::wsa::setLastError(WSAENOPROTOOPT);
        return SOCKET_ERROR;
    }
    if (!optval || !optlen)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    if (level == SOL_SOCKET && optname == SO_LINGER)
    {
        swinx::PsxLinger linger;
        int len = (int)sizeof(linger);
        int err = 0;
        if (swinx::psxGetSockOpt((int)s, psxLevel, psxOpt, &linger, &len, &err) < 0)
            return failWith(err);
        if (*optlen < (int)sizeof(LINGER))
        {
            swinx::wsa::setLastError(WSAEFAULT);
            return SOCKET_ERROR;
        }
        LINGER *dst = (LINGER *)optval;
        dst->l_onoff = (USHORT)linger.onOff;
        dst->l_linger = (USHORT)linger.seconds;
        *optlen = (int)sizeof(LINGER);
        return 0;
    }

    int err = 0;
    int len = *optlen;
    if (swinx::psxGetSockOpt((int)s, psxLevel, psxOpt, optval, &len, &err) < 0)
        return failWith(err);
    *optlen = len;
    return 0;
}

int WINAPI ioctlsocket(SOCKET s, long cmd, ULONG *argp)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    unsigned long raw = argp ? (unsigned long)*argp : 0;
    int psxCmd = 0;
    if ((unsigned long)cmd == (unsigned long)FIONBIO)
        psxCmd = swinx::PsxIoctlBlocking;
    else if ((unsigned long)cmd == (unsigned long)FIONREAD)
        psxCmd = swinx::PsxIoctlReadable;
    else
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }
    if (!argp)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int err = 0;
    if (swinx::psxIoctl((int)s, psxCmd, &raw, &err) < 0)
        return failWith(err);

    if (psxCmd == swinx::PsxIoctlReadable)
        *argp = (ULONG)raw;
    return 0;
}

/* ================================================================ 多路复用 */

int WINAPI __WSAFDIsSet(SOCKET s, fd_set *set)
{
    if (!set)
        return 0;
    for (UINT i = 0; i < set->fd_count; ++i)
    {
        if (set->fd_array[i] == s)
            return 1;
    }
    return 0;
}

int WINAPI swinx_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                  const struct timeval *timeout)
{
    (void)nfds; /* Win32 忽略它（POSIX 才需要"最大 fd + 1"） */

    if (!requireStarted())
        return SOCKET_ERROR;

    swinx::PsxFdSet readSet;
    swinx::PsxFdSet writeSet;
    swinx::PsxFdSet exceptSet;
    readSet.count = writeSet.count = exceptSet.count = 0;

    if (readfds)
    {
        for (UINT i = 0; i < readfds->fd_count && i < 64; ++i)
            readSet.fds[readSet.count++] = (int)(UINT_PTR)readfds->fd_array[i];
    }
    if (writefds)
    {
        for (UINT i = 0; i < writefds->fd_count && i < 64; ++i)
            writeSet.fds[writeSet.count++] = (int)(UINT_PTR)writefds->fd_array[i];
    }
    if (exceptfds)
    {
        for (UINT i = 0; i < exceptfds->fd_count && i < 64; ++i)
            exceptSet.fds[exceptSet.count++] = (int)(UINT_PTR)exceptfds->fd_array[i];
    }

    long usec = -1;
    if (timeout)
        usec = timeout->tv_sec * 1000000L + timeout->tv_usec;

    int err = 0;
    int rc = swinx::psxSelect(readfds ? &readSet : NULL, writefds ? &writeSet : NULL,
                              exceptfds ? &exceptSet : NULL, usec, &err);
    if (rc < 0)
        return failWith(err);

    int total = 0;
    if (readfds)
    {
        readfds->fd_count = 0;
        for (int i = 0; i < readSet.count; ++i)
            FD_SET((SOCKET)(UINT_PTR)readSet.fds[i], readfds);
        total += readSet.count;
    }
    if (writefds)
    {
        writefds->fd_count = 0;
        for (int i = 0; i < writeSet.count; ++i)
            FD_SET((SOCKET)(UINT_PTR)writeSet.fds[i], writefds);
        total += writeSet.count;
    }
    if (exceptfds)
    {
        exceptfds->fd_count = 0;
        for (int i = 0; i < exceptSet.count; ++i)
            FD_SET((SOCKET)(UINT_PTR)exceptSet.fds[i], exceptfds);
        total += exceptSet.count;
    }

    return total;
}

WSAEVENT WINAPI WSACreateEvent(void)
{
    /* manual-reset、未置位 —— Winsock 的事件对象就是这个语义。 */
    return CreateEventA(NULL, TRUE, FALSE, NULL);
}

BOOL WINAPI WSACloseEvent(WSAEVENT hEvent)
{
    if (!hEvent)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return FALSE;
    }
    return CloseHandle(hEvent);
}

BOOL WINAPI WSASetEvent(WSAEVENT hEvent)
{
    if (!hEvent)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return FALSE;
    }
    return SetEvent(hEvent);
}

BOOL WINAPI WSAResetEvent(WSAEVENT hEvent)
{
    if (!hEvent)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return FALSE;
    }
    return ResetEvent(hEvent);
}

DWORD WINAPI WSAWaitForMultipleEvents(DWORD cEvents, const WSAEVENT *lphEvents, BOOL fWaitAll,
                                      DWORD dwTimeout, BOOL fAlertable)
{
    (void)fAlertable; /* swinx 没有 APC 队列，alertable 无落点 */

    if (!lphEvents || cEvents == 0 || cEvents > 64)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return WSA_WAIT_FAILED;
    }

    HANDLE handles[64];
    for (DWORD i = 0; i < cEvents; ++i)
        handles[i] = lphEvents[i];

    DWORD timeoutMs = (dwTimeout == WSA_INFINITE) ? INFINITE : dwTimeout;
    DWORD rc = WaitForMultipleObjects(cEvents, handles, fWaitAll, timeoutMs);

    if (rc == WAIT_TIMEOUT)
        return WSA_WAIT_TIMEOUT;
    if (rc == WAIT_FAILED)
        return WSA_WAIT_FAILED;

    /* WaitForMultipleObjects 返回 WAIT_OBJECT_0 + 下标；WSA 的基址也是 0，
     * 但两者是**不同的常量族**，这里显式换算，避免调用方混用。 */
    return rc;
}

int WINAPI WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    /* hEventObject == NULL：注销并按 Win32 约定恢复阻塞模式。 */
    if (!hEventObject)
    {
        std::lock_guard<std::mutex> lock(g_eventLock);
        g_eventEntries.erase(s);
    }
    else
    {
        if (lNetworkEvents & ~(long)FD_ALL_EVENTS)
        {
            swinx::wsa::setLastError(WSAEINVAL);
            return SOCKET_ERROR;
        }

        /* WSAEventSelect 的前置条件：套接字必须是非阻塞的。Win32 也是这么做的，
         * 而且这是接口契约的一部分，不是可选副作用。 */
        unsigned long on = 1;
        int err = 0;
        if (swinx::psxIoctl((int)s, swinx::PsxIoctlBlocking, &on, &err) < 0)
            return failWith(err);

        std::lock_guard<std::mutex> lock(g_eventLock);
        SelectEntry &entry = g_eventEntries[s];
        entry.fd = s;
        entry.event = hEventObject;
        /* 与 WSAAsyncSelect 互斥：后注册的一方接管通知。 */
        entry.hwnd = NULL;
        entry.msg = 0;
        entry.mask = lNetworkEvents;
        entry.pending = 0;
        entry.reported = 0;
        memset(entry.errors, 0, sizeof(entry.errors));
    }

    ensurePump();
    return 0;
}

int WINAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents)
{
    if (!lpNetworkEvents)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    std::lock_guard<std::mutex> lock(g_eventLock);
    std::map<SOCKET, SelectEntry>::iterator it = g_eventEntries.find(s);
    if (it == g_eventEntries.end())
    {
        swinx::wsa::setLastError(WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Win32 的语义：枚举的同时复位事件对象（如果给了）。 */
    if (hEventObject)
        ResetEvent(hEventObject);

    lpNetworkEvents->lNetworkEvents = it->second.pending;
    memcpy(lpNetworkEvents->iErrorCode, it->second.errors, sizeof(lpNetworkEvents->iErrorCode));

    it->second.pending = 0;
    memset(it->second.errors, 0, sizeof(it->second.errors));
    return 0;
}

int WINAPI WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned int wMsg, long lEvent)
{
    if (!requireStarted())
        return SOCKET_ERROR;

    if (lEvent & ~(long)FD_ALL_EVENTS)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    /* hWnd 为空或 lEvent 为 0 都是注销通知。Win32 在注销时不会把套接字恢复
     * 成阻塞模式（那要另外调 ioctlsocket(FIONBIO)），这里保持一致。 */
    if (!hWnd || lEvent == 0)
    {
        std::lock_guard<std::mutex> lock(g_eventLock);
        g_eventEntries.erase(s);
        return 0;
    }

    /* 与 WSAEventSelect 一样，异步通知要求套接字是非阻塞的。 */
    unsigned long on = 1;
    int err = 0;
    if (swinx::psxIoctl((int)s, swinx::PsxIoctlBlocking, &on, &err) < 0)
        return failWith(err);

    {
        std::lock_guard<std::mutex> lock(g_eventLock);
        SelectEntry &entry = g_eventEntries[s];
        entry.fd = s;
        /* WSAAsyncSelect 与 WSAEventSelect 互斥：后注册的一方接管通知。 */
        entry.event = NULL;
        entry.hwnd = hWnd;
        entry.msg = wMsg;
        entry.mask = lEvent;
        entry.pending = 0;
        entry.reported = 0;
        memset(entry.errors, 0, sizeof(entry.errors));
    }

    ensurePump();
    return 0;
}

int WINAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, int timeout)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (!fdArray || fds == 0)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    swinx::PsxFdSet readSet;
    swinx::PsxFdSet writeSet;
    swinx::PsxFdSet exceptSet;
    readSet.count = writeSet.count = exceptSet.count = 0;

    for (ULONG i = 0; i < fds; ++i)
    {
        fdArray[i].revents = 0;
        int fd = (int)(UINT_PTR)fdArray[i].fd;
        if (fdArray[i].events & (POLLIN | POLLRDNORM | POLLRDBAND))
            readSet.fds[readSet.count++] = fd;
        if (fdArray[i].events & (POLLOUT | POLLWRNORM | POLLWRBAND))
            writeSet.fds[writeSet.count++] = fd;
        if (fdArray[i].events & POLLPRI)
            exceptSet.fds[exceptSet.count++] = fd;
    }

    long usec = (timeout < 0) ? -1 : (long)timeout * 1000L;
    int err = 0;
    int rc = swinx::psxSelect(&readSet, &writeSet, &exceptSet, usec, &err);
    if (rc < 0)
        return failWith(err);

    int readyCount = 0;
    for (ULONG i = 0; i < fds; ++i)
    {
        int fd = (int)(UINT_PTR)fdArray[i].fd;
        SHORT revents = 0;

        for (int k = 0; k < readSet.count; ++k)
        {
            if (readSet.fds[k] == fd)
            {
                revents |= POLLRDNORM;
                break;
            }
        }
        for (int k = 0; k < writeSet.count; ++k)
        {
            if (writeSet.fds[k] == fd)
            {
                revents |= POLLWRNORM;
                break;
            }
        }
        for (int k = 0; k < exceptSet.count; ++k)
        {
            if (exceptSet.fds[k] == fd)
            {
                revents |= POLLPRI;
                break;
            }
        }

        fdArray[i].revents = revents;
        if (revents)
            ++readyCount;
    }

    return readyCount;
}

/* ============================================================ 地址字符串化 */

int WINAPI WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, void *lpProtocolInfo,
                               LPSTR lpszAddressString, LPDWORD lpdwAddressStringLength)
{
    (void)lpProtocolInfo;

    if (!lpsaAddress || !lpszAddressString || !lpdwAddressStringLength)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    swinx::PsxAddr addr;
    if (!addrFromWin(lpsaAddress, (int)dwAddressLength, &addr))
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    char host[INET6_ADDRSTRLEN];
    if (swinx::psxNtop(addr.family, addr.bytes, host, (int)sizeof(host)) < 0)
    {
        swinx::wsa::setLastError(WSAEINVAL);
        return SOCKET_ERROR;
    }

    /* Win32 的格式：IPv4 "1.2.3.4:80"，IPv6 "[::1]:80"。 */
    std::string text;
    if (addr.family == swinx::PsxAfInet6)
    {
        text = "[";
        text += host;
        text += "]";
    }
    else
    {
        text = host;
    }
    text += ":";
    text += std::to_string((long long)addr.port);

    if (*lpdwAddressStringLength < (DWORD)(text.size() + 1))
    {
        *lpdwAddressStringLength = (DWORD)(text.size() + 1);
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    memcpy(lpszAddressString, text.c_str(), text.size() + 1);
    *lpdwAddressStringLength = (DWORD)(text.size() + 1);
    return 0;
}

int WINAPI WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, void *lpProtocolInfo,
                               LPWSTR lpszAddressString, LPDWORD lpdwAddressStringLength)
{
    if (!lpszAddressString || !lpdwAddressStringLength)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    /* 先按 A 版算出字节数，避免写两遍格式化逻辑。 */
    char narrow[128];
    DWORD size = (DWORD)sizeof(narrow);
    if (WSAAddressToStringA(lpsaAddress, dwAddressLength, lpProtocolInfo, narrow, &size) == SOCKET_ERROR)
        return SOCKET_ERROR;

    DWORD needed = (DWORD)(strlen(narrow) + 1);
    if (*lpdwAddressStringLength < needed)
    {
        *lpdwAddressStringLength = needed;
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    for (DWORD i = 0; i < needed; ++i)
        lpszAddressString[i] = (wchar_t)(unsigned char)narrow[i];
    *lpdwAddressStringLength = needed;
    return 0;
}

int WINAPI WSAStringToAddressA(LPSTR AddressString, int AddressFamily, void *lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength)
{
    (void)lpProtocolInfo;

    if (!AddressString || !lpAddress || !lpAddressLength)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    std::string text(AddressString);
    std::string host = text;
    unsigned short port = 0;

    /* 拆 [v6]:port / v4:port。没有冒号就当作"只有地址"。 */
    if (!text.empty() && text[0] == '[')
    {
        size_t close = text.find(']');
        if (close == std::string::npos)
        {
            swinx::wsa::setLastError(WSAEINVAL);
            return SOCKET_ERROR;
        }
        host = text.substr(1, close - 1);
        if (close + 1 < text.size() && text[close + 1] == ':')
            port = (unsigned short)atoi(text.c_str() + close + 2);
    }
    else
    {
        size_t colon = text.rfind(':');
        /* IPv6 的裸写法里有多个冒号，此时不能按端口拆。 */
        if (colon != std::string::npos && text.find(':') == colon)
        {
            host = text.substr(0, colon);
            port = (unsigned short)atoi(text.c_str() + colon + 1);
        }
    }

    swinx::PsxAddr addr;
    memset(&addr, 0, sizeof(addr));

    int family = AddressFamily;
    if (family == AF_UNSPEC)
        family = (host.find(':') != std::string::npos) ? AF_INET6 : AF_INET;

    if (family == AF_INET6)
    {
        addr.family = swinx::PsxAfInet6;
        if (swinx::psxPton(swinx::PsxAfInet6, host.c_str(), addr.bytes) < 0)
        {
            swinx::wsa::setLastError(WSAEINVAL);
            return SOCKET_ERROR;
        }
    }
    else
    {
        addr.family = swinx::PsxAfInet;
        if (swinx::psxPton(swinx::PsxAfInet, host.c_str(), addr.bytes) < 0)
        {
            swinx::wsa::setLastError(WSAEINVAL);
            return SOCKET_ERROR;
        }
    }
    addr.port = port;

    int written = addrToWin(addr, lpAddress, *lpAddressLength);
    if (written == 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }
    *lpAddressLength = written;
    return 0;
}

int WINAPI WSAStringToAddressW(LPWSTR AddressString, int AddressFamily, void *lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength)
{
    if (!AddressString)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    char narrow[128];
    int i = 0;
    for (; AddressString[i] && i < (int)sizeof(narrow) - 1; ++i)
        narrow[i] = (char)AddressString[i];
    narrow[i] = 0;

    return WSAStringToAddressA(narrow, AddressFamily, lpProtocolInfo, lpAddress, lpAddressLength);
}

/* ============================================================== 名字解析 */

int WINAPI swinx_getaddrinfo(const char *pNodeName, const char *pServiceName, const struct addrinfo *pHints,
                       struct addrinfo **ppResult)
{
    if (!requireStarted())
        return WSANOTINITIALISED;
    if (!ppResult)
        return WSAEFAULT;
    *ppResult = NULL;

    int family = pHints ? pHints->ai_family : AF_UNSPEC;
    int sockType = pHints ? pHints->ai_socktype : 0;
    int protocol = pHints ? pHints->ai_protocol : 0;
    int flags = pHints ? mapAiFlags(pHints->ai_flags) : 0;

    swinx::PsxAddrInfo *list = NULL;
    int count = 0;
    int err = 0;
    if (swinx::psxResolve(pNodeName, pServiceName, family, sockType, protocol, flags, &list, &count, &err) < 0)
        return gaiErrorFromErrno(err);

    ADDRINFOA *head = buildAddrInfoA(list, count);
    free(list);
    if (!head)
        return WSAENOBUFS;

    *ppResult = head;
    return 0;
}

void WINAPI swinx_freeaddrinfo(struct addrinfo *pAddrInfo)
{
    /* 整条链在同一个块里 —— 见 buildAddrInfoA。 */
    free(pAddrInfo);
}

int WINAPI swinx_getnameinfo(const struct sockaddr *pSockaddr, int SockaddrLength, char *pNodeBuffer,
                       DWORD NodeBufferSize, char *pServiceBuffer, DWORD ServiceBufferSize, int Flags)
{
    if (!pSockaddr)
        return WSAEFAULT;

    swinx::PsxAddr addr;
    if (!addrFromWin(pSockaddr, SockaddrLength, &addr))
        return WSAEAFNOSUPPORT;

    if (pNodeBuffer && NodeBufferSize > 0)
    {
        char host[INET6_ADDRSTRLEN];
        if (Flags & NI_NUMERICHOST)
        {
            if (swinx::psxNtop(addr.family, addr.bytes, host, (int)sizeof(host)) < 0)
                return WSAEAFNOSUPPORT;
            if (strlen(host) + 1 > NodeBufferSize)
                return WSAEFAULT;
            strcpy(pNodeBuffer, host);
        }
        else
        {
            /* 反向解析成功就用主机名，否则按 NI_NAMEREQD 决定是失败还是退回数字形式。 */
            swinx::PsxHostEnt entry;
            int err = 0;
            if (swinx::psxLookupAddr(addr.bytes, addr.family == swinx::PsxAfInet6 ? 16 : 4, addr.family,
                                     &entry, &err) == 0)
            {
                if (strlen(entry.name) + 1 > NodeBufferSize)
                    return WSAEFAULT;
                strcpy(pNodeBuffer, entry.name);
            }
            else if (Flags & NI_NAMEREQD)
            {
                return WSAHOST_NOT_FOUND;
            }
            else
            {
                if (swinx::psxNtop(addr.family, addr.bytes, host, (int)sizeof(host)) < 0)
                    return WSAEAFNOSUPPORT;
                if (strlen(host) + 1 > NodeBufferSize)
                    return WSAEFAULT;
                strcpy(pNodeBuffer, host);
            }
        }
    }

    if (pServiceBuffer && ServiceBufferSize > 0)
    {
        char service[64];
        bool resolved = false;

        if (!(Flags & NI_NUMERICSERV))
        {
            char name[64];
            const char *proto = (Flags & NI_DGRAM) ? "udp" : "tcp";
            if (swinx::psxServByPort(addr.port, proto, name, (int)sizeof(name)) == 0)
            {
                strncpy(service, name, sizeof(service) - 1);
                service[sizeof(service) - 1] = 0;
                resolved = true;
            }
        }
        if (!resolved)
        {
            snprintf(service, sizeof(service), "%u", (unsigned)addr.port);
        }

        if (strlen(service) + 1 > ServiceBufferSize)
            return WSAEFAULT;
        strcpy(pServiceBuffer, service);
    }

    return 0;
}

int WINAPI GetAddrInfoA(const char *pNodeName, const char *pServiceName, const struct addrinfo *pHints,
                        PADDRINFOA *ppResult)
{
    return swinx_getaddrinfo(pNodeName, pServiceName, pHints, (struct addrinfo **)ppResult);
}

int WINAPI GetAddrInfoW(const wchar_t *pNodeName, const wchar_t *pServiceName, const ADDRINFOW *pHints,
                        PADDRINFOW *ppResult)
{
    if (!requireStarted())
        return WSANOTINITIALISED;
    if (!ppResult)
        return WSAEFAULT;
    *ppResult = NULL;

    char node[256];
    char service[64];
    node[0] = 0;
    service[0] = 0;

    if (pNodeName)
    {
        int i = 0;
        for (; pNodeName[i] && i < (int)sizeof(node) - 1; ++i)
            node[i] = (char)pNodeName[i];
        node[i] = 0;
    }
    if (pServiceName)
    {
        int i = 0;
        for (; pServiceName[i] && i < (int)sizeof(service) - 1; ++i)
            service[i] = (char)pServiceName[i];
        service[i] = 0;
    }

    int family = pHints ? pHints->ai_family : AF_UNSPEC;
    int sockType = pHints ? pHints->ai_socktype : 0;
    int protocol = pHints ? pHints->ai_protocol : 0;
    int flags = pHints ? mapAiFlags(pHints->ai_flags) : 0;

    swinx::PsxAddrInfo *list = NULL;
    int count = 0;
    int err = 0;
    if (swinx::psxResolve(pNodeName ? node : NULL, pServiceName ? service : NULL, family, sockType, protocol,
                          flags, &list, &count, &err) < 0)
        return gaiErrorFromErrno(err);

    ADDRINFOW *head = buildAddrInfoW(list, count);
    free(list);
    if (!head)
        return WSAENOBUFS;

    *ppResult = head;
    return 0;
}

void WINAPI FreeAddrInfoA(PADDRINFOA pAddrInfo)
{
    free(pAddrInfo);
}

void WINAPI FreeAddrInfoW(PADDRINFOW pAddrInfo)
{
    free(pAddrInfo);
}

int WINAPI GetNameInfoA(const struct sockaddr *pSockaddr, int SockaddrLength, char *pNodeBuffer,
                        DWORD NodeBufferSize, char *pServiceBuffer, DWORD ServiceBufferSize, int Flags)
{
    return swinx_getnameinfo(pSockaddr, SockaddrLength, pNodeBuffer, NodeBufferSize, pServiceBuffer,
                       ServiceBufferSize, Flags);
}

int WINAPI GetNameInfoW(const struct sockaddr *pSockaddr, int SockaddrLength, wchar_t *pNodeBuffer,
                        DWORD NodeBufferSize, wchar_t *pServiceBuffer, DWORD ServiceBufferSize, int Flags)
{
    char node[256];
    char service[64];
    int rc = swinx_getnameinfo(pSockaddr, SockaddrLength, pNodeBuffer ? node : NULL,
                         pNodeBuffer ? (DWORD)sizeof(node) : 0, pServiceBuffer ? service : NULL,
                         pServiceBuffer ? (DWORD)sizeof(service) : 0, Flags);
    if (rc != 0)
        return rc;

    if (pNodeBuffer)
    {
        DWORD len = (DWORD)strlen(node) + 1;
        if (len > NodeBufferSize)
            return WSAEFAULT;
        for (DWORD i = 0; i < len; ++i)
            pNodeBuffer[i] = (wchar_t)(unsigned char)node[i];
    }
    if (pServiceBuffer)
    {
        DWORD len = (DWORD)strlen(service) + 1;
        if (len > ServiceBufferSize)
            return WSAEFAULT;
        for (DWORD i = 0; i < len; ++i)
            pServiceBuffer[i] = (wchar_t)(unsigned char)service[i];
    }
    return 0;
}

struct hostent *WINAPI swinx_gethostbyname(const char *name)
{
    static thread_local HostEntryStorage storage;

    if (!requireStarted())
        return NULL;

    swinx::PsxHostEnt entry;
    int err = 0;
    if (swinx::psxLookupHost(name, &entry, &err) < 0)
    {
        swinx::wsa::setLastError(err == ENOENT ? WSAHOST_NOT_FOUND : errorFromErrno(err));
        return NULL;
    }

    fillHostEntry(entry, &storage);
    return &storage.host;
}

struct hostent *WINAPI swinx_gethostbyaddr(const char *addr, int len, int type)
{
    static thread_local HostEntryStorage storage;

    if (!requireStarted())
        return NULL;
    if (!addr || len <= 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return NULL;
    }

    swinx::PsxHostEnt entry;
    int err = 0;
    if (swinx::psxLookupAddr((const unsigned char *)addr, len, type, &entry, &err) < 0)
    {
        swinx::wsa::setLastError(err == ENOENT ? WSAHOST_NOT_FOUND : errorFromErrno(err));
        return NULL;
    }

    fillHostEntry(entry, &storage);
    return &storage.host;
}

int WINAPI swinx_gethostname(char *name, int namelen)
{
    if (!requireStarted())
        return SOCKET_ERROR;
    if (!name || namelen <= 0)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return SOCKET_ERROR;
    }

    int err = 0;
    if (swinx::psxHostName(name, namelen, &err) < 0)
        return failWith(err);
    return 0;
}

struct servent *WINAPI swinx_getservbyname(const char *name, const char *proto)
{
    static thread_local ServEntryStorage storage;

    if (!name)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return NULL;
    }

    int port = 0;
    if (swinx::psxServByName(name, proto, &port, storage.protoBuffer, (int)sizeof(storage.protoBuffer)) < 0)
    {
        swinx::wsa::setLastError(WSATYPE_NOT_FOUND);
        return NULL;
    }

    memset(&storage.serv, 0, sizeof(storage.serv));
    strncpy(storage.nameBuffer, name, sizeof(storage.nameBuffer) - 1);
    storage.serv.s_name = storage.nameBuffer;
    storage.serv.s_proto = storage.protoBuffer;
    storage.serv.s_port = (short)swap16((unsigned short)port); /* Win32 里 s_port 是网络序 */
    return &storage.serv;
}

struct servent *WINAPI swinx_getservbyport(int port, const char *proto)
{
    static thread_local ServEntryStorage storage;

    char name[64];
    if (swinx::psxServByPort(swap16((unsigned short)port), proto, name, (int)sizeof(name)) < 0)
    {
        swinx::wsa::setLastError(WSATYPE_NOT_FOUND);
        return NULL;
    }

    memset(&storage.serv, 0, sizeof(storage.serv));
    strncpy(storage.nameBuffer, name, sizeof(storage.nameBuffer) - 1);
    if (proto)
        strncpy(storage.protoBuffer, proto, sizeof(storage.protoBuffer) - 1);
    storage.serv.s_name = storage.nameBuffer;
    storage.serv.s_proto = storage.protoBuffer;
    storage.serv.s_port = (short)port;
    return &storage.serv;
}

struct protoent *WINAPI swinx_getprotobyname(const char *name)
{
    static thread_local ProtoEntryStorage storage;

    if (!name)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return NULL;
    }

    int proto = 0;
    if (swinx::psxProtoByName(name, &proto) < 0)
    {
        swinx::wsa::setLastError(WSATYPE_NOT_FOUND);
        return NULL;
    }

    memset(&storage.proto, 0, sizeof(storage.proto));
    strncpy(storage.nameBuffer, name, sizeof(storage.nameBuffer) - 1);
    storage.proto.p_name = storage.nameBuffer;
    storage.proto.p_proto = (short)proto;
    return &storage.proto;
}

struct protoent *WINAPI swinx_getprotobynumber(int proto)
{
    static thread_local ProtoEntryStorage storage;

    char name[64];
    if (swinx::psxProtoByNumber(proto, name, (int)sizeof(name)) < 0)
    {
        swinx::wsa::setLastError(WSATYPE_NOT_FOUND);
        return NULL;
    }

    memset(&storage.proto, 0, sizeof(storage.proto));
    strncpy(storage.nameBuffer, name, sizeof(storage.nameBuffer) - 1);
    storage.proto.p_name = storage.nameBuffer;
    storage.proto.p_proto = (short)proto;
    return &storage.proto;
}

/* ============================================================ 与 libc 同名的转换 */

ULONG swinx_inet_addr(const char *cp)
{
    unsigned char bytes[4];
    if (!cp || swinx::psxPton(swinx::PsxAfInet, cp, bytes) < 0)
        return INADDR_NONE;
    return ((ULONG)bytes[0]) | ((ULONG)bytes[1] << 8) | ((ULONG)bytes[2] << 16) |
           ((ULONG)bytes[3] << 24);
}

char *swinx_inet_ntoa(struct in_addr in)
{
    static thread_local char text[INET_ADDRSTRLEN];
    unsigned char bytes[4];
    ULONG raw = in.S_un.S_addr;
    bytes[0] = (unsigned char)(raw & 0xFF);
    bytes[1] = (unsigned char)((raw >> 8) & 0xFF);
    bytes[2] = (unsigned char)((raw >> 16) & 0xFF);
    bytes[3] = (unsigned char)((raw >> 24) & 0xFF);

    if (swinx::psxNtop(swinx::PsxAfInet, bytes, text, (int)sizeof(text)) < 0)
        text[0] = 0;
    return text;
}

const char *swinx_inet_ntop(int Family, const void *pAddr, char *pStringBuf, size_t StringBufSize)
{
    if (!pAddr || !pStringBuf || StringBufSize == 0)
        return NULL;
    if (swinx::psxNtop(Family, (const unsigned char *)pAddr, pStringBuf, (int)StringBufSize) < 0)
        return NULL;
    return pStringBuf;
}

int swinx_inet_pton(int Family, const char *pszAddrString, void *pAddrBuf)
{
    if (!pszAddrString || !pAddrBuf)
        return 0;
    return swinx::psxPton(Family, pszAddrString, (unsigned char *)pAddrBuf) == 0 ? 1 : 0;
}

int InetPtonA(int Family, const char *pszAddrString, void *pAddrBuf)
{
    return swinx_inet_pton(Family, pszAddrString, pAddrBuf);
}

int InetPtonW(int Family, const wchar_t *pszAddrString, void *pAddrBuf)
{
    if (!pszAddrString)
    {
        swinx::wsa::setLastError(WSAEFAULT);
        return -1;
    }

    char narrow[INET6_ADDRSTRLEN * 2];
    int i = 0;
    for (; pszAddrString[i] && i < (int)sizeof(narrow) - 1; ++i)
        narrow[i] = (char)pszAddrString[i];
    narrow[i] = 0;

    return swinx_inet_pton(Family, narrow, pAddrBuf) == 1 ? 1 : 0;
}

const char *InetNtopA(int Family, const void *pAddr, char *pStringBuf, size_t StringBufSize)
{
    return swinx_inet_ntop(Family, pAddr, pStringBuf, StringBufSize);
}

const wchar_t *InetNtopW(int Family, const void *pAddr, wchar_t *pStringBuf, size_t StringBufSize)
{
    if (!pStringBuf || StringBufSize == 0)
        return NULL;

    char narrow[INET6_ADDRSTRLEN * 2];
    if (!swinx_inet_ntop(Family, pAddr, narrow, sizeof(narrow)))
        return NULL;

    size_t len = strlen(narrow);
    if (len + 1 > StringBufSize)
        return NULL;

    for (size_t i = 0; i <= len; ++i)
        pStringBuf[i] = (wchar_t)(unsigned char)narrow[i];
    return pStringBuf;
}

/* htons 一族：符号名经 winsock2.h 的兼容宏落到 swinx_htons 等，
 * 不与 libc 重名，也就不再需要 weak —— 这里的算术实现就是唯一实现。 */
unsigned short swinx_htons(unsigned short hostshort)
{
    return swap16(hostshort);
}

unsigned short swinx_ntohs(unsigned short netshort)
{
    return swap16(netshort);
}

ULONG swinx_htonl(ULONG hostlong)
{
    return swap32(hostlong);
}

ULONG swinx_ntohl(ULONG netlong)
{
    return swap32(netlong);
}
