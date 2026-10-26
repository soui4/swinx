#ifndef __STRFUN_H_
#define __STRFUN_H_
#include <sfr_config.h>

#if SFR_HOSTED
/* The freertos port's _mbs* surface is byte/UTF-8 semantics (see
 * src/strapi.cpp), while mingw's <mbstring.h> (pulled in by <tchar.h>)
 * declares the real DBCS-aware MBCS functions with conflicting signatures.
 * Keep the shim macros authoritative and suppress the DBCS header. */
#define _INC_MBSTRING
#endif
#include <stdio.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <ctypes.h>
#include <errno.h>
#include <tchar.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

// count the length of the formatted string
#define _vscprintf(fmt, argList)  vsnprintf(nullptr, 0, fmt, argList)
#define _vscwprintf(fmt, argList) vswprintf(nullptr, 0, fmt, argList)

#define _mbslwr(x) _mbscvt((uint8_t *)(x), TRUE)
#define _mbsupr(x) _mbscvt((uint8_t *)(x), FALSE)

#define _mbsstr(s1, s2) strstr((const char *)(s1), (const char *)(s2))

#define _mbschr(s1, c)                 strchr((const char *)(s1), (c))
#define _mbsrchr(s1, c)                strrchr((const char *)(s1), (c))
#define _mbsicmp(s1, s2)               strcasecmp((const char *)(s1), (const char *)(s2))
#define _mbscmp(s1, s2)                strcmp((const char *)(s1), (const char *)(s2))
#define _ismbcspace                    isspace
#if !SFR_HOSTED
	#define vswprintf_s                    vswprintf
	#define swprintf_s                     swprintf
	#define memmove_s(dst, ndst, src, len) memmove(dst, src, len)
	#define swscanf_s                      swscanf
	#define vsprintf_s                vsnprintf
	#define sprintf_s                 _snprintf
	#define strcat_s(dst, n, src)     strncat(dst, src, n)
	#define wcscat_s(dst, n, src)     wcsncat(dst, src, n)

    /* MSVC's *_s copy helpers return errno_t (int): 0 on success, EINVAL/ERANGE
     * otherwise. Callers test the result, so it cannot be void. */
    int WINAPI strcpy_s(char *destination, size_t num, const char *source);
    int WINAPI wcscpy_s(wchar_t *destination, size_t num, const wchar_t *source);
    float WINAPI _wtof(const wchar_t *src);
#endif
#define _wtoi(x)                       wcstol(x, NULL, 10)
#define _wtol(x)                       wcstol(x, NULL, 10)
#define lstrlenA                       strlen
#define lstrlenW                       wcslen
#define lstrcpyA                       strcpy
#define lstrcpyW                       wcscpy
#define lstrcatA                       strcat
#define lstrcatW                       wcscat

    const uint8_t *_mbsinc(const uint8_t *srcU8);

    uint8_t *_mbscvt(uint8_t *srcU8, BOOL bLower);

    wchar_t *WINAPI _wcslwr(wchar_t *s);

    wchar_t *WINAPI _wcsupr(wchar_t *s);


    int WINAPI _snprintf(char *buffer, size_t size, const char *format, ...);

    int WINAPI _vsnprintf(char *buffer, size_t size, const char *format, va_list args);

    /* MSVC CRT 的 _timeb/_ftime（<sys/timeb.h> 在 Windows 上提供）。
     * 与 POSIX 的 struct timeb/ftime 同义，只是命名不同。 */
    struct _timeb
    {
        time_t time;
        unsigned short millitm;
        short timezone;
        short dstflag;
    };
    void WINAPI _ftime(struct _timeb *tp);

    int WINAPI _snwprintf(wchar_t *buffer, size_t size, const wchar_t *format, ...);

    const char *WINAPI CharNextA(const char *src);

    const wchar_t *WINAPI CharNextW(const wchar_t *src);

    int _wrename(const wchar_t *oldpath, const wchar_t *newpath);

    void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext);
    void _wsplitpath(const wchar_t *path, wchar_t *drive, wchar_t *dir, wchar_t *fname, wchar_t *ext);

#ifdef UNICODE
#define _tsplitpath _wsplitpath
#define _trename    _wrename
#else
#define _tsplitpath _splitpath
#define _trename    rename
#endif // UNICODE

#if SFR_HOSTED
// mingw's CRT provides _wcsicmp/_wcsnicmp (no POSIX wcscasecmp there);
// strcasecmp/strncasecmp do exist in mingw's string.h.
#define _wcsicmp(s1, s2)       _wcsicmp(s1, s2)
#define _wcsnicmp(s1, s2, num) _wcsnicmp(s1, s2, num)
#else
#define _wcsicmp(s1, s2)       wcscasecmp(s1, s2)
#define _wcsnicmp(s1, s2, num) wcsncasecmp(s1, s2, num)
#endif
#define wcsnicmp               _wcsnicmp

#define stricmp(s1, s2)       strcasecmp(s1, s2)
#define strnicmp(s1, s2, num) strncasecmp(s1, s2, num)
#define wcsicmp               _wcsicmp

/* MSVC CRT spellings of the above; code ported from Windows uses them. */
#define _stricmp(s1, s2)       stricmp(s1, s2)
#define _strnicmp(s1, s2, num) strnicmp(s1, s2, num)

/* _putenv()/putenv() both take a mutable "NAME=value" string and do not copy
 * it, so keeping the caller's buffer alive is the caller's business either way. */
#ifndef _putenv
#define _putenv putenv
#endif

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

#define lstrcpynW wcsncpy
#define lstrcpynA strncpy

#define CP_ACP        0  // default to ANSI code page
#define CP_OEMCP      1  // default to OEM  code page
#define CP_MACCP      2  // default to MAC  code page
#define CP_THREAD_ACP 3  // current thread's ANSI code page
#define CP_SYMBOL     42 // SYMBOL translations

#define CP_UTF7     65000 // UTF-7 translation
#define CP_UTF8     65001 // UTF-8 translation
#define CP_UTF16_LE 1200  // UTF-16 little endian translation
#define CP_UTF16_BE 1201  // UTF-16 big endian translation

#define MB_PRECOMPOSED       0x00000001 // DEPRECATED: use single precomposed characters when possible.
#define MB_COMPOSITE         0x00000002 // DEPRECATED: use multiple discrete characters when possible.
#define MB_USEGLYPHCHARS     0x00000004 // DEPRECATED: use glyph chars, not ctrl chars
#define MB_ERR_INVALID_CHARS 0x00000008 // error for invalid chars

    BOOL WINAPI CharToOemBuffA(LPCSTR lpszSrc, LPSTR lpszDst, DWORD cchSrcLength);

    BOOL WINAPI CharToOemBuffW(LPCWSTR lpszSrc, LPSTR lpszDst, DWORD cchSrcLength);

    BOOL WINAPI OemToCharBuffA(LPCSTR lpszSrc, LPSTR lpszDst, DWORD cchSrcLength);

    BOOL WINAPI OemToCharBuffW(LPCSTR lpszSrc, LPWSTR lpszDst, DWORD cchSrcLength);

    BOOL WINAPI CharToOemA(LPCSTR s, LPSTR d);
    BOOL WINAPI CharToOemW(LPCWSTR s, LPSTR d);
    BOOL WINAPI OemToCharA(LPCSTR s, LPSTR d);
    BOOL WINAPI OemToCharW(LPCSTR s, LPWSTR d);

#ifdef UNICODE
#define CharToOemBuff CharToOemBuffW
#define OemToCharBuff OemToCharBuffW
#define CharToOem     CharToOemW
#define OemToChar     OemToCharW
#else
#define CharToOemBuff CharToOemBuffA
#define OemToCharBuff OemToCharBuffA
#define CharToOem     CharToOemA
#define OemToChar     OemToCharA
#endif // !UNICODE

    BOOL WINAPI GetStringTypeExW(_In_ LCID lcid, _In_ DWORD dwInfoType, _In_reads_(nLength) LPCWSTR pszSrc, _In_ int nLength, _Out_ LPWORD pwCharType);

    BOOL WINAPI GetStringTypeExA(_In_ LCID lcid, _In_ DWORD dwInfoType, _In_reads_(nLength) LPCSTR pszSrc, _In_ int nLength, _Out_ LPWORD pwCharType);

    LPWSTR WINAPI CharLowerW(LPWSTR lpsz);
    LPSTR WINAPI CharLowerA(LPSTR lpsz);

    DWORD WINAPI CharLowerBuffW(LPWSTR lpsz, DWORD cchLength);
    DWORD WINAPI CharLowerBuffA(LPSTR lpsz, DWORD cchLength);

    DWORD WINAPI CharUpperBuffW(LPWSTR lpsz, DWORD cchLength);
    DWORD WINAPI CharUpperBuffA(LPSTR lpsz, DWORD cchLength);

#ifdef UNICODE
#define GetStringTypeEx GetStringTypeExW
#define CharLower       CharLowerW
#define CharLowerBuff   CharLowerBuffW
#define CharUpperBuff   CharUpperBuffW
#else
#define GetStringTypeEx GetStringTypeExA
#define CharLower       CharLowerA
#define CharLowerBuff   CharLowerBuffA
#define CharUpperBuff   CharUpperBuffA
#endif // UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //__STRFUN_H_
