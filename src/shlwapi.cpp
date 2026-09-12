#include <fileapi.h>
#include <shlwapi.h>
#include "tostring.h"

BOOL WINAPI PathIsDirectoryA(LPCSTR pszPath)
{
    // INVALID_FILE_ATTRIBUTES (0xFFFFFFFF) would AND with every flag,
    // so a non-existent path must be rejected explicitly
    DWORD attr = GetFileAttributesA(pszPath);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return attr & FILE_ATTRIBUTE_DIRECTORY;
}

BOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath)
{
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathIsDirectoryA(strPath.c_str());
}

BOOL WINAPI PathFileExistsA(LPCSTR pszPath)
{
    return GetFileAttributesA(pszPath) != INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI PathFileExistsW(LPCWSTR pszPath)
{
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathFileExistsA(strPath.c_str());
}