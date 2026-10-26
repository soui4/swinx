/* WS2_32 的**进程级状态**：版本协商、初始化计数、每线程错误码。
 *
 * 套接字运行时（socket/bind/select/…）在 winsock2.cpp，两者分工明确：
 * 这里管"Winsock 这一层有没有被启动、本线程上次报了什么错"，那边管"套接字
 * 对象怎么映射到 POSIX fd"。分开是因为初始化计数必须是进程级且只此一份，
 * 而运行时那段代码体量大得多、也更值得单独读。
 */

#include <winsock2.h>

#include <string.h>
#include <atomic>

#include "debug.h"
#include "winsock_internal.h"
#define kLogTag "winsock"

namespace
{

/* 初始化计数：WSAStartup/WSACleanup 必须成对，多调 Cleanup 要报
 * WSANOTINITIALISED（与 Win32 一致），所以不能只用一个 bool。 */
std::atomic<int> s_startupCount(0);

/* Win32 的 WSA 错误码是**每线程**的（TLS）。 */
thread_local int t_lastError = 0;

const WORD kHighestVersion = 0x0202; /* 2.2 */

} // namespace

int WINAPI WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData)
{
    if (!lpWSAData)
        return WSAEFAULT;

    const WORD major = (WORD)(wVersionRequested & 0xFF);
    const WORD minor = (WORD)(wVersionRequested >> 8);

    /* 只支持 2.x。1.x（Winsock 1.1）是另一套 API 面，声称支持它会让调用方
     * 走进它没有的分支。 */
    if (major < 2)
        return WSAVERNOTSUPPORTED;

    memset(lpWSAData, 0, sizeof(WSADATA));
    lpWSAData->wHighVersion = kHighestVersion;

    /* 受支持的最高版本 = 请求版本与 2.2 的较小者。
     * 注意 MAKEWORD(a,b) = a | (b << 8)：低字节是主版本，高字节是次版本。 */
    if (minor > 2)
        lpWSAData->wVersion = kHighestVersion;
    else
        lpWSAData->wVersion = (WORD)(2 | (minor << 8));

    strncpy(lpWSAData->szDescription, "swinx (POSIX sockets via WS2_32 shim)",
            sizeof(lpWSAData->szDescription) - 1);
    strncpy(lpWSAData->szSystemStatus, "Running", sizeof(lpWSAData->szSystemStatus) - 1);

    /* 这两个上限对"没有套接字运行时"的实现没有意义，如实给 0，
     * 而不是编一个看起来合理的数。 */
    lpWSAData->iMaxSockets = 0;
    lpWSAData->iMaxUdpDg = 0;
    lpWSAData->lpVendorInfo = 0;

    ++s_startupCount;
    return 0;
}

int WINAPI WSACleanup(void)
{
    /* fetch_sub 之后再判断，避免 load/dec 之间被别的线程插进来导致计数变负：
     * 负计数会让"多调一次 Cleanup 报错"之后的 WSACleanup 又开始成功。 */
    if (s_startupCount.fetch_sub(1) <= 0)
    {
        s_startupCount.fetch_add(1); /* 撤回这次多余的递减 */
        return WSANOTINITIALISED;
    }

    return 0;
}

int WINAPI WSAGetLastError(void)
{
    return t_lastError;
}

void WINAPI WSASetLastError(int iError)
{
    t_lastError = iError;
}

/* winsock2.cpp 只问这两个问题，计数器本身不外泄。 */
namespace swinx
{
namespace wsa
{

bool started()
{
    return s_startupCount.load() > 0;
}

int lastError()
{
    return t_lastError;
}

void setLastError(int error)
{
    t_lastError = error;
}

} // namespace wsa
} // namespace swinx
