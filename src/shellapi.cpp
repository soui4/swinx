#include <shellapi.h>
#include "tostring.hpp"
#include <mutex>
#include <list>
#include <map>
#include <assert.h>
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
    UINT i = 1;
    while (buf)
    {
        buf = strchr(buf, '\n');
        if (!buf)
            break;
        i++;
        buf++;
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
        buf = strchr(buf, '\n');
        if (!buf)
            return 0;
        i++;
        buf++;
    }
    const char *end = strchr(buf, '\n');
    if (!end)
        end = buf + strlen(buf);
    if (!lpszFile)
        return end - buf;
    if (end - buf > cch)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return 0;
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
