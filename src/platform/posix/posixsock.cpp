/* 套接字操作的 POSIX 实现（见 src/posixsock.h 的分层说明）。
 *
 * 这一层是唯一 include <sys/socket.h> 的地方：Win32 头与 POSIX 头在这里之后
 * 就再也不会相遇。上层的 winsock2.cpp 只看得到 PsxAddr / PsxFdSet 这些 POD。
 *
 * 几个刻意的取舍：
 *   - select 用 poll 实现。POSIX 的 fd_set 有 FD_SETSIZE 上限、还要自己算
 *     nfds；poll 没有这两个坑，而且 Win32 的 fd_set 本来就是「fd 数组」，
 *     直接映射过去比位图更自然。
 *   - gethostbyname/gethostbyaddr 的 *_r 变体在 macOS 上签名不同，所以用
 *     非重入版加一把锁。名字解析的并发度不是瓶颈，能编译优先。
 *   - SO_EXCLUSIVEADDRUSE 在 POSIX 上没有对应物，用「关掉 SO_REUSEADDR」
 *     近似，并如实返回成功——它的语义本来就是"别把地址让给别人"。
 */

#include "posixsock.h"
/* 注意：本文件**不能** include winsock2.h —— 它在本文件里先于 POSIX 头，
 * 一旦两者相遇，winsock2.h 的 SOCK_STREAM/sockaddr 宏会把 glibc 的
 * <bits/socket_type.h> 枚举与 <sys/socket.h> 的结构体全部撕碎（真实的
 * Linux 构建已经踩过一遍）。本层只认 posixsock.h 的 POD。 */
#include <arpa/inet.h> /* inet_ntop / inet_pton（glibc 不经其他头间接声明） */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/filio.h> /* FIONREAD 在 macOS 上单独成头 */
#endif

#include <map>
#include <mutex>
#include <vector>

namespace swinx
{

namespace
{

int toPosixFamily(int family)
{
    if (family == PsxAfInet)
        return AF_INET;
    if (family == PsxAfInet6)
        return AF_INET6;
    return AF_UNSPEC;
}

bool toSockAddr(const PsxAddr &in, struct sockaddr_storage *out, socklen_t *outLen)
{
    memset(out, 0, sizeof(*out));

    if (in.family == PsxAfInet)
    {
        struct sockaddr_in *sa = (struct sockaddr_in *)out;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(in.port);
        memcpy(&sa->sin_addr, in.bytes, 4);
        *outLen = (socklen_t)sizeof(struct sockaddr_in);
        return true;
    }
    if (in.family == PsxAfInet6)
    {
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)out;
        sa->sin6_family = AF_INET6;
        sa->sin6_port = htons(in.port);
        memcpy(&sa->sin6_addr, in.bytes, 16);
        sa->sin6_scope_id = in.scope;
        *outLen = (socklen_t)sizeof(struct sockaddr_in6);
        return true;
    }
    *outLen = 0;
    return false;
}

bool fromSockAddr(const struct sockaddr *in, socklen_t len, PsxAddr *out)
{
    memset(out, 0, sizeof(*out));
    if (!in)
        return false;

    if (in->sa_family == AF_INET && len >= (socklen_t)sizeof(struct sockaddr_in))
    {
        const struct sockaddr_in *sa = (const struct sockaddr_in *)in;
        out->family = PsxAfInet;
        out->port = ntohs(sa->sin_port);
        memcpy(out->bytes, &sa->sin_addr, 4);
        return true;
    }
    if (in->sa_family == AF_INET6 && len >= (socklen_t)sizeof(struct sockaddr_in6))
    {
        const struct sockaddr_in6 *sa = (const struct sockaddr_in6 *)in;
        out->family = PsxAfInet6;
        out->port = ntohs(sa->sin6_port);
        memcpy(out->bytes, &sa->sin6_addr, 16);
        out->scope = sa->sin6_scope_id;
        return true;
    }

    out->family = PsxAfUnspec;
    return false;
}

int fail(int *err)
{
    if (err)
        *err = errno;
    return -1;
}

int setBlocking(int fd, int blocking)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

/* 名字解析用的一把全局锁：gethostbyname/gethostbyaddr 返回的是静态缓冲。 */
std::mutex &resolverMutex()
{
    static std::mutex lock;
    return lock;
}

} // namespace

int psxSocket(int family, int type, int protocol, int *err)
{
    int fd = ::socket(toPosixFamily(family), type, protocol);
    if (fd < 0)
        return fail(err);
    return fd;
}

int psxBind(int fd, const PsxAddr *addr, int *err)
{
    struct sockaddr_storage storage;
    socklen_t len = 0;
    if (!addr || !toSockAddr(*addr, &storage, &len))
    {
        if (err)
            *err = EINVAL;
        return -1;
    }
    if (::bind(fd, (const struct sockaddr *)&storage, len) < 0)
        return fail(err);
    return 0;
}

int psxConnect(int fd, const PsxAddr *addr, int *err)
{
    struct sockaddr_storage storage;
    socklen_t len = 0;
    if (!addr || !toSockAddr(*addr, &storage, &len))
    {
        if (err)
            *err = EINVAL;
        return -1;
    }
    if (::connect(fd, (const struct sockaddr *)&storage, len) < 0)
        return fail(err);
    return 0;
}

int psxListen(int fd, int backlog, int *err)
{
    if (::listen(fd, backlog) < 0)
        return fail(err);
    return 0;
}

int psxAccept(int fd, PsxAddr *peer, int *err)
{
    struct sockaddr_storage storage;
    socklen_t len = (socklen_t)sizeof(storage);
    int client = ::accept(fd, (struct sockaddr *)&storage, &len);
    if (client < 0)
        return fail(err);

    if (peer)
        fromSockAddr((const struct sockaddr *)&storage, len, peer);
    return client;
}

long psxSend(int fd, const void *buf, unsigned long len, int flags, int *err)
{
    ssize_t n = ::send(fd, buf, (size_t)len, flags);
    if (n < 0)
        return fail(err);
    return (long)n;
}

long psxRecv(int fd, void *buf, unsigned long len, int flags, int *err)
{
    ssize_t n = ::recv(fd, buf, (size_t)len, flags);
    if (n < 0)
        return fail(err);
    return (long)n;
}

long psxSendTo(int fd, const void *buf, unsigned long len, int flags, const PsxAddr *to, int *err)
{
    struct sockaddr_storage storage;
    socklen_t addrLen = 0;
    if (!to || !toSockAddr(*to, &storage, &addrLen))
    {
        if (err)
            *err = EINVAL;
        return -1;
    }
    ssize_t n = ::sendto(fd, buf, (size_t)len, flags, (const struct sockaddr *)&storage, addrLen);
    if (n < 0)
        return fail(err);
    return (long)n;
}

long psxRecvFrom(int fd, void *buf, unsigned long len, int flags, PsxAddr *from, int *err)
{
    struct sockaddr_storage storage;
    socklen_t addrLen = (socklen_t)sizeof(storage);
    ssize_t n = ::recvfrom(fd, buf, (size_t)len, flags, (struct sockaddr *)&storage, &addrLen);
    if (n < 0)
        return fail(err);
    if (from)
        fromSockAddr((const struct sockaddr *)&storage, addrLen, from);
    return (long)n;
}

int psxShutdown(int fd, int how, int *err)
{
    int mode = SHUT_RDWR;
    if (how == 0)
        mode = SHUT_RD;
    else if (how == 1)
        mode = SHUT_WR;
    if (::shutdown(fd, mode) < 0)
        return fail(err);
    return 0;
}

int psxClose(int fd, int *err)
{
    if (::close(fd) < 0)
        return fail(err);
    return 0;
}

int psxGetSockName(int fd, PsxAddr *out, int *err)
{
    struct sockaddr_storage storage;
    socklen_t len = (socklen_t)sizeof(storage);
    if (::getsockname(fd, (struct sockaddr *)&storage, &len) < 0)
        return fail(err);
    if (!fromSockAddr((const struct sockaddr *)&storage, len, out))
    {
        if (err)
            *err = EAFNOSUPPORT;
        return -1;
    }
    return 0;
}

int psxGetPeerName(int fd, PsxAddr *out, int *err)
{
    struct sockaddr_storage storage;
    socklen_t len = (socklen_t)sizeof(storage);
    if (::getpeername(fd, (struct sockaddr *)&storage, &len) < 0)
        return fail(err);
    if (!fromSockAddr((const struct sockaddr *)&storage, len, out))
    {
        if (err)
            *err = EAFNOSUPPORT;
        return -1;
    }
    return 0;
}

namespace
{

/* PsxSockOpt → (level, optname, 值的宽度类别)。 */
struct OptMap
{
    int level;
    int name;
    int kind; /* 0 = int, 1 = PsxLinger, 2 = timeval(毫秒) */
};

bool mapOption(int level, int option, OptMap *out)
{
    if (level == PsxLevelSocket)
    {
        switch (option)
        {
        case PsxOptReuseAddr:
            out->level = SOL_SOCKET;
            out->name = SO_REUSEADDR;
            out->kind = 0;
            return true;
        case PsxOptKeepAlive:
            out->level = SOL_SOCKET;
            out->name = SO_KEEPALIVE;
            out->kind = 0;
            return true;
        case PsxOptRecvBuf:
            out->level = SOL_SOCKET;
            out->name = SO_RCVBUF;
            out->kind = 0;
            return true;
        case PsxOptSendBuf:
            out->level = SOL_SOCKET;
            out->name = SO_SNDBUF;
            out->kind = 0;
            return true;
        case PsxOptLinger:
            out->level = SOL_SOCKET;
            out->name = SO_LINGER;
            out->kind = 1;
            return true;
        case PsxOptRecvTimeout:
            out->level = SOL_SOCKET;
            out->name = SO_RCVTIMEO;
            out->kind = 2;
            return true;
        case PsxOptSendTimeout:
            out->level = SOL_SOCKET;
            out->name = SO_SNDTIMEO;
            out->kind = 2;
            return true;
        case PsxOptBroadcast:
            out->level = SOL_SOCKET;
            out->name = SO_BROADCAST;
            out->kind = 0;
            return true;
        case PsxOptDontRoute:
            out->level = SOL_SOCKET;
            out->name = SO_DONTROUTE;
            out->kind = 0;
            return true;
        case PsxOptError:
            out->level = SOL_SOCKET;
            out->name = SO_ERROR;
            out->kind = 0;
            return true;
        case PsxOptType:
            out->level = SOL_SOCKET;
            out->name = SO_TYPE;
            out->kind = 0;
            return true;
        case PsxOptExclusiveAddrUse:
            /* POSIX 没有独占绑定；"不让别人复用"就是关掉 SO_REUSEADDR。 */
            out->level = SOL_SOCKET;
            out->name = SO_REUSEADDR;
            out->kind = 0;
            return true;
        default:
            return false;
        }
    }

    if (level == PsxLevelTcp)
    {
        switch (option)
        {
        case PsxOptNoDelay:
            out->level = IPPROTO_TCP;
            out->name = TCP_NODELAY;
            out->kind = 0;
            return true;
        default:
            return false;
        }
    }

    if (level == PsxLevelIp)
    {
        switch (option)
        {
        case PsxOptTtl:
            out->level = IPPROTO_IP;
            out->name = IP_TTL;
            out->kind = 0;
            return true;
        case PsxOptMulticastTtl:
            out->level = IPPROTO_IP;
            out->name = IP_MULTICAST_TTL;
            out->kind = 0;
            return true;
        case PsxOptMulticastLoop:
            out->level = IPPROTO_IP;
            out->name = IP_MULTICAST_LOOP;
            out->kind = 0;
            return true;
        default:
            return false;
        }
    }

    return false;
}

} // namespace

int psxSetSockOpt(int fd, int level, int option, const void *value, int len, int *err)
{
    OptMap map;
    if (!value || !mapOption(level, option, &map))
    {
        if (err)
            *err = ENOPROTOOPT;
        return -1;
    }

    int rc = 0;
    if (map.kind == 0 && len >= (int)sizeof(int))
    {
        int raw = *(const int *)value;
        if (option == PsxOptExclusiveAddrUse)
            raw = raw ? 0 : 1; /* 反过来写进 SO_REUSEADDR */
        rc = ::setsockopt(fd, map.level, map.name, &raw, sizeof(raw));
    }
    else if (map.kind == 1 && len >= (int)sizeof(PsxLinger))
    {
        const PsxLinger *src = (const PsxLinger *)value;
        struct linger raw;
        raw.l_onoff = src->onOff;
        raw.l_linger = src->seconds;
        rc = ::setsockopt(fd, map.level, map.name, &raw, sizeof(raw));
    }
    else if (map.kind == 2 && len >= (int)sizeof(int))
    {
        /* Win32 的超时单位是毫秒，POSIX 是 timeval。 */
        struct timeval tv;
        int ms = *(const int *)value;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        rc = ::setsockopt(fd, map.level, map.name, &tv, sizeof(tv));
    }
    else
    {
        if (err)
            *err = EINVAL;
        return -1;
    }

    if (rc < 0)
        return fail(err);
    return 0;
}

int psxGetSockOpt(int fd, int level, int option, void *value, int *len, int *err)
{
    OptMap map;
    if (!value || !len || !mapOption(level, option, &map))
    {
        if (err)
            *err = ENOPROTOOPT;
        return -1;
    }

    if (map.kind == 0)
    {
        int raw = 0;
        socklen_t rawLen = (socklen_t)sizeof(raw);
        if (::getsockopt(fd, map.level, map.name, &raw, &rawLen) < 0)
            return fail(err);
        if (option == PsxOptExclusiveAddrUse)
            raw = raw ? 0 : 1;
        if (*len < (int)sizeof(int))
        {
            if (err)
                *err = EINVAL;
            return -1;
        }
        *(int *)value = raw;
        *len = (int)sizeof(int);
        return 0;
    }

    if (map.kind == 1)
    {
        struct linger raw;
        socklen_t rawLen = (socklen_t)sizeof(raw);
        if (::getsockopt(fd, map.level, map.name, &raw, &rawLen) < 0)
            return fail(err);
        if (*len < (int)sizeof(PsxLinger))
        {
            if (err)
                *err = EINVAL;
            return -1;
        }
        PsxLinger *dst = (PsxLinger *)value;
        dst->onOff = raw.l_onoff;
        dst->seconds = raw.l_linger;
        *len = (int)sizeof(PsxLinger);
        return 0;
    }

    struct timeval tv;
    socklen_t rawLen = (socklen_t)sizeof(tv);
    if (::getsockopt(fd, map.level, map.name, &tv, &rawLen) < 0)
        return fail(err);
    if (*len < (int)sizeof(int))
    {
        if (err)
            *err = EINVAL;
        return -1;
    }
    *(int *)value = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
    *len = (int)sizeof(int);
    return 0;
}

int psxIoctl(int fd, int cmd, unsigned long *arg, int *err)
{
    if (cmd == PsxIoctlBlocking)
    {
        int on = (arg && *arg) ? 1 : 0;
        if (setBlocking(fd, !on) < 0)
            return fail(err);
        return 0;
    }
    if (cmd == PsxIoctlReadable)
    {
        if (!arg)
        {
            if (err)
                *err = EINVAL;
            return -1;
        }
        int available = 0;
        if (ioctl(fd, FIONREAD, &available) < 0)
            return fail(err);
        *arg = (unsigned long)available;
        return 0;
    }

    if (err)
        *err = EINVAL;
    return -1;
}

int psxSelect(PsxFdSet *readSet, PsxFdSet *writeSet, PsxFdSet *exceptSet, long timeoutUsec, int *err)
{
    std::map<int, short> wanted;

    if (readSet)
    {
        for (int i = 0; i < readSet->count && i < 64; ++i)
            wanted[readSet->fds[i]] |= POLLIN;
    }
    if (writeSet)
    {
        for (int i = 0; i < writeSet->count && i < 64; ++i)
            wanted[writeSet->fds[i]] |= POLLOUT;
    }
    if (exceptSet)
    {
        for (int i = 0; i < exceptSet->count && i < 64; ++i)
            wanted[exceptSet->fds[i]] |= POLLPRI;
    }

    if (wanted.empty())
    {
        /* 与 select 一致：三个集合都空时只做一次纯睡眠。 */
        if (timeoutUsec < 0)
        {
            if (err)
                *err = EINVAL;
            return -1;
        }
        if (timeoutUsec > 0)
        {
            struct timespec ts;
            ts.tv_sec = timeoutUsec / 1000000L;
            ts.tv_nsec = (timeoutUsec % 1000000L) * 1000L;
            nanosleep(&ts, NULL);
        }
        return 0;
    }

    std::vector<struct pollfd> pfds;
    pfds.reserve(wanted.size());
    for (std::map<int, short>::const_iterator it = wanted.begin(); it != wanted.end(); ++it)
    {
        struct pollfd pfd;
        pfd.fd = it->first;
        pfd.events = it->second;
        pfd.revents = 0;
        pfds.push_back(pfd);
    }

    int timeoutMs = -1;
    if (timeoutUsec >= 0)
        timeoutMs = (int)((timeoutUsec + 999) / 1000); /* 向上取整：不提前返回 */

    int rc;
    do
    {
        rc = ::poll(&pfds[0], (nfds_t)pfds.size(), timeoutMs);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0)
        return fail(err);

    /* 把 poll 的结果换算回三个集合。POLLERR/POLLHUP 在 select 语义里既算
     * 可读也算异常 —— 不这么算，调用方会一直等到超时都发现不了连接已断。 */
    std::map<int, int> ready;
    for (size_t i = 0; i < pfds.size(); ++i)
    {
        short rev = pfds[i].revents;
        int bits = 0;
        if (rev & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
            bits |= PsxReadyRead;
        if (rev & POLLOUT)
            bits |= PsxReadyWrite;
        if (rev & (POLLPRI | POLLERR | POLLNVAL))
            bits |= PsxReadyExcept;
        if (bits)
            ready[pfds[i].fd] = bits;
    }

    if (readSet)
    {
        int n = 0;
        for (int i = 0; i < readSet->count && i < 64; ++i)
        {
            std::map<int, int>::const_iterator it = ready.find(readSet->fds[i]);
            if (it != ready.end() && (it->second & PsxReadyRead))
                readSet->fds[n++] = readSet->fds[i];
        }
        readSet->count = n;
    }
    if (writeSet)
    {
        int n = 0;
        for (int i = 0; i < writeSet->count && i < 64; ++i)
        {
            std::map<int, int>::const_iterator it = ready.find(writeSet->fds[i]);
            if (it != ready.end() && (it->second & PsxReadyWrite))
                writeSet->fds[n++] = writeSet->fds[i];
        }
        writeSet->count = n;
    }
    if (exceptSet)
    {
        int n = 0;
        for (int i = 0; i < exceptSet->count && i < 64; ++i)
        {
            std::map<int, int>::const_iterator it = ready.find(exceptSet->fds[i]);
            if (it != ready.end() && (it->second & PsxReadyExcept))
                exceptSet->fds[n++] = exceptSet->fds[i];
        }
        exceptSet->count = n;
    }

    return rc;
}

int psxPollOne(int fd, int *err)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN | POLLOUT | POLLPRI;
    pfd.revents = 0;

    int rc;
    do
    {
        rc = ::poll(&pfd, 1, 0);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0)
        return fail(err);
    if (rc == 0)
        return 0;

    int bits = 0;
    if (pfd.revents & (POLLIN | POLLERR | POLLNVAL))
        bits |= PsxReadyRead;
    if (pfd.revents & POLLOUT)
        bits |= PsxReadyWrite;
    if (pfd.revents & (POLLPRI | POLLERR | POLLNVAL))
        bits |= PsxReadyExcept;
    if (pfd.revents & POLLHUP)
        bits |= PsxReadyHangup;
    return bits;
}

int psxResolve(const char *node, const char *service, int family, int sockType, int protocol, int flags,
               PsxAddrInfo **outList, int *outCount, int *err)
{
    if (outList)
        *outList = NULL;
    if (outCount)
        *outCount = 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = toPosixFamily(family);
    hints.ai_socktype = sockType;
    hints.ai_protocol = protocol;
    if (flags & PsxAiCanonName)
        hints.ai_flags |= AI_CANONNAME;
    if (flags & PsxAiPassive)
        hints.ai_flags |= AI_PASSIVE;
    if (flags & PsxAiNumericHost)
        hints.ai_flags |= AI_NUMERICHOST;
    if (flags & PsxAiNumericService)
        hints.ai_flags |= AI_NUMERICSERV;

    struct addrinfo *result = NULL;
    int rc = ::getaddrinfo(node, service, &hints, &result);
    if (rc != 0)
    {
        if (err)
            *err = (rc == EAI_MEMORY) ? ENOMEM : EINVAL;
        return -1;
    }

    int count = 0;
    for (struct addrinfo *p = result; p; p = p->ai_next)
        ++count;
    if (count == 0)
    {
        ::freeaddrinfo(result);
        if (err)
            *err = EINVAL;
        return -1;
    }

    PsxAddrInfo *list = (PsxAddrInfo *)calloc((size_t)count, sizeof(PsxAddrInfo));
    if (!list)
    {
        ::freeaddrinfo(result);
        if (err)
            *err = ENOMEM;
        return -1;
    }

    int written = 0;
    for (struct addrinfo *p = result; p && written < count; p = p->ai_next)
    {
        PsxAddr addr;
        if (!fromSockAddr(p->ai_addr, (socklen_t)p->ai_addrlen, &addr))
            continue;

        PsxAddrInfo *slot = &list[written];
        slot->family = addr.family;
        slot->sockType = p->ai_socktype;
        slot->protocol = p->ai_protocol;
        slot->addr = addr;
        slot->flags = 0;
        if (p->ai_canonname)
        {
            strncpy(slot->canonName, p->ai_canonname, sizeof(slot->canonName) - 1);
            slot->flags |= PsxAiCanonName;
        }
        ++written;
    }

    ::freeaddrinfo(result);

    if (written == 0)
    {
        free(list);
        if (err)
            *err = EAFNOSUPPORT;
        return -1;
    }

    if (outList)
        *outList = list;
    else
        free(list);
    if (outCount)
        *outCount = written;
    return 0;
}

namespace
{

void fillHostEnt(const char *name, int family, int addrLength, int addrCount, const unsigned char **addresses,
                 PsxHostEnt *out)
{
    memset(out, 0, sizeof(*out));
    out->family = family;
    out->addrLength = addrLength;
    out->addrCount = addrCount;
    if (name)
        strncpy(out->name, name, sizeof(out->name) - 1);
    for (int i = 0; i < addrCount && i < 8; ++i)
        memcpy(out->addresses[i], addresses[i], (size_t)addrLength);
}

} // namespace

int psxLookupHost(const char *name, PsxHostEnt *out, int *err)
{
    if (!name || !out)
    {
        if (err)
            *err = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock(resolverMutex());

    struct hostent *host = ::gethostbyname(name);
    if (!host || host->h_addrtype != AF_INET || !host->h_addr_list)
    {
        if (err)
            *err = (host ? EAFNOSUPPORT : ENOENT);
        return -1;
    }

    const unsigned char *addresses[8];
    int count = 0;
    for (int i = 0; host->h_addr_list[i] && i < 8; ++i, ++count)
        addresses[i] = (const unsigned char *)host->h_addr_list[i];

    if (count == 0)
    {
        if (err)
            *err = ENOENT;
        return -1;
    }

    fillHostEnt(host->h_name ? host->h_name : name, PsxAfInet, 4, count, addresses, out);
    return 0;
}

int psxLookupAddr(const unsigned char *bytes, int len, int family, PsxHostEnt *out, int *err)
{
    if (!bytes || !out || (family == PsxAfInet && len < 4) || (family == PsxAfInet6 && len < 16))
    {
        if (err)
            *err = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock(resolverMutex());

    struct hostent *host = ::gethostbyaddr(bytes, (socklen_t)len, toPosixFamily(family));
    if (!host || !host->h_addr_list)
    {
        if (err)
            *err = ENOENT;
        return -1;
    }

    const unsigned char *addresses[8];
    int count = 0;
    for (int i = 0; host->h_addr_list[i] && i < 8; ++i, ++count)
        addresses[i] = (const unsigned char *)host->h_addr_list[i];

    if (count == 0)
    {
        if (err)
            *err = ENOENT;
        return -1;
    }

    int addrLength = host->h_length > 0 ? host->h_length : (family == PsxAfInet6 ? 16 : 4);
    fillHostEnt(host->h_name, family, addrLength, count, addresses, out);
    return 0;
}

int psxHostName(char *out, int capacity, int *err)
{
    if (!out || capacity <= 0)
    {
        if (err)
            *err = EINVAL;
        return -1;
    }
    if (::gethostname(out, (size_t)capacity) < 0)
        return fail(err);
    out[capacity - 1] = 0;
    return 0;
}

int psxNtop(int family, const unsigned char *bytes, char *dst, int capacity)
{
    const void *src = bytes;
    int posixFamily = toPosixFamily(family);
    if (posixFamily == AF_UNSPEC || !bytes || !dst || capacity <= 0)
        return -1;
    return ::inet_ntop(posixFamily, src, dst, (socklen_t)capacity) ? 0 : -1;
}

int psxPton(int family, const char *src, unsigned char *bytes)
{
    int posixFamily = toPosixFamily(family);
    if (posixFamily == AF_UNSPEC || !src || !bytes)
        return -1;
    return ::inet_pton(posixFamily, src, bytes) == 1 ? 0 : -1;
}

int psxServByName(const char *name, const char *proto, int *port, char *protoOut, int protoCap)
{
    if (!name || !port)
        return -1;

    struct servent *entry = ::getservbyname(name, proto);
    if (!entry)
        return -1;

    *port = ntohs((unsigned short)entry->s_port);
    if (protoOut && protoCap > 0)
    {
        protoOut[0] = 0;
        if (entry->s_proto)
            strncpy(protoOut, entry->s_proto, (size_t)protoCap - 1);
    }
    return 0;
}

int psxServByPort(int port, const char *proto, char *name, int capacity)
{
    if (!name || capacity <= 0)
        return -1;

    struct servent *entry = ::getservbyport(htons((unsigned short)port), proto);
    if (!entry || !entry->s_name)
        return -1;

    strncpy(name, entry->s_name, (size_t)capacity - 1);
    name[capacity - 1] = 0;
    return 0;
}

int psxProtoByName(const char *name, int *proto)
{
    if (!name || !proto)
        return -1;

    struct protoent *entry = ::getprotobyname(name);
    if (!entry)
        return -1;

    *proto = entry->p_proto;
    return 0;
}

int psxProtoByNumber(int proto, char *name, int capacity)
{
    if (!name || capacity <= 0)
        return -1;

    struct protoent *entry = ::getprotobynumber(proto);
    if (!entry || !entry->p_name)
        return -1;

    strncpy(name, entry->p_name, (size_t)capacity - 1);
    name[capacity - 1] = 0;
    return 0;
}

} // namespace swinx
