#include <windows.h>
#include "SwinxUtils.h"
#include "platform_api.h"

// Windows MessageBeep 在移动端（Android / OHOS）的实现：转发给宿主应用的平台回调。
//
// 移动端直接发声要靠 Java 的 ToneGenerator/RingtoneManager 或 ArkTS 的 AudioRenderer，
// 而 swinx 侧既没有 JNI 通道（拿不到 JavaVM）也没有 N-API 通道（拿不到 ArkTS 上下文），
// 所以与 PlaySound / ShowSoftKeyboard 一样，走 g_platformAPI 回调宿主应用层：
//     Android   AndroidPlatformAPI::messageBeep → SouiPlatformBridge.messageBeep
//     OHOS      OhosPlatformAPI::messageBeep    → SouiPlatformBridge.messageBeep
// 回调未注册（例如应用未调用 PlatformAPI_Init）时返回 FALSE，与 Win32 的失败语义一致。
//
// 保持"同名同签名、由链接器解析"的单一形态（见 SwinxUtils.h），因此这里不按平台
// #ifdef 分支——Android 与 OHOS 共用本文件，差异全部由应用层的回调吸收。
int swinx_messageBeep(UINT uType)
{
    if (g_platformAPI.audio.messageBeep)
        return g_platformAPI.audio.messageBeep(uType);
    return FALSE;
}
