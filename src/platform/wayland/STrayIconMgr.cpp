#include "STrayIconMgr.h"

#ifdef SWINX_HAVE_SNI

#include <dbus/dbus.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include "SConnection.h"
#include "log.h"
#define kLogTag "tray"

/* 点击链路诊断：SWINX_TRAY_TRACE=1 时打印 D-Bus 方法调用与投递给 SOUI 的消息。
 * 默认关闭，避免托盘空闲时刷屏。 */
static int trayTrace(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("SWINX_TRAY_TRACE") ? 1 : 0;
    return on;
}

/* 同一次点击会被宿主连发 Event("opened") 与 AboutToShow(0)；更麻烦的是，
 * 我们投出 WM_RBUTTONDOWN 让 SOUI 弹出自绘菜单后，指针仍停在托盘图标上，
 * GNOME 会在宿主菜单被关掉后立刻重新 toggle，形成"开了又开"的回环。
 * 这段时间内只认第一次，既能合并两条信号，也能吃掉回环里的重复打开。 */
static const unsigned int TRAY_MENU_DEDUP_MS = 800;

/* host 给出的真实点击坐标（KDE 的 ContextMenu(x,y)、GNOME 的 Activate(x,y)）
 * 在此期间内有效，过期就不再信任，改用面板锚点。 */
static const unsigned int TRAY_POS_HINT_MS = 1500;

/* GNOME 只发 Event("opened")，不带坐标。此时把光标锚到面板右端的指示器区，
 * SOUI 的托盘菜单才会弹在托盘图标附近，而不是指针离开窗口时的旧位置。
 * 默认落在 GNOME 顶栏下沿（Ubuntu 面板高 32），SMenuEx::ShowMenu 会把超出
 * 屏幕右边的菜单向左翻，正好贴着托盘图标。
 * SWINX_TRAY_ANCHOR="x,y" 可覆盖（负值 = 从右/下边界往内数）。 */
static const int TRAY_ANCHOR_DEFAULT_X = -12;
static const int TRAY_ANCHOR_DEFAULT_Y = 34;

/* 宿主菜单里那个"点击载体"项的 label 为空且不可点：GNOME 要求菜单至少有一个
 * 可见项才会打开宿主菜单并回投 Event("opened")，但那个菜单不是应用的菜单
 * （SOUI 自己会画），所以尽量让它看起来是空的。
 * 设 SWINX_TRAY_MENU_LABEL="显示/隐藏" 可以让它变成真正可点的动作
 * （回投 WM_LBUTTONDOWN），用于没有双击/中键习惯的环境。 */
static const char *trayMenuLabel()
{
    static const char *label = NULL;
    static bool inited = false;
    if (!inited)
    {
        const char *env = getenv("SWINX_TRAY_MENU_LABEL");
        label = (env && *env) ? env : "";
        inited = true;
    }
    return label;
}

/* SNI 协议固定串。用 org.kde. 命名空间——这是 KDE、Ubuntu appindicator 扩展
 * 和多数 wlroots bar 实际实现的名字（org.freedesktop. 版本从未被广泛采用）。 */
static const char *SNI_IFACE = "org.kde.StatusNotifierItem";
static const char *SNI_PATH = "/StatusNotifierItem";
static const char *WATCHER_SERVICE = "org.kde.StatusNotifierWatcher";
static const char *WATCHER_PATH = "/StatusNotifierWatcher";

/* GNOME 的 appindicator 扩展（Ubuntu 自带）对 SNI 点击的处理与 X11 托盘不同：
 *   左键单击 -> 切 Shell 菜单（不调用 Activate）
 *   左键双击 -> Activate
 *   右键     -> 切 Shell 菜单（不调用 ContextMenu）
 *   中键     -> SecondaryActivate
 * 且它要求 Menu 属性有效才把 item 置为 ready 并绑定点击处理。
 * 所以 Wayland 下菜单只有一条路：应用通过 SetTrayMenuProvider 提供菜单
 * 内容，这里导出成 com.canonical.dbusmenu，面板画菜单、用户点项后我们按命令
 * id 回投 WM_COMMAND —— 与 X11/Windows 下 SMenuEx 选中一项的效果相同。
 * 应用没登记 provider 时才退回旧行为：菜单里放一个 label 空、不可点的可见项
 * （GNOME 的 PopupMenu.open() 用 isEmpty() 判空，全隐藏就收不到 opened），
 * 仅用于把"右键被按下"翻译成 WM_RBUTTONDOWN。 */
static const char *SNI_MENU_PATH = "/StatusNotifierItem/menu";
static const char *DBUSMENU_IFACE = "com.canonical.dbusmenu";

/* Introspection XML：host（GDBusProxy）靠它获知本对象暴露哪些接口/属性/方法。
 * 属性全部声明可读；方法带 in 参数；信号供属性变化时通知。 */
static const char *SNI_INTROSPECTION =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
    "<node>"
    "  <interface name='org.freedesktop.DBus.Introspectable'>"
    "    <method name='Introspect'>"
    "      <arg name='data' type='s' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.DBus.Properties'>"
    "    <method name='Get'>"
    "      <arg name='interface' type='s' direction='in'/>"
    "      <arg name='property' type='s' direction='in'/>"
    "      <arg name='value' type='v' direction='out'/>"
    "    </method>"
    "    <method name='GetAll'>"
    "      <arg name='interface' type='s' direction='in'/>"
    "      <arg name='properties' type='a{sv}' direction='out'/>"
    "    </method>"
    "    <signal name='PropertiesChanged'>"
    "      <arg name='interface' type='s'/>"
    "      <arg name='changed' type='a{sv}'/>"
    "      <arg name='invalidated' type='as'/>"
    "    </signal>"
    "  </interface>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='Activate'><arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/></method>"
    "    <method name='SecondaryActivate'><arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/></method>"
    "    <method name='XAyatanaSecondaryActivate'><arg name='timestamp' type='u' direction='in'/></method>"
    "    <method name='ContextMenu'><arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/></method>"
    "    <method name='Scroll'><arg name='delta' type='i' direction='in'/>"
    "      <arg name='orientation' type='s' direction='in'/></method>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewTitle'/>"
    "    <signal name='NewStatus'/>"
    "    <signal name='NewToolTip'/>"
    "  </interface>"
    "</node>";

/* 最小 dbusmenu：能让 GNOME 扩展建起 DBusMenu.Client 并进入 ready 即可。
 * 放一个真实可见的占位项，确保 menu.numMenuItems>0；真正的菜单仍由 SOUI 自己画。 */
static const char *DBUSMENU_INTROSPECTION =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
    "<node>"
    "  <interface name='org.freedesktop.DBus.Introspectable'>"
    "    <method name='Introspect'>"
    "      <arg name='data' type='s' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.DBus.Properties'>"
    "    <method name='Get'>"
    "      <arg name='interface' type='s' direction='in'/>"
    "      <arg name='property' type='s' direction='in'/>"
    "      <arg name='value' type='v' direction='out'/>"
    "    </method>"
    "    <method name='GetAll'>"
    "      <arg name='interface' type='s' direction='in'/>"
    "      <arg name='properties' type='a{sv}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'>"
    "      <arg name='parent' type='i' direction='in'/>"
    "      <arg name='recursion' type='i' direction='in'/>"
    "      <arg name='properties' type='as' direction='in'/>"
    "      <arg name='revision' type='u' direction='out'/>"
    "      <arg name='layout' type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='GetGroupProperties'>"
    "      <arg name='ids' type='ai' direction='in'/>"
    "      <arg name='properties' type='as' direction='in'/>"
    "      <arg name='props' type='a(ia{sv})' direction='out'/>"
    "    </method>"
    "    <method name='GetProperty'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='name' type='s' direction='in'/>"
    "      <arg name='value' type='v' direction='out'/>"
    "    </method>"
    "    <method name='Event'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='event' type='s' direction='in'/>"
    "      <arg name='data' type='v' direction='in'/>"
    "      <arg name='timestamp' type='u' direction='in'/>"
    "    </method>"
    "    <method name='AboutToShow'>"
    "      <arg name='id' type='i' direction='in'/>"
    "      <arg name='needUpdate' type='b' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShowGroup'>"
    "      <arg name='ids' type='ai' direction='in'/>"
    "      <arg name='updates' type='ai' direction='out'/>"
    "      <arg name='idErrors' type='ai' direction='out'/>"
    "    </method>"
    "    <signal name='LayoutUpdated'><arg name='revision' type='u'/><arg name='parent' type='i'/></signal>"
    "  </interface>"
    "</node>";

//---------------------------------------------------------------------------
// 图标像素：HICON(32bpp 预乘 BGRA 内存序) -> SNI IconPixmap 网络序 ARGB32
//---------------------------------------------------------------------------
bool STrayIconMgr::iconPixels(HICON hIcon, std::vector<unsigned char> &out,
                              int &w, int &h)
{
    out.clear();
    w = h = 0;
    if (!hIcon)
        return false;
    ICONINFO info = {};
    if (!GetIconInfo(hIcon, &info) || !info.hbmColor)
        return false;
    BITMAP bm = {};
    bool ok = GetObject(info.hbmColor, sizeof(bm), &bm) != 0 &&
              bm.bmBitsPixel == 32 && bm.bmBits && bm.bmWidth > 0 && bm.bmHeight > 0;
    if (ok)
    {
        w = (int)bm.bmWidth;
        h = (int)bm.bmHeight < 0 ? -(int)bm.bmHeight : (int)bm.bmHeight;
        const unsigned char *src = (const unsigned char *)bm.bmBits;
        out.resize((size_t)w * (size_t)h * 4);
        // cairo image surface 的 32bpp 是预乘 BGRA（内存序 B,G,R,A）；SNI 规范
        // 要求非预乘、网络字节序 ARGB（字节序 A,R,G,B）。两步都要做：
        //   1) 反预乘 c = c*a/255，否则半透明边缘会偏暗；
        //   2) BGRA -> ARGB 字节序重排。
        // a==0 时无色贡献，直接写全 0；a==255 时免除法走快路径。
        for (int i = 0; i < w * h; ++i)
        {
            unsigned char b = src[i * 4 + 0];
            unsigned char g = src[i * 4 + 1];
            unsigned char r = src[i * 4 + 2];
            unsigned char a = src[i * 4 + 3];
            if (a == 0)
            {
                out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4 + 3] = 0;
            }
            else if (a == 255)
            {
                out[i * 4 + 0] = 255;
                out[i * 4 + 1] = r;
                out[i * 4 + 2] = g;
                out[i * 4 + 3] = b;
            }
            else
            {
                // 反预乘：round(c*255/a)，钳到 255
                #define UNPREMUL(c) ((unsigned char)(((int)(c) * 255 + a / 2) / a))
                out[i * 4 + 0] = a;
                out[i * 4 + 1] = UNPREMUL(r);
                out[i * 4 + 2] = UNPREMUL(g);
                out[i * 4 + 3] = UNPREMUL(b);
                #undef UNPREMUL
            }
        }
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return ok;
}

//---------------------------------------------------------------------------
// 通知消息：把点击映射回 SOUI 的 uCallbackMessage
//---------------------------------------------------------------------------
static void postTrayNotify(WlTrayIcon *icon, UINT mouseMsg)
{
    if (!icon || !icon->data.hWnd)
    {
        if (trayTrace())
            fprintf(stderr, "[tray] drop notify: icon=%p hwnd=%p\n",
                    (void *)icon, icon ? icon->data.hWnd : NULL);
        return;
    }
    BOOL ok = ::PostMessage(icon->data.hWnd, icon->data.uCallbackMessage,
                            (WPARAM)icon->data.uID, (LPARAM)mouseMsg);
    if (trayTrace())
        fprintf(stderr, "[tray] PostMessage hwnd=%p cb=%u wp=%u lp=%u -> %d\n",
                icon->data.hWnd, icon->data.uCallbackMessage,
                (unsigned)icon->data.uID, (unsigned)mouseMsg, (int)ok);
}

/* 与 X11 托盘一致按 (hWnd,uID) 标识图标。用 pair 精确比较，避免位哈希冲突。 */
typedef std::pair<void *, unsigned int> TrayKey;
static TrayKey iconKey(PNOTIFYICONDATAA lpData)
{
    return TrayKey((void *)lpData->hWnd, lpData->uID);
}

//---------------------------------------------------------------------------
// 菜单文本：应用传进来的是 wchar_t（Linux 上 4 字节码点，Windows 上可能是
// UTF-16 代理对），这里统一转成 UTF-8 交给 dbusmenu。
//---------------------------------------------------------------------------
static void appendUtf8(std::string &out, unsigned int cp)
{
    if (cp < 0x80)
    {
        out += (char)cp;
    }
    else if (cp < 0x800)
    {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

static std::string wideToUtf8(LPCWSTR w)
{
    std::string out;
    if (!w)
        return out;
    for (; *w; ++w)
    {
        unsigned int cp = (unsigned int)*w;
        if (cp >= 0xD800 && cp <= 0xDBFF)
        {
            unsigned int lo = (unsigned int)w[1];
            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++w;
            }
        }
        appendUtf8(out, cp);
    }
    return out;
}

//---------------------------------------------------------------------------
// dbusmenu 选项属性。用 a{sv} 表达 canonical 属性集：label/enabled/visible/
// type/toggle-*/children-display，GNOME 与 KDE 的菜单客户端都按这套解析。
//---------------------------------------------------------------------------
static void appendMenuStringProp(DBusMessageIter *props, const char *key, const char *val)
{
    DBusMessageIter e, v;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(props, &e);
}

static void appendMenuBoolProp(DBusMessageIter *props, const char *key, dbus_bool_t val)
{
    DBusMessageIter e, v;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(props, &e);
}

static void appendMenuIntProp(DBusMessageIter *props, const char *key, dbus_int32_t val)
{
    DBusMessageIter e, v;
    dbus_message_iter_open_container(props, DBUS_TYPE_DICT_ENTRY, NULL, &e);
    dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&e, DBUS_TYPE_VARIANT, "i", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &val);
    dbus_message_iter_close_container(&e, &v);
    dbus_message_iter_close_container(props, &e);
}

static void appendMenuItemProps(DBusMessageIter *props, const HostMenuItem &item)
{
    appendMenuStringProp(props, "label", item.label.c_str());
    appendMenuBoolProp(props, "enabled", (item.uFlags & TRMIF_DISABLED) ? FALSE : TRUE);
    appendMenuBoolProp(props, "visible", TRUE);
    bool sep = (item.uFlags & TRMIF_SEPARATOR) != 0;
    appendMenuStringProp(props, "type", sep ? "separator" : "standard");
    if (sep)
        return;
    if (item.uFlags & TRMIF_CHECKED)
    {
        appendMenuStringProp(props, "toggle-type",
                             (item.uFlags & TRMIF_RADIOCHECK) ? "radio" : "checkmark");
        appendMenuIntProp(props, "toggle-state", 1);
    }
    else
    {
        appendMenuStringProp(props, "toggle-type", "");
        appendMenuIntProp(props, "toggle-state", 0);
    }
    if (!item.children.empty())
        appendMenuStringProp(props, "children-display", "submenu");
}

/* 把树按深度优先摊平（GetGroupProperties 要按 id 回答任意子集）。 */
static void flattenMenu(std::vector<HostMenuItem> &items, std::vector<HostMenuItem *> &out)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        out.push_back(&items[i]);
        flattenMenu(items[i].children, out);
    }
}

/* 一个 dbusmenu 节点 = (i a{sv} av)，children 里递归放同结构。 */
static void appendMenuNode(DBusMessageIter *children, const HostMenuItem &item)
{
    DBusMessageIter var, st, props, kids;
    dbus_message_iter_open_container(children, DBUS_TYPE_VARIANT, "(ia{sv}av)", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_STRUCT, NULL, &st);
    dbus_int32_t id = item.id;
    dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &id);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &props);
    appendMenuItemProps(&props, item);
    dbus_message_iter_close_container(&st, &props);
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "v", &kids);
    for (size_t i = 0; i < item.children.size(); ++i)
        appendMenuNode(&kids, item.children[i]);
    dbus_message_iter_close_container(&st, &kids);
    dbus_message_iter_close_container(&var, &st);
    dbus_message_iter_close_container(children, &var);
}

//---------------------------------------------------------------------------
// DBus 对象：Properties Get/GetAll + Introspectable + SNI 方法
//---------------------------------------------------------------------------
/* 在 parent iter 内直接构建 a(iiay)（parent 通常是已打开的 variant）。
 * SNI IconPixmap = array of (width:i, height:i, data:ay)，data 为网络序 ARGB32。 */
void STrayIconMgr::appendIconPixmap(DBusMessageIter *parent, WlTrayIcon *icon)
{
    DBusMessageIter arr, st, pix;
    dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "(iiay)", &arr);
    if (icon && !icon->pixmap.empty())
    {
        int w = icon->pixW, h = icon->pixH;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &w);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &h);
        const char *bytes = (const char *)&icon->pixmap[0];
        int len = (int)icon->pixmap.size();
        dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "y", &pix);
        dbus_message_iter_append_fixed_array(&pix, DBUS_TYPE_BYTE, &bytes, len);
        dbus_message_iter_close_container(&st, &pix);
        dbus_message_iter_close_container(&arr, &st);
    }
    dbus_message_iter_close_container(parent, &arr);
}

/* ToolTip = (sa(iiay)ss)：icon name, icon pixmaps, title, description。
 * 我们不传 icon name（让 host 用 pixmap），pixmap 列表留空。 */
void STrayIconMgr::appendToolTip(DBusMessageIter *parent, WlTrayIcon *icon)
{
    DBusMessageIter st, arr;
    dbus_message_iter_open_container(parent, DBUS_TYPE_STRUCT, NULL, &st);
    const char *empty = "";
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty); // icon name: 用 pixmap
    dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &arr);
    dbus_message_iter_close_container(&st, &arr);                  // icon pixmap: 空
    const char *title = (icon && !icon->title.empty()) ? icon->title.c_str() : "swinx";
    const char *tip = (icon && !icon->tip.empty()) ? icon->tip.c_str() : "";
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &title);
    dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &tip);
    dbus_message_iter_close_container(parent, &st);
}

const char *STrayIconMgr::propertySignature(const char *prop)
{
    if (strcmp(prop, "IconPixmap") == 0) return "a(iiay)";
    if (strcmp(prop, "ToolTip") == 0)    return "(sa(iiay)ss)";
    if (strcmp(prop, "ItemIsMenu") == 0) return "b";
    if (strcmp(prop, "Menu") == 0)       return "o";
    if (strcmp(prop, "WindowId") == 0)   return "i";
    return "s"; // Id / Category / Title / Status 都是字符串
}

/* 往已打开的 variant iter 里写一个属性值（类型签名由 prop 决定）。
 * Get 和 GetAll 都复用这里，保证两处类型签名一致。 */
void STrayIconMgr::writePropertyValue(DBusMessageIter *variant, const char *prop,
                                      WlTrayIcon *icon)
{
    if (strcmp(prop, "Id") == 0)
    {
        const char *v = "swinx-tray";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "Category") == 0)
    {
        const char *v = "ApplicationStatus";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "Title") == 0)
    {
        const char *v = (icon && !icon->title.empty()) ? icon->title.c_str() : "swinx";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "Status") == 0)
    {
        const char *v = "Active";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "IconPixmap") == 0)
    {
        appendIconPixmap(variant, icon);
    }
    else if (strcmp(prop, "ToolTip") == 0)
    {
        appendToolTip(variant, icon);
    }
    else if (strcmp(prop, "ItemIsMenu") == 0)
    {
        // false：左键走 Activate，右键走 ContextMenu，二者都能映射到 SOUI 回调
        dbus_bool_t v = FALSE;
        dbus_message_iter_append_basic(variant, DBUS_TYPE_BOOLEAN, &v);
    }
    else if (strcmp(prop, "Menu") == 0)
    {
        const char *v = SNI_MENU_PATH;
        dbus_message_iter_append_basic(variant, DBUS_TYPE_OBJECT_PATH, &v);
    }
    else if (strcmp(prop, "WindowId") == 0)
    {
        int v = 0;
        dbus_message_iter_append_basic(variant, DBUS_TYPE_INT32, &v);
    }
    else
    {
        // 未知属性也必须填一个合法值，否则 variant 空容器会让 host 读属性时报错
        const char *v = "";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
}

/* D-Bus object path 回调：必须自己发送 reply 并返回 DBusHandlerResult。
 * （不能返回 DBusMessage* 让 dbus 代发——那不是 object path vtable 的约定，
 * host 会一直等不到应答而超时，表现为托盘图标读不出属性、不显示。） */
DBusHandlerResult STrayIconMgr::messageFunction(DBusConnection *conn, DBusMessage *msg,
                                                void *userData)
{
    STrayIconMgr *self = (STrayIconMgr *)userData;
    WlTrayIcon *icon = self ? self->firstIcon() : NULL;
    const char *member = dbus_message_get_member(msg);
    const char *iface = dbus_message_get_interface(msg);
    if (!member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    /* 动画图标每帧 NIM_MODIFY -> emitSignal(NewIcon) -> host 每帧来读属性，
     * Properties.Get/GetAll 会把 trace 刷屏；属性读取与点击链路无关，跳过。
     * 只保留 Introspect / Activate / ContextMenu 等真正有用的调用。 */
    static const char *PROP_IFACE = "org.freedesktop.DBus.Properties";
    bool isPropRead = iface && strcmp(iface, PROP_IFACE) == 0 &&
                      (strcmp(member, "Get") == 0 || strcmp(member, "GetAll") == 0);
    if (trayTrace() && !isPropRead)
        fprintf(stderr, "[tray] dbus call path=%s iface=%s member=%s sig=%s icons=%zu\n",
                dbus_message_get_path(msg) ? dbus_message_get_path(msg) : "(null)",
                iface ? iface : "(null)", member, dbus_message_get_signature(msg),
                self ? self->iconCount() : 0);

    DBusMessage *reply = NULL;

    if (strcmp(member, "Introspect") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &SNI_INTROSPECTION,
                                     DBUS_TYPE_INVALID);
    }
    else if (iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0 &&
             strcmp(member, "Get") == 0)
    {
        const char *ifaceName = NULL, *prop = NULL;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &ifaceName,
                              DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            // variant 必须按属性实际类型声明签名，host 才会按对应类型解析
            const char *sig = propertySignature(prop ? prop : "");
            DBusMessageIter args, variant;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, sig, &variant);
            writePropertyValue(&variant, prop ? prop : "", icon);
            dbus_message_iter_close_container(&args, &variant);
        }
    }
    else if (iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0 &&
             strcmp(member, "GetAll") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, dict;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
            const char *props[] = { "Id", "Category", "Title", "Status",
                                    "IconPixmap", "ToolTip", "ItemIsMenu", "Menu", "WindowId" };
            for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); ++i)
            {
                DBusMessageIter entry, variant;
                dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
                dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &props[i]);
                dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                                  propertySignature(props[i]), &variant);
                writePropertyValue(&variant, props[i], icon);
                dbus_message_iter_close_container(&entry, &variant);
                dbus_message_iter_close_container(&dict, &entry);
            }
            dbus_message_iter_close_container(&args, &dict);
        }
    }
    /* SNI 方法分发：不能死板要求 iface == org.kde.StatusNotifierItem。
     * ① org.freedesktop.StatusNotifierItem 是同一协议的另一个命名空间，
     *    部分 host（含一些 wlroots bar）用它；
     * ② 少数 host 的 method call 不带 interface 字段（libdbus 允许），
     *    此时 dbus_message_get_interface() 返回 NULL。
     * 两种情况此前都会落进 NOT_YET_HANDLED —— 属性读取正常所以图标能显示，
     * 但点击方法完全没有回执，表现就是"托盘图标在、左右键都没反应"。 */
    else if (strcmp(member, "Activate") == 0 ||
             strcmp(member, "SecondaryActivate") == 0 ||
             strcmp(member, "XAyatanaSecondaryActivate") == 0 ||
             strcmp(member, "ContextMenu") == 0 ||
             strcmp(member, "Scroll") == 0)
    {
        int x = 0, y = 0;
        /* Ubuntu 的 appindicator 扩展在中键点击时优先调用 Ayatana 扩展方法，
         * 存在它就不走标准 SecondaryActivate。按 X11 托盘口径，中键与左键同义
         * （都投 WM_LBUTTONDOWN，由 demo 切换主窗显示/隐藏）。 */
        if (strcmp(member, "XAyatanaSecondaryActivate") == 0)
        {
            dbus_uint32_t timestamp = 0;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &timestamp,
                                  DBUS_TYPE_INVALID);
            if (trayTrace())
                fprintf(stderr, "[tray] click %s(t=%u) -> msg=%u\n",
                        member, (unsigned)timestamp, (unsigned)WM_LBUTTONDOWN);
            postTrayNotify(icon, WM_LBUTTONDOWN);
        }
        else if (strcmp(member, "Activate") == 0 || strcmp(member, "SecondaryActivate") == 0 ||
                 strcmp(member, "ContextMenu") == 0)
        {
            dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                                  DBUS_TYPE_INVALID);
            if (self)
                self->noteClickPosition(x, y);
            if (self && self->menuHasProvider(icon) &&
                strcmp(member, "ContextMenu") == 0)
            {
                /* 菜单由宿主按 dbusmenu 绘制：KDE 的 ContextMenu 只是"打开菜单"，
                 * 不能再投 WM_RBUTTONDOWN 让应用弹第二份。 */
                if (trayTrace())
                    fprintf(stderr, "[tray] ContextMenu(%d,%d) -> host menu, no notify\n", x, y);
            }
            else
            {
                // SOUI 托盘靠 WM_RBUTTONDOWN 弹菜单、WM_LBUTTONDOWN 切换窗口（见 demo
                // 的 OnShellTrayNotify）；SecondaryActivate（中键）也归到左键语义。
                UINT mm = strcmp(member, "ContextMenu") == 0 ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
                /* ContextMenu 之后宿主往往还会打开它自己的 dbusmenu（发
                 * Event("opened")）。同一次点击不能再投一条 WM_RBUTTONDOWN，
                 * 否则 SOUI 会连弹两个菜单。这里预先打上"刚投过菜单"的时间戳。 */
                if (self && mm == WM_RBUTTONDOWN)
                    self->m_lastMenuClickTick = GetTickCount();
                if (trayTrace())
                    fprintf(stderr, "[tray] click %s(%d,%d) -> msg=%u\n", member, x, y, mm);
                postTrayNotify(icon, mm);
            }
        }
        else if (strcmp(member, "Scroll") == 0)
        {
            int delta = 0;
            const char *orientation = NULL;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &delta, DBUS_TYPE_STRING,
                                  &orientation, DBUS_TYPE_INVALID);
            // WM_MOUSEWHEEL 的 delta 按 WHEEL_DELTA 缩放，wp 低位留给按键状态
            WPARAM wp = (WPARAM)((delta > 0 ? 120 : -120) << 16);
            if (icon && icon->data.hWnd)
                ::PostMessage(icon->data.hWnd, icon->data.uCallbackMessage, wp,
                              (LPARAM)WM_MOUSEWHEEL);
            (void)orientation;
        }
        reply = dbus_message_new_method_return(msg);
    }
    else
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (reply)
    {
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

//---------------------------------------------------------------------------
// com.canonical.dbusmenu：应用登记的菜单内容导出成面板可绘制的菜单树，
// 用户点项后按命令 id 回投 WM_COMMAND；未登记时退化为占位项 + 右键回投。
//---------------------------------------------------------------------------
/* 有些 GDBusProxy 版本仍会读 dbusmenu 对象的 Properties.GetAll。缺这个接口时
 * proxy 初始化失败，菜单不会 attach，点击照样没有任何 D-Bus 调用。属性集合与
 * canonical DBusMenu.xml 一致，值只求合法，不参与我们的点击逻辑。 */
static const char *menuPropertySignature(const char *prop)
{
    if (strcmp(prop, "Version") == 0)       return "u";
    if (strcmp(prop, "TextDirection") == 0) return "s";
    if (strcmp(prop, "Status") == 0)        return "s";
    if (strcmp(prop, "IconThemePath") == 0) return "as";
    return "s";
}

static void writeMenuPropertyValue(DBusMessageIter *variant, const char *prop)
{
    if (strcmp(prop, "Version") == 0)
    {
        dbus_uint32_t v = 1;
        dbus_message_iter_append_basic(variant, DBUS_TYPE_UINT32, &v);
    }
    else if (strcmp(prop, "TextDirection") == 0)
    {
        const char *v = "ltr";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "Status") == 0)
    {
        const char *v = "normal";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
    else if (strcmp(prop, "IconThemePath") == 0)
    {
        DBusMessageIter arr;
        dbus_message_iter_open_container(variant, DBUS_TYPE_ARRAY, "s", &arr);
        dbus_message_iter_close_container(variant, &arr);
    }
    else
    {
        const char *v = "";
        dbus_message_iter_append_basic(variant, DBUS_TYPE_STRING, &v);
    }
}

DBusHandlerResult STrayIconMgr::menuMessageFunction(DBusConnection *conn, DBusMessage *msg,
                                                    void *userData)
{
    STrayIconMgr *self = (STrayIconMgr *)userData;
    const char *member = dbus_message_get_member(msg);
    const char *iface = dbus_message_get_interface(msg);
    if (!member)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (trayTrace())
        fprintf(stderr, "[tray] dbusmenu call iface=%s member=%s sig=%s\n",
                iface ? iface : "(null)", member, dbus_message_get_signature(msg));

    DBusMessage *reply = NULL;

    if (strcmp(member, "Introspect") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &DBUSMENU_INTROSPECTION,
                                     DBUS_TYPE_INVALID);
    }
    else if (iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0 &&
             strcmp(member, "Get") == 0)
    {
        const char *prop = NULL;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
                              DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, variant;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT,
                                              menuPropertySignature(prop ? prop : ""), &variant);
            writeMenuPropertyValue(&variant, prop ? prop : "");
            dbus_message_iter_close_container(&args, &variant);
        }
    }
    else if (iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0 &&
             strcmp(member, "GetAll") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, dict;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
            const char *props[] = { "Version", "TextDirection", "Status", "IconThemePath" };
            for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); ++i)
            {
                DBusMessageIter entry, variant;
                dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
                dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &props[i]);
                dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
                                                  menuPropertySignature(props[i]), &variant);
                writeMenuPropertyValue(&variant, props[i]);
                dbus_message_iter_close_container(&entry, &variant);
                dbus_message_iter_close_container(&dict, &entry);
            }
            dbus_message_iter_close_container(&args, &dict);
        }
    }
    else if (strcmp(member, "GetLayout") == 0)
    {
        /* 参数 (parent:i, recursion:i, properties:as)。宿主一般从根(0)递归取全部，
         * 但按需请求某个子菜单也合法，parent 必须尊重。 */
        int parentId = 0;
        DBusMessageIter argIt;
        if (dbus_message_iter_init(msg, &argIt) &&
            dbus_message_iter_get_arg_type(&argIt) == DBUS_TYPE_INT32)
            dbus_message_iter_get_basic(&argIt, &parentId);
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, layout, props;
            dbus_message_iter_init_append(reply, &args);
            /* canonical DBusMenu.xml 把 revision 声明为 u。写成 i 会让 GDBusProxy
             * 因签名不匹配丢弃整个 GetLayout 应答，客户端永远不 ready，宿主菜单
             * numMenuItems 停在 0 —— 表现就是托盘图标在、点击没有任何 D-Bus 调用。 */
            WlTrayIcon *trayIcon = self ? self->firstIcon() : NULL;
            bool hasProvider = self && self->menuHasProvider(trayIcon);
            if (hasProvider && trayIcon->menuItems.empty())
                self->rebuildMenu(trayIcon);
            dbus_uint32_t revision = trayIcon ? trayIcon->menuRevision : 1;
            dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &revision);
            dbus_message_iter_open_container(&args, DBUS_TYPE_STRUCT, NULL, &layout);
            dbus_message_iter_append_basic(&layout, DBUS_TYPE_INT32, &parentId);
            dbus_message_iter_open_container(&layout, DBUS_TYPE_ARRAY, "{sv}", &props);
            DBusMessageIter entry, variant;
            dbus_message_iter_open_container(&props, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
            const char *key = "children-display";
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
            const char *val = "submenu";
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
            dbus_message_iter_close_container(&entry, &variant);
            dbus_message_iter_close_container(&props, &entry);
            dbus_message_iter_close_container(&layout, &props);
            DBusMessageIter children;
            dbus_message_iter_open_container(&layout, DBUS_TYPE_ARRAY, "v", &children);
            if (hasProvider)
            {
                /* 应用登记了菜单：把它提供的 item 树原样导出，面板按 dbusmenu 画
                 * 出来；用户点中某项后 Event(id,"clicked") 按命令 id 回投
                 * WM_COMMAND，应用侧与 SMenuEx 选中一项完全一致。 */
                std::vector<HostMenuItem> *src = &trayIcon->menuItems;
                if (parentId != 0)
                {
                    HostMenuItem *node = self->findMenuItem(trayIcon->menuItems, parentId);
                    src = node ? &node->children : NULL;
                }
                if (src)
                {
                    for (size_t i = 0; i < src->size(); ++i)
                        appendMenuNode(&children, (*src)[i]);
                }
            }
            else if (parentId == 0)
            {
                /* 没有 provider 的兜底（旧行为）：GNOME 的 PopupMenu.open() 用
                 * isEmpty() 判空，菜单至少要有一个可见项才会打开，否则点击连
                 * Event("opened") 都收不到。放一个空 label、不可点的"点击载体"，
                 * 只用于把"右键被按下"翻译成 WM_RBUTTONDOWN。 */
                HostMenuItem ph;
                const char *label = trayMenuLabel();
                ph.id = 1;
                ph.uID = 0;
                ph.uFlags = label[0] ? 0 : TRMIF_DISABLED;
                ph.label = label;
                appendMenuNode(&children, ph);
            }
            dbus_message_iter_close_container(&layout, &children);
            dbus_message_iter_close_container(&args, &layout);
        }
    }
    else if (strcmp(member, "GetGroupProperties") == 0)
    {
        WlTrayIcon *trayIcon = self ? self->firstIcon() : NULL;
        if (self && self->menuHasProvider(trayIcon) && trayIcon->menuItems.empty())
            self->rebuildMenu(trayIcon);
        /* 第一参数是 ai（要查询的 item id），空数组表示"全部"。GNOME 的
         * dbusMenu.js 在初始化时会一次要全部属性，用来建 St 菜单项。 */
        std::vector<int> ids;
        DBusMessageIter it;
        if (dbus_message_iter_init(msg, &it) &&
            dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY)
        {
            DBusMessageIter sub;
            dbus_message_iter_recurse(&it, &sub);
            while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_INT32)
            {
                int v = 0;
                dbus_message_iter_get_basic(&sub, &v);
                ids.push_back(v);
                dbus_message_iter_next(&sub);
            }
        }
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, arr;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
            if (self && self->menuHasProvider(trayIcon))
            {
                std::vector<HostMenuItem *> flat;
                flattenMenu(trayIcon->menuItems, flat);
                for (size_t i = 0; i < flat.size(); ++i)
                {
                    bool want = ids.empty();
                    for (size_t k = 0; k < ids.size() && !want; ++k)
                        want = (ids[k] == flat[i]->id);
                    if (!want)
                        continue;
                    DBusMessageIter entry, props;
                    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &entry);
                    dbus_int32_t id = flat[i]->id;
                    dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &id);
                    dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sv}", &props);
                    appendMenuItemProps(&props, *flat[i]);
                    dbus_message_iter_close_container(&entry, &props);
                    dbus_message_iter_close_container(&arr, &entry);
                }
            }
            else
            {
                /* 与 GetLayout 的兜底项保持同一份属性（见那里的说明）。 */
                DBusMessageIter entry, props;
                dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &entry);
                dbus_int32_t childId = 1;
                dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &childId);
                dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sv}", &props);
                HostMenuItem ph;
                const char *label = trayMenuLabel();
                ph.id = 1;
                ph.uID = 0;
                ph.uFlags = label[0] ? 0 : TRMIF_DISABLED;
                ph.label = label;
                appendMenuItemProps(&props, ph);
                dbus_message_iter_close_container(&entry, &props);
                dbus_message_iter_close_container(&arr, &entry);
            }
            dbus_message_iter_close_container(&args, &arr);
        }
    }
    else if (strcmp(member, "GetProperty") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            DBusMessageIter args, variant;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "s", &variant);
            const char *empty = "";
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &empty);
            dbus_message_iter_close_container(&args, &variant);
        }
    }
    else if (strcmp(member, "AboutToShowGroup") == 0)
    {
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            /* 与 canonical DBusMenu.xml 一致：返回 updatesNeeded 与 idErrors 两个 ai。
             * 少一个也会让 GDBusProxy 判应答签名不符，菜单客户端无法就绪。 */
            DBusMessageIter args, updates, errors;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "i", &updates);
            dbus_message_iter_close_container(&args, &updates);
            dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "i", &errors);
            dbus_message_iter_close_container(&args, &errors);
        }
    }
    else if (strcmp(member, "AboutToShow") == 0)
    {
        int itemId = -1;
        dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &itemId, DBUS_TYPE_INVALID);
        WlTrayIcon *trayIcon = self ? self->firstIcon() : NULL;
        bool hasProvider = self && self->menuHasProvider(trayIcon);
        bool needUpdate = false;
        if (hasProvider && itemId == 0)
        {
            /* 每次打开前重新导出一次（勾选/文本可能已经变了），并递增 revision
             * 通知宿主重新拉取布局；否则它一直画第一次拿到的旧内容。 */
            self->rebuildMenu(trayIcon);
            self->emitMenuLayoutUpdated(trayIcon->menuRevision);
            needUpdate = true;
        }
        reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            dbus_bool_t flag = needUpdate ? TRUE : FALSE;
            DBusMessageIter args;
            dbus_message_iter_init_append(reply, &args);
            dbus_message_iter_append_basic(&args, DBUS_TYPE_BOOLEAN, &flag);
        }
        if (trayTrace())
            fprintf(stderr, "[tray] dbusmenu AboutToShow id=%d provider=%d\n",
                    itemId, hasProvider ? 1 : 0);
        /* 只吞掉 attach 阶段的第一次：GNOME dbusMenu.js 为兼容 Dropbox 会
         * 在 menu attach 时调一次 AboutToShow(0)，若把它当点击会在启动时
         * 误弹菜单。之后的每次根节点 AboutToShow 都是宿主菜单真的被点开了——
         * 有些宿主版本不会发 Event("opened")，这是唯一能收到点击的机会。
         * 登记了 provider 时菜单由宿主绘制，这条兜底路径不适用。 */
        if (!hasProvider && itemId == 0)
        {
            if (self && self->m_menuAttached)
                self->onHostMenuOpened();
            else if (self)
                self->m_menuAttached = true;
        }
    }
    else if (strcmp(member, "Event") == 0)
    {
        int id = -1;
        const char *event = NULL;
        /* 用迭代器逐个读：dbus_message_get_args 要求签名完全匹配，而
         * GNOME 发的 Event 签名是 (isvu)，用 (is) 去 get_args 会整体失败，
         * id/event 保持初始值 -> 点击被静默丢弃。 */
        DBusMessageIter it;
        if (dbus_message_iter_init(msg, &it))
        {
            if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_INT32)
                dbus_message_iter_get_basic(&it, &id);
            if (dbus_message_iter_has_next(&it))
            {
                dbus_message_iter_next(&it);
                if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&it, &event);
            }
        }
        reply = dbus_message_new_method_return(msg);
        if (trayTrace())
            fprintf(stderr, "[tray] dbusmenu Event id=%d event=%s\n",
                    id, event ? event : "(null)");
        WlTrayIcon *trayIcon = self ? self->firstIcon() : NULL;
        /* GNOME 打开宿主菜单时发 Event(0,"opened")，attach 阶段不发。只认
         * 根节点，避免展开子菜单时重复投消息。
         * 应用登记了菜单 provider 时，宿主绘制的这份菜单就是应用菜单本身，
         * 不需要再回投 WM_RBUTTONDOWN 让应用弹第二份菜单。 */
        if (id == 0 && event && strcmp(event, "opened") == 0 && self)
        {
            if (self->menuHasProvider(trayIcon))
            {
                if (trayTrace())
                    fprintf(stderr, "[tray] host menu opened (provider, no notify)\n");
            }
            else
            {
                self->onHostMenuOpened();
            }
        }
        /* 菜单项被点中：按命令 id 回投 WM_COMMAND，与 SMenuEx 选中一项同义。 */
        else if (event && strcmp(event, "clicked") == 0 && self)
        {
            HostMenuItem *node = trayIcon ? self->findMenuItem(trayIcon->menuItems, id) : NULL;
            if (node && node->uID)
            {
                if (trayTrace())
                    fprintf(stderr, "[tray] menu item clicked id=%d cmd=%u -> WM_COMMAND\n",
                            id, (unsigned)node->uID);
                self->postMenuCommand(trayIcon, node->uID);
            }
            else if (!self->menuHasProvider(trayIcon) && id == 1)
            {
                /* 无 provider 的占位项：补上 GNOME 保留左键单击时的语义。 */
                if (trayTrace())
                    fprintf(stderr, "[tray] menu item clicked -> WM_LBUTTONDOWN\n");
                postTrayNotify(trayIcon, WM_LBUTTONDOWN);
            }
        }
    }
    else
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (reply)
    {
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

//---------------------------------------------------------------------------
// SNI host 名字监听：KDE/Ubuntu appindicator 的 watcher 可能晚于本进程启动，
// 或崩溃后重启。监听 org.kde.StatusNotifierWatcher 的 NameOwnerChanged，
// 一旦出现新的 owner 就重新报到，避免托盘图标永久丢失。
//---------------------------------------------------------------------------
static DBusHandlerResult watcherFilter(DBusConnection *, DBusMessage *msg, void *userData)
{
    STrayIconMgr *self = (STrayIconMgr *)userData;
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged"))
    {
        const char *name = NULL, *oldOwner = NULL, *newOwner = NULL;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
                                  DBUS_TYPE_STRING, &oldOwner,
                                  DBUS_TYPE_STRING, &newOwner, DBUS_TYPE_INVALID) &&
            name && strcmp(name, WATCHER_SERVICE) == 0)
        {
            if (newOwner && *newOwner)
                self->registerToWatcher();
            else
                self->setWatcherSeen(false);
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

STrayIconMgr::STrayIconMgr(SConnection *conn)
    : m_conn(conn)
    , m_dbus(NULL)
    , m_registered(false)
    , m_watcherSeen(false)
    , m_nextId(1)
    , m_menuAttached(false)
    , m_lastMenuClickTick(0)
    , m_lastClickPosTick(0)
{
    DBusError err;
    dbus_error_init(&err);
    // 用私有连接：本对象要注册自定义 object path + filter，且退出期由我们
    // 自己 unref；共享连接（dbus_bus_get）会被 dlghelper/AT-SPI 复用，互相
    // unref 会在退出期踩到别人。
    m_dbus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err) || !m_dbus)
    {
        /* 无条件打一行：连 session bus 失败时实例根本不会注册任何 D-Bus 对象，
         * 后面所有 [tray] 点击日志都不可能出现。用它能立刻区分"没接上 bus"
         * 与"接上了但 host 没点"。*/
        fprintf(stderr, "[tray] DISABLED: no session bus (%s)\n",
                dbus_error_is_set(&err) ? err.message : "null connection");
        SLOG_FMTI("no session bus, tray disabled: %s",
              dbus_error_is_set(&err) ? err.message : "null connection");
        if (dbus_error_is_set(&err))
            dbus_error_free(&err);
        return;
    }
    dbus_error_free(&err);
    // 私有连接退出不会自动断开；这里不依赖 dbus 的 exit-on-disconnect 行为
    dbus_connection_set_exit_on_disconnect(m_dbus, FALSE);

    // 申请 service name。PID + 序号保证多进程不冲突；若被占（同进程第二个连接）
    // 用 unique name 兜底也能工作——watcher 接受 object path 形式的注册。
    char name[128];
    snprintf(name, sizeof(name), "org.kde.StatusNotifierItem-%d-%u",
             (int)getpid(), m_nextId++);
    m_serviceName = name;
    int ret = dbus_bus_request_name(m_dbus, m_serviceName.c_str(),
                                    DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err))
    {
        SLOG_FMTI("request_name failed: %s", err.message);
        dbus_error_free(&err);
    }
    else if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        SLOG_FMTI("service name %s not acquired (ret=%d)", m_serviceName.c_str(), ret);
    }

    static const DBusObjectPathVTable vtable = {
        NULL,
        &STrayIconMgr::messageFunction,
        NULL, NULL, NULL, NULL
    };
    if (!dbus_connection_register_object_path(m_dbus, SNI_PATH, &vtable, this))
        SLOG_FMTI("register object path failed");
    static const DBusObjectPathVTable menuVTable = {
        NULL,
        &STrayIconMgr::menuMessageFunction,
        NULL, NULL, NULL, NULL
    };
    if (!dbus_connection_register_object_path(m_dbus, SNI_MENU_PATH, &menuVTable, this))
        SLOG_FMTI("register dbusmenu object path failed");

    dbus_bus_add_match(m_dbus,
        "type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
        "arg0='org.kde.StatusNotifierWatcher'", &err);
    if (dbus_error_is_set(&err))
    {
        SLOG_FMTI("add_match failed: %s", err.message);
        dbus_error_free(&err);
    }
    if (!dbus_connection_add_filter(m_dbus, watcherFilter, this, NULL))
        SLOG_FMTI("add filter failed");

    registerToWatcher();
    dbus_connection_flush(m_dbus);
    /* 无条件打一行存活标记：只要 Shell_NotifyIcon(NIM_ADD) 走到 Wayland 后端
     * 就会构造本对象。看不到这行 => 二进制没重编 / 图标根本没走 Wayland 托盘；
     * 看得到但后面没有任何 dbus call => host（appindicator）没在跑。 */
    fprintf(stderr, "[tray] READY service=%s trace=%d\n",
            m_serviceName.c_str(), trayTrace());
    SLOG_FMTI("created: service=%s", m_serviceName.c_str());
}

STrayIconMgr::~STrayIconMgr()
{
    for (std::map<TrayKey, WlTrayIcon *>::iterator it = m_icons.begin();
         it != m_icons.end(); ++it)
    {
        delete it->second;
    }
    m_icons.clear();
    if (m_dbus)
    {
        dbus_connection_close(m_dbus); // private connection 必须先 close 再 unref
        dbus_connection_unref(m_dbus);
        m_dbus = NULL;
    }
}

void STrayIconMgr::registerToWatcher()
{
    if (!m_dbus)
        return;
    DBusMessage *msg = dbus_message_new_method_call(
        WATCHER_SERVICE, WATCHER_PATH, WATCHER_SERVICE, "RegisterStatusNotifierItem");
    if (!msg)
        return;
    // 传 service name：watcher 会据此连回本进程读取 /StatusNotifierItem 属性
    const char *service = m_serviceName.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(m_dbus, msg, NULL);
    dbus_message_unref(msg);
    dbus_connection_flush(m_dbus);
    m_registered = true;
    m_watcherSeen = true;
    SLOG_FMTI("registered to watcher as %s", service);
}

WlTrayIcon *STrayIconMgr::firstIcon()
{
    if (m_icons.empty())
        return NULL;
    return m_icons.begin()->second;
}

void STrayIconMgr::onHostMenuOpened()
{
    /* 应用登记了菜单 provider：宿主菜单就是应用菜单本身，点项会回投
     * WM_COMMAND，不需要再让应用弹一份自己的菜单。 */
    if (menuHasProvider(firstIcon()))
    {
        if (trayTrace())
            fprintf(stderr, "[tray] host menu opened (provider, no notify)\n");
        return;
    }
    unsigned int now = GetTickCount();
    /* GNOME 的 Event("opened") 后面紧跟着 AboutToShow(0)，两者属于同一次
     * 点击；而且 SOUI 自己的菜单弹出来后指针仍停在托盘图标上，GNOME 会在
     * 宿主菜单被关掉后立刻重新 toggle，形成"开了又开"的回环。一段时间内
     * 只认第一次，既合并两条信号也吃掉回环里的重复打开。 */
    if (m_lastMenuClickTick && (unsigned int)(now - m_lastMenuClickTick) < TRAY_MENU_DEDUP_MS)
    {
        if (trayTrace())
            fprintf(stderr, "[tray] host menu opened (suppressed, dt=%ums)\n",
                    (unsigned int)(now - m_lastMenuClickTick));
        return;
    }
    m_lastMenuClickTick = now;
    /* GNOME 这条路不带坐标，SOUI 的菜单会弹在指针离开窗口时的旧位置。
     * 锚到面板指示器区，让菜单出现在托盘图标附近；KDE 的 ContextMenu(x,y)
     * 给过真实坐标时不动它。 */
    if (!m_lastClickPosTick || (unsigned int)(now - m_lastClickPosTick) > TRAY_POS_HINT_MS)
        anchorCursorToTrayArea();
    if (trayTrace())
        fprintf(stderr, "[tray] host menu opened -> WM_RBUTTONDOWN\n");
    postTrayNotify(firstIcon(), WM_RBUTTONDOWN);
}

//---------------------------------------------------------------------------
// 应用登记的托盘菜单（Wayland）：把 SMenuEx 之类的菜单导出成 dbusmenu
//---------------------------------------------------------------------------
BOOL STrayIconMgr::SetMenuProvider(HWND hWnd, UINT uID, const TRAYMENUPROVIDER *pProvider)
{
    if (!hWnd)
        return FALSE;
    if (pProvider && (!pProvider->pfnCount || !pProvider->pfnItem))
        return FALSE;

    TrayKey key((void *)hWnd, uID);
    std::map<TrayKey, WlTrayIcon *>::iterator it = m_icons.find(key);
    if (it == m_icons.end())
    {
        /* 图标还没 NIM_ADD：先挂起，addIcon 时绑定。"先注册、后显示托盘"也能
         * 正常工作，应用不用关心调用顺序。 */
        if (pProvider)
            m_pendingMenus[key] = *pProvider;
        else
            m_pendingMenus.erase(key);
        return TRUE;
    }

    WlTrayIcon *icon = it->second;
    if (pProvider)
    {
        icon->menuProvider = *pProvider;
        icon->menuProvided = true;
    }
    else
    {
        memset(&icon->menuProvider, 0, sizeof(icon->menuProvider));
        icon->menuProvided = false;
    }
    rebuildMenu(icon);
    emitMenuLayoutUpdated(icon->menuRevision);
    if (trayTrace())
        fprintf(stderr, "[tray] menu provider %s, %u root items\n",
                pProvider ? "set" : "cleared", (unsigned)icon->menuItems.size());
    return TRUE;
}

BOOL STrayIconMgr::UpdateMenu(HWND hWnd, UINT uID)
{
    TrayKey key((void *)hWnd, uID);
    std::map<TrayKey, WlTrayIcon *>::iterator it = m_icons.find(key);
    if (it == m_icons.end() || !it->second->menuProvided)
        return FALSE;
    rebuildMenu(it->second);
    emitMenuLayoutUpdated(it->second->menuRevision);
    return TRUE;
}

void STrayIconMgr::collectMenu(WlTrayIcon *icon, UINT uSubMenu, int depth,
                               std::vector<HostMenuItem> &out, int &nextId)
{
    out.clear();
    if (!icon || !icon->menuProvided || depth > 4)
        return;
    const TRAYMENUPROVIDER &p = icon->menuProvider;
    if (!p.pfnCount || !p.pfnItem)
        return;
    int count = p.pfnCount(icon->data.hWnd, icon->data.uID, uSubMenu, p.pCtx);
    if (count <= 0)
        return;
    if (count > 256)
        count = 256; // 防御：应用回调给异常值时别把 D-Bus 消息撑爆
    for (int i = 0; i < count; ++i)
    {
        TRAYMENUITEMW item;
        memset(&item, 0, sizeof(item));
        if (!p.pfnItem(icon->data.hWnd, icon->data.uID, uSubMenu, i, &item, p.pCtx))
            continue;
        HostMenuItem node;
        node.id = nextId++;
        node.uID = item.uID;
        node.uFlags = item.uFlags;
        node.label = wideToUtf8(item.pszText);
        out.push_back(node);
        if (item.uSubMenu && !(item.uFlags & TRMIF_SEPARATOR))
            collectMenu(icon, item.uSubMenu, depth + 1, out.back().children, nextId);
    }
}

void STrayIconMgr::rebuildMenu(WlTrayIcon *icon)
{
    if (!icon)
        return;
    int nextId = 1;
    collectMenu(icon, 0, 0, icon->menuItems, nextId);
    icon->menuRevision++; // 内容/顺序变了，宿主据此重取布局
}

HostMenuItem *STrayIconMgr::findMenuItem(std::vector<HostMenuItem> &items, int id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].id == id)
            return &items[i];
        HostMenuItem *child = findMenuItem(items[i].children, id);
        if (child)
            return child;
    }
    return NULL;
}

void STrayIconMgr::postMenuCommand(WlTrayIcon *icon, UINT cmdId)
{
    if (!icon || !cmdId)
        return;
    HWND target = icon->menuProvider.hWndCmd ? icon->menuProvider.hWndCmd : icon->data.hWnd;
    if (!target)
        return;
    BOOL ok = ::PostMessage(target, WM_COMMAND, MAKEWPARAM(cmdId, 0), 0);
    if (trayTrace())
        fprintf(stderr, "[tray] PostMessage hwnd=%p WM_COMMAND id=%u -> %d\n",
                target, (unsigned)cmdId, (int)ok);
}

void STrayIconMgr::emitMenuLayoutUpdated(unsigned int revision)
{
    if (!m_dbus)
        return;
    DBusMessage *msg = dbus_message_new_signal(SNI_MENU_PATH, DBUSMENU_IFACE, "LayoutUpdated");
    if (!msg)
        return;
    dbus_uint32_t rev = revision;
    int parent = 0;
    dbus_message_append_args(msg, DBUS_TYPE_UINT32, &rev, DBUS_TYPE_INT32, &parent,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(m_dbus, msg, NULL);
    dbus_message_unref(msg);
    dbus_connection_flush(m_dbus);
}

/* GNOME 的 appindicator 只在宿主菜单打开时发 Event("opened")，不带任何坐标；
 * 此时指针在 Shell 面板上，不在本进程任何 surface 内，swinx 的光标缓存停在
 * 指针离开窗口时的位置，SOUI 的托盘菜单就会弹到窗口里的任意位置。这里把光标
 * 锚到面板上的指示器区（默认屏幕右上角）。
 * SWINX_TRAY_ANCHOR="x,y" 可覆盖，负值表示从右/下边界往内数，
 * 默认 "-12,34" = 距右边缘 12px、顶栏下沿 2px（Ubuntu 面板高 32）。 */
void STrayIconMgr::anchorCursorToTrayArea()
{
    if (!m_conn)
        return;
    int x = TRAY_ANCHOR_DEFAULT_X;
    int y = TRAY_ANCHOR_DEFAULT_Y;
    const char *env = getenv("SWINX_TRAY_ANCHOR");
    if (env && *env)
    {
        char *end = NULL;
        long vx = strtol(env, &end, 10);
        if (end && (*end == ',' || *end == ';'))
        {
            long vy = strtol(end + 1, NULL, 10);
            x = (int)vx;
            y = (int)vy;
        }
    }
    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);
    if (cx <= 0 || cy <= 0)
        return;
    if (x < 0) x = cx + x;
    if (y < 0) y = cy + y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    m_conn->SetCursorPos(x, y);
    if (trayTrace())
        fprintf(stderr, "[tray] anchor cursor to (%d,%d) of %dx%d\n", x, y, cx, cy);
}

void STrayIconMgr::emitSignal(const char *name)
{
    if (!m_dbus)
        return;
    DBusMessage *msg = dbus_message_new_signal(SNI_PATH, SNI_IFACE, name);
    if (!msg)
        return;
    dbus_connection_send(m_dbus, msg, NULL);
    dbus_message_unref(msg);
}

bool STrayIconMgr::findIcon(PNOTIFYICONDATAA lpData, WlTrayIcon **out)
{
    if (!lpData)
        return false;
    // X11 版按 (hWnd,uID) 定位图标；这里保持同口径
    TrayKey key = iconKey(lpData);
    std::map<TrayKey, WlTrayIcon *>::iterator it = m_icons.find(key);
    if (it == m_icons.end())
        return false;
    if (out)
        *out = it->second;
    return true;
}

BOOL STrayIconMgr::addIcon(PNOTIFYICONDATAA lpData)
{
    if (!lpData || !IsWindow(lpData->hWnd))
        return FALSE;
    WlTrayIcon *existing = NULL;
    if (findIcon(lpData, &existing))
        return FALSE; // 同 (hWnd,uID) 已存在，Win32 NIM_ADD 语义也是失败

    WlTrayIcon *icon = new WlTrayIcon();
    memset(&icon->data, 0, sizeof(icon->data));
    memcpy(&icon->data, lpData, sizeof(NOTIFYICONDATAA));
    icon->pixW = icon->pixH = 0;
    memset(&icon->menuProvider, 0, sizeof(icon->menuProvider));
    icon->menuProvided = false;
    icon->menuRevision = 0;
    icon->menuItems.clear();
    if (lpData->uFlags & NIF_ICON)
        iconPixels(lpData->hIcon, icon->pixmap, icon->pixW, icon->pixH);
    if (lpData->uFlags & NIF_TIP && lpData->szTip[0])
        icon->tip = lpData->szTip;
    // Title 取 szTip 的第一段，作为 tooltip 的标题显示
    icon->title = icon->tip;

    TrayKey key = iconKey(lpData);
    m_icons[key] = icon;

    /* 应用在 NIM_ADD 之前登记过菜单：现在绑定。 */
    std::map<TrayKey, TRAYMENUPROVIDER>::iterator pit = m_pendingMenus.find(key);
    if (pit != m_pendingMenus.end())
    {
        icon->menuProvider = pit->second;
        icon->menuProvided = true;
        m_pendingMenus.erase(pit);
        rebuildMenu(icon);
    }

    emitSignal("NewIcon");
    emitSignal("NewTitle");
    emitSignal("NewToolTip");
    return TRUE;
}

BOOL STrayIconMgr::modifyIcon(PNOTIFYICONDATAA lpData)
{
    WlTrayIcon *icon = NULL;
    if (!findIcon(lpData, &icon) || !lpData)
        return FALSE;
    if (lpData->uFlags & NIF_ICON)
    {
        iconPixels(lpData->hIcon, icon->pixmap, icon->pixW, icon->pixH);
        icon->data.hIcon = lpData->hIcon;
        emitSignal("NewIcon");
    }
    if (lpData->uFlags & NIF_TIP)
    {
        icon->tip = lpData->szTip;
        icon->title = icon->tip;
        memcpy(icon->data.szTip, lpData->szTip, sizeof(icon->data.szTip));
        emitSignal("NewToolTip");
        emitSignal("NewTitle");
    }
    if (lpData->uFlags & NIF_MESSAGE)
        icon->data.uCallbackMessage = lpData->uCallbackMessage;
    return TRUE;
}

BOOL STrayIconMgr::delIcon(PNOTIFYICONDATAA lpData)
{
    if (!lpData)
        return FALSE;
    TrayKey key = iconKey(lpData);
    std::map<TrayKey, WlTrayIcon *>::iterator it = m_icons.find(key);
    if (it == m_icons.end())
        return FALSE;
    delete it->second;
    m_icons.erase(it);
    // 没有图标了就把 Status 换成 Passive（hiding 语义）；SNI 通过属性变化传播
    emitSignal("NewIcon");
    return TRUE;
}

BOOL STrayIconMgr::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    switch (dwMessage)
    {
    case NIM_ADD:    return addIcon(lpData);
    case NIM_MODIFY: return modifyIcon(lpData);
    case NIM_DELETE: return delIcon(lpData);
    }
    return FALSE;
}

void STrayIconMgr::pump()
{
    if (!m_dbus)
        return;
    // 每次定时器 tick 把 socket 上排队的消息派发完，并立即 flush 回复。
    // SNI 的请求-应答很少（属性读取 + 点击），不需要 SAtSpi 那种 20ms 预算。
    for (int i = 0; i < 32; ++i)
    {
        if (!dbus_connection_read_write_dispatch(m_dbus, 0))
            break;
        dbus_connection_flush(m_dbus);
        if (dbus_connection_get_dispatch_status(m_dbus) != DBUS_DISPATCH_DATA_REMAINS)
            break;
    }
}

void STrayIconMgr::noteClickPosition(int x, int y)
{
    if (m_conn)
        m_conn->SetCursorPos(x, y);
    /* 记下"host 给了真实坐标"的时刻：onHostMenuOpened 据此决定还要不要用
     * 面板锚点覆盖（KDE 的 ContextMenu/Activate 路径不该被覆盖）。 */
    m_lastClickPosTick = GetTickCount();
}

#endif // SWINX_HAVE_SNI
