/* 网络接口采集（POSIX 侧）。
 *
 * 只 include POSIX 头：Win32 的 <winsock2.h> 与本文件用到的 <netinet/in.h>
 * 都会定义 `struct sockaddr`，同一个 TU 里同时 include 会重定义。因此这里
 * 产出平台中立的 swinx::IfaceInfo，由 src/platform/posix/iphlpapi.cpp 转成
 * IP_ADAPTER_ADDRESSES。详见 src/posixiface.h 的说明。
 */

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "posixiface.h"
#include "debug.h"
#define kLogTag "iface"

#if defined(__linux__)
#include <netpacket/packet.h>
/* SIOCGIFMTU 在 glibc 的 <net/if.h> / <sys/ioctl.h> 里都没有，只在 linux/sockios.h */
#include <linux/sockios.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <net/if_dl.h>
#endif

namespace swinx
{

namespace
{

unsigned queryMtu(const char *name)
{
    // 没有 SIOCGIFMTU 就没 MTU 可报。用临时 SOCK_DGRAM 套接字问一次，
    // 比读 /sys/class/net/<name>/mtu 更跨平台（macOS 没有 sysfs）。
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return 0;

    struct ifreq req;
    memset(&req, 0, sizeof(req));
    strncpy(req.ifr_name, name, IFNAMSIZ - 1);

    unsigned mtu = 0;
    if (ioctl(s, SIOCGIFMTU, &req) == 0)
        mtu = (unsigned)req.ifr_mtu;
    close(s);
    return mtu;
}

int classify(const IfaceInfo &info)
{
    if (info.loopback)
        return IfaceLoopback;

    // 名字前缀是唯一在没有额外系统接口的情况下能拿到的线索；无线/隧道
    // 的判定本来就要问驱动（sysfs / SIOCGIFMEDIA），这里只认最常见的
    // 隧道命名，其余按以太处理 —— WebKit 只用 IfType 区分 loopback，
    // 猜错不会影响它的在线判定。
    if (info.physical.empty() &&
        (info.name.compare(0, 3, "tun") == 0 || info.name.compare(0, 4, "utun") == 0 ||
         info.name.compare(0, 3, "ppp") == 0))
        return IfaceTunnel;

    return IfaceEthernet;
}

void appendAddress(IfaceInfo &info, const struct sockaddr *addr)
{
    if (!addr)
        return;

    IfaceAddress out;
    if (addr->sa_family == AF_INET)
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
        out.family = 4;
        out.length = 4;
        memcpy(out.bytes, &in->sin_addr, 4);
    }
    else if (addr->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)addr;
        out.family = 6;
        out.length = 16;
        memcpy(out.bytes, &in6->sin6_addr, 16);
    }
    else
    {
        return;
    }
    info.addresses.push_back(out);
}

/* 链路层地址（MAC）。两个平台的取法完全不同：
 *   Linux : AF_PACKET + sockaddr_ll
 *   *BSD  : AF_LINK   + sockaddr_dl（LLADDR 从 sdl_data 的 sdl_nlen 之后开始）
 */
bool appendPhysical(IfaceInfo &info, const struct sockaddr *addr)
{
#if defined(__linux__)
    if (addr->sa_family != AF_PACKET)
        return false;
    const struct sockaddr_ll *ll = (const struct sockaddr_ll *)addr;
    if (ll->sll_halen == 0)
        return false;
    info.physical.assign(ll->sll_addr, ll->sll_addr + (ll->sll_halen > 8 ? 8 : ll->sll_halen));
    return true;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    if (addr->sa_family != AF_LINK)
        return false;
    const struct sockaddr_dl *dl = (const struct sockaddr_dl *)addr;
    if (dl->sdl_alen == 0)
        return false;
    const unsigned char *mac = (const unsigned char *)LLADDR(dl);
    info.physical.assign(mac, mac + (dl->sdl_alen > 8 ? 8 : dl->sdl_alen));
    return true;
#else
    (void)info;
    (void)addr;
    return false;
#endif
}

} // namespace

bool collectInterfaces(std::vector<IfaceInfo> &out)
{
    out.clear();

    struct ifaddrs *list = 0;
    if (getifaddrs(&list) != 0)
    {
        SLOG_FMTW("getifaddrs failed: %d", errno);
        return false;
    }

    // getifaddrs 对同一个接口会给出多条记录（每条地址一条），这里按名字归并，
    // 同时保留首次出现的顺序 —— 顺序稳定的指纹才好比较。
    std::vector<std::string> order;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_name)
            continue;

        std::string name(ifa->ifa_name);
        size_t index = order.size();
        for (size_t i = 0; i < order.size(); ++i)
        {
            if (order[i] == name)
            {
                index = i;
                break;
            }
        }

        if (index == order.size())
        {
            order.push_back(name);

            IfaceInfo info;
            info.name = name;
            info.index = if_nametoindex(ifa->ifa_name);
            info.up = (ifa->ifa_flags & IFF_UP) != 0;
            info.running = (ifa->ifa_flags & IFF_RUNNING) != 0;
            info.loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
            info.mtu = queryMtu(ifa->ifa_name);
            out.push_back(info);
        }

        IfaceInfo &info = out[index];
        if (!ifa->ifa_addr)
            continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET || family == AF_INET6)
            appendAddress(info, ifa->ifa_addr);
        else
            appendPhysical(info, ifa->ifa_addr);
    }

    freeifaddrs(list);

    // kind 依赖 physical，必须等所有记录都归并完再算。
    for (size_t i = 0; i < out.size(); ++i)
        out[i].kind = classify(out[i]);

    return true;
}

} // namespace swinx
