#include <windows.h>
#include "clsmgr.h"
#include "nativewnd.h"
#include "cmnctl32/cmnctl32.h"
#include "cmnctl32/builtin_classname.h"
#include "atoms.h"
#include "log.h"
#define kLogTag "classmgr"

void LISTBOX_Register(void);

void ClassMgr::builtin_register()
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    if (builtin_registed)
        return;
    builtin_registed = true;
    {
        WNDCLASSEXA clsInfo = { 0 };
        clsInfo.cbSize = sizeof(clsInfo);
        clsInfo.lpfnWndProc = DefWindowProc;
        clsInfo.lpszClassName = CLS_WINDOWA;
        clsInfo.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        RegisterClassExA(&clsInfo);
    }
    {
        WNDCLASSEXA clsInfo = { 0 };
        clsInfo.cbSize = sizeof(clsInfo);
        clsInfo.lpfnWndProc = DefWindowProc;
        clsInfo.lpszClassName = TOOLTIPS_CLASSA;
        clsInfo.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
        RegisterClassExA(&clsInfo);
    }
    CNativeWnd::RegisterCls(WC_MENUA);
    TRegisterClass<CStatic>(WC_STATICA);
    TRegisterClass<CButton>(WC_BUTTONA);

    LISTBOX_Register();
}

ClassMgr *ClassMgr::instance()
{
    static ClassMgr _thisObj;
    return &_thisObj;
}

CLASS *ClassMgr::_find_class(HINSTANCE module, LPCSTR clsName)
{
    for (auto &it : class_list)
    {
        if (stricmp(it->name, clsName) == 0)
            return it;
    }
    return NULL;
}

CLASS *ClassMgr::find_class(HINSTANCE module, LPCSTR clsName)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    builtin_register();
    if (IS_INTRESOURCE(clsName))
    {
        ATOM atom = (ATOM) reinterpret_cast<LONG_PTR>(clsName);
        int len = SAtoms::getAtomName(atom, NULL, 0);
        char *buf = new char[len + 1];
        SAtoms::getAtomName(atom, buf, len + 1);
        CLASS *ret = _find_class(module, buf);
        delete[] buf;
        return ret;
    }
    else
    {
        return _find_class(module, clsName);
    }
}

ATOM ClassMgr::get_class_info(HINSTANCE instance, const char *class_name, WNDCLASSEXA *wc)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    CLASS *_class;
    if (!(_class = find_class(instance, class_name)))
        return 0;
    if (wc)
    {
        wc->style = _class->style;
        wc->lpfnWndProc = _class->winproc;
        wc->cbClsExtra = _class->cbClsExtra;
        wc->cbWndExtra = _class->cbWndExtra;
        wc->hInstance = instance;
        wc->hIcon = _class->hIcon;
        wc->hIconSm = _class->hIconSm;
        wc->hCursor = _class->hCursor;
        wc->hbrBackground = _class->hbrBackground;
        wc->lpszClassName = _class->basename;
    }
    return _class->atomName;
}

UINT ClassMgr::get_atom_name(ATOM atomName, LPSTR name, int cchLen)
{
    return SAtoms::getAtomName(atomName, name, cchLen);
}

/***********************************************************************
 *           get_int_atom_value
 */
static ATOM get_int_atom_value(const char *name)
{
    const char *ptr = name;
    const char *end = ptr + strlen(name);
    UINT ret = 0;

    if (IS_INTRESOURCE(ptr))
        return LOWORD(ptr);

    if (*ptr++ != '#')
        return 0;
    while (ptr < end)
    {
        if (*ptr < '0' || *ptr > '9')
            return 0;
        ret = ret * 10 + *ptr++ - '0';
        if (ret > 0xffff)
            return 0;
    }
    return ret;
}

ATOM ClassMgr::register_class(const WNDCLASSEXA *wc)
{
    HINSTANCE instance;
    CLASS *_class;
    ATOM atom;
    BOOL ret;

    if (wc->cbSize != sizeof(*wc) || wc->cbClsExtra < 0 || wc->cbWndExtra < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (find_class(wc->hInstance, wc->lpszClassName))
    {
        SetLastError(ERROR_CLASS_ALREADY_EXISTS);
        return 0;
    }
    /* Fix the extra bytes value */

    if (wc->cbClsExtra > 40) /* Extra bytes are limited to 40 in Win32 */
        SLOG_FMTW("Class extra bytes %d is > 40\n", wc->cbClsExtra);
    if (wc->cbWndExtra > 40) /* Extra bytes are limited to 40 in Win32 */
        SLOG_FMTW("Win extra bytes %d is > 40\n", wc->cbWndExtra);

    if (!(_class = (CLASS *)calloc(1, sizeof(CLASS) + wc->cbClsExtra)))
        return 0;

    _class->atomName = get_int_atom_value(wc->lpszClassName);
    _class->basename = _class->name;
    if (!_class->atomName && wc->lpszClassName)
    {
        strcpy(_class->name, wc->lpszClassName);
    }
    else
    {
        GetAtomNameA(_class->atomName, _class->name, sizeof(_class->name));
    }

    _class->style = wc->style;
    _class->cbWndExtra = wc->cbWndExtra;
    _class->cbClsExtra = wc->cbClsExtra;
    _class->instance = (UINT_PTR)instance;

    /* Other non-null values must be set by caller */
    // if (wc->hIcon && !wc->hIconSm)
    //     sm_icon = CopyImage( wc->hIcon, IMAGE_ICON,
    //                          get_system_metrics( SM_CXSMICON ),
    //                          get_system_metrics( SM_CYSMICON ),
    //                          LR_COPYFROMRESOURCE );

    _class->hIcon = wc->hIcon;
    _class->hIconSm = wc->hIconSm;
    _class->hCursor = wc->hCursor;
    _class->winproc = wc->lpfnWndProc;
    if (wc->hbrBackground && IS_INTRESOURCE(wc->hbrBackground))
    {
        _class->hbrBackground = RefGdiObj(GetSysColorBrush((int)(UINT_PTR)wc->hbrBackground));
    }
    else
    {
        _class->hbrBackground = wc->hbrBackground;
    }
    if (!_class->atomName)
    {
        _class->atomName = SAtoms::registerAtom(_class->name);
    }
    atom = _class->atomName;
    {
        std::unique_lock<std::recursive_mutex> lock(cls_mutex);
        class_list.push_front(_class);
    }

    return atom;
}

/***********************************************************************
 *		UnregisterClassW (USER32.@)
 */
BOOL ClassMgr::unregister_class(LPCSTR className, HINSTANCE instance)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    for (auto it = class_list.begin(); it != class_list.end(); it++)
    {
        CLASS *_class = *it;
        BOOL bMatch = FALSE;
        if (IS_INTRESOURCE(className))
            bMatch = _class->atomName == (ATOM)(UINT_PTR)className;
        else
            bMatch = strcmp(_class->name, className) == 0;
        if (bMatch)
        {
            DeleteObject(_class->hbrBackground);
            DestroyCursor(_class->hCursor);
            DestroyIcon(_class->hIcon);
            DestroyIcon(_class->hIconSm);

            free(_class);
            class_list.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}
