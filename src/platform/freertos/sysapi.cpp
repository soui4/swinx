/*
 * sysapi.cpp -- USER32 系统度量/光标、时间、CRT 杂项、IMM（没有平台的公共入口）
 *
 * 与 posix 侧的 src/platform/posix/sysapi.cpp 是同名对应关系：那边放的就是
 * GetSystemMetrics / LoadCursor / SystemParametersInfo / _time64 / StrToIntEx /
 * IMM 这一批。消息泵（GetMessage/PeekMessage 及输入查询）不在本文件 —— 它已经
 * 下沉到 src/platform/common/msgpump.cpp，与 posix 家族共用一份。
 *
 * 保留在平台目录的理由：
 *   * GetSystemMetrics 的取值来自 SConnection 的屏幕尺寸与本板 DPI；
 *   * 时间族在裸机上没有 RTC，GetLocalTime 由 tick 推出来（见 winobjs.cpp），
 *     _mkgmtime 用 days_from_civil 自实现且**任何路径都不返回 -1**（旧桩返回 -1
 *     曾让 SOUI 的动画时钟每秒回绕一次，见 README 11.5）；
 *   * IMM / 光标 / SystemParametersInfo 在本端口没有真实实现对端。
 */
#include <sfr_config.h>
#include <windows.h>
#include "sysapi.h"

#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <wchar.h>

#include "SConnection.h"
#include "SwinxUtils.h" // swinx_ansi_codepage() / swinx_system_metric_scale() 的契约声明
#include "cursormgr.h"
#include "swinx_freertos_api.h"
#include "tostring.h"

// ---- 屏幕度量 -------------------------------------------------------------
// GetSystemMetrics 的取值表与 GetSystemScale 本身已下沉到
// src/platform/common/sysapi.cpp；这里只回答这一个平台问题：某个度量值要不要
// 按 DPI 放大。裸机没有 CoreGraphics 的 point 语义，所有度量统一按 DPI 放大。
int swinx_system_metric_scale(int nIndex)
{
    (void)nIndex;
    return GetSystemScale();
}

// ANSI 代码页（契约见 src/SwinxUtils.h）：裸机端沿用 Windows-1252 —— 单字节、
// 没有 lead byte，与 posix 家族的 CP_UTF8 不同。GetACP 本体在
// src/platform/common/codepage.cpp，由它派生 IsDBCSLeadByte。
// 放在本文件而不是 win32_rich.cpp：后者在 soui_fr / swinx_test 这两个 target 里
// 不参与编译，契约必须落在每个 target 都会编进来的文件里。
int swinx_ansi_codepage(void)
{
    return 1252;
}

HCURSOR SetCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetCursor(0, hCursor);
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT val __attribute__((unused)), void *ptr, UINT winini __attribute__((unused)))
{
    switch (action)
    {
    case SPI_GETWHEELSCROLLLINES:
    {
        unsigned int *pv = (unsigned int *)ptr;
        *pv = 3;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT val, void *ptr, UINT winini)
{
    return SystemParametersInfoA(action, val, ptr, winini);
}

/* bare metal: any non-null address is readable, no SIGSEGV probing */
BOOL IsBadReadPtr(const void *ptr, size_t)
{
    return ptr == NULL ? TRUE : FALSE;
}

// ---- time (Linux: sysapi.cpp) ----------------------------------------------
// GetTickCount / GetLocalTime live in winobjs.cpp.  GetTickCount64 shares the
// 32-bit tick source (wraps like GetTickCount; no 64-bit counter on CM3).
uint64_t GetTickCount64(void) { return (uint64_t)swinx_fr::ticks_now(); }

void GetSystemTime(SYSTEMTIME *st)
{
    /* no RTC / no timezone on bare metal: same as local time */
    GetLocalTime(st);
}

DWORD GetCurrentProcessId(void) { return 1; }

BOOL UpdateLayeredWindow(HWND hWnd, HDC hdcDst, POINT *pptDst, SIZE *psize, HDC hdcSrc, POINT *pptSrc, COLORREF crKey, BLENDFUNCTION *pblend, DWORD dwFlags)
{
    /* layered windows have no meaning on a virtual-window platform */
    (void)hWnd; (void)hdcDst; (void)pptDst; (void)psize; (void)hdcSrc;
    (void)pptSrc; (void)crKey; (void)pblend; (void)dwFlags;
    return FALSE;
}


__time64_t _time64(__time64_t *t)
{
    __time64_t v = (__time64_t)time(NULL);
    if (t)
        *t = v;
    return v;
}

#if !SFR_HOSTED
// host: MinGW CRT provides _mktime64 with a non-const tm* parameter
__time64_t _mktime64(const struct tm *tm)
{
    return (__time64_t)mktime(const_cast<struct tm *>(tm));
}
#endif

// UTC -> Unix epoch seconds (timegm / _mkgmtime equivalent), implemented
// without libc so the bare-metal newlib target and the hosted simulator share
// one implementation.  This is the platform layer's contract, not something
// SOUI works around: the old stub returned -1, and STime::GetCurrentTimeMs()
// casts it straight to uint64_t, giving a "clock" of 2^64-1000+ms that wraps
// backwards once per second and freezes every value animator.  Hence this
// implementation must NEVER return -1 on any path.
time_t _mkgmtime(struct tm *tm)
{
    if (!tm)
        return (time_t)0; // a valid epoch value, not the error sentinel
    // days_from_civil (Howard Hinnant), proleptic Gregorian calendar.
    int64_t y = (int64_t)tm->tm_year + 1900;
    const unsigned m = (unsigned)(tm->tm_mon + 1); // [1, 12]
    const unsigned d = (unsigned)tm->tm_mday;      // [1, 31]
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                // [0, 399]
    const unsigned mp = (m > 2) ? (m - 3) : (m + 9);               // [0, 11]
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;           // [0, 365]
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy; // [0, 146096]
    const int64_t days = era * 146097 + (int64_t)doe - 719468;
    const int64_t secs = days * 86400
                       + (int64_t)tm->tm_hour * 3600
                       + (int64_t)tm->tm_min * 60
                       + (int64_t)tm->tm_sec;
    // -1 is also a legal instant (1969-12-31 23:59:59 UTC) but doubles as
    // the mktime error sentinel; clamp it so it can never reach the caller.
    if (secs == (time_t)-1)
        return (time_t)0;
    return (time_t)secs;
}

extern "C" {

// ---- misc CRT / entropy ----------------------------------------------------
int _getentropy(void *buf, size_t len)
{
    /* no hardware RNG wired yet: refuse, callers must fall back */
    (void)buf;
    (void)len;
    return -1;
}

/* newlib reentrant time syscalls (pulled in by clock()/time()) */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tv;
    (void)tz;
    return -1; /* no RTC: clock()/time() report failure */
}

clock_t _times(clock_t *t)
{
    if (t)
        *t = 0;
    return 0;
}

void qsort_s(void *base, size_t nmemb, size_t size,
             int (*cmp)(void *, const void *, const void *), void *ctx)
{
    /* insertion-sort fallback with the qsort_s comparator shape (ctx first) */
    char *b = (char *)base;
    static char tmp[256];
    if (!b || !cmp || size == 0 || size > sizeof(tmp))
        return;
    for (size_t i = 1; i < nmemb; ++i)
    {
        memcpy(tmp, b + i * size, size);
        size_t j = i;
        while (j > 0 && cmp(ctx, b + (j - 1) * size, tmp) > 0)
        {
            memcpy(b + j * size, b + (j - 1) * size, size);
            --j;
        }
        memcpy(b + j * size, tmp, size);
    }
}

void OutputDebugStringA(LPCSTR s) { printf("%s\n",s); }

void PostThreadMessageA(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)tid;
    (void)msg;
    (void)wp;
    (void)lp;
}

// ---- IMM (Linux: XIM bridge; FreeRTOS: no IME) ------------------------------
HIMC ImmCreateContext(void) { return NULL; }
BOOL ImmDestroyContext(HIMC h) { (void)h; return FALSE; }
HIMC ImmGetContext(HWND hWnd) { (void)hWnd; return NULL; }
HIMC ImmAssociateContext(HWND hWnd, HIMC h) { (void)hWnd; (void)h; return NULL; }
BOOL ImmReleaseContext(HWND hWnd, HIMC h) { (void)hWnd; (void)h; return FALSE; }

// ---- cursor / icon / bitmap (Linux: cursoricon.cpp) -------------------------
// 光标不画进 framebuffer（LoadCursorA 走 CursorMgr），所以 DestroyCursor 只需
// 如实拒绝；图标/位图不再是桩：LoadImageA/LoadImageBuf/LoadBitmapA 由
// src/platform/posix/cursoricon.cpp 提供（该文件也编进本端口，SWINX_NO_MMAP
// 只去掉它的 mmap/文件路径，图标走资源 blob）。
BOOL DestroyCursor(HCURSOR h) { (void)h; return FALSE; }

} /* extern "C" */

// ---------------------------------------------------------------------------
// newlib 缺失的 iconv
// ---------------------------------------------------------------------------
/* newlib 的头文件声明了 iconv_open/iconv/iconv_close，但本工具链没有实现它们
 * （不提供桩会在链接期报未定义）。桩的返回值就是「iconv 不可用」这个信号：
 * src/platform/common/codepage.cpp 的 to_unicode/to_mb 看到 isInvalidIconv()
 * 后走 cptable.h 的内置码表 —— 与 Android(bionic)/OHOS(musl) 完全同一条兜底
 * 路径，所以码表转换本身不需要为这个平台保留任何特例代码。
 *
 * 这是工具链/CRT 层的问题，不属于码表转换：真要在这块板上用 iconv，补的是
 * newlib，而不是在这里写第二份转换实现。 */
extern "C" iconv_t iconv_open(const char *, const char *)
{
    return (iconv_t)0; // isInvalidIconv() 认 (iconv_t)0 与 (iconv_t)-1
}

extern "C" size_t iconv(iconv_t, char **, size_t *, char **, size_t *)
{
    return (size_t)-1;
}

extern "C" int iconv_close(iconv_t)
{
    return 0;
}
