#include <shellapi.h>
#include "tostring.hpp"
#include <mutex>
#include <list>
#include <map>
#include <assert.h>
#include <fnmatch.h>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <shlobj.h>
#include <fileapi.h>
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
        /* [out][in] */ DWORD *pdwEffect) override
    {
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE DragOver(
        /* [in] */ DWORD grfKeyState,
        /* [in] */ POINTL pt,
        /* [out][in] */ DWORD *pdwEffect) override
    {
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE DragLeave(void) override
    {
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE Drop(
        /* [unique][in] */ IDataObject *pDataObj,
        /* [in] */ DWORD grfKeyState,
        /* [in] */ POINTL pt,
        /* [out][in] */ DWORD *pdwEffect) override
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
        ReleaseStgMedium(&medium);
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

static const wchar_t *getLineEnd(const wchar_t *p, int &nEndLen)
{
    const wchar_t *end = wcsstr(p, L"\r\n");
    nEndLen = 0;
    if (!end)
    {
        end = wcsstr(p, L"\n");
        if (end)
            nEndLen = 1;
    }
    else
    {
        nEndLen = 2;
    }
    return end;
}

static const char *getLineEnd(const char *p, int &nEndLen)
{
    const char *end = strstr(p, "\r\n");
    nEndLen = 0;
    if (!end)
    {
        end = strstr(p, "\n");
        if (end)
            nEndLen = 1;
    }
    else
    {
        nEndLen = 2;
    }
    return end;
}

// 从 DROPFILES 定位到指定索引的 Wide 字符串行，返回行起始指针和长度
static const wchar_t *GetDragQueryLineAt(const wchar_t *buf, UINT iFile, UINT &outLen)
{
    outLen = 0;
    UINT i = 0;
    while (i < iFile)
    {
        int nEndLen = 0;
        const wchar_t *end = getLineEnd(buf, nEndLen);
        if (!end)
            return NULL;
        i++;
        buf = end + nEndLen;
    }

    int nEndLen = 0;
    const wchar_t *end = getLineEnd(buf, nEndLen);
    if (!end)
        end = buf + wcslen(buf);

    outLen = end - buf;
    return buf;
}

// 从 DROPFILES 定位到指定索引的 ANSI 字符串行，返回行起始指针和长度
static const char *GetDragQueryLineAt(const char *buf, UINT iFile, UINT &outLen)
{
    outLen = 0;
    UINT i = 0;
    while (i < iFile)
    {
        int nEndLen = 0;
        const char *end = getLineEnd(buf, nEndLen);
        if (!end)
            return NULL;
        i++;
        buf = end + nEndLen;
    }

    int nEndLen = 0;
    const char *end = getLineEnd(buf, nEndLen);
    if (!end)
        end = buf + strlen(buf);

    outLen = end - buf;
    return buf;
}

static UINT DragQueryFileSize(HDROP hDrop)
{
    DROPFILES *pDropfiles = (DROPFILES *)hDrop;
    if (pDropfiles->fWide)
    {
        const wchar_t *buf = (const wchar_t *)((char *)pDropfiles + pDropfiles->pFiles);
        if (!buf)
            return 0;
        UINT i = 0;
        while (buf && *buf)
        {
            int nEndLen = 0;
            const wchar_t *end = getLineEnd(buf, nEndLen);
            if (end)
            {
                i++;
                buf = end + nEndLen;
            }
            else
            {
                if (buf[0] != 0)
                    i++;
                break;
            }
        }
        return i;
    }
    else
    {
        const char *buf = (const char *)hDrop + sizeof(DROPFILES);
        if (!buf)
            return 0;
        UINT i = 0;
        while (buf && *buf)
        {
            int nEndLen = 0;
            const char *end = getLineEnd(buf, nEndLen);
            if (end)
            {
                i++;
                buf = end + nEndLen;
            }
            else
            {
                if (buf[0] != 0)
                    i++;
                break;
            }
        }
        return i;
    }
}

UINT WINAPI DragQueryFileA(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPSTR lpszFile, _In_ UINT cch)
{
    if (iFile == -1)
    {
        return DragQueryFileSize(hDrop);
    }
    DROPFILES *pDropfiles = (DROPFILES *)hDrop;
    if (pDropfiles->fWide)
    {
        // 处理 Wide 字符数据，需要转换为 ANSI
        const wchar_t *buf = (const wchar_t *)((char *)pDropfiles + pDropfiles->pFiles);
        UINT wideLen = 0;
        const wchar_t *pLine = GetDragQueryLineAt(buf, iFile, wideLen);
        if (!pLine)
            return 0;

        if (!lpszFile)
            return wideLen;

        // 计算转换后的 ANSI 长度
        int ansiLen = WideCharToMultiByte(CP_UTF8, 0, pLine, wideLen, NULL, 0, NULL, NULL);
        if (ansiLen <= 0)
            return 0;

        if (ansiLen > (int)cch)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }

        // 缓冲区足够，执行转码
        int convertedLen = WideCharToMultiByte(CP_UTF8, 0, pLine, wideLen, lpszFile, cch, NULL, NULL);
        if (convertedLen > 0 && convertedLen < (int)cch)
        {
            lpszFile[convertedLen] = 0;
        }
        return convertedLen;
    }
    else
    {
        const char *buf = (char *)pDropfiles + pDropfiles->pFiles;
        UINT lineLen = 0;
        const char *pLine = GetDragQueryLineAt(buf, iFile, lineLen);
        if (!pLine)
            return 0;

        if (!lpszFile)
            return lineLen;
        if ((int)lineLen > (int)cch)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        if (lineLen > 8 && strncmp(pLine, "file:///", 8) == 0)
        {
            // remove file header
            pLine += 7;
            lineLen -= 7;
        }
        memcpy(lpszFile, pLine, lineLen);
        if (lineLen + 1 <= cch)
        {
            lpszFile[lineLen] = 0;
        }
        return lineLen;
    }
}

UINT WINAPI DragQueryFileW(_In_ HDROP hDrop, _In_ UINT iFile, _Out_writes_opt_(cch) LPWSTR lpszFile, _In_ UINT cch)
{
    if (iFile == -1)
    {
        return DragQueryFileSize(hDrop);
    }
    DROPFILES *pDropfiles = (DROPFILES *)hDrop;
    if (pDropfiles->fWide)
    {
        // Wide input → Wide output
        const wchar_t *buf = (const wchar_t *)((char *)pDropfiles + pDropfiles->pFiles);
        UINT lineLen = 0;
        const wchar_t *pLine = GetDragQueryLineAt(buf, iFile, lineLen);
        if (!pLine)
            return 0;
        if (!lpszFile)
            return lineLen;

        // Check buffer size
        if (lineLen > cch)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        // Direct copy - no encoding needed
        wmemcpy(lpszFile, pLine, lineLen);
        if (lineLen + 1 <= cch)
        {
            lpszFile[lineLen] = 0;
        }
        return lineLen;
    }
    else
    {
        // ANSI input → Wide output
        const char *buf = (char *)pDropfiles + pDropfiles->pFiles;
        UINT lineLen = 0;
        const char *pLine = GetDragQueryLineAt(buf, iFile, lineLen);
        if (!pLine)
            return 0;
        if (!lpszFile)
            return lineLen;

        // Query conversion size
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, pLine, lineLen, NULL, 0);
        if (wideLen <= 0)
            return 0;
        if (wideLen > (int)cch)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }

        // Conversion is safe - execute
        int convertedLen = MultiByteToWideChar(CP_UTF8, 0, pLine, lineLen, lpszFile, cch);
        if (convertedLen > 0 && convertedLen < (int)cch)
            lpszFile[convertedLen] = 0;
        return convertedLen;
    }
}

BOOL WINAPI DragQueryPoint(_In_ HDROP hDrop, _Out_ POINT *ppt)
{
    if (!ppt)
        return FALSE;
    DROPFILES *pDropfiles = (DROPFILES *)hDrop;
    *ppt = pDropfiles->pt;
    return TRUE;
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
BOOL WINAPI Shell_NotifyIconA(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    return SConnMgr::instance()->getConnection()->NotifyIcon(dwMessage, lpData);
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

char *WINAPI PathFindFileNameA(const char *path)
{
    const char *last_slash = path;
    if (!path)
        return (char *)path;

    while (*path)
    {
        if (*path == '\\' || *path == '/')
        {
            // 检查是否为路径分隔符，而不是协议中的 //
            if (path[1] && path[1] != '\\' && path[1] != '/')
                last_slash = path + 1;
        }
        path = CharNextA(path);
    }

    return (char *)last_slash;
}

wchar_t *WINAPI PathFindFileNameW(const wchar_t *path)
{
    const wchar_t *last_slash = path;
    if (!path)
        return (wchar_t *)path;

    while (*path)
    {
        if (*path == '\\' || *path == '/')
        {
            // 检查是否为路径分隔符，而不是协议中的 //
            if (path[1] && path[1] != '\\' && path[1] != '/')
                last_slash = path + 1;
        }
        path++;
    }

    return (wchar_t *)last_slash;
}

char *WINAPI PathFindExtensionA(const char *path)
{
    const char *lastpoint = NULL;

    if (path)
    {
        while (*path)
        {
            if (*path == '\\' || *path == '/' || *path == ' ')
                lastpoint = NULL;
            else if (*path == '.')
                lastpoint = path;
            path = CharNextA(path);
        }
    }

    return (LPSTR)(lastpoint ? lastpoint : path);
}

wchar_t *WINAPI PathFindExtensionW(const wchar_t *path)
{
    const wchar_t *lastpoint = NULL;

    if (path)
    {
        while (*path)
        {
            if (*path == '\\' || *path == '/' || *path == ' ')
                lastpoint = NULL;
            else if (*path == '.')
                lastpoint = path;
            path++;
        }
    }

    return (LPWSTR)(lastpoint ? lastpoint : path);
}

BOOL WINAPI PathIsRelativeA(const char *path)
{
    if (!path || !*path)
        return TRUE;

    // 检查是否为绝对路径
    if (*path == '/' || *path == '\\')
        return FALSE;

    // 检查是否为 Drive:\... 格式 (Windows)
    if (path[1] == ':')
        return FALSE;

    return TRUE;
}

BOOL WINAPI PathIsRelativeW(const wchar_t *path)
{
    if (!path || !*path)
        return TRUE;

    // 检查是否为绝对路径
    if (*path == '/' || *path == '\\')
        return FALSE;

    // 检查是否为 Drive:\... 格式 (Windows)
    if (path[1] == ':')
        return FALSE;

    return TRUE;
}

BOOL WINAPI PathCanonicalizeW(wchar_t *buffer, const wchar_t *path)
{
    const wchar_t *src = path;
    wchar_t *dst = buffer;

    if (dst)
        *dst = '\0';

    if (!dst || !path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!*path)
    {
        *buffer++ = '/';
        *buffer = '\0';
        return TRUE;
    }

    /* Copy path root */
    if (*src == '/' || *src == '\\')
    {
        *dst++ = '/';
        src++;
    }
    else if (*src && src[1] == ':')
    {
        /* X:\ */
        *dst++ = *src++;
        *dst++ = *src++;
        if (*src == '/' || *src == '\\')
        {
            *dst++ = '/';
            src++;
        }
    }

    /* Canonicalize the rest of the path */
    while (*src)
    {
        if (*src == '.')
        {
            if (src[1] == '/' || src[1] == '\\')
            {
                // skip ./
                src += 2;
            }
            else if (src[1] == '.' && dst != buffer && (*(dst - 1) == '/' || *(dst - 1) == '\\'))
            {
                /* \.. backs up a directory, over the root if it has no \ following X:.
                 * .. is ignored if it would remove a UNC server name or initial /
                 */
                dst--;
                if (dst != buffer)
                {
                    *dst = '\0'; /* Allow PathIsUNCServerShareA test on lpszBuf */
                    while (dst > buffer && (*(dst - 1) != '/' && *(dst - 1) != '\\'))
                        dst--;
                    if (dst == buffer)
                    {
                        *dst++ = '/';
                        src++;
                    }
                }
                src += 2; /* Skip .. in src path */
            }
            else
                *dst++ = *src++;
        }
        else if (*src == '/' || *src == '\\')
        {
            /* 规范化路径分隔符为 \\ */
            *dst++ = '/';
            src++;
        }
        else
            *dst++ = *src++;
    }

    /* Append \ to naked drive specs */
    if (dst - buffer == 2 && *(dst - 1) == ':')
        *dst++ = '/';
    *dst++ = '\0';
    return TRUE;
}

BOOL WINAPI PathCanonicalizeA(char *buffer, const char *path)
{
    wchar_t pathW[MAX_PATH], bufferW[MAX_PATH];
    BOOL ret;
    int len;

    if (buffer)
        *buffer = '\0';

    if (!buffer || !path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    len = MultiByteToWideChar(CP_UTF8, 0, path, -1, pathW, ARRAYSIZE(pathW));
    if (!len)
        return FALSE;

    ret = PathCanonicalizeW(bufferW, pathW);
    WideCharToMultiByte(CP_UTF8, 0, bufferW, -1, buffer, MAX_PATH, 0, 0);

    return ret;
}

void WINAPI PathQuoteSpacesA(char *path)
{
    if (path)
    {
        // 检查是否包含空格且还未被引号包围
        if (strchr(path, ' ') && (path[0] != '\"'))
        {
            size_t len = strlen(path) + 1;

            if (len + 2 < MAX_PATH)
            {
                memmove(path + 1, path, len);
                path[0] = '\"';
                path[len] = '\"';
                path[len + 1] = '\0';
            }
        }
    }
}

void WINAPI PathQuoteSpacesW(wchar_t *path)
{
    if (path)
    {
        // 检查是否包含空格且还未被引号包围
        if (wcschr(path, ' ') && (path[0] != L'\"'))
        {
            int len = lstrlenW(path) + 1;

            if (len + 2 < MAX_PATH)
            {
                memmove(path + 1, path, len * sizeof(wchar_t));
                path[0] = L'\"';
                path[len] = L'\"';
                path[len + 1] = L'\0';
            }
        }
    }
}

#define PATH_CHAR_CLASS_LETTER      0x00000001
#define PATH_CHAR_CLASS_ASTERIX     0x00000002
#define PATH_CHAR_CLASS_DOT         0x00000004
#define PATH_CHAR_CLASS_BACKSLASH   0x00000008
#define PATH_CHAR_CLASS_COLON       0x00000010
#define PATH_CHAR_CLASS_SEMICOLON   0x00000020
#define PATH_CHAR_CLASS_COMMA       0x00000040
#define PATH_CHAR_CLASS_SPACE       0x00000080
#define PATH_CHAR_CLASS_OTHER_VALID 0x00000100
#define PATH_CHAR_CLASS_DOUBLEQUOTE 0x00000200

#define PATH_CHAR_CLASS_INVALID 0x00000000
#define PATH_CHAR_CLASS_ANY     0xffffffff

static const DWORD path_charclass[] = {
    /* 0x00 */ PATH_CHAR_CLASS_INVALID,     /* 0x01 */ PATH_CHAR_CLASS_INVALID,
    /* 0x02 */ PATH_CHAR_CLASS_INVALID,     /* 0x03 */ PATH_CHAR_CLASS_INVALID,
    /* 0x04 */ PATH_CHAR_CLASS_INVALID,     /* 0x05 */ PATH_CHAR_CLASS_INVALID,
    /* 0x06 */ PATH_CHAR_CLASS_INVALID,     /* 0x07 */ PATH_CHAR_CLASS_INVALID,
    /* 0x08 */ PATH_CHAR_CLASS_INVALID,     /* 0x09 */ PATH_CHAR_CLASS_INVALID,
    /* 0x0a */ PATH_CHAR_CLASS_INVALID,     /* 0x0b */ PATH_CHAR_CLASS_INVALID,
    /* 0x0c */ PATH_CHAR_CLASS_INVALID,     /* 0x0d */ PATH_CHAR_CLASS_INVALID,
    /* 0x0e */ PATH_CHAR_CLASS_INVALID,     /* 0x0f */ PATH_CHAR_CLASS_INVALID,
    /* 0x10 */ PATH_CHAR_CLASS_INVALID,     /* 0x11 */ PATH_CHAR_CLASS_INVALID,
    /* 0x12 */ PATH_CHAR_CLASS_INVALID,     /* 0x13 */ PATH_CHAR_CLASS_INVALID,
    /* 0x14 */ PATH_CHAR_CLASS_INVALID,     /* 0x15 */ PATH_CHAR_CLASS_INVALID,
    /* 0x16 */ PATH_CHAR_CLASS_INVALID,     /* 0x17 */ PATH_CHAR_CLASS_INVALID,
    /* 0x18 */ PATH_CHAR_CLASS_INVALID,     /* 0x19 */ PATH_CHAR_CLASS_INVALID,
    /* 0x1a */ PATH_CHAR_CLASS_INVALID,     /* 0x1b */ PATH_CHAR_CLASS_INVALID,
    /* 0x1c */ PATH_CHAR_CLASS_INVALID,     /* 0x1d */ PATH_CHAR_CLASS_INVALID,
    /* 0x1e */ PATH_CHAR_CLASS_INVALID,     /* 0x1f */ PATH_CHAR_CLASS_INVALID,
    /* ' '  */ PATH_CHAR_CLASS_SPACE,       /* '!'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '"'  */ PATH_CHAR_CLASS_DOUBLEQUOTE, /* '#'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '$'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '%'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '&'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '\'' */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '('  */ PATH_CHAR_CLASS_OTHER_VALID, /* ')'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '*'  */ PATH_CHAR_CLASS_ASTERIX,     /* '+'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* ','  */ PATH_CHAR_CLASS_COMMA,       /* '-'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '.'  */ PATH_CHAR_CLASS_DOT,         /* '/'  */ PATH_CHAR_CLASS_INVALID,
    /* '0'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '1'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '2'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '3'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '4'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '5'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '6'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '7'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '8'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '9'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* ':'  */ PATH_CHAR_CLASS_COLON,       /* ';'  */ PATH_CHAR_CLASS_SEMICOLON,
    /* '<'  */ PATH_CHAR_CLASS_INVALID,     /* '='  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '>'  */ PATH_CHAR_CLASS_INVALID,     /* '?'  */ PATH_CHAR_CLASS_LETTER,
    /* '@'  */ PATH_CHAR_CLASS_OTHER_VALID, /* 'A'  */ PATH_CHAR_CLASS_ANY,
    /* 'B'  */ PATH_CHAR_CLASS_ANY,         /* 'C'  */ PATH_CHAR_CLASS_ANY,
    /* 'D'  */ PATH_CHAR_CLASS_ANY,         /* 'E'  */ PATH_CHAR_CLASS_ANY,
    /* 'F'  */ PATH_CHAR_CLASS_ANY,         /* 'G'  */ PATH_CHAR_CLASS_ANY,
    /* 'H'  */ PATH_CHAR_CLASS_ANY,         /* 'I'  */ PATH_CHAR_CLASS_ANY,
    /* 'J'  */ PATH_CHAR_CLASS_ANY,         /* 'K'  */ PATH_CHAR_CLASS_ANY,
    /* 'L'  */ PATH_CHAR_CLASS_ANY,         /* 'M'  */ PATH_CHAR_CLASS_ANY,
    /* 'N'  */ PATH_CHAR_CLASS_ANY,         /* 'O'  */ PATH_CHAR_CLASS_ANY,
    /* 'P'  */ PATH_CHAR_CLASS_ANY,         /* 'Q'  */ PATH_CHAR_CLASS_ANY,
    /* 'R'  */ PATH_CHAR_CLASS_ANY,         /* 'S'  */ PATH_CHAR_CLASS_ANY,
    /* 'T'  */ PATH_CHAR_CLASS_ANY,         /* 'U'  */ PATH_CHAR_CLASS_ANY,
    /* 'V'  */ PATH_CHAR_CLASS_ANY,         /* 'W'  */ PATH_CHAR_CLASS_ANY,
    /* 'X'  */ PATH_CHAR_CLASS_ANY,         /* 'Y'  */ PATH_CHAR_CLASS_ANY,
    /* 'Z'  */ PATH_CHAR_CLASS_ANY,         /* '['  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '\\' */ PATH_CHAR_CLASS_BACKSLASH,   /* ']'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '^'  */ PATH_CHAR_CLASS_OTHER_VALID, /* '_'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '`'  */ PATH_CHAR_CLASS_OTHER_VALID, /* 'a'  */ PATH_CHAR_CLASS_ANY,
    /* 'b'  */ PATH_CHAR_CLASS_ANY,         /* 'c'  */ PATH_CHAR_CLASS_ANY,
    /* 'd'  */ PATH_CHAR_CLASS_ANY,         /* 'e'  */ PATH_CHAR_CLASS_ANY,
    /* 'f'  */ PATH_CHAR_CLASS_ANY,         /* 'g'  */ PATH_CHAR_CLASS_ANY,
    /* 'h'  */ PATH_CHAR_CLASS_ANY,         /* 'i'  */ PATH_CHAR_CLASS_ANY,
    /* 'j'  */ PATH_CHAR_CLASS_ANY,         /* 'k'  */ PATH_CHAR_CLASS_ANY,
    /* 'l'  */ PATH_CHAR_CLASS_ANY,         /* 'm'  */ PATH_CHAR_CLASS_ANY,
    /* 'n'  */ PATH_CHAR_CLASS_ANY,         /* 'o'  */ PATH_CHAR_CLASS_ANY,
    /* 'p'  */ PATH_CHAR_CLASS_ANY,         /* 'q'  */ PATH_CHAR_CLASS_ANY,
    /* 'r'  */ PATH_CHAR_CLASS_ANY,         /* 's'  */ PATH_CHAR_CLASS_ANY,
    /* 't'  */ PATH_CHAR_CLASS_ANY,         /* 'u'  */ PATH_CHAR_CLASS_ANY,
    /* 'v'  */ PATH_CHAR_CLASS_ANY,         /* 'w'  */ PATH_CHAR_CLASS_ANY,
    /* 'x'  */ PATH_CHAR_CLASS_ANY,         /* 'y'  */ PATH_CHAR_CLASS_ANY,
    /* 'z'  */ PATH_CHAR_CLASS_ANY,         /* '{'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '|'  */ PATH_CHAR_CLASS_INVALID,     /* '}'  */ PATH_CHAR_CLASS_OTHER_VALID,
    /* '~'  */ PATH_CHAR_CLASS_OTHER_VALID
};

BOOL WINAPI PathIsValidCharA(char c, DWORD _class)
{
    if ((unsigned)c > 0x7e)
        return _class & PATH_CHAR_CLASS_OTHER_VALID;

    return _class & path_charclass[(unsigned)c];
}

BOOL WINAPI PathIsValidCharW(wchar_t c, DWORD _class)
{
    if (c > 0x7e)
        return _class & PATH_CHAR_CLASS_OTHER_VALID;

    return _class & path_charclass[c];
}

int WINAPI PathCommonPrefixA(const char *file1, const char *file2, char *path)
{
    const char *iter1 = file1;
    const char *iter2 = file2;
    unsigned int len = 0;

    if (path)
        *path = '\0';

    if (!file1 || !file2)
        return 0;

    for (;;)
    {
        // 更新 len - 在路径分隔符处更新
        if ((!*iter1 || *iter1 == '/' || *iter1 == '\\') && (!*iter2 || *iter2 == '/' || *iter2 == '\\'))
            len = iter1 - file1; // Common to this point

        if (!*iter1 || *iter1 != *iter2)
            break; // Strings differ at this point

        iter1++;
        iter2++;
    }

    if (len && path)
    {
        memcpy(path, file1, len);
        path[len] = '\0';
    }

    return len;
}

int WINAPI PathCommonPrefixW(const wchar_t *file1, const wchar_t *file2, wchar_t *path)
{
    const wchar_t *iter1 = file1;
    const wchar_t *iter2 = file2;
    unsigned int len = 0;

    if (path)
        *path = '\0';

    if (!file1 || !file2)
        return 0;

    for (;;)
    {
        // 更新 len - 在路径分隔符处更新
        if ((!*iter1 || *iter1 == '/' || *iter1 == '\\') && (!*iter2 || *iter2 == '/' || *iter2 == '\\'))
            len = iter1 - file1; // Common to this point

        if (!*iter1 || *iter1 != *iter2)
            break; // Strings differ at this point

        iter1++;
        iter2++;
    }

    if (len && path)
    {
        memcpy(path, file1, len * sizeof(wchar_t));
        path[len] = '\0';
    }

    return len;
}

BOOL WINAPI PathIsPrefixA(const char *prefix, const char *path)
{
    if (!prefix || !path)
        return FALSE;

    size_t prefixLen = strlen(prefix);
    return PathCommonPrefixA(path, prefix, NULL) == (int)prefixLen;
}

BOOL WINAPI PathIsPrefixW(const wchar_t *prefix, const wchar_t *path)
{
    if (!prefix || !path)
        return FALSE;

    size_t prefixLen = lstrlenW(prefix);
    return PathCommonPrefixW(path, prefix, NULL) == (int)prefixLen;
}

DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart)
{
    if (!lpFileName || !lpBuffer)
        return 0;

    if (PathIsRelativeW(lpFileName))
    {
        DWORD dwRet = GetCurrentDirectoryW(nBufferLength, lpBuffer);
        if (dwRet == 0 || dwRet >= nBufferLength)
            return 0;

        // 确保目录以 \ 结尾
        if (lpBuffer[dwRet - 1] != L'\\' && lpBuffer[dwRet - 1] != L'/')
        {
            if (dwRet + 1 >= nBufferLength)
                return 0;
            lpBuffer[dwRet] = L'\\';
            lpBuffer[dwRet + 1] = L'\0';
            dwRet++;
        }

        if (wcslen(lpBuffer) + wcslen(lpFileName) >= nBufferLength)
            return 0;
        wcscat_s(lpBuffer, nBufferLength, lpFileName);
    }
    else
    {
        if (wcslen(lpFileName) >= nBufferLength)
            return 0;
        wcscpy_s(lpBuffer, nBufferLength, lpFileName);
    }

    // 规范化路径
    wchar_t tmpBuffer[MAX_PATH];
    if (!PathCanonicalizeW(tmpBuffer, lpBuffer))
        return 0;

    wcscpy_s(lpBuffer, nBufferLength, tmpBuffer);

    if (lpFilePart)
    {
        *lpFilePart = PathFindFileNameW(lpBuffer);
    }

    return wcslen(lpBuffer);
}

DWORD WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
    if (!lpFileName || !lpBuffer)
        return 0;

    if (PathIsRelativeA(lpFileName))
    {
        DWORD dwRet = GetCurrentDirectoryA(nBufferLength, lpBuffer);
        if (dwRet == 0 || dwRet >= nBufferLength)
            return 0;

        // 确保目录以 \ 结尾
        if (lpBuffer[dwRet - 1] != '\\' && lpBuffer[dwRet - 1] != '/')
        {
            if (dwRet + 1 >= nBufferLength)
                return 0;
            lpBuffer[dwRet] = '/';
            lpBuffer[dwRet + 1] = '\0';
            dwRet++;
        }

        if (strlen(lpBuffer) + strlen(lpFileName) >= nBufferLength)
            return 0;
        strcat_s(lpBuffer, nBufferLength, lpFileName);
    }
    else
    {
        if (strlen(lpFileName) >= nBufferLength)
            return 0;
        strcpy_s(lpBuffer, nBufferLength, lpFileName);
    }

    // 规范化路径
    char tmpBuffer[MAX_PATH];
    if (!PathCanonicalizeA(tmpBuffer, lpBuffer))
        return 0;

    strcpy_s(lpBuffer, nBufferLength, tmpBuffer);

    if (lpFilePart)
    {
        *lpFilePart = PathFindFileNameA(lpBuffer);
    }

    return strlen(lpBuffer);
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

// 更准确判断字符串是否为网址（支持常见域名后缀和IP）
static bool is_probably_url(const char *str)
{
    if (!str)
        return false;
    // 1. 以http/https/ftp/mailto等协议开头
    if (strncmp(str, "http://", 7) == 0 || strncmp(str, "https://", 8) == 0 || strncmp(str, "ftp://", 6) == 0 || strncmp(str, "mailto:", 7) == 0)
        return true;
    // 2. 以www.开头
    if (strncasecmp(str, "www.", 4) == 0)
        return true;
    // 3. 检查是否为常见域名后缀
    const char *dot = strrchr(str, '.');
    if (dot && dot != str)
    {
        static const char *tlds[] = { ".com", ".net", ".org", ".cn", ".gov", ".edu", ".io", ".co", ".dev", ".xyz", ".info", ".me", ".cc", ".tv", ".ai", ".app", ".shop", ".site", ".top", ".club", ".online", ".store", ".tech", ".pro", ".link", ".live", ".news", ".fun", ".work", ".cloud", ".wiki", ".mobi", ".name", ".today", ".space", ".website", ".page", ".life", ".run", ".group", ".vip", ".ltd", ".red", ".blue", ".green", ".pink", ".black", ".gold", ".plus", ".team", ".center", ".company", ".email", ".market", ".press", ".solutions", ".world", ".zone", ".asia", ".biz", ".cat", ".jobs", ".law", ".moda", ".museum", ".tel", ".travel", ".us", ".uk", ".de", ".fr", ".jp", ".kr", ".hk", ".tw", ".sg", ".my", ".au", ".ca", ".es", ".it", ".ru", ".ch", ".se", ".no", ".fi", ".pl", ".tr", ".be", ".at", ".cz", ".sk", ".hu", ".ro", ".bg", ".lt", ".lv", ".ee", ".gr", ".pt", ".ie", ".il",
                                      ".za",  ".mx",  ".ar",  ".br", ".cl",  ".co",  ".pe", ".ve", ".uy",  ".ec",  ".bo",   ".py", ".do", ".cr", ".pa", ".gt",  ".hn",   ".ni",   ".sv",  ".cu",   ".pr",     ".jm",    ".tt",   ".bs",  ".ag",   ".bb",   ".dm",   ".gd",  ".kn",   ".lc",    ".vc",   ".sr",   ".gy",   ".bz",    ".ai",    ".bm",      ".ky",   ".ms",   ".tc",  ".vg",    ".vi",  ".fk",  ".gs",  ".aq",   ".bv",    ".hm",   ".tf",    ".wf",   ".yt",   ".pm",   ".re",     ".tf",      ".mq",    ".gp",     ".bl",    ".mf",        ".sx",    ".cw",   ".bq",   ".aw",  ".an",  ".nl",   ".dk",  ".is",   ".fo",     ".gl",  ".sj",     ".ax", ".by", ".ua", ".md", ".ge", ".am", ".az", ".kg", ".kz", ".tj", ".tm", ".uz", ".af", ".pk", ".bd", ".lk", ".np", ".mv", ".bt", ".ir", ".iq", ".sy", ".jo", ".lb", ".ps", ".kw", ".qa", ".bh", ".om", ".ye", ".sa", ".ae", ".dz", ".eg", ".ly",
                                      ".ma",  ".sd",  ".tn",  ".eh", ".ss",  ".cm",  ".cf", ".td", ".gq",  ".ga",  ".cg",   ".cd", ".ao", ".gw", ".cv", ".st",  ".sc",   ".mg",   ".yt",  ".mu",   ".km",     ".tz",    ".ke",   ".ug",  ".rw",   ".bi",   ".dj",   ".er",  ".et",   ".so",    ".zm",   ".mw",   ".mz",   ".zw",    ".na",    ".bw",      ".sz",   ".ls",   ".sz",  ".zm",    ".zw",  ".ng",  ".gh",  ".ci",   ".sn",    ".ml",   ".bf",    ".ne",   ".tg",   ".bj",   ".sl",     ".lr",      ".gm",    ".gn",     ".gw",    ".mr",        ".sd",    ".ss",   ".cf",   ".td",  ".gq",  ".ga",   ".cg",  ".cd",   ".ao",     ".gw",  ".cv",     ".st", ".sc", ".mg", ".yt", ".mu", ".km", ".tz", ".ke", ".ug", ".rw", ".bi", ".dj", ".er", ".et", ".so", ".zm", ".mw", ".mz", ".zw", ".na", ".bw", ".sz", ".ls", ".sz", ".zm", ".zw", ".ng", ".gh", ".ci", ".sn", ".ml", ".bf", ".ne", ".tg", ".bj",
                                      ".sl",  ".lr",  ".gm",  ".gn", ".gw",  ".mr",  ".sd", ".ss", ".cf",  ".td",  ".gq",   ".ga", ".cg", ".cd", ".ao", ".gw",  ".cv",   ".st",   ".sc",  ".mg",   ".yt",     ".mu",    ".km",   ".tz",  ".ke",   ".ug",   ".rw",   ".bi",  ".dj",   ".er",    ".et",   ".so",   ".zm",   ".mw",    ".mz",    ".zw",      ".na",   ".bw",   ".sz",  ".ls",    ".sz",  ".zm",  ".zw",  ".ng",   ".gh",    ".ci",   ".sn",    ".ml",   ".bf",   ".ne",   ".tg",     ".bj",      ".sl",    ".lr",     ".gm",    ".gn",        ".gw",    ".mr",   ".sd",   ".ss",  ".cf",  ".td",   ".gq",  ".ga",   ".cg",     ".cd",  ".ao",     ".gw", ".cv", ".st", ".sc", ".mg", ".yt", ".mu", ".km", ".tz", ".ke", ".ug", ".rw", ".bi", ".dj", ".er", ".et", ".so", ".zm", ".mw", ".mz", ".zw", ".na", ".bw", ".sz", ".ls", ".sz", ".zm", ".zw", ".ng", ".gh", ".ci", ".sn", ".ml", ".bf", ".ne",
                                      ".tg",  ".bj",  ".sl",  ".lr", ".gm",  ".gn",  ".gw", ".mr", ".sd",  ".ss",  ".cf",   ".td", ".gq", ".ga", ".cg", ".cd",  ".ao",   ".gw",   ".cv",  ".st",   ".sc",     ".mg",    ".yt",   ".mu",  ".km",   ".tz",   ".ke",   ".ug",  ".rw",   ".bi",    ".dj",   ".er",   ".et",   ".so",    ".zm",    ".mw",      ".mz",   ".zw",   ".na",  ".bw",    ".sz",  ".ls",  ".sz",  ".zm",   ".zw",    ".ng",   ".gh",    ".ci",   ".sn",   ".ml",   ".bf",     ".ne",      ".tg",    ".bj",     ".sl",    ".lr",        ".gm",    ".gn",   ".gw",   ".mr",  ".sd",  ".ss",   ".cf",  ".td",   ".gq",     ".ga",  ".cg",     ".cd", ".ao", ".gw", ".cv", ".st", ".sc", ".mg", ".yt", ".mu", ".km", ".tz", ".ke", ".ug", ".rw", ".bi", ".dj", ".er", ".et", ".so", ".zm", ".mw", ".mz", ".zw", ".na", ".bw", ".sz", ".ls", ".sz", ".zm", ".zw", ".ng", ".gh", ".ci", ".sn", ".ml",
                                      ".bf",  ".ne",  ".tg",  ".bj", ".sl",  ".lr",  ".gm", ".gn", ".gw",  ".mr",  ".sd",   ".ss", ".cf", ".td", ".gq", ".ga",  ".cg",   ".cd",   ".ao",  ".gw",   ".cv",     ".st",    ".sc",   ".mg",  ".yt",   ".mu",   ".km",   ".tz",  ".ke",   ".ug",    ".rw",   ".bi",   ".dj",   ".er",    ".et",    ".so",      ".zm",   ".mw",   ".mz",  ".zw",    ".na",  ".bw",  ".sz",  ".ls",   ".sz",    ".zm",   ".zw",    ".ng",   ".gh",   ".ci",   ".sn",     ".ml",      ".bf",    ".ne",     ".tg",    ".bj",        ".sl",    ".lr",   ".gm",   ".gn",  ".gw",  ".mr",   ".sd",  ".ss",   ".cf",     ".td",  ".gq",     ".ga", ".cg", ".cd", ".ao", ".gw", ".cv", ".st", ".sc", ".mg", ".yt", ".mu", ".km", ".tz", ".ke", ".ug", ".rw", ".bi", ".dj", ".er", ".et", ".so", ".zm", ".mw", ".mz", ".zw", ".na", ".bw", ".sz", ".ls", ".sz", ".zm", ".zw" };
        for (size_t i = 0; i < sizeof(tlds) / sizeof(tlds[0]); ++i)
        {
            size_t tldlen = strlen(tlds[i]);
            if (strlen(dot) >= tldlen && strncasecmp(dot, tlds[i], tldlen) == 0)
                return true;
        }
    }
    // 4. 检查是否为IPv4地址
    int d1, d2, d3, d4;
    if (sscanf(str, "%d.%d.%d.%d", &d1, &d2, &d3, &d4) == 4)
    {
        if (d1 >= 0 && d1 <= 255 && d2 >= 0 && d2 <= 255 && d3 >= 0 && d3 <= 255 && d4 >= 0 && d4 <= 255)
            return true;
    }
    return false;
}

static int mysystem(const char *cmd){
#if defined(__IOS__)
    return -1;
#else
    return system(cmd);
#endif
}


BOOL WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    if (!lpOperation || stricmp(lpOperation, "open") != 0)
        return FALSE;

    // 处理资源管理器选中文件/文件夹功能
    if (lpFile && stricmp(lpFile, "explorer.exe") == 0 && lpParameters)
    {
        // 检查是否为 /select, 命令
        const char *selectPrefix = "/select,";
        size_t prefixLen = strlen(selectPrefix);
        if (strncmp(lpParameters, selectPrefix, prefixLen) == 0)
        {
            // 提取要选中的文件路径
            std::string filePath = lpParameters + prefixLen;
            if(filePath.front() == '\"')
            {
                filePath.erase(0, 1);
                if(filePath.back() == '\"')
                    filePath.pop_back();
            }
#ifdef __linux__
            // Linux: 使用 xdg-open 打开文件所在目录
            std::string dirPath = filePath;
            
            size_t lastSlash = dirPath.find_last_of('/');
            if (lastSlash != std::string::npos)
            {
                dirPath = dirPath.substr(0, lastSlash);
            }
            int len = dirPath.length();
            char *cmd = new char[len + 12];
            sprintf(cmd, "xdg-open '%s'", dirPath.c_str());
            int ret = system(cmd);
            delete[] cmd;
            return ret == 0;
#elif defined(__APPLE__)
            // macOS: 使用 open -R 命令选中文件
            char *cmd = new char[filePath.length() + 20];
            sprintf(cmd, "open -R '%s'", filePath.c_str());
            int ret = mysystem(cmd);
            delete[] cmd;
            return (ret == 0);
#else
            return FALSE;
#endif
        }
    }

    // 原有逻辑
    int exe = is_executable(lpFile);
    if (exe <= 0)
    {
        // 检查是否为网址（含.且无本地文件）
        const char *url = lpFile;
        std::string urlBuf;
        if (is_probably_url(lpFile))
        {
            if (strncmp(lpFile, "http://", 7) != 0 && strncmp(lpFile, "https://", 8) != 0)
            {
                urlBuf = "https://";
                urlBuf += lpFile;
                url = urlBuf.c_str();
            }
        }
#ifdef __linux__
        int len = strlen(url);
        char *cmd = new char[len + 12];
        sprintf(cmd, "xdg-open '%s'", url);
        int ret = system(cmd);
        delete[] cmd;
        return ret == 0;
#elif defined(__APPLE__)
        int len = strlen(url);
        char *cmd = new char[len + 10];
        sprintf(cmd, "open '%s'", url);
        int ret = mysystem(cmd);
        delete[] cmd;
        return (ret == 0);
#else
        return FALSE;
#endif
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
        int ret = mysystem(cmd);
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

HRESULT SHCreateStreamOnFileA(LPCSTR pszFile, DWORD grfMode, IStream **ppstm)
{
    return SHCreateStreamOnFileExA(pszFile, grfMode, 0, FALSE, nullptr, ppstm);
}
HRESULT SHCreateStreamOnFileW(LPCWSTR pszFile, DWORD grfMode, IStream **ppstm)
{
    std::string strFile;
    tostring(pszFile, -1, strFile);
    return SHCreateStreamOnFileA(strFile.c_str(), grfMode, ppstm);
}

HRESULT SHCreateStreamOnFileExW(LPCWSTR pszFile, DWORD grfMode, DWORD dwAttributes, BOOL fCreate, IStream *pstmTemplate, IStream **ppstm)
{
    std::string strFile;
    tostring(pszFile, -1, strFile);
    return SHCreateStreamOnFileExA(strFile.c_str(), grfMode, dwAttributes, fCreate, pstmTemplate, ppstm);
}

// Custom IStream implementation for file operations
class FileStream : public SUnkImpl<IStream> {
  private:
    FILE *m_file;
    DWORD m_grfMode;

  public:
    FileStream(FILE *file, DWORD grfMode)
        : m_file(file)
        , m_grfMode(grfMode)
    {
    }

  public:
    IUNKNOWN_BEGIN(IStream)
        IUNKNOWN_ADD_IID(ISequentialStream)
    IUNKNOWN_END()
    void OnFinalRelease() override
    {
        if (m_file)
        {
            fclose(m_file);
            m_file = nullptr;
        }
        delete this;
    }

  public:
    // ISequentialStream methods
    STDMETHOD(Read)(void *pv, ULONG cb, ULONG *pcbRead) override
    {
        if (!m_file)
            return STG_E_ACCESSDENIED;

        size_t bytesRead = fread(pv, 1, cb, m_file);
        if (pcbRead)
            *pcbRead = (bytesRead > 0 || cb == 0) ? (ULONG)bytesRead : 0;

        if (bytesRead < cb && ferror(m_file))
            return E_FAIL;

        return S_OK;
    }

    STDMETHOD(Write)(const void *pv, ULONG cb, ULONG *pcbWritten) override
    {
        if (!m_file)
            return STG_E_ACCESSDENIED;

        if (!(m_grfMode & STGM_WRITE))
            return STG_E_ACCESSDENIED;

        size_t bytesWritten = fwrite(pv, 1, cb, m_file);
        if (pcbWritten)
            *pcbWritten = (ULONG)bytesWritten;

        if (bytesWritten < cb)
            return E_FAIL;

        return S_OK;
    }

    // IStream methods
    STDMETHOD(Seek)(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) override
    {
        if (!m_file)
            return STG_E_ACCESSDENIED;

        int origin;
        switch (dwOrigin)
        {
        case STREAM_SEEK_SET:
            origin = SEEK_SET;
            break;
        case STREAM_SEEK_CUR:
            origin = SEEK_CUR;
            break;
        case STREAM_SEEK_END:
            origin = SEEK_END;
            break;
        default:
            return STG_E_INVALIDFUNCTION;
        }

        if (fseek(m_file, dlibMove.QuadPart, origin) != 0)
            return E_FAIL;

        if (plibNewPosition)
        {
            long pos = ftell(m_file);
            if (pos < 0)
                return E_FAIL;
            plibNewPosition->QuadPart = pos;
        }

        return S_OK;
    }

    STDMETHOD(SetSize)(ULARGE_INTEGER libNewSize) override
    {
        if (!m_file)
            return STG_E_ACCESSDENIED;

        long oldPos = ftell(m_file);
        if (oldPos < 0)
            return E_FAIL;

        if (fseek(m_file, libNewSize.QuadPart, SEEK_SET) != 0)
            return E_FAIL;

        if (ftruncate(fileno(m_file), libNewSize.QuadPart) != 0)
            return E_FAIL;

        if (fseek(m_file, oldPos, SEEK_SET) != 0)
            return E_FAIL;

        return S_OK;
    }

    STDMETHOD(CopyTo)(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(Commit)(DWORD grfCommitFlags) override
    {
        if (!m_file)
            return STG_E_ACCESSDENIED;

        if (fflush(m_file) != 0)
            return E_FAIL;

        return S_OK;
    }

    STDMETHOD(Revert)() override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(LockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(UnlockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override
    {
        return E_NOTIMPL;
    }

    STDMETHOD(Stat)(STATSTG *pstatstg, DWORD grfStatFlag) override
    {
        if (!pstatstg)
            return E_INVALIDARG;

        memset(pstatstg, 0, sizeof(STATSTG));

        if (!(grfStatFlag & STATFLAG_NONAME))
        {
            // We don't fill the name as it's not essential for basic operation
            pstatstg->pwcsName = nullptr;
        }

        pstatstg->type = STGTY_STREAM;

        // Get file size
        if (m_file)
        {
            long oldPos = ftell(m_file);
            if (fseek(m_file, 0, SEEK_END) == 0)
            {
                long fileSize = ftell(m_file);
                if (fileSize >= 0)
                    pstatstg->cbSize.QuadPart = fileSize;
                fseek(m_file, oldPos, SEEK_SET);
            }
        }

        pstatstg->grfMode = m_grfMode;

        return S_OK;
    }

    STDMETHOD(Clone)(IStream **ppstm) override
    {
        return E_NOTIMPL;
    }
};

HRESULT SHCreateStreamOnFileExA(LPCSTR pszFile, DWORD grfMode, DWORD dwAttributes, BOOL fCreate, IStream *pstmTemplate, IStream **ppstm)
{
    if (!pszFile || !ppstm)
        return E_INVALIDARG;

    *ppstm = nullptr;

    if (pstmTemplate)
        return E_NOTIMPL; // We don't support template streams

    const char *mode = "";
    bool canRead = true;
    bool canWrite = (grfMode & STGM_WRITE) != 0;

    // Determine file access mode
    if (canRead && canWrite)
    {
        mode = fCreate ? "w+b" : "r+b";
    }
    else if (canRead)
    {
        mode = "rb";
    }
    else if (canWrite)
    {
        mode = fCreate ? "wb" : "r+b";
    }
    else
    {
        return STG_E_ACCESSDENIED;
    }

    FILE *file = fopen(pszFile, mode);

    // If we couldn't open for read+write and we're not creating, try opening for read-only
    if (!file && !fCreate && canRead && canWrite)
    {
        file = fopen(pszFile, "rb");
    }

    // If still no file and we're supposed to create it
    if (!file && fCreate)
    {
        file = fopen(pszFile, mode);
    }

    if (!file)
    {
        // Try to determine the appropriate error code
        if (errno == ENOENT)
            return STG_E_FILENOTFOUND;
        else if (errno == EACCES)
            return STG_E_ACCESSDENIED;
        else
            return E_FAIL;
    }
    // Create our stream implementation
    FileStream *stream = new FileStream(file, grfMode);
    if (!stream)
    {
        fclose(file);
        return E_OUTOFMEMORY;
    }

    *ppstm = static_cast<IStream *>(stream);
    return S_OK;
}

//----------------------------------------------------------------------
// SHFileOperationA helpers
//----------------------------------------------------------------------

// 检查路径是否包含通配符
static bool path_has_wildcard(const char *path)
{
    return path && (strchr(path, '*') || strchr(path, '?'));
}

// 展开通配符为实际文件列表
static std::vector<std::string> expand_wildcard(const std::string &path, bool filesOnly)
{
    std::vector<std::string> result;
    if (!path_has_wildcard(path.c_str()))
    {
        result.push_back(path);
        return result;
    }

    // 分离目录和通配模式
    std::string dir, pattern;
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos)
    {
        dir = path.substr(0, lastSlash);
        pattern = path.substr(lastSlash + 1);
    }
    else
    {
        dir = ".";
        pattern = path;
    }
    if (dir.empty())
        dir = "/";

    DIR *d = opendir(dir.c_str());
    if (!d)
        return result;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (fnmatch(pattern.c_str(), entry->d_name, 0) != 0)
            continue;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir.c_str(), entry->d_name);

        if (filesOnly)
        {
            struct stat st;
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                continue; // FOF_FILESONLY: 跳过目录
        }
        result.push_back(full);
    }
    closedir(d);
    return result;
}

// 从路径提取文件名
static std::string get_base_name(const std::string &path)
{
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos)
        lastSlash = path.find_last_of('\\');
    if (lastSlash != std::string::npos)
        return path.substr(lastSlash + 1);
    return path;
}

// 检查是否为目录
static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// 生成避免冲突的新文件名（Windows 风格 "Copy of X" / "Copy 2 of X"）
static std::string generate_collision_name(const std::string &destPath)
{
    std::string dir, name;
    size_t lastSlash = destPath.find_last_of('/');
    if (lastSlash != std::string::npos)
    {
        dir = destPath.substr(0, lastSlash);
        name = destPath.substr(lastSlash + 1);
    }
    else
    {
        dir = ".";
        name = destPath;
    }

    for (int count = 2;; ++count)
    {
        std::string newName;
        if (count == 2)
            newName = "Copy of " + name;
        else
            newName = "Copy " + std::to_string(count) + " of " + name;

        std::string fullPath = dir + "/" + newName;
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0)
            return fullPath; // 不存在，可以使用
    }
}

// 复制文件或目录（目录时递归）
static bool copy_path(const std::string &from, const std::string &to)
{
    if (is_dir(from.c_str()))
        return CopyDirA(from.c_str(), to.c_str()) == 0;

    FILE *src = fopen(from.c_str(), "rb");
    if (!src)
        return false;
    FILE *dst = fopen(to.c_str(), "wb");
    if (!dst)
    {
        fclose(src);
        return false;
    }
    char buffer[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, n, dst) != n)
        {
            ok = false;
            break;
        }
    }
    fclose(src);
    fclose(dst);
    return ok;
}

// 删除文件或目录（目录时递归），支持回收站
static bool delete_path(const std::string &path, bool allowUndo)
{
    return DelDirA(path.c_str(), allowUndo) == 0;
}

// 移动文件或目录（rename 失败则复制+删除）
static bool move_path(const std::string &from, const std::string &to)
{
    if (rename(from.c_str(), to.c_str()) == 0)
        return true;
    // rename 失败（跨文件系统等），回退到复制+删除
    if (copy_path(from, to))
    {
        return delete_path(from, false);
    }
    return false;
}

// 确保目标路径完整（如果目标为已存在的目录，将源文件名追加到目标路径后）
static std::string resolve_dest(const std::string &from, const std::string &to)
{
    if (is_dir(to.c_str()))
    {
        return to + "/" + get_base_name(from);
    }
    return to;
}

int WINAPI SHFileOperationA(LPSHFILEOPSTRUCTA lpFileOp)
{
    if (!lpFileOp)
        return ERROR_INVALID_PARAMETER;

    DWORD fFlags = lpFileOp->fFlags;
    bool bAllowUndo = (fFlags & FOF_ALLOWUNDO) != 0;
    bool bMultiDest = (fFlags & FOF_MULTIDESTFILES) != 0;
    bool bRenameOnCollision = (fFlags & FOF_RENAMEONCOLLISION) != 0;
    bool bFilesOnly = (fFlags & FOF_FILESONLY) != 0;
    bool bNoRecursion = (fFlags & FOF_NORECURSION) != 0;

    BOOL anyAborted = FALSE;
    int result = 0;

    // 解析双 NULL 结尾的源路径列表
    std::vector<std::string> rawFrom;
    if (lpFileOp->pFrom)
    {
        const char *p = lpFileOp->pFrom;
        while (*p)
        {
            rawFrom.push_back(p);
            p += strlen(p) + 1;
        }
    }

    // 解析双 NULL 结尾的目标路径列表
    std::vector<std::string> toPaths;
    if (lpFileOp->pTo)
    {
        const char *p = lpFileOp->pTo;
        while (*p)
        {
            toPaths.push_back(p);
            p += strlen(p) + 1;
        }
    }

    if (rawFrom.empty())
        return ERROR_INVALID_PARAMETER;

    // 展开通配符，合并为最终源路径列表
    std::vector<std::string> fromPaths;
    for (const auto &raw : rawFrom)
    {
        auto expanded = expand_wildcard(raw, bFilesOnly);
        for (auto &e : expanded)
            fromPaths.push_back(std::move(e));
    }

    switch (lpFileOp->wFunc)
    {
    case FO_DELETE:
    {
        for (const auto &path : fromPaths)
        {
            if (!delete_path(path, bAllowUndo))
                anyAborted = TRUE;
        }
    }
    break;

    case FO_COPY:
    case FO_MOVE:
    {
        for (size_t i = 0; i < fromPaths.size(); ++i)
        {
            const std::string &from = fromPaths[i];

            // 确定目标路径
            std::string to;
            if (bMultiDest && i < toPaths.size())
            {
                // 多目标模式：一一对应
                to = toPaths[i];
            }
            else if (!toPaths.empty())
            {
                // 单目标模式：全部放到第一个目标
                to = toPaths[0];
                // 如果目标是已存在的目录，追加文件名
                to = resolve_dest(from, to);
            }
            else
            {
                anyAborted = TRUE;
                continue;
            }

            // 处理目标已存在
            struct stat st;
            if (stat(to.c_str(), &st) == 0)
            {
                if (bRenameOnCollision)
                {
                    // 自动重命名以避免冲突
                    to = generate_collision_name(to);
                }
                else if (S_ISDIR(st.st_mode))
                {
                    // 目标是已存在目录：将源放入目录内
                    to = to + "/" + get_base_name(from);
                }
                // 如果目标文件已存在且未设 RENAMEONCOLLISION，覆盖
            }

            bool ok = false;
            if (lpFileOp->wFunc == FO_COPY)
                ok = copy_path(from, to);
            else // FO_MOVE
                ok = move_path(from, to);

            if (!ok)
                anyAborted = TRUE;
        }
    }
    break;

    case FO_RENAME:
    {
        if (toPaths.empty())
            return ERROR_INVALID_PARAMETER;

        size_t count = std::min(fromPaths.size(), toPaths.size());
        for (size_t i = 0; i < count; ++i)
        {
            std::string to = toPaths[i];

            // 处理目标冲突
            struct stat st;
            if (stat(to.c_str(), &st) == 0)
            {
                if (bRenameOnCollision)
                {
                    to = generate_collision_name(to);
                }
            }

            if (rename(fromPaths[i].c_str(), to.c_str()) != 0)
                anyAborted = TRUE;
        }
    }
    break;

    default:
        return ERROR_INVALID_PARAMETER;
    }

    lpFileOp->fAnyOperationsAborted = anyAborted;
    return result;
}
int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW lpFileOp)
{
    SHFILEOPSTRUCTA op;
    op.hwnd = lpFileOp->hwnd;
    op.wFunc = lpFileOp->wFunc;
    std::string strFrom, strTo, strTitle;
    tostring_filter(lpFileOp->pFrom, strFrom);
    tostring_filter(lpFileOp->pTo, strTo);
    tostring(lpFileOp->lpszProgressTitle, -1, strTitle);
    op.pFrom = strFrom.c_str();
    op.pTo = strTo.c_str();
    op.fFlags = lpFileOp->fFlags;
    op.fAnyOperationsAborted = lpFileOp->fAnyOperationsAborted;
    op.hNameMappings = lpFileOp->hNameMappings;
    op.lpszProgressTitle = strTitle.c_str();
    ;
    return SHFileOperationA(&op);
}
