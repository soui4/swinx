#include <shellapi.h>
#include "tostring.hpp"
#include <mutex>
#include <list>
#include <map>
#include <assert.h>
#include <fnmatch.h>
#include <algorithm>
#include "SConnection.h"
#include "shellapi.h"
#include "SUnkImpl.h"

namespace swinx
{
class CDropFileTarget : public SUnkImpl<IDropTarget> {
    HWND m_hOwner;

  public:
    CDropFileTarget(HWND hOwner)
        : m_hOwner(hOwner)
    {
    }

    virtual ~CDropFileTarget()
    {
    }

    //////////////////////////////////////////////////////////////////////////
    // IDropTarget

    virtual HRESULT STDMETHODCALLTYPE DragEnter(
        /* [unique][in] */ IDataObject *pDataObj,
        /* [in] */ DWORD grfKeyState,
        /* [in] */ POINTL pt,
        /* [out][in] */ DWORD *pdwEffect)
    {
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE DragOver(
        /* [in] */ DWORD grfKeyState,
        /* [in] */ POINTL pt,
        /* [out][in] */ DWORD *pdwEffect)
    {
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE DragLeave(void)
    {
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Drop(
        /* [unique][in] */ IDataObject *pDataObj,
        /* [in] */ DWORD grfKeyState,
        /* [in] */ POINTL pt,
        /* [out][in] */ DWORD *pdwEffect)
    {
        FORMATETC format = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium;
        if (FAILED(pDataObj->GetData(&format, &medium)))
        {
            return S_FALSE;
        }

        HDROP hdrop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        SendMessage(m_hOwner, WM_DROPFILES, (WPARAM)hdrop, 0);
        GlobalUnlock(medium.hGlobal);
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    IUNKNOWN_BEGIN(IDropTarget)
    IUNKNOWN_END()
};

class CDropFileMgr {
  public:
    static CDropFileMgr *instance()
    {
        static CDropFileMgr singleton;
        return &singleton;
    }
    BOOL Register(HWND hWnd, IDropTarget *target)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        if (m_mapTargets.find(hWnd) != m_mapTargets.end())
            return FALSE;
        auto ret = m_mapTargets.insert(std::make_pair(hWnd, target));
        if (ret.second)
        {
            target->AddRef();
        }
        return ret.second;
    }

    BOOL Unregister(HWND hWnd)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        auto it = m_mapTargets.find(hWnd);
        if (it == m_mapTargets.end())
            return FALSE;
        it->second->Release();
        return TRUE;
    }

  private:
    CDropFileMgr()
    {
    }
    ~CDropFileMgr()
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        for (auto &it : m_mapTargets)
        {
            it.second->Release();
        }
        m_mapTargets.clear();
    }

    std::recursive_mutex m_mutex;
    std::map<HWND, IDropTarget *> m_mapTargets;
};

} // namespace swinx

static UINT DragQueryFileSize(HDROP hDrop)
{
    const char *buf = (const char *)hDrop;
    if (!buf)
        return 0;
    UINT i = 0;
    while (buf)
    {
        buf = strstr(buf, "\r\n");
        if (!buf)
            break;
        i++;
        buf+=2;
    }
    return i;
}

UINT WINAPI DragQueryFileA(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPSTR lpszFile, _In_ UINT cch)
{
    if (iFile == -1)
    {
        return DragQueryFileSize(hDrop);
    }
    const char *buf = (const char *)hDrop;
    UINT i = 0;
    while (i < iFile)
    {
        buf = strstr(buf, "\r\n");
        if (!buf)
            return 0;
        i++;
        buf+=2;
    }
    const char *end = strstr(buf, "\r\n");
    if (!end){
        return 0;
    }
    if (!lpszFile)
        return end - buf;
    if (end - buf > cch)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return 0;
    }
    if(end - buf > 8 && strncmp(buf,"file:///",8)==0){
        //remove file header
        buf+=7;
    }
    memcpy(lpszFile, buf, end - buf);
    if (end - buf + 1 <= cch)
    {
        lpszFile[end - buf] = 0;
    }
    return end - buf;
}

UINT WINAPI DragQueryFileW(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPWSTR lpszFile, _In_ UINT cch)
{
    if (iFile == -1)
    {
        return DragQueryFileSize(hDrop);
    }
    UINT len = DragQueryFileA(hDrop, iFile, NULL, 0);
    if (len == 0)
        return 0;
    std::string str;
    str.resize(len);
    DragQueryFileA(hDrop, iFile, (char *)str.c_str(), len);
    int required = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), len, NULL, 0);
    if (!lpszFile)
        return required;
    if (cch < required)
        return 0;
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), len, lpszFile, cch);
    return required;
}

BOOL WINAPI DragQueryPoint(_In_ HDROP hDrop, _Out_ POINT *ppt)
{
    return FALSE;
}

void WINAPI DragFinish(_In_ HDROP hDrop)
{
}

void WINAPI DragAcceptFiles(_In_ HWND hWnd, _In_ BOOL fAccept)
{
    if (fAccept)
    {
        swinx::CDropFileTarget *target = new swinx::CDropFileTarget(hWnd);
        swinx::CDropFileMgr::instance()->Register(hWnd, target);
        target->Release();
    }
    else
    {
        swinx::CDropFileMgr::instance()->Unregister(hWnd);
    }
}

//----------------------------------------------------------------------
static STrayIconMgr *getTrayIconMgr()
{
    return SConnMgr::instance()->getConnection()->GetTrayIconMgr();
}

BOOL WINAPI Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    return getTrayIconMgr()->NotifyIcon(dwMessage, lpData);
}

BOOL WINAPI Shell_NotifyIconW(DWORD dwMessage, PNOTIFYICONDATAW lpData)
{
    NOTIFYICONDATAA dataA;
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpData->szInfo, -1, dataA.szInfo, 256, nullptr, nullptr))
        return FALSE;
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpData->szTip, -1, dataA.szTip, 128, nullptr, nullptr))
        return FALSE;
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpData->szInfoTitle, -1, dataA.szInfoTitle, 64, nullptr, nullptr))
        return FALSE;
    return Shell_NotifyIconA(dwMessage, &dataA);
}

BOOL WINAPI PathMatchSpecExW(LPCWSTR pszFile, LPCWSTR pszSpec, DWORD dwFlags)
{
    std::string strFile, strSpec;
    tostring(pszFile, -1, strFile);
    tostring(pszSpec, -1, strSpec);
    return PathMatchSpecExA(strFile.c_str(), strSpec.c_str(), dwFlags);
}

// 实现 trim 函数
static void str_trim(std::string &str)
{
    // 定义空白字符集合
    const std::string whitespace = " \t\n\r\f\v";
    // 找到第一个非空白字符的位置
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos)
    {
        str = "";
        return; // 如果字符串全是空白字符，返回空字符串
    }

    // 找到最后一个非空白字符的位置
    size_t end = str.find_last_not_of(whitespace);

    // 返回去除首尾空白字符的子字符串
    str = str.substr(start, end - start + 1);
}

static int myfnmatch(LPCSTR pszFile, LPCSTR pszSpec, BOOL bStrip)
{
    std::string strFile(pszFile), strSpec(pszSpec);
    std::transform(strFile.begin(), strFile.end(), strFile.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(strSpec.begin(), strSpec.end(), strSpec.begin(), [](unsigned char c) { return std::tolower(c); });
    if (!bStrip)
        return fnmatch(strSpec.c_str(), strFile.c_str(), FNM_NOESCAPE);
    else
    {
        str_trim(strSpec);
        if (strSpec.empty())
            return FALSE;
        return fnmatch(strSpec.c_str(), strFile.c_str(), FNM_NOESCAPE);
    }
}

BOOL WINAPI PathMatchSpecExA(LPCSTR pszFile, LPCSTR pszSpec, DWORD dwFlags)
{
    if (dwFlags & PMSF_MULTIPLE)
    {
        char *patterns = strdup(pszSpec); // 复制模式字符串
        if (!patterns)
        {
            perror("strdup");
            return FALSE;
        }

        char *token = strtok(patterns, ";"); // 使用分号分隔模式
        while (token)
        {
            if (myfnmatch(pszFile, token, !(dwFlags & PMSF_DONT_STRIP_SPACES)) == 0)
            {
                free(patterns);
                return TRUE; // 匹配成功
            }
            token = strtok(NULL, ";");
        }

        free(patterns);
        return FALSE; // 未匹配
    }
    else
    {
        return myfnmatch(pszFile, pszSpec, !(dwFlags & PMSF_DONT_STRIP_SPACES)) == 0;
    }
}

BOOL WINAPI PathMatchSpecW(LPCWSTR pszFile, LPCWSTR pszSpec)
{
    return PathMatchSpecExW(pszFile, pszSpec, 0);
}

BOOL WINAPI PathMatchSpecA(LPCSTR pszFile, LPCSTR pszSpec)
{
    return PathMatchSpecExA(pszFile, pszSpec, 0);
}

static int is_executable(const char *filename)
{
    struct stat file_stat;

    // 获取文件状态
    if (stat(filename, &file_stat) == -1)
    {
        return -1; // 获取文件状态失败
    }

    // 检查文件权限是否包含执行权限
    if (file_stat.st_mode & S_IXUSR)
    {
        return 1; // 文件对用户可执行
    }
    else if (file_stat.st_mode & S_IXGRP)
    {
        return 2; // 文件对组可执行
    }
    else if (file_stat.st_mode & S_IXOTH)
    {
        return 3; // 文件对其他用户可执行
    }

    return 0; // 文件不可执行
}

BOOL WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    if (!lpOperation || stricmp(lpOperation, "open") != 0)
        return FALSE;
    int exe = is_executable(lpFile);
    if (exe == -1)
        return FALSE;
    if (exe == 0)
    {
        int len = strlen(lpFile);
        char *cmd = new char[len + 10];
        sprintf(cmd, "xdg-open %s", lpFile);
        int ret = system(cmd);
        delete[] cmd;
        return TRUE;
    }
    else
    {
        PROCESS_INFORMATION procInfo = { 0 };
        char *params = lpParameters ? strdup(lpParameters) : NULL;
        BOOL bRet = CreateProcessA(lpFile, params, NULL, NULL, FALSE, 0, NULL, lpDirectory, NULL, &procInfo);
        if (params)
            free(params);
        if (bRet)
        {
            CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
        }
        return bRet;
    }
}

BOOL WINAPI ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    std::string strOp, strFile, strParam, strDir;
    tostring(lpOperation, -1, strOp);
    tostring(lpFile, -1, strFile);
    tostring(lpParameters, -1, strParam);
    tostring(lpDirectory, -1, strDir);
    return ShellExecuteA(hwnd, lpOperation ? strOp.c_str() : NULL, lpFile ? strFile.c_str() : NULL, lpParameters ? strParam.c_str() : NULL, lpDirectory ? strDir.c_str() : NULL, nShowCmd);
}

BOOL WINAPI ShellExecuteExA(LPSHELLEXECUTEINFOA lpExecInfo)
{
    LPCSTR lpOperation = lpExecInfo->lpVerb;
    if (!lpOperation)
        return FALSE;
    UINT_PTR verb = Verb_Unknown;
    if (stricmp(lpOperation, "open") == 0)
        verb = Verb_Open;
    else if (stricmp(lpOperation, "runas") == 0)
        verb = Verb_RunAs;
    if (verb == Verb_Unknown)
        return FALSE;
    LPCSTR lpFile = lpExecInfo->lpFile;
    int exe = is_executable(lpFile);
    if (exe == -1)
        return FALSE;
    if (exe == 0)
    {
        int len = strlen(lpFile);
        char *cmd = new char[len + 10];
        sprintf(cmd, "xdg-open %s", lpFile);
        int ret = system(cmd);
        delete[] cmd;
        return TRUE;
    }
    else
    {
        LPCSTR lpParameters = lpExecInfo->lpParameters;
        PROCESS_INFORMATION procInfo = { 0 };
        char *params = lpParameters ? strdup(lpParameters) : NULL;
        BOOL bRet = CreateProcessAsUserA((HANDLE)verb, lpFile, params, NULL, NULL, FALSE, 0, NULL, lpExecInfo->lpDirectory, NULL, &procInfo);
        if (params)
            free(params);
        if (bRet)
        {
            if (lpExecInfo->fMask & SEE_MASK_NOCLOSEPROCESS)
                lpExecInfo->hProcess = procInfo.hProcess;
            else
                CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
        }
        return bRet;
    }
}

BOOL WINAPI ShellExecuteExW(LPSHELLEXECUTEINFOW lpExecInfo)
{
    SHELLEXECUTEINFOA infoA;
    infoA.cbSize = sizeof(infoA);
    infoA.fMask = lpExecInfo->fMask;
    std::string strVerb, strFile, strParam, strDir;
    tostring(lpExecInfo->lpVerb, -1, strVerb);
    tostring(lpExecInfo->lpFile, -1, strFile);
    tostring(lpExecInfo->lpParameters, -1, strParam);
    tostring(lpExecInfo->lpDirectory, -1, strDir);
    infoA.lpVerb = strVerb.c_str();
    infoA.lpFile = strFile.c_str();
    infoA.lpParameters = strParam.c_str();
    infoA.lpDirectory = strDir.c_str();
    BOOL bRet = ShellExecuteExA(&infoA);
    lpExecInfo->hProcess = infoA.hProcess;
    return bRet;
}
