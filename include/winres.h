/*
 * winres.h - Windows Resource API for Linux/Unix compatibility
 *
 * This file provides Windows resource management API compatibility
 * for non-Windows platforms using ELF sections to store resources.
 */

#ifndef __WINRES_H_
#define __WINRES_H_

#include <ctypes.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Predefined Resource Types
 */
#define RT_CURSOR       MAKEINTRESOURCE(1)
#define RT_BITMAP       MAKEINTRESOURCE(2)
#define RT_ICON         MAKEINTRESOURCE(3)
#define RT_MENU         MAKEINTRESOURCE(4)
#define RT_DIALOG       MAKEINTRESOURCE(5)
#define RT_STRING       MAKEINTRESOURCE(6)
#define RT_FONTDIR      MAKEINTRESOURCE(7)
#define RT_FONT         MAKEINTRESOURCE(8)
#define RT_ACCELERATOR  MAKEINTRESOURCE(9)
#define RT_RCDATA       MAKEINTRESOURCE(10)
#define RT_MESSAGETABLE MAKEINTRESOURCE(11)
#define RT_GROUP_CURSOR MAKEINTRESOURCE(12)
#define RT_GROUP_ICON   MAKEINTRESOURCE(14)
#define RT_VERSION      MAKEINTRESOURCE(16)
#define RT_DLGINCLUDE   MAKEINTRESOURCE(17)
#define RT_PLUGPLAY     MAKEINTRESOURCE(19)
#define RT_VXD          MAKEINTRESOURCE(20)
#define RT_ANICURSOR    MAKEINTRESOURCE(21)
#define RT_ANIICON      MAKEINTRESOURCE(22)
#define RT_HTML         MAKEINTRESOURCE(23)
#define RT_MANIFEST     MAKEINTRESOURCE(24)

    /*
     * Resource Enumeration Callback Types
     */
    typedef BOOL(CALLBACK *ENUMRESNAMEPROCW)(HMODULE hModule, LPCWSTR lpType, LPWSTR lpName, LONG_PTR lParam);
    typedef BOOL(CALLBACK *ENUMRESTYPEPROCW)(HMODULE hModule, LPWSTR lpType, LONG_PTR lParam);
    typedef BOOL(CALLBACK *ENUMRESLANGPROCW)(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, LONG_PTR lParam);

    typedef BOOL(CALLBACK *ENUMRESNAMEPROCA)(HMODULE hModule, LPCSTR lpType, LPSTR lpName, LONG_PTR lParam);
    typedef BOOL(CALLBACK *ENUMRESTYPEPROCA)(HMODULE hModule, LPSTR lpType, LONG_PTR lParam);
    typedef BOOL(CALLBACK *ENUMRESLANGPROCA)(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, WORD wLanguage, LONG_PTR lParam);

#ifdef UNICODE
#define ENUMRESNAMEPROC ENUMRESNAMEPROCW
#define ENUMRESTYPEPROC ENUMRESTYPEPROCW
#define ENUMRESLANGPROC ENUMRESLANGPROCW
#else
#define ENUMRESNAMEPROC ENUMRESNAMEPROCA
#define ENUMRESTYPEPROC ENUMRESTYPEPROCA
#define ENUMRESLANGPROC ENUMRESLANGPROCA
#endif

    /*
     * Resource Management Functions
     */

    /**
     * Find a resource in the specified module
     */
    HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
    HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType);
#ifdef UNICODE
#define FindResource FindResourceW
#else
#define FindResource FindResourceA
#endif

    /**
     * Find a resource with language specification
     */
    HRSRC WINAPI FindResourceExA(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, WORD wLanguage);
    HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage);
#ifdef UNICODE
#define FindResourceEx FindResourceExW
#else
#define FindResourceEx FindResourceExA
#endif

    /**
     * Load a resource into memory
     */
    HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo);

    /**
     * Get the size of a resource
     */
    DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo);

    /**
     * Lock a resource in memory (get pointer to data)
     */
    LPVOID WINAPI LockResource(HGLOBAL hResData);

    /**
     * Free a loaded resource
     */
    BOOL WINAPI FreeResource(HGLOBAL hResData);

    /**
     * Enumerate resource names
     */
    BOOL WINAPI EnumResourceNamesA(HMODULE hModule, LPCSTR lpType, ENUMRESNAMEPROCA lpEnumFunc, LONG_PTR lParam);
    BOOL WINAPI EnumResourceNamesW(HMODULE hModule, LPCWSTR lpType, ENUMRESNAMEPROCW lpEnumFunc, LONG_PTR lParam);
#ifdef UNICODE
#define EnumResourceNames EnumResourceNamesW
#else
#define EnumResourceNames EnumResourceNamesA
#endif

    /**
     * Enumerate resource types
     */
    BOOL WINAPI EnumResourceTypesA(HMODULE hModule, ENUMRESTYPEPROCA lpEnumFunc, LONG_PTR lParam);
    BOOL WINAPI EnumResourceTypesW(HMODULE hModule, ENUMRESTYPEPROCW lpEnumFunc, LONG_PTR lParam);
#ifdef UNICODE
#define EnumResourceTypes EnumResourceTypesW
#else
#define EnumResourceTypes EnumResourceTypesA
#endif

    /**
     * Enumerate resource languages
     */
    BOOL WINAPI EnumResourceLanguagesA(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, ENUMRESLANGPROCA lpEnumFunc, LONG_PTR lParam);
    BOOL WINAPI EnumResourceLanguagesW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, ENUMRESLANGPROCW lpEnumFunc, LONG_PTR lParam);
#ifdef UNICODE
#define EnumResourceLanguages EnumResourceLanguagesW
#else
#define EnumResourceLanguages EnumResourceLanguagesA
#endif

    /*
     * Resource Update Functions (stub implementations for read-only support)
     */

    HANDLE WINAPI BeginUpdateResourceA(LPCSTR pFileName, BOOL bDeleteExistingResources);
    HANDLE WINAPI BeginUpdateResourceW(LPCWSTR pFileName, BOOL bDeleteExistingResources);
#ifdef UNICODE
#define BeginUpdateResource BeginUpdateResourceW
#else
#define BeginUpdateResource BeginUpdateResourceA
#endif

    BOOL WINAPI UpdateResourceA(HANDLE hUpdate, LPCSTR lpType, LPCSTR lpName, WORD wLanguage, LPVOID lpData, DWORD cbData);
    BOOL WINAPI UpdateResourceW(HANDLE hUpdate, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, LPVOID lpData, DWORD cbData);
#ifdef UNICODE
#define UpdateResource UpdateResourceW
#else
#define UpdateResource UpdateResourceA
#endif

    BOOL WINAPI EndUpdateResourceA(HANDLE hUpdate, BOOL fDiscard);
    BOOL WINAPI EndUpdateResourceW(HANDLE hUpdate, BOOL fDiscard);
#ifdef UNICODE
#define EndUpdateResource EndUpdateResourceW
#else
#define EndUpdateResource EndUpdateResourceA
#endif

    /*
     * Resource String Functions
     */
    int WINAPI LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax);
    int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax);
#ifdef UNICODE
#define LoadString LoadStringW
#else
#define LoadString LoadStringA
#endif

    /*
     * Internal initialization function (called automatically)
     */
    void WINAPI _InitResourceSystem(void);

#ifdef __cplusplus
}
#endif

#endif /* __WINRES_H_ */
