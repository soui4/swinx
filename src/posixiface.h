#ifndef _SWINX_POSIX_IFACE_H_
#define _SWINX_POSIX_IFACE_H_

/* 网络接口采集的**平台中立**结果类型。
 *
 * 为什么单独一层：Win32 的 <winsock2.h> 与 POSIX 的 <netinet/in.h> 都会定义
 * `struct sockaddr`，同一个 TU 里同时 include 必然重定义。所以
 *   - src/platform/posix/iface.cpp    只 include POSIX 头，产出 IfaceInfo
 *   - src/platform/posix/iphlpapi.cpp 只 include Win32 头，消费 IfaceInfo
 * 两边通过这个不含任何 Win32/POSIX 类型的小结构体对接。
 */

#include <string>
#include <vector>

namespace swinx
{

enum IfaceKind
{
    IfaceOther = 0,
    IfaceLoopback,
    IfaceEthernet,
    IfaceWireless,
    IfaceTunnel,
};

struct IfaceAddress
{
    int family; /* 4 或 6 */
    unsigned char bytes[16];
    unsigned length; /* 4 / 16 */

    IfaceAddress()
        : family(0)
        , length(0)
    {
        for (int i = 0; i < 16; ++i)
            bytes[i] = 0;
    }
};

struct IfaceInfo
{
    std::string name;
    unsigned index;
    bool loopback;
    bool up;
    bool running;
    unsigned mtu;
    int kind; /* IfaceKind */
    std::vector<unsigned char> physical;
    std::vector<IfaceAddress> addresses;

    IfaceInfo()
        : index(0)
        , loopback(false)
        , up(false)
        , running(false)
        , mtu(0)
        , kind(IfaceOther)
    {
    }
};

/* 采集当前接口表（getifaddrs）。失败时清空 out 并返回 false。 */
bool collectInterfaces(std::vector<IfaceInfo> &out);

/* 把接口表压成一个可比较的指纹，用来判断"地址表变了没有"。
 * 只由 iphlpapi 的通知线程使用，放在这里是为了让测试能直接比对。 */
std::string fingerprintInterfaces(const std::vector<IfaceInfo> &ifaces);

} // namespace swinx

#endif /* _SWINX_POSIX_IFACE_H_ */
