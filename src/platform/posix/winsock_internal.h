#ifndef _SWINX_WINSOCK_INTERNAL_H_
#define _SWINX_WINSOCK_INTERNAL_H_

/* winsock.cpp 与 winsock2.cpp 之间的内部契约。
 *
 * 两个文件都在 src/platform/posix/ 下，但承担不同角色：
 *   winsock.cpp   进程级状态：WSAStartup/WSACleanup 计数、每线程错误码
 *   winsock2.cpp  套接字运行时（socket/bind/select/…）
 * 后者要问前者两件事：初始化过没有、本线程的错误码是多少。这里只暴露这两个
 * 问题，不把计数器本身泄漏出去。
 */

namespace swinx
{
namespace wsa
{

bool started();
int lastError();
void setLastError(int error);

} // namespace wsa
} // namespace swinx

#endif /* _SWINX_WINSOCK_INTERNAL_H_ */
