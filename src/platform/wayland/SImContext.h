#ifndef _SIM_CONTEXT_WAYLAND_H_
#define _SIM_CONTEXT_WAYLAND_H_

#include <unknwn.h>
#include "SUnkImpl.h"

// Windows 约定：一个 IME 上下文最多维护 4 个候选窗口
#ifndef IMC_MAXCANDIDATEWINDOW
#define IMC_MAXCANDIDATEWINDOW 4
#endif

/* Wayland 没有 XIM 的 xic；text-input-v3 的状态在 framebuffer.cpp 的 WlSeat
 * 里，HIMC 这里只保存 Win32 IMM 兼容层需要的窗口/组合窗位置和启停状态。 */
typedef struct _IMContext : SUnkImpl<IUnknown> {
    BOOL fOpen;
    DWORD fdwConversion;
    DWORD fdwSentence;
    POINT ptStatus;
    COMPOSITIONFORM compForm;
    CANDIDATEFORM candForm[IMC_MAXCANDIDATEWINDOW];

    _IMContext()
        : fOpen(FALSE)
        , fdwConversion(IME_CMODE_ALPHANUMERIC)
        , fdwSentence(IME_SMODE_NONE)
    {
        ptStatus.x = ptStatus.y = 0;
        compForm.dwStyle = CFS_DEFAULT;
        compForm.ptCurrentPos.x = compForm.ptCurrentPos.y = 0;
        compForm.rcArea.left = compForm.rcArea.top = 0;
        compForm.rcArea.right = compForm.rcArea.bottom = 0;
        for (int i = 0; i < IMC_MAXCANDIDATEWINDOW; ++i)
        {
            candForm[i].dwIndex = i;
            candForm[i].dwStyle = CFS_DEFAULT;
            candForm[i].ptCurrentPos.x = candForm[i].ptCurrentPos.y = 0;
            candForm[i].rcArea.left = candForm[i].rcArea.top = 0;
            candForm[i].rcArea.right = candForm[i].rcArea.bottom = 0;
        }
    }

    IUNKNOWN_BEGIN(IUnknown)
    IUNKNOWN_END()
} IMContext;

#endif // _SIM_CONTEXT_WAYLAND_H_
