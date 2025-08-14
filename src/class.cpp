#include <windows.h>
#include <class.h>
#include <list>
#include <map>
#include <string>
#include <atomic>
#include <mutex>
#include <gdi.h>
#include "clsmgr.h"
#include "tostring.hpp"
#include "debug.h"
#define kLogTag "class"

UINT GetAtomNameA(ATOM atomName, LPSTR name, int cchLen)
{
    return ClassMgr::instance()->get_atom_name(atomName, name, cchLen);
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

ATOM WINAPI FindAtomA(LPCSTR lpString)
{
    CLASS *cls = ClassMgr::instance()->find_class(0, lpString);
    return cls ? cls->atomName : 0;
}

ATOM WINAPI FindAtomW(LPCWSTR lpString)
{
    std::string str;
    tostring(lpString, -1, str);
    return FindAtomA(str.c_str());
}

/***********************************************************************
 *		RegisterClassExW (USER32.@)
 */
ATOM WINAPI RegisterClassExA(const WNDCLASSEXA *wc)
{
    return ClassMgr::instance()->register_class(wc);
}

ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *wc)
{
    WNDCLASSEXA wca;
    memcpy(&wca, wc, FIELD_OFFSET(WNDCLASSEXW, lpszMenuName));
    wca.cbSize = sizeof(wca);
    char szClsName[MAX_ATOM_LEN] = { 0 };
    char szMenuName[MAX_ATOM_LEN] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, wc->lpszClassName, -1, szClsName, MAX_ATOM_LEN, NULL, NULL);
    if (wc->lpszMenuName)
        WideCharToMultiByte(CP_UTF8, 0, wc->lpszMenuName, -1, szMenuName, MAX_ATOM_LEN, NULL, NULL);
    wca.lpszClassName = szClsName;
    wca.lpszMenuName = szMenuName;
    wca.hIconSm = wc->hIconSm;
    return RegisterClassExA(&wca);
}

ATOM WINAPI RegisterClassA(const WNDCLASSA *lpWndClass)
{
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

ATOM WINAPI RegisterClassW(const WNDCLASSW *lpWndClass)
{
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

BOOL WINAPI UnregisterClassA(LPCSTR className, HINSTANCE instance)
{
    return ClassMgr::instance()->unregister_class(className, instance);
}

BOOL WINAPI UnregisterClassW(LPCWSTR className, HINSTANCE instance)
{
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
ATOM WINAPI GetClassInfoExA(HINSTANCE hInstance, LPCSTR name, WNDCLASSEXA *wc)
{
    if (!wc)
    {
        SetLastError(ERROR_NOACCESS);
        return 0;
    }
    return ClassMgr::instance()->get_class_info(hInstance, name, wc);
}

ATOM WINAPI GetClassInfoExW(HINSTANCE hInstance, LPCWSTR name, WNDCLASSEXW *wc)
{
    ATOM atom;

    if (!wc)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    WNDCLASSEXA wca;
    if (IS_INTRESOURCE(name))
    {
        atom = ClassMgr::instance()->get_class_info(hInstance, (LPCSTR)name, &wca);
    }
    else
    {
        char szClsName[MAX_ATOM_LEN] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, name, -1, szClsName, MAX_ATOM_LEN, NULL, NULL);
        atom = ClassMgr::instance()->get_class_info(hInstance, szClsName, &wca);
    }
    if (!atom)
    {
        return 0;
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
    return atom;
}

ULONG_PTR GetClassLongSize(ATOM atom, int nIndex, int sz)
{
    if (nIndex < 0)
        return 0;
    char szName[MAX_ATOM_LEN + 1];
    if (0 == GetAtomNameA(atom, szName, MAX_ATOM_LEN + 1))
        return 0;
    CLASS *cls = ClassMgr::instance()->find_class(0, szName);
    if (!cls)
        return 0;
    ULONG_PTR ret = 0;
    memcpy(&ret, (char *)(cls + 1) + nIndex, sz);
    return ret;
}

ULONG_PTR SetClassLongSize(ATOM atom, int nIndex, ULONG_PTR data, int sz)
{
    if (nIndex < 0)
        return 0;
    char szName[MAX_ATOM_LEN + 1];
    if (0 == GetAtomNameA(atom, szName, MAX_ATOM_LEN + 1))
        return 0;
    CLASS *cls = ClassMgr::instance()->find_class(0, szName);
    if (!cls)
        return 0;
    ULONG_PTR ret = 0;
    memcpy(&ret, (char *)(cls + 1) + nIndex, sz);
    memcpy((char *)(cls + 1) + nIndex, &data, sz);
    return ret;
}
