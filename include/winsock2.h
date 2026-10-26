#include <sfr_config.h>
#include <windows.h>
#ifndef _WINSOCK2API_
#define _WINSOCK2API_

/* winsock2.h —— WS2_32 的完整实现面。
 *
 * 分三层（与 posixiface.h 同一个套路，理由也一样）：
 *   include/winsock2.h               类型、常量、函数声明（本文件）
 *   src/posixsock.h + posixsock.cpp  平台中立描述 + POSIX 实现
 *   src/platform/posix/winsock2.cpp  WS2_32 → psx* 的翻译（只 include Win32 头）
 *
 * ⚠️ 本头**不能**与 <sys/socket.h> / <netdb.h> / <arpa/inet.h> 出现在同一个
 * TU 里：`struct sockaddr`、`fd_set`、`AF_INET`（数值不同）全都重名且布局
 * 不同。这是 Win32 与 POSIX 的先天冲突，兼容层绕不过去，只能靠分层隔离
 * —— 上面的三层就是为此而设。
 *
 * ⚠️ 符号名策略：凡是 POSIX 也有同名函数的符号（socket/bind/select/
 * getaddrinfo/htons/…）一律以 `swinx_` 前缀导出，本头尾部用宏把 Win32 名字
 * 映射过去。若直接用原名导出，swinx.so 会"符号插入"劫持全进程的 libc 调用
 * （libcurl 的 getaddrinfo/select 会被引到 Win32 语义实现上，fd_set 布局又
 * 不同，必炸）；静态链接时同样会覆盖 libc。详见文末宏块的注释。
 *
 * 实现边界（不是"没做"，而是"语义说清楚了"）：
 *   - 阻塞/非阻塞套接字、流/数据报、IPv4/IPv6、select、WSAEventSelect、
 *     WSAAsyncSelect、名字解析、地址字符串化 —— 都是真实现。
 *   - WSAAsyncSelect 与 WSAEventSelect 共用同一个轮询线程：事件就绪时
 *     PostMessage(hwnd, wMsg, (WPARAM)s, MAKELONG(事件位, 错误码))，只在事件位
 *     的"有无"发生变化时投递一次（与 Winsock 的边沿语义一致）。
 *   - WSARecv/WSASend 支持非 overlapped 的同步语义；带 OVERLAPPED 的请求
 *     因为没有完成端口而返回 WSAEOPNOTSUPP。
 *   - ioctlsocket 只认 FIONBIO / FIONREAD，其余返回 WSAEINVAL。
 */

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_ /* 防止将来任何 <winsock.h> 再定义一套 SOCKET */
#endif

typedef UINT_PTR SOCKET;
typedef USHORT ADDRESS_FAMILY;

#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)

/* 地址族。数值取 SDK 的 winsock2.h / ws2def.h。 */
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_IMPLINK 3
#define AF_PUP 4
#define AF_CHAOS 5
#define AF_IPX 6
#define AF_NS 6
#define AF_ISO 7
#define AF_ECMA 8
#define AF_DATAKIT 9
#define AF_CCITT 10
#define AF_SNA 11
#define AF_DECnet 12
#define AF_DLI 13
#define AF_LAT 14
#define AF_HYLINK 15
#define AF_APPLETALK 16
#define AF_NETBIOS 17
#define AF_VOICEVIEW 18
#define AF_FIREFOX 19
#define AF_UNKNOWN1 20
#define AF_BAN 21
#define AF_MAX 22
#define AF_INET6 23

#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define PF_INET6 AF_INET6

/* 套接字类型与协议。 */
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define SOCK_RDM 4
#define SOCK_SEQPACKET 5

#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_IGMP 2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_ICMPV6 58
#define IPPROTO_RAW 255

/* WSAStartup 的版本号编码：MAKEWORD(2,2)。 */
#define WINSOCK_VERSION 0x0202

typedef struct in_addr
{
    union
    {
        struct
        {
            UCHAR s_b1;
            UCHAR s_b2;
            UCHAR s_b3;
            UCHAR s_b4;
        } S_un_b;
        struct
        {
            USHORT s_w1;
            USHORT s_w2;
        } S_un_w;
        ULONG S_addr;
    } S_un;
} IN_ADDR, *PIN_ADDR, *LPIN_ADDR;

typedef struct sockaddr
{
    ADDRESS_FAMILY sa_family;
    CHAR sa_data[14];
} SOCKADDR, *PSOCKADDR, *LPSOCKADDR;

typedef struct sockaddr_in
{
    ADDRESS_FAMILY sin_family;
    USHORT sin_port;
    IN_ADDR sin_addr;
    CHAR sin_zero[8];
} SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;

typedef struct in6_addr
{
    union
    {
        UCHAR Byte[16];
        USHORT Word[8];
    } u;
} IN6_ADDR, *PIN6_ADDR, *LPIN6_ADDR;

typedef struct sockaddr_in6
{
    ADDRESS_FAMILY sin6_family;
    USHORT sin6_port;
    ULONG sin6_flowinfo;
    IN6_ADDR sin6_addr;
    ULONG sin6_scope_id;
} SOCKADDR_IN6, *PSOCKADDR_IN6, *LPSOCKADDR_IN6;

typedef struct _SOCKET_ADDRESS
{
    LPSOCKADDR lpSockaddr;
    INT iSockaddrLength;
} SOCKET_ADDRESS, *PSOCKET_ADDRESS, *LPSOCKET_ADDRESS;

/* 足够容纳任何地址族的定长容器（SDK 里是 128 字节）。 */
typedef struct _SOCKADDR_STORAGE
{
    ADDRESS_FAMILY ss_family;
    CHAR __ss_pad1[6];
    LONGLONG __ss_align;
    CHAR __ss_pad2[112];
} SOCKADDR_STORAGE, *PSOCKADDR_STORAGE, *LPSOCKADDR_STORAGE;

typedef struct WSAData
{
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA, *LPWSADATA;

/* ---------------------------------------------------------------- 错误码 */

#ifndef WSAEINTR
#define WSAEINTR 10004L
#endif
#ifndef WSAEBADF
#define WSAEBADF 10009L
#endif
#ifndef WSAEACCES
#define WSAEACCES 10013L
#endif
#ifndef WSAEFAULT
#define WSAEFAULT 10014L
#endif
#ifndef WSAEINVAL
#define WSAEINVAL 10022L
#endif
#ifndef WSAEMFILE
#define WSAEMFILE 10024L
#endif
#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK 10035L
#endif
#ifndef WSAEINPROGRESS
#define WSAEINPROGRESS 10036L
#endif
#ifndef WSAEALREADY
#define WSAEALREADY 10037L
#endif
#ifndef WSAENOTSOCK
#define WSAENOTSOCK 10038L
#endif
#ifndef WSAEDESTADDRREQ
#define WSAEDESTADDRREQ 10039L
#endif
#ifndef WSAEMSGSIZE
#define WSAEMSGSIZE 10040L
#endif
#ifndef WSAEPROTOTYPE
#define WSAEPROTOTYPE 10041L
#endif
#ifndef WSAENOPROTOOPT
#define WSAENOPROTOOPT 10042L
#endif
#ifndef WSAEPROTONOSUPPORT
#define WSAEPROTONOSUPPORT 10043L
#endif
#ifndef WSAESOCKTNOSUPPORT
#define WSAESOCKTNOSUPPORT 10044L
#endif
#ifndef WSAEOPNOTSUPP
#define WSAEOPNOTSUPP 10045L
#endif
#ifndef WSAEPFNOSUPPORT
#define WSAEPFNOSUPPORT 10046L
#endif
#ifndef WSAEAFNOSUPPORT
#define WSAEAFNOSUPPORT 10047L
#endif
#ifndef WSAEADDRINUSE
#define WSAEADDRINUSE 10048L
#endif
#ifndef WSAEADDRNOTAVAIL
#define WSAEADDRNOTAVAIL 10049L
#endif
#ifndef WSAENETDOWN
#define WSAENETDOWN 10050L
#endif
#ifndef WSAENETUNREACH
#define WSAENETUNREACH 10051L
#endif
#ifndef WSAENETRESET
#define WSAENETRESET 10052L
#endif
#ifndef WSAECONNABORTED
#define WSAECONNABORTED 10053L
#endif
#ifndef WSAECONNRESET
#define WSAECONNRESET 10054L
#endif
#ifndef WSAENOBUFS
#define WSAENOBUFS 10055L
#endif
#ifndef WSAEISCONN
#define WSAEISCONN 10056L
#endif
#ifndef WSAENOTCONN
#define WSAENOTCONN 10057L
#endif
#ifndef WSAESHUTDOWN
#define WSAESHUTDOWN 10058L
#endif
#ifndef WSAETOOMANYREFS
#define WSAETOOMANYREFS 10059L
#endif
#ifndef WSAETIMEDOUT
#define WSAETIMEDOUT 10060L
#endif
#ifndef WSAECONNREFUSED
#define WSAECONNREFUSED 10061L
#endif
#ifndef WSAELOOP
#define WSAELOOP 10062L
#endif
#ifndef WSAENAMETOOLONG
#define WSAENAMETOOLONG 10063L
#endif
#ifndef WSAEHOSTDOWN
#define WSAEHOSTDOWN 10064L
#endif
#ifndef WSAEHOSTUNREACH
#define WSAEHOSTUNREACH 10065L
#endif
#ifndef WSAENOTEMPTY
#define WSAENOTEMPTY 10066L
#endif
#ifndef WSAEPROCLIM
#define WSAEPROCLIM 10067L
#endif
#ifndef WSAEUSERS
#define WSAEUSERS 10068L
#endif
#ifndef WSAEDQUOT
#define WSAEDQUOT 10069L
#endif
#ifndef WSAESTALE
#define WSAESTALE 10070L
#endif
#ifndef WSAEREMOTE
#define WSAEREMOTE 10071L
#endif
#ifndef WSASYSNOTREADY
#define WSASYSNOTREADY 10091L
#endif
#ifndef WSAVERNOTSUPPORTED
#define WSAVERNOTSUPPORTED 10092L
#endif
#ifndef WSANOTINITIALISED
#define WSANOTINITIALISED 10093L
#endif
#ifndef WSAEDISCON
#define WSAEDISCON 10101L
#endif
#ifndef WSAENOMORE
#define WSAENOMORE 10102L
#endif
#ifndef WSAECANCELLED
#define WSAECANCELLED 10103L
#endif
#ifndef WSAEINVALIDPROCTABLE
#define WSAEINVALIDPROCTABLE 10104L
#endif
#ifndef WSAEINVALIDPROVIDER
#define WSAEINVALIDPROVIDER 10105L
#endif
#ifndef WSAEPROVIDERFAILEDINIT
#define WSAEPROVIDERFAILEDINIT 10106L
#endif
#ifndef WSASYSCALLFAILURE
#define WSASYSCALLFAILURE 10107L
#endif
#ifndef WSASERVICE_NOT_FOUND
#define WSASERVICE_NOT_FOUND 10108L
#endif
#ifndef WSATYPE_NOT_FOUND
#define WSATYPE_NOT_FOUND 10109L
#endif
#ifndef WSAHOST_NOT_FOUND
#define WSAHOST_NOT_FOUND 11001L
#endif
#ifndef WSATRY_AGAIN
#define WSATRY_AGAIN 11002L
#endif
#ifndef WSANO_RECOVERY
#define WSANO_RECOVERY 11003L
#endif
#ifndef WSANO_DATA
#define WSANO_DATA 11004L
#endif

/* -------------------------------------------------------- fd_set / timeval */

/* glibc 的 <sys/select.h> 经 windows.h → strapi.h → stdlib.h → sys/types.h
 * 传递包含，在 Linux 的任何 TU 里都先于本头出现，并把 fd_set 定义成位图
 * typedef —— C++ 不允许同名 typedef 指向不同类型。所以本头的真名是
 * swinx_fd_set，定义完立即把 fd_set 一词重定向过去（与文件尾的函数改名
 * 宏同一策略）。 */
#ifdef FD_SETSIZE
#undef FD_SETSIZE
#endif
#define FD_SETSIZE 64

/* Win32 的 fd_set 是「句柄数组 + 计数」，POSIX 是位图 —— 这也是两边不能
 * 共存的直接原因之一。布局按 SDK。 */
typedef struct swinx_fd_set
{
    UINT fd_count;
    SOCKET fd_array[FD_SETSIZE];
} swinx_fd_set, FD_SET, *PFD_SET, *LPFD_SET;

#define fd_set swinx_fd_set

#ifdef FD_CLR
#undef FD_CLR
#endif
#ifdef FD_SET
#undef FD_SET
#endif
#ifdef FD_ZERO
#undef FD_ZERO
#endif
#ifdef FD_ISSET
#undef FD_ISSET
#endif

#define FD_CLR(fd, set)                                                                          \
    do                                                                                           \
    {                                                                                            \
        UINT __i;                                                                                \
        for (__i = 0; __i < ((fd_set *)(set))->fd_count; ++__i)                                   \
        {                                                                                        \
            if (((fd_set *)(set))->fd_array[__i] == (SOCKET)(fd))                                 \
            {                                                                                    \
                while (__i + 1 < ((fd_set *)(set))->fd_count)                                      \
                {                                                                                \
                    ((fd_set *)(set))->fd_array[__i] = ((fd_set *)(set))->fd_array[__i + 1];       \
                    ++__i;                                                                         \
                }                                                                                \
                --((fd_set *)(set))->fd_count;                                                     \
                break;                                                                             \
            }                                                                                      \
        }                                                                                          \
    } while (0)

#define FD_SET(fd, set)                                                                            \
    do                                                                                             \
    {                                                                                              \
        UINT __i;                                                                                  \
        for (__i = 0; __i < ((fd_set *)(set))->fd_count; ++__i)                                     \
        {                                                                                          \
            if (((fd_set *)(set))->fd_array[__i] == (SOCKET)(fd))                                   \
                break;                                                                             \
        }                                                                                          \
        if (__i == ((fd_set *)(set))->fd_count && __i < FD_SETSIZE)                                 \
            ((fd_set *)(set))->fd_array[((fd_set *)(set))->fd_count++] = (SOCKET)(fd);              \
    } while (0)

#define FD_ZERO(set) (((fd_set *)(set))->fd_count = 0)
#define FD_ISSET(fd, set) __WSAFDIsSet((SOCKET)(fd), (fd_set *)(set))

/* Win32 的 timeval 用 LONG 计秒/微秒。POSIX 的 <sys/time.h> 已经有同名结构
 * （tv_sec 是 time_t），两者都出现在同一个 TU 时只能留一个 —— 各家的 guard
 * 宏不同，这里把三个都认上。 */
#if !defined(_TIMEVAL_DEFINED) && !defined(_STRUCT_TIMEVAL) && !defined(__timeval_defined)
#define _TIMEVAL_DEFINED 1
struct timeval
{
    LONG tv_sec;
    LONG tv_usec;
};
#endif

/* -------------------------------------------------------------- 选项与命令 */

#define SOL_SOCKET 0xFFFF

#define SO_DEBUG 0x0001
#define SO_ACCEPTCONN 0x0002
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_DONTROUTE 0x0010
#define SO_BROADCAST 0x0020
#define SO_USELOOPBACK 0x0040
#define SO_LINGER 0x0080
#define SO_OOBINLINE 0x0100
#define SO_DONTLINGER (int)(~SO_LINGER)
#define SO_EXCLUSIVEADDRUSE ((int)(~SO_REUSEADDR))
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDLOWAT 0x1003
#define SO_RCVLOWAT 0x1004
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define SO_TYPE 0x1008
#define SO_PROTOCOL_INFO 0x2003

#define TCP_NODELAY 0x0001
#define TCP_MAXSEG 0x0004

#define IP_TOS 1
#define IP_TTL 2
#define IP_MULTICAST_IF 9
#define IP_MULTICAST_TTL 10
#define IP_MULTICAST_LOOP 11
#define IP_ADD_MEMBERSHIP 12
#define IP_DROP_MEMBERSHIP 13

#define MSG_OOB 0x1
#define MSG_PEEK 0x2
#define MSG_DONTROUTE 0x4
#define MSG_INTERRUPT 0x10
#define MSG_PARTIAL 0x8000

#define FIONREAD 0x4004667F
#define FIONBIO 0x8004667E

#define SD_RECEIVE 0x00
#define SD_SEND 0x01
#define SD_BOTH 0x02

#define INADDR_ANY ((ULONG)0x00000000)
#define INADDR_LOOPBACK ((ULONG)0x7f000001)
#define INADDR_BROADCAST ((ULONG)0xffffffff)
#define INADDR_NONE ((ULONG)0xffffffff)

#define INET_ADDRSTRLEN 22
#define INET6_ADDRSTRLEN 65

/* WSAEventSelect 的事件位。 */
#define FD_READ_BIT 0
#define FD_WRITE_BIT 1
#define FD_OOB_BIT 2
#define FD_ACCEPT_BIT 3
#define FD_CONNECT_BIT 4
#define FD_CLOSE_BIT 5
#define FD_QOS_BIT 6
#define FD_GROUP_QOS_BIT 7
#define FD_ROUTING_INTERFACE_CHANGE_BIT 8
#define FD_ADDRESS_LIST_CHANGE_BIT 9
#define FD_MAX_EVENTS 10

#define FD_READ (1 << FD_READ_BIT)
#define FD_WRITE (1 << FD_WRITE_BIT)
#define FD_OOB (1 << FD_OOB_BIT)
#define FD_ACCEPT (1 << FD_ACCEPT_BIT)
#define FD_CONNECT (1 << FD_CONNECT_BIT)
#define FD_CLOSE (1 << FD_CLOSE_BIT)
#define FD_QOS (1 << FD_QOS_BIT)
#define FD_GROUP_QOS (1 << FD_GROUP_QOS_BIT)
#define FD_ROUTING_INTERFACE_CHANGE (1 << FD_ROUTING_INTERFACE_CHANGE_BIT)
#define FD_ADDRESS_LIST_CHANGE (1 << FD_ADDRESS_LIST_CHANGE_BIT)
#define FD_ALL_EVENTS 0x3FF

#define WSA_WAIT_EVENT_0 0
#define WSA_WAIT_TIMEOUT 258
#define WSA_WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define WSA_INFINITE ((DWORD)0xFFFFFFFF)

/* getaddrinfo 的 flags 与 getnameinfo 的 flags。 */
#define AI_PASSIVE 0x00000001
#define AI_CANONNAME 0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00000008
#define AI_ALL 0x00000100
#define AI_ADDRCONFIG 0x00000400
#define AI_V4MAPPED 0x00000800

#define NI_NOFQDN 0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD 0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM 0x10

#define WSA_FLAG_OVERLAPPED 0x01
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80

/* ---------------------------------------------------------------- 结构 */

typedef struct hostent
{
    char *h_name;
    char **h_aliases;
    short h_addrtype;
    short h_length;
    char **h_addr_list;
} HOSTENT, *PHOSTENT, *LPHOSTENT;

typedef struct servent
{
    char *s_name;
    char **s_aliases;
    short s_port;
    char *s_proto;
} SERVENT, *PSERVENT, *LPSERVENT;

typedef struct protoent
{
    char *p_name;
    char **p_aliases;
    short p_proto;
} PROTOENT, *PPROTOENT, *LPPROTOENT;

typedef struct addrinfo
{
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
} ADDRINFOA, *PADDRINFOA;

typedef struct addrinfoW
{
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    wchar_t *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfoW *ai_next;
} ADDRINFOW, *PADDRINFOW;

typedef struct _WSABUF
{
    ULONG len;
    CHAR *buf;
} WSABUF, *LPWSABUF;

typedef struct _WSANETWORKEVENTS
{
    LONG lNetworkEvents;
    int iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

typedef struct pollfd
{
    SOCKET fd;
    SHORT events;
    SHORT revents;
} WSAPOLLFD, *LPWSAPOLLFD;

/* WSAPoll 的事件位。与 SDK 的 winsock2.h 一致。 */
#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN (POLLRDNORM | POLLRDBAND)
#define POLLPRI 0x0400
#define POLLWRNORM 0x0010
#define POLLOUT (POLLWRNORM)
#define POLLWRBAND 0x0020
#define POLLERR 0x0001
#define POLLHUP 0x0002
#define POLLNVAL 0x0004

typedef struct linger
{
    USHORT l_onoff;
    USHORT l_linger;
} LINGER, *PLINGER, *LPLINGER;

typedef HANDLE WSAEVENT, *LPWSAEVENT;

/* WS2_32 里 WSAOVERLAPPED 就是 OVERLAPPED 的别名（定义在 fileapi.h）。 */
typedef LPOVERLAPPED LPWSAOVERLAPPED;

typedef int(WINAPI *LPCONDITIONPROC)(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPWSABUF lpSQOS,
                                     LPWSABUF lpGQOS, LPWSABUF lpCalleeId, LPWSABUF lpCalleeData,
                                     ULONG_PTR *g, DWORD_PTR dwCallbackData);

/* ---------------------------------------------------------------- 函数 */

#ifdef __cplusplus
extern "C" {
#endif

int WINAPI WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WINAPI WSACleanup(void);
int WINAPI WSAGetLastError(void);
void WINAPI WSASetLastError(int iError);

SOCKET WINAPI swinx_socket(int af, int type, int protocol);
SOCKET WINAPI WSASocketA(int af, int type, int protocol, void *lpProtocolInfo, UINT g, DWORD dwFlags);
SOCKET WINAPI WSASocketW(int af, int type, int protocol, void *lpProtocolInfo, UINT g, DWORD dwFlags);
int WINAPI closesocket(SOCKET s);
int WINAPI swinx_bind(SOCKET s, const struct sockaddr *name, int namelen);
int WINAPI swinx_connect(SOCKET s, const struct sockaddr *name, int namelen);
int WINAPI swinx_listen(SOCKET s, int backlog);
SOCKET WINAPI swinx_accept(SOCKET s, struct sockaddr *addr, int *addrlen);
SOCKET WINAPI WSAAccept(SOCKET s, struct sockaddr *addr, int *addrlen, LPCONDITIONPROC lpfnCondition,
                        DWORD_PTR dwCallbackData);
int WINAPI swinx_shutdown(SOCKET s, int how);

int WINAPI swinx_send(SOCKET s, const char *buf, int len, int flags);
int WINAPI swinx_recv(SOCKET s, char *buf, int len, int flags);
int WINAPI swinx_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen);
int WINAPI swinx_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);

int WINAPI WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                   DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine);
int WINAPI WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                   LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine);
int WINAPI WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                     DWORD dwFlags, const struct sockaddr *lpTo, int iToLen, LPWSAOVERLAPPED lpOverlapped,
                     void *lpCompletionRoutine);
int WINAPI WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                       LPDWORD lpFlags, struct sockaddr *lpFrom, LPINT lpFromlen,
                       LPWSAOVERLAPPED lpOverlapped, void *lpCompletionRoutine);

int WINAPI swinx_getsockname(SOCKET s, struct sockaddr *name, int *namelen);
int WINAPI swinx_getpeername(SOCKET s, struct sockaddr *name, int *namelen);
int WINAPI swinx_getsockopt(SOCKET s, int level, int optname, char *optval, int *optlen);
int WINAPI swinx_setsockopt(SOCKET s, int level, int optname, const char *optval, int optlen);
int WINAPI ioctlsocket(SOCKET s, long cmd, ULONG *argp);

int WINAPI swinx_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                        const struct timeval *timeout);
int WINAPI __WSAFDIsSet(SOCKET s, fd_set *set);

WSAEVENT WINAPI WSACreateEvent(void);
BOOL WINAPI WSACloseEvent(WSAEVENT hEvent);
BOOL WINAPI WSASetEvent(WSAEVENT hEvent);
BOOL WINAPI WSAResetEvent(WSAEVENT hEvent);
DWORD WINAPI WSAWaitForMultipleEvents(DWORD cEvents, const WSAEVENT *lphEvents, BOOL fWaitAll,
                                      DWORD dwTimeout, BOOL fAlertable);
int WINAPI WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents);
int WINAPI WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents);
int WINAPI WSAAsyncSelect(SOCKET s, HWND hWnd, unsigned int wMsg, long lEvent);
int WINAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, int timeout);

int WINAPI WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, void *lpProtocolInfo,
                               LPSTR lpszAddressString, LPDWORD lpdwAddressStringLength);
int WINAPI WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, void *lpProtocolInfo,
                               LPWSTR lpszAddressString, LPDWORD lpdwAddressStringLength);
int WINAPI WSAStringToAddressA(LPSTR AddressString, int AddressFamily, void *lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength);
int WINAPI WSAStringToAddressW(LPWSTR AddressString, int AddressFamily, void *lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength);

int WINAPI swinx_getaddrinfo(const char *pNodeName, const char *pServiceName, const struct addrinfo *pHints,
                             struct addrinfo **ppResult);
void WINAPI swinx_freeaddrinfo(struct addrinfo *pAddrInfo);
int WINAPI swinx_getnameinfo(const struct sockaddr *pSockaddr, int SockaddrLength, char *pNodeBuffer,
                             DWORD NodeBufferSize, char *pServiceBuffer, DWORD ServiceBufferSize, int Flags);
int WINAPI GetAddrInfoA(const char *pNodeName, const char *pServiceName, const struct addrinfo *pHints,
                        PADDRINFOA *ppResult);
int WINAPI GetAddrInfoW(const wchar_t *pNodeName, const wchar_t *pServiceName, const ADDRINFOW *pHints,
                        PADDRINFOW *ppResult);
void WINAPI FreeAddrInfoA(PADDRINFOA pAddrInfo);
void WINAPI FreeAddrInfoW(PADDRINFOW pAddrInfo);
int WINAPI GetNameInfoA(const struct sockaddr *pSockaddr, int SockaddrLength, char *pNodeBuffer,
                        DWORD NodeBufferSize, char *pServiceBuffer, DWORD ServiceBufferSize, int Flags);
int WINAPI GetNameInfoW(const struct sockaddr *pSockaddr, int SockaddrLength, wchar_t *pNodeBuffer,
                        DWORD NodeBufferSize, wchar_t *pServiceBuffer, DWORD ServiceBufferSize, int Flags);

struct hostent *WINAPI swinx_gethostbyname(const char *name);
struct hostent *WINAPI swinx_gethostbyaddr(const char *addr, int len, int type);
int WINAPI swinx_gethostname(char *name, int namelen);
struct servent *WINAPI swinx_getservbyname(const char *name, const char *proto);
struct servent *WINAPI swinx_getservbyport(int port, const char *proto);
struct protoent *WINAPI swinx_getprotobyname(const char *name);
struct protoent *WINAPI swinx_getprotobynumber(int proto);

/* 与 libc 同名的转换函数同样改名导出；swinx_inet_addr/inet_ntop 等**不加**
 * weak —— 符号已经不与 libc 重名，weak 只会让真实的实现被 libc 意外抢占。 */
ULONG swinx_inet_addr(const char *cp);
char *swinx_inet_ntoa(struct in_addr in);
const char *swinx_inet_ntop(int Family, const void *pAddr, char *pStringBuf, size_t StringBufSize);
int swinx_inet_pton(int Family, const char *pszAddrString, void *pAddrBuf);
int InetPtonA(int Family, const char *pszAddrString, void *pAddrBuf);
int InetPtonW(int Family, const wchar_t *pszAddrString, void *pAddrBuf);
const char *InetNtopA(int Family, const void *pAddr, char *pStringBuf, size_t StringBufSize);
const wchar_t *InetNtopW(int Family, const void *pAddr, wchar_t *pStringBuf, size_t StringBufSize);

unsigned short swinx_htons(unsigned short hostshort);
unsigned short swinx_ntohs(unsigned short netshort);
ULONG swinx_htonl(ULONG hostlong);
ULONG swinx_ntohl(ULONG netlong);

/* ---------------------------------------------------------------- 源码兼容宏
 *
 * 上面凡是 POSIX 里也有同名函数的符号（socket/bind/select/getaddrinfo/
 * htons/…），在 ELF 里都用 `swinx_` 前缀的名字导出。原因：swinx.so 一旦导出
 * `select`/`getaddrinfo` 这类名字，动态装载后就形成**符号插入**——全进程
 * （包括 libcurl、libc 自己的调用方）都会解析到 swinx 的 Win32 语义实现，
 * 而 Win32 的 fd_set（句柄数组）与 POSIX 的 fd_set（位图）布局完全不同，
 * 结果是灾难性的；静态链接时同理，这些定义会整体覆盖 libc。
 *
 * Win32 源码兼容由这组宏承担：include 本头的 TU 写 `socket(...)` 会展开成
 * `swinx_socket(...)`，链接到 swinx 的实现；没 include 本头的代码（libcurl
 * 等）继续用 libc 的同名函数，互不干扰。
 *
 * 注意：宏会展开后文里所有同名 token（包括成员名），include 本头之后不要
 * 再声明/定义以这些名字命名的结构成员或函数。 */
#define socket          swinx_socket
#define bind            swinx_bind
#define listen          swinx_listen
#define accept          swinx_accept
#define connect         swinx_connect
#define shutdown        swinx_shutdown
#define send            swinx_send
#define recv            swinx_recv
#define sendto          swinx_sendto
#define recvfrom        swinx_recvfrom
#define getsockname     swinx_getsockname
#define getpeername     swinx_getpeername
#define getsockopt      swinx_getsockopt
#define setsockopt      swinx_setsockopt
#define select          swinx_select
#define getaddrinfo     swinx_getaddrinfo
#define freeaddrinfo    swinx_freeaddrinfo
#define getnameinfo     swinx_getnameinfo
#define gethostbyname   swinx_gethostbyname
#define gethostbyaddr   swinx_gethostbyaddr
#define gethostname     swinx_gethostname
#define getservbyname   swinx_getservbyname
#define getservbyport   swinx_getservbyport
#define getprotobyname  swinx_getprotobyname
#define getprotobynumber swinx_getprotobynumber
#define inet_addr       swinx_inet_addr
#define inet_ntoa       swinx_inet_ntoa
#define inet_ntop       swinx_inet_ntop
#define inet_pton       swinx_inet_pton
#define htons           swinx_htons
#define ntohs           swinx_ntohs
#define htonl           swinx_htonl
#define ntohl           swinx_ntohl

#ifdef __cplusplus
}
#endif

#endif /* _WINSOCK2API_ */
