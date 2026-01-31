#include <fileapi.h>
#include <shlwapi.h>
#include "tostring.hpp"


BOOL WINAPI PathIsDirectoryA(LPCSTR pszPath){
    return GetFileAttributesA(pszPath) & FILE_ATTRIBUTE_DIRECTORY;
}

BOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath){
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathIsDirectoryA(strPath.c_str());
}

BOOL WINAPI PathFileExistsA(LPCSTR pszPath){
    return GetFileAttributesA(pszPath) != INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI PathFileExistsW(LPCWSTR pszPath){
    std::string strPath;
    tostring(pszPath, -1, strPath);
    return PathFileExistsA(strPath.c_str());
}