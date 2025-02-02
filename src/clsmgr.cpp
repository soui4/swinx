#include <windows.h>
#include "clsmgr.h"
#include "nativewnd.h"
#include "cmnctl32/cmnctl32.h"
#include "cmnctl32/builtin_classname.h"
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
        clsInfo.lpszClassName = CLS_WINDOW;
        clsInfo.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        RegisterClassEx(&clsInfo);
    }
    {
        WNDCLASSEXA clsInfo = { 0 };
        clsInfo.cbSize = sizeof(clsInfo);
        clsInfo.lpfnWndProc = DefWindowProc;
        clsInfo.lpszClassName = TOOLTIPS_CLASS;
        clsInfo.hbrBackground = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
        RegisterClassEx(&clsInfo);
    }
    CNativeWnd::RegisterCls(WC_MENU);
    TRegisterClass<CStatic>(WC_STATIC);
    TRegisterClass<CButton>(WC_BUTTON);

    LISTBOX_Register();
}

ClassMgr *ClassMgr::instance()
{
    static ClassMgr _thisObj;
    return &_thisObj;
}

CLASS *ClassMgr::find_class(HINSTANCE module, LPCSTR clsName)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    builtin_register();
    if (IS_INTRESOURCE(clsName))
    {
        ATOM atom = (ATOM) reinterpret_cast<LONG_PTR>(clsName);
        bool bValid = false;
        for (auto &it : atom_map)
        {
            if (it.second == atom)
            {
                clsName = it.first.c_str();
                bValid = true;
                break;
            }
        }
        if (!bValid)
            return nullptr;
    }
    for (auto &it : class_list)
    {
        if (stricmp(it->name, clsName) == 0)
            return it;
    }
    return NULL;
}

ATOM ClassMgr::get_class_info(HINSTANCE instance, const char *class_name, WNDCLASSEXA *wc)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    CLASS *_class;
    ATOM atom;
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
    atom = _class->atomName;
    return atom;
}

UINT ClassMgr::get_atom_name(ATOM atomName, LPSTR name, int cchLen)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    for (auto &it : atom_map)
    {
        if (it.second == atomName)
        {
            if (cchLen == 0)
                return it.first.length();
            if (cchLen > it.first.length())
            {
                strcpy(name, it.first.c_str());
                return it.first.length() + 1;
            }
            return 0;
        }
    }
    return 0;
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
        GetAtomName(_class->atomName, _class->name, sizeof(_class->name));
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
        // todo:hjx
        std::unique_lock<std::recursive_mutex> lock(cls_mutex);
        auto it = atom_map.find(_class->name);
        if (it != atom_map.end())
        {
            _class->atomName = it->second;
        }
        else
        {
            _class->atomName = ++atom_start;
            atom_map.insert(std::make_pair(_class->name, _class->atomName));
        }
    }
    else
    {
        std::unique_lock<std::recursive_mutex> lock(cls_mutex);
        atom_map.insert(std::make_pair(_class->name, _class->atomName));
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
            atom_map.erase(std::string(_class->name));
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
