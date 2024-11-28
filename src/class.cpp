#include <windows.h>
#include <class.h>
#include <list>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <gdi.h>
#include "tostring.hpp"

#include "cmnctl32/nativewnd.h"
#include "cmnctl32/cmnctl32.h"
#include "cmnctl32/builtin_classname.h"

#include "debug.h"
#define kLogTag "class"

void LISTBOX_Register(void);

class ClassMgr {
public:
    ClassMgr() :atom_start(10){
        builtin_register();
    }

    ~ClassMgr() {
        std::unique_lock<std::recursive_mutex> lock(cls_mutex);
        for (auto it : class_list) {
            DeleteObject(it->hbrBackground);
            DestroyIcon(it->hIcon);
            DestroyIcon(it->hIconSm);
            free(it);
        }
        class_list.clear();
    }

public:
    CLASS* find_class(HINSTANCE module, LPCSTR clsName);
    ATOM get_class_info(HINSTANCE instance, const char* class_name, WNDCLASSEXA* wc);
    UINT get_atom_name(ATOM atomName, LPSTR name, int cchLen);
    ATOM register_class(const WNDCLASSEXA* wc);
    BOOL unregister_class(LPCSTR className, HINSTANCE instance);
    void builtin_register();
public:
    std::recursive_mutex cls_mutex;
    std::list<CLASS*> class_list;
    std::map<std::string, ATOM> atom_map;
    std::atomic_uint32_t atom_start;
};


void ClassMgr::builtin_register() {
    WNDCLASSEXA clsInfo = { 0 };
    clsInfo.cbSize = sizeof(clsInfo);
    clsInfo.lpfnWndProc = DefWindowProc;
    clsInfo.lpszClassName = CLS_WINDOW;
    clsInfo.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    RegisterClassEx(&clsInfo);

    LISTBOX_Register();
    CNativeWnd::RegisterCls(WC_NATIVE);
    CNativeWnd::RegisterCls(WC_MENU);

    TRegisterClass<CStatic>(WC_STATIC);
    TRegisterClass<CButton>(WC_BUTTON);
}

static ClassMgr _clsMgr;

CLASS* ClassMgr::find_class(HINSTANCE module, LPCSTR clsName)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    if (IS_INTRESOURCE(clsName))
    {
        ATOM atom = (ATOM) reinterpret_cast<LONG_PTR>(clsName);
        bool bValid = false;
        for (auto& it : atom_map)
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
    for (auto& it : class_list)
    {
        if (stricmp(it->name, clsName) == 0)
            return it;
    }
    return NULL;
}

ATOM ClassMgr::get_class_info(HINSTANCE instance, const char* class_name, WNDCLASSEXA* wc)
{
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    CLASS* _class;
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

/***********************************************************************
 *           get_int_atom_value
 */
ATOM get_int_atom_value(const char *name)
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

UINT ClassMgr::get_atom_name(ATOM atomName, LPSTR name, int cchLen) {
    std::unique_lock<std::recursive_mutex> lock(cls_mutex);
    for (auto& it : atom_map)
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


UINT GetAtomNameA(ATOM atomName, LPSTR name, int cchLen)
{
    return _clsMgr.get_atom_name(atomName, name, cchLen);
}

UINT GetAtomNameW(ATOM atomName, LPWSTR name, int cchLen)
{
    char szName[MAX_ATOM_LEN + 1];
    UINT ret = GetAtomNameA(atomName, szName, MAX_ATOM_LEN + 1);
    if (!ret)
        return 0;
    wchar_t wsName[MAX_ATOM_LEN + 1];
    int cbWchar = MultiByteToWideChar(CP_UTF8, 0, szName, ret, wsName, MAX_ATOM_LEN + 1);
    if (cchLen == 0)
        return cbWchar + 1;
    if (cchLen < cbWchar + 1)
        return 0;
    wcscpy(name, wsName);
    return cbWchar + 1;
}

ATOM WINAPI FindAtomA(LPCSTR lpString) {
    CLASS* cls = _clsMgr.find_class(0, lpString);
    return cls ? cls->atomName : 0;
}

ATOM WINAPI FindAtomW(LPCWSTR lpString) {
    std::string str;
    tostring(lpString, -1, str);
    return FindAtomA(str.c_str());
}

ATOM ClassMgr::register_class(const WNDCLASSEXA* wc) {
    HINSTANCE instance;
    CLASS* _class;
    ATOM atom;
    BOOL ret;

    if (wc->cbSize != sizeof(*wc) || wc->cbClsExtra < 0 || wc->cbWndExtra < 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (_clsMgr.find_class(wc->hInstance, wc->lpszClassName)) {
        SetLastError(ERROR_CLASS_ALREADY_EXISTS);
        return 0;
    }
    /* Fix the extra bytes value */

    if (wc->cbClsExtra > 40) /* Extra bytes are limited to 40 in Win32 */
        WARN("Class extra bytes %d is > 40\n", wc->cbClsExtra);
    if (wc->cbWndExtra > 40) /* Extra bytes are limited to 40 in Win32 */
        WARN("Win extra bytes %d is > 40\n", wc->cbWndExtra);

    if (!(_class = (CLASS*)calloc(1, sizeof(CLASS) + wc->cbClsExtra)))
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
    if (wc->hbrBackground && IS_INTRESOURCE(wc->hbrBackground)) {
        _class->hbrBackground = RefGdiObj(GetSysColorBrush((int)(UINT_PTR)wc->hbrBackground));
    }
    else {
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
 *		RegisterClassExW (USER32.@)
 */
ATOM WINAPI RegisterClassExA(const WNDCLASSEXA *wc)
{
    return _clsMgr.register_class(wc);
}

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW* wc)
{
    WNDCLASSEXA wca;
    memcpy(&wca, wc, FIELD_OFFSET(WNDCLASSEXW, lpszMenuName));
    wca.cbSize = sizeof(wca);
    char szClsName[MAX_ATOM_LEN] = { 0 };
    char szMenuName[MAX_ATOM_LEN] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, wc->lpszClassName, -1, szClsName, MAX_ATOM_LEN, NULL, NULL);
    if(wc->lpszMenuName)
        WideCharToMultiByte(CP_UTF8, 0, wc->lpszMenuName, -1, szMenuName, MAX_ATOM_LEN, NULL, NULL);
    wca.lpszClassName = szClsName;
    wca.lpszMenuName = szMenuName;
    wca.hIconSm = wc->hIconSm;
    return RegisterClassExA(&wca);
}

ATOM WINAPI RegisterClassA(const WNDCLASSA* lpWndClass) {
    WNDCLASSEXA wca = { 0 };
    wca.cbSize = sizeof(wca);
    wca.style = lpWndClass->style;
    wca.cbClsExtra = lpWndClass->cbClsExtra;
    wca.cbWndExtra = lpWndClass->cbWndExtra;
    wca.lpfnWndProc = lpWndClass->lpfnWndProc;
    wca.hInstance = lpWndClass->hInstance;
    wca.hIcon = lpWndClass->hIcon;
    wca.hCursor = lpWndClass->hCursor;
    wca.hbrBackground = lpWndClass->hbrBackground;
    wca.lpszClassName = lpWndClass->lpszClassName;
    return RegisterClassExA(&wca);
}

ATOM WINAPI RegisterClassW(const WNDCLASSW* lpWndClass) {
    WNDCLASSEXW wca = { 0 };
    wca.cbSize = sizeof(wca);
    wca.style = lpWndClass->style;
    wca.cbClsExtra = lpWndClass->cbClsExtra;
    wca.cbWndExtra = lpWndClass->cbWndExtra;
    wca.lpfnWndProc = lpWndClass->lpfnWndProc;
    wca.hInstance = lpWndClass->hInstance;
    wca.hIcon = lpWndClass->hIcon;
    wca.hCursor = lpWndClass->hCursor;
    wca.hbrBackground = lpWndClass->hbrBackground;
    wca.lpszClassName = lpWndClass->lpszClassName;
    return RegisterClassExW(&wca);

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

BOOL WINAPI UnregisterClassA(LPCSTR className, HINSTANCE instance) {
    return _clsMgr.unregister_class(className, instance);
}


BOOL WINAPI UnregisterClassW(LPCWSTR className, HINSTANCE instance) {
    if (IS_INTRESOURCE(className))
        return UnregisterClassA((LPCSTR)className, instance);
    char szClsName[MAX_ATOM_LEN];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, className, -1, szClsName, MAX_ATOM_LEN, NULL, NULL))
        return FALSE;
    return UnregisterClassA(szClsName, instance);

}

/***********************************************************************
 *		GetClassInfoExW (USER32.@)
 */
BOOL WINAPI GetClassInfoExA(HINSTANCE hInstance, LPCSTR name, WNDCLASSEXA *wc)
{
    ATOM atom;

    if (!wc)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return _clsMgr.get_class_info(hInstance, name, wc);
}

BOOL WINAPI GetClassInfoExW(HINSTANCE hInstance, LPCWSTR name, WNDCLASSEXW* wc)
{
    ATOM atom;

    if (!wc)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    WNDCLASSEXA wca;
    if (IS_INTRESOURCE(name)) {
        atom = _clsMgr.get_class_info(hInstance, (LPCSTR)name, &wca);
    }
    else {
        char szClsName[MAX_ATOM_LEN] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, szClsName, MAX_ATOM_LEN, NULL, NULL);
        atom = _clsMgr.get_class_info(hInstance, szClsName, &wca);
    }
    if (!atom) {        
        return FALSE;
    }
    wc->style = wca.style;
    wc->lpfnWndProc = wca.lpfnWndProc;
    wc->cbClsExtra = wca.cbClsExtra;
    wc->cbWndExtra = wca.cbWndExtra;
    wc->hInstance = wca.hInstance;
    wc->hIcon = wca.hIcon;
    wc->hIconSm = wca.hIconSm;
    wc->hCursor = wca.hCursor;
    wc->hbrBackground = wca.hbrBackground;
    wc->lpszClassName = name;
    return TRUE;
}

ULONG_PTR GetClassLongSize(ATOM atom, int nIndex, int sz) {
    if (nIndex < 0)
        return 0;
    char szName[MAX_ATOM_LEN+1];
    if (0 == GetAtomNameA(atom, szName, MAX_ATOM_LEN+1))
        return 0;
    CLASS* cls = _clsMgr.find_class(0, szName);
    if (!cls)
        return 0;
    ULONG_PTR ret = 0;
    memcpy(&ret, (char*)(cls + 1) + nIndex, sz);
    return ret;
}

ULONG_PTR SetClassLongSize(ATOM atom, int nIndex, ULONG_PTR data, int sz) {
    if (nIndex < 0)
        return 0;
    char szName[MAX_ATOM_LEN + 1];
    if (0 == GetAtomNameA(atom, szName, MAX_ATOM_LEN + 1))
        return 0;
    CLASS* cls = _clsMgr.find_class(0, szName);
    if (!cls)
        return 0;
    ULONG_PTR ret = 0;
    memcpy(&ret, (char*)(cls + 1) + nIndex, sz);
    memcpy((char*)(cls + 1) + nIndex, &data, sz);
    return ret;
}
