#include <windows.h>
#include "uniconv.h"
using namespace swinx;

const uint8_t *_mbsinc(const uint8_t *srcU8)
{
    return srcU8 + UTF8CharLength(*srcU8);
}

uint8_t *_mbscvt(uint8_t *srcU8, BOOL bLower)
{
    uint8_t *p = srcU8;
    while (*p)
    {
        uint8_t *next = (uint8_t *)_mbsinc(p);
        if (!next)
            break;
        if (next - p == 1)
        {
            if (bLower)
                *p = tolower(*p);
            else
                *p = toupper(*p);
        }
        p = next;
    }
    return srcU8;
}

wchar_t *_wcslwr(wchar_t *s)
{
    wchar_t *str = s;
    while (*str)
    {
        *str = towlower(*str);
        str++;
    }
    return s;
}

wchar_t *_wcsupr(wchar_t *s)
{
    wchar_t *str = s;
    while (*str)
    {
        *str = towupper(*str);
        str++;
    }
    return s;
}

void strcpy_s(char *destination, size_t num, const char *source)
{
    strncpy(destination, source, num);
    destination[num - 1] = L'\0';
}

void wcscpy_s(wchar_t *destination, size_t num, const wchar_t *source)
{
    wcsncpy(destination, source, num);
    destination[num - 1] = L'\0';
}

float _wtof(const wchar_t *src)
{
    wchar_t *endptr;
    double value = wcstod(src, &endptr);
    if (src == endptr)
    {
        return 0.0;
    }
    else
    {
        return value;
    }
}

const char *CharNextA(const char *src)
{
    return (const char *)_mbsinc((const uint8_t *)src);
}

const wchar_t *CharNextW(const wchar_t *src)
{
    if (*src >= 0xD800 && *src <= 0xDBFF)
        return src + 2;
    else
        return src + 1;
}

/***********************************************************************
 *           CharToOemA   (USER32.@)
 */
BOOL WINAPI CharToOemA(LPCSTR s, LPSTR d)
{
    if (!s || !d)
        return FALSE;
    return CharToOemBuffA(s, d, strlen(s) + 1);
}

/***********************************************************************
 *           CharToOemBuffA   (USER32.@)
 */
BOOL WINAPI CharToOemBuffA(LPCSTR s, LPSTR d, DWORD len)
{
    WCHAR *bufW;

    if (!s || !d)
        return FALSE;

    bufW = (WCHAR *)malloc(len * sizeof(WCHAR));
    if (bufW)
    {
        MultiByteToWideChar(CP_ACP, 0, s, len, bufW, len);
        WideCharToMultiByte(CP_OEMCP, 0, bufW, len, d, len, NULL, NULL);
        free(bufW);
    }
    return TRUE;
}

/***********************************************************************
 *           CharToOemBuffW   (USER32.@)
 */
BOOL WINAPI CharToOemBuffW(LPCWSTR s, LPSTR d, DWORD len)
{
    if (!s || !d)
        return FALSE;
    WideCharToMultiByte(CP_OEMCP, 0, s, len, d, len, NULL, NULL);
    return TRUE;
}

/***********************************************************************
 *           CharToOemW   (USER32.@)
 */
BOOL WINAPI CharToOemW(LPCWSTR s, LPSTR d)
{
    if (!s || !d)
        return FALSE;
    return CharToOemBuffW(s, d, lstrlenW(s) + 1);
}

/***********************************************************************
 *           OemToCharA   (USER32.@)
 */
BOOL WINAPI OemToCharA(LPCSTR s, LPSTR d)
{
    if (!s || !d)
        return FALSE;
    return OemToCharBuffA(s, d, strlen(s) + 1);
}

/***********************************************************************
 *           OemToCharBuffA   (USER32.@)
 */
BOOL WINAPI OemToCharBuffA(LPCSTR s, LPSTR d, DWORD len)
{
    WCHAR *bufW;

    if (!s || !d)
        return FALSE;
    bufW = (WCHAR *)malloc(len * sizeof(WCHAR));
    if (bufW)
    {
        MultiByteToWideChar(CP_OEMCP, 0, s, len, bufW, len);
        WideCharToMultiByte(CP_ACP, 0, bufW, len, d, len, NULL, NULL);
        free(bufW);
    }
    return TRUE;
}

/***********************************************************************
 *           OemToCharBuffW   (USER32.@)
 */
BOOL WINAPI OemToCharBuffW(LPCSTR s, LPWSTR d, DWORD len)
{
    if (!s || !d)
        return FALSE;
    MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS, s, len, d, len);
    return TRUE;
}

/***********************************************************************
 *           OemToCharW   (USER32.@)
 */
BOOL WINAPI OemToCharW(LPCSTR s, LPWSTR d)
{
    if (!s || !d)
        return FALSE;
    return OemToCharBuffW(s, d, strlen(s) + 1);
}

#define INRANGE(x, a, b) ((x) >= (a) && (x) <= (b))

static WORD get_char_type(wchar_t ch, DWORD dwInfoType)
{
    WORD ret = 0;
    if (ch == 0x3000)
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_DEFINED;
            break;
        case CT_CTYPE2:
            ret = C2_WHITESPACE;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (ch > 0x7f)
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_DEFINED;
            break;
        case CT_CTYPE2:
            ret = C2_OTHERNEUTRAL;
            break;
        case CT_CTYPE3:
            ret = C3_LOWSURROGATE;
            break;
        }
    }
    else if (INRANGE(ch, 'a', 'z'))
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_LOWER;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (INRANGE(ch, 'A', 'Z'))
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_UPPER;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (INRANGE(ch, '0', '9'))
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_DIGIT;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (INRANGE(ch, 0x21, 0x2f) || ch == 0x7F)
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_PUNCT;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (INRANGE(ch, 0x21, 0x2f)    // !=0x21, /=0x2f
             || INRANGE(ch, 0x3A, 0x40) //:=0x3A @=0x40
             || INRANGE(ch, 0x5B, 0x60) //[=0x5B `=0x60
             || INRANGE(ch, 0x7B, 0x7E) // {=0x7B, ~=0x7E
    )
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_PUNCT;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    else if (ch == 0x20)
    {
        switch (dwInfoType)
        {
        case CT_CTYPE1:
            ret = C1_BLANK;
            break;
        case CT_CTYPE2:
            ret = C2_ARABICNUMBER;
            break;
        case CT_CTYPE3:
            ret = C3_ALPHA;
            break;
        }
    }
    return ret;
}

BOOL WINAPI GetStringTypeExW(_In_ LCID lcid, _In_ DWORD dwInfoType, _In_reads_(nLength) LPCWSTR pszSrc, _In_ int nLength, _Out_ LPWORD pwCharType)
{

    if (dwInfoType != CT_CTYPE1 || dwInfoType != CT_CTYPE2 || dwInfoType != CT_CTYPE3)
        return FALSE;
    int i = 0;
    while (i < nLength)
    {
        int charLen = WideCharLength(*pszSrc);
        if (charLen > 1)
        {
            for (int j = 0; j < charLen; j++)
            {
                switch (dwInfoType)
                {
                case CT_CTYPE1:
                    *pwCharType = C1_DEFINED;
                    break;
                case CT_CTYPE2:
                    *pwCharType = C2_OTHERNEUTRAL;
                    break;
                case CT_CTYPE3:
                    *pwCharType = C3_HIGHSURROGATE;
                    break;
                }
                pwCharType++;
            }
        }
        else
        {
            *pwCharType = get_char_type(*pszSrc, dwInfoType);
            pwCharType++;
        }
        pszSrc += charLen;
        i += charLen;
    }
    return TRUE;
}

BOOL WINAPI GetStringTypeExA(_In_ LCID lcid, _In_ DWORD dwInfoType, _In_reads_(nLength) LPCSTR pszSrc, _In_ int nLength, _Out_ LPWORD pwCharType)
{

    if (dwInfoType != CT_CTYPE1 || dwInfoType != CT_CTYPE2 || dwInfoType != CT_CTYPE3)
        return FALSE;
    int i = 0;
    while (i < nLength)
    {
        int u8CharLen = UTF8CharLength(*pszSrc);
        wchar_t wc[3];
        MultiByteToWideChar(CP_UTF8, 0, pszSrc, u8CharLen, wc, 3);
        int charLen = WideCharLength(wc[0]);
        if (charLen > 1)
        {
            for (int j = 0; j < u8CharLen; j++)
            {
                switch (dwInfoType)
                {
                case CT_CTYPE1:
                    *pwCharType = C1_DEFINED;
                    break;
                case CT_CTYPE2:
                    *pwCharType = C2_OTHERNEUTRAL;
                    break;
                case CT_CTYPE3:
                    *pwCharType = C3_HIGHSURROGATE;
                    break;
                }
                pwCharType++;
            }
        }
        else
        {
            WORD charType = get_char_type(wc[0], dwInfoType);
            for (int j = 0; j < u8CharLen; j++)
            {
                *pwCharType++ = charType;
            }
        }
        pszSrc += charLen;
        i += charLen;
    }
    return TRUE;
}

LPWSTR WINAPI CharLowerW(LPWSTR lpsz)
{
    if (HIWORD(DWORD_PTR(lpsz)) == 0)
    {
        wchar_t c = LOWORD(DWORD_PTR(lpsz));
        return (LPWSTR)(INT_PTR)tolower(c);
    }
    else
    {
        return _wcslwr(lpsz);
    }
}

LPSTR WINAPI CharLowerA(LPSTR lpsz)
{
    if (HIWORD(DWORD_PTR(lpsz)) == 0)
    {
        wchar_t c = LOWORD(DWORD_PTR(lpsz));
        return (LPSTR)(INT_PTR)tolower(c);
    }
    else
    {
        return (LPSTR)_mbscvt((uint8_t *)lpsz, TRUE);
    }
}

DWORD WINAPI CharLowerBuffW(LPWSTR lpsz, DWORD cchLength)
{
    int i = 0;
    while (i < cchLength)
    {
        int charLen = WideCharLength(*lpsz);
        if (charLen > cchLength - i)
            break;
        if (charLen == 1)
        {
            *lpsz = tolower(*lpsz);
        }
        i += charLen;
        lpsz += charLen;
    }
    return i;
}

DWORD WINAPI CharLowerBuffA(LPSTR lpsz, DWORD cchLength)
{
    int i = 0;
    while (i < cchLength)
    {
        int charLen = UTF8CharLength(*lpsz);
        if (charLen > cchLength - i)
            break;
        if (charLen == 1)
        {
            *lpsz = tolower(*lpsz);
        }
        i += charLen;
        lpsz += charLen;
    }
    return i;
}

DWORD WINAPI CharUpperBuffW(LPWSTR lpsz, DWORD cchLength)
{
    int i = 0;
    while (i < cchLength)
    {
        int charLen = WideCharLength(*lpsz);
        if (charLen > cchLength - i)
            break;
        if (charLen == 1)
        {
            *lpsz = toupper(*lpsz);
        }
        i += charLen;
        lpsz += charLen;
    }
    return i;
}

DWORD WINAPI CharUpperBuffA(LPSTR lpsz, DWORD cchLength)
{
    int i = 0;
    while (i < cchLength)
    {
        int charLen = UTF8CharLength(*lpsz);
        if (charLen > cchLength - i)
            break;
        if (charLen == 1)
        {
            *lpsz = toupper(*lpsz);
        }
        i += charLen;
        lpsz += charLen;
    }
    return i;
}