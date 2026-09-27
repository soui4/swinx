#ifndef _SIM_CONTEXT_H_
#define _SIM_CONTEXT_H_

#include <unknwn.h>
#include "SUnkImpl.h"

// Windows 约定：一个 IME 上下文最多维护 4 个候选窗口
#ifndef IMC_MAXCANDIDATEWINDOW
#define IMC_MAXCANDIDATEWINDOW 4
#endif

// swinx 的 HIMC：保存与窗口关联的输入法上下文状态（候选窗/组合窗位置、
// 开关状态、转换模式等），供 ImmGet/SetCandidateWindow 等 API 读写。
// 注意：本头文件依赖 <windows.h> / <imm.h> 的类型（POINT、RECT、
// COMPOSITIONFORM、CANDIDATEFORM 及 IME_CMODE_* / IME_SMODE_* / CFS_* 常量），
// 使用方（imm.mm / imm.cpp）须在包含本文件前先包含这两个头。
typedef struct _IMContext : SUnkImpl<IUnknown>{

    BOOL fOpen;                     //@cmember IME 是否打开
    DWORD fdwConversion;            //@cmember 转换模式
    DWORD fdwSentence;              //@cmember 句子模式
    POINT ptStatus;                 //@cmember 状态窗口位置
    COMPOSITIONFORM compForm;       //@cmember 组合窗口（输入法光标跟随用）
    CANDIDATEFORM candForm[IMC_MAXCANDIDATEWINDOW]; //@cmember 候选窗口表单

    _IMContext(){
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
