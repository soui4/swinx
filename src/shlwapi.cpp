#include <fileapi.h>
#include <shlwapi.h>
#include <winerror.h>
#include <string.h>
#include <wchar.h>
#include "tostring.h"

BOOL WINAPI PathIsDirectoryA(LPCSTR pszPath)
{
    // INVALID_FILE_ATTRIBUTES (0xFFFFFFFF) would AND with every flag,
    // so a non-existent path must be rejected explicitly
    DWORD attr = GetFileAttributesA(pszPath);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return attr & FILE_ATTRIBUTE_DIRECTORY;
}

BOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath)
{
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathIsDirectoryA(strPath.c_str());
}

BOOL WINAPI PathFileExistsA(LPCSTR pszPath)
{
    return GetFileAttributesA(pszPath) != INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI PathFileExistsW(LPCWSTR pszPath)
{
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathFileExistsA(strPath.c_str());
}

/* ===========================================================================
 * 路径字符串运算（shlwapi）
 *
 * 这组函数不访问文件系统：路径对它们而言只是一段文本。'\' 与 '/' 都算分隔符
 * （POSIX 上 '/' 是真正的分隔符，而 WebKit 传进来的多是 Windows 风格路径）。
 *
 * A 系版本直接在 UTF-8 字节上操作：需要判断的字符（'\' '/' '.' '*' '?' 以及
 * 盘符字母）全是单字节 ASCII，而 UTF-8 的多字节字符首字节 >= 0x80，因此不会
 * 出现「把某个汉字的后半截当成分隔符」的问题，也就不必来回转码。
 * ======================================================================== */

static inline bool path_is_sep_a(char c)
{
    return c == '\\' || c == '/';
}

static inline bool path_is_sep_w(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

/* 带盘符（"C:..."）或以分隔符开头 → 视为绝对路径。 */
static inline bool path_is_abs_a(const char *p)
{
    if (!p || !p[0])
        return false;
    if (path_is_sep_a(p[0]))
        return true;
    return ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':';
}

static inline bool path_is_abs_w(const wchar_t *p)
{
    if (!p || !p[0])
        return false;
    if (path_is_sep_w(p[0]))
        return true;
    return ((p[0] >= L'a' && p[0] <= L'z') || (p[0] >= L'A' && p[0] <= L'Z')) && p[1] == L':';
}

/* UTF-8 的 '/' 与 '\\' 都是单字节，因此可以直接定位「最后一个路径分量」的起点。 */
static inline char *path_last_component_a(char *pszPath)
{
    char *pLast = pszPath;
    for (char *p = pszPath; *p; p++)
        if (path_is_sep_a(*p))
            pLast = p + 1;
    return pLast;
}

static inline wchar_t *path_last_component_w(wchar_t *pszPath)
{
    wchar_t *pLast = pszPath;
    for (wchar_t *p = pszPath; *p; p++)
        if (path_is_sep_w(*p))
            pLast = p + 1;
    return pLast;
}

UINT WINAPI PathGetCharTypeA(UCHAR ch)
{
    // 拉丁-1 字符在 Unicode 里码点相同，直接按宽字符判定即可
    return PathGetCharTypeW((WCHAR)(unsigned char)ch);
}

UINT WINAPI PathGetCharTypeW(WCHAR ch)
{
    if (ch == L'\\' || ch == L'/')
        return GCT_SEPARATOR;
    if (ch == L'*' || ch == L'?')
        return GCT_WILD;
    // Windows 文件名的非法字符（分号、逗号、等号等 FAT 允许，此处照抄 Win32 口径）
    if (ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || ch == L':')
        return GCT_INVALID;
    if (ch < L' ')
        return GCT_INVALID; // 控制字符
    if (ch >= 0x80)
        return GCT_LFNCHAR; // 非 ASCII：只有长文件名允许（本工程 WCHAR 是 UTF-32，不会遇到代理对）
    if (ch == L' ')
        return GCT_LFNCHAR; // 空格在 8.3 里不合法
    return GCT_LFNCHAR | GCT_SHORTCHAR;
}

BOOL WINAPI PathAppendA(LPSTR pszPath, LPCSTR pszMore)
{
    if (!pszPath || !pszMore)
        return FALSE;
    size_t nPath = strlen(pszPath);
    size_t nMore = strlen(pszMore);
    // 只有两侧都不是分隔符时才需要补一个
    bool bAddSep = nPath > 0 && nMore > 0 && !path_is_sep_a(pszPath[nPath - 1]) && !path_is_sep_a(pszMore[0]);
    if (nPath + (bAddSep ? 1 : 0) + nMore >= MAX_PATH)
    { // 装不下：Win32 会把 pszPath 清空
        pszPath[0] = '\0';
        return FALSE;
    }
    if (bAddSep)
        pszPath[nPath++] = '\\';
    memcpy(pszPath + nPath, pszMore, nMore + 1);
    return TRUE;
}

BOOL WINAPI PathAppendW(LPWSTR pszPath, LPCWSTR pszMore)
{
    if (!pszPath || !pszMore)
        return FALSE;
    size_t nPath = wcslen(pszPath);
    size_t nMore = wcslen(pszMore);
    bool bAddSep = nPath > 0 && nMore > 0 && !path_is_sep_w(pszPath[nPath - 1]) && !path_is_sep_w(pszMore[0]);
    if (nPath + (bAddSep ? 1 : 0) + nMore >= MAX_PATH)
    {
        pszPath[0] = L'\0';
        return FALSE;
    }
    if (bAddSep)
        pszPath[nPath++] = L'\\';
    memcpy(pszPath + nPath, pszMore, (nMore + 1) * sizeof(WCHAR));
    return TRUE;
}

LPSTR WINAPI PathCombineA(LPSTR pszDest, LPCSTR pszDir, LPCSTR pszFile)
{
    if (!pszDest || !pszFile)
        return NULL;
    // pszFile 是绝对路径（或 pszDir 为空）时，结果就是 pszFile
    if (path_is_abs_a(pszFile) || !pszDir || !pszDir[0])
    {
        size_t n = strlen(pszFile);
        if (n >= MAX_PATH)
        {
            pszDest[0] = '\0';
            return NULL;
        }
        memcpy(pszDest, pszFile, n + 1);
        return pszDest;
    }
    size_t nDir = strlen(pszDir);
    // pszDir 以分隔符结尾时不必再补
    bool bAddSep = !path_is_sep_a(pszDir[nDir - 1]);
    size_t nPre = nDir + (bAddSep ? 1 : 0);
    if (nPre + strlen(pszFile) >= MAX_PATH)
    {
        pszDest[0] = '\0';
        return NULL;
    }
    memcpy(pszDest, pszDir, nDir);
    size_t nOut = nDir;
    if (bAddSep)
        pszDest[nOut++] = '\\';
    strcpy(pszDest + nOut, pszFile);
    return pszDest;
}

LPWSTR WINAPI PathCombineW(LPWSTR pszDest, LPCWSTR pszDir, LPCWSTR pszFile)
{
    if (!pszDest || !pszFile)
        return NULL;
    if (path_is_abs_w(pszFile) || !pszDir || !pszDir[0])
    {
        size_t n = wcslen(pszFile);
        if (n >= MAX_PATH)
        {
            pszDest[0] = L'\0';
            return NULL;
        }
        memcpy(pszDest, pszFile, (n + 1) * sizeof(WCHAR));
        return pszDest;
    }
    size_t nDir = wcslen(pszDir);
    bool bAddSep = !path_is_sep_w(pszDir[nDir - 1]);
    size_t nPre = nDir + (bAddSep ? 1 : 0);
    if (nPre + wcslen(pszFile) >= MAX_PATH)
    {
        pszDest[0] = L'\0';
        return NULL;
    }
    memcpy(pszDest, pszDir, nDir * sizeof(WCHAR));
    size_t nOut = nDir;
    if (bAddSep)
        pszDest[nOut++] = L'\\';
    memcpy(pszDest + nOut, pszFile, (wcslen(pszFile) + 1) * sizeof(WCHAR));
    return pszDest;
}

BOOL WINAPI PathIsUNCA(LPCSTR pszPath)
{
    if (!pszPath || !pszPath[0] || !pszPath[1])
        return FALSE;
    // swinx 把 '/' 也当分隔符，因此 "//host/share" 同样算 UNC（Win32 只认 '\\'）
    return (pszPath[0] == '\\' && pszPath[1] == '\\') || (pszPath[0] == '/' && pszPath[1] == '/');
}

BOOL WINAPI PathIsUNCW(LPCWSTR pszPath)
{
    if (!pszPath || !pszPath[0] || !pszPath[1])
        return FALSE;
    return (pszPath[0] == L'\\' && pszPath[1] == L'\\') || (pszPath[0] == L'/' && pszPath[1] == L'/');
}

void WINAPI PathRemoveExtensionA(LPSTR pszPath)
{
    if (!pszPath)
        return;
    for (char *p = path_last_component_a(pszPath); *p; p++)
    {
        if (*p == '.')
        { // 最后一个分量里的第一个 '.' 之后全是扩展名
            *p = '\0';
            return;
        }
    }
}

void WINAPI PathRemoveExtensionW(LPWSTR pszPath)
{
    if (!pszPath)
        return;
    for (wchar_t *p = path_last_component_w(pszPath); *p; p++)
    {
        if (*p == L'.')
        {
            *p = L'\0';
            return;
        }
    }
}

BOOL WINAPI PathRemoveFileSpecA(LPSTR pszPath)
{
    if (!pszPath || !pszPath[0])
        return FALSE;
    size_t n = strlen(pszPath);
    while (n > 0 && path_is_sep_a(pszPath[n - 1]))
        pszPath[--n] = '\0'; // 先去掉末尾的 '\'，否则 "a\b\" 会只剩 "a\b"
    for (char *p = pszPath + n; p > pszPath;)
    {
        if (path_is_sep_a(*--p))
        {
            if (p == pszPath)
                pszPath[0] = '\0'; // 根目录（"\\server" 或 "/"）：没有目录部分
            else if (p == pszPath + 2 && pszPath[1] == ':')
                p[1] = '\0'; // "C:\dir" → 保留 "C:\"
            else
                *p = '\0';
            return TRUE;
        }
    }
    return FALSE;
}

BOOL WINAPI PathRemoveFileSpecW(LPWSTR pszPath)
{
    if (!pszPath || !pszPath[0])
        return FALSE;
    size_t n = wcslen(pszPath);
    while (n > 0 && path_is_sep_w(pszPath[n - 1]))
        pszPath[--n] = L'\0';
    for (wchar_t *p = pszPath + n; p > pszPath;)
    {
        if (path_is_sep_w(*--p))
        {
            if (p == pszPath)
                pszPath[0] = L'\0';
            else if (p == pszPath + 2 && pszPath[1] == L':')
                p[1] = L'\0';
            else
                *p = L'\0';
            return TRUE;
        }
    }
    /* 同 PathRemoveFileSpecA：没有目录部分时返回 FALSE 并保留原串。 */
    return FALSE;
}

BOOL WINAPI PathRenameExtensionA(LPSTR pszPath, LPCSTR pszExt)
{
    if (!pszPath || !pszExt)
        return FALSE;
    char *pDot = NULL;
    for (char *p = path_last_component_a(pszPath); *p; p++)
    {
        if (*p == '.')
        {
            pDot = p;
            break;
        }
    }
    if (!pDot)
        pDot = pszPath + strlen(pszPath); // 没有扩展名 → 追加
    if ((size_t)(pDot - pszPath) + strlen(pszExt) >= MAX_PATH)
        return FALSE;
    strcpy(pDot, pszExt);
    return TRUE;
}

BOOL WINAPI PathRenameExtensionW(LPWSTR pszPath, LPCWSTR pszExt)
{
    if (!pszPath || !pszExt)
        return FALSE;
    wchar_t *pDot = NULL;
    for (wchar_t *p = path_last_component_w(pszPath); *p; p++)
    {
        if (*p == L'.')
        {
            pDot = p;
            break;
        }
    }
    if (!pDot)
        pDot = pszPath + wcslen(pszPath);
    size_t nExt = wcslen(pszExt);
    if ((size_t)(pDot - pszPath) + nExt >= MAX_PATH)
        return FALSE;
    memcpy(pDot, pszExt, (nExt + 1) * sizeof(WCHAR));
    return TRUE;
}

/* ------------------------------------------------------------ 注册表读取 */

/* SHGetValue 就是一个 RegOpenKeyEx + RegQueryValueEx + RegCloseKey 的组合。
 * 现在注册表族已经有真实后端（src/platform/posix/registry.cpp，只读、可由
 * 配置文件填充），这里改成真的去查，而不是恒返回 ERROR_FILE_NOT_FOUND。 */
DWORD WINAPI SHGetValueA(HKEY hkey, LPCSTR pszSubKey, LPCSTR pszValue, LPDWORD pdwType, void *pvData, LPDWORD pcbData)
{
    HKEY opened = 0;
    LONG rc = RegOpenKeyExA(hkey, pszSubKey, 0, KEY_READ, &opened);
    if (rc != ERROR_SUCCESS)
        return (DWORD)rc;

    rc = RegQueryValueExA(opened, pszValue, NULL, pdwType, (LPBYTE)pvData, pcbData);
    RegCloseKey(opened);
    return (DWORD)rc;
}

DWORD WINAPI SHGetValueW(HKEY hkey, LPCWSTR pszSubKey, LPCWSTR pszValue, LPDWORD pdwType, void *pvData, LPDWORD pcbData)
{
    HKEY opened = 0;
    LONG rc = RegOpenKeyExW(hkey, pszSubKey, 0, KEY_READ, &opened);
    if (rc != ERROR_SUCCESS)
        return (DWORD)rc;

    rc = RegQueryValueExW(opened, pszValue, NULL, pdwType, (LPBYTE)pvData, pcbData);
    RegCloseKey(opened);
    return (DWORD)rc;
}