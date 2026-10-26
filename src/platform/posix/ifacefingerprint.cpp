/* 接口表指纹：只由 iface.cpp 的采集结果决定，不碰任何系统接口。
 *
 * 单独成文件是为了它能被单元测试直接编译 —— iface.cpp 依赖 <ifaddrs.h>，
 * 在 Windows 上没法编译验证，而"什么变化才算地址变化"这条判断是会出错的
 * 逻辑（比如把 MTU 也算进去就会莫名多唤醒一次），值得单独测。
 */

#include <stdio.h>

#include "posixiface.h"

namespace swinx
{

std::string fingerprintInterfaces(const std::vector<IfaceInfo> &ifaces)
{
    // 只把"影响在线判定"的东西放进指纹：名字、index、up/running、地址。
    // MTU / MAC 变了不算地址变化，忽略可以少唤醒一次通知。
    std::string out;
    char scratch[32];
    for (size_t i = 0; i < ifaces.size(); ++i)
    {
        const IfaceInfo &info = ifaces[i];
        out += info.name;
        snprintf(scratch, sizeof(scratch), "|%u|%d|%d|", info.index, info.up ? 1 : 0,
                 info.running ? 1 : 0);
        out += scratch;
        for (size_t j = 0; j < info.addresses.size(); ++j)
        {
            const IfaceAddress &addr = info.addresses[j];
            snprintf(scratch, sizeof(scratch), "%d:", addr.family);
            out += scratch;
            for (unsigned k = 0; k < addr.length; ++k)
            {
                snprintf(scratch, sizeof(scratch), "%02x", addr.bytes[k]);
                out += scratch;
            }
            out += ',';
        }
        out += ';';
    }
    return out;
}

} // namespace swinx
