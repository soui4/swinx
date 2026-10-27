#include <sfr_config.h>
#include <windows.h>
#include <winsock2.h>
#ifndef _IPHLPAPI_H_
#define _IPHLPAPI_H_

/* iphlpapi.h —— 接口枚举与地址变化通知的最小集。
 *
 * WebKit 只用两个入口（`NetworkStateNotifierWin.cpp`）：
 *   - GetAdaptersAddresses：问「有没有一个 up 着的非 loopback 接口」
 *   - NotifyAddrChange：注册一次地址变化通知
 * 其余（GetAdaptersInfo / GetIpAddrTable / GetIfTable / GetBestInterface …）
 * 没有调用点，这里不提供 —— 与其给一堆返回空表的假实现，不如让缺失暴露出来。
 *
 * 结构体布局照 SDK 的 iptypes.h（Vista+ 的 _LH 版本）。字段给全是因为
 * 调用方可能按偏移访问任意字段，只给 WebKit 用到的那几个会让其他代码
 * 读出垃圾；实现里没有值的字段一律清零。
 */

#define MAX_ADAPTER_ADDRESS_LENGTH     8
#define MAX_ADAPTER_DESCRIPTION_LENGTH 128
#define MAX_ADAPTER_NAME_LENGTH        256
#define MAX_ADAPTER_NAME               128
#define MAX_DHCPV6_DUID_LENGTH         130

/* ipifcons.h 的接口类型。swinx 只需要区分「以太/无线」与「回环」。 */
#define MIB_IF_TYPE_OTHER     1
#define MIB_IF_TYPE_ETHERNET  6
#define MIB_IF_TYPE_TOKENRING 9
#define MIB_IF_TYPE_FDDI      15
#define MIB_IF_TYPE_PPP       23
#define MIB_IF_TYPE_LOOPBACK  24
#define MIB_IF_TYPE_SLIP      28
#define MIB_IF_TYPE_TUNNEL    131
#define MIB_IF_TYPE_IEEE80211 71

typedef ULONG IFTYPE;

typedef enum
{
    IfOperStatusUp = 1,
    IfOperStatusDown,
    IfOperStatusTesting,
    IfOperStatusUnknown,
    IfOperStatusDormant,
    IfOperStatusNotPresent,
    IfOperStatusLowerLayerDown
} IF_OPER_STATUS;

typedef enum
{
    IpPrefixOriginOther = 0,
    IpPrefixOriginManual,
    IpPrefixOriginWellKnown,
    IpPrefixOriginDhcp,
    IpPrefixOriginRouterAdvertisement,
    IpPrefixOriginUnchanged = 1 << 4
} IP_PREFIX_ORIGIN;

typedef enum
{
    IpSuffixOriginOther = 0,
    IpSuffixOriginManual,
    IpSuffixOriginWellKnown,
    IpSuffixOriginDhcp,
    IpSuffixOriginLinkLayerAddress,
    IpSuffixOriginRandom,
    IpSuffixOriginUnchanged = 1 << 4
} IP_SUFFIX_ORIGIN;

typedef enum
{
    IpDadStateInvalid = 0,
    IpDadStateTentative,
    IpDadStateDuplicate,
    IpDadStateDeprecated,
    IpDadStatePreferred
} IP_DAD_STATE;

typedef enum
{
    IfUnknown = 0,
    IfLoopback,
    IfWireless,
    IfEthernet,
} NET_IF_CONNECTION_TYPE;

typedef enum
{
    TUNNEL_TYPE_NONE = 0,
    TUNNEL_TYPE_OTHER = 1,
    TUNNEL_TYPE_DIRECT = 2,
    TUNNEL_TYPE_6TO4 = 11,
    TUNNEL_TYPE_ISATAP = 13,
    TUNNEL_TYPE_TEREDO = 14,
} TUNNEL_TYPE;

typedef union _NET_LUID_LH {
    ULONG64 Value;
    struct
    {
        ULONG64 Reserved : 24;
        ULONG64 NetLuidIndex : 24;
        ULONG64 IfType : 16;
    } Info;
} NET_LUID, *PNET_LUID, IF_LUID, *PIF_LUID;

typedef UINT32 NET_IF_COMPARTMENT_ID;
typedef GUID NET_IF_NETWORK_GUID;

typedef struct _IP_ADAPTER_UNICAST_ADDRESS_LH
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_UNICAST_ADDRESS_LH *Next;
    SOCKET_ADDRESS Address;
    IP_PREFIX_ORIGIN PrefixOrigin;
    IP_SUFFIX_ORIGIN SuffixOrigin;
    IP_DAD_STATE DadState;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG LeaseLifetime;
    UINT8 OnLinkPrefixLength;
} IP_ADAPTER_UNICAST_ADDRESS_LH, *PIP_ADAPTER_UNICAST_ADDRESS_LH;

typedef IP_ADAPTER_UNICAST_ADDRESS_LH IP_ADAPTER_UNICAST_ADDRESS;
typedef PIP_ADAPTER_UNICAST_ADDRESS_LH PIP_ADAPTER_UNICAST_ADDRESS;

typedef struct _IP_ADAPTER_ANYCAST_ADDRESS_XP
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_ANYCAST_ADDRESS_XP *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_ANYCAST_ADDRESS_XP, *PIP_ADAPTER_ANYCAST_ADDRESS_XP;

typedef IP_ADAPTER_ANYCAST_ADDRESS_XP IP_ADAPTER_ANYCAST_ADDRESS;
typedef PIP_ADAPTER_ANYCAST_ADDRESS_XP PIP_ADAPTER_ANYCAST_ADDRESS;

typedef struct _IP_ADAPTER_MULTICAST_ADDRESS_XP
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_MULTICAST_ADDRESS_XP *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_MULTICAST_ADDRESS_XP, *PIP_ADAPTER_MULTICAST_ADDRESS_XP;

typedef IP_ADAPTER_MULTICAST_ADDRESS_XP IP_ADAPTER_MULTICAST_ADDRESS;
typedef PIP_ADAPTER_MULTICAST_ADDRESS_XP PIP_ADAPTER_MULTICAST_ADDRESS;

typedef struct _IP_ADAPTER_DNS_SERVER_ADDRESS_XP
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Reserved;
        };
    };
    struct _IP_ADAPTER_DNS_SERVER_ADDRESS_XP *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_DNS_SERVER_ADDRESS_XP, *PIP_ADAPTER_DNS_SERVER_ADDRESS_XP;

typedef IP_ADAPTER_DNS_SERVER_ADDRESS_XP IP_ADAPTER_DNS_SERVER_ADDRESS;
typedef PIP_ADAPTER_DNS_SERVER_ADDRESS_XP PIP_ADAPTER_DNS_SERVER_ADDRESS;

typedef struct _IP_ADAPTER_PREFIX_XP
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Flags;
        };
    };
    struct _IP_ADAPTER_PREFIX_XP *Next;
    SOCKET_ADDRESS Address;
    ULONG PrefixLength;
} IP_ADAPTER_PREFIX_XP, *PIP_ADAPTER_PREFIX_XP;

typedef IP_ADAPTER_PREFIX_XP IP_ADAPTER_PREFIX;
typedef PIP_ADAPTER_PREFIX_XP PIP_ADAPTER_PREFIX;

typedef struct _IP_ADAPTER_WINS_SERVER_ADDRESS_LH
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Reserved;
        };
    };
    struct _IP_ADAPTER_WINS_SERVER_ADDRESS_LH *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_WINS_SERVER_ADDRESS_LH, *PIP_ADAPTER_WINS_SERVER_ADDRESS_LH;

typedef struct _IP_ADAPTER_GATEWAY_ADDRESS_LH
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD Reserved;
        };
    };
    struct _IP_ADAPTER_GATEWAY_ADDRESS_LH *Next;
    SOCKET_ADDRESS Address;
} IP_ADAPTER_GATEWAY_ADDRESS_LH, *PIP_ADAPTER_GATEWAY_ADDRESS_LH;

typedef struct _IP_ADAPTER_DNS_SUFFIX
{
    struct _IP_ADAPTER_DNS_SUFFIX *Next;
    WCHAR String[MAX_ADAPTER_NAME];
} IP_ADAPTER_DNS_SUFFIX, *PIP_ADAPTER_DNS_SUFFIX;

typedef struct _IP_ADAPTER_ADDRESSES_LH
{
    union {
        ULONGLONG Alignment;
        struct
        {
            ULONG Length;
            DWORD IfIndex;
        };
    };
    struct _IP_ADAPTER_ADDRESSES_LH *Next;
    PCHAR AdapterName;
    PIP_ADAPTER_UNICAST_ADDRESS_LH FirstUnicastAddress;
    PIP_ADAPTER_ANYCAST_ADDRESS_XP FirstAnycastAddress;
    PIP_ADAPTER_MULTICAST_ADDRESS_XP FirstMulticastAddress;
    PIP_ADAPTER_DNS_SERVER_ADDRESS_XP FirstDnsServerAddress;
    PWCHAR DnsSuffix;
    PWCHAR Description;
    PWCHAR FriendlyName;
    BYTE PhysicalAddress[MAX_ADAPTER_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    union {
        ULONG Flags;
        struct
        {
            ULONG DdnsEnabled : 1;
            ULONG RegisterAdapterSuffix : 1;
            ULONG Dhcpv4Enabled : 1;
            ULONG ReceiveOnly : 1;
            ULONG NoMulticast : 1;
            ULONG Ipv6OtherStatefulConfig : 1;
            ULONG NetbiosOverTcpipEnabled : 1;
            ULONG Ipv4Enabled : 1;
            ULONG Ipv6Enabled : 1;
            ULONG Ipv6ManagedAddressConfigurationSupported : 1;
        };
    };
    ULONG Mtu;
    IFTYPE IfType;
    IF_OPER_STATUS OperStatus;
    DWORD Ipv6IfIndex;
    ULONG ZoneIndices[16];
    PIP_ADAPTER_PREFIX_XP FirstPrefix;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    PIP_ADAPTER_WINS_SERVER_ADDRESS_LH FirstWinsServerAddress;
    PIP_ADAPTER_GATEWAY_ADDRESS_LH FirstGatewayAddress;
    ULONG Ipv4Metric;
    ULONG Ipv6Metric;
    IF_LUID Luid;
    SOCKET_ADDRESS Dhcpv4Server;
    NET_IF_COMPARTMENT_ID CompartmentId;
    NET_IF_NETWORK_GUID NetworkGuid;
    NET_IF_CONNECTION_TYPE ConnectionType;
    TUNNEL_TYPE TunnelType;
    SOCKET_ADDRESS Dhcpv6Server;
    BYTE Dhcpv6ClientDuid[MAX_DHCPV6_DUID_LENGTH];
    ULONG Dhcpv6ClientDuidLength;
    ULONG Dhcpv6Iaid;
    PIP_ADAPTER_DNS_SUFFIX FirstDnsSuffix;
} IP_ADAPTER_ADDRESSES_LH, *PIP_ADAPTER_ADDRESSES_LH;

typedef IP_ADAPTER_ADDRESSES_LH IP_ADAPTER_ADDRESSES;
typedef PIP_ADAPTER_ADDRESSES_LH PIP_ADAPTER_ADDRESSES;

/* GetAdaptersAddresses 的 Flags（值取 SDK 的 iptypes.h）。 */
#define GAA_FLAG_SKIP_UNICAST                0x0001
#define GAA_FLAG_SKIP_ANYCAST                0x0002
#define GAA_FLAG_SKIP_MULTICAST              0x0004
#define GAA_FLAG_SKIP_DNS_SERVER             0x0008
#define GAA_FLAG_INCLUDE_PREFIX              0x0010
#define GAA_FLAG_SKIP_FRIENDLY_NAME          0x0020
#define GAA_FLAG_INCLUDE_WINS_INFO           0x0040
#define GAA_FLAG_INCLUDE_GATEWAYS            0x0080
#define GAA_FLAG_INCLUDE_ALL_INTERFACES      0x0100
#define GAA_FLAG_INCLUDE_ALL_COMPARTMENTS    0x0200
#define GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER 0x0400

#ifdef __cplusplus
extern "C"
{
#endif

    ULONG WINAPI GetAdaptersAddresses(ULONG Family, ULONG Flags, PVOID Reserved, PIP_ADAPTER_ADDRESSES AdapterAddresses, PULONG SizePointer);

    /* 一次性的地址变化通知。带 lpOverlapped 时立即返回 ERROR_IO_PENDING，
     * 变化发生时 SetEvent(lpOverlapped->hEvent)。 */
    DWORD WINAPI NotifyAddrChange(PHANDLE Handle, LPOVERLAPPED overlapped);
    BOOL WINAPI CancelIPChangeNotify(LPOVERLAPPED lpOverlapped);

#ifdef __cplusplus
}
#endif

#endif /* _IPHLPAPI_H_ */
