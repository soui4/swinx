#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mutex>
#include <map>
#include <string>
#include <vector>

#include "tostring.h"
#include "debug.h"
#define kLogTag "registry"

/* 注册表的 POSIX 实现（只读）。
 *
 * 存储模型：一张"规范化键路径 -> 键"的扁平表。子键枚举靠前缀扫描实现——条目
 * 数量是"配置文件里手写的几行"这个量级，不值得为它建前缀树。
 *
 * 键路径一律按**小写**做索引（注册表不分大小写），但每个键另外记住它自己那一段
 * 的**原始大小写**：FontCacheWin 把子键名直接当字体家族名用，返回小写会改变语义。
 *
 * 数据来源见 include/winreg.h 头部的说明（$SWINX_REGISTRY 或 ~/.config/swinx/registry.ini）。
 * 文件不存在时表为空，所有查询返回"找不到"。
 */

namespace
{

struct RegistryValue
{
    DWORD type;
    // REG_SZ / REG_EXPAND_SZ / REG_MULTI_SZ：UTF-8 文本（MULTI_SZ 用 '\n' 分隔各段，
    // 输出时再转成双 0 结尾）；REG_DWORD：4 字节小端；其余：原样字节。
    std::vector<uint8_t> data;
};

struct RegistryKey
{
    std::string displayName; // 本段原始大小写
    std::string writtenPath; // 完整路径的原始大小写（序列化成节名用；空则退回大写索引路径）
    std::map<std::string, RegistryValue> values; // 值名小写 -> 值
    std::map<std::string, std::string> valueNames; // 值名小写 -> 原始大小写（空串表示默认值）
};

struct RegistryStore
{
    std::mutex mutex;
    std::map<std::string, RegistryKey> keys; // 规范化小写路径 -> 键
    std::map<HKEY, std::string> openHandles;
    HKEY nextHandle = 0x00010001;
    bool dbActive = false;    // 是否挂载了数据库文件
    std::string dbPath;       // 数据库文件路径（dbActive 时有效）
};

// 函数局部静态 + 刻意不析构：注册表可能被静态析构期的代码访问
// （详见 swinx-add-win32-api 里关于跨 DSO 生存期的说明）。
RegistryStore &store()
{
    static RegistryStore *s = new RegistryStore();
    return *s;
}

std::string lowerAscii(const std::string &s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char)(out[i] + 32);
    }
    return out;
}

bool isPredefinedKey(HKEY key)
{
    return key == HKEY_CLASSES_ROOT || key == HKEY_CURRENT_USER || key == HKEY_LOCAL_MACHINE ||
           key == HKEY_USERS || key == HKEY_CURRENT_CONFIG;
}

const char *predefinedName(HKEY key)
{
    switch (key)
    {
    case HKEY_CLASSES_ROOT:
        return "HKEY_CLASSES_ROOT";
    case HKEY_CURRENT_USER:
        return "HKEY_CURRENT_USER";
    case HKEY_LOCAL_MACHINE:
        return "HKEY_LOCAL_MACHINE";
    case HKEY_USERS:
        return "HKEY_USERS";
    case HKEY_CURRENT_CONFIG:
        return "HKEY_CURRENT_CONFIG";
    default:
        return NULL;
    }
}

// 压掉重复分隔符、去掉尾部 '\'
std::string collapseSeparators(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && !out.empty() && out[out.size() - 1] == '\\')
            continue;
        out += s[i];
    }
    while (!out.empty() && out[out.size() - 1] == '\\')
        out.erase(out.size() - 1);
    return out;
}

// 把 "Software\\Microsoft\\..." 规范化成 "hkey_local_machine\\software\\microsoft\\..."
// 已打开的句柄会先被换成它对应的根路径。
bool resolvePath(HKEY hKey, const std::string &subKey, std::string &out)
{
    std::string base;
    if (isPredefinedKey(hKey))
    {
        base = predefinedName(hKey);
    }
    else
    {
        std::lock_guard<std::mutex> lock(store().mutex);
        auto it = store().openHandles.find(hKey);
        if (it == store().openHandles.end())
            return false;
        base = it->second;
    }

    std::string joined = base;
    if (!subKey.empty())
    {
        if (joined.empty() || joined[joined.size() - 1] != '\\')
            joined += '\\';
        joined += subKey;
    }

    out = lowerAscii(collapseSeparators(joined));
    return true;
}

// 逐级补出各级父键，这样即使配置文件只写了叶子键，枚举也能逐级看到父键。
// asWritten 是同一路径的原始大小写版本：displayName 取自它，因为 FontCacheWin
// 之类调用方会把子键名直接当字体家族名用，返回小写会改变语义。
void ensureKey(std::map<std::string, RegistryKey> &keys, const std::string &path, const std::string &asWritten)
{
    std::string partial;
    std::string partialWritten;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '\\')
        {
            if (!partial.empty() && keys.find(partial) == keys.end())
            {
                RegistryKey key;
                size_t slash = partialWritten.rfind('\\');
                key.displayName = slash == std::string::npos ? partialWritten : partialWritten.substr(slash + 1);
                key.writtenPath = partialWritten;
                keys[partial] = key;
            }
            if (i == path.size())
                break;
        }
        if (i < path.size())
        {
            partial += path[i];
            partialWritten += asWritten[i];
        }
    }
}

// ---------------------------------------------------------------- 配置文件解析

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    // fgets 会把行尾的 '\n' 一起交出来，所以它也必须算空白，否则
    // "name"=value 里的值会永远带着一个换行符
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

std::string registryFilePath()
{
    const char *explicitPath = getenv("SWINX_REGISTRY");
    if (explicitPath && *explicitPath)
        return explicitPath;

    const char *configHome = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (configHome && *configHome)
        return std::string(configHome) + "/swinx/registry.ini";
    if (home && *home)
        return std::string(home) + "/.config/swinx/registry.ini";
    return std::string();
}

std::vector<uint8_t> parseHex(const std::string &text)
{
    std::vector<uint8_t> out;
    const char *p = text.c_str();
    while (*p)
    {
        while (*p == ',' || *p == ' ')
            ++p;
        if (!*p)
            break;
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 16);
        if (end == p)
            break;
        out.push_back((uint8_t)v);
        p = end;
    }
    return out;
}

void storeValue(std::map<std::string, RegistryKey> &keys, const std::string &keyPath,
                const std::string &valueName, const std::string &text)
{
    RegistryValue value;
    std::string raw = text;

    if (raw.size() >= 6 && lowerAscii(raw.substr(0, 6)) == "dword:")
    {
        value.type = REG_DWORD;
        uint32_t d = (uint32_t)strtoul(raw.c_str() + 6, NULL, 16);
        value.data.resize(4);
        value.data[0] = (uint8_t)(d & 0xFF);
        value.data[1] = (uint8_t)((d >> 8) & 0xFF);
        value.data[2] = (uint8_t)((d >> 16) & 0xFF);
        value.data[3] = (uint8_t)((d >> 24) & 0xFF);
    }
    else if (raw.size() >= 4 && lowerAscii(raw.substr(0, 4)) == "hex(")
    {
        /* .reg 文件约定：hex(NN): 前缀带类型号 —— REG_EXPAND_SZ 是 hex(2):，
         * REG_MULTI_SZ 是 hex(7):。数据本身照 hex: 解。 */
        size_t close = raw.find(')', 4);
        if (close != std::string::npos && close > 4 && close + 1 < raw.size() && raw[close + 1] == ':')
        {
            value.type = (DWORD)strtoul(raw.c_str() + 4, NULL, 10);
            value.data = parseHex(raw.c_str() + close + 2);
        }
        else
        {
            value.type = REG_BINARY;
            value.data = parseHex(raw.c_str() + 4);
        }
    }
    else if (raw.size() >= 4 && lowerAscii(raw.substr(0, 4)) == "hex:")
    {
        value.type = REG_BINARY;
        value.data = parseHex(raw.c_str() + 4);
    }
    else
    {
        value.type = REG_SZ;
        value.data.assign(raw.begin(), raw.end());
    }

    RegistryKey &key = keys[keyPath];
    std::string lowered = lowerAscii(valueName);
    key.values[lowered] = value;
    key.valueNames[lowered] = valueName;
}

// 把 "..." 或裸文本去掉引号
std::string unquote(const std::string &s)
{
    if (s.size() >= 2 && s[0] == '"' && s[s.size() - 1] == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// 配置文件里的节名形如 "[HKEY_LOCAL_MACHINE\Software\...]"：先校验根键名，再做
// 与 resolvePath 相同的规范化。同时产出两份：小写索引路径，以及原始大小写路径
// （displayName 要用后者，见 ensureKey 的说明）。
bool resolvePathRawSection(const std::string &section, std::string &indexPath, std::string &asWritten)
{
    static const char *const roots[] = {
        "hkey_classes_root", "hkey_current_user", "hkey_local_machine",
        "hkey_users", "hkey_current_config"
    };

    size_t slash = section.find('\\');
    std::string root = lowerAscii(slash == std::string::npos ? section : section.substr(0, slash));

    bool knownRoot = false;
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        if (root == roots[i])
        {
            knownRoot = true;
            break;
        }
    }
    if (!knownRoot)
        return false;

    asWritten = collapseSeparators(section);
    indexPath = lowerAscii(asWritten);
    return true;
}

// 五个预定义根键在 Windows 上恒存在，这里也保证它们存在：于是
// RegOpenKeyEx(HKEY_LOCAL_MACHINE) 成功，而其下未配置的子键返回
// ERROR_FILE_NOT_FOUND —— 与真实系统"根键有、内容未必有"的形状一致。
void ensureRootKeys(std::map<std::string, RegistryKey> &keys)
{
    static const char *const roots[] = {
        "hkey_classes_root", "hkey_current_user", "hkey_local_machine",
        "hkey_users", "hkey_current_config"
    };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i)
    {
        if (keys.find(roots[i]) == keys.end())
        {
            RegistryKey key;
            key.displayName = roots[i];
            key.writtenPath = key.displayName;
            for (size_t c = 0; c < key.displayName.size(); ++c)
                key.displayName[c] = (char)toupper((unsigned char)key.displayName[c]);
            keys[roots[i]] = key;
        }
    }
}

// 解析一份 ini 形态的注册表文本（loadRegistryFile 与 RegMountDatabase 共用）
void parseIniStream(FILE *f, RegistryStore &st)
{
    std::string currentKey;
    char line[2048];
    while (fgets(line, sizeof(line), f))
    {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';')
            continue;

        if (text[0] == '[')
        {
            size_t close = text.find(']');
            if (close == std::string::npos)
                continue;
            std::string section = text.substr(1, close - 1);
            std::string asWritten;
            if (!resolvePathRawSection(section, currentKey, asWritten))
                currentKey.clear();
            else
                ensureKey(st.keys, currentKey, asWritten);
            continue;
        }

        if (currentKey.empty())
            continue;

        size_t eq = text.find('=');
        if (eq == std::string::npos)
            continue;

        std::string name = trim(text.substr(0, eq));
        std::string valueText = trim(text.substr(eq + 1));
        storeValue(st.keys, currentKey, name == "@" ? std::string() : unquote(name), unquote(valueText));
    }
}

void loadRegistryFile()
{
    static std::once_flag once;
    std::call_once(once, []() {
        std::lock_guard<std::mutex> lock(store().mutex);

        // 数据库挂载期间（含挂载过又卸载的进程里），注册表内容以数据库为准，
        // 默认 ini 不再叠加。
        if (store().dbActive)
        {
            ensureRootKeys(store().keys);
            return;
        }

        ensureRootKeys(store().keys);

        std::string path = registryFilePath();
        if (path.empty())
            return;

        FILE *f = fopen(path.c_str(), "rb");
        if (!f)
            return;

        parseIniStream(f, store());
        fclose(f);
    });
}

} // namespace

LONG WINAPI RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    (void)ulOptions;
    (void)samDesired; // 只读存储：任何权限请求都按"能读"处理
    if (!phkResult)
        return ERROR_INVALID_PARAMETER;

    loadRegistryFile();

    std::string subKey;
    if (lpSubKey)
        tostring(lpSubKey, -1, subKey);

    std::string path;
    if (!resolvePath(hKey, subKey, path))
        return ERROR_INVALID_HANDLE;

    // 键必须已经存在（由配置文件/数据库提供，或本身就是预定义根键，或由
    // RegCreateKeyEx 创建）。
    // 这里刻意不"顺手创建"：RegOpenKeyEx 在 Win32 上不存在就报错，创建是
    // RegCreateKeyEx 的职责。
    std::lock_guard<std::mutex> lock(store().mutex);
    if (store().keys.find(path) == store().keys.end())
        return ERROR_FILE_NOT_FOUND;

    HKEY handle = store().nextHandle++;
    store().openHandles[handle] = path;
    *phkResult = handle;
    return ERROR_SUCCESS;
}

LONG WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult)
{
    if (!lpSubKey)
        return RegOpenKeyExW(hKey, NULL, ulOptions, samDesired, phkResult);
    std::wstring wide;
    towstring(lpSubKey, -1, wide);
    return RegOpenKeyExW(hKey, wide.c_str(), ulOptions, samDesired, phkResult);
}

LONG WINAPI RegCloseKey(HKEY hKey)
{
    if (isPredefinedKey(hKey))
        return ERROR_SUCCESS; // 预定义键不需要关闭

    std::lock_guard<std::mutex> lock(store().mutex);
    if (store().openHandles.erase(hKey) == 0)
        return ERROR_INVALID_HANDLE;
    return ERROR_SUCCESS;
}

namespace
{

// 取 hKey 对应的规范化路径；句柄非法时返回 false
bool pathOfHandle(HKEY hKey, std::string &path)
{
    std::lock_guard<std::mutex> lock(store().mutex);
    auto it = store().openHandles.find(hKey);
    if (it == store().openHandles.end())
        return false;
    path = it->second;
    return true;
}

// 按目标宽度算出一个字符串值需要多少字节
DWORD stringValueBytes(const std::vector<uint8_t> &utf8, bool wide)
{
    if (!wide)
        return (DWORD)utf8.size() + 1;

    std::string text(utf8.begin(), utf8.end());
    std::wstring wideText;
    towstring(text.c_str(), -1, wideText);
    return (DWORD)((wideText.size() + 1) * sizeof(WCHAR));
}

LONG queryValue(HKEY hKey, const std::string &valueName, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData, bool wide)
{
    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;
    if (!lpcbData)
        return ERROR_INVALID_PARAMETER;

    loadRegistryFile();

    RegistryValue value;
    {
        std::lock_guard<std::mutex> lock(store().mutex);
        auto keyIt = store().keys.find(path);
        if (keyIt == store().keys.end())
            return ERROR_FILE_NOT_FOUND;
        auto valueIt = keyIt->second.values.find(lowerAscii(valueName));
        if (valueIt == keyIt->second.values.end())
            return ERROR_FILE_NOT_FOUND;
        value = valueIt->second;
    }

    if (lpType)
        *lpType = value.type;

    DWORD required = 0;
    if (value.type == REG_SZ || value.type == REG_EXPAND_SZ)
        required = stringValueBytes(value.data, wide);
    else
        required = (DWORD)value.data.size(); // REG_DWORD 等定长/二进制值

    if (!lpData)
    {
        // Win32：只问尺寸时返回所需大小并报成功
        *lpcbData = required;
        return ERROR_SUCCESS;
    }

    if (*lpcbData < required)
    {
        *lpcbData = required;
        return ERROR_MORE_DATA;
    }

    if (value.type == REG_SZ || value.type == REG_EXPAND_SZ)
    {
        if (wide)
        {
            std::string text(value.data.begin(), value.data.end());
            std::wstring wideText;
            towstring(text.c_str(), -1, wideText);
            memcpy(lpData, wideText.c_str(), (wideText.size() + 1) * sizeof(WCHAR));
        }
        else
        {
            memcpy(lpData, value.data.data(), value.data.size());
            lpData[value.data.size()] = 0;
        }
    }
    else
    {
        memcpy(lpData, value.data.data(), value.data.size());
    }

    *lpcbData = required;
    return ERROR_SUCCESS;
}

} // namespace

LONG WINAPI RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    (void)lpReserved;
    std::string name;
    if (lpValueName)
        tostring(lpValueName, -1, name);
    return queryValue(hKey, name, lpType, lpData, lpcbData, true);
}

LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    (void)lpReserved;
    std::string name = lpValueName ? lpValueName : "";
    return queryValue(hKey, name, lpType, lpData, lpcbData, false);
}

namespace
{

// 收集 hKey 的直接子键（保持插入顺序即可，用 map 的有序性统一成字典序）
std::vector<std::string> subKeyNames(const std::string &path)
{
    std::vector<std::string> result;
    std::string prefix = path + "\\";
    std::lock_guard<std::mutex> lock(store().mutex);
    for (auto it = store().keys.lower_bound(prefix); it != store().keys.end(); ++it)
    {
        if (it->first.compare(0, prefix.size(), prefix) != 0)
            break;
        std::string rest = it->first.substr(prefix.size());
        if (rest.find('\\') == std::string::npos)
            result.push_back(it->second.displayName.empty() ? rest : it->second.displayName);
    }
    return result;
}

LONG enumKey(HKEY hKey, DWORD dwIndex, std::vector<WCHAR> *wideName, std::string *narrowName, LPDWORD lpcchName,
             LPWSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime)
{
    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;
    if (!lpcchName)
        return ERROR_INVALID_PARAMETER;

    loadRegistryFile();

    std::vector<std::string> names = subKeyNames(path);
    if (dwIndex >= names.size())
        return ERROR_NO_MORE_ITEMS;

    const std::string &name = names[dwIndex];

    if (lpClass && lpcchClass)
        *lpcchClass = 0; // swinx 不支持键的 class 字符串，返回 0 长度
    if (lpftLastWriteTime)
        memset(lpftLastWriteTime, 0, sizeof(FILETIME));

    if (wideName)
    {
        std::wstring wide;
        towstring(name.c_str(), -1, wide);
        // Win32：*lpcchName 是缓冲区能放的字符数（含结尾 0），不够时返回 ERROR_MORE_DATA
        if (*lpcchName <= wide.size())
        {
            *lpcchName = (DWORD)(wide.size() + 1);
            return ERROR_MORE_DATA;
        }
        memcpy(wideName->data(), wide.c_str(), (wide.size() + 1) * sizeof(WCHAR));
        *lpcchName = (DWORD)wide.size();
    }
    else
    {
        if (*lpcchName <= name.size())
        {
            *lpcchName = (DWORD)(name.size() + 1);
            return ERROR_MORE_DATA;
        }
        // C++11：std::string::data() 只有 const 版本，写指针要走 &s[0]
        memcpy(&(*narrowName)[0], name.c_str(), name.size());
        (*narrowName)[name.size()] = 0;
        *lpcchName = (DWORD)name.size();
    }
    return ERROR_SUCCESS;
}

} // namespace

LONG WINAPI RegEnumKeyExW(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcchName,
                          LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime)
{
    (void)lpReserved;
    if (!lpName || !lpcchName)
        return ERROR_INVALID_PARAMETER;
    // 名字先写进临时缓冲再拷出，避免把调用方的容量当成本函数的知识
    std::vector<WCHAR> buffer(*lpcchName + 1);
    LONG rc = enumKey(hKey, dwIndex, &buffer, NULL, lpcchName, lpClass, lpcchClass, lpftLastWriteTime);
    if (rc == ERROR_SUCCESS)
        memcpy(lpName, buffer.data(), (*lpcchName + 1) * sizeof(WCHAR));
    return rc;
}

LONG WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName,
                          LPDWORD lpReserved, LPSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime)
{
    (void)lpReserved;
    if (!lpName || !lpcchName)
        return ERROR_INVALID_PARAMETER;
    std::string buffer(*lpcchName + 1, '\0');
    LONG rc = enumKey(hKey, dwIndex, NULL, &buffer, lpcchName, NULL, lpcchClass, lpftLastWriteTime);
    if (rc == ERROR_SUCCESS)
        memcpy(lpName, buffer.data(), *lpcchName + 1);
    return rc;
}

LONG WINAPI RegQueryInfoKeyW(HKEY hKey, LPWSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved,
                             LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen,
                             LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen,
                             LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime)
{
    (void)lpReserved;
    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;

    loadRegistryFile();
    std::vector<std::string> names = subKeyNames(path);

    LPDWORD out[6] = { lpcSubKeys, lpcbMaxSubKeyLen, lpcbMaxClassLen, lpcValues, lpcbMaxValueNameLen, lpcbMaxValueLen };
    for (int i = 0; i < 6; ++i)
        if (out[i])
            *out[i] = 0;

    if (lpcSubKeys)
        *lpcSubKeys = (DWORD)names.size();
    if (lpcbMaxSubKeyLen)
        for (size_t i = 0; i < names.size(); ++i)
            if ((DWORD)names[i].size() > *lpcbMaxSubKeyLen)
                *lpcbMaxSubKeyLen = (DWORD)names[i].size();

    if (lpClass && lpcchClass)
        *lpcchClass = 0;
    if (lpcbSecurityDescriptor)
        *lpcbSecurityDescriptor = 0;
    if (lpftLastWriteTime)
        memset(lpftLastWriteTime, 0, sizeof(FILETIME));

    {
        std::lock_guard<std::mutex> lock(store().mutex);
        auto it = store().keys.find(path);
        if (it != store().keys.end())
        {
            if (lpcValues)
                *lpcValues = (DWORD)it->second.values.size();
            if (lpcbMaxValueNameLen)
                for (auto v = it->second.valueNames.begin(); v != it->second.valueNames.end(); ++v)
                    if ((DWORD)v->second.size() > *lpcbMaxValueNameLen)
                        *lpcbMaxValueNameLen = (DWORD)v->second.size();
            if (lpcbMaxValueLen)
                for (auto v = it->second.values.begin(); v != it->second.values.end(); ++v)
                    if ((DWORD)v->second.data.size() > *lpcbMaxValueLen)
                        *lpcbMaxValueLen = (DWORD)v->second.data.size();
        }
    }
    return ERROR_SUCCESS;
}

LONG WINAPI RegQueryInfoKeyA(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved,
                             LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen,
                             LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen,
                             LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime)
{
    (void)lpClass;
    (void)lpcchClass;
    return RegQueryInfoKeyW(hKey, NULL, NULL, lpReserved, lpcSubKeys, lpcbMaxSubKeyLen, lpcbMaxClassLen,
                            lpcValues, lpcbMaxValueNameLen, lpcbMaxValueLen, lpcbSecurityDescriptor, lpftLastWriteTime);
}

/* ---------------------------------------------------------------- 写入与持久化
 *
 * 挂载数据库（RegMountDatabase）之后，注册表 API 的读写都作用于该文件：
 *   - 挂载即加载（文件不存在则为空库，五个预定义根键恒在）；
 *   - 每次成功的写操作（建键/设值/删键/删值）**写穿**到文件——库的规模是
 *     "配置文件几行"这个量级，整库序列化的成本可以忽略，换来崩溃安全的
 *     持久化语义，不用调用方记得 flush；
 *   - RegUnmountDatabase 落盘并解除挂载，之后注册表退回内存模式（内容
 *     保留，不再持久化）。
 *
 * 文件格式与 registry.ini 相同（见 winreg.h 头部），外加 .reg 文件的
 * hex(NN): 类型前缀约定来保住 REG_EXPAND_SZ / REG_MULTI_SZ 的类型。
 */

namespace
{

// 值能不能按"带引号的纯文本"安全往返：含引号/换行/收尾空白的文本走 hex
std::string serializeValueText(const RegistryValue &value, bool *ok)
{
    char buf[32];
    *ok = true;
    switch (value.type)
    {
    case REG_DWORD:
        if (value.data.size() == 4)
        {
            unsigned d = (unsigned)value.data[0] | ((unsigned)value.data[1] << 8) |
                         ((unsigned)value.data[2] << 16) | ((unsigned)value.data[3] << 24);
            snprintf(buf, sizeof(buf), "dword:%08x", d);
            return buf;
        }
        *ok = false;
        return std::string();
    default:
    {
        bool asText = value.type == REG_SZ;
        if (asText)
        {
            std::string text(value.data.begin(), value.data.end());
            /* Win32 语义 cbData 含收尾 NUL：序列化成文本前剥掉一个，
             * 否则文件里出现 NUL，fgets/一行解析会被 C 串截断 */
            if (!text.empty() && text[text.size() - 1] == '\0')
                text.erase(text.size() - 1);
            /* 文本里还有 NUL（内嵌 0）也无法按行安全往返，走 hex */
            bool safe = !text.empty() && text.find('\0') == std::string::npos &&
                        text[0] != ' ' && text[text.size() - 1] != ' ' &&
                        text.find('"') == std::string::npos && text.find('\n') == std::string::npos &&
                        text.find('\r') == std::string::npos;
            if (safe)
                return "\"" + text + "\"";
        }
        /* REG_EXPAND_SZ/MULTI_SZ 与不安全的 SZ 用 hex(类型): 保住类型号。
         * 注意裸 hex: 在 .reg 约定里是 REG_BINARY，REG_SZ 也必须带 (1)。 */
        char prefix[32];
        snprintf(prefix, sizeof(prefix), "hex(%u):", (unsigned)value.type);
        std::string out = prefix;
        for (size_t i = 0; i < value.data.size(); ++i)
        {
            if (i)
                out += ',';
            snprintf(buf, sizeof(buf), "%02x", value.data[i]);
            out += buf;
        }
        if (value.data.empty())
            out += "00"; /* 空 hex 也是合法的一字节 0 —— 保底不产生悬空前缀 */
        return out;
    }
    }
}

std::string sectionName(const std::map<std::string, RegistryKey>::const_iterator &it)
{
    if (!it->second.writtenPath.empty())
        return it->second.writtenPath;
    /* 旧数据没有 writtenPath：退回大写索引路径（仅影响节名大小写） */
    std::string up = it->first;
    for (size_t i = 0; i < up.size(); ++i)
        up[i] = (char)toupper((unsigned char)up[i]);
    return up;
}

std::string serializeStore(RegistryStore &st)
{
    std::string out = "# swinx registry database\n";
    for (auto it = st.keys.begin(); it != st.keys.end(); ++it)
    {
        if (it->second.values.empty())
            continue; /* 没有任何值的键不占一节（含空根键） */
        out += "[";
        out += sectionName(it);
        out += "]\n";
        for (auto v = it->second.values.begin(); v != it->second.values.end(); ++v)
        {
            bool ok;
            std::string text = serializeValueText(v->second, &ok);
            if (!ok)
                continue;
            const std::string &name = it->second.valueNames[v->first];
            out += name.empty() ? std::string("@") : "\"" + name + "\"";
            out += "=";
            out += text;
            out += "\n";
        }
    }
    return out;
}

// 原子落盘：写临时文件再 rename，避免写一半崩溃留下截断的库
bool writeFileAtomically(const std::string &path, const std::string &content)
{
    if (path.empty())
        return FALSE;
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return FALSE;
    size_t written = fwrite(content.data(), 1, content.size(), f);
    bool ok = written == content.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok)
    {
        remove(tmp.c_str());
        return FALSE;
    }
    if (remove(path.c_str()) != 0 && errno != ENOENT)
    {
        remove(tmp.c_str());
        return FALSE;
    }
    if (rename(tmp.c_str(), path.c_str()) != 0)
    {
        remove(tmp.c_str());
        return FALSE;
    }
    return TRUE;
}

// 落盘（调用方持锁）。没挂载数据库时是无害的 no-op。
bool flushLocked()
{
    if (!store().dbActive)
        return TRUE;
    return writeFileAtomically(store().dbPath, serializeStore(store()));
}

} // namespace

LONG WINAPI RegFlushKey(HKEY hKey)
{
    std::string path;
    if (!isPredefinedKey(hKey) && !pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(store().mutex);
    return flushLocked() ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

BOOL WINAPI RegMountDatabase(const char *lpPath)
{
    if (!lpPath || !*lpPath)
        return FALSE;

    std::lock_guard<std::mutex> lock(store().mutex);

    // 全量替换：数据库就是"这份文件"，不与默认 ini 或上一个库叠加。
    store().keys.clear();
    store().openHandles.clear();
    store().dbPath = lpPath;
    store().dbActive = true;

    ensureRootKeys(store().keys);

    FILE *f = fopen(lpPath, "rb");
    if (f)
    {
        parseIniStream(f, store());
        fclose(f);
    }
    /* 文件不存在 → 空库，等调用方写 */
    return TRUE;
}

BOOL WINAPI RegUnmountDatabase(void)
{
    std::lock_guard<std::mutex> lock(store().mutex);
    if (!store().dbActive)
        return FALSE;
    bool ok = flushLocked();
    store().dbActive = false;
    store().dbPath.clear();
    /* 内容保留在内存里：进程内后续的注册表 API 照常工作，只是不再持久化 */
    return ok ? TRUE : FALSE;
}

LONG WINAPI RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions,
                            REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            PHKEY phkResult, LPDWORD lpdwDisposition)
{
    (void)Reserved;
    (void)lpClass;
    (void)dwOptions;
    (void)samDesired;
    (void)lpSecurityAttributes;
    if (!phkResult || !lpSubKey || !*lpSubKey)
        return ERROR_INVALID_PARAMETER;

    loadRegistryFile();

    std::string subKey;
    tostring(lpSubKey, -1, subKey);

    std::string parentPath;
    if (!resolvePath(hKey, std::string(), parentPath))
        return ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(store().mutex);

    // 完整路径（小写索引 + 原始大小写各一份）。
    // 父段的"原始大小写"必须取父键的 writtenPath —— resolvePath 给出的是
    // 小写索引路径，直接拼会把根键段写成 "hkey_current_user"。
    std::string parentWritten;
    if (isPredefinedKey(hKey))
        parentWritten = predefinedName(hKey);
    else
    {
        auto pit = store().keys.find(parentPath);
        if (pit != store().keys.end() && !pit->second.writtenPath.empty())
            parentWritten = pit->second.writtenPath;
        else
        {
            parentWritten = parentPath;
            for (size_t i = 0; i < parentWritten.size(); ++i)
                parentWritten[i] = (char)toupper((unsigned char)parentWritten[i]);
        }
    }
    std::string written = parentWritten;
    written += '\\';
    written += subKey;
    std::string indexPath = lowerAscii(collapseSeparators(parentPath + "\\" + subKey));
    written = collapseSeparators(written);

    bool existed = store().keys.find(indexPath) != store().keys.end();
    ensureKey(store().keys, indexPath, written);

    HKEY handle = store().nextHandle++;
    store().openHandles[handle] = indexPath;
    *phkResult = handle;
    if (lpdwDisposition)
        *lpdwDisposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;

    if (!existed && !flushLocked())
        return ERROR_ACCESS_DENIED; /* 键已建在内存里，只是没写穿 */
    return ERROR_SUCCESS;
}

LONG WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
                            REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                            PHKEY phkResult, LPDWORD lpdwDisposition)
{
    if (!lpSubKey)
        return ERROR_INVALID_PARAMETER;
    std::wstring wide;
    towstring(lpSubKey, -1, wide);
    return RegCreateKeyExW(hKey, wide.c_str(), Reserved, NULL, dwOptions, samDesired,
                           lpSecurityAttributes, phkResult, lpdwDisposition);
}

LONG WINAPI RegSetValueExW(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType,
                           const BYTE *lpData, DWORD cbData)
{
    (void)Reserved;
    if (!lpData && cbData)
        return ERROR_INVALID_PARAMETER;

    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;

    loadRegistryFile();

    std::string name;
    if (lpValueName)
        tostring(lpValueName, -1, name);

    RegistryValue value;
    value.type = dwType;
    value.data.assign(lpData, lpData + cbData);
    /* W 入口把文本转成 UTF-8 存（与 ini 装载一致），查询时再按调用方宽度转回 */
    if (dwType == REG_SZ || dwType == REG_EXPAND_SZ)
    {
        std::wstring wide((const WCHAR *)lpData, cbData / sizeof(WCHAR));
        std::string narrow;
        tostring(wide.c_str(), -1, narrow);
        value.data.assign(narrow.begin(), narrow.end());
    }
    else if (dwType == REG_MULTI_SZ)
    {
        /* 双 0 结尾的多串按原样存字节，查询原样交还 —— 宽窄编码由调用方自己
         * 保持一致（跨宽度读回不做转换，文档里有说明）。 */
    }

    std::lock_guard<std::mutex> lock(store().mutex);
    auto keyIt = store().keys.find(path);
    if (keyIt == store().keys.end())
        return ERROR_INVALID_HANDLE;

    std::string lowered = lowerAscii(name);
    keyIt->second.values[lowered] = value;
    keyIt->second.valueNames[lowered] = name;

    return flushLocked() ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
                           const BYTE *lpData, DWORD cbData)
{
    (void)Reserved;
    if (!lpData && cbData)
        return ERROR_INVALID_PARAMETER;

    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;

    loadRegistryFile();

    std::string name = lpValueName ? lpValueName : "";

    RegistryValue value;
    value.type = dwType;
    value.data.assign(lpData, lpData + cbData);

    std::lock_guard<std::mutex> lock(store().mutex);
    auto keyIt = store().keys.find(path);
    if (keyIt == store().keys.end())
        return ERROR_INVALID_HANDLE;

    std::string lowered = lowerAscii(name);
    keyIt->second.values[lowered] = value;
    keyIt->second.valueNames[lowered] = name;

    return flushLocked() ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

LONG WINAPI RegDeleteValueW(HKEY hKey, LPCWSTR lpValueName)
{
    if (!lpValueName)
        return ERROR_INVALID_PARAMETER;
    std::string path;
    if (!pathOfHandle(hKey, path))
        return ERROR_INVALID_HANDLE;
    loadRegistryFile();

    std::string name;
    tostring(lpValueName, -1, name);

    std::lock_guard<std::mutex> lock(store().mutex);
    auto keyIt = store().keys.find(path);
    if (keyIt == store().keys.end())
        return ERROR_INVALID_HANDLE;
    std::string lowered = lowerAscii(name);
    if (keyIt->second.values.erase(lowered) == 0)
        return ERROR_FILE_NOT_FOUND;
    keyIt->second.valueNames.erase(lowered);

    return flushLocked() ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

LONG WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName)
{
    if (!lpValueName)
        return ERROR_INVALID_PARAMETER;
    std::wstring wide;
    towstring(lpValueName, -1, wide);
    return RegDeleteValueW(hKey, wide.c_str());
}

LONG WINAPI RegDeleteKeyW(HKEY hKey, LPCWSTR lpSubKey)
{
    if (!lpSubKey || !*lpSubKey)
        return ERROR_INVALID_PARAMETER;
    loadRegistryFile();

    std::string subKey;
    tostring(lpSubKey, -1, subKey);
    std::string path;
    if (!resolvePath(hKey, subKey, path))
        return ERROR_INVALID_HANDLE;

    std::lock_guard<std::mutex> lock(store().mutex);
    if (store().keys.find(path) == store().keys.end())
        return ERROR_FILE_NOT_FOUND;

    /* NT 语义：有子键的键不能删 */
    std::string prefix = path + "\\";
    auto child = store().keys.lower_bound(prefix);
    if (child != store().keys.end() && child->first.compare(0, prefix.size(), prefix) == 0)
        return ERROR_ACCESS_DENIED;

    store().keys.erase(path);
    /* 吊销指向它的句柄 */
    for (auto h = store().openHandles.begin(); h != store().openHandles.end();)
    {
        if (h->second == path)
            h = store().openHandles.erase(h);
        else
            ++h;
    }

    return flushLocked() ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

LONG WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey)
{
    if (!lpSubKey)
        return ERROR_INVALID_PARAMETER;
    std::wstring wide;
    towstring(lpSubKey, -1, wide);
    return RegDeleteKeyW(hKey, wide.c_str());
}
