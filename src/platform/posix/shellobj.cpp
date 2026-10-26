#include <shlobj.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "enumformatetc.h"
#include "tostring.h"
#include "platform_api.h"
HRESULT SHCreateStdEnumFmtEtc(UINT cfmt, const FORMATETC afmt[], IEnumFORMATETC **ppenumFormatEtc)
{
    *ppenumFormatEtc = new SEnumFormatEtc(cfmt, afmt);
    if (!(*ppenumFormatEtc))
        return E_OUTOFMEMORY;
    return S_OK;
}

#if defined(__IOS__)
// 由 platform/ios/utils.mm 实现，基于 NSSearchPathForDirectoriesInDomains
// 映射 CSIDL_* 常量到 iOS 沙盒目录（NSCachesDirectory/NSDocumentDirectory 等）
BOOL swinx_iOSSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate);
#endif

BOOL WINAPI SHGetSpecialFolderPathA(HWND hwndOwner __attribute__((unused)), LPSTR lpszPath, int nFolder, BOOL fCreate)
{
#if defined(__IOS__)
    // iOS：委托 utils.mm，基于 NSSearchPathForDirectoriesInDomains 获取沙盒目录
    return swinx_iOSSpecialFolderPathA(hwndOwner, lpszPath, nFolder, fCreate);
#elif defined(__ANDROID__) || defined(__OHOS__)
    // Android：优先委托平台层（Java 层 getCacheDir/getFilesDir 等）
    if (g_platformAPI.path.getSpecialFolderPathA)
        return g_platformAPI.path.getSpecialFolderPathA(hwndOwner, lpszPath, nFolder, fCreate);
    return FALSE;
#else
    const char *homeDir = getenv("HOME");
    if (!homeDir)
    {
        return FALSE; // HOME environment variable is required
    }

#ifdef __APPLE__
    // macOS specific paths using ~/Library structure
    if (nFolder == CSIDL_MYMUSIC)
    {
        sprintf(lpszPath, "%s/Music", homeDir);
    }
    else if (nFolder == CSIDL_MYVIDEO)
    {
        sprintf(lpszPath, "%s/Movies", homeDir);
    }
    else if (nFolder == CSIDL_MYDOCUMENTS || nFolder == CSIDL_PERSONAL)
    {
        sprintf(lpszPath, "%s/Documents", homeDir);
    }
    else if (nFolder == CSIDL_APPDATA)
    {
        // macOS uses ~/Library/Application Support for app data
        sprintf(lpszPath, "%s/Library/Application Support", homeDir);
    }
    else if (nFolder == CSIDL_DESKTOP)
    {
        sprintf(lpszPath, "%s/Desktop", homeDir);
    }
    else if (nFolder == CSIDL_MYPICTURES)
    {
        sprintf(lpszPath, "%s/Pictures", homeDir);
    }
    else if (nFolder == CSIDL_DESKTOPDIRECTORY)
    {
        sprintf(lpszPath, "%s/Desktop", homeDir);
    }
    else if (nFolder == CSIDL_FAVORITES)
    {
        sprintf(lpszPath, "%s/Favorites", homeDir);
    }
    else if (nFolder == CSIDL_STARTUP)
    {
        sprintf(lpszPath, "%s/Library/LaunchAgents", homeDir);
    }
    else if (nFolder == CSIDL_RECENT)
    {
        sprintf(lpszPath, "%s/Library/Preferences", homeDir);
    }
    else if (nFolder == CSIDL_SENDTO)
    {
        sprintf(lpszPath, "%s/Library/Services", homeDir);
    }
    else if (nFolder == CSIDL_STARTMENU)
    {
        sprintf(lpszPath, "%s/Applications", homeDir);
    }
    else if (nFolder == CSIDL_PROGRAMS)
    {
        sprintf(lpszPath, "%s/Applications", homeDir);
    }
    else if (nFolder == CSIDL_TEMPLATES)
    {
        sprintf(lpszPath, "%s/Documents/Templates", homeDir);
    }
    else if (nFolder == CSIDL_INTERNET)
    {
        sprintf(lpszPath, "%s/Library/Safari", homeDir);
    }
    else if (nFolder == CSIDL_FONTS)
    {
        sprintf(lpszPath, "%s/Library/Fonts", homeDir);
    }
    else
    {
        // Default fallback
        strcpy(lpszPath, homeDir);
        return FALSE;
    }
#else
    // Linux implementation using XDG Base Directory Specification
    const char *xdgDocuments = getenv("XDG_DOCUMENTS_DIR");
    const char *xdgDesktop = getenv("XDG_DESKTOP_DIR");
    const char *xdgData = getenv("XDG_DATA_HOME");
    const char *xdgCache = getenv("XDG_CACHE_HOME");
    const char *xdgPictures = getenv("XDG_PICTURES_DIR");
    const char *xdgMusic = getenv("XDG_MUSIC_DIR");
    const char *xdgVideos = getenv("XDG_VIDEOS_DIR");
    const char *xdgConfig = getenv("XDG_CONFIG_HOME");

    if (nFolder == CSIDL_MYMUSIC)
    {
        if (xdgMusic && xdgMusic[0] != '\0')
        {
            strcpy(lpszPath, xdgMusic);
        }
        else
        {
            sprintf(lpszPath, "%s/Music", homeDir);
        }
    }
    else if (nFolder == CSIDL_MYVIDEO)
    {
        if (xdgVideos && xdgVideos[0] != '\0')
        {
            strcpy(lpszPath, xdgVideos);
        }
        else
        {
            sprintf(lpszPath, "%s/Videos", homeDir);
        }
    }
    else if (nFolder == CSIDL_MYDOCUMENTS || nFolder == CSIDL_PERSONAL)
    {
        if (xdgDocuments && xdgDocuments[0] != '\0')
        {
            strcpy(lpszPath, xdgDocuments);
        }
        else
        {
            sprintf(lpszPath, "%s/Documents", homeDir);
        }
    }
    else if (nFolder == CSIDL_APPDATA)
    {
        // XDG_DATA_HOME defaults to ~/.local/share
        if (xdgData && xdgData[0] != '\0')
        {
            sprintf(lpszPath, "%s", xdgData);
        }
        else
        {
            sprintf(lpszPath, "%s/.local/share", homeDir);
        }
    }
    else if (nFolder == CSIDL_DESKTOP)
    {
        if (xdgDesktop && xdgDesktop[0] != '\0')
        {
            strcpy(lpszPath, xdgDesktop);
        }
        else
        {
            sprintf(lpszPath, "%s/Desktop", homeDir);
        }
    }
    else if (nFolder == CSIDL_DESKTOPDIRECTORY)
    {
        if (xdgDesktop && xdgDesktop[0] != '\0')
        {
            strcpy(lpszPath, xdgDesktop);
        }
        else
        {
            sprintf(lpszPath, "%s/Desktop", homeDir);
        }
    }
    else if (nFolder == CSIDL_MYPICTURES)
    {
        if (xdgPictures && xdgPictures[0] != '\0')
        {
            strcpy(lpszPath, xdgPictures);
        }
        else
        {
            sprintf(lpszPath, "%s/Pictures", homeDir);
        }
    }
    else if (nFolder == CSIDL_LOCAL_APPDATA)
    {
        // Cache directory
        if (xdgCache && xdgCache[0] != '\0')
        {
            sprintf(lpszPath, "%s", xdgCache);
        }
        else
        {
            sprintf(lpszPath, "%s/.cache", homeDir);
        }
    }
    else if (nFolder == CSIDL_FAVORITES)
    {
        sprintf(lpszPath, "%s/.favorites", homeDir);
    }
    else if (nFolder == CSIDL_STARTUP)
    {
        if (xdgConfig && xdgConfig[0] != '\0')
        {
            sprintf(lpszPath, "%s/autostart", xdgConfig);
        }
        else
        {
            sprintf(lpszPath, "%s/.config/autostart", homeDir);
        }
    }
    else if (nFolder == CSIDL_RECENT)
    {
        if (xdgData && xdgData[0] != '\0')
        {
            sprintf(lpszPath, "%s/recently-used", xdgData);
        }
        else
        {
            sprintf(lpszPath, "%s/.local/share/recently-used", homeDir);
        }
    }
    else if (nFolder == CSIDL_SENDTO)
    {
        sprintf(lpszPath, "%s/.local/share/nautilus/scripts", homeDir);
    }
    else if (nFolder == CSIDL_STARTMENU)
    {
        if (xdgData && xdgData[0] != '\0')
        {
            sprintf(lpszPath, "%s/applications", xdgData);
        }
        else
        {
            sprintf(lpszPath, "%s/.local/share/applications", homeDir);
        }
    }
    else if (nFolder == CSIDL_PROGRAMS)
    {
        if (xdgData && xdgData[0] != '\0')
        {
            sprintf(lpszPath, "%s/applications", xdgData);
        }
        else
        {
            sprintf(lpszPath, "%s/.local/share/applications", homeDir);
        }
    }
    else if (nFolder == CSIDL_TEMPLATES)
    {
        const char *xdgTemplates = getenv("XDG_TEMPLATES_DIR");
        if (xdgTemplates && xdgTemplates[0] != '\0')
        {
            strcpy(lpszPath, xdgTemplates);
        }
        else
        {
            sprintf(lpszPath, "%s/Templates", homeDir);
        }
    }
    else if (nFolder == CSIDL_INTERNET)
    {
        // No direct equivalent on Linux
        sprintf(lpszPath, "%s", homeDir);
        return FALSE;
    }
    else if (nFolder == CSIDL_FONTS)
    {
        sprintf(lpszPath, "%s/.fonts", homeDir);
    }
    else
    {
        // Default fallback
        strcpy(lpszPath, homeDir);
        return FALSE;
    }
#endif

    // Verify directory exists, create if needed for appdata/cache
    if (fCreate)
    {
        struct stat st;
        if (stat(lpszPath, &st) != 0 || !S_ISDIR(st.st_mode))
        {
            // Try to create the directory
            if (0 != mkdir(lpszPath, 0755))
                return FALSE; // Failed to create directory
        }
    }

    return TRUE;
#endif
}
BOOL WINAPI SHGetSpecialFolderPathW(HWND hwndOwner, LPWSTR lpszPath, int nFolder, BOOL fCreate)
{
    char szPath[MAX_PATH];
    if (!SHGetSpecialFolderPathA(hwndOwner, szPath, nFolder, fCreate))
        return FALSE;
    std::wstring strPath;
    towstring(szPath, -1, strPath);
    wcscpy(lpszPath, strPath.c_str());
    return TRUE;
}

/* ===========================================================================
 * SHGetFolderPath（SHELL32）
 *
 * 只是个壳：CSIDL_* 的取值与 SHGetSpecialFolderPath 完全一致，区别仅在于
 *   - nFolder 的高字节是标志位，必须先掩掉；
 *   - 返回值是 HRESULT（失败时 HRESULT_FROM_WIN32(错误码)），不是 BOOL。
 * swinx 没有「默认用户 / 漫游」的概念，所以 SHGFP_TYPE_CURRENT 与
 * SHGFP_TYPE_DEFAULT 等价，hToken 忽略。
 * ======================================================================== */

HRESULT WINAPI SHGetFolderPathA(HWND hwndOwner, int nFolder, HANDLE hToken __attribute__((unused)),
                                DWORD dwFlags __attribute__((unused)), LPSTR pszPath)
{
    if (!pszPath)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);

    BOOL fCreate = (nFolder & CSIDL_FLAG_CREATE) ? TRUE : FALSE;

    pszPath[0] = '\0';
    if (!SHGetSpecialFolderPathA(hwndOwner, pszPath, nFolder & ~CSIDL_FLAG_MASK, fCreate))
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);

    return S_OK;
}

HRESULT WINAPI SHGetFolderPathW(HWND hwndOwner, int nFolder, HANDLE hToken __attribute__((unused)),
                                DWORD dwFlags __attribute__((unused)), LPWSTR pszPath)
{
    if (!pszPath)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);

    BOOL fCreate = (nFolder & CSIDL_FLAG_CREATE) ? TRUE : FALSE;

    pszPath[0] = L'\0';
    if (!SHGetSpecialFolderPathW(hwndOwner, pszPath, nFolder & ~CSIDL_FLAG_MASK, fCreate))
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);

    return S_OK;
}

/* ===========================================================================
 * SHCreateDirectoryEx（SHELL32）
 *
 * 递归创建整条路径。返回的是 Win32 错误码而不是 BOOL，这与
 * SHGetSpecialFolderPath 的区别要留意（调用方写的是 != ERROR_SUCCESS）。
 * 与 Win32 的差异：
 *   - hwnd 只在失败时用来弹错误框，swinx 不弹窗；
 *   - psa（安全描述符）忽略——POSIX 上没有 ACL 概念，一律 0755；
 *   - 会把 '\' 归一成 '/'（见下），因为 WebKit 的 Windows 端口代码传进来的
 *     多是反斜杠路径。
 * ======================================================================== */

/* POSIX 上 '\' 是合法文件名字符，直接 mkdir("a\\b") 会建出名字里带反斜杠的
 * 目录。WebKit 走的是 Windows 端口代码，路径多为 "a\b\c" 形式，因此这里统一
 * 归一成 '/'。（Win32 本身也接受两种分隔符，这么做不违背接口语义。） */
static std::string shcreatedir_posix_path_a(const char *pszPath)
{
    std::string strPath(pszPath);
    for (size_t i = 0; i < strPath.size(); ++i)
    {
        if (strPath[i] == '\\')
            strPath[i] = '/';
    }
    return strPath;
}

static int shcreatedir_tree_a(const std::string &strPath)
{
    if (strPath.empty())
        return ERROR_BAD_PATHNAME;

    struct stat st;
    if (stat(strPath.c_str(), &st) == 0)
        return S_ISDIR(st.st_mode) ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS;

    /* 先建父目录。根（"/"、"C:"）之前没有可建的东西。 */
    size_t nRootEnd = 0;
    if (strPath.size() >= 2 && strPath[1] == ':')
        nRootEnd = 2; // 盘符
    else if (strPath[0] == '/')
        nRootEnd = 1;

    size_t nSlash = strPath.find_last_of('/');
    if (nSlash != std::string::npos && nSlash > nRootEnd)
    {
        int nErr = shcreatedir_tree_a(strPath.substr(0, nSlash));
        if (nErr != ERROR_SUCCESS && nErr != ERROR_ALREADY_EXISTS)
            return nErr;
    }

    if (mkdir(strPath.c_str(), 0755) == 0)
        return ERROR_SUCCESS;

    if (errno == EEXIST)
    {
        if (stat(strPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            return ERROR_ALREADY_EXISTS;
        return ERROR_FILE_EXISTS;
    }
    if (errno == ENOENT)
        return ERROR_PATH_NOT_FOUND;
    if (errno == EACCES || errno == EPERM)
        return ERROR_ACCESS_DENIED;
    if (errno == ENAMETOOLONG)
        return ERROR_FILENAME_EXCED_RANGE;
    return ERROR_INVALID_NAME;
}

int WINAPI SHCreateDirectoryExA(HWND hwnd __attribute__((unused)), LPCSTR pszPath,
                                const SECURITY_ATTRIBUTES *psa __attribute__((unused)))
{
    if (!pszPath || !*pszPath)
        return ERROR_BAD_PATHNAME;

    std::string strPath = shcreatedir_posix_path_a(pszPath);

    /* 去掉末尾的分隔符，但保留 "/" 与 "C:/" 这类根本身。 */
    while (strPath.size() > 1 && strPath[strPath.size() - 1] == '/')
    {
        if (strPath.size() == 3 && strPath[1] == ':')
            break;
        strPath.erase(strPath.size() - 1);
    }

    int nErr = shcreatedir_tree_a(strPath);
    SetLastError((DWORD)nErr);
    return nErr;
}

int WINAPI SHCreateDirectoryExW(HWND hwnd, LPCWSTR pszPath, const SECURITY_ATTRIBUTES *psa)
{
    if (!pszPath || !*pszPath)
        return ERROR_BAD_PATHNAME;

    std::string strPath;
    tostring(pszPath, -1, strPath);
    return SHCreateDirectoryExA(hwnd, strPath.c_str(), psa);
}