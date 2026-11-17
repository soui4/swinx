#ifndef _SWINX_UTILS_H_
#define _SWINX_UTILS_H_

/* swinx 平台助手契约
 *
 * 声明在本文件，实现在 src/platform/<平台>/utils.*。同一个符号在多个平台目录里各有一份
 * 实现，构建时只有当前平台的目录进入源文件列表（见 linux.cmake / macos.cmake / ios.cmake
 * / mobile.cmake 里的 file(GLOB)），由链接器决定最终链接哪一份。因此调用方写的是同一个
 * 函数名，不需要任何平台 #ifdef 分支。
 *
 * 注意：这是 **实现细节，不是公共 API**——本文件位于 src/，不会随 include/ 安装出去。
 *
 * 返回类型用 int 而不是 BOOL：这些函数跨 TU 边界（实现在平台目录、调用方在 src 下的 cpp），
 * 而 swinx 的 BOOL 在 Objective-C++ 编译单元里是 bool / signed char（clang 预定义
 * __OBJC_BOOL_IS_BOOL）、在 .cpp 里是 int，两边不一致（见 include/ctypes.h）。
 */

#include <windows.h>
#include <commdlg.h>

/* Windows MessageBeep 的平台实现，调用方见 src/sysapi.cpp。
 *
 * uType 为 0xFFFFFFFF(-1) 时 Windows 播放"简单提示音"，其余为 MB_ICON* 系列。各平台都
 * 只有一种系统提示音，没有与 MB_ICON* 一一对应的提示音集合，因此统一映射到系统提示音：
 *     Linux    X11 核心协议 Bell 请求（platform/linux/utils.cpp）
 *     macOS    NSBeep()（platform/cocoa/utils.mm）
 *     iOS      AudioServicesPlayAlertSound（platform/ios/utils.mm）
 *     移动端   转发宿主应用的平台回调 g_platformAPI.audio.messageBeep（platform/mobile/utils.cpp），
 *              由应用层用 Android ToneGenerator/RingtoneManager 或 OHOS AudioRenderer 发声；
 *              回调未注册时返回 FALSE
 * 返回 TRUE 表示提示音请求已交给系统，与 Win32"声音已入队即返回 TRUE"的语义一致。
 */
int swinx_messageBeep(UINT uType);

/* 单调计数原语，单位 100ns（10MHz）。唯一调用方是
 * src/platform/common/perfclock.cpp 里的 QueryPerformanceCounter()。
 *
 * 为什么把它做成跨平台契约：QueryPerformanceFrequency/Counter 是 Win32 语义
 * （刻度 + 单调读数），与「时间从哪来」无关。把时间源压成这一个函数之后，
 * QPF/QPC 就只有 src/platform/common/perfclock.cpp 一份实现，各平台只答"现在
 * 多少 tick"：
 *
 *     posix（linux / macOS / iOS / Android / OHOS / wasm）
 *         gettimeofday()，纪元 1601-01-01，见 src/platform/posix/sysapi.cpp
 *     freertos
 *         esp_timer_get_time()（IDF）或内核 tick，纪元为开机时刻，见
 *         src/platform/freertos/winobjs.cpp
 *
 * 只要求单调递增 —— QPC 用来算区间，不读挂钟（挂钟是 GetSystemTime /
 * GetLocalTime 的职责），所以两个平台纪元不同不影响任何调用方。
 *
 * 返回类型写 long long 而不是 LONGLONG：本头会被 Objective-C++ 单元包含，
 * long long 在所有前端都是同一个类型（理由同本文件头关于 BOOL 的说明）。 */
long long swinx_perf_ticks_100ns(void);

/* GetSystemMetrics 的缩放百分比，调用方见 src/platform/common/sysapi.cpp。
 *
 * 取值表（sm_cxicon=32、sm_cxminTrack=6、非客户区厚度=0……）是 Win32 语义，全平台
 * 只有一份；各平台唯一可能不同的是「这个度量值要不要按 DPI 放大」：
 *
 *     多数平台     return GetSystemScale()        （100 = 不缩放）
 *     macOS        SM_CXCURSOR / SM_CYCURSOR 返回 100 —— CoreGraphics 的 cursor
 *                  尺寸本身就是 point，再乘 DPI 会翻倍（其余度量仍按 DPI 放大）
 *
 * 之所以把它抽成契约而不是在 common 里写 #ifdef __APPLE__：common 层不许有平台
 * 分支（见 AGENTS.md §2）。返回类型用 int 不用 BOOL，理由同上面的
 * swinx_perf_ticks_100ns：本头会被 Objective-C++ 单元包含，BOOL 在 Apple 前端
 * 与 .cpp 里宽度不一致。 */
int swinx_system_metric_scale(int nIndex);

/* ANSI 代码页（GetACP 的返回值），调用方见 src/platform/common/codepage.cpp。
 *
 * 「ANSI 串在这平台上是什么编码」是平台约定，不是 Win32 语义，所以值留在平台
 * 目录，API 本体和「由它派生的判断」（IsDBCSLeadByte）合并在 common 一份：
 *
 *     posix 家族   CP_UTF8（65001）—— swinx 内部统一以 UTF-8 表示多字节串
 *     freertos    1252（Windows-1252，单字节，因此没有 lead byte）
 *
 * 两个值各自内部自洽，别只改一个：IsDBCSLeadByte 由 GetACP() 派生，注入别的
 * 代码页时它会跟着变。返回 int 而不是 UINT，理由同上面的 BOOL 说明。 */
int swinx_ansi_codepage(void);

/* 文件选择的平台实现，调用方见 src/cmmmdlg.cpp 的 SwinxPickFileAsyncA。
 *
 * 各平台目录的 dlghelper.* 给出同名实现：wasm 是真异步（结果由页面在稍后回调重入口
 * 送回，见 src/platform/wasm/dlghelper.cpp），其余平台的本地对话框本来就阻塞，直接把
 * 同步钩子的结果内联回调出去。为什么浏览器不能同步：change 事件只在 JS 栈返回之后才
 * 派发，阻塞在这一次调用里等就等于把整个标签页冻住。
 *
 * p 是 SwinxPickFileAsyncA 在堆上准备的私有副本：lpstrFile 指向它自有的缓冲（结果写到
 * 那里），lpstrFileTitle / nMaxFileTitle 已被置空，其余字段指向调用方内存、只在本次调用
 * 期间读用。返回 1 = 已受理（结果一律经 pfnDone 给出，可能就在本次调用内联给出），
 * 0 = 未发起且已 SetLastError，此时 pfnDone 不会被调用。
 *
 * 返回类型用 int 而不是 BOOL（见本文件头）。 */
int swinx_pick_file_async(LPOPENFILENAMEA p, DlgMode mode, SWINXFILEPICKCALLBACK pfnDone, void *pCtx);

/* 非 wasm 平台的共用尾部：把同步钩子的结果按异步契约回调出去。同步钩子返回 BOOL，
 * 这里收的是已经转成 int 的值 —— BOOL 跨 TU 不一致（见本文件头），不能让它在
 * .cpp 与 .mm 之间裸传。 */
inline int swinx_pick_file_deliver(int nSyncOk, LPCSTR lpszUtf8Path,
                                   SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    int nResult;
    if (nSyncOk)
    {
        nResult = 1;
    }
    else if (GetLastError() == ERROR_CALL_NOT_IMPLEMENTED)
    {
        /* 只有"本平台显式拒绝"的桩会置这个码（移动端/裸金属没有对话框能力）。 */
        nResult = -1;
    }
    else
    {
        /* 同步钩子分不出"用户按了取消"和"取文件失败"（swinx 没有
         * CommDlgExtendedError），所以这里保守地按取消报，不伪造第三种结果。 */
        nResult = 0;
    }
    pfnDone(nResult, nResult == 1 ? lpszUtf8Path : NULL, pCtx);
    return 1;
}

#endif // _SWINX_UTILS_H_
