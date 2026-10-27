#ifndef _SHLWAPI_H_
#define _SHLWAPI_H_

#include <shellapi.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    BOOL WINAPI PathIsDirectoryA(LPCSTR pszPath);
    BOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath);

#ifdef UNICODE
#define PathIsDirectory PathIsDirectoryW
#else
#define PathIsDirectory PathIsDirectoryA
#endif // UNICODE

    BOOL WINAPI PathFileExistsA(LPCSTR pszPath);
    BOOL WINAPI PathFileExistsW(LPCWSTR pszPath);

#ifdef UNICODE
#define PathFileExists PathFileExistsW
#else
#define PathFileExists PathFileExistsA
#endif // !UNICODE

    /* ---- 路径字符串运算 ----
     *
     * Win32 里这组函数属于 shlwapi.dll，swinx 只做「路径即纯文本」这一层运算：
     * 不访问文件系统、也不认识盘符，`\` 与 `/` 都当分隔符。盘符只有 PathIsUNC
     * 需要排除。
     *
     * 所有 A 系版本按 UTF-8 解释字符串，与其它 A 系 API 保持一致；W 系按本工程
     * 的 wchar_t（UTF-32）口径处理，见 WCHAR_SIZE。
     *
     * 缓冲区约定与 Win32 相同：pszPath 必须能容纳 MAX_PATH 个字符。
     */

    /* 判断单个字符在路径里的类别，返回 GCT_* 位组合。 */
#define GCT_INVALID   0x0000 // 路径里不允许出现的字符
#define GCT_LFNCHAR   0x0001 // 长文件名（LFN）允许的字符
#define GCT_SHORTCHAR 0x0002 // 8.3 短文件名允许的字符
#define GCT_WILD      0x0004 // 通配符 '*' '?'
#define GCT_SEPARATOR 0x0008 // 分隔符 '\' '/'

    UINT WINAPI PathGetCharTypeA(UCHAR ch);
    UINT WINAPI PathGetCharTypeW(WCHAR ch);

    /* 追加 pszMore 到 pszPath 末尾（必要时自动补 '\'）。
     * 失败（越界或参数非法）时 pszPath 被置空并返回 FALSE。 */
    BOOL WINAPI PathAppendA(LPSTR pszPath, LPCSTR pszMore);
    BOOL WINAPI PathAppendW(LPWSTR pszPath, LPCWSTR pszMore);

    /* 把 pszDir 与 pszFile 组合进 pszDest。
     * pszFile 是绝对路径时直接取代 pszDir；pszDir 为空时按 pszFile 原样拷贝。
     * 失败返回 NULL，否则返回 pszDest。 */
    LPSTR WINAPI PathCombineA(LPSTR pszDest, LPCSTR pszDir, LPCSTR pszFile);
    LPWSTR WINAPI PathCombineW(LPWSTR pszDest, LPCWSTR pszDir, LPCWSTR pszFile);

    /* 是否以 "\\"（两个分隔符）开头的 UNC 路径。注意 "\\?\" 设备路径同样满足。 */
    BOOL WINAPI PathIsUNCA(LPCSTR pszPath);
    BOOL WINAPI PathIsUNCW(LPCWSTR pszPath);

    /* 去掉最后一个路径分量末尾的 ".ext"（点号必须在该分量内）。 */
    void WINAPI PathRemoveExtensionA(LPSTR pszPath);
    void WINAPI PathRemoveExtensionW(LPWSTR pszPath);

    /* 去掉末尾的 "\name"，只保留目录部分。
     * 路径里没有分隔符时 pszPath 被置空并返回 FALSE。 */
    BOOL WINAPI PathRemoveFileSpecA(LPSTR pszPath);
    BOOL WINAPI PathRemoveFileSpecW(LPWSTR pszPath);

    /* 把末尾的扩展名换成 pszExt（pszExt 需自带 '.'）。
     * 没有扩展名时直接在末尾追加；越界返回 FALSE。 */
    BOOL WINAPI PathRenameExtensionA(LPSTR pszPath, LPCSTR pszExt);
    BOOL WINAPI PathRenameExtensionW(LPWSTR pszPath, LPCWSTR pszExt);

#ifdef UNICODE
#define PathGetCharType     PathGetCharTypeW
#define PathAppend          PathAppendW
#define PathCombine         PathCombineW
#define PathIsUNC           PathIsUNCW
#define PathRemoveExtension PathRemoveExtensionW
#define PathRemoveFileSpec  PathRemoveFileSpecW
#define PathRenameExtension PathRenameExtensionW
#else
#define PathGetCharType     PathGetCharTypeA
#define PathAppend          PathAppendA
#define PathCombine         PathCombineA
#define PathIsUNC           PathIsUNCA
#define PathRemoveExtension PathRemoveExtensionA
#define PathRemoveFileSpec  PathRemoveFileSpecA
#define PathRenameExtension PathRenameExtensionA
#endif // UNICODE

    /* 读取注册表值。swinx 没有注册表后端（见 doc/swinx-API实现清单.md 的
     * ADVAPI32 段），因此恒返回 ERROR_FILE_NOT_FOUND——对调用方而言就是
     * 「这一项没有配置」，与 Linux/macOS 上不存在 Windows 注册表的语义一致。
     * pszValue 为 NULL 表示取子键的默认值。 */
    DWORD WINAPI SHGetValueA(HKEY hkey, LPCSTR pszSubKey, LPCSTR pszValue, LPDWORD pdwType, void *pvData, LPDWORD pcbData);
    DWORD WINAPI SHGetValueW(HKEY hkey, LPCWSTR pszSubKey, LPCWSTR pszValue, LPDWORD pdwType, void *pvData, LPDWORD pcbData);

#ifdef UNICODE
#define SHGetValue SHGetValueW
#else
#define SHGetValue SHGetValueA
#endif // UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //_SHLWAPI_H_