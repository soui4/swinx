/*
 * swinx/platform/wasm/audio.cpp
 *
 * PlaySound 的 wasm 实现（声明在 include/mmsystem.h，调用方是
 * utilities/audio/SPlaySound_unix.cpp → ScriptModule-LUA 的 PlaySound 导出）。
 *
 * 浏览器里唯一可行的发声通道是 WebAudio，而 AudioContext 只能由宿主页面持有：
 * autoplay 策略要求它在用户手势里 resume()，swinx 既没有 DOM 上下文，也无从得知
 * 下一次手势何时到来（同一取舍见 platform/wasm/utils.cpp 的 MessageBeep）。所以
 * 这里只做三件事——判定 flags、把 wav 从 wasm 文件系统读出来、把字节连同路径交给
 * 页面实现的 Module.swinxAudioPlay（见 soui-wasm/soui.html）——解码与播放都在页面
 * 侧。宿主没装这个钩子时返回 FALSE：与补齐前"显式不支持"的行为一致，不假装入队。
 *
 * 与其它后端的差别：
 *   - 只支持 SND_FILENAME。SND_MEMORY / SND_RESOURCE / SND_ALIAS 直接 FALSE，
 *     与 platform/posix/mmsystem.cpp 同一口径。
 *   - 一律异步：浏览器里不存在"阻塞到播完"的语义，SND_SYNC 按 SND_ASYNC 处理。
 *   - SND_LOOP / SND_NOSTOP / SND_PURGE 原样转发给页面侧；停止请求（pszSound 为
 *     NULL/空串，或带 SND_PURGE）走同一个钩子的 ptr == 0 形态。
 *   - 返回值只表达"请求已交给页面"，与 Win32 的"声音已入队即 TRUE"一致；解码失败
 *     发生在 decodeAudioData 的异步回调里，C++ 这一侧看不见。
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <emscripten.h>

/* 把整段文件读进堆。MEMFS 里的预载资源走的就是普通 stdio，不需要 FS 导出到 JS。 */
static unsigned char *readWholeFile(const char *pszPath, int *pLen)
{
    FILE *fp = fopen(pszPath, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    long n = ftell(fp);
    if (n <= 0 || fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)n);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n)
    {
        free(buf);
        return NULL;
    }
    *pLen = (int)n;
    return buf;
}

BOOL PlaySound(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    (void)hmod;

    if (fdwSound & (SND_MEMORY | SND_RESOURCE | SND_ALIAS | SND_ALIAS_ID))
        return FALSE;

    /* 停止请求。SND_PURGE 带文件名时"先停全部再播新的"，与
     * platform/posix/mmsystem.cpp 的 AudioPlayer::play 同序。 */
    if (pszSound == NULL || pszSound[0] == '\0' || (fdwSound & SND_PURGE) != 0)
    {
        /* 没有宿主钩子时本来就没有声音在播，停止本身仍然算成功（Win32 同此）。 */
        EM_ASM({
            if (typeof Module.swinxAudioPlay === 'function')
                Module.swinxAudioPlay(0, 0, $0, 0);
        }, (int)fdwSound);
        if (pszSound == NULL || pszSound[0] == '\0')
            return TRUE;
    }

    int len = 0;
    unsigned char *data = readWholeFile(pszSound, &len);
    if (!data)
        return FALSE; /* 文件不在 wasm 文件系统里：显式失败，不静默 */

    /* 页面侧拷走字节后才返回（EM_ASM 返回后这里就 free），所以路径与长度都要给全；
     * 路径同时充当缓存键与 SND_NOSTOP 的判重键。 */
    const int accepted = EM_ASM_INT({
        if (typeof Module.swinxAudioPlay !== 'function')
            return 0;
        return Module.swinxAudioPlay($0, $1, $2, $3) ? 1 : 0;
    }, (int)(uintptr_t)data, len, (int)fdwSound, (int)(uintptr_t)pszSound);
    free(data);
    return accepted ? TRUE : FALSE;
}
