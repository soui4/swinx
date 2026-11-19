/*
 * swinx/platform/wayland/dlghelper.cpp
 *
 * 通用文件对话框：走 freedesktop xdg-desktop-portal 的
 * org.freedesktop.portal.FileChooser（OpenFile / SaveFile），与 X11 后端用的
 * 是同一个 D-Bus 接口，但父窗口句柄按 Wayland 语义传空串（portal 后端从
 * D-Bus 调用方的 app_id 关联窗口，Wayland 没有 x11:<id> 那种跨进程句柄）。
 *
 * 颜色 / 字体对话框没有对应的 portal 接口，保持显式失败
 * （ERROR_CALL_NOT_IMPLEMENTED），不伪装成功。
 *
 * 依赖：vendor 的 dbus-1（linux.cmake 的 Wayland 分支已链接）+ 系统装了
 * xdg-desktop-portal 及某个后端（xdg-desktop-portal-gtk/kde）。缺失时
 * GetOpenFileName 返回 FALSE，GetLastError = ERROR_CALL_NOT_IMPLEMENTED。
 */
#include <windows.h>
#include <commdlg.h>
#include <dbus/dbus.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>
#include "SwinxUtils.h"

#define kLogTag "dlghelper"
#include <log.h>

namespace {

struct FileFilter
{
    std::string name;
    std::vector<std::string> patterns;
};

/* ---------- URI / 路径工具 ------------------------------------------------ */

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string uriDecode(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string uriToPath(const std::string &uri)
{
    const std::string prefix = "file://";
    if (uri.compare(0, prefix.size(), prefix) != 0)
        return std::string(); // 非 file: URI，portal 理论不会给
    // file:///abs/path -> /abs/path；file://host/path 的 host 忽略
    std::string rest = uri.substr(prefix.size());
    size_t slash = rest.find('/');
    if (slash == std::string::npos)
        return std::string();
    return uriDecode(rest.substr(slash));
}

/* ---------- D-Bus 辅助 ---------------------------------------------------- */

void addDictString(DBusMessageIter *dict, const char *key, const char *value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

void addDictBool(DBusMessageIter *dict, const char *key, dbus_bool_t value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

/* current_folder 是 ay（字节数组），末尾必须含 '\0'。 */
void addDictPath(DBusMessageIter *dict, const char *key, const std::string &path)
{
    DBusMessageIter entry, variant, array;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array);
    for (size_t i = 0; i <= path.size(); ++i) // 含结尾 0
    {
        unsigned char byte = (i < path.size()) ? (unsigned char)path[i] : 0;
        dbus_message_iter_append_basic(&array, DBUS_TYPE_BYTE, &byte);
    }
    dbus_message_iter_close_container(&variant, &array);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

/* filters: a(sa(us))，type 0=glob、1=mime。 */
void addDictFilters(DBusMessageIter *dict, const std::vector<FileFilter> &filters)
{
    DBusMessageIter entry, variant, arr;
    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    const char *key = "filters";
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &arr);

    for (const FileFilter &f : filters)
    {
        DBusMessageIter fs, pats;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &fs);
        const char *name = f.name.c_str();
        dbus_message_iter_append_basic(&fs, DBUS_TYPE_STRING, &name);
        dbus_message_iter_open_container(&fs, DBUS_TYPE_ARRAY, "(us)", &pats);
        for (const std::string &p : f.patterns)
        {
            DBusMessageIter ps;
            dbus_message_iter_open_container(&pats, DBUS_TYPE_STRUCT, NULL, &ps);
            uint32_t type = (p.find('/') != std::string::npos &&
                             p.find('.') == std::string::npos) ? 1u : 0u;
            dbus_message_iter_append_basic(&ps, DBUS_TYPE_UINT32, &type);
            const char *pat = p.c_str();
            dbus_message_iter_append_basic(&ps, DBUS_TYPE_STRING, &pat);
            dbus_message_iter_close_container(&pats, &ps);
        }
        dbus_message_iter_close_container(&fs, &pats);
        dbus_message_iter_close_container(&arr, &fs);
    }
    dbus_message_iter_close_container(&variant, &arr);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

/* 从 portal Response 信号的 a{sv} 里取 uris (as)。response 0=成功。 */
std::vector<std::string> parseResponse(DBusMessage *msg)
{
    std::vector<std::string> result;
    DBusMessageIter it;
    if (!dbus_message_iter_init(msg, &it) ||
        dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32)
        return result;
    uint32_t response = 0;
    dbus_message_iter_get_basic(&it, &response);
    if (response != 0 || !dbus_message_iter_next(&it))
        return result;
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
        return result;

    DBusMessageIter dict;
    dbus_message_iter_recurse(&it, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        dbus_message_iter_get_basic(&entry, &key);
        if (key && strcmp(key, "uris") == 0 && dbus_message_iter_next(&entry) &&
            dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
        {
            DBusMessageIter var, arr;
            dbus_message_iter_recurse(&entry, &var);
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY)
            {
                dbus_message_iter_recurse(&var, &arr);
                while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING)
                {
                    const char *uri = NULL;
                    dbus_message_iter_get_basic(&arr, &uri);
                    if (uri)
                    {
                        std::string path = uriToPath(uri);
                        if (!path.empty())
                            result.push_back(path);
                    }
                    dbus_message_iter_next(&arr);
                }
            }
        }
        dbus_message_iter_next(&dict);
    }
    return result;
}

/* ---------- portal 调用 --------------------------------------------------- */

class PortalFileDialog
{
public:
    PortalFileDialog() : m_conn(NULL)
    {
        DBusError err;
        dbus_error_init(&err);
        m_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
        if (dbus_error_is_set(&err))
        {
            SLOG_STMW() << "portal: session bus unavailable: " << err.message;
            m_conn = NULL;
        }
        dbus_error_free(&err);
    }
    ~PortalFileDialog()
    {
        /* dbus_bus_get 返回共享连接，不 unref。 */
    }

    bool available() const { return m_conn != NULL; }

    std::vector<std::string> open(const char *title, const std::string &initialPath,
                                  const std::vector<FileFilter> &filters,
                                  bool multiple, bool directory, bool save)
    {
        if (!m_conn)
            return std::vector<std::string>();

        static unsigned int counter = 0;
        char token[96];
        snprintf(token, sizeof(token), "swinx_%u_%ld", ++counter, (long)getpid());

        DBusMessage *msg = dbus_message_new_method_call(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.FileChooser",
            save ? "SaveFile" : "OpenFile");
        if (!msg)
            return std::vector<std::string>();

        DBusMessageIter args, opts;
        dbus_message_iter_init_append(msg, &args);
        /* Wayland 没有跨进程窗口句柄；空串让 portal 用调用方 app_id 关联。
         * 对齐 portal 规范 parent_window: "x11:...", "wayland:...", ""。 */
        const char *parent = "";
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
        const char *titleStr = title ? title : "";
        dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &titleStr);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts);
        addDictString(&opts, "handle_token", token);
        if (multiple && !save)  addDictBool(&opts, "multiple", TRUE);
        if (directory)          addDictBool(&opts, "directory", TRUE);
        if (!initialPath.empty()) addDictPath(&opts, "current_folder", initialPath);
        if (!filters.empty() && !directory) addDictFilters(&opts, filters);
        dbus_message_iter_close_container(&args, &opts);

        DBusError err;
        dbus_error_init(&err);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            m_conn, msg, 5000, &err);
        dbus_message_unref(msg);
        if (!reply || dbus_error_is_set(&err))
        {
            SLOG_STMW() << "portal: OpenFile/SaveFile failed: "
                        << (dbus_error_is_set(&err) ? err.message : "no reply");
            if (reply) dbus_message_unref(reply);
            dbus_error_free(&err);
            return std::vector<std::string>();
        }
        const char *requestPath = NULL;
        dbus_bool_t ok = dbus_message_get_args(reply, &err, DBUS_TYPE_OBJECT_PATH,
                                               &requestPath, DBUS_TYPE_INVALID);
        dbus_message_unref(reply);
        if (!ok || !requestPath)
        {
            dbus_error_free(&err);
            return std::vector<std::string>();
        }
        /* 注意：requestPath 属于 reply，已 unref。strdup 保留。 */
        std::string path(requestPath);

        char rule[512];
        snprintf(rule, sizeof(rule),
                 "type='signal',interface='org.freedesktop.portal.Request',"
                 "member='Response',path='%s'", path.c_str());
        dbus_bus_add_match(m_conn, rule, &err);
        if (dbus_error_is_set(&err))
        {
            SLOG_STMW() << "portal: add match failed: " << err.message;
            dbus_error_free(&err);
            return std::vector<std::string>();
        }

        std::vector<std::string> files;
        const int timeoutMs = 300000; // 5 分钟，与 X11 后端一致
        const int pollMs = 100;
        int elapsed = 0;
        bool got = false;
        while (elapsed < timeoutMs && !got)
        {
            dbus_connection_read_write_dispatch(m_conn, pollMs);
            DBusMessage *sig;
            while ((sig = dbus_connection_pop_message(m_conn)) != NULL)
            {
                if (dbus_message_is_signal(sig, "org.freedesktop.portal.Request", "Response") &&
                    path == (dbus_message_get_path(sig) ? dbus_message_get_path(sig) : ""))
                {
                    files = parseResponse(sig);
                    got = true;
                }
                dbus_message_unref(sig);
            }
            elapsed += pollMs;
        }
        dbus_bus_remove_match(m_conn, rule, NULL);
        if (!got)
            SLOG_STMW() << "portal: timeout waiting Response";
        return files;
    }

private:
    DBusConnection *m_conn;
};

/* ---------- OPENFILENAME 解析 / 回填 ------------------------------------- */

std::vector<FileFilter> parseFilters(LPCSTR lpstrFilter)
{
    std::vector<FileFilter> out;
    if (!lpstrFilter)
        return out;
    const char *p = lpstrFilter;
    while (*p)
    {
        FileFilter f;
        f.name = p;
        p += strlen(p) + 1;
        if (!*p)
            break;
        std::string patterns(p);
        p += strlen(p) + 1;
        size_t start = 0;
        while (start < patterns.size())
        {
            size_t end = patterns.find(';', start);
            if (end == std::string::npos)
                end = patterns.size();
            std::string pat = patterns.substr(start, end - start);
            pat.erase(0, pat.find_first_not_of(" \t"));
            pat.erase(pat.find_last_not_of(" \t") + 1);
            if (!pat.empty())
                f.patterns.push_back(pat);
            start = end + 1;
        }
        if (!f.name.empty() && !f.patterns.empty())
            out.push_back(f);
    }
    return out;
}

std::string initialDirOf(LPOPENFILENAMEA p, const std::string &file)
{
    if (p->lpstrInitialDir && *p->lpstrInitialDir)
        return std::string(p->lpstrInitialDir);
    size_t slash = file.find_last_of('/');
    return slash != std::string::npos ? file.substr(0, slash) : std::string();
}

void fillSingle(LPOPENFILENAMEA p, const std::string &file)
{
    if (file.size() >= p->nMaxFile)
        return;
    strcpy(p->lpstrFile, file.c_str());
    p->nFileOffset = 0;
    p->nFileExtension = 0;
    for (size_t i = 0; i < file.size(); ++i)
    {
        if (file[i] == '/')
            p->nFileOffset = (WORD)(i + 1);
        else if (file[i] == '.' && p->nFileExtension == 0)
            p->nFileExtension = (WORD)(i + 1);
    }
}

void fillMulti(LPOPENFILENAMEA p, const std::vector<std::string> &files)
{
    if (files.size() == 1)
    {
        fillSingle(p, files[0]);
        return;
    }
    std::string dir = files[0];
    size_t slash = dir.find_last_of('/');
    dir = (slash != std::string::npos) ? dir.substr(0, slash + 1) : std::string();
    size_t need = dir.size() + 1;
    for (const std::string &f : files)
    {
        size_t base = f.find_last_of('/');
        base = (base != std::string::npos) ? base + 1 : 0;
        need += f.size() - base + 1;
    }
    ++need;
    if (need > p->nMaxFile)
        return;
    char *buf = p->lpstrFile;
    size_t pos = 0;
    memcpy(buf + pos, dir.c_str(), dir.size() + 1);
    pos += dir.size() + 1;
    for (const std::string &f : files)
    {
        size_t base = f.find_last_of('/');
        base = (base != std::string::npos) ? base + 1 : 0;
        const char *name = f.c_str() + base;
        size_t len = strlen(name);
        memcpy(buf + pos, name, len + 1);
        pos += len + 1;
    }
    buf[pos] = '\0';
    p->nFileOffset = (WORD)(dir.size() + 1);
    p->nFileExtension = 0;
}

} // namespace

// ---------------------------------------------------------------------------
// 对外 API
// ---------------------------------------------------------------------------

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode)
{
    if (!p || !p->lpstrFile || p->nMaxFile == 0)
        return FALSE;
    memset(p->lpstrFile, 0, p->nMaxFile);

    PortalFileDialog dlg;
    if (!dlg.available())
    {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }

    std::string currentFile = p->lpstrFile;
    std::string initial = initialDirOf(p, currentFile);
    bool directory = (mode == FOLDER);
    bool save = (mode == SAVE);
    bool multiple = (p->Flags & OFN_ALLOWMULTISELECT) && !save && !directory;
    std::vector<FileFilter> filters = directory ? std::vector<FileFilter>()
                                                : parseFilters(p->lpstrFilter);
    const char *title = p->lpstrTitle && *p->lpstrTitle ? p->lpstrTitle : NULL;

    std::vector<std::string> results = dlg.open(title, initial, filters,
                                                 multiple, directory, save);
    if (results.empty())
        return FALSE; // 用户取消或 portal 不可用

    if (directory || save || !multiple)
        fillSingle(p, results[0]);
    else
        fillMulti(p, results);
    return TRUE;
}

BOOL SChooseColor(HWND, const COLORREF *, COLORREF *)
{
    /* xdg-desktop-portal 没有颜色选择器接口，显式失败。 */
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

int swinx_pick_file_async(LPOPENFILENAMEA p, DlgMode mode,
                          SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    int ok = SGetOpenFileNameA(p, mode) ? 1 : 0;
    return swinx_pick_file_deliver(ok, p->lpstrFile, pfnDone, pCtx);
}
