#ifndef _SATOMH_
#define _SATOMH_
#include <xcb/xcb.h>

class SAtoms {
  public:
    // make sure the order of atoms is same as AtomNames
    xcb_atom_t TEXT;
    xcb_atom_t UTF8_STRING;
    xcb_atom_t CARDINAL;
    xcb_atom_t _NET_WM_PID;
    xcb_atom_t WM_CLASS;
    xcb_atom_t WM_DELETE_WINDOW;
    xcb_atom_t WM_PROTOCOLS;
    xcb_atom_t _NET_WM_STATE;
    xcb_atom_t _NET_WM_STATE_HIDDEN;
    xcb_atom_t _NET_WM_STATE_MAXIMIZED_HORZ;
    xcb_atom_t _NET_WM_STATE_MAXIMIZED_VERT;

    xcb_atom_t _NET_WM_STATE_ABOVE;
    xcb_atom_t _NET_WM_STATE_BELOW;
    xcb_atom_t _NET_WM_STATE_FULLSCREEN;
    xcb_atom_t _NET_WM_STATE_STAYS_ON_TOP;
    xcb_atom_t _NET_WM_STATE_DEMANDS_ATTENTION;

    xcb_atom_t _NET_WM_NAME;
    xcb_atom_t _NET_WM_ICON;
    xcb_atom_t _NET_WM_ICON_NAME;

    xcb_atom_t WM_STATE;
    xcb_atom_t WM_NAME;
    xcb_atom_t WM_CHANGE_STATE;
    xcb_atom_t _XKB_RULES_NAMES;

    xcb_atom_t _NET_WM_WINDOW_TYPE_NORMAL;
    xcb_atom_t _NET_WM_WINDOW_TYPE_DOCK;
    xcb_atom_t _NET_WM_WINDOW_TYPE_TOOLBAR;
    xcb_atom_t _NET_WM_WINDOW_TYPE_SPLASH;
    xcb_atom_t _NET_WM_WINDOW_TYPE;

    xcb_atom_t _MOTIF_WM_HINTS;
    xcb_atom_t _XEMBED_INFO;
    xcb_atom_t _NET_WM_WINDOW_OPACITY;
    xcb_atom_t _NET_WORKAREA;

    xcb_atom_t _NET_DOUBLE_CLICK_TIME;
    xcb_atom_t CLIPBOARD,
        INCR,
        TARGETS,
        MULTIPLE,
        TIMESTAMP,
        SAVE_TARGETS,
        CLIP_TEMPORARY,
        CLIPBOARD_MANAGER;

    //use defined atom
    xcb_atom_t WM_WIN4XCB_IPC;
    xcb_atom_t SO_SELECTION;

    const char **AtomNames(int &atoms)
    {
        static const char *kAtomNames[] = {
            "TEXT",
            "UTF8_STRING",
            "CARDINAL",
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
            "_NET_DOUBLE_CLICK_TIME",

            "CLIPBOARD",
            "INCR",
            "TARGETS",
            "MULTIPLE",
            "TIMESTAMP",
            "SAVE_TARGETS",
            "CLIP_TEMPORARY",
            "CLIPBOARD_MANAGER",

            //use defined atom
            "WM_WIN4XCB_IPC",
            "SO_SELECTION",
        };
        atoms = sizeof(kAtomNames) / sizeof(kAtomNames[0]);
        return kAtomNames;
    }

    void Init(xcb_connection_t *conn);
    static xcb_atom_t internAtom(xcb_connection_t *connection, uint8_t onlyIfExist, const char *atomName);
};

#endif //_SATOMH_