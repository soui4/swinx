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
        const char *end = strstr(buf, "\r\n");
        if (end)
        {
            i++;
            buf = end + 2;
        }
        else
        {
            end = strstr(buf, "\n");
            if (end)
            {
                i++;
                buf = end + 1;
            }
            else
            {
                if (buf[0] != 0)
                    i++;
                break;
            }
        }
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
        const char *end = strstr(buf, "\r\n");
        if (!end)
        {
            end = strstr(buf, "\n");
            if (!end)
                return 0;
        }
        i++;
        if (end[0] == 0x0d)
            buf = end + 2;
        else
            buf = end + 1;
    }
    const char *end = strstr(buf, "\r\n");
    if (!end)
    {
        end = strstr(buf, "\n");
        if (!end)
            end = buf + strlen(buf);
    }
    if (!lpszFile)
        return end - buf;
    if (end - buf > cch)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return 0;
    }
    if (end - buf > 8 && strncmp(buf, "file:///", 8) == 0)
    {
        // remove file header
        buf += 7;
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
    while (path && *path)
    {
        if ((*path == '\\' || *path == '/' || *path == ':') && path[1] && path[1] != '\\' && path[1] != '/')
            last_slash = path + 1;
        path = CharNextA(path);
    }

    return (char *)last_slash;
}

wchar_t *WINAPI PathFindFileNameW(const wchar_t *path)
{
    const wchar_t *last_slash = path;

    while (path && *path)
    {
        if ((*path == '\\' || *path == '/' || *path == ':') && path[1] && path[1] != '\\' && path[1] != '/')
            last_slash = path + 1;
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
            if (*path == '\\' || *path == ' ')
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
            if (*path == '\\' || *path == ' ')
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
    if (!path || !*path || IsDBCSLeadByte(*path))
        return TRUE;

    return !(*path == '/');
}

BOOL WINAPI PathIsRelativeW(const wchar_t *path)
{
    if (!path || !*path)
        return TRUE;

    return !(*path == '/');
}

BOOL WINAPI PathFileExistsA(const char *path)
{
    if (!path)
        return FALSE;
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI PathFileExistsW(const wchar_t *path)
{
    if (!path)
        return FALSE;
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
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
    if (*src == '/')
    {
        *dst++ = *src++;
    }
    else if (*src && src[1] == ':')
    {
        /* X:\ */
        *dst++ = *src++;
        *dst++ = *src++;
        if (*src == '/')
            *dst++ = *src++;
    }

    /* Canonicalize the rest of the path */
    while (*src)
    {
        if (*src == '.')
        {
            if (src[1] == '/' && (src == path || src[-1] == '/'))
            {
                src += 2; /* Skip .\ */
            }
            else if (src[1] == '.' && dst != buffer && dst[-1] == '/')
            {
                /* \.. backs up a directory, over the root if it has no \ following X:.
                 * .. is ignored if it would remove a UNC server name or initial /
                 */
                if (dst != buffer)
                {
                    *dst = '\0'; /* Allow PathIsUNCServerShareA test on lpszBuf */
                    while (dst > buffer && *dst != '/')
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
        else
            *dst++ = *src++;
    }

    /* Append \ to naked drive specs */
    if (dst - buffer == 2 && dst[-1] == ':')
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
    if (path && strchr(path, ' '))
    {
        size_t len = strlen(path) + 1;

        if (len + 2 < MAX_PATH)
        {
            memmove(path + 1, path, len);
            path[0] = '"';
            path[len] = '"';
            path[len + 1] = '\0';
        }
    }
}

void WINAPI PathQuoteSpacesW(wchar_t *path)
{
    if (path && wcschr(path, ' '))
    {
        int len = lstrlenW(path) + 1;

        if (len + 2 < MAX_PATH)
        {
            memmove(path + 1, path, len * sizeof(wchar_t));
            path[0] = '"';
            path[len] = '"';
            path[len + 1] = '\0';
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
        /* Update len */
        if ((!*iter1 || *iter1 == '/') && (!*iter2 || *iter2 == '/'))
            len = iter1 - file1; /* Common to this point */

        if (!*iter1 || *iter1 != *iter2)
            break; /* Strings differ at this point */

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
        /* Update len */
        if ((!*iter1 || *iter1 == '/') && (!*iter2 || *iter2 == '/'))
            len = iter1 - file1; /* Common to this point */

        if (!*iter1 || *iter1 != *iter2)
            break; /* Strings differ at this point */

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
    return prefix && path && PathCommonPrefixA(path, prefix, NULL) == (int)strlen(prefix);
}

BOOL WINAPI PathIsPrefixW(const wchar_t *prefix, const wchar_t *path)
{
    return prefix && path && PathCommonPrefixW(path, prefix, NULL) == (int)lstrlenW(prefix);
}

DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart)
{
    if (PathIsRelativeW(lpFileName))
    {
        GetCurrentDirectoryW(nBufferLength, lpBuffer);
        if (wcslen(lpBuffer) + wcslen(lpFileName) > nBufferLength - 2)
            return 0;
        wcscat(lpBuffer, lpFileName);
    }
    else
    {
        if (wcslen(lpFileName) > nBufferLength)
            return 0;
        wcscpy(lpBuffer, lpFileName);
    }
    wchar_t *tmp = wcsdup(lpBuffer);
    PathCanonicalizeW(lpBuffer, tmp);
    free(tmp);
    if (lpFilePart)
    {
        *lpFilePart = PathFindFileNameW(lpBuffer);
    }
    return wcslen(lpBuffer);
}

DWORD WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
    if (PathIsRelativeA(lpFileName))
    {
        GetCurrentDirectoryA(nBufferLength, lpBuffer);
        if (strlen(lpBuffer) + strlen(lpFileName) > nBufferLength - 2)
            return 0;
        strcat(lpBuffer, lpFileName);
    }
    else
    {
        if (strlen(lpFileName) > nBufferLength)
            return 0;
        strcpy(lpBuffer, lpFileName);
    }
    char *tmp = strdup(lpBuffer);
    PathCanonicalizeA(lpBuffer, tmp);
    free(tmp);
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
BOOL WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    if (!lpOperation || stricmp(lpOperation, "open") != 0)
        return FALSE;
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
        int ret = system(cmd);
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
