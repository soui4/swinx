#ifndef _SATOMH_
#define _SATOMH_
#include <xcb/xcb.h>

class SAtoms {
  public:
    // make sure the order of atoms is same as AtomNames
    xcb_atom_t 
    TEXT,
    UTF8_STRING,
    CARDINAL,
    CLIPF_UTF8,
    CLIPF_BITMAP, 
    CLIPF_UNICODETEXT, 
    CLIPF_WAVE,

    _NET_WM_PID,
    WM_CLASS,
    WM_DELETE_WINDOW,
    WM_PROTOCOLS,
    _NET_WM_STATE,
    _NET_WM_STATE_HIDDEN,
    _NET_WM_STATE_MAXIMIZED_HORZ,
    _NET_WM_STATE_MAXIMIZED_VERT,

    _NET_WM_STATE_ABOVE,
    _NET_WM_STATE_BELOW,
    _NET_WM_STATE_FULLSCREEN,
    _NET_WM_STATE_STAYS_ON_TOP,
    _NET_WM_STATE_DEMANDS_ATTENTION,
    _NET_WM_STATE_FOCUSED,

    _NET_WM_NAME,
    _NET_WM_ICON,
    _NET_WM_ICON_NAME,

    WM_STATE,
    WM_NAME,
    WM_CHANGE_STATE,
    _XKB_RULES_NAMES,

    _NET_WM_WINDOW_TYPE_NORMAL,
    _NET_WM_WINDOW_TYPE_DOCK,
    _NET_WM_WINDOW_TYPE_TOOLBAR,
    _NET_WM_WINDOW_TYPE_SPLASH,
    _NET_WM_WINDOW_TYPE,

    _MOTIF_WM_HINTS,
    _XEMBED_INFO,
    _NET_WM_WINDOW_OPACITY,
    _NET_WORKAREA,
    _NET_ACTIVE_WINDOW,
    _NET_DOUBLE_CLICK_TIME,
    _XSETTINGS_S0,
    _XSETTINGS_SETTINGS,
    CLIPBOARD,
    INCR,
    TARGETS,
    MULTIPLE,
    TIMESTAMP,
    SAVE_TARGETS,
    CLIP_TEMPORARY,
    CLIPBOARD_MANAGER,

    // Xdnd
    XdndEnter,
    XdndPosition,
    XdndStatus,
    XdndLeave,
    XdndDrop,
    XdndFinished,
    XdndTypeList,
    XdndActionList,

    XdndSelection,

    XdndAware,
    XdndProxy,

    XdndActionCopy,
    XdndActionLink,
    XdndActionMove,
    XdndActionPrivate,

    _NET_SYSTEM_TRAY_OPCODE,
    _NET_SYSTEM_TRAY_S0,
    _NET_SYSTEM_TRAY_VISUAL,

    //use defined atom
    WM_WIN4XCB_IPC,
    WM_DISCONN,

    SO_SELECTION,
    WM_CLASS_ATOM;


    const char **AtomNames(int &atoms)
    {
        static const char *kAtomNames[] = {
            "TEXT",
            "UTF8_STRING",
            "CARDINAL",

            "text/plain;charset=utf-8", // CLIPF_UTF8
            "image/ppm",                // CLIPF_BITMAP
            "CLIPF_UNICODETEXT",
            "CLIPF_WAVE",

            "_NET_WM_PID",
            "WM_CLASS",
            "WM_DELETE_WINDOW",
            "WM_PROTOCOLS",
            "_NET_WM_STATE",
            "_NET_WM_STATE_HIDDEN",
            "_NET_WM_STATE_MAXIMIZED_HORZ",
            "_NET_WM_STATE_MAXIMIZED_VERT",

            "_NET_WM_STATE_ABOVE",
            "_NET_WM_STATE_BELOW",
            "_NET_WM_STATE_FULLSCREEN",
            "_NET_WM_STATE_STAYS_ON_TOP",
            "_NET_WM_STATE_DEMANDS_ATTENTION",
            "_NET_WM_STATE_FOCUSED",
            
            "_NET_WM_NAME",
            "_NET_WM_ICON",
            "_NET_WM_ICON_NAME",

            "WM_STATE",
            "WM_NAME",
            "WM_CHANGE_STATE",
            "_XKB_RULES_NAMES",

            "_NET_WM_WINDOW_TYPE_NORMAL",
            "_NET_WM_WINDOW_TYPE_DOCK",
            "_NET_WM_WINDOW_TYPE_TOOLBAR",
            "_NET_WM_WINDOW_TYPE_SPLASH",
            "_NET_WM_WINDOW_TYPE",

            "_MOTIF_WM_HINTS",
            "_XEMBED_INFO",
            "_NET_WM_WINDOW_OPACITY",
            "_NET_WORKAREA",
            "_NET_ACTIVE_WINDOW",
            "_NET_DOUBLE_CLICK_TIME",
            "_XSETTINGS_S%d",
            "_XSETTINGS_SETTINGS",
            "CLIPBOARD",
            "INCR",
            "TARGETS",
            "MULTIPLE",
            "TIMESTAMP",
            "SAVE_TARGETS",
            "CLIP_TEMPORARY",
            "CLIPBOARD_MANAGER",

            // Xdnd
            "XdndEnter",
            "XdndPosition",
            "XdndStatus",
            "XdndLeave",
            "XdndDrop",
            "XdndFinished",
            "XdndTypeList",
            "XdndActionList",
            "XdndSelection",
            "XdndAware",
            "XdndProxy",

            "XdndActionCopy",
            "XdndActionLink",
            "XdndActionMove",
            "XdndActionPrivate",

            "_NET_SYSTEM_TRAY_OPCODE",
            "_NET_SYSTEM_TRAY_S%d",
            "_NET_SYSTEM_TRAY_VISUAL",
            // use defined atom
            "WM_WIN4XCB_IPC",
            "WM_DISCONN",
            "SO_SELECTION",
            "WM_CLASS_ATOM"
        };
        atoms = sizeof(kAtomNames) / sizeof(kAtomNames[0]);
        return kAtomNames;
    }

    void Init(xcb_connection_t *conn,int nScrNo);
    static int getAtomName(xcb_atom_t atom,char *buf,int bufSize);
    static xcb_atom_t registerAtom(const char *name,xcb_connection_t *xcb_conn=nullptr);
private:
    static xcb_atom_t internAtom(xcb_connection_t *connection, uint8_t onlyIfExist, const char *atomName);
};

#endif //_SATOMH_