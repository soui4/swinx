#ifndef __TCHAR_H_
#define __TCHAR_H_

#include <ctypes.h>
#include <strapi.h>

#ifndef _UNICODE
#define TCHAR                char
#define LPCTSTR              LPCSTR
#define LPTSTR               LPSTR
#define _tcscpy_s(p1, n, p2) strcpy(p1, p2)
#define _tcslwr              _mbslwr
#define _tcscmp              strcmp
#define _T(x)                x
#define _tcsftime            strftime
#define _tfopen              fopen
#else
#define TCHAR                wchar_t
#define LPCTSTR              LPCWSTR
#define LPTSTR               LPWSTR
#define _tcscpy_s(p1, n, p2) wcscpy(p1, p2)
#define _tcslwr              _wcslwr
#define _tcscmp              wcscmp
#define _T(x)                L##x
#define _tcsftime            wcsftime
#define _tfopen              _wfopen
#endif

#endif //__TCHAR_H_