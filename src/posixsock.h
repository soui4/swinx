#ifndef _SWINX_POSIX_SOCK_H_
#define _SWINX_POSIX_SOCK_H_

/* 套接字操作的**平台中立**描述层。
 *
 * 存在的理由和 posixiface.h 完全一样，只是更大：Win32 的 <winsock2.h> 与
 * POSIX 的 <sys/socket.h> 在同一个 TU 里不能共存 —— `struct sockaddr`、
 * `fd_set`、`select`、`AF_INET`（数值不同）全是重名。所以切成三层：
 *
 *   src/posixsock.h             只有 POD 与整数（本文件，谁都能 include）
 *   src/platform/posix/posixsock.cpp  只 include POSIX 头，实现 psx*
 *   src/platform/posix/winsock2.cpp   只 include Win32 头，做 WS2_32 → psx* 的翻译
 *
 * 地址一律写成「族 + 端口(主机序) + 16 字节地址 + scope」，不出现 sockaddr；
 * 套接字句柄就是 POSIX fd 的整数值（Win32 那边把它塞进 SOCKET，无效值 -1
 * 恰好等于 INVALID_SOCKET 的 ~0）。
 *
 * 错误一律用「返回 -1 + 写出 POSIX errno」表达。把 errno 翻译成 WSAE* 是
 * winsock2.cpp 的职责 —— errno 是平台细节，不该渗到中立的这一层来。
 */

namespace swinx
{

/* 与 Win32 的 AF_* 数值一致，这样 winsock2.cpp 可以直接透传。 */
enum PsxFamily
{
    PsxAfUnspec = 0,
    PsxAfInet = 2,
    PsxAfInet6 = 23,
};

struct PsxAddr
{
    int family;
    unsigned short port; /* 主机序 */
    unsigned char bytes[16];
    unsigned int scope;
};

struct PsxAddrInfo
{
    int family;
    int sockType;
    int protocol;
    int flags; /* PsxAiCanonName 等位，见下 */
    PsxAddr addr;
    char canonName[256];
};

/* psxResolve 的 flags 位。与 Win32 的 AI_* 一一对应，但数值独立，
 * 免得 winsock2.cpp 把两个平台的常量搞混。 */
enum PsxAiFlag
{
    PsxAiCanonName = 1,
    PsxAiPassive = 2,
    PsxAiNumericHost = 4,
    PsxAiNumericService = 8,
};

/* gethostbyname / gethostbyaddr 的结果。地址按族分别是 4 / 16 字节。 */
struct PsxHostEnt
{
    int family;
    int addrLength;
    int addrCount;
    unsigned char addresses[8][16];
    char name[256];
    char aliases[4][256];
};

struct PsxFdSet
{
    int count;
    int fds[64];
};

/* 套接字的可读/可写/异常状态。位定义供 WSAEnumNetworkEvents 换算成 FD_*。
 * Hangup 单独一位：对端关闭与"有数据可读"在 poll 里是两回事，Win32 那边
 * 对应 FD_CLOSE 而不是 FD_READ。 */
enum PsxReady
{
    PsxReadyRead = 1,
    PsxReadyWrite = 2,
    PsxReadyExcept = 4,
    PsxReadyHangup = 8,
};

/* 供 getsockopt / setsockopt 用的中立选项编号。level 用 PsxOptLevel。 */
enum PsxOptLevel
{
    PsxLevelSocket = 1,
    PsxLevelTcp = 2,
    PsxLevelIp = 3,
};

enum PsxSockOpt
{
    PsxOptReuseAddr = 1,
    PsxOptKeepAlive,
    PsxOptRecvBuf,
    PsxOptSendBuf,
    PsxOptLinger,
    PsxOptRecvTimeout,
    PsxOptSendTimeout,
    PsxOptBroadcast,
    PsxOptDontRoute,
    PsxOptError,
    PsxOptType,
    PsxOptNoDelay,
    PsxOptTtl,
    PsxOptMulticastTtl,
    PsxOptMulticastLoop,
    PsxOptExclusiveAddrUse,
};

/* SO_LINGER 的 POSIX 布局（两个 int）。Win32 的两个 u_short 在 winsock2.cpp 里转换。 */
struct PsxLinger
{
    int onOff;
    int seconds;
};

/* ---------------------------------------------------------------- 套接字 */

int psxSocket(int family, int type, int protocol, int *err);
int psxBind(int fd, const PsxAddr *addr, int *err);
int psxConnect(int fd, const PsxAddr *addr, int *err);
int psxListen(int fd, int backlog, int *err);
/* 返回新 fd；*peer 可以传 0。 */
int psxAccept(int fd, PsxAddr *peer, int *err);

long psxSend(int fd, const void *buf, unsigned long len, int flags, int *err);
long psxRecv(int fd, void *buf, unsigned long len, int flags, int *err);
long psxSendTo(int fd, const void *buf, unsigned long len, int flags, const PsxAddr *to, int *err);
long psxRecvFrom(int fd, void *buf, unsigned long len, int flags, PsxAddr *from, int *err);

int psxShutdown(int fd, int how, int *err); /* how: 0=读 1=写 2=双向 */
int psxClose(int fd, int *err);

int psxGetSockName(int fd, PsxAddr *out, int *err);
int psxGetPeerName(int fd, PsxAddr *out, int *err);

int psxSetSockOpt(int fd, int level, int option, const void *value, int len, int *err);
int psxGetSockOpt(int fd, int level, int option, void *value, int *len, int *err);

/* ioctlsocket 支持的两个命令。用枚举而不是裸数字，免得 winsock2.cpp 与
 * posixsock.cpp 各记一套 magic number。 */
enum PsxIoctl
{
    PsxIoctlBlocking = 1, /* arg: 0 = 阻塞，非 0 = 非阻塞 */
    PsxIoctlReadable = 2, /* arg: 写出可读字节数 */
};

/* cmd: PsxIoctlBlocking / PsxIoctlReadable。 */
int psxIoctl(int fd, int cmd, unsigned long *arg, int *err);

/* select 语义，但用 poll 实现：没有 FD_SETSIZE 上限，也不用自己算 nfds。
 * 就绪的 fd 会被写回各自的集合（count 更新为就绪个数）。
 * timeoutUsec < 0 表示无限等待。返回 0 表示超时，-1 表示出错。 */
int psxSelect(PsxFdSet *readSet, PsxFdSet *writeSet, PsxFdSet *exceptSet, long timeoutUsec, int *err);

/* 单个 fd 的就绪位（PsxReady 的掩码），0 超时。 */
int psxPollOne(int fd, int *err);

/* ------------------------------------------------------------ 名字与地址 */

/* 解析主机名/服务名。成功时 *outList 用 free() 释放。
 * node 为 0 表示"本机任意地址"（AI_PASSIVE 语义由 flags 控制）。 */
int psxResolve(const char *node, const char *service, int family, int sockType, int protocol, int flags,
               PsxAddrInfo **outList, int *outCount, int *err);

int psxLookupHost(const char *name, PsxHostEnt *out, int *err);
int psxLookupAddr(const unsigned char *bytes, int len, int family, PsxHostEnt *out, int *err);
/* 返回本机主机名到 out（含结尾 0）。 */
int psxHostName(char *out, int capacity, int *err);

int psxNtop(int family, const unsigned char *bytes, char *dst, int capacity);
int psxPton(int family, const char *src, unsigned char *bytes);

/* 服务/协议名查询。成功时 s_port 为主机序。 */
int psxServByName(const char *name, const char *proto, int *port, char *protoOut, int protoCap);
int psxServByPort(int port, const char *proto, char *name, int capacity);
int psxProtoByName(const char *name, int *proto);
int psxProtoByNumber(int proto, char *name, int capacity);

} // namespace swinx

#endif /* _SWINX_POSIX_SOCK_H_ */
