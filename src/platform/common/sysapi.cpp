/*
 * sysapi.cpp -- 与平台无关的那批零散 Win32 语义（USER32 / KERNEL32 杂项）
 *
 * 这里的每个函数在 posix/sysapi.cpp 与 freertos/sysapi.cpp 里都曾各有一份，
 * 而且除注释外**逐字相同**（2026-11-16 第二批下沉）。它们与窗口系统无关，
 * 只有两处取值来自 SConnection（屏幕尺寸、双击间隔）—— 与 common/msgpump.cpp
 * 依赖的是同一份契约，所以收到这一层，平台目录不再留副本。
 *
 * 判断"是不是真的中立"的标准是：换个平台，语义会不会变？
 *   * GetSystemMetrics 的取值表是 Win32 语义（sm_cxicon=32、sm_cxminTrack=6、
 *     非客户区厚度=0……），全平台一致才有意义；唯一可能不同的是"要不要按 DPI
 *     放大"，这一点单独交给 swinx_system_metric_scale()（契约见 src/SwinxUtils.h），
 *     目前只有 macOS 的 cursor 尺寸要求不放大。
 *   * GetDoubleClickTime 转发到 SConnection —— 间隔值本来就在各平台的
 *     SConnection 里。旧 freertos 另有一份硬编码 500 的副本，与它自己
 *     SConnection::GetDoubleClickTime() 的 400 互相矛盾，已随本文件移除：
 *     唯一事实来源是 SConnection。
 *   * MulDiv / StrToIntEx / StrToInt64Ex 是纯算术与纯解析，一个平台分支都不该有。
 *   * _wfopen 只是"wchar_t 路径/模式转 UTF-8 再 fopen"，真正的转换在 codepage.cpp。
 *
 * 明确**不算**中立、因而留在平台目录里的邻居（别往这里搬）：
 *   GetACP                        ANSI 代码页是平台约定（posix=UTF-8，裸机=1252）
 *   GetLocalTime / GetSystemTime  裸机没有 RTC，由 tick 推出来
 *   SystemParametersInfo          滚轮行数、字体会平滑、非客户区度量都是宿主的事
 *   文件系统一族                   CreateFile/FindFirstFile/GetFileAttributes
 *   InterlockedXxx64              32 位核上 8 字节原子要关中断（见 interlocked.cpp）
 *   DestroyCursor                 各平台是否真有光标系统不一样
 */
#include <windows.h>
#include "sysapi.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <string>

#include "SConnection.h"
#include "SwinxUtils.h"
#include "cursormgr.h"
#include "tostring.h"

using namespace swinx;

// ---- last-error -----------------------------------------------------------
// 用线程 errno 代理：GetLastError() 返回的是 errno 的数值，不是 Windows 的
// ERROR_* 表（依赖具体数值的调用方必须先确认）。各平台都是这条口径。
void SetLastError(int e)
{
    errno = e;
}

int GetLastError()
{
    return errno;
}

// ---- MulDiv ---------------------------------------------------------------
int MulDiv(int a, int b, int c)
{
    if (c == 0)
    {
        SetLastError(ERROR_INT_DIVIDE_BY_ZERO);
        return -1;
    }
    LONGLONG ret;
    /* We want to deal with a positive divisor to simplify the logic. */
    if (c < 0)
    {
        a = -a;
        c = -c;
    }

    /* If the result is positive, we "add" to round. else, we subtract to round. */
    if ((a < 0 && b < 0) || (a >= 0 && b >= 0))
        ret = (((LONGLONG)a * b) + (c / 2)) / c;
    else
        ret = (((LONGLONG)a * b) - (c / 2)) / c;

    if (ret > 2147483647 || ret < -2147483647)
        return -1;
    return ret;
}

// ---- 字符串 -> 整数（shlwapi 的 StrToIntEx / StrToInt64Ex） ----------------
// Win32 语义：0x 前缀只有在 flags 带 STIF_SUPPORT_HEX 时才认；不认八进制；
// 前导空白与正负号允许。旧 freertos 版本是裸 strtol(,,0)，既无条件接受 0x、
// 又把 "010" 读成八进制、还完全忽略 flags —— 故以此实现为准。
#define STIF_DEFAULT     0x00000000L
#define STIF_SUPPORT_HEX 0x00000001L

BOOL StrToInt64ExW(const wchar_t *str, DWORD flags, LONGLONG *ret)
{
    BOOL negative = FALSE;
    LONGLONG value = 0;

    if (!str || !ret)
        return FALSE;

    /* Win32 在失败时会把结果写成 0，不是"保持原值"（实测 shlwapi：
     * StrToIntExA("+-12", 0, &v) 之后 v == 0，即使进来时 v 是 -999）。
     * 这有实际后果：SNamedValue 解析 "rgba(x,y,z,1)" 时忽略返回值，swinx 上
     * 保留的是上一个通道的值，Windows 上是 0。所以入口先清零，各失败分支
     * 直接 return 即可。 */
    *ret = 0;

    /* Skip leading space, '+', '-' */
    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;

    if (*str == '-')
    {
        negative = TRUE;
        str++;
    }
    else if (*str == '+')
        str++;

    if (flags & STIF_SUPPORT_HEX && *str == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        /* Read hex number */
        str += 2;

        if (!isxdigit((int)*str))
            return FALSE;

        while (isxdigit((int)*str))
        {
            value *= 16;
            if (*str >= '0' && *str <= '9')
                value += (*str - '0');
            else if (*str >= 'A' && *str <= 'Z')
                value += 10 + (*str - 'A');
            else
                value += 10 + (*str - 'a');
            str++;
        }

        *ret = value;
        return TRUE;
    }

    /* Read decimal number */
    if (*str < '0' || *str > '9')
        return FALSE;

    while (*str >= '0' && *str <= '9')
    {
        value *= 10;
        value += (*str - '0');
        str++;
    }

    *ret = negative ? -value : value;
    return TRUE;
}

BOOL StrToIntExW(const wchar_t *str, DWORD flags, INT *ret)
{
    if (!ret)
        return FALSE;

    LONGLONG value = 0;
    BOOL res;
    res = StrToInt64ExW(str, flags, &value);
    *ret = res ? (INT)value : 0;
    return res;
}

BOOL StrToInt64ExA(const char *str, DWORD flags, LONGLONG *ret)
{
    if (!str || !ret)
        return FALSE;

    std::wstring wstr;
    towstring(str, -1, wstr);
    return StrToInt64ExW(wstr.c_str(), flags, ret);
}

BOOL StrToIntExA(const char *str, DWORD flags, INT *ret)
{
    if (!ret)
        return FALSE;

    LONGLONG value = 0;
    BOOL res;
    res = StrToInt64ExA(str, flags, &value);
    *ret = res ? (INT)value : 0;
    return res;
}

// ---- 屏幕度量 -------------------------------------------------------------
int GetSystemScale()
{
    int dpi = GetDpiForWindow(0);
    return dpi * 100 / 96;
}

int GetSystemMetrics(int nIndex)
{
    int ret = 0;
    SConnection *conn = SConnMgr::instance()->getConnection();
    HMONITOR hMonitor = conn->GetScreen(0);
    switch (nIndex)
    {
    case SM_CXSCREEN:
        return conn->GetScreenWidth(hMonitor);
    case SM_CYSCREEN:
        return conn->GetScreenHeight(hMonitor);
    case SM_CXBORDER:
    case SM_CYBORDER:
    case SM_CXEDGE:
    case SM_CYEDGE:
        return 1;
    case SM_CXSMICON:
        return 16;
    case SM_CYSMICON:
        return 16;
    // swinx 不自绘的非客户区尺寸：标题栏及其按钮、调整边框、对话框边框、菜单栏。
    // 它们要么由原生窗口管理器画在窗口矩形之外（Linux 的 _MOTIF_WM_HINTS、macOS 的
    // NSWindowStyleMask），要么由上层 UI 库（SOUI 的菜单控件）负责，因此在 swinx 的
    // 窗口矩形里厚度恒为 0。显式返回 0 而不是落进 default 分支，免得每次查询都刷
    // 一遍 "unknown index"——0 在这里是"确知没有"，不是"未实现"。
    case SM_CYCAPTION:
    case SM_CXSIZE:
    case SM_CYSIZE:
    case SM_CXSMSIZE:
    case SM_CYSMSIZE:
    case SM_CXFRAME:
    case SM_CYFRAME:
    case SM_CXDLGFRAME:
    case SM_CYDLGFRAME:
    case SM_CYMENU:
        return 0;
    case SM_CXICON:
        ret = 32;
        break;
    case SM_CYICON:
        ret = 32;
        break;
    case SM_CXCURSOR:
        ret = 24;
        break;
    case SM_CYCURSOR:
        ret = 24;
        break;
    case SM_CYHSCROLL:
    case SM_CXVSCROLL:
        ret = 16;
        break;
    case SM_CYVTHUMB:
    case SM_CXHTHUMB:
        ret = 16;
        break;
    case SM_CXMENUCHECK:
    case SM_CYMENUCHECK:
        ret = 16;
        break;
    case SM_CXDRAG:
    case SM_CYDRAG:
        ret = 4;
        break;
    case SM_CXMINTRACK:
    case SM_CYMINTRACK:
        ret = 6;
        break;
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK:
        ret = 1;
        break;
    default:
        printf("unknown index for GetSystemMetrics, index=%d\n", nIndex);
        break;
    }
    // 是否按 DPI 放大由平台回答（旧代码是这里一个 #ifdef __APPLE__）
    return ret * swinx_system_metric_scale(nIndex) / 100;
}

UINT WINAPI GetDoubleClickTime(VOID)
{
    return SConnMgr::instance()->getConnection()->GetDoubleClickTime();
}

// ---- 光标 ---------------------------------------------------------------
// 光标不画进窗口：实体由 src/cursormgr.cpp 的名表管理，显示由 SConnection 负责。
// LoadCursor 只查表，所以两个平台完全一样；SetCursor/DestroyCursor 有平台差异，
// 仍留在各自的 sysapi.cpp。
HCURSOR LoadCursorA(HINSTANCE hInstance __attribute__((unused)), LPCSTR lpCursorName)
{
    return CursorMgr::loadCursor(lpCursorName);
}

HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName)
{
    if (IS_INTRESOURCE(lpCursorName))
        return LoadCursorA(hInstance, (LPCSTR)lpCursorName);
    std::string str;
    tostring(lpCursorName, -1, str);
    return CursorMgr::loadCursor(str.c_str());
}

// ---- CRT：wchar_t 路径的 fopen -------------------------------------------
// 路径与模式统一转成 UTF-8 再交给 libc。裸机上 fopen 永远失败（没有文件系统），
// 但接口必须在：调用方无法分辨"路径不存在"和"这个平台没有 _wfopen"。
FILE *_wfopen(const wchar_t *path, const wchar_t *mode)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    char *u8path = (char *)malloc(len + 1);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, u8path, len + 1, nullptr, nullptr);
    u8path[len] = 0;
    char u8mode[20] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, mode, -1, u8mode, 20, nullptr, nullptr);
    FILE *ret = fopen(u8path, u8mode);
    free(u8path);
    return ret;
}
