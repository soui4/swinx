/*
 * winres.cpp - Windows Resource API implementation for Linux/Unix
 *
 * This implementation uses ELF sections to store resources compiled
 * from .rc files using MinGW's windres tool.
 */

#include <windows.h>
#include <resapi.h>
#include <memory>
#include <mutex>
#include "coffparser.h"
#include "tostring.hpp"
#include "log.h"
#define kLogTag "winres"

static const char *kCoof_o_start = "_binary_soui_coff_o_start";
static const char *kCoof_o_end = "_binary_soui_coff_o_end";

// Resource database per module
struct ResourceModule
{
    HMODULE hModule;
    std::shared_ptr<WindResResourceParser> parser;
    bool loaded;
};

// Global resource database
static std::map<HMODULE, ResourceModule> g_resourceModules;
static std::recursive_mutex g_resourceMutex;
static bool g_initialized = false;

// Forward declarations
void LoadModuleResources(HMODULE hModule);
void UnloadModuleResources(HMODULE hModule);

// Helper functions
static std::wstring MakeResourceString(LPCWSTR lpRes)
{
    if (IS_INTRESOURCE(lpRes))
    {
        wchar_t buf[16];
        swprintf(buf, 16, L"#%u", (UINT)(UINT_PTR)lpRes);
        return std::wstring(buf);
    }
    return std::wstring(lpRes ? lpRes : L"");
}

static std::wstring MakeResourceString(LPCSTR lpRes)
{
    if (IS_INTRESOURCE(lpRes))
    {
        return MakeResourceString(reinterpret_cast<LPCWSTR>(lpRes));
    }
    std::wstring result;
    towstring(lpRes, -1, result);
    return result;
}

// Load resources from a module
void LoadModuleResources(HMODULE hModule)
{
    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);

    if (g_resourceModules.find(hModule) != g_resourceModules.end())
    {
        return; // Already loaded
    }

    void *startSym = NULL;
    void *endSym = NULL;
    if (hModule == NULL || hModule == GetModuleHandle(NULL))
    {
        startSym = dlsym(RTLD_DEFAULT, kCoof_o_start);
        endSym = dlsym(RTLD_DEFAULT, kCoof_o_end);
    }
    else
    {
        startSym = dlsym((void *)hModule, kCoof_o_start);
        endSym = dlsym((void *)hModule, kCoof_o_end);
    }
    ResourceModule mod;
    mod.hModule = hModule;
    mod.loaded = false;

    if (startSym && endSym && startSym < endSym)
    {
        size_t blobSize = (uintptr_t)endSym - (uintptr_t)startSym;
        // 验证数据大小
        if (blobSize < sizeof(COFF_FILE_HEADER))
        {
            SLOG_STME() << "COFF data too small: " << blobSize;
            g_resourceModules[hModule] = mod;
            return;
        }

        mod.parser = std::make_shared<WindResResourceParser>(startSym, endSym);
        mod.parser->Parse();
        mod.loaded = true;
    }
    g_resourceModules[hModule] = mod;
}

// Unload resources from a module
void UnloadModuleResources(HMODULE hModule)
{
    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);

    auto it = g_resourceModules.find(hModule);
    if (it != g_resourceModules.end())
    {
        g_resourceModules.erase(it);
    }
}

// Initialize resource system
void WINAPI _InitResourceSystem(void)
{
    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    if (g_initialized)
        return;
    g_initialized = true;

    // Load resources from main executable
    LoadModuleResources(NULL);
}

// FindResource implementation
HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType)
{
    return FindResourceExW(hModule, lpType, lpName, 0);
}

HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType)
{
    std::wstring typeW = MakeResourceString(lpType);
    std::wstring nameW = MakeResourceString(lpName);
    return FindResourceW(hModule, nameW.c_str(), typeW.c_str());
}

// FindResourceEx implementation
HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage)
{
    if (!g_initialized)
        _InitResourceSystem();

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return NULL;

    std::wstring typeStr = MakeResourceString(lpType);
    std::wstring nameStr = MakeResourceString(lpName);
    return it->second.parser->FindResourceW(hModule, nameStr.c_str(), typeStr.c_str(), wLanguage);
}

HRSRC WINAPI FindResourceExA(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, WORD wLanguage)
{
    std::wstring typeW = MakeResourceString(lpType);
    std::wstring nameW = MakeResourceString(lpName);
    return FindResourceExW(hModule, typeW.c_str(), nameW.c_str(), wLanguage);
}

// SizeofResource implementation
DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo)
{
    if (!hResInfo)
        return 0;

    if (!hModule)
        hModule = GetModuleHandle(NULL);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return 0;

    return it->second.parser->SizeofResource(hModule, hResInfo);
}

// LoadResource implementation
HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo)
{
    if (!hResInfo)
        return NULL;

    if (!hModule)
        hModule = GetModuleHandle(NULL);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return NULL;

    return it->second.parser->LoadResource(hModule, hResInfo);
}

// LockResource implementation
LPVOID WINAPI LockResource(HGLOBAL hResData)
{
    return (LPVOID)WindResResourceParser::LockResource(hResData);
}

// FreeResource implementation
BOOL WINAPI FreeResource(HGLOBAL hResData)
{
    (void)hResData;
    return TRUE;
}

// EnumResourceNames implementation
BOOL WINAPI EnumResourceNamesW(HMODULE hModule, LPCWSTR lpType, ENUMRESNAMEPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;
    return it->second.parser->EnumResourceNamesW(hModule, lpType, lpEnumFunc, lParam);
}

BOOL WINAPI EnumResourceNamesA(HMODULE hModule, LPCSTR lpType, ENUMRESNAMEPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;
    return it->second.parser->EnumResourceNamesA(hModule, lpType, lpEnumFunc, lParam);
}

// EnumResourceTypes implementation
BOOL WINAPI EnumResourceTypesW(HMODULE hModule, ENUMRESTYPEPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;

    return it->second.parser->EnumResourceTypesW(hModule, lpEnumFunc, lParam);
}

BOOL WINAPI EnumResourceTypesA(HMODULE hModule, ENUMRESTYPEPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;

    return it->second.parser->EnumResourceTypesA(hModule, lpEnumFunc, lParam);
}

// EnumResourceLanguages implementation
BOOL WINAPI EnumResourceLanguagesW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, ENUMRESLANGPROCW lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;

    return it->second.parser->EnumResourceLanguagesW(hModule, lpType, lpName, lpEnumFunc, lParam);
}

BOOL WINAPI EnumResourceLanguagesA(HMODULE hModule, LPCSTR lpType, LPCSTR lpName, ENUMRESLANGPROCA lpEnumFunc, LONG_PTR lParam)
{
    if (!g_initialized)
        _InitResourceSystem();
    if (!lpEnumFunc)
        return FALSE;

    if (!hModule)
        hModule = GetModuleHandle(NULL);
    LoadModuleResources(hModule);

    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto it = g_resourceModules.find(hModule);
    if (it == g_resourceModules.end() || !it->second.parser)
        return FALSE;

    return it->second.parser->EnumResourceLanguagesA(hModule, lpType, lpName, lpEnumFunc, lParam);
}

// UpdateResource functions - stubs (read-only implementation)
HANDLE WINAPI BeginUpdateResourceW(LPCWSTR pFileName, BOOL bDeleteExistingResources)
{
    (void)pFileName;
    (void)bDeleteExistingResources;
    // Read-only implementation
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

HANDLE WINAPI BeginUpdateResourceA(LPCSTR pFileName, BOOL bDeleteExistingResources)
{
    (void)pFileName;
    (void)bDeleteExistingResources;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

BOOL WINAPI UpdateResourceW(HANDLE hUpdate, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage, LPVOID lpData, DWORD cbData)
{
    (void)hUpdate;
    (void)lpType;
    (void)lpName;
    (void)wLanguage;
    (void)lpData;
    (void)cbData;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI UpdateResourceA(HANDLE hUpdate, LPCSTR lpType, LPCSTR lpName, WORD wLanguage, LPVOID lpData, DWORD cbData)
{
    (void)hUpdate;
    (void)lpType;
    (void)lpName;
    (void)wLanguage;
    (void)lpData;
    (void)cbData;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI EndUpdateResourceW(HANDLE hUpdate, BOOL fDiscard)
{
    (void)hUpdate;
    (void)fDiscard;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL WINAPI EndUpdateResourceA(HANDLE hUpdate, BOOL fDiscard)
{
    (void)hUpdate;
    (void)fDiscard;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// LoadString implementation
int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer, int cchBufferMax)
{
    if (!lpBuffer || cchBufferMax <= 0)
        return 0;

    if (!g_initialized)
        _InitResourceSystem();

    // 加载模块资源
    LoadModuleResources((HMODULE)hInstance);

    // 获取模块的资源解析器
    std::lock_guard<std::recursive_mutex> lock(g_resourceMutex);
    auto modIt = g_resourceModules.find((HMODULE)hInstance);
    if (modIt == g_resourceModules.end() || !modIt->second.parser)
    {
        if (cchBufferMax > 0)
            lpBuffer[0] = L'\0';
        return 0;
    }

    auto &parser = modIt->second.parser;

    // 调用解析器的 LoadString 方法
    return parser->LoadStringW(uID, lpBuffer, cchBufferMax);
}

int WINAPI LoadStringA(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int cchBufferMax)
{
    WCHAR wbuf[1024];
    int len = LoadStringW(hInstance, uID, wbuf, 1024);
    if (len > 0)
    {
        return WideCharToMultiByte(CP_ACP, 0, wbuf, len + 1, lpBuffer, cchBufferMax, NULL, NULL) - 1;
    }
    if (cchBufferMax > 0)
        lpBuffer[0] = '\0';
    return 0;
}
