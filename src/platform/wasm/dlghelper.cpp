/*
 * swinx/platform/wasm/dlghelper.cpp
 *
 * WASM home of the common-dialog platform hooks.  Unlike src/platform/
 * freertos/dlghelper.cpp, this file must not re-implement ShellExecute or
 * RegisterDragDrop: the wasm build compiles src/platform/posix/shellapi.cpp
 * + shellobj.cpp (Emscripten ships a full libc, so the POSIX implementations
 * link), and duplicate definitions would break the archive.  What remains is
 * the dialog surface a browser page cannot serve -- no native color/font
 * picker, and no *synchronous* file picker -- so those hooks fail explicitly,
 * mirroring src/platform/mobile/dlghelper.cpp.
 *
 * The file picker is the one browser capability worth bridging, but only
 * asynchronously (contract in include/commdlg.h): the change event of
 * <input type=file> is dispatched only after the JS stack has returned, so
 * blocking inside this call would freeze the whole tab.  The page owns the
 * dialog (Module.swinxPickFile) and hands the bytes back through the exported
 * SwinxFilePickDone; we write them into the wasm filesystem so the path given
 * to the callback opens with plain stdio, like every other backend.
 */
#include <windows.h>
#include <commdlg.h>
#include "SwinxUtils.h"
#include <emscripten.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

BOOL SGetOpenFileNameA(LPOPENFILENAMEA, DlgMode)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL SChooseColor(HWND, const COLORREF *, COLORREF *)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

//=============================================================================
// 异步文件选择
//=============================================================================

/* 等待回投的请求。固定槽位而不是容器：进程级状态不要动态初始化（见 swinx AGENTS 的
 * 裸金属约束，这条在 wasm 上同样省事），而重入口按 nId 查表，把槽位号编进 id 就够。
 *
 * 只有一条线程碰得到它：页面的 change 事件处理器在浏览器主线程上直接调进
 * SwinxFilePickDone，所以不加锁。带 -pthread 的构建里，worker 线程调
 * SwinxPickFileAsyncA 会在下面的 EM_ASM_INT 处失败 —— worker 的 JS realm 没有页面
 * 那份 Module，取到的是"宿主钩子不存在"，请求不会被挂到别的线程上。 */
struct SPickSlot
{
    int nId; // 0 = 空闲
    LPOPENFILENAMEA pOf;
    SWINXFILEPICKCALLBACK pfnDone;
    void *pCtx;
};
static SPickSlot g_pickSlots[8];
static int g_nPickSeq;

static const char kPickDir[] = "/tmp/swinx-pick";

/* 浏览器给的文件名只用来拼一个人类可读的 wasm 路径，不是路径契约的一部分，所以分隔符、
 * ".."、控制字符都必须挡掉 —— 否则 fopen 的目标可能跑到别的目录里去。非 ASCII（UTF-8
 * 中文名）原样保留：它的每个字节都 >= 0x80，不含 '/'、'\\' 或 '\0'，没有注入面。 */
static void _pick_safe_name(char *pszOut, int cchOut, const char *pszName)
{
    int n = 0;
    char cPrev = 0;
    for (const char *p = pszName; p != NULL && *p != 0 && n < cchOut - 1; ++p)
    {
        const char c = *p;
        if (c == '.' && cPrev == '.')
            continue; // 压掉 ".."，单个点（扩展名分隔）保留
        if ((unsigned char)c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.')
        {
            pszOut[n++] = c;
        }
        else if (c == ' ')
        {
            pszOut[n++] = '_';
        }
        else
        {
            continue; // '/'、'\\'、':'、控制字符等：丢弃
        }
        cPrev = pszOut[n - 1];
    }
    /* 前导点整段去掉：它只会把落到文件系统里的文件变成隐藏文件，而分隔符和 ".." 已经在
     * 上面处理过，剩下能出现在开头的点没有一种是需要保留的（".." 已被压成 "."）。 */
    int nLead = 0;
    while (nLead < n && pszOut[nLead] == '.')
        ++nLead;
    if (nLead > 0)
    {
        n -= nLead;
        memmove(pszOut, pszOut + nLead, n);
    }
    if (n == 0)
    {
        pszOut[0] = 'f';
        pszOut[1] = 'i';
        pszOut[2] = 'l';
        pszOut[3] = 'e';
        n = 4;
    }
    pszOut[n] = 0;
}

/* 把选中的字节落到 wasm 文件系统里，路径写进 pszPath。返回 0 表示没落地。 */
static int _pick_write_to_fs(char *pszPath, int cchPath, int nId, const unsigned char *pData,
                             int nLen, const char *pszName)
{
    pszPath[0] = 0;
    if (cchPath <= 0)
        return 0;

    char szName[128];
    _pick_safe_name(szName, (int)sizeof(szName), pszName);

    if (mkdir(kPickDir, 0700) != 0 && errno != EEXIST)
        return 0;
    /* 拼出来的路径必须整个放得下：截断了就把一个不存在的路径交给调用方，比失败更糟。 */
    const int nNeed = snprintf(pszPath, cchPath, "%s/%d-%s", kPickDir, nId, szName);
    if (nNeed < 0 || nNeed >= cchPath)
    {
        pszPath[0] = 0;
        return 0;
    }

    FILE *fp = fopen(pszPath, "wb");
    if (fp == NULL)
    {
        pszPath[0] = 0;
        return 0;
    }
    const size_t nWritten = (nLen > 0) ? fwrite(pData, 1, (size_t)nLen, fp) : 0;
    fclose(fp);
    if (nWritten != (size_t)nLen)
    {
        remove(pszPath);
        pszPath[0] = 0;
        return 0;
    }
    return 1;
}

int swinx_pick_file_async(LPOPENFILENAMEA p, DlgMode mode, SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    if (mode != OPEN)
    {
        /* 浏览器侧只有 <input type=file>：SAVE 要把字节写到磁盘上的某个名字（下载既不
         * 回传路径、也不算对话框），FOLDER 要枚举整个目录。这两种都不在这里假装支持。 */
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }

    int iSlot = 0;
    while (iSlot < (int)ARRAYSIZE(g_pickSlots) && g_pickSlots[iSlot].nId != 0)
        ++iSlot;
    if (iSlot == (int)ARRAYSIZE(g_pickSlots))
    {
        /* 有请求发出去之后没等到回投（页面把 input 摘掉却没触发 change 之类）。槽位不
         * 自动回收：宁可让新请求显式失败，也不让它压到别人的回投上。 */
        SetLastError(ERROR_NO_PROC_SLOTS);
        return 0;
    }

    char szAccept[512];
    if (SwinxFilterToAcceptA(p->lpstrFilter, szAccept, (int)sizeof(szAccept)) < 0)
    {
        /* 筛选串装不下，accept 就给不出可信的映射；留空等于"接受全部"，那是静默降级。
         * 错误码由 SwinxFilterToAcceptA 置好，这里直接显式失败。 */
        return 0;
    }

    if (++g_nPickSeq == 0)
        g_nPickSeq = 1; // id 0 表示空闲槽位，跳过
    const int nId = g_nPickSeq;
    g_pickSlots[iSlot].nId = nId;
    g_pickSlots[iSlot].pOf = p;
    g_pickSlots[iSlot].pfnDone = pfnDone;
    g_pickSlots[iSlot].pCtx = pCtx;

    const int nAccepted = EM_ASM_INT({
        if (typeof Module.swinxPickFile !== 'function')
            return 0;
        return Module.swinxPickFile($0, UTF8ToString($1), UTF8ToString($2)) ? 1 : 0;
    }, nId, (int)(uintptr_t)(p->lpstrTitle ? p->lpstrTitle : ""), (int)(uintptr_t)szAccept);

    if (nAccepted == 0)
    {
        /* 宿主没装钩子，或它自己拒绝：与 ChooseFont/SChooseColor 同一口径 —— 显式
         * 失败，不回调、不假装发起过。 */
        memset(&g_pickSlots[iSlot], 0, sizeof(g_pickSlots[iSlot]));
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    return nAccepted;
}

/* 页面把选择结果送回 wasm 的唯一重入口，由宿主程序自己导出（wasm 侧的符号导出表在
 * 各目标的 CMakeLists 里，例如 demos/wasm-demo/CMakeLists.txt 的 `_SwinxFilePickDone`；
 * 没导出时宿主的钩子只能报 "Module._SwinxFilePickDone is not a function"）。
 *
 * nResult：1=选中，0=用户取消，-1=失败。pszName/pData 由 JS 侧 _malloc，本函数只读
 * 不 free —— 与 SwinxClipboardSetBrowserText 同一所有权约定（JS 调用完自己 free）。 */
extern "C" void SwinxFilePickDone(int nId, int nResult, const char *pszName,
                                  const unsigned char *pData, int nLen)
{
    int iSlot = 0;
    while (iSlot < (int)ARRAYSIZE(g_pickSlots) && g_pickSlots[iSlot].nId != nId)
        ++iSlot;
    if (nId == 0 || iSlot == (int)ARRAYSIZE(g_pickSlots))
        return; // 过期或重复的回投：丢掉，绝不回调第二次

    /* 先摘槽位再回调：回调里可以再发一次选择请求，而表里还留着这一条的话，
     * 第二次回投会命中同一个槽位。 */
    LPOPENFILENAMEA pOf = g_pickSlots[iSlot].pOf;
    SWINXFILEPICKCALLBACK pfnDone = g_pickSlots[iSlot].pfnDone;
    void *pCtx = g_pickSlots[iSlot].pCtx;
    memset(&g_pickSlots[iSlot], 0, sizeof(g_pickSlots[iSlot]));

    if (nResult == 1)
    {
        /* 字节先落到 wasm 文件系统，回调给出的路径才能被 fopen —— 这是 wasm 与其它
         * 后端唯一的差别：别处的 lpstrFile 本来就是磁盘上的路径。 */
        if (pData == NULL || nLen < 0 ||
            !_pick_write_to_fs(pOf->lpstrFile, (int)pOf->nMaxFile, nId, pData, nLen, pszName))
        {
            nResult = -1;
        }
    }
    pfnDone(nResult, nResult == 1 ? pOf->lpstrFile : NULL, pCtx);
}
