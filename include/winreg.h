#include <sfr_config.h>
#include <windows.h>
#ifndef _WINREG_H_
#define _WINREG_H_

/* winreg.h —— 注册表 API。
 *
 * Windows 的 <windows.h> 会包含本头，所以这里必须能被它安全地拉进来。
 *
 * swinx 提供一个层级存储：默认是空的（于是所有查询都返回"找不到"，这与
 * Linux 上确实没有 Windows 注册表的事实一致），但可以
 * 用一份配置文件预先填入条目，供那些确实需要读 Windows 注册表的移植代码使用：
 *
 *   路径：$SWINX_REGISTRY，否则 $XDG_CONFIG_HOME/swinx/registry.ini，
 *         否则 $HOME/.config/swinx/registry.ini
 *   格式：
 *     [HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\Fonts]
 *     "Arial (TrueType)"="arial.ttf"      ; REG_SZ
 *     @="默认值"                           ; 键的默认值
 *     "Flags"=dword:00000001              ; REG_DWORD
 *     "Blob"=hex:01,02,03                 ; REG_BINARY
 *     "Path"=hex(2):...                   ; REG_EXPAND_SZ（.reg 的类型前缀约定）
 *     "List"=hex(7):...                   ; REG_MULTI_SZ
 *     # 以 # 或 ; 开头的行是注释
 *
 * 需要把注册表当程序状态用（写入 + 持久化）时，调用 RegMountDatabase 挂载
 * 一个数据库文件：此后所有注册表 API 的读写都作用于它，每次写操作立即写穿
 * 落盘；RegUnmountDatabase 落盘并解除挂载。没挂载时的写入只落在内存里，
 * 进程结束即消失。REG_MULTI_SZ 的字节按调用方写入时的编码原样存取，宽窄
 * 入口之间不做转换。
 */

typedef HKEY *PHKEY;
typedef DWORD REGSAM;

#define HKEY_CLASSES_ROOT   ((HKEY)0x80000000)
#define HKEY_CURRENT_USER   ((HKEY)0x80000001)
#define HKEY_LOCAL_MACHINE  ((HKEY)0x80000002)
#define HKEY_USERS          ((HKEY)0x80000003)
#define HKEY_CURRENT_CONFIG ((HKEY)0x80000005)

#define KEY_QUERY_VALUE        0x0001
#define KEY_SET_VALUE          0x0002
#define KEY_CREATE_SUB_KEY     0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_NOTIFY             0x0010
#define KEY_CREATE_LINK        0x0020
#define KEY_WOW64_64KEY        0x0100
#define KEY_WOW64_32KEY        0x0200
#define KEY_WRITE              ((STANDARD_RIGHTS_WRITE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY) & (~SYNCHRONIZE))
#define KEY_EXECUTE            ((KEY_READ) & (~SYNCHRONIZE))
#define KEY_ALL_ACCESS         ((STANDARD_RIGHTS_ALL | KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY | KEY_CREATE_LINK) & (~SYNCHRONIZE))

#ifndef KEY_READ
#define KEY_READ ((STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY) & (~SYNCHRONIZE))
#endif

/* 值类型 */
#define REG_NONE                       0
#define REG_SZ                         1
#define REG_EXPAND_SZ                  2
#define REG_BINARY                     3
#define REG_DWORD                      4
#define REG_DWORD_LITTLE_ENDIAN        4
#define REG_DWORD_BIG_ENDIAN           5
#define REG_LINK                       6
#define REG_MULTI_SZ                   7
#define REG_RESOURCE_LIST              8
#define REG_FULL_RESOURCE_DESCRIPTOR   9
#define REG_RESOURCE_REQUIREMENTS_LIST 10
#define REG_QWORD                      11
#define REG_QWORD_LITTLE_ENDIAN        11

/* RegCreateKeyEx 的 disposition */
#define REG_CREATED_NEW_KEY     0x00000001
#define REG_OPENED_EXISTING_KEY 0x00000002

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    LONG WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
    LONG WINAPI RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);

    LONG WINAPI RegCloseKey(HKEY hKey);

    LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
    LONG WINAPI RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);

    LONG WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName, LPDWORD lpReserved, LPSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime);
    LONG WINAPI RegEnumKeyExW(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcchName, LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime);

    /* 简化的打开/查询组合：SHGetValue 的底层就是这两个 Reg 调用，
     * 见 src/platform/posix/registry.cpp。 */
    LONG WINAPI RegQueryInfoKeyA(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);
    LONG WINAPI RegQueryInfoKeyW(HKEY hKey, LPWSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);

    /* ---- 写入（内存生效；挂载数据库后写穿持久化，见 RegMountDatabase）---- */
    LONG WINAPI RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
    LONG WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
    LONG WINAPI RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
    LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE *lpData, DWORD cbData);
    /* NT 语义：有子键的键不能删（ERROR_ACCESS_DENIED） */
    LONG WINAPI RegDeleteKeyW(HKEY hKey, LPCWSTR lpSubKey);
    LONG WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey);
    LONG WINAPI RegDeleteValueW(HKEY hKey, LPCWSTR lpValueName);
    LONG WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName);
    /* 落盘。没挂载数据库时是 no-op，恒返回 ERROR_SUCCESS */
    LONG WINAPI RegFlushKey(HKEY hKey);

    /* ---- swinx 扩展：注册表数据库持久化 ----
     * RegMountDatabase：打开（不存在则创建）path 指向的数据库文件并挂载。
     * 挂载是全量替换 —— 文件内容就是整个注册表，不与默认 ini 叠加。
     * 之后所有注册表 API 的读写都作用于它，每次成功的写操作立即写穿落盘。
     * RegUnmountDatabase：落盘并解除挂载；内容保留在内存里，之后的注册表
     * API 继续在内存上工作，只是不再持久化。重复挂载按全量替换处理。 */
    BOOL WINAPI RegMountDatabase(const char *lpPath);
    BOOL WINAPI RegUnmountDatabase(void);

#ifdef __cplusplus
}
#endif //__cplusplus

#ifdef UNICODE
#define RegOpenKeyEx    RegOpenKeyExW
#define RegQueryValueEx RegQueryValueExW
#define RegEnumKeyEx    RegEnumKeyExW
#define RegQueryInfoKey RegQueryInfoKeyW
#define RegCreateKeyEx  RegCreateKeyExW
#define RegSetValueEx   RegSetValueExW
#define RegDeleteKey    RegDeleteKeyW
#define RegDeleteValue  RegDeleteValueW
#else
#define RegOpenKeyEx    RegOpenKeyExA
#define RegQueryValueEx RegQueryValueExA
#define RegEnumKeyEx    RegEnumKeyExA
#define RegQueryInfoKey RegQueryInfoKeyA
#define RegCreateKeyEx  RegCreateKeyExA
#define RegSetValueEx   RegSetValueExA
#define RegDeleteKey    RegDeleteKeyA
#define RegDeleteValue  RegDeleteValueA
#endif

#endif //_WINREG_H_
