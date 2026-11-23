#ifndef _SWINX_WL_TRAY_ICON_MGR_H_
#define _SWINX_WL_TRAY_ICON_MGR_H_

/*
 * Wayland 系统托盘：StatusNotifierItem over session D-Bus。
 *
 * Wayland 协议本身没有系统托盘，freedesktop/KDE 的事实标准是让应用作为
 * D-Bus 服务提供一个 org.kde.StatusNotifierItem 对象，由面板（KDE Plasma、
 * Ubuntu 的 gnome-shell-extension-appindicator、wlroots 系 bar 等）充当
 * org.kde.StatusNotifierWatcher 收集并显示。GNOME 原生 shell 不实现该 host，
 * 这是桌面环境的能力限制，不是 swinx 能绕过的问题。
 *
 * 本类把 Win32 的 Shell_NotifyIcon(NIM_ADD/MODIFY/DELETE) 语义翻译成 SNI：
 * 图标/提示走 IconPixmap / ToolTip 属性，点击走 Activate / ContextMenu 方法，
 * 再映射回 NOTIFYICONDATAA.uCallbackMessage 投递给 SOUI（调用方零改动）。
 *
 * 仅在 SWINX_USE_WAYLAND + SWINX_HAVE_SNI 下参与构建；X11 后端走自己的
 * STrayIconMgr（_NET_SYSTEM_TRAY/XEmbed），两条路径互不影响。
 */

#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <cstddef>

// D-Bus 类型直接 include：messageFunction 的签名要用真实的 DBusHandlerResult，
// 前向声明 typedef 会与 dbus.h 里的定义冲突。Wayland 后端已把 dbus include dir
// 加进 SWINX_BACKEND_INCLUDE_DIRS，所有包含本头的 TU 都能找到它。
#include <dbus/dbus.h>

class SConnection;

/* 导出给面板绘制的菜单项（com.canonical.dbusmenu）。
 * id 是 dbusmenu 内部的 item 编号（>=1，0 留给根），uID 是应用的命令 id：
 * 用户点中某项后，swinx 按 uID 回投 WM_COMMAND，应用按命令 id 处理。 */
struct HostMenuItem
{
    int id;
    UINT uID;
    UINT uFlags;               // TRMIF_*
    std::string label;         // UTF-8
    std::vector<HostMenuItem> children;
};

struct WlTrayIcon
{
    NOTIFYICONDATAA data;      // 调用方传进来的原始结构（含 hWnd/uID/callback）
    std::vector<unsigned char> pixmap; // 当前图标的网络序 ARGB32 像素
    int pixW, pixH;
    std::string tip;           // NIF_TIP 的 UTF-8 文本
    std::string title;         // 显示名（取自 lpCaption/应用名）

    /* 菜单：Wayland 无法像 X11 那样让图标成为自己的窗口，指针事件在面板进程内
     * 被消费，应用收不到鼠标消息。所以菜单内容由应用登记（SetTrayMenuProvider），
     * 这里保存回调并把它导出成 dbusmenu，面板点中某项后按命令 id 回投 WM_COMMAND。 */
    TRAYMENUPROVIDER menuProvider;
    bool menuProvided;         // 应用已登记 provider -> 菜单由宿主绘制
    std::vector<HostMenuItem> menuItems; // 最近一次导出的树
    unsigned int menuRevision;           // dbusmenu revision，内容变化时递增
};

class STrayIconMgr
{
public:
    explicit STrayIconMgr(SConnection *conn);
    ~STrayIconMgr();

    // Shell_NotifyIcon 入口；返回 TRUE 表示已处理
    BOOL NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData);

    // 供 SConnection 在定时器里驱动 D-Bus 事件泵（UI 线程）
    void pump();
    // watcher 消失/重启时由 D-Bus filter 复位注册标志
    void setWatcherSeen(bool seen) { m_watcherSeen = seen; m_registered = seen; }
    // 向 Watcher 报到（连接建立后、以及 watcher 重启后由 filter 调用）
    void registerToWatcher();
    // 当前首项图标（SNI 一个 service 只暴露一个 item）
    WlTrayIcon *firstIcon();
    size_t iconCount() const { return m_icons.size(); }
    // 把点击坐标写入 SConnection 光标缓存，供 SOUI 右键菜单 GetCursorPos 使用
    void noteClickPosition(int x, int y);

    /* 应用登记托盘菜单内容。返回 TRUE 表示由本后端接管菜单（面板绘制 + 点击
     * 回投 WM_COMMAND）；FALSE 表示没有对应图标（应用应继续自绘菜单）。
     * pProvider 为 NULL 时注销，回到"占位项 + 回投 WM_RBUTTONDOWN"的旧行为。 */
    BOOL SetMenuProvider(HWND hWnd, UINT uID, const TRAYMENUPROVIDER *pProvider);
    // 菜单内容变化（勾选/文本）后通知宿主重新拉取布局
    BOOL UpdateMenu(HWND hWnd, UINT uID);

private:
    BOOL addIcon(PNOTIFYICONDATAA lpData);
    BOOL modifyIcon(PNOTIFYICONDATAA lpData);
    BOOL delIcon(PNOTIFYICONDATAA lpData);
    bool findIcon(PNOTIFYICONDATAA lpData, WlTrayIcon **out);

    // 从 HICON 抽取 32bpp 像素，转成网络序 ARGB32（SNI IconPixmap 要求）
    bool iconPixels(HICON hIcon, std::vector<unsigned char> &out, int &w, int &h);

    // 属性变化时通知 host 重新拉取
    void emitSignal(const char *name);

    // D-Bus 对象路径消息处理（static：作为 vtable 的函数指针，userData 即本实例）
    static DBusHandlerResult messageFunction(DBusConnection *conn, DBusMessage *msg, void *userData);
    // com.canonical.dbusmenu：应用登记的菜单导出成面板可绘制的树，点项后按
    // 命令 id 回投 WM_COMMAND；未登记 provider 时退化为占位项 + 右键回投。
    static DBusHandlerResult menuMessageFunction(DBusConnection *conn, DBusMessage *msg, void *userData);
    // 菜单被宿主打开 -> 投 WM_RBUTTONDOWN；内部去抖（Event + AboutToShow 只投一次）
    void onHostMenuOpened();
    // 宿主打开菜单但不带坐标时（GNOME），把光标锚到面板指示器区，让 SOUI 自己
    // 的托盘菜单弹在托盘图标附近。SWINX_TRAY_ANCHOR="x,y" 可覆盖。
    void anchorCursorToTrayArea();
    // 属性名 -> D-Bus 类型签名（variant 必须带正确签名）
    static const char *propertySignature(const char *prop);
    // 把一个属性值写进已打开的 variant iter（prop 决定类型）
    static void writePropertyValue(DBusMessageIter *variant, const char *prop, WlTrayIcon *icon);
    static void appendIconPixmap(DBusMessageIter *parent, WlTrayIcon *icon);
    static void appendToolTip(DBusMessageIter *parent, WlTrayIcon *icon);

    // --- 托盘菜单导出（application -> com.canonical.dbusmenu）-------------
    // 递归调用应用回调，把菜单转成 dbusmenu 树。nextId 从 1 开始顺序分配。
    static void collectMenu(WlTrayIcon *icon, UINT uSubMenu, int depth,
                            std::vector<HostMenuItem> &out, int &nextId);
    // 重新导出并递增 revision（首次 GetLayout/AboutToShow 时懒构建）
    void rebuildMenu(WlTrayIcon *icon);
    static HostMenuItem *findMenuItem(std::vector<HostMenuItem> &items, int id);
    // 用户点中菜单项：按命令 id 回投 WM_COMMAND（与 SMenuEx 选中项一致）
    void postMenuCommand(WlTrayIcon *icon, UINT cmdId);
    // 通知面板布局变了（内容/勾选状态变化）
    void emitMenuLayoutUpdated(unsigned int revision);
    bool menuHasProvider(WlTrayIcon *icon) const { return icon && icon->menuProvided; }

    SConnection *m_conn;
    DBusConnection *m_dbus;
    bool m_registered;         // 是否已向 Watcher 报到成功
    bool m_watcherSeen;        // watcher host 是否出现过（NameOwnerChanged）
    std::string m_serviceName;
    // key = (hWnd, uID)，与 X11 托盘的图标标识口径一致
    std::map<std::pair<void *, unsigned int>, WlTrayIcon *> m_icons;
    // 应用可能在 NIM_ADD 之前就登记菜单（先注册、后显示托盘），这里先挂起，
    // 等对应图标创建时再绑定。
    std::map<std::pair<void *, unsigned int>, TRAYMENUPROVIDER> m_pendingMenus;
    unsigned m_nextId;

    // GNOME appindicator 打开宿主菜单时先发 Event(0,"opened") 再发
    // AboutToShow(0)；宿主菜单被我们自己的弹窗顶掉后还会重开，又是一轮。
    // m_menuAttached 吞掉 attach 那次；m_lastMenuClickTick 保证同一轮点击
    // 只投一条 WM_RBUTTONDOWN，否则 demo 会反复弹菜单。
    // m_lastClickPosTick 记录 host 最近一次给真实点击坐标的时刻，过期后
    // onHostMenuOpened 会用面板锚点替换过期的光标位置。
    bool m_menuAttached;
    unsigned int m_lastMenuClickTick;
    unsigned int m_lastClickPosTick;
};

#endif // _SWINX_WL_TRAY_ICON_MGR_H_
