#ifndef __OLEACC_H__
#define __OLEACC_H__

/* Microsoft Active Accessibility (MSAA) 接口面
 *
 * 说明：本头文件只声明 swinx 提供的 MSAA 面（IAccessible / IEnumVARIANT 与
 * 常量），语义对齐 Windows SDK 的 oleacc.h。实现见 src/oleacc.cpp：
 *  - Windows 上的 MSAA 依赖 COM 跨进程封送（LresultFromObject 返回的是系统
 *    管理的引用句柄），swinx 是进程内兼容层，因此句柄用内部弱表实现，
 *    AccessibleObjectFromWindow 通过向窗口发送 WM_GETOBJECT 拿到句柄后
 *    再解析，与 SOUI 的 SHostWnd::OnGetObject 形成闭环。
 *  - 事件转发走标准 SetWinEventHook/UnhookWinEvent（声明在 winuser.h）：
 *    平台桥注册后即可收到 NotifyWinEvent 的事件，未注册时为空操作。
 *  - 平台胶水的内部共享助手（对象路径按需解析）声明在 src/SwinxAccGlue.h，
 *    属实现细节，不进公共 API 面。
 */

#include <windows.h>
#include <oaidl.h>
#include <objidl.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef __IAccessible_FWD_DEFINED__
#define __IAccessible_FWD_DEFINED__
    typedef interface IAccessible IAccessible;
#endif /* __IAccessible_FWD_DEFINED__ */

#ifndef __IEnumVARIANT_FWD_DEFINED__
#define __IEnumVARIANT_FWD_DEFINED__
    typedef interface IEnumVARIANT IEnumVARIANT;
#endif /* __IEnumVARIANT_FWD_DEFINED__ */

    /* ------------------------------------------------------------------ *
     *                          常量                                       *
     * ------------------------------------------------------------------ */

#define CHILDID_SELF 0

/* 与 WinSDK 的 0xFFFFFFFCL 字面量不同：那里 LONG 是 32 位（= -4），而
 * LP64 平台（Linux/macOS）上 long 是 64 位，0xFFFFFFFCL 会变成正数
 * 4294967292，导致 "(LONG)lParam == OBJID_CLIENT" 这类服务端判断永远
 * 不成立（真实 Windows 上 lParam 是符号扩展的 -4）。这里显式收窄到
 * LONG，保证所有平台上与 Windows 数值一致。 */
#define OBJID_WINDOW   ((LONG)0x00000000)
#define OBJID_SYSMENU  ((LONG)0xFFFFFFFF)
#define OBJID_TITLEBAR ((LONG)0xFFFFFFFE)
#define OBJID_MENU     ((LONG)0xFFFFFFFD)
#define OBJID_CLIENT   ((LONG)0xFFFFFFFC)
#define OBJID_VSCROLL  ((LONG)0xFFFFFFFB)
#define OBJID_HSCROLL  ((LONG)0xFFFFFFFA)
#define OBJID_SIZEGRIP ((LONG)0xFFFFFFF9)
#define OBJID_CARET    ((LONG)0xFFFFFFF8)
#define OBJID_CURSOR   ((LONG)0xFFFFFFF7)
#define OBJID_ALERT    ((LONG)0xFFFFFFF6)
#define OBJID_SOUND    ((LONG)0xFFFFFFF5)

#define OBJID_NATIVEOM          ((LONG)0xFFFFFFF0)
#define OBJID_QUERYCLASSNAMEIDX ((LONG)0xFFFFFFF4)

    /* Roles */
#define ROLE_SYSTEM_TITLEBAR           0x1
#define ROLE_SYSTEM_MENUBAR            0x2
#define ROLE_SYSTEM_SCROLLBAR          0x3
#define ROLE_SYSTEM_GRIP               0x4
#define ROLE_SYSTEM_SOUND              0x5
#define ROLE_SYSTEM_CURSOR             0x6
#define ROLE_SYSTEM_CARET              0x7
#define ROLE_SYSTEM_ALERT              0x8
#define ROLE_SYSTEM_WINDOW             0x9
#define ROLE_SYSTEM_CLIENT             0xA
#define ROLE_SYSTEM_MENUPOPUP          0xB
#define ROLE_SYSTEM_MENUITEM           0xC
#define ROLE_SYSTEM_TOOLTIP            0xD
#define ROLE_SYSTEM_APPLICATION        0xE
#define ROLE_SYSTEM_DOCUMENT           0xF
#define ROLE_SYSTEM_PANE               0x10
#define ROLE_SYSTEM_CHART              0x11
#define ROLE_SYSTEM_DIALOG             0x12
#define ROLE_SYSTEM_BORDER             0x13
#define ROLE_SYSTEM_GROUPING           0x14
#define ROLE_SYSTEM_SEPARATOR          0x15
#define ROLE_SYSTEM_TOOLBAR            0x16
#define ROLE_SYSTEM_STATUSBAR          0x17
#define ROLE_SYSTEM_TABLE              0x18
#define ROLE_SYSTEM_COLUMNHEADER       0x19
#define ROLE_SYSTEM_ROWHEADER          0x1A
#define ROLE_SYSTEM_COLUMN             0x1B
#define ROLE_SYSTEM_ROW                0x1C
#define ROLE_SYSTEM_CELL               0x1D
#define ROLE_SYSTEM_LINK               0x1E
#define ROLE_SYSTEM_HELPBALLOON        0x1F
#define ROLE_SYSTEM_CHARACTER          0x20
#define ROLE_SYSTEM_LIST               0x21
#define ROLE_SYSTEM_LISTITEM           0x22
#define ROLE_SYSTEM_OUTLINE            0x23
#define ROLE_SYSTEM_OUTLINEITEM        0x24
#define ROLE_SYSTEM_PAGETAB            0x25
#define ROLE_SYSTEM_PROPERTYPAGE       0x26
#define ROLE_SYSTEM_INDICATOR          0x27
#define ROLE_SYSTEM_GRAPHIC            0x28
#define ROLE_SYSTEM_STATICTEXT         0x29
#define ROLE_SYSTEM_TEXT               0x2A
#define ROLE_SYSTEM_PUSHBUTTON         0x2B
#define ROLE_SYSTEM_CHECKBUTTON        0x2C
#define ROLE_SYSTEM_RADIOBUTTON        0x2D
#define ROLE_SYSTEM_COMBOBOX           0x2E
#define ROLE_SYSTEM_DROPLIST           0x2F
#define ROLE_SYSTEM_PROGRESSBAR        0x30
#define ROLE_SYSTEM_DIAL               0x31
#define ROLE_SYSTEM_HOTKEYFIELD        0x32
#define ROLE_SYSTEM_SLIDER             0x33
#define ROLE_SYSTEM_SPINBUTTON         0x34
#define ROLE_SYSTEM_DIAGRAM            0x35
#define ROLE_SYSTEM_ANIMATION          0x36
#define ROLE_SYSTEM_EQUATION           0x37
#define ROLE_SYSTEM_BUTTONDROPDOWN     0x38
#define ROLE_SYSTEM_BUTTONMENU         0x39
#define ROLE_SYSTEM_BUTTONDROPDOWNGRID 0x3A
#define ROLE_SYSTEM_WHITESPACE         0x3B
#define ROLE_SYSTEM_PAGETABLIST        0x3C
#define ROLE_SYSTEM_CLOCK              0x3D
#define ROLE_SYSTEM_SPLITBUTTON        0x3E
#define ROLE_SYSTEM_IPADDRESS          0x3F
#define ROLE_SYSTEM_OUTLINEBUTTON      0x40

    /* States */
#define STATE_SYSTEM_NONE            0
#define STATE_SYSTEM_UNAVAILABLE     0x1
#define STATE_SYSTEM_SELECTED        0x2
#define STATE_SYSTEM_FOCUSED         0x4
#define STATE_SYSTEM_PRESSED         0x8
#define STATE_SYSTEM_CHECKED         0x10
#define STATE_SYSTEM_MIXED           0x20
#define STATE_SYSTEM_INDETERMINATE   STATE_SYSTEM_MIXED
#define STATE_SYSTEM_READONLY        0x40
#define STATE_SYSTEM_HOTTRACKED      0x80
#define STATE_SYSTEM_DEFAULT         0x100
#define STATE_SYSTEM_EXPANDED        0x200
#define STATE_SYSTEM_COLLAPSED       0x400
#define STATE_SYSTEM_BUSY            0x800
#define STATE_SYSTEM_FLOATING        0x1000
#define STATE_SYSTEM_MARQUEED        0x2000
#define STATE_SYSTEM_ANIMATED        0x4000
#define STATE_SYSTEM_INVISIBLE       0x8000
#define STATE_SYSTEM_OFFSCREEN       0x10000
#define STATE_SYSTEM_SIZEABLE        0x20000
#define STATE_SYSTEM_MOVEABLE        0x40000
#define STATE_SYSTEM_SELFVOICING     0x80000
#define STATE_SYSTEM_FOCUSABLE       0x100000
#define STATE_SYSTEM_SELECTABLE      0x200000
#define STATE_SYSTEM_LINKED          0x400000
#define STATE_SYSTEM_TRAVERSED       0x800000
#define STATE_SYSTEM_MULTISELECTABLE 0x1000000
#define STATE_SYSTEM_EXTSELECTABLE   0x2000000
#define STATE_SYSTEM_ALERT_LOW       0x4000000
#define STATE_SYSTEM_ALERT_MEDIUM    0x8000000
#define STATE_SYSTEM_ALERT_HIGH      0x10000000
#define STATE_SYSTEM_PROTECTED       0x20000000
#define STATE_SYSTEM_HASPOPUP        0x40000000
#define STATE_SYSTEM_VALID           0x7FFFFFFF

    /* Navigation directions */
#define NAVDIR_UP         0x1
#define NAVDIR_DOWN       0x2
#define NAVDIR_LEFT       0x3
#define NAVDIR_RIGHT      0x4
#define NAVDIR_NEXT       0x5
#define NAVDIR_PREVIOUS   0x6
#define NAVDIR_FIRSTCHILD 0x7
#define NAVDIR_LASTCHILD  0x8

    /* Selection flags */
#define SELFLAG_NONE            0
#define SELFLAG_TAKEFOCUS       0x1
#define SELFLAG_TAKESELECTION   0x2
#define SELFLAG_EXTENDSELECTION 0x4
#define SELFLAG_ADDSELECTION    0x8
#define SELFLAG_REMOVESELECTION 0x10
#define SELFLAG_VALID           0x1f

    /* DISPIDs for IAccessible (IDispatch) */
#define DISPID_ACC_PARENT           (-5000)
#define DISPID_ACC_CHILDCOUNT       (-5001)
#define DISPID_ACC_CHILD            (-5002)
#define DISPID_ACC_NAME             (-5003)
#define DISPID_ACC_VALUE            (-5004)
#define DISPID_ACC_DESCRIPTION      (-5005)
#define DISPID_ACC_ROLE             (-5006)
#define DISPID_ACC_STATE            (-5007)
#define DISPID_ACC_HELP             (-5008)
#define DISPID_ACC_KEYBOARDSHORTCUT (-5009)
#define DISPID_ACC_FOCUS            (-5010)
#define DISPID_ACC_SELECTION        (-5011)
#define DISPID_ACC_DEFAULTACTION    (-5012)
#define DISPID_ACC_SELECT           (-5014)
#define DISPID_ACC_LOCATION         (-5015)
#define DISPID_ACC_NAVIGATE         (-5016)
#define DISPID_ACC_HITTEST          (-5017)
#define DISPID_ACC_DODEFAULTACTION  (-5018)

    /* ------------------------------------------------------------------ *
     *                          接口                                       *
     * ------------------------------------------------------------------ */

    EXTERN_C const IID IID_IAccessible;
    EXTERN_C const IID IID_IEnumVARIANT;

#if defined(__cplusplus) && !defined(CINTERFACE)

    DEFINE_GUID(IID_IAccessible, 0x618736E0, 0x3C3D, 0x11CF, 0x81, 0x0C, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71);
#undef INTERFACE
#define INTERFACE IAccessible
    DECLARE_INTERFACE_(IAccessible, IDispatch)
    {
        DECLARE_CLASS_SIID(IID_IAccessible)

        STDMETHOD(get_accParent)(THIS_ IDispatch * *ppdispParent) PURE;
        STDMETHOD(get_accChildCount)(THIS_ long *pcountChildren) PURE;
        STDMETHOD(get_accChild)(THIS_ VARIANT varChild, IDispatch * *ppdispChild) PURE;
        STDMETHOD(get_accName)(THIS_ VARIANT varChild, BSTR * pszName) PURE;
        STDMETHOD(get_accValue)(THIS_ VARIANT varChild, BSTR * pszValue) PURE;
        STDMETHOD(get_accDescription)(THIS_ VARIANT varChild, BSTR * pszDescription) PURE;
        STDMETHOD(get_accRole)(THIS_ VARIANT varChild, VARIANT * pvarRole) PURE;
        STDMETHOD(get_accState)(THIS_ VARIANT varChild, VARIANT * pvarState) PURE;
        STDMETHOD(get_accHelp)(THIS_ VARIANT varChild, BSTR * pszHelp) PURE;
        STDMETHOD(get_accHelpTopic)(THIS_ BSTR * pszHelpFile, VARIANT varChild, long *pidTopic) PURE;
        STDMETHOD(get_accKeyboardShortcut)(THIS_ VARIANT varChild, BSTR * pszKeyboardShortcut) PURE;
        STDMETHOD(get_accFocus)(THIS_ VARIANT * pvarChild) PURE;
        STDMETHOD(get_accSelection)(THIS_ VARIANT * pvarChildren) PURE;
        STDMETHOD(get_accDefaultAction)(THIS_ VARIANT varChild, BSTR * pszDefaultAction) PURE;

        STDMETHOD(accSelect)(THIS_ long flagsSelect, VARIANT varChild) PURE;
        STDMETHOD(accLocation)(THIS_ long *pxLeft, long *pyTop, long *pcxWidth, long *pcyHeight, VARIANT varChild) PURE;
        STDMETHOD(accNavigate)(THIS_ long navDir, VARIANT varStart, VARIANT *pvarEndUpAt) PURE;
        STDMETHOD(accHitTest)(THIS_ long xLeft, long yTop, VARIANT *pvarChild) PURE;
        STDMETHOD(accDoDefaultAction)(THIS_ VARIANT varChild) PURE;

        STDMETHOD(put_accName)(THIS_ VARIANT varChild, BSTR szName) PURE;
        STDMETHOD(put_accValue)(THIS_ VARIANT varChild, BSTR szValue) PURE;
    };

    DEFINE_GUID(IID_IEnumVARIANT, 0x00020404, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#undef INTERFACE
#define INTERFACE IEnumVARIANT
    DECLARE_INTERFACE_(IEnumVARIANT, IUnknown)
    {
        DECLARE_CLASS_SIID(IID_IEnumVARIANT)

        STDMETHOD(Next)(THIS_ unsigned long celt, VARIANT *rgVar, unsigned long *pCeltFetched) PURE;
        STDMETHOD(Skip)(THIS_ unsigned long celt) PURE;
        STDMETHOD(Reset)(THIS) PURE;
        STDMETHOD(Clone)(THIS_ IEnumVARIANT * *ppEnum) PURE;
    };

#endif /* __cplusplus && !CINTERFACE */

    /* ------------------------------------------------------------------ *
     *                          函数                                       *
     * ------------------------------------------------------------------ */

    LRESULT WINAPI LresultFromObject(REFIID riid, WPARAM wParam, LPUNKNOWN pAcc);
    HRESULT WINAPI ObjectFromLresult(LRESULT lResult, REFIID riid, WPARAM wParam, void **ppv);

    HRESULT WINAPI AccessibleObjectFromWindow(HWND hwnd, DWORD dwObjectID, REFIID riid, void **ppv);
    HRESULT WINAPI AccessibleObjectFromPoint(POINT ptScreen, IAccessible **ppAcc, VARIANT *pvarChild);
    HRESULT WINAPI AccessibleObjectFromEvent(HWND hwnd, DWORD dwObjectID, DWORD dwChildID, IAccessible **ppAcc, VARIANT *pvarChild);
    HRESULT WINAPI AccessibleChildren(IAccessible *paccContainer, long iChildStart, long cChildren, VARIANT *rgvarChildren, long *pcObtained);

    void WINAPI NotifyWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild);

    UINT WINAPI GetRoleTextA(DWORD lRole, LPSTR lpszRole, UINT cchRoleMax);
    UINT WINAPI GetRoleTextW(DWORD lRole, LPWSTR lpszRole, UINT cchRoleMax);
    UINT WINAPI GetStateTextA(DWORD lStateBit, LPSTR lpszState, UINT cchStateMax);
    UINT WINAPI GetStateTextW(DWORD lStateBit, LPWSTR lpszState, UINT cchStateMax);

#ifdef __cplusplus
}
#endif

#endif /* __OLEACC_H__ */
