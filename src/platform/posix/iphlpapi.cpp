/* iphlpapi 的 POSIX 实现：接口枚举 + 地址变化通知。
 *
 * 本文件只 include Win32 头；真正碰 POSIX 的是同目录的 iface.cpp（它产出
 * 平台中立的 swinx::IfaceInfo）。见 src/posixiface.h 里关于 `struct sockaddr`
 * 重定义的说明。
 *
 * 两处刻意的语义选择：
 *
 * 1. GetAdaptersAddresses 严格按 Win32 的两段式契约：第一次传 NULL 返回
 *    ERROR_BUFFER_OVERFLOW 并给出所需字节数；缓冲不够同样是 OVERFLOW。
 *    Windows 上的调用方（包括 WebKit）都是照这个契约写的，返回别的码会让
 *    它走进"探测失败，假定在线"的分支，永远测不出离线。
 *
 * 2. 字符串字段（Description/FriendlyName）在 Win32 上是 UTF-16；swinx 的
 *    WCHAR 在 POSIX 上是 4 字节，所以这里用 towstring() 生成**本平台宽度**的
 *    宽字符串。让调用方按 swinx 的 WCHAR 去读是自洽的；绝不能把 UTF-16 字节
 *    原样塞进去（每个字符后会多出两个 0）。
 */

#include <windows.h>
#include <iphlpapi.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

#include "posixiface.h"
#include "tostring.h"
#include "debug.h"
#define kLogTag "iphlpapi"

namespace
{

const size_t kAlign = sizeof(void *);

size_t alignUp(size_t v)
{
    return (v + kAlign - 1) & ~(kAlign - 1);
}

/* ------------------------------------------------------------------ 描述层 */

struct UnicastDesc
{
    int family; /* 4 / 6 */
    unsigned char bytes[16];
    unsigned length;
};

struct AdapterDesc
{
    std::string name;     /* AdapterName（ANSI） */
    std::wstring display; /* Description / FriendlyName */
    DWORD index;
    ULONG mtu;
    IFTYPE type;
    IF_OPER_STATUS status;
    std::vector<unsigned char> physical;
    std::vector<UnicastDesc> unicast;
};

IFTYPE toIfType(int kind)
{
    switch (kind)
    {
    case swinx::IfaceLoopback:
        return MIB_IF_TYPE_LOOPBACK;
    case swinx::IfaceWireless:
        return MIB_IF_TYPE_IEEE80211;
    case swinx::IfaceTunnel:
        return MIB_IF_TYPE_TUNNEL;
    case swinx::IfaceEthernet:
        return MIB_IF_TYPE_ETHERNET;
    default:
        return MIB_IF_TYPE_OTHER;
    }
}

/* 接口是否"工作正常"。IFF_UP 只说明管理上启用了，还要载波（IFF_RUNNING）
 * 才算 up —— 拔掉网线的网卡 IFF_UP 仍然是 1。loopback / tunnel 没有载波
 * 概念，它们靠 IFF_UP 判断。 */
bool operStatusIsUp(const swinx::IfaceInfo &info)
{
    if (!info.up)
        return false;
    if (info.physical.empty())
        return true;
    return info.running;
}

void buildDescriptors(const std::vector<swinx::IfaceInfo> &ifaces, ULONG family, ULONG flags,
                      std::vector<AdapterDesc> &out)
{
    out.clear();

    for (size_t i = 0; i < ifaces.size(); ++i)
    {
        const swinx::IfaceInfo &info = ifaces[i];

        AdapterDesc desc;
        desc.name = info.name;
        if (!towstring(info.name.c_str(), -1, desc.display))
            desc.display.clear();
        desc.index = info.index;
        desc.mtu = info.mtu;
        desc.type = toIfType(info.kind);
        desc.status = operStatusIsUp(info) ? IfOperStatusUp : IfOperStatusDown;
        desc.physical = info.physical;

        if (!(flags & GAA_FLAG_SKIP_UNICAST))
        {
            for (size_t j = 0; j < info.addresses.size(); ++j)
            {
                const swinx::IfaceAddress &addr = info.addresses[j];
                if (family == AF_INET && addr.family != 4)
                    continue;
                if (family == AF_INET6 && addr.family != 6)
                    continue;

                UnicastDesc unicast;
                unicast.family = addr.family;
                unicast.length = addr.length;
                for (unsigned k = 0; k < sizeof(unicast.bytes); ++k)
                    unicast.bytes[k] = k < addr.length ? addr.bytes[k] : 0;
                desc.unicast.push_back(unicast);
            }
        }

        out.push_back(desc);
    }
}

/* --------------------------------------------------------------- 尺寸与序列化
 *
 * neededSize() 与 serialize() 必须**逐次对齐**地切同一串块。为了不让两边
 * 悄悄漂移，serialize 末尾把实际用量与 neededSize 的差当作错误上报（调用方
 * 传的 capacity 就是 neededSize，多用一字节就会写越界）。
 */

ULONG neededSize(const std::vector<AdapterDesc> &descs, ULONG flags)
{
    size_t total = 0;
    const size_t wideBytes = sizeof(WCHAR);

    for (size_t i = 0; i < descs.size(); ++i)
    {
        const AdapterDesc &desc = descs[i];

        total += alignUp(sizeof(IP_ADAPTER_ADDRESSES));
        total += alignUp(desc.name.size() + 1);
        if (!(flags & GAA_FLAG_SKIP_FRIENDLY_NAME))
            total += alignUp((desc.display.size() + 1) * wideBytes);

        for (size_t j = 0; j < desc.unicast.size(); ++j)
        {
            total += alignUp(sizeof(IP_ADAPTER_UNICAST_ADDRESS));
            total += alignUp(desc.unicast[j].family == 6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in));
        }
    }

    return (ULONG)total;
}

ULONG serialize(const std::vector<AdapterDesc> &descs, ULONG flags, BYTE *base, ULONG capacity)
{
    size_t used = 0;
    IP_ADAPTER_ADDRESSES *previous = 0;

    for (size_t i = 0; i < descs.size(); ++i)
    {
        const AdapterDesc &desc = descs[i];

        size_t start = alignUp(used);
        used = start + sizeof(IP_ADAPTER_ADDRESSES);
        if (used > capacity)
            return 0;
        IP_ADAPTER_ADDRESSES *node = (IP_ADAPTER_ADDRESSES *)(base + start);
        memset(node, 0, sizeof(*node));

        node->Length = sizeof(IP_ADAPTER_ADDRESSES);
        node->IfIndex = desc.index;
        node->Ipv6IfIndex = desc.index;
        node->Mtu = desc.mtu;
        node->IfType = desc.type;
        node->OperStatus = desc.status;
        node->Ipv4Enabled = 1;
        node->Ipv6Enabled = 1;
        node->TransmitLinkSpeed = 0;
        node->ReceiveLinkSpeed = 0;

        start = alignUp(used);
        used = start + desc.name.size() + 1;
        if (used > capacity)
            return 0;
        CHAR *ansi = (CHAR *)(base + start);
        memcpy(ansi, desc.name.c_str(), desc.name.size() + 1);
        node->AdapterName = ansi;

        if (!(flags & GAA_FLAG_SKIP_FRIENDLY_NAME))
        {
            size_t chars = desc.display.size() + 1;
            start = alignUp(used);
            used = start + chars * sizeof(WCHAR);
            if (used > capacity)
                return 0;
            WCHAR *wide = (WCHAR *)(base + start);
            if (chars > 1)
                memcpy(wide, desc.display.c_str(), (chars - 1) * sizeof(WCHAR));
            wide[chars - 1] = 0;
            /* Description 与 FriendlyName 是同一个字符串，共用一份存储 */
            node->Description = wide;
            node->FriendlyName = wide;
        }

        if (desc.physical.size() > sizeof(node->PhysicalAddress))
            node->PhysicalAddressLength = sizeof(node->PhysicalAddress);
        else
            node->PhysicalAddressLength = (ULONG)desc.physical.size();
        if (node->PhysicalAddressLength)
            memcpy(node->PhysicalAddress, &desc.physical[0], node->PhysicalAddressLength);

        IP_ADAPTER_UNICAST_ADDRESS *previousUnicast = 0;
        for (size_t j = 0; j < desc.unicast.size(); ++j)
        {
            const UnicastDesc &unicast = desc.unicast[j];

            start = alignUp(used);
            used = start + sizeof(IP_ADAPTER_UNICAST_ADDRESS);
            if (used > capacity)
                return 0;
            IP_ADAPTER_UNICAST_ADDRESS *item = (IP_ADAPTER_UNICAST_ADDRESS *)(base + start);
            memset(item, 0, sizeof(*item));
            item->Length = sizeof(IP_ADAPTER_UNICAST_ADDRESS);
            item->PrefixOrigin = IpPrefixOriginOther;
            item->SuffixOrigin = IpSuffixOriginOther;
            item->DadState = IpDadStatePreferred;
            item->ValidLifetime = 0xFFFFFFFF;
            item->PreferredLifetime = 0xFFFFFFFF;
            item->LeaseLifetime = 0xFFFFFFFF;

            start = alignUp(used);
            if (unicast.family == 6)
            {
                used = start + sizeof(sockaddr_in6);
                if (used > capacity)
                    return 0;
                sockaddr_in6 *in6 = (sockaddr_in6 *)(base + start);
                memset(in6, 0, sizeof(*in6));
                in6->sin6_family = AF_INET6;
                memcpy(&in6->sin6_addr, unicast.bytes, 16);
                item->Address.lpSockaddr = (LPSOCKADDR)in6;
                item->Address.iSockaddrLength = sizeof(sockaddr_in6);
                item->OnLinkPrefixLength = 128;
            }
            else
            {
                used = start + sizeof(sockaddr_in);
                if (used > capacity)
                    return 0;
                sockaddr_in *in = (sockaddr_in *)(base + start);
                memset(in, 0, sizeof(*in));
                in->sin_family = AF_INET;
                memcpy(&in->sin_addr, unicast.bytes, 4);
                item->Address.lpSockaddr = (LPSOCKADDR)in;
                item->Address.iSockaddrLength = sizeof(sockaddr_in);
                item->OnLinkPrefixLength = 32;
            }

            if (previousUnicast)
                previousUnicast->Next = item;
            else
                node->FirstUnicastAddress = item;
            previousUnicast = item;
        }

        if (previous)
            previous->Next = node;
        previous = node;
    }

    /* 末尾补齐到对齐边界再报告：neededSize() 是按"每块对齐后相加"算的，
     * 这里少补最后一段的话，调用方会看到"要 size 却只用掉 size-3"，两次口径
     * 不一致会让严格按 pOutBufLen 复用的调用方困惑。 */
    return (ULONG)alignUp(used);
}

/* ------------------------------------------------------------ 地址变化通知 */

class AddressWatcher
{
public:
    static AddressWatcher &instance()
    {
        // 函数内静态 new：故意不析构。注册的 OVERLAPPED 属于调用方，
        // 进程退出时让线程自然结束比在静态析构期 join 安全。
        static AddressWatcher *watcher = new AddressWatcher();
        return *watcher;
    }

    void start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_)
            return;
        std::vector<swinx::IfaceInfo> ifaces;
        swinx::collectInterfaces(ifaces);
        snapshot_ = swinx::fingerprintInterfaces(ifaces);
        started_ = true;
        worker_ = std::thread(&AddressWatcher::run, this);
    }

    void add(LPOVERLAPPED overlapped)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(overlapped);
        changed_.notify_all();
    }

    bool remove(LPOVERLAPPED overlapped)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < pending_.size(); ++i)
        {
            if (pending_[i] == overlapped)
            {
                pending_.erase(pending_.begin() + i);
                return true;
            }
        }
        return false;
    }

private:
    void run()
    {
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait_for(lock, std::chrono::milliseconds(500));
                if (pending_.empty())
                    continue;
            }

            std::vector<swinx::IfaceInfo> ifaces;
            if (!swinx::collectInterfaces(ifaces))
                continue;
            std::string fingerprint = swinx::fingerprintInterfaces(ifaces);

            std::vector<LPOVERLAPPED> fire;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (fingerprint == snapshot_)
                    continue;
                snapshot_ = fingerprint;
                fire.swap(pending_);
            }

            for (size_t i = 0; i < fire.size(); ++i)
            {
                if (fire[i] && fire[i]->hEvent)
                    SetEvent(fire[i]->hEvent);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<LPOVERLAPPED> pending_;
    std::thread worker_;
    std::string snapshot_;
    bool started_ = false;
};

} // namespace

ULONG WINAPI GetAdaptersAddresses(ULONG Family, ULONG Flags, PVOID Reserved,
                                  PIP_ADAPTER_ADDRESSES AdapterAddresses, PULONG SizePointer)
{
    (void)Reserved;

    if (!SizePointer)
        return ERROR_INVALID_PARAMETER;

    std::vector<swinx::IfaceInfo> ifaces;
    if (!swinx::collectInterfaces(ifaces))
        return ERROR_NO_DATA;

    std::vector<AdapterDesc> descs;
    buildDescriptors(ifaces, Family, Flags, descs);

    ULONG needed = neededSize(descs, Flags);
    if (!AdapterAddresses)
    {
        *SizePointer = needed;
        return ERROR_BUFFER_OVERFLOW;
    }
    if (*SizePointer < needed)
    {
        *SizePointer = needed;
        return ERROR_BUFFER_OVERFLOW;
    }

    ULONG written = serialize(descs, Flags, (BYTE *)AdapterAddresses, *SizePointer);
    if (written == 0 && needed != 0)
        return ERROR_INVALID_PARAMETER; /* neededSize 与 serialize 不一致 */

    *SizePointer = written;
    return ERROR_SUCCESS;
}

DWORD WINAPI NotifyAddrChange(PHANDLE Handle, LPOVERLAPPED overlapped)
{
    if (!overlapped)
    {
        // 同步形态：Win32 在这里阻塞到下一次变化为止。用一次性事件把同一个
        // 等待路径复用，避免维护第二套实现。
        HANDLE event = CreateEventA(0, FALSE, FALSE, 0);
        if (!event)
            return GetLastError();

        OVERLAPPED local;
        memset(&local, 0, sizeof(local));
        local.hEvent = event;

        AddressWatcher::instance().start();
        AddressWatcher::instance().add(&local);
        DWORD waited = WaitForSingleObject(event, INFINITE);
        AddressWatcher::instance().remove(&local);
        CloseHandle(event);

        if (waited != WAIT_OBJECT_0)
            return ERROR_OPERATION_ABORTED;
        return ERROR_SUCCESS;
    }

    if (!overlapped->hEvent)
        return ERROR_INVALID_PARAMETER;

    AddressWatcher::instance().start();
    AddressWatcher::instance().add(overlapped);
    if (Handle)
        *Handle = (HANDLE)overlapped;

    return ERROR_IO_PENDING;
}

BOOL WINAPI CancelIPChangeNotify(LPOVERLAPPED lpOverlapped)
{
    if (!lpOverlapped)
        return FALSE;
    return AddressWatcher::instance().remove(lpOverlapped) ? TRUE : FALSE;
}
