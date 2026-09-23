/*
 * swinx — Linux 无障碍桥接实现（MSAA <-> AT-SPI2 over D-Bus）
 * 设计说明见 SAtSpi.h 头部。
 */
#include "SAtSpi.h"

#include <windows.h>
#include <oleacc.h>

#include <dbus/dbus.h>

#include <poll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "SwinxAccGlue.h"

#include "SConnection.h"

#include "log.h"
#define kLogTag "SwinxAtSpi"
/* ------------------------------------------------------------------ */
/* 常量                                                                */
/* ------------------------------------------------------------------ */

#define ATSPI_NAME_REGISTRY "org.a11y.atspi.Registry"
#define ATSPI_PATH_ROOT "/org/a11y/atspi/accessible/root"
#define ATSPI_IFACE_SOCKET "org.a11y.atspi.Socket"

#define ATSPI_IFACE_ACCESSIBLE "org.a11y.atspi.Accessible"
#define ATSPI_IFACE_COMPONENT "org.a11y.atspi.Component"
#define ATSPI_IFACE_ACTION "org.a11y.atspi.Action"
#define ATSPI_IFACE_APPLICATION "org.a11y.atspi.Application"
#define ATSPI_IFACE_CACHE "org.a11y.atspi.Cache"
#define ATSPI_IFACE_PROPERTIES "org.freedesktop.DBus.Properties"
#define ATSPI_IFACE_INTROSPECTABLE "org.freedesktop.DBus.Introspectable"

#define ATSPI_PATH_NULL "/org/a11y/atspi/null"
#define ATSPI_PATH_PREFIX "/org/a11y/atspi/accessible/"

/* swinx 的 winuser.h 目前只定义了 EVENT_OBJECT_*，这里补齐事件桥需要的
 * EVENT_SYSTEM_*（取值与 Win32 一致）。 */
#ifndef EVENT_SYSTEM_FOREGROUND
#define EVENT_SYSTEM_FOREGROUND 0x0003
#endif
#ifndef EVENT_SYSTEM_DIALOGSTART
#define EVENT_SYSTEM_DIALOGSTART 0x0010
#endif
#ifndef EVENT_SYSTEM_DIALOGEND
#define EVENT_SYSTEM_DIALOGEND 0x0011
#endif

/* AT-SPI role（atspi-constants.h 的 AtspiRole） */
enum
{
    ATSPI_ROLE_INVALID = 0,
    ATSPI_ROLE_FRAME = 23,
    ATSPI_ROLE_DIALOG = 16,
    ATSPI_ROLE_WINDOW = 69,
    ATSPI_ROLE_APPLICATION = 75,
    ATSPI_ROLE_PANEL = 39,
    ATSPI_ROLE_FILLER = 20,
    ATSPI_ROLE_LABEL = 29,
    ATSPI_ROLE_STATIC = 116,
    ATSPI_ROLE_TEXT = 61,
    ATSPI_ROLE_ENTRY = 79,
    ATSPI_ROLE_PASSWORD_TEXT = 40,
    ATSPI_ROLE_BUTTON = 43,
    ATSPI_ROLE_PUSH_BUTTON = 43,
    ATSPI_ROLE_TOGGLE_BUTTON = 62,
    ATSPI_ROLE_CHECK_BOX = 7,
    ATSPI_ROLE_RADIO_BUTTON = 44,
    ATSPI_ROLE_COMBO_BOX = 11,
    ATSPI_ROLE_LIST = 31,
    ATSPI_ROLE_LIST_ITEM = 32,
    ATSPI_ROLE_LIST_BOX = 98,
    ATSPI_ROLE_MENU = 33,
    ATSPI_ROLE_MENU_ITEM = 35,
    ATSPI_ROLE_MENU_BAR = 34,
    ATSPI_ROLE_PAGE_TAB = 37,
    ATSPI_ROLE_PAGE_TAB_LIST = 38,
    ATSPI_ROLE_PROGRESS_BAR = 42,
    ATSPI_ROLE_SCROLL_BAR = 48,
    ATSPI_ROLE_SLIDER = 51,
    ATSPI_ROLE_SPIN_BUTTON = 52,
    ATSPI_ROLE_STATUS_BAR = 54,
    ATSPI_ROLE_TOOL_BAR = 63,
    ATSPI_ROLE_TOOL_TIP = 64,
    ATSPI_ROLE_TREE = 65,
    ATSPI_ROLE_TREE_ITEM = 91,
    ATSPI_ROLE_SEPARATOR = 50,
    ATSPI_ROLE_GROUPING = 99,
    ATSPI_ROLE_IMAGE = 27,
    ATSPI_ROLE_LINK = 88,
    ATSPI_ROLE_TABLE = 55,
    ATSPI_ROLE_TABLE_CELL = 56,
    ATSPI_ROLE_COLUMN_HEADER = 10,
    ATSPI_ROLE_ROW_HEADER = 47,
    ATSPI_ROLE_UNKNOWN = 67,
};

/* AT-SPI state（atspi-constants.h 的 AtspiStateType） */
enum
{
    ATSPI_STATE_ACTIVE = 1,
    ATSPI_STATE_BUSY = 3,
    ATSPI_STATE_CHECKED = 4,
    ATSPI_STATE_COLLAPSED = 5,
    ATSPI_STATE_EDITABLE = 7,
    ATSPI_STATE_ENABLED = 8,
    ATSPI_STATE_EXPANDED = 10,
    ATSPI_STATE_FOCUSABLE = 11,
    ATSPI_STATE_FOCUSED = 12,
    ATSPI_STATE_IS_DEFAULT = 39,
    ATSPI_STATE_INDETERMINATE = 32,
    ATSPI_STATE_MULTISELECTABLE = 18,
    ATSPI_STATE_PRESSED = 20,
    ATSPI_STATE_RESIZABLE = 21,
    ATSPI_STATE_SELECTABLE = 22,
    ATSPI_STATE_SELECTED = 23,
    ATSPI_STATE_SENSITIVE = 24,
    ATSPI_STATE_SHOWING = 25,
    ATSPI_STATE_VISIBLE = 30,
    ATSPI_STATE_VISITED = 40,
    ATSPI_STATE_HAS_POPUP = 42,
    ATSPI_STATE_READ_ONLY = 43,
    ATSPI_STATE_ANIMATED = 35,
};

namespace
{
    /* ------------------------------------------------------------------ */
    /* 对象路径与节点标识                                                  */
    /* ------------------------------------------------------------------ */

    struct AccNode
    {
        bool bRoot = false;  // /org/a11y/atspi/accessible/root
        bool bWindow = false; // 顶层窗口节点（OBJID_WINDOW）
        HWND hwnd = 0;
        swinx_stl::vector<LONG> chain; // client 之后的 MSAA child id 链（1-based）
    };

    /* 前向声明（定义在本文件后半部分） */
    swinx_stl::vector<HWND> TopLevelWindows();
    long NodeIndexInParent(const AccNode &node);
    swinx_stl::string NodePath(const AccNode &node);
    DBusHandlerResult AtSpiMessageFunction(DBusConnection *conn, DBusMessage *msg, void *userData);
    void CALLBACK AtSpiTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
    void CALLBACK AtSpiWinEventHook(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                    LONG idChild, DWORD idEventThread, DWORD dwmsEventTime);

    /* ------------------------------------------------------------------ */
    /* 小工具                                                             */
    /* ------------------------------------------------------------------ */

    /* swinx 的 WCHAR 在 Linux 是 4 字节（UTF-32），不能按 UTF-16 处理。 */
    swinx_stl::string Utf8FromBstr(BSTR bstr)
    {
        swinx_stl::string out;
        if (!bstr)
            return out;
        const unsigned int *u = reinterpret_cast<const unsigned int *>(bstr);
        for (; *u; ++u)
        {
            unsigned int c = *u;
            if (c < 0x80)
            {
                out += (char)c;
            }
            else if (c < 0x800)
            {
                out += (char)(0xC0 | (c >> 6));
                out += (char)(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += (char)(0xE0 | (c >> 12));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
            else if (c < 0x110000)
            {
                out += (char)(0xF0 | (c >> 18));
                out += (char)(0x80 | ((c >> 12) & 0x3F));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    /* 取 MSAA 字符串属性（get_accName / get_accValue / ...）。
     * 这里不用成员函数指针传参，避免 IAccessible 多继承下的类型适配问题。 */
    enum AccStrField
    {
        ACC_STR_NAME,
        ACC_STR_VALUE,
        ACC_STR_DESCRIPTION,
        ACC_STR_HELP,
        ACC_STR_DEFAULTACTION
    };

    swinx_stl::string GetAccString(IAccessible *acc, LONG childId, AccStrField field)
    {
        swinx_stl::string ret;
        if (!acc)
            return ret;
        VARIANT varChild;
        VariantInit(&varChild);
        varChild.vt = VT_I4;
        varChild.lVal = childId;
        BSTR bstr = NULL;
        HRESULT hr = E_FAIL;
        switch (field)
        {
        case ACC_STR_NAME:
            hr = acc->get_accName(varChild, &bstr);
            break;
        case ACC_STR_VALUE:
            hr = acc->get_accValue(varChild, &bstr);
            break;
        case ACC_STR_DESCRIPTION:
            hr = acc->get_accDescription(varChild, &bstr);
            break;
        case ACC_STR_HELP:
            hr = acc->get_accHelp(varChild, &bstr);
            break;
        case ACC_STR_DEFAULTACTION:
            hr = acc->get_accDefaultAction(varChild, &bstr);
            break;
        }
        VariantClear(&varChild);
        if (hr == S_OK && bstr)
        {
            ret = Utf8FromBstr(bstr);
            SysFreeString(bstr);
        }
        return ret;
    }



    /* 路径编码（必须与 ParseNodePath 严格互逆）：
     *   root         = /org/a11y/atspi/accessible/root
     *   window       = /org/a11y/atspi/accessible/w<hex hwnd>
     *   client root  = /org/a11y/atspi/accessible/w<hex hwnd>_c
     *   chain node   = /org/a11y/atspi/accessible/w<hex hwnd>_c_1_2...
     * 关键点：hwnd 与后缀之间必须用 '_' 分隔。'c' 是合法十六进制字符，
     * 若直接拼 "w1800002c"，ParseNodePath 的 strtoull(base 16) 会把 'c'
     * 当作 hwnd 的一部分（0x1800002c），该路径随即被解析成一个"顶层窗口"
     * ——窗口节点的 ChildCount 恒为 1，客户区根路径再追加 'c'，形成
     * w1800002c -> w1800002cc -> w1800002ccc 的无限展开（leak.log 已复现）。
     * '_' 不是十六进制字符，strtoull 会在其处停住，编码自此无歧义。 */
    swinx_stl::string NodePath(const AccNode &node)
    {
        if (node.bRoot)
            return ATSPI_PATH_ROOT;
        char buf[64];
        snprintf(buf, sizeof(buf), "w%llx", (unsigned long long)(UINT_PTR)node.hwnd);
        swinx_stl::string path = ATSPI_PATH_PREFIX + swinx_stl::string(buf);
        if (node.bWindow)
            return path;
        path += "_c";
        for (size_t i = 0; i < node.chain.size(); i++)
        {
            path += "_";
            snprintf(buf, sizeof(buf), "%d", node.chain[i]);
            path += buf;
        }
        return path;
    }

    bool ParseNodePath(const char *path, AccNode &node)
    {
        node = AccNode();
        if (!path)
            return false;
        if (strcmp(path, ATSPI_PATH_ROOT) == 0)
        {
            node.bRoot = true;
            return true;
        }
        if (strncmp(path, ATSPI_PATH_PREFIX, strlen(ATSPI_PATH_PREFIX)) != 0)
            return false;
        const char *p = path + strlen(ATSPI_PATH_PREFIX);
        if (*p != 'w')
            return false;
        p++;
        char *end = NULL;
        unsigned long long hwnd = strtoull(p, &end, 16);
        if (end == p)
            return false;
        node.hwnd = (HWND)(UINT_PTR)hwnd;
        p = end;
        if (*p == 0)
        {
            node.bWindow = true;
            return true;
        }
        if (*p != '_')
            return false;
        p++;
        if (*p != 'c')
            return false;
        p++;
        if (*p == 0)
            return true; // 客户区根节点（chain 为空）
        while (*p == '_')
        {
            p++;
            char *e2 = NULL;
            long v = strtol(p, &e2, 10);
            if (e2 == p)
                return false;
            node.chain.push_back(v);
            p = e2;
        }
        return *p == 0;
    }

    /* ------------------------------------------------------------------ */
    /* MSAA 树导航                                                        */
    /* ------------------------------------------------------------------ */

    /* 解析结果：acc 是"用于查询"的对象，childId 为 CHILDID_SELF 时表示 acc
     * 自身就是目标；否则 acc 是父对象、childId 是简单元素的 id。 */
    struct Resolved
    {
        IAccessible *acc = NULL;
        LONG childId = CHILDID_SELF;
        bool ok = false;

        Resolved()
        {
        }
        ~Resolved()
        {
            if (acc)
                acc->Release();
        }

      private:
        Resolved(const Resolved &);
        Resolved &operator=(const Resolved &);
    };

    /* 沿 chain 逐级下钻。复用 swinx 的共享胶水助手（swinx/src/oleacc.cpp）：
     * 每次都重新向窗口发 WM_GETOBJECT 查询（SHostWnd 按 SOUI_ENABLE_ACC 决定
     * 返回 NULL 还是 IAccessible），现查现用现放——桥里不持有 IAccessible，
     * 返回 false 即路径已失效（控件被销毁/重建），绝不访问悬垂指针。 */
    bool ResolveNode(const AccNode &node, Resolved &out)
    {
        LONG childId = CHILDID_SELF;
        IAccessible *acc = NULL;
        HRESULT hr = SwinxAccResolvePath(node.hwnd, node.chain.empty() ? NULL : &node.chain[0],
                                         (LONG)node.chain.size(), &acc, &childId);
        if (hr != S_OK || !acc)
            return false;
        out.acc = acc;
        out.childId = childId;
        out.ok = true;
        return true;
    }

    /* ------------------------------------------------------------------ */
    /* role / state 映射                                                  */
    /* ------------------------------------------------------------------ */

    unsigned int RoleFromMsaA(long role)
    {
        switch (role)
        {
        case ROLE_SYSTEM_WINDOW:
            return ATSPI_ROLE_FRAME;
        case ROLE_SYSTEM_DIALOG:
            return ATSPI_ROLE_DIALOG;
        case ROLE_SYSTEM_CLIENT:
            return ATSPI_ROLE_PANEL;
        case ROLE_SYSTEM_PANE:
            return ATSPI_ROLE_PANEL;
        case ROLE_SYSTEM_GROUPING:
            return ATSPI_ROLE_GROUPING;
        case ROLE_SYSTEM_TITLEBAR:
            return ATSPI_ROLE_FRAME;
        case ROLE_SYSTEM_STATICTEXT:
            return ATSPI_ROLE_LABEL;
        case ROLE_SYSTEM_TEXT:
            return ATSPI_ROLE_TEXT;
        case ROLE_SYSTEM_PUSHBUTTON:
            return ATSPI_ROLE_BUTTON;
        case ROLE_SYSTEM_CHECKBUTTON:
            return ATSPI_ROLE_CHECK_BOX;
        case ROLE_SYSTEM_RADIOBUTTON:
            return ATSPI_ROLE_RADIO_BUTTON;
        case ROLE_SYSTEM_COMBOBOX:
        case ROLE_SYSTEM_DROPLIST:
            return ATSPI_ROLE_COMBO_BOX;
        case ROLE_SYSTEM_LIST:
            return ATSPI_ROLE_LIST;
        case ROLE_SYSTEM_LISTITEM:
            return ATSPI_ROLE_LIST_ITEM;
        case ROLE_SYSTEM_OUTLINE:
            return ATSPI_ROLE_TREE;
        case ROLE_SYSTEM_OUTLINEITEM:
            return ATSPI_ROLE_TREE_ITEM;
        case ROLE_SYSTEM_MENUPOPUP:
            return ATSPI_ROLE_MENU;
        case ROLE_SYSTEM_MENUITEM:
            return ATSPI_ROLE_MENU_ITEM;
        case ROLE_SYSTEM_MENUBAR:
            return ATSPI_ROLE_MENU_BAR;
        case ROLE_SYSTEM_PAGETAB:
            return ATSPI_ROLE_PAGE_TAB;
        case ROLE_SYSTEM_PAGETABLIST:
            return ATSPI_ROLE_PAGE_TAB_LIST;
        case ROLE_SYSTEM_PROGRESSBAR:
            return ATSPI_ROLE_PROGRESS_BAR;
        case ROLE_SYSTEM_SCROLLBAR:
            return ATSPI_ROLE_SCROLL_BAR;
        case ROLE_SYSTEM_SLIDER:
            return ATSPI_ROLE_SLIDER;
        case ROLE_SYSTEM_SPINBUTTON:
            return ATSPI_ROLE_SPIN_BUTTON;
        case ROLE_SYSTEM_STATUSBAR:
            return ATSPI_ROLE_STATUS_BAR;
        case ROLE_SYSTEM_TOOLBAR:
            return ATSPI_ROLE_TOOL_BAR;
        case ROLE_SYSTEM_TOOLTIP:
            return ATSPI_ROLE_TOOL_TIP;
        case ROLE_SYSTEM_SEPARATOR:
            return ATSPI_ROLE_SEPARATOR;
        case ROLE_SYSTEM_GRAPHIC:
            return ATSPI_ROLE_IMAGE;
        case ROLE_SYSTEM_LINK:
            return ATSPI_ROLE_LINK;
        case ROLE_SYSTEM_TABLE:
            return ATSPI_ROLE_TABLE;
        case ROLE_SYSTEM_CELL:
            return ATSPI_ROLE_TABLE_CELL;
        case ROLE_SYSTEM_COLUMNHEADER:
            return ATSPI_ROLE_COLUMN_HEADER;
        case ROLE_SYSTEM_ROWHEADER:
            return ATSPI_ROLE_ROW_HEADER;
        case ROLE_SYSTEM_APPLICATION:
            return ATSPI_ROLE_APPLICATION;
        default:
            return ATSPI_ROLE_UNKNOWN;
        }
    }

    /* AT-SPI role 的可读名。libatspi 只在遇到未知 role 时才调 GetRoleName，
     * 这里给一份够用的英文名表即可。 */
    const char *RoleNameFromAtSpi(unsigned int role)
    {
        switch (role)
        {
        case ATSPI_ROLE_APPLICATION:
            return "application";
        case ATSPI_ROLE_WINDOW:
        case ATSPI_ROLE_FRAME:
            return "frame";
        case ATSPI_ROLE_DIALOG:
            return "dialog";
        case ATSPI_ROLE_PANEL:
            return "panel";
        case ATSPI_ROLE_FILLER:
            return "filler";
        case ATSPI_ROLE_LABEL:
            return "label";
        case ATSPI_ROLE_STATIC:
            return "static";
        case ATSPI_ROLE_TEXT:
            return "text";
        case ATSPI_ROLE_ENTRY:
            return "entry";
        case ATSPI_ROLE_BUTTON:
            return "push button";
        case ATSPI_ROLE_TOGGLE_BUTTON:
            return "toggle button";
        case ATSPI_ROLE_CHECK_BOX:
            return "check box";
        case ATSPI_ROLE_RADIO_BUTTON:
            return "radio button";
        case ATSPI_ROLE_COMBO_BOX:
            return "combo box";
        case ATSPI_ROLE_LIST:
            return "list";
        case ATSPI_ROLE_LIST_ITEM:
            return "list item";
        case ATSPI_ROLE_LIST_BOX:
            return "list box";
        case ATSPI_ROLE_MENU:
            return "menu";
        case ATSPI_ROLE_MENU_ITEM:
            return "menu item";
        case ATSPI_ROLE_MENU_BAR:
            return "menu bar";
        case ATSPI_ROLE_PAGE_TAB:
            return "page tab";
        case ATSPI_ROLE_PAGE_TAB_LIST:
            return "page tab list";
        case ATSPI_ROLE_PROGRESS_BAR:
            return "progress bar";
        case ATSPI_ROLE_SCROLL_BAR:
            return "scroll bar";
        case ATSPI_ROLE_SLIDER:
            return "slider";
        case ATSPI_ROLE_SPIN_BUTTON:
            return "spin button";
        case ATSPI_ROLE_STATUS_BAR:
            return "status bar";
        case ATSPI_ROLE_TOOL_BAR:
            return "tool bar";
        case ATSPI_ROLE_TOOL_TIP:
            return "tool tip";
        case ATSPI_ROLE_TREE:
            return "tree";
        case ATSPI_ROLE_TREE_ITEM:
            return "tree item";
        case ATSPI_ROLE_SEPARATOR:
            return "separator";
        case ATSPI_ROLE_GROUPING:
            return "grouping";
        case ATSPI_ROLE_IMAGE:
            return "image";
        case ATSPI_ROLE_LINK:
            return "link";
        case ATSPI_ROLE_TABLE:
            return "table";
        case ATSPI_ROLE_TABLE_CELL:
            return "table cell";
        case ATSPI_ROLE_COLUMN_HEADER:
            return "column header";
        case ATSPI_ROLE_ROW_HEADER:
            return "row header";
        default:
            return "unknown";
        }
    }

    void AppendMsaAStates(swinx_stl::vector<unsigned int> &states, DWORD msaa)
    {
        struct Map
        {
            DWORD bit;
            unsigned int state;
        };
        static const Map kMap[] = {
            { STATE_SYSTEM_SELECTED, ATSPI_STATE_SELECTED },
            { STATE_SYSTEM_FOCUSED, ATSPI_STATE_FOCUSED },
            { STATE_SYSTEM_PRESSED, ATSPI_STATE_PRESSED },
            { STATE_SYSTEM_CHECKED, ATSPI_STATE_CHECKED },
            { STATE_SYSTEM_MIXED, ATSPI_STATE_INDETERMINATE },
            { STATE_SYSTEM_READONLY, ATSPI_STATE_READ_ONLY },
            { STATE_SYSTEM_DEFAULT, ATSPI_STATE_IS_DEFAULT },
            { STATE_SYSTEM_EXPANDED, ATSPI_STATE_EXPANDED },
            { STATE_SYSTEM_COLLAPSED, ATSPI_STATE_COLLAPSED },
            { STATE_SYSTEM_BUSY, ATSPI_STATE_BUSY },
            { STATE_SYSTEM_ANIMATED, ATSPI_STATE_ANIMATED },
            { STATE_SYSTEM_SIZEABLE, ATSPI_STATE_RESIZABLE },
            { STATE_SYSTEM_FOCUSABLE, ATSPI_STATE_FOCUSABLE },
            { STATE_SYSTEM_SELECTABLE, ATSPI_STATE_SELECTABLE },
            { STATE_SYSTEM_TRAVERSED, ATSPI_STATE_VISITED },
            { STATE_SYSTEM_MULTISELECTABLE, ATSPI_STATE_MULTISELECTABLE },
            { STATE_SYSTEM_HASPOPUP, ATSPI_STATE_HAS_POPUP },
        };
        for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++)
        {
            if (msaa & kMap[i].bit)
                states.push_back(kMap[i].state);
        }
        if (!(msaa & STATE_SYSTEM_UNAVAILABLE))
        {
            states.push_back(ATSPI_STATE_ENABLED);
            states.push_back(ATSPI_STATE_SENSITIVE);
        }
        if (!(msaa & STATE_SYSTEM_INVISIBLE))
            states.push_back(ATSPI_STATE_VISIBLE);
        if (!(msaa & STATE_SYSTEM_OFFSCREEN))
            states.push_back(ATSPI_STATE_SHOWING);
    }

    swinx_stl::vector<unsigned int> NodeStates(const AccNode &node)
    {
        swinx_stl::vector<unsigned int> states;
        if (node.bRoot)
        {
            states.push_back(ATSPI_STATE_ACTIVE);
            states.push_back(ATSPI_STATE_ENABLED);
            states.push_back(ATSPI_STATE_SENSITIVE);
            states.push_back(ATSPI_STATE_SHOWING);
            states.push_back(ATSPI_STATE_VISIBLE);
            return states;
        }
        if (node.bWindow)
        {
            states.push_back(ATSPI_STATE_ACTIVE);
            states.push_back(ATSPI_STATE_ENABLED);
            states.push_back(ATSPI_STATE_SENSITIVE);
            states.push_back(ATSPI_STATE_SHOWING);
            states.push_back(ATSPI_STATE_VISIBLE);
            states.push_back(ATSPI_STATE_RESIZABLE);
            return states;
        }
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
        {
            states.push_back(ATSPI_STATE_VISIBLE);
            return states;
        }
        VARIANT varChild, varState;
        VariantInit(&varChild);
        VariantInit(&varState);
        varChild.vt = VT_I4;
        varChild.lVal = r.childId;
        if (r.acc->get_accState(varChild, &varState) == S_OK && varState.vt == VT_I4)
            AppendMsaAStates(states, (DWORD)varState.lVal);
        VariantClear(&varState);
        VariantClear(&varChild);
        return states;
    }

    unsigned int NodeRole(const AccNode &node)
    {
        if (node.bRoot)
            return ATSPI_ROLE_APPLICATION;
        if (node.bWindow)
            return ATSPI_ROLE_FRAME;
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
            return ATSPI_ROLE_UNKNOWN;
        VARIANT varChild, varRole;
        VariantInit(&varChild);
        VariantInit(&varRole);
        varChild.vt = VT_I4;
        varChild.lVal = r.childId;
        unsigned int role = ATSPI_ROLE_UNKNOWN;
        if (r.acc->get_accRole(varChild, &varRole) == S_OK && varRole.vt == VT_I4)
            role = RoleFromMsaA(varRole.lVal);
        VariantClear(&varRole);
        VariantClear(&varChild);
        return role;
    }

    swinx_stl::string NodeName(const AccNode &node)
    {
        if (node.bRoot)
            return "swinx application";
        if (node.bWindow)
        {
            char szText[512] = { 0 };
            if (::GetWindowTextA(node.hwnd, szText, sizeof(szText) - 1) > 0)
                return swinx_stl::string(szText);
            return "window";
        }
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
            return "";
        return GetAccString(r.acc, r.childId, ACC_STR_NAME);
    }

    swinx_stl::string NodeDescription(const AccNode &node)
    {
        if (node.bRoot || node.bWindow)
            return "";
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
            return "";
        return GetAccString(r.acc, r.childId, ACC_STR_DESCRIPTION);
    }

    long NodeChildCount(const AccNode &node)
    {
        if (node.bRoot)
            return (long)TopLevelWindows().size();
        if (node.bWindow)
            return 1; // 客户区根节点
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
            return 0;
        if (r.childId != CHILDID_SELF)
            return 0; // 简单元素没有子节点
        long count = 0;
        r.acc->get_accChildCount(&count);
        return count < 0 ? 0 : count;
    }

    /* ------------------------------------------------------------------ */
    /* 顶层窗口枚举                                                        */
    /* ------------------------------------------------------------------ */

    /* 本进程顶层窗口的候选表。窗口映射是 wndobj.cpp 的文件私有数据，为
     * 单一消费者在 WndMgr 上加公开枚举接口得不偿失，因此在这里自维护：
     *
     * 候选集 = EnsureInit 时的 EnumWindows 种子（覆盖 AT 钩子安装前已
     * 存在的窗口）∪ 此后 WinEvent（CREATE/SHOW）携带的宿主窗口。
     * **入口必须按进程过滤**：EnumWindows 走 X 根窗口枚举，回调会收到
     * 桌面上所有进程的顶层窗口，而 swinx 的 IsWindow（对外来窗口做
     * xcb_get_geometry）、IsWindowVisible（返回 VIEWABLE）、GetParent
     * （fromHwnd 落空返回 0）对它们全部放行——不过滤的话 root 的子列
     * 表会被整个桌面的窗口污染。判据用 GetWindowThreadProcessId(hwnd,
     * NULL)：窗口不在本进程映射中时返回 0，纯 map 查找、零 X 往返。
     *
     * 读取时再逐项实时校验（IsWindow + IsWindowVisible + GetParent==0）
     * 并剔除失效项——销毁/隐藏/层级变化都按调用瞬间的实时状态判定，不
     * 依赖事件不丢失（tooltip 平时隐藏，天然被可见性过滤排除，符合
     * AT-SPI 不暴露不可见窗口的语义）。 */
    swinx_stl::vector<HWND> &TopLevelCandidates()
    {
        static swinx_stl::vector<HWND> s_candidates;
        return s_candidates;
    }

    void TrackTopLevelCandidate(HWND hwnd)
    {
        if (!hwnd)
            return;
        swinx_stl::vector<HWND> &cands = TopLevelCandidates();
        for (size_t i = 0; i < cands.size(); i++)
            if (cands[i] == hwnd)
                return;
        cands.push_back(hwnd);
    }

    BOOL CALLBACK CbSeedTopLevelCandidates(HWND hwnd, LPARAM lParam)
    {
        (void)lParam;
        /* 只收本进程窗口（见上方注释），可见性/层级留给读取时的实时校验。 */
        if (::GetWindowThreadProcessId(hwnd, NULL) != 0)
            TrackTopLevelCandidate(hwnd);
        return TRUE;
    }

    void SeedTopLevelCandidates()
    {
        ::EnumWindows(CbSeedTopLevelCandidates, 0);
    }

    swinx_stl::vector<HWND> &TopLevelWindowsCache(swinx_stl::vector<HWND> *pOut)
    {
        static swinx_stl::vector<HWND> cache;
        static uint64_t tsCache = 0;
        uint64_t tsNow = GetTickCount64();
        if (pOut)
        {
            /* 顶层窗口列表变化不频繁，缓存 100ms，避免一次 Cache.GetItems
             * 里对每个节点都重新校验一遍。 */
            if (tsNow - tsCache > 100 || tsCache == 0)
            {
                swinx_stl::vector<HWND> &cands = TopLevelCandidates();
                /* 自愈：候选表为空说明种子时机早于窗口创建（首次
                 * NotifyWinEvent 可能发生在宿主窗口尚未登记时）。重新枚举
                 * 一次，节流 1s；候选表非空时不走这里，无额外开销。 */
                static uint64_t tsLastSeed = 0;
                if (cands.empty() && tsNow - tsLastSeed > 1000)
                {
                    tsLastSeed = tsNow;
                    SeedTopLevelCandidates();
                }
                swinx_stl::vector<HWND> wins;
                wins.reserve(cands.size());
                for (size_t i = 0; i < cands.size(); i++)
                {
                    HWND h = cands[i];
                    if (::IsWindow(h) && ::IsWindowVisible(h) && ::GetParent(h) == 0)
                        wins.push_back(h);
                }
                /* 顺带剔除已销毁的候选，控制候选表规模。 */
                swinx_stl::vector<HWND> alive;
                alive.reserve(cands.size());
                for (size_t i = 0; i < cands.size(); i++)
                    if (::IsWindow(cands[i]))
                        alive.push_back(cands[i]);
                cands.swap(alive);
                cache.swap(wins);
                tsCache = tsNow;
            }
            *pOut = cache;
        }
        return cache;
    }

    swinx_stl::vector<HWND> TopLevelWindows()
    {
        swinx_stl::vector<HWND> out;
        TopLevelWindowsCache(&out);
        return out;
    }

    /* ------------------------------------------------------------------ */
    /* D-Bus 应答辅助                                                      */
    /* ------------------------------------------------------------------ */

    struct AtSpi;

    void IterAppendString(DBusMessageIter *it, const char *s)
    {
        const char *v = s ? s : "";
        dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &v);
    }

    void IterAppendObjectRef(DBusMessageIter *it, const char *busName, const char *path)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &sub);
        IterAppendString(&sub, busName);
        const char *p = path ? path : ATSPI_PATH_NULL;
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_OBJECT_PATH, &p);
        dbus_message_iter_close_container(it, &sub);
    }

    void IterAppendRect(DBusMessageIter *it, int x, int y, int w, int h)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &sub);
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &x);
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &y);
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &w);
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &h);
        dbus_message_iter_close_container(it, &sub);
    }

    void IterAppendUint32Array(DBusMessageIter *it, const swinx_stl::vector<unsigned int> &vals)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "u", &sub);
        for (size_t i = 0; i < vals.size(); i++)
        {
            dbus_uint32_t v = (dbus_uint32_t)vals[i];
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_UINT32, &v);
        }
        dbus_message_iter_close_container(it, &sub);
    }

    void IterAppendStringArray(DBusMessageIter *it, const swinx_stl::vector<swinx_stl::string> &vals)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "s", &sub);
        for (size_t i = 0; i < vals.size(); i++)
            IterAppendString(&sub, vals[i].c_str());
        dbus_message_iter_close_container(it, &sub);
    }

    void IterAppendVariantString(DBusMessageIter *it, const swinx_stl::string &val)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "s", &sub);
        IterAppendString(&sub, val.c_str());
        dbus_message_iter_close_container(it, &sub);
    }

    void IterAppendVariantInt(DBusMessageIter *it, int val)
    {
        DBusMessageIter sub;
        dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "i", &sub);
        dbus_int32_t v = (dbus_int32_t)val;
        dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &v);
        dbus_message_iter_close_container(it, &sub);
    }

} // namespace

/* ================================================================== */
/* 桥主体                                                              */
/* ================================================================== */

namespace
{

    class AtSpi
    {
      public:
        static AtSpi &Inst()
        {
            static AtSpi s_inst;
            return s_inst;
        }

        void EnsureInit();
        void Shutdown();
        void Pump();

        /* 供 WinEvent 钩子调用 */
        void OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild);

        /* MSAA 事件 -> AT-SPI 信号 */
        void EmitStateChanged(HWND hwnd, const char *state, int value);
        void EmitPropertyChange(HWND hwnd, const char *prop);
        void EmitChildrenChanged(HWND hwnd, bool bAdd);
        void EmitFocusEvent(HWND hwnd);
        void EmitWindowEvent(HWND hwnd, const char *name);
        void EmitSignalFor(AccNode &node, const char *klass, const char *member,
                           const char *detail, dbus_int32_t detail1, dbus_int32_t detail2,
                           const char *variantType, const void *variantVal);

        const char *UniqueName() const
        {
            return m_uniqueName.c_str();
        }

        /* registry 根对象（desktop）的引用，root 节点的 Parent 用它 */
        const char *DesktopName() const
        {
            return m_desktopName.c_str();
        }

        const char *DesktopPath() const
        {
            return m_desktopPath.c_str();
        }

      private:
        AtSpi()
            : m_conn(NULL)
            , m_bRegistered(false)
            , m_bEmbedDone(false)
            , m_timerId(0)
            , m_hWinEventHook(NULL)
        {
        }

        AtSpi(const AtSpi &);
        AtSpi &operator=(const AtSpi &);

        bool ConnectBus();
        void DoEmbed();
        void EmitSignalInternal(const char *path, const char *klass, const char *member,
                                const char *detail, dbus_int32_t detail1, dbus_int32_t detail2,
                                const char *variantType, const void *variantVal);

        DBusConnection *m_conn;
        swinx_stl::string m_uniqueName;
        swinx_stl::string m_desktopName;
        swinx_stl::string m_desktopPath;
        bool m_bRegistered;
        bool m_bEmbedDone;
        UINT_PTR m_timerId;
        HWINEVENTHOOK m_hWinEventHook;
        uint64_t m_tsEmbedTry = 0;

        /* 信号队列：WinEvent 在主线程派发（SwinxDispatchPendingWinEvents）。
         * 若主线程直接 dbus_connection_send，会在持有窗口对象锁的同时等待
         * libdbus 连接锁；而泵线程在 read_write_dispatch 中持有连接锁处理
         * 请求时又要获取窗口对象锁（GetWindowTextA→fromHwnd 等）——形成
         * AB-BA 死锁（实测展开 tooltip 枚举即挂死）。因此主线程只入队，
         * 由泵线程在派发间隙统一发送。 */
        struct QueuedSig
        {
            swinx_stl::string path, klass, member, detail;
            dbus_int32_t detail1, detail2;
            swinx_stl::string variantType; /* "" / "(so)" / "i" / "s" */
            dbus_int32_t iVal;
            swinx_stl::string sVal;
            AccNode node;
        };
        std::mutex m_sigMtx;
        swinx_stl::deque<QueuedSig> m_sigQueue;
        void DrainSignals();

        /* 用可重入锁：Pump 处理 D-Bus 请求时会调用 MSAA，而后者可能经过
         * SendMessage(WM_GETOBJECT) 再次走到 LresultFromObject 触发
         * EnsureInit，同线程二次加锁必须安全。 */
        std::recursive_mutex m_mutex;
    };

    void AtSpi::EnsureInit()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_bRegistered)
            return;

        const char *disabled = getenv("SWINX_DISABLE_ATSPI");
        if (disabled && disabled[0] == '1')
        {
            SLOG_STMI() << "EnsureInit,skip,SWINX_DISABLE_ATSPI=1";
            return;
        }

        if (!ConnectBus())
        {
            /* a11y bus 可能晚于本进程就绪（开启 Screen Reader 时会话才拉起
             * at-spi-bus-launcher）：起一个 500ms 的慢速重试定时器，由 Pump
             * 驱动重试，直到总线可用。 */
            SLOG_STMW() << "EnsureInit,ConnectBus failed,will retry every 500ms";
            if (!m_timerId)
                m_timerId = ::SetTimer(0, 0, 500, &AtSpiTimerProc);
            return;
        }

        DBusError err;
        dbus_error_init(&err);
        /* libdbus 会拷贝 vtable，但用静态对象更稳妥：注册表在整个连接生命周期
         * 内都有效。 */
        static DBusObjectPathVTable vtable;
        memset(&vtable, 0, sizeof(vtable));
        vtable.message_function = &AtSpiMessageFunction;
        /* 用 fallback 覆盖 /org/a11y/atspi 下的全部动态路径。注意必须包含
         * /org/a11y/atspi/cache 子树（Cache.GetItems 的标准路径）：只注册
         * /accessible 时，cache 路径的请求落不到任何处理器，libdbus 对未
         * 处理的方法调用不保证自动回错，客户端（pyatspi 展开树时）会永久
         * 等待应答——表现为 accerciser 卡死且我方日志无任何记录。 */
        if (!dbus_connection_register_fallback(m_conn, "/org/a11y/atspi", &vtable, this))
        {
            SLOG_STMW() << "EnsureInit,abort,register_fallback failed";
            dbus_connection_unref(m_conn);
            m_conn = NULL;
            if (!m_timerId)
                m_timerId = ::SetTimer(0, 0, 500, &AtSpiTimerProc);
            return;
        }
        m_bRegistered = true;

        /* 挂进 swinx 消息循环：所有 D-Bus 处理都在同一个（UI）线程完成。
         * 若此前有慢速重试定时器，替换为正常的 50ms 泵。 */
        if (m_timerId)
            ::KillTimer(0, m_timerId);
        m_timerId = ::SetTimer(0, 0, 50, &AtSpiTimerProc);

        /* 事件桥：NotifyWinEvent -> AT-SPI 信号（标准 SetWinEventHook，winuser.h）。
         * 钩子只接收注册之后发出的事件，因此先用一次 EnumWindows 把已存在
         * 的窗口收进顶层候选表（见 TopLevelCandidates 注释）。 */
        m_hWinEventHook = ::SetWinEventHook(EVENT_MIN, EVENT_MAX, NULL, &AtSpiWinEventHook, 0, 0,
                                            WINEVENT_OUTOFCONTEXT);
        SeedTopLevelCandidates();
    }

    bool AtSpi::ConnectBus()
    {
        DBusError err;
        dbus_error_init(&err);

        swinx_stl::string address;
        const char *envAddr = getenv("AT_SPI_BUS");
        if (envAddr && envAddr[0])
        {
            address = envAddr;
        }
        else
        {
            DBusConnection *session = dbus_bus_get(DBUS_BUS_SESSION, &err);
            if (!session)
            {
                SLOG_STMW() << "ConnectBus,session bus failed,err="
                            << (err.message ? err.message : "?");
                dbus_error_free(&err);
                return false;
            }
            DBusMessage *msg = dbus_message_new_method_call("org.a11y.Bus", "/org/a11y/bus",
                                                            "org.a11y.Bus", "GetAddress");
            DBusMessage *reply = NULL;
            if (msg)
            {
                reply = dbus_connection_send_with_reply_and_block(session, msg, 1000, &err);
                dbus_message_unref(msg);
            }
            if (reply)
            {
                const char *addr = NULL;
                if (dbus_message_get_args(reply, &err, DBUS_TYPE_STRING, &addr,
                                          DBUS_TYPE_INVALID))
                    address = addr ? addr : "";
                dbus_message_unref(reply);
            }
            else if (dbus_error_is_set(&err))
            {
                SLOG_STMW() << "ConnectBus,org.a11y.Bus GetAddress failed,err="
                            << (err.message ? err.message : "?");
            }
            dbus_error_free(&err);
            dbus_connection_unref(session);
            if (address.empty())
            {
                SLOG_STMW() << "ConnectBus,no a11y bus address"
                            << "（at-spi-bus-launcher 未运行？请检查会话无障碍设置）";
                return false;
            }
        }

        m_conn = dbus_connection_open(address.c_str(), &err);
        if (!m_conn)
        {
            SLOG_STMW() << "ConnectBus,open failed,err=" << (err.message ? err.message : "?");
            dbus_error_free(&err);
            return false;
        }
        if (!dbus_bus_register(m_conn, &err))
        {
            SLOG_STMW() << "ConnectBus,bus_register failed,err="
                        << (err.message ? err.message : "?");
            dbus_error_free(&err);
            dbus_connection_unref(m_conn);
            m_conn = NULL;
            return false;
        }
        const char *unique = dbus_bus_get_unique_name(m_conn);
        m_uniqueName = unique ? unique : "";
        return true;
    }

    void AtSpi::DoEmbed()
    {
        if (!m_conn || m_bEmbedDone)
            return;
        /* registry 可能比本进程晚启动（at-spi-bus-launcher 由会话拉起），失败时
         * 允许重试，但要节流：Ember 是同步调用，每 50ms 重试一次会卡住 UI。 */
        uint64_t now = GetTickCount64();
        if (m_tsEmbedTry != 0 && now - m_tsEmbedTry < 3000)
            return;
        m_tsEmbedTry = now;

        DBusMessage *msg = dbus_message_new_method_call(ATSPI_NAME_REGISTRY, ATSPI_PATH_ROOT,
                                                       ATSPI_IFACE_SOCKET, "Embed");
        if (!msg)
            return;
        DBusMessageIter it;
        dbus_message_iter_init_append(msg, &it);
        IterAppendObjectRef(&it, m_uniqueName.c_str(), ATSPI_PATH_ROOT);

        DBusError err;
        dbus_error_init(&err);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(m_conn, msg, 1000, &err);
        dbus_message_unref(msg);
        if (!reply)
        {
            SLOG_STMW() << "DoEmbed,Socket.Embed failed,err=" << (err.message ? err.message : "?")
                        << "（registry 未就绪？3 秒后重试）";
            dbus_error_free(&err);
            return; // 保持 m_bEmbedDone 为 false，稍后重试
        }
        m_bEmbedDone = true;
        DBusMessageIter rit, rsub;
        dbus_message_iter_init(reply, &rit);
        if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_STRUCT)
        {
            dbus_message_iter_recurse(&rit, &rsub);
            const char *name = NULL;
            const char *path = NULL;
            if (dbus_message_iter_get_arg_type(&rsub) == DBUS_TYPE_STRING)
            {
                dbus_message_iter_get_basic(&rsub, &name);
                dbus_message_iter_next(&rsub);
                if (dbus_message_iter_get_arg_type(&rsub) == DBUS_TYPE_OBJECT_PATH)
                    dbus_message_iter_get_basic(&rsub, &path);
            }
            m_desktopName = name ? name : "";
            m_desktopPath = path ? path : "";
        }
        dbus_message_unref(reply);
    }

    void AtSpi::Pump()
    {
        /* 重入防御：Pump 由 peekMsg 的 WM_TIMER 派发调用。若处理器内部
         * （dbus dispatch 回调栈中）有任何路径再进 peekMsg（跨线程
         * SendMessage 等待、waitMutliObjectAndMsg 等）并触发同一 WM_TIMER，
         * 会对同一 DBusConnection 嵌套 read_write_dispatch——libdbus 连接
         * 锁自锁，主线程永久挂死。同线程重入直接跳过。 */
        static thread_local bool s_inPump = false;
        if (s_inPump)
            return;
        struct InPumpFlag
        {
            bool &flag;
            ~InPumpFlag() { flag = false; }
        } inPumpGuard{s_inPump};
        s_inPump = true;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (!m_bRegistered)
            {
                /* 总线晚就绪的慢速重试路径（重试定时器驱动）：EnsureInit 成功后
                 * 会把 500ms 重试定时器替换为 50ms 正常泵。 */
                EnsureInit();
                if (!m_bRegistered)
                    return;
            }
        }
        /* 派发期间不得持有 m_mutex：D-Bus 处理器会经 MSAA 获取窗口对象锁
         * （GetWindowTextA→fromHwnd 等），若请求处理被窗口锁阻塞，主线程
         * 又持窗口锁反等本进程资源（如 libdbus 连接锁）即死锁。
         * m_bRegistered/m_conn 注册成功后不再变化，快照后即可放锁。 */
        if (!m_conn)
            return;
        DoEmbed();
        DrainSignals();
        /* 吞吐修复：read_write_dispatch 每次调用只派发一条消息，且回复要等
         * 下一次调用才真正写到 socket——同步客户端（accerciser/pyatspi）的
         * 每个请求-应答会跨两个 50ms 定时器 tick，吞吐被压到 ~10-20 请求/秒。
         * 这里持续泵空 socket：每派发一条就 flush 一次回复；socket 上暂时没
         * 有下一请求时等 5ms 再确认——同步客户端收到应答后要过一小会儿才发
         * 下一个请求，timeout=0 会错过它，退化成每 tick 只处理一个请求。
         *
         * 时间预算（关键）：Pump 跑在 UI 线程（peekMsg 的 WM_TIMER 派发）。
         * accerciser 启动时会自动枚举整棵桌面树，请求流持续不断——没有预算
         * 的话 poll(5ms) 每次都能等到下一个请求，泵循环一路跑满上限，UI
         * 线程被连续霸占，fun_test 界面整个冻结（"双进程卡死"的直接机制）。
         * 单轮最多处理 20ms 就交还主线程处理输入/绘制，定时器 50ms 后再来。 */
        uint64_t tsDrainStart = GetTickCount64();
        for (int nDrain = 0; nDrain < 200; nDrain++)
        {
            dbus_connection_read_write_dispatch(m_conn, 0);
            dbus_connection_flush(m_conn);
            if (GetTickCount64() - tsDrainStart > 20)
                break; /* 时间预算用尽，让 UI 线程喘口气 */
            if ((nDrain & 31) == 31)
                DrainSignals(); /* 高负载下信号也不饿死 */
            if (dbus_connection_get_dispatch_status(m_conn) != DBUS_DISPATCH_DATA_REMAINS)
            {
                struct pollfd pfd;
                dbus_connection_get_socket(m_conn, &pfd.fd);
                pfd.events = POLLIN;
                pfd.revents = 0;
                if (poll(&pfd, 1, 5) <= 0)
                    break; // 5ms 内没有后续请求，本轮结束
            }
        }
        DrainSignals();
    }

    void AtSpi::Shutdown()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_hWinEventHook)
        {
            ::UnhookWinEvent(m_hWinEventHook);
            m_hWinEventHook = NULL;
        }
        if (m_timerId)
        {
            ::KillTimer(0, m_timerId);
            m_timerId = 0;
        }
        if (m_conn)
        {
            dbus_connection_unref(m_conn);
            m_conn = NULL;
        }
        m_bRegistered = false;
    }

    void AtSpi::EmitSignalInternal(const char *path, const char *klass, const char *member,
                                   const char *detail, dbus_int32_t detail1,
                                   dbus_int32_t detail2, const char *variantType,
                                   const void *variantVal)
    {
        /* 只入队，不直接发送——见 m_sigQueue 注释（避免主线程持窗口锁时
         * 与泵线程形成 AB-BA 死锁）。 */
        QueuedSig q;
        q.path = path ? path : "";
        q.klass = klass ? klass : "";
        q.member = member ? member : "";
        q.detail = detail ? detail : "";
        q.detail1 = detail1;
        q.detail2 = detail2;
        q.variantType = variantType ? variantType : "";
        if (variantType && variantVal)
        {
            if (strcmp(variantType, "(so)") == 0)
                q.node = *(const AccNode *)variantVal;
            else if (variantType[0] == 's')
                q.sVal = *(const char *const *)variantVal;
            else
                q.iVal = *(const dbus_int32_t *)variantVal;
        }
        std::lock_guard<std::mutex> lock(m_sigMtx);
        m_sigQueue.push_back(q);
    }

    void AtSpi::DrainSignals()
    {
        if (!m_conn)
            return;
        swinx_stl::deque<QueuedSig> queue;
        {
            std::lock_guard<std::mutex> lock(m_sigMtx);
            m_sigQueue.swap(queue);
        }
        bool bSent = false;
        while (!queue.empty())
        {
            const QueuedSig &q = queue.front();
            DBusMessage *sig =
                dbus_message_new_signal(q.path.c_str(), q.klass.c_str(), q.member.c_str());
            if (sig)
            {
                DBusMessageIter it;
                dbus_message_iter_init_append(sig, &it);
                const char *d = q.detail.c_str();
                dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &d);
                dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &q.detail1);
                dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &q.detail2);
                if (!q.variantType.empty())
                {
                    DBusMessageIter sub;
                    if (q.variantType == "(so)")
                    {
                        /* ChildrenChanged 规范要求 any 载荷为被增删子对象的 (so)
                         * 引用，否则 libatspi 会丢弃事件（缓存不更新），或按残缺
                         * 数据建树。 */
                        swinx_stl::string objPath = NodePath(q.node);
                        DBusMessageIter subsub;
                        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "(so)", &sub);
                        dbus_message_iter_open_container(&sub, DBUS_TYPE_STRUCT, NULL, &subsub);
                        IterAppendString(&subsub, m_uniqueName.c_str());
                        const char *p = objPath.c_str();
                        dbus_message_iter_append_basic(&subsub, DBUS_TYPE_OBJECT_PATH, &p);
                        dbus_message_iter_close_container(&sub, &subsub);
                        dbus_message_iter_close_container(&it, &sub);
                    }
                    else
                    {
                        /* 单字符类型（'i'/'s'/'u'...）：variantType[0] 即 D-Bus 类型码。 */
                        dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT,
                                                         q.variantType.c_str(), &sub);
                        if (q.variantType[0] == 's')
                        {
                            const char *s = q.sVal.c_str();
                            dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &s);
                        }
                        else
                        {
                            dbus_message_iter_append_basic(&sub, (int)q.variantType[0], &q.iVal);
                        }
                        dbus_message_iter_close_container(&it, &sub);
                    }
                }
                else
                {
                    DBusMessageIter sub;
                    dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &sub);
                    dbus_uint32_t zero = 0;
                    dbus_message_iter_append_basic(&sub, DBUS_TYPE_UINT32, &zero);
                    dbus_message_iter_close_container(&it, &sub);
                }

                DBusMessageIter dict;
                dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
                dbus_message_iter_close_container(&it, &dict);

                dbus_connection_send(m_conn, sig, NULL);
                dbus_message_unref(sig);
                bSent = true;
            }
            queue.pop_front();
        }
        if (bSent)
            dbus_connection_flush(m_conn);
    }

    void AtSpi::OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild)
    {
        if (!hwnd)
            return;
        (void)idChild;
        (void)idObject;
        /* SetWinEventHook 是全局钩子，会收到桌面上所有进程窗口的事件。
         * 只桥接本进程窗口，避免把其他应用的事件以本应用的对象路径
         * 广播出去污染 a11y 总线。 */
        DWORD pid = 0;
        ::GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ::GetCurrentProcessId())
            return;
        switch (event)
        {
        case EVENT_OBJECT_CREATE:
        case EVENT_OBJECT_SHOW:
            /* 新窗口进入顶层候选表（去重）。CREATE/SHOW 事件以宿主 HWND
             * 携带，非顶层窗口会在读取校验时被 GetParent 过滤。 */
            TrackTopLevelCandidate(hwnd);
            if (event == EVENT_OBJECT_CREATE)
            {
                EmitChildrenChanged(hwnd, true);
                break;
            }
            EmitStateChanged(hwnd, "visible", 1);
            EmitStateChanged(hwnd, "showing", 1);
            break;
        case EVENT_OBJECT_DESTROY:
            EmitChildrenChanged(hwnd, false);
            break;
        case EVENT_OBJECT_HIDE:
            EmitStateChanged(hwnd, "visible", 0);
            EmitStateChanged(hwnd, "showing", 0);
            break;
        case EVENT_OBJECT_FOCUS:
            EmitStateChanged(hwnd, "focused", 1);
            EmitFocusEvent(hwnd);
            break;
        case EVENT_OBJECT_NAMECHANGE:
            EmitPropertyChange(hwnd, "accessible-name");
            break;
        case EVENT_OBJECT_VALUECHANGE:
            EmitPropertyChange(hwnd, "accessible-value");
            break;
        case EVENT_OBJECT_DESCRIPTIONCHANGE:
            EmitPropertyChange(hwnd, "accessible-description");
            break;
        case EVENT_OBJECT_SELECTION:
            EmitStateChanged(hwnd, "selected", 1);
            break;
        case EVENT_OBJECT_STATECHANGE:
            EmitStateChanged(hwnd, "enabled", 1);
            break;
        case EVENT_SYSTEM_FOREGROUND:
            EmitWindowEvent(hwnd, "Activate");
            break;
        case EVENT_SYSTEM_DIALOGSTART:
            EmitWindowEvent(hwnd, "Create");
            break;
        case EVENT_SYSTEM_DIALOGEND:
            EmitWindowEvent(hwnd, "Destroy");
            break;
        default:
            /* 未映射事件不再发通用 PropertyChange：之前对每个未知事件都广播
             * 一条空载荷信号（含鼠标移动触发的 LOCATIONCHANGE 风暴），纯属
             * 噪声。位置/名称变化由客户端按需拉取即可。 */
            break;
        }
    }

} // namespace

/* ================================================================== */
/* 事件发射 + 钩子 / 定时器入口                                          */
/* ================================================================== */

namespace
{

    void AtSpi::EmitSignalFor(AccNode &node, const char *klass, const char *member,
                              const char *detail, dbus_int32_t detail1, dbus_int32_t detail2,
                              const char *variantType, const void *variantVal)
    {
        swinx_stl::string path = NodePath(node);
        EmitSignalInternal(path.c_str(), klass, member, detail, detail1, detail2, variantType,
                           variantVal);
    }

    void AtSpi::EmitStateChanged(HWND hwnd, const char *state, int value)
    {
        AccNode node;
        node.hwnd = hwnd;
        dbus_int32_t v = value ? 1 : 0;
        EmitSignalFor(node, "org.a11y.atspi.Event.Object", "StateChanged", state, v, 0, "i", &v);
    }

    void AtSpi::EmitPropertyChange(HWND hwnd, const char *prop)
    {
        AccNode node;
        node.hwnd = hwnd;
        const char *empty = "";
        /* 注意：dbus_message_iter_append_basic 对 DBUS_TYPE_STRING 要求
         * const char **（指向指针的指针）。此前误传字符串指针本身，libdbus
         * 把字面量前 8 字节解引用成 char* 再 strlen，直接段错误。 */
        EmitSignalFor(node, "org.a11y.atspi.Event.Object", "PropertyChange", prop, 0, 0, "s",
                      &empty);
    }

    void AtSpi::EmitChildrenChanged(HWND hwnd, bool bAdd)
    {
        /* 只有顶层窗口的增删才广播 ChildrenChanged：AT-SPI 规范要求该信号
         * 携带被增删子对象的 (so) 引用和其在父中的索引。SOUI 内部控件的
         * 增删不必逐个广播——客户端展开时会通过 GetChildren 实时枚举。
         * 此前对每个 EVENT_OBJECT_CREATE 都以客户区根路径 + 残缺载荷广播
         * 一条 add（同一窗口连发 70+ 条），是客户端缓存混乱的来源之一。 */
        AccNode win;
        win.hwnd = hwnd;
        win.bWindow = true;
        long idx = NodeIndexInParent(win);
        if (idx < 0)
            return; // 不是（或不再是）可见顶层窗口
        /* 防抖：swinx 一次启动流程会对同一窗口连发几十条 EVENT_OBJECT_CREATE
         * （leak.log 实测 70+），对根路径的 add 广播按 hwnd 去重，200ms 内
         * 重复事件只发第一条。 */
        {
            static swinx_stl::map<HWND, uint64_t> s_lastEmit;
            uint64_t now = GetTickCount64();
            swinx_stl::map<HWND, uint64_t>::iterator it = s_lastEmit.find(hwnd);
            if (it != s_lastEmit.end() && now - it->second < 200)
                return;
            s_lastEmit[hwnd] = now;
        }
        EmitSignalInternal(ATSPI_PATH_ROOT, "org.a11y.atspi.Event.Object", "ChildrenChanged",
                           bAdd ? "add" : "remove", (dbus_int32_t)idx, 0, "(so)", &win);
    }

    void AtSpi::EmitFocusEvent(HWND hwnd)
    {
        AccNode node;
        node.hwnd = hwnd;
        const char *empty = "";
        /* "s" 载荷必须传 const char **（见 EmitPropertyChange 的注释）——
         * 此前的错误间接引用正是退出时段错误的元凶（崩溃栈
         * EmitFocusEvent -> EmitSignalInternal -> append_basic -> strlen）。 */
        EmitSignalFor(node, "org.a11y.atspi.Event.Focus", "Focus", "", 0, 0, "s", &empty);
    }

    void AtSpi::EmitWindowEvent(HWND hwnd, const char *name)
    {
        AccNode node;
        node.hwnd = hwnd;
        node.bWindow = true;
        const char *empty = "";
        EmitSignalFor(node, "org.a11y.atspi.Event.Window", name, "", 0, 0, "s", &empty);
    }

    void CALLBACK AtSpiWinEventHook(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObject,
                                    LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
    {
        (void)hHook;
        (void)idEventThread;
        (void)dwmsEventTime;
        AtSpi::Inst().OnWinEvent(event, hwnd, idObject, idChild);
    }

    void CALLBACK AtSpiTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
    {
        (void)hwnd;
        (void)uMsg;
        (void)idEvent;
        (void)dwTime;
        AtSpi::Inst().Pump();
    }

    /* ------------------------------------------------------------------ */
    /* 属性值                                                              */
    /* ------------------------------------------------------------------ */

    struct PropValue
    {
        enum Type
        {
            T_STRING,
            T_INT,
            T_UINT,
            T_OBJREF
        } type = T_STRING;
        swinx_stl::string str;
        int i = 0;
        unsigned int u = 0;
        swinx_stl::string refName;
        swinx_stl::string refPath;
    };

    swinx_stl::vector<swinx_stl::string> NodeInterfaces(const AccNode &node)
    {
        swinx_stl::vector<swinx_stl::string> v;
        v.push_back(ATSPI_IFACE_ACCESSIBLE);
        if (node.bRoot)
        {
            v.push_back(ATSPI_IFACE_APPLICATION);
            v.push_back(ATSPI_IFACE_CACHE);
        }
        else
        {
            v.push_back(ATSPI_IFACE_COMPONENT);
            v.push_back(ATSPI_IFACE_ACTION);
        }
        return v;
    }

    bool GetNodeProperty(const AccNode &node, const char *iface, const char *prop, PropValue &out)
    {
        const char *busName = AtSpi::Inst().UniqueName();

        if (strcmp(iface, ATSPI_IFACE_ACCESSIBLE) == 0)
        {
            if (strcmp(prop, "Name") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = NodeName(node);
                return true;
            }
            if (strcmp(prop, "Description") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = NodeDescription(node);
                return true;
            }
            if (strcmp(prop, "HelpText") == 0 || strcmp(prop, "AccessibleId") == 0 ||
                strcmp(prop, "Locale") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = "";
                return true;
            }
            if (strcmp(prop, "ChildCount") == 0)
            {
                out.type = PropValue::T_INT;
                out.i = (int)NodeChildCount(node);
                return true;
            }
            if (strcmp(prop, "Parent") == 0)
            {
                out.type = PropValue::T_OBJREF;
                if (node.bRoot)
                {
                    out.refName = AtSpi::Inst().DesktopName();
                    out.refPath = AtSpi::Inst().DesktopPath();
                }
                else if (node.bWindow)
                {
                    out.refName = busName;
                    out.refPath = ATSPI_PATH_ROOT;
                }
                else if (node.chain.empty())
                {
                    AccNode parent;
                    parent.hwnd = node.hwnd;
                    parent.bWindow = true;
                    out.refName = busName;
                    out.refPath = NodePath(parent);
                }
                else
                {
                    AccNode parent = node;
                    parent.chain.pop_back();
                    out.refName = busName;
                    out.refPath = NodePath(parent);
                }
                return true;
            }
            if (strcmp(prop, "version") == 0)
            {
                out.type = PropValue::T_UINT;
                out.u = 1;
                return true;
            }
            return false;
        }
        if (strcmp(iface, ATSPI_IFACE_APPLICATION) == 0)
        {
            if (strcmp(prop, "ToolkitName") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = "swinx";
                return true;
            }
            if (strcmp(prop, "Version") == 0 || strcmp(prop, "ToolkitVersion") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = "1.0";
                return true;
            }
            if (strcmp(prop, "AtspiVersion") == 0)
            {
                out.type = PropValue::T_STRING;
                out.str = "2.1";
                return true;
            }
            if (strcmp(prop, "InterfaceVersion") == 0)
            {
                out.type = PropValue::T_UINT;
                out.u = 1;
                return true;
            }
            if (strcmp(prop, "Id") == 0)
            {
                out.type = PropValue::T_INT;
                out.i = 1;
                return true;
            }
            return false;
        }
        if (strcmp(iface, ATSPI_IFACE_COMPONENT) == 0 || strcmp(iface, ATSPI_IFACE_ACTION) == 0 ||
            strcmp(iface, ATSPI_IFACE_CACHE) == 0)
        {
            if (strcmp(prop, "version") == 0)
            {
                out.type = PropValue::T_UINT;
                out.u = 1;
                return true;
            }
            return false;
        }
        return false;
    }

    void IterAppendPropValue(DBusMessageIter *it, const PropValue &v)
    {
        switch (v.type)
        {
        case PropValue::T_STRING:
            IterAppendVariantString(it, v.str);
            break;
        case PropValue::T_INT:
            IterAppendVariantInt(it, v.i);
            break;
        case PropValue::T_UINT:
        {
            DBusMessageIter sub;
            dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "u", &sub);
            dbus_uint32_t val = (dbus_uint32_t)v.u;
            dbus_message_iter_append_basic(&sub, DBUS_TYPE_UINT32, &val);
            dbus_message_iter_close_container(it, &sub);
            break;
        }
        case PropValue::T_OBJREF:
        {
            DBusMessageIter sub;
            dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, "(so)", &sub);
            IterAppendObjectRef(&sub, v.refName.c_str(), v.refPath.c_str());
            dbus_message_iter_close_container(it, &sub);
            break;
        }
        }
    }

    /* ------------------------------------------------------------------ */
    /* 几何                                                                */
    /* ------------------------------------------------------------------ */

    /* 直接向 X 服务器查询窗口原点（root 坐标）。
     * swinx 的 WndMgr 里缓存的 wndObj->rc 依赖 WM_MOVE 消息同步；若该同步
     * 链路失效（消息被吞/未派发），rc.left/top 会停留在创建时的值（常见为
     * 0），ClientToScreen/GetWindowRect 全部失真——accLocation 返回的客户
     * 区坐标与 GetWindowRect 的 (0,0) 原点"自洽"，AT 端看到所有元素挤在屏
     * 幕左上角。X 服务器的 translate_coordinates 是唯一可信的位置来源。 */
    bool QueryXWindowOrigin(HWND hwnd, long &ox, long &oy)
    {
        static swinx_stl::map<HWND, swinx_stl::pair<long, long>> s_cache;
        static swinx_stl::map<HWND, uint64_t> s_cacheTs;
        uint64_t now = GetTickCount64();
        swinx_stl::map<HWND, uint64_t>::iterator itTs = s_cacheTs.find(hwnd);
        if (itTs != s_cacheTs.end() && now - itTs->second < 200)
        {
            swinx_stl::map<HWND, swinx_stl::pair<long, long>>::iterator it = s_cache.find(hwnd);
            if (it != s_cache.end())
            {
                ox = it->second.first;
                oy = it->second.second;
                return true;
            }
        }
        SConnection *conn = SConnMgr::instance()->getConnection();
        if (!conn || !conn->connection || !conn->screen)
            return false;
        xcb_translate_coordinates_cookie_t ck = xcb_translate_coordinates(
            conn->connection, (xcb_window_t)(UINT_PTR)hwnd, conn->screen->root, 0, 0);
        xcb_translate_coordinates_reply_t *rp =
            xcb_translate_coordinates_reply(conn->connection, ck, NULL);
        if (!rp)
            return false;
        ox = rp->dst_x;
        oy = rp->dst_y;
        free(rp);
        s_cache[hwnd] = swinx_stl::make_pair(ox, oy);
        s_cacheTs[hwnd] = now;
        return true;
    }

    bool NodeScreenRect(const AccNode &node, RECT &rc)
    {
        memset(&rc, 0, sizeof(rc));
        if (node.bRoot)
            return false;
        if (node.bWindow)
        {
            if (!::GetWindowRect(node.hwnd, &rc))
                return false;
            /* 以 X 服务器为准校正窗口原点（详见 QueryXWindowOrigin 注释）。 */
            long ox = 0, oy = 0;
            if (QueryXWindowOrigin(node.hwnd, ox, oy) && (rc.left != ox || rc.top != oy))
            {
                static uint64_t s_tsWndLog = 0;
                uint64_t tsNow = GetTickCount64();
                if (tsNow - s_tsWndLog > 5000)
                {
                    s_tsWndLog = tsNow;
                    SLOG_STMW() << "NodeScreenRect,window origin out of sync,hwnd="
                                << (long long)(UINT_PTR)node.hwnd
                                << ",wndMgrOrigin=" << rc.left << "," << rc.top
                                << ",xOrigin=" << ox << "," << oy;
                }
                OffsetRect(&rc, ox - rc.left, oy - rc.top);
            }
            return true;
        }
        Resolved r;
        if (!ResolveNode(node, r) || !r.ok)
            return false;
        VARIANT varChild;
        VariantInit(&varChild);
        varChild.vt = VT_I4;
        varChild.lVal = r.childId;
        long x = 0, y = 0, cx = 0, cy = 0;
        HRESULT hr = r.acc->accLocation(&x, &y, &cx, &cy, varChild);
        VariantClear(&varChild);
        if (hr != S_OK)
            return false;
        rc.left = x;
        rc.top = y;
        rc.right = x + cx;
        rc.bottom = y + cy;

        /* SOUI 的 accLocation 依赖 ClientToScreen(GetHostHwnd()) 折算屏幕坐
         * 标；ClientToScreen 又依赖 WndMgr 缓存的窗口矩形。若该矩形位置失
         * 同步（见 QueryXWindowOrigin），accLocation 会返回客户区坐标。这里
         * 以 X 服务器的真实窗口矩形为参照自校验：结果不在真实窗口矩形内、
         * 却落在"客户区尺寸@原点"内时，按窗口真实原点校正。 */
        RECT rcWnd;
        if (cx > 0 && cy > 0 && ::GetWindowRect(node.hwnd, &rcWnd) && rcWnd.right > rcWnd.left &&
            rcWnd.bottom > rcWnd.top)
        {
            long ox = 0, oy = 0;
            if (QueryXWindowOrigin(node.hwnd, ox, oy) && (rcWnd.left != ox || rcWnd.top != oy))
            {
                OffsetRect(&rcWnd, ox - rcWnd.left, oy - rcWnd.top);
            }
            bool bInsideWin = rc.left >= rcWnd.left && rc.top >= rcWnd.top &&
                              rc.right <= rcWnd.right && rc.bottom <= rcWnd.bottom;
            if (!bInsideWin)
            {
                long wndW = rcWnd.right - rcWnd.left;
                long wndH = rcWnd.bottom - rcWnd.top;
                if (rc.left >= 0 && rc.top >= 0 && rc.right <= wndW && rc.bottom <= wndH)
                {
                    OffsetRect(&rc, rcWnd.left, rcWnd.top);
                    /* 校正被触发说明 ClientToScreen 折算未生效（swinx 核心
                     * 的窗口矩形同步存在失真），节流记录以便定位。 */
                    static uint64_t s_tsCorrLog = 0;
                    uint64_t tsNow = GetTickCount64();
                    if (tsNow - s_tsCorrLog > 5000)
                    {
                        s_tsCorrLog = tsNow;
                        SLOG_STMW() << "NodeScreenRect,accLocation missing window origin,"
                                    << "corrected,hwnd=" << (long long)(UINT_PTR)node.hwnd
                                    << ",wndOrigin=" << rcWnd.left << "," << rcWnd.top;
                    }
                }
            }
        }
        return true;
    }

    RECT NodeExtents(const AccNode &node, dbus_uint32_t coordType)
    {
        RECT rc;
        if (!NodeScreenRect(node, rc))
            return rc;
        if (coordType == 1)
        { // 相对顶层窗口
            RECT rcWnd;
            if (node.bWindow)
                return rc;
            if (::GetWindowRect(node.hwnd, &rcWnd))
            {
                rc.left -= rcWnd.left;
                rc.right -= rcWnd.left;
                rc.top -= rcWnd.top;
                rc.bottom -= rcWnd.top;
            }
        }
        else if (coordType == 2)
        { // 相对直接父对象
            AccNode parent = node;
            if (parent.bWindow)
            {
                return rc;
            }
            if (parent.chain.empty())
            {
                parent.bWindow = true;
            }
            else
            {
                parent.chain.pop_back();
            }
            RECT rcParent;
            if (NodeScreenRect(parent, rcParent))
            {
                rc.left -= rcParent.left;
                rc.right -= rcParent.left;
                rc.top -= rcParent.top;
                rc.bottom -= rcParent.top;
            }
        }
        return rc;
    }

    /* ------------------------------------------------------------------ */
    /* 子节点                                                              */
    /* ------------------------------------------------------------------ */

    AccNode NodeChildAt(const AccNode &node, long index)
    {
        AccNode child;
        child.hwnd = node.hwnd;
        if (node.bRoot)
        {
            swinx_stl::vector<HWND> wins = TopLevelWindows();
            if (index < 0 || index >= (long)wins.size())
            {
                child.bRoot = true;
                return child;
            }
            child.bWindow = true;
            child.hwnd = wins[index];
            return child;
        }
        if (node.bWindow)
        {
            if (index == 0)
                return child; // 客户区根节点（chain 为空）
            child.bRoot = true; // 越界标记
            return child;
        }
        child.chain = node.chain;
        child.chain.push_back(index + 1); // AT-SPI 0-based -> MSAA 1-based
        return child;
    }

    long NodeIndexInParent(const AccNode &node)
    {
        if (node.bRoot)
            return -1;
        if (node.bWindow)
        {
            swinx_stl::vector<HWND> wins = TopLevelWindows();
            for (size_t i = 0; i < wins.size(); i++)
                if (wins[i] == node.hwnd)
                    return (long)i;
            return -1;
        }
        if (node.chain.empty())
            return 0; // 客户区在窗口节点下
        return node.chain.back() - 1;
    }

    void CollectTree(const AccNode &node, swinx_stl::vector<AccNode> &out, int depth, size_t maxNodes)
    {
        if (depth > 20 || out.size() >= maxNodes)
            return;
        if (!node.bRoot)
            out.push_back(node);
        long count = NodeChildCount(node);
        for (long i = 0; i < count && out.size() < maxNodes; i++)
        {
            AccNode child = NodeChildAt(node, i);
            if (child.bRoot) // 越界标记
                break;
            CollectTree(child, out, depth + 1, maxNodes);
        }
    }

    void AppendCacheItem(DBusMessageIter *it, const AccNode &node)
    {
        const char *busName = AtSpi::Inst().UniqueName();

        /* Cache.GetItems 一次会拉出整棵树。如果每个字段都各自走一遍
         * NodeName/NodeRole/NodeStates，每个节点就要重复导航 O(字段数 × 深度)
         * 次；这里在单个节点上只解析一次 MSAA 对象，把需要的字段一次取齐。 */
        swinx_stl::string name, description;
        unsigned int role = ATSPI_ROLE_UNKNOWN;
        swinx_stl::vector<unsigned int> states;
        long childCount = 0;

        if (node.bRoot)
        {
            name = "swinx application";
            role = ATSPI_ROLE_APPLICATION;
            states = NodeStates(node);
            childCount = NodeChildCount(node);
        }
        else if (node.bWindow)
        {
            name = NodeName(node);
            role = ATSPI_ROLE_FRAME;
            states = NodeStates(node);
            childCount = 1;
        }
        else
        {
            Resolved r;
            if (ResolveNode(node, r) && r.ok)
            {
                name = GetAccString(r.acc, r.childId, ACC_STR_NAME);
                description = GetAccString(r.acc, r.childId, ACC_STR_DESCRIPTION);

                VARIANT varChild, varOut;
                VariantInit(&varChild);
                varChild.vt = VT_I4;
                varChild.lVal = r.childId;

                VariantInit(&varOut);
                if (r.acc->get_accRole(varChild, &varOut) == S_OK && varOut.vt == VT_I4)
                    role = RoleFromMsaA(varOut.lVal);
                VariantClear(&varOut);

                VariantInit(&varOut);
                if (r.acc->get_accState(varChild, &varOut) == S_OK && varOut.vt == VT_I4)
                    AppendMsaAStates(states, (DWORD)varOut.lVal);
                VariantClear(&varOut);

                if (r.childId == CHILDID_SELF)
                {
                    long c = 0;
                    if (r.acc->get_accChildCount(&c) == S_OK && c > 0)
                        childCount = c;
                }
                VariantClear(&varChild);
            }
            if (states.empty())
                states.push_back(ATSPI_STATE_VISIBLE);
        }

        DBusMessageIter item;
        dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &item);

        /* 1. 对象引用 */
        IterAppendObjectRef(&item, busName, NodePath(node).c_str());
        /* 2. 所属应用 */
        IterAppendObjectRef(&item, busName, ATSPI_PATH_ROOT);
        /* 3. 父对象 */
        PropValue parent;
        GetNodeProperty(node, ATSPI_IFACE_ACCESSIBLE, "Parent", parent);
        IterAppendObjectRef(&item, parent.refName.c_str(), parent.refPath.c_str());
        /* 4. index in parent */
        dbus_int32_t idx = (dbus_int32_t)NodeIndexInParent(node);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &idx);
        /* 5. child count */
        dbus_int32_t cc = (dbus_int32_t)childCount;
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &cc);
        /* 6. interfaces */
        IterAppendStringArray(&item, NodeInterfaces(node));
        /* 7. name */
        IterAppendString(&item, name.c_str());
        /* 8. role */
        dbus_uint32_t r8 = (dbus_uint32_t)role;
        dbus_message_iter_append_basic(&item, DBUS_TYPE_UINT32, &r8);
        /* 9. description */
        IterAppendString(&item, description.c_str());
        /* 10. states */
        IterAppendUint32Array(&item, states);

        dbus_message_iter_close_container(it, &item);
    }

    /* ------------------------------------------------------------------ */
    /* 方法分派                                                            */
    /* ------------------------------------------------------------------ */

    void ReplyError(DBusConnection *conn, DBusMessage *msg, const char *name, const char *text)
    {
        DBusMessage *reply = dbus_message_new_error(msg, name, text);
        if (reply)
        {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
    }

    void SendReply(DBusConnection *conn, DBusMessage *reply)
    {
        if (reply)
        {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
    }

    void HandleAccessible(DBusConnection *conn, DBusMessage *msg, const AccNode &node,
                          const char *member)
    {
        const char *busName = AtSpi::Inst().UniqueName();

        if (strcmp(member, "GetRole") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_uint32_t role = (dbus_uint32_t)NodeRole(node);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &role);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetRoleName") == 0 || strcmp(member, "GetLocalizedRoleName") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendString(&it, RoleNameFromAtSpi(NodeRole(node)));
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetState") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendUint32Array(&it, NodeStates(node));
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetChildAtIndex") == 0)
        {
            dbus_int32_t index = 0;
            if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID))
            {
                ReplyError(conn, msg, DBUS_ERROR_INVALID_ARGS, "expected int32");
                return;
            }
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            if (index < 0 || index >= (dbus_int32_t)NodeChildCount(node))
            {
                IterAppendObjectRef(&it, "", ATSPI_PATH_NULL);
            }
            else
            {
                AccNode child = NodeChildAt(node, index);
                IterAppendObjectRef(&it, busName, NodePath(child).c_str());
            }
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetChildren") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(so)", &arr);
            long count = NodeChildCount(node);
            for (long i = 0; i < count; i++)
            {
                AccNode child = NodeChildAt(node, i);
                if (child.bRoot)
                    break;
                IterAppendObjectRef(&arr, busName, NodePath(child).c_str());
            }
            dbus_message_iter_close_container(&it, &arr);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetIndexInParent") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_int32_t idx = (dbus_int32_t)NodeIndexInParent(node);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &idx);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetRelationSet") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ua(so))", &arr);
            dbus_message_iter_close_container(&it, &arr);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetAttributes") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{ss}", &arr);
            dbus_message_iter_close_container(&it, &arr);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetApplication") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendObjectRef(&it, busName, ATSPI_PATH_ROOT);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetInterfaces") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendStringArray(&it, NodeInterfaces(node));
            SendReply(conn, reply);
            return;
        }
        ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown Accessible method");
    }

    void HandleComponent(DBusConnection *conn, DBusMessage *msg, const AccNode &node,
                         const char *member)
    {
        const char *busName = AtSpi::Inst().UniqueName();

        if (strcmp(member, "GetExtents") == 0)
        {
            dbus_uint32_t ct = 0;
            if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID))
            {
                ReplyError(conn, msg, DBUS_ERROR_INVALID_ARGS, "expected uint32");
                return;
            }
            RECT rc = NodeExtents(node, ct);
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendRect(&it, (int)rc.left, (int)rc.top, (int)(rc.right - rc.left),
                           (int)(rc.bottom - rc.top));
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetPosition") == 0)
        {
            dbus_uint32_t ct = 0;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID);
            RECT rc = NodeExtents(node, ct);
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_int32_t x = (dbus_int32_t)rc.left, y = (dbus_int32_t)rc.top;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &x);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &y);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetSize") == 0)
        {
            RECT rc = NodeExtents(node, 0);
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_int32_t w = (dbus_int32_t)(rc.right - rc.left);
            dbus_int32_t h = (dbus_int32_t)(rc.bottom - rc.top);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &w);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &h);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "Contains") == 0)
        {
            dbus_int32_t x = 0, y = 0;
            dbus_uint32_t ct = 0;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                                  DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID);
            RECT rc = NodeExtents(node, ct);
            dbus_bool_t inside = (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom);
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &inside);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetAccessibleAtPoint") == 0)
        {
            dbus_int32_t x = 0, y = 0;
            dbus_uint32_t ct = 0;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y,
                                  DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID);
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            swinx_stl::string foundName, foundPath;
            long count = NodeChildCount(node);
            for (long i = 0; i < count; i++)
            {
                AccNode child = NodeChildAt(node, i);
                if (child.bRoot)
                    break;
                RECT rc = NodeExtents(child, ct);
                if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
                {
                    foundName = busName;
                    foundPath = NodePath(child);
                    break;
                }
            }
            if (foundPath.empty())
                IterAppendObjectRef(&it, "", ATSPI_PATH_NULL);
            else
                IterAppendObjectRef(&it, foundName.c_str(), foundPath.c_str());
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetLayer") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_uint32_t layer = node.bWindow ? 7u : 3u; // WINDOW / WIDGET
            dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &layer);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetMDIZOrder") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_int16_t z = 0;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_INT16, &z);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetAlpha") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            double alpha = 1.0;
            dbus_message_iter_append_basic(&it, DBUS_TYPE_DOUBLE, &alpha);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GrabFocus") == 0)
        {
            dbus_bool_t ok = FALSE;
            if (!node.bRoot && !node.bWindow)
            {
                Resolved r;
                if (ResolveNode(node, r) && r.ok)
                {
                    VARIANT varChild;
                    VariantInit(&varChild);
                    varChild.vt = VT_I4;
                    varChild.lVal = r.childId;
                    ok = (r.acc->accSelect((long)SELFLAG_TAKEFOCUS, varChild) == S_OK);
                    VariantClear(&varChild);
                }
            }
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &ok);
            SendReply(conn, reply);
            return;
        }
        /* SetExtents / SetPosition / SetSize / ScrollTo / ScrollToPoint 不支持 */
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it;
        dbus_message_iter_init_append(reply, &it);
        dbus_bool_t ok = FALSE;
        dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &ok);
        SendReply(conn, reply);
    }

    void HandleAction(DBusConnection *conn, DBusMessage *msg, const AccNode &node,
                      const char *member)
    {
        if (strcmp(member, "GetActions") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ssss)", &arr);
            if (!node.bRoot && !node.bWindow)
            {
                Resolved r;
                if (ResolveNode(node, r) && r.ok)
                {
                    swinx_stl::string action = GetAccString(r.acc, r.childId, ACC_STR_DEFAULTACTION);
                    if (!action.empty())
                    {
                        DBusMessageIter item;
                        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &item);
                        IterAppendString(&item, action.c_str());
                        IterAppendString(&item, action.c_str());
                        IterAppendString(&item, "");
                        IterAppendString(&item, action.c_str());
                        dbus_message_iter_close_container(&arr, &item);
                    }
                }
            }
            dbus_message_iter_close_container(&it, &arr);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "DoAction") == 0)
        {
            dbus_int32_t index = 0;
            dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID);
            dbus_bool_t ok = FALSE;
            if (index == 0 && !node.bRoot && !node.bWindow)
            {
                Resolved r;
                if (ResolveNode(node, r) && r.ok)
                {
                    VARIANT varChild;
                    VariantInit(&varChild);
                    varChild.vt = VT_I4;
                    varChild.lVal = r.childId;
                    ok = (r.acc->accDoDefaultAction(varChild) == S_OK);
                    VariantClear(&varChild);
                }
            }
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_append_basic(&it, DBUS_TYPE_BOOLEAN, &ok);
            SendReply(conn, reply);
            return;
        }
        /* GetName / GetDescription / GetKeyBinding / GetLocalizedName */
        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it;
        dbus_message_iter_init_append(reply, &it);
        swinx_stl::string text;
        if (!node.bRoot && !node.bWindow)
        {
            Resolved r;
            if (ResolveNode(node, r) && r.ok)
                text = GetAccString(r.acc, r.childId, ACC_STR_DEFAULTACTION);
        }
        IterAppendString(&it, text.c_str());
        SendReply(conn, reply);
    }

    void HandleApplication(DBusConnection *conn, DBusMessage *msg, const char *member)
    {
        if (strcmp(member, "GetLocale") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendString(&it, "C");
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetApplicationBusAddress") == 0)
        {
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendString(&it, "");
            SendReply(conn, reply);
            return;
        }
        ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown Application method");
    }

    void HandleCache(DBusConnection *conn, DBusMessage *msg, const char *member)
    {
        if (strcmp(member, "GetItems") != 0)
        {
            ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown Cache method");
            return;
        }
        AccNode root;
        root.bRoot = true;
        swinx_stl::vector<AccNode> nodes;
        CollectTree(root, nodes, 0, 2000);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        DBusMessageIter it, arr;
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "((so)(so)(so)iiassusau)", &arr);
        for (size_t i = 0; i < nodes.size(); i++)
            AppendCacheItem(&arr, nodes[i]);
        dbus_message_iter_close_container(&it, &arr);
        SendReply(conn, reply);
    }

    void HandleProperties(DBusConnection *conn, DBusMessage *msg, const AccNode &node,
                          const char *member)
    {
        DBusError err;
        dbus_error_init(&err);
        char *iface = NULL, *prop = NULL;

        if (strcmp(member, "Get") == 0)
        {
            if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING,
                                       &prop, DBUS_TYPE_INVALID))
            {
                dbus_error_free(&err);
                ReplyError(conn, msg, DBUS_ERROR_INVALID_ARGS, "expected (ss)");
                return;
            }
            PropValue v;
            if (!GetNodeProperty(node, iface, prop, v))
            {
                ReplyError(conn, msg, DBUS_ERROR_INVALID_ARGS, "no such property");
                return;
            }
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it;
            dbus_message_iter_init_append(reply, &it);
            IterAppendPropValue(&it, v);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "GetAll") == 0)
        {
            if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID))
            {
                dbus_error_free(&err);
                ReplyError(conn, msg, DBUS_ERROR_INVALID_ARGS, "expected (s)");
                return;
            }
            const char *props[8];
            size_t nProps = 0;
            if (strcmp(iface, ATSPI_IFACE_ACCESSIBLE) == 0)
            {
                props[nProps++] = "Name";
                props[nProps++] = "Description";
                props[nProps++] = "Parent";
                props[nProps++] = "ChildCount";
                props[nProps++] = "Locale";
                props[nProps++] = "AccessibleId";
                props[nProps++] = "HelpText";
                props[nProps++] = "version";
            }
            else if (strcmp(iface, ATSPI_IFACE_APPLICATION) == 0)
            {
                props[nProps++] = "ToolkitName";
                props[nProps++] = "Version";
                props[nProps++] = "ToolkitVersion";
                props[nProps++] = "AtspiVersion";
                props[nProps++] = "InterfaceVersion";
                props[nProps++] = "Id";
            }
            else
            {
                props[nProps++] = "version";
            }
            DBusMessage *reply = dbus_message_new_method_return(msg);
            DBusMessageIter it, arr;
            dbus_message_iter_init_append(reply, &it);
            dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
            for (size_t i = 0; i < nProps; i++)
            {
                PropValue v;
                if (!GetNodeProperty(node, iface, props[i], v))
                    continue;
                DBusMessageIter entry;
                dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
                IterAppendString(&entry, props[i]);
                IterAppendPropValue(&entry, v);
                dbus_message_iter_close_container(&arr, &entry);
            }
            dbus_message_iter_close_container(&it, &arr);
            SendReply(conn, reply);
            return;
        }
        if (strcmp(member, "Set") == 0)
        {
            /* 只有 Application.Id 可写，且我们不在意具体值。 */
            DBusMessage *reply = dbus_message_new_method_return(msg);
            SendReply(conn, reply);
            return;
        }
        ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown Properties method");
    }

    static const char *kIntrospectXml =
        "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
        "<node>\n"
        "  <interface name='org.a11y.atspi.Accessible'>\n"
        "    <method name='GetRole'><arg type='u' direction='out'/></method>\n"
        "    <method name='GetRoleName'><arg type='s' direction='out'/></method>\n"
        "    <method name='GetLocalizedRoleName'><arg type='s' direction='out'/></method>\n"
        "    <method name='GetState'><arg type='au' direction='out'/></method>\n"
        "    <method name='GetChildAtIndex'><arg type='i' direction='in'/>"
        "<arg type='(so)' direction='out'/></method>\n"
        "    <method name='GetChildren'><arg type='a(so)' direction='out'/></method>\n"
        "    <method name='GetIndexInParent'><arg type='i' direction='out'/></method>\n"
        "    <method name='GetRelationSet'><arg type='a(ua(so))' direction='out'/></method>\n"
        "    <method name='GetAttributes'><arg type='a{ss}' direction='out'/></method>\n"
        "    <method name='GetApplication'><arg type='(so)' direction='out'/></method>\n"
        "    <method name='GetInterfaces'><arg type='as' direction='out'/></method>\n"
        "    <property name='Name' type='s' access='read'/>\n"
        "    <property name='Description' type='s' access='read'/>\n"
        "    <property name='Parent' type='(so)' access='read'/>\n"
        "    <property name='ChildCount' type='i' access='read'/>\n"
        "  </interface>\n"
        "  <interface name='org.a11y.atspi.Component'>\n"
        "    <method name='GetExtents'><arg type='u' direction='in'/>"
        "<arg type='(iiii)' direction='out'/></method>\n"
        "    <method name='GetPosition'><arg type='u' direction='in'/>"
        "<arg type='i' direction='out'/><arg type='i' direction='out'/></method>\n"
        "    <method name='GetSize'><arg type='i' direction='out'/>"
        "<arg type='i' direction='out'/></method>\n"
        "    <method name='GrabFocus'><arg type='b' direction='out'/></method>\n"
        "  </interface>\n"
        "  <interface name='org.a11y.atspi.Action'>\n"
        "    <method name='GetActions'><arg type='a(ssss)' direction='out'/></method>\n"
        "    <method name='DoAction'><arg type='i' direction='in'/>"
        "<arg type='b' direction='out'/></method>\n"
        "  </interface>\n"
        "  <interface name='org.a11y.atspi.Application'>\n"
        "    <property name='ToolkitName' type='s' access='read'/>\n"
        "    <property name='AtspiVersion' type='s' access='read'/>\n"
        "  </interface>\n"
        "  <interface name='org.a11y.atspi.Cache'>\n"
        "    <method name='GetItems'>"
        "<arg type='a((so)(so)(so)iiassusau)' direction='out'/></method>\n"
        "  </interface>\n"
        "</node>\n";

    DBusHandlerResult AtSpiMessageFunction(DBusConnection *conn, DBusMessage *msg, void *userData)
    {
        (void)userData;
        if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *iface = dbus_message_get_interface(msg);
        const char *member = dbus_message_get_member(msg);
        const char *path = dbus_message_get_path(msg);
        if (!iface || !member || !path)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        AccNode node;
        /* Cache 子树：标准路径 /org/a11y/atspi/cache（见注册处注释）。 */
        if (strncmp(path, "/org/a11y/atspi/cache", 21) == 0 && (path[21] == 0 || path[21] == '/'))
        {
            if (strcmp(iface, ATSPI_IFACE_CACHE) == 0)
            {
                HandleCache(conn, msg, member);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown cache method");
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (!ParseNodePath(path, node))
        {
            static DWORD s_lastBad = 0;
            DWORD now = GetTickCount();
            if (s_lastBad == 0 || now - s_lastBad >= 2000)
            {
                s_lastBad = now;
                SLOG_STMW() << "dbus call,unparseable path=" << path;
            }
            /* 现在的 fallback 覆盖了整个 /org/a11y/atspi 子树，解析失败的路
             * 径必须显式回错，否则客户端可能永久等待应答。 */
            ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_OBJECT, "no such object");
            return DBUS_HANDLER_RESULT_HANDLED;
        }

        if (strcmp(iface, ATSPI_IFACE_INTROSPECTABLE) == 0)
        {
            if (strcmp(member, "Introspect") == 0)
            {
                DBusMessage *reply = dbus_message_new_method_return(msg);
                DBusMessageIter it;
                dbus_message_iter_init_append(reply, &it);
                IterAppendString(&it, kIntrospectXml);
                SendReply(conn, reply);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        if (strcmp(iface, ATSPI_IFACE_ACCESSIBLE) == 0)
        {
            HandleAccessible(conn, msg, node, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (strcmp(iface, ATSPI_IFACE_COMPONENT) == 0)
        {
            HandleComponent(conn, msg, node, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (strcmp(iface, ATSPI_IFACE_ACTION) == 0)
        {
            HandleAction(conn, msg, node, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (strcmp(iface, ATSPI_IFACE_APPLICATION) == 0)
        {
            HandleApplication(conn, msg, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (strcmp(iface, ATSPI_IFACE_CACHE) == 0)
        {
            HandleCache(conn, msg, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        if (strcmp(iface, ATSPI_IFACE_PROPERTIES) == 0)
        {
            HandleProperties(conn, msg, node, member);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        /* fallback 覆盖整个 /org/a11y/atspi 子树后，到这里说明是未知接口，
         * 必须显式回错（否则客户端可能永久等待应答）。 */
        ReplyError(conn, msg, DBUS_ERROR_UNKNOWN_METHOD, "unknown interface");
        return DBUS_HANDLER_RESULT_HANDLED;
    }

} // namespace

/* ================================================================== */
/* 对外 C 接口                                                          */
/* ================================================================== */

extern "C"
{

    void SwinxAtSpiInit(void)
    {
        /* 同线程重入防御：EnsureInit 内部的 SetTimer(NULL,...) 会经 wnd.cpp
         * 回到 SConnMgr::instance()，而 instance() 首建后也会兜底调到这里——
         * 两条路径相遇时，AtSpi::Inst() 的静态初始化守卫（__cxa_guard）尚未
         * 释放，重入将对同一 guard 永久阻塞（静态初始化守卫不可重入，
         * leak.log 曾捕获此类死锁栈）。先置标志再进入，重入直接跳过；
         * EnsureInit 自身有 recursive_mutex + m_bRegistered 幂等保护，跳过
         * 不损失语义。thread_local 使跨线程并发调用仍按原有方式在
         * EnsureInit 的锁上排队。 */
        static thread_local bool s_inInit = false;
        if (s_inInit)
            return;
        s_inInit = true;
        AtSpi::Inst().EnsureInit();
        s_inInit = false;
    }

    void SwinxAtSpiShutdown(void)
    {
        AtSpi::Inst().Shutdown();
    }

} // extern "C"
