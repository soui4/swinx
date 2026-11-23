#ifndef _SIM_CONTEXT_H_
#define _SIM_CONTEXT_H_

extern "C"{
#include <imclient.h>
#include <encoding.h>
}
#include <unknwn.h>
#include "SUnkImpl.h"

// Windows 约定：一个 IME 上下文最多维护 4 个候选窗口
#ifndef IMC_MAXCANDIDATEWINDOW
#define IMC_MAXCANDIDATEWINDOW 4
#endif

typedef struct _IMContext : SUnkImpl<IUnknown>{

    xcb_xim_t* xim;
    xcb_xic_t xic;

    BOOL fOpen;                     //@cmember IME 是否打开
    DWORD fdwConversion;            //@cmember 转换模式
    DWORD fdwSentence;              //@cmember 句子模式
    POINT ptStatus;                 //@cmember 状态窗口位置
    COMPOSITIONFORM compForm;       //@cmember 组合窗口（输入法光标跟随用）
    CANDIDATEFORM candForm[IMC_MAXCANDIDATEWINDOW]; //@cmember 候选窗口表单

    _IMContext():xim(nullptr),xic(0){
        fOpen = FALSE;
        fdwConversion = IME_CMODE_ALPHANUMERIC;
        fdwSentence = IME_SMODE_NONE;
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
}IMContext;

#endif//_SIM_CONTEXT_H_