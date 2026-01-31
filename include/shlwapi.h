#ifndef _SHLWAPI_H_
#define _SHLWAPI_H_ 

#include <shellapi.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    BOOL WINAPI PathIsDirectoryA(LPCSTR pszPath);
    BOOL WINAPI PathIsDirectoryW(LPCWSTR pszPath);
    
#ifdef UNICODE
#define PathIsDirectory  PathIsDirectoryW
#else
#define PathIsDirectory  PathIsDirectoryA
#endif//UNICODE

    BOOL WINAPI PathFileExistsA(LPCSTR pszPath);
    BOOL WINAPI PathFileExistsW(LPCWSTR pszPath);

#ifdef UNICODE
#define PathFileExists PathFileExistsW
#else
#define PathFileExists PathFileExistsA
#endif // !UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus

#endif//_SHLWAPI_H_