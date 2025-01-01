﻿#include <windows.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include "SConnection.h"
#include "wnd.h"
#include "uimsg.h"
#include "uniconv.h"
#include <setjmp.h>
#include "tostring.hpp"
#include "debug.h"
#define kLogTag "sysapi"

using namespace swinx;

FILE *_wfopen(const wchar_t *path, const wchar_t *mode)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    char *u8path = (char *)malloc(len + 1);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, u8path, len + 1, nullptr, nullptr);
    u8path[len] = 0;
    char u8mode[20] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, mode, -1, u8mode, 20, nullptr, nullptr);
    FILE *ret = fopen(u8path, u8mode);
    free(u8path);
    return ret;
}

void SetLastError(int e)
{
    errno = e;
}

int GetLastError()
{
    return errno;
}

int MulDiv(int a, int b, int c)
{
    int64_t t = int64_t(a) * b;
    return (int)(t / c);
}

tid_t GetCurrentThreadId()
{
    return (tid_t)pthread_self();
}

int MultiByteToWideChar(int cp, int flags, const char *src, int len, wchar_t *dst, int dstLen)
{
    assert(src);
    if (cp == CP_OEMCP)
        cp = CP_UTF8; // todo:hjx
    if (cp != CP_ACP && cp != CP_UTF8)
        return 0;
    if (len < 0)
		len = strlen(src);
#if (WCHAR_SIZE==2)
	assert(sizeof(wchar_t) == 2);
	//handle for utf16
    int bufRequire = UTF16Length(src, len);
    if (!dst) {
		return bufRequire;
	}
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
	else {
		return UTF16FromUTF8(src, len, (uint16_t*)dst, dstLen);
}
#else
	assert(sizeof(wchar_t) == 4);
	//handle for utf32
    int bufRequire = UTF32Length(src, len);
    if (!dst) {
		return bufRequire;
    }if (bufRequire > dstLen) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
	else {
		return UTF32FromUTF8(src, len, (uint32_t*)dst, dstLen);
	}
#endif
}

int WideCharToMultiByte(int cp, int flags, const wchar_t *src, int len, char *dst, int dstLen, LPCSTR p1, BOOL *p2)
{
    assert(src);
    const wchar_t *ptr = src;
    if (len < 0)
        len = wcslen(src);
    const wchar_t *stop = src + len;
    size_t i = 0;
    if (cp == CP_OEMCP)
        cp = CP_UTF8; // todo:hjx
    if (cp != CP_ACP && cp != CP_UTF8)
        return 0;

#if (WCHAR_SIZE==2)
	assert(sizeof(wchar_t) == 2);
    int bufRequire = UTF16toUTF8Length((const uint16_t*)src, len);
    if (!dst) {
		return bufRequire;
    }
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
	else {
		return UTF8FromUTF16((const uint16_t*)src, len, dst, dstLen);
	}
#else
	assert(sizeof(wchar_t) == 4);
    int bufRequire = UTF32toUTF8Length((const uint32_t*)src, len);
    if (!dst)
        return bufRequire;
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
		return UTF8FromUTF32((const uint32_t*)src, len, dst, dstLen);
#endif
}

#define STIF_DEFAULT     0x00000000L
#define STIF_SUPPORT_HEX 0x00000001L

BOOL StrToInt64ExW(const wchar_t *str, DWORD flags, LONGLONG *ret)
{
    BOOL negative = FALSE;
    LONGLONG value = 0;

    if (!str || !ret)
        return FALSE;

    /* Skip leading space, '+', '-' */
    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;

    if (*str == '-')
    {
        negative = TRUE;
        str++;
    }
    else if (*str == '+')
        str++;

    if (flags & STIF_SUPPORT_HEX && *str == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        /* Read hex number */
        str += 2;

        if (!isxdigit(*str))
            return FALSE;

        while (isxdigit(*str))
        {
            value *= 16;
            if (*str >= '0' && *str <= '9')
                value += (*str - '0');
            else if (*str >= 'A' && *str <= 'Z')
                value += 10 + (*str - 'A');
            else
                value += 10 + (*str - 'a');
            str++;
        }

        *ret = value;
        return TRUE;
    }

    /* Read decimal number */
    if (*str < '0' || *str > '9')
        return FALSE;

    while (*str >= '0' && *str <= '9')
    {
        value *= 10;
        value += (*str - '0');
        str++;
    }

    *ret = negative ? -value : value;
    return TRUE;
}

BOOL StrToIntExW(const wchar_t *str, DWORD flags, INT *ret)
{
    LONGLONG value;
    BOOL res;
    res = StrToInt64ExW(str, flags, &value);
    if (res)
        *ret = value;
    return res;
}

BOOL StrToInt64ExA(const char* str, DWORD flags, LONGLONG* ret) {
    std::wstring wstr;
    towstring(str, -1, wstr);
    return StrToInt64ExW(wstr.c_str(), flags, ret);
}

BOOL StrToIntExA(const char* str, DWORD flags, INT* ret) {
    LONGLONG value;
    BOOL res;
    res = StrToInt64ExA(str, flags, &value);
    if (res)
        *ret = value;
    return res;
}

void GetLocalTime(SYSTEMTIME *pSysTime)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *now = localtime(&tv.tv_sec);
    pSysTime->wYear = now->tm_year+1900;
    pSysTime->wMonth = now->tm_mon;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = tv.tv_usec / 1000;
}

void GetSystemTime(SYSTEMTIME *pSysTime)
{
    time_t rawTime = time(NULL);
    struct tm *now = localtime(&rawTime);
    pSysTime->wYear = now->tm_year;
    pSysTime->wMonth = now->tm_mon;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = 0; // todo:hjx
}

time_t _mkgmtime(struct tm *_Tm)
{
    //todo:hjx
    return 0;
}

int _localtime64_s(struct tm *ptm, const __time64_t *ptime)
{
    *ptm = *localtime((time_t*)ptime);
    return 0;
}

static inline BOOL is_valid_hex(WCHAR c)
{
    if (!(((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F'))))
        return FALSE;
    return TRUE;
}

static const BYTE guid_conv_table[256] = {
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 */
    0, 1,   2,   3,   4,   5,   6,   7, 8, 9, 0, 0, 0, 0, 0, 0, /* 0x30 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    0, 0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf                             /* 0x60 */
};

BOOL IIDFromString(LPCWSTR s, GUID *id)
{
    int i;

    if (!s || s[0] != '{')
    {
        memset(id, 0, sizeof(*id));
        if (!s)
            return TRUE;
        return FALSE;
    }

    /* In form {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} */

    id->Data1 = 0;
    for (i = 1; i < 9; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data1 = (id->Data1 << 4) | guid_conv_table[s[i]];
    }
    if (s[9] != '-')
        return FALSE;

    id->Data2 = 0;
    for (i = 10; i < 14; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data2 = (id->Data2 << 4) | guid_conv_table[s[i]];
    }
    if (s[14] != '-')
        return FALSE;

    id->Data3 = 0;
    for (i = 15; i < 19; ++i)
    {
        if (!is_valid_hex(s[i]))
            return FALSE;
        id->Data3 = (id->Data3 << 4) | guid_conv_table[s[i]];
    }
    if (s[19] != '-')
        return FALSE;

    for (i = 20; i < 37; i += 2)
    {
        if (i == 24)
        {
            if (s[i] != '-')
                return FALSE;
            i++;
        }
        if (!is_valid_hex(s[i]) || !is_valid_hex(s[i + 1]))
            return FALSE;
        id->Data4[(i - 20) / 2] = guid_conv_table[s[i]] << 4 | guid_conv_table[s[i + 1]];
    }

    if (s[37] == '}' && s[38] == '\0')
        return TRUE;

    return FALSE;
}

#define __COMPARE(context, p1, p2)                comp(context, p1, p2)
#define __SHORTSORT(lo, hi, width, comp, context) shortsort_s(lo, hi, width, comp, context);

#ifndef _ASSERT_EXPR
#define _ASSERT_EXPR(expr, expr_str) ((void)0)
#endif
#ifndef _CRT_WIDE
#define __CRT_WIDE(_String) L##_String
#define _CRT_WIDE(_String)  __CRT_WIDE(_String)
#endif

#define _INVALID_PARAMETER(expr) _CALL_INVALID_PARAMETER_FUNC(_invalid_parameter, expr)

#define _VALIDATE_RETURN_VOID(expr, errorcode)       \
    {                                                \
        int _Expr_val = !!(expr);                    \
        _ASSERT_EXPR((_Expr_val), _CRT_WIDE(#expr)); \
        if (!(_Expr_val))                            \
        {                                            \
            errno = errorcode;                       \
            return;                                  \
        }                                            \
    }

#define STKSIZ (8 * sizeof(void *) - 2)
#define CUTOFF 8 /* testing shows that this is good value */

static void shortsort_s(char *lo, char *hi, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *);
#define swap swap_c

static void swap(char *p, char *q, size_t width);

LONG InterlockedDecrement(LONG *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

LONG InterlockedIncrement(LONG*v)
{
    return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
}

void qsort_s(void *base, size_t num, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *context)
{
    char *lo, *hi;       /* ends of sub-array currently sorting */
    char *mid;           /* points to middle of subarray */
    char *loguy, *higuy; /* traveling pointers for partition step */
    size_t size;         /* size of the sub-array */
    char *lostk[STKSIZ], *histk[STKSIZ];
    int stkptr; /* stack for saving sub-array to be processed */

    /* validation section */
    _VALIDATE_RETURN_VOID(base != NULL || num == 0, EINVAL);
    _VALIDATE_RETURN_VOID(width > 0, EINVAL);
    _VALIDATE_RETURN_VOID(comp != NULL, EINVAL);

    if (num < 2)
        return; /* nothing to do */

    stkptr = 0; /* initialize stack */

    lo = (char *)base;
    hi = (char *)base + width * (num - 1); /* initialize limits */

    /* this entry point is for pseudo-recursion calling: setting
       lo and hi and jumping to here is like recursion, but stkptr is
       preserved, locals aren't, so we preserve stuff on the stack */
recurse:

    size = (hi - lo) / width + 1; /* number of el's to sort */

    /* below a certain size, it is faster to use a O(n^2) sorting method */
    if (size <= CUTOFF)
    {
        __SHORTSORT(lo, hi, width, comp, context);
    }
    else
    {
        /* First we pick a partitioning element.  The efficiency of the
           algorithm demands that we find one that is approximately the median
           of the values, but also that we select one fast.  We choose the
           median of the first, middle, and last elements, to avoid bad
           performance in the face of already sorted data, or data that is made
           up of multiple sorted runs appended together.  Testing shows that a
           median-of-three algorithm provides better performance than simply
           picking the middle element for the latter case. */

        mid = lo + (size / 2) * width; /* find middle element */

        /* Sort the first, middle, last elements into order */
        if (__COMPARE(context, lo, mid) > 0)
        {
            swap(lo, mid, width);
        }
        if (__COMPARE(context, lo, hi) > 0)
        {
            swap(lo, hi, width);
        }
        if (__COMPARE(context, mid, hi) > 0)
        {
            swap(mid, hi, width);
        }

        /* We now wish to partition the array into three pieces, one consisting
           of elements <= partition element, one of elements equal to the
           partition element, and one of elements > than it.  This is done
           below; comments indicate conditions established at every step. */

        loguy = lo;
        higuy = hi;

        /* Note that higuy decreases and loguy increases on every iteration,
           so loop must terminate. */
        for (;;)
        {
            /* lo <= loguy < hi, lo < higuy <= hi,
               A[i] <= A[mid] for lo <= i <= loguy,
               A[i] > A[mid] for higuy <= i < hi,
               A[hi] >= A[mid] */

            /* The doubled loop is to avoid calling comp(mid,mid), since some
               existing comparison funcs don't work when passed the same
               value for both pointers. */

            if (mid > loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy < mid && __COMPARE(context, loguy, mid) <= 0);
            }
            if (mid <= loguy)
            {
                do
                {
                    loguy += width;
                } while (loguy <= hi && __COMPARE(context, loguy, mid) <= 0);
            }

            /* lo < loguy <= hi+1, A[i] <= A[mid] for lo <= i < loguy,
               either loguy > hi or A[loguy] > A[mid] */

            do
            {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) > 0);

            /* lo <= higuy < hi, A[i] > A[mid] for higuy < i < hi,
               either higuy == lo or A[higuy] <= A[mid] */

            if (higuy < loguy)
                break;

            /* if loguy > hi or higuy == lo, then we would have exited, so
               A[loguy] > A[mid], A[higuy] <= A[mid],
               loguy <= hi, higuy > lo */

            swap(loguy, higuy, width);

            /* If the partition element was moved, follow it.  Only need
               to check for mid == higuy, since before the swap,
               A[loguy] > A[mid] implies loguy != mid. */

            if (mid == higuy)
                mid = loguy;

            /* A[loguy] <= A[mid], A[higuy] > A[mid]; so condition at top
               of loop is re-established */
        }

        /*     A[i] <= A[mid] for lo <= i < loguy,
               A[i] > A[mid] for higuy < i < hi,
               A[hi] >= A[mid]
               higuy < loguy
           implying:
               higuy == loguy-1
               or higuy == hi - 1, loguy == hi + 1, A[hi] == A[mid] */

        /* Find adjacent elements equal to the partition element.  The
           doubled loop is to avoid calling comp(mid,mid), since some
           existing comparison funcs don't work when passed the same value
           for both pointers. */

        higuy += width;
        if (mid < higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > mid && __COMPARE(context, higuy, mid) == 0);
        }
        if (mid >= higuy)
        {
            do
            {
                higuy -= width;
            } while (higuy > lo && __COMPARE(context, higuy, mid) == 0);
        }

        /* OK, now we have the following:
              higuy < loguy
              lo <= higuy <= hi
              A[i]  <= A[mid] for lo <= i <= higuy
              A[i]  == A[mid] for higuy < i < loguy
              A[i]  >  A[mid] for loguy <= i < hi
              A[hi] >= A[mid] */

        /* We've finished the partition, now we want to sort the subarrays
           [lo, higuy] and [loguy, hi].
           We do the smaller one first to minimize stack usage.
           We only sort arrays of length 2 or more.*/

        if (higuy - lo >= hi - loguy)
        {
            if (lo < higuy)
            {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy;
                ++stkptr;
            } /* save big recursion for later */

            if (loguy < hi)
            {
                lo = loguy;
                goto recurse; /* do small recursion */
            }
        }
        else
        {
            if (loguy < hi)
            {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                ++stkptr; /* save big recursion for later */
            }

            if (lo < higuy)
            {
                hi = higuy;
                goto recurse; /* do small recursion */
            }
        }
    }

    /* We have sorted the array, except for any pending sorts on the stack.
       Check if there are any, and do them. */

    --stkptr;
    if (stkptr >= 0)
    {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse; /* pop subarray from stack */
    }
    else
        return; /* all subarrays done */
}

/***
 *shortsort(hi, lo, width, comp) - insertion sort for sorting short arrays
 *shortsort_s(hi, lo, width, comp, context) - insertion sort for sorting short arrays
 *
 *Purpose:
 *       sorts the sub-array of elements between lo and hi (inclusive)
 *       side effects:  sorts in place
 *       assumes that lo < hi
 *
 *Entry:
 *       char *lo = pointer to low element to sort
 *       char *hi = pointer to high element to sort
 *       size_t width = width in bytes of each array element
 *       int (*comp)() = pointer to function returning analog of strcmp for
 *               strings, but supplied by user for comparing the array elements.
 *               it accepts 2 pointers to elements, together with a pointer to a context
 *               (if present). Returns neg if 1<2, 0 if 1=2, pos if 1>2.
 *       void *context - pointer to the context in which the function is
 *               called. This context is passed to the comparison function.
 *
 *Exit:
 *       returns void
 *
 *Exceptions:
 *
 *******************************************************************************/

static void shortsort_s(char *lo, char *hi, size_t width, int(__cdecl *comp)(void *, const void *, const void *), void *context)
{
    char *p, *max;

    /* Note: in assertions below, i and j are alway inside original bound of
       array to sort. */

    while (hi > lo)
    {
        /* A[i] <= A[j] for i <= j, j > hi */
        max = lo;
        for (p = lo + width; p <= hi; p += width)
        {
            /* A[i] <= A[max] for lo <= i < p */
            if (__COMPARE(context, p, max) > 0)
            {
                max = p;
            }
            /* A[i] <= A[max] for lo <= i <= p */
        }

        /* A[i] <= A[max] for lo <= i <= hi */

        swap(max, hi, width);

        /* A[i] <= A[hi] for i <= hi, so A[i] <= A[j] for i <= j, j >= hi */

        hi -= width;

        /* A[i] <= A[j] for i <= j, j > hi, loop top condition established */
    }
    /* A[i] <= A[j] for i <= j, j > lo, which implies A[i] <= A[j] for i < j,
       so array is sorted */
}

/***
 *swap(a, b, width) - swap two elements
 *
 *Purpose:
 *       swaps the two array elements of size width
 *
 *Entry:
 *       char *a, *b = pointer to two elements to swap
 *       size_t width = width in bytes of each array element
 *
 *Exit:
 *       returns void
 *
 *Exceptions:
 *
 *******************************************************************************/

static void swap(char *a, char *b, size_t width)
{
    char tmp;

    if (a != b)
        /* Do the swap one character at a time to avoid potential alignment
           problems. */
        while (width--)
        {
            tmp = *a;
            *a++ = *b;
            *b++ = tmp;
        }
}

#undef __COMPARE
#undef __SHORTSORT
#undef swap

void PostThreadMessageW(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{
    return PostThreadMessageA(tid, msg, wp, lp);
}
void PostThreadMessageA(uint64_t tid, UINT msg, WPARAM wp, LPARAM lp)
{
    SConnection *conn = SConnMgr::instance()->getConnection(tid);
    if (!conn)
        return;
    conn->postMsg(0, msg, wp, lp);
}

BOOL WaitMessage()
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMsg();
}

DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE *pHandles, BOOL fWaitAll, DWORD dwMilliseconds, DWORD dwWakeMask)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->waitMutliObjectAndMsg(pHandles, nCount, dwMilliseconds, fWaitAll, dwWakeMask);
}

BOOL GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->getMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}

BOOL PeekMessage(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    return conn->peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

int GetSystemScale(){
    //todo:hjx
    int dpi = GetDpiForWindow(0);
    return dpi*100/96;
}

int GetSystemMetrics(int nIndex)
{
    int ret = 0;
    switch (nIndex)
    {
    case SM_CXBORDER:
    case SM_CYBORDER:
    case SM_CXEDGE:
    case SM_CYEDGE:
        return 1;
    case SM_CXICON:
        ret = 32;
        break;
    case SM_CYICON:
        ret = 32;
        break;
    case SM_CXCURSOR:
        ret = 32;
        break;
    case SM_CYCURSOR:
        ret = 32;
        break;
    case SM_CYHSCROLL:
    case SM_CXVSCROLL:
        ret = 16;
        break;
    case SM_CYVTHUMB:
    case SM_CXHTHUMB:
        ret = 16;
        break;
    case SM_CXDRAG:
    case SM_CYDRAG:
        return 4;
    case SM_CXMINTRACK:
    case SM_CYMINTRACK:
        return 6;
    default:
        printf("unknown index for GetSystemMetrics, index=%d\n", nIndex);
        break;
    }
    return ret*GetSystemScale()/100;
}

BOOL ShellExecute(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd)
{
    return FALSE;
}

BOOL GetKeyboardState(PBYTE lpKeyState)
{
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyboardState(lpKeyState);
}

SHORT GetKeyState(int nVirtKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyState(nVirtKey);
}

SHORT WINAPI GetAsyncKeyState(int vKey) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetAsyncKeyState(vKey);
}


UINT MapVirtualKey(UINT uCode, UINT uMapType)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->MapVirtualKey(uCode, uMapType);
}

UINT MapVirtualKeyEx(UINT uCode, UINT uMapType, HKL dwhkl)
{
    //todo:hjx
    return MapVirtualKey(uCode, uMapType);
}

DWORD GetTickCount()
{
    return (DWORD)GetTickCount64();
}

uint64_t GetTickCount64()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

BOOL CallMsgFilter(LPMSG lpMsg, int nCode)
{
    return 0;
}

__time64_t _mktime64(const tm *ptime)
{
    return mktime((struct tm *)ptime);
}

__time64_t _time64(__time64_t *_Time)
{
    time_t rawtime=0;
    time_t ret = time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);
    return time((time_t *)_Time);
}

HCURSOR LoadCursor(HINSTANCE hInstance, LPCSTR lpCursorName)
{
    return CursorMgr::LoadCursor(lpCursorName);
}

BOOL DestroyCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    BOOL bRet = conn->DestroyCursor(hCursor);
    if(!bRet) return FALSE;
    return CursorMgr::DestroyCursor(hCursor);
}

HCURSOR SetCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetCursor(hCursor);
}

HCURSOR GetCursor(VOID) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetCursor();
}

static __thread sigjmp_buf jump_buffer; 

static void sigsegv_handler(int sig) {
//    printf("Caught signal %d\n", sig);
    siglongjmp(jump_buffer, 1);
}

BOOL IsBadReadPtr(const void* ptr, size_t size)
{
	if (ptr == NULL || size == 0)
	{
		return 1; // Invalid pointer or size
	}
	// Set up signal handler
	struct sigaction new_action, old_action;
	new_action.sa_handler = sigsegv_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGSEGV, &new_action, &old_action) == -1) {
		perror("sigaction");
		return FALSE;
	}
	BOOL bRet = FALSE;
	if (sigsetjmp(jump_buffer, 1) == 0) {
		volatile const char* p = (const char*)ptr;
		char dummy __attribute__((unused));
		UINT_PTR count = size;
		size_t pageSize = sysconf(_SC_PAGESIZE);
		while (count > pageSize)
		{
			dummy = *p;
			p += pageSize;
			count -= pageSize;
		}
		dummy = p[0];
		dummy = p[count - 1];
	}
	else {
		bRet = TRUE;
	}
	if (sigaction(SIGSEGV, &old_action, NULL) == -1) {
		perror("sigaction");
	}
	return bRet; // Invalid pointer
}

BOOL IsBadWritePtr(const void* ptr, size_t size)
{
	if (ptr == NULL || size == 0)
	{
		return 1; // Invalid pointer or size
	}
	struct sigaction new_action, old_action;
	new_action.sa_handler = sigsegv_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGSEGV, &new_action, &old_action) == -1) {
		perror("sigaction");
		return FALSE;
	}
	BOOL bRet = FALSE;
	if (sigsetjmp(jump_buffer, 1) == 0) {

		volatile char* p = (char*)ptr;
		UINT_PTR count = size;
		size_t pageSize = sysconf(_SC_PAGESIZE);

		while (count > pageSize)
		{
			*p |= 0;
			p += pageSize;
			count -= pageSize;
		}
		p[0] |= 0;
		p[count - 1] |= 0;
	}
	else {
		bRet = TRUE;
	}
	if (sigaction(SIGSEGV, &old_action, NULL) == -1) {
		perror("sigaction");
	}
    //printf("IsBadWritePtr %p,len=%d,ret=%d\n",ptr,(int)size,bRet);
	return bRet; // Invalid pointer       
}

BOOL WINAPI IsBadStringPtrA(LPCSTR lpsz, UINT_PTR ucchMax)
{
    if (!lpsz || ucchMax == 0)
        return TRUE;
    for (UINT_PTR i = 0; i < ucchMax; i++)
    {
        if (lpsz[i] == 0)
        {
            ucchMax = i + 1;
            break;
        }
    }
    return IsBadReadPtr(lpsz, ucchMax);
}

BOOL WINAPI IsBadStringPtrW(LPCWSTR lpsz, UINT_PTR ucchMax)
{
    if (!lpsz || ucchMax == 0)
        return TRUE;
    for (UINT_PTR i = 0; i < ucchMax; i++)
    {
        if (lpsz[i] == 0)
        {
            ucchMax = i + 1;
            break;
        }
    }
    return IsBadReadPtr(lpsz, ucchMax * sizeof(wchar_t));
}

HANDLE
WINAPI
GetCurrentProcess(VOID)
{
    return 0;
}

DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
    if (hModule)
    {
        Dl_info info;
        int result = dladdr(hModule, &info);
        if (!result)
            return 0;
        ssize_t len = strlen(info.dli_fname);
        if (!lpFilename)
            return len;
        if (nSize < len)
            return 0;
        strncpy(lpFilename, info.dli_fname, len);
        if (len < nSize)
            lpFilename[len] = 0;
        return len;
    }
    else
    {
        char path[MAX_PATH];
        ssize_t len;
        len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1)
        {
            perror("readlink");
            return 0;
        }
        if (lpFilename == 0)
            return len;
        if (nSize < len)
            return 0;
        strncpy(lpFilename, path, len);
        if (len < nSize)
            lpFilename[len] = 0;
        return len;
    }
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    char szName[MAX_PATH];
    if (GetModuleFileNameA(hModule, szName, MAX_PATH) == 0)
        return 0;
    std::wstring str;
    towstring(szName, -1, str);
    if (str.length() >= nSize)
        return 0;
    wcscpy(lpFilename, str.c_str());
    return str.length();
}

void WINAPI OutputDebugStringA(LPCSTR lpOutputString)
{
    printf("%s", lpOutputString);
}

void WINAPI OutputDebugStringW(LPCWSTR lpOutputString)
{
    wprintf(L"%s", lpOutputString);
}

void WINAPI set_error(int e)
{
    errno = e;
}

VOID WINAPI Sleep(DWORD dwMilliseconds)
{
    usleep(dwMilliseconds * 1000);
}


LONG CompareFileTime(const FILETIME* ft1, const FILETIME* ft2)
{
    if (ft1->dwHighDateTime < ft2->dwHighDateTime) return -1;
    if (ft1->dwHighDateTime > ft2->dwHighDateTime) return 1;
    if (ft1->dwLowDateTime < ft2->dwLowDateTime) return -1;
    if (ft1->dwLowDateTime > ft2->dwLowDateTime) return 1;
    return 0;
}


BOOL WINAPI SystemParametersInfoA(UINT action, UINT val, void* ptr, UINT winini) {
    //todo:hjx
    return FALSE;
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT val, void* ptr, UINT winini) {
    return SystemParametersInfoA(action, val, ptr, winini);
}

void WINAPI DebugBreak(void)
{
    DbgBreakPoint();
}

VOID WINAPI DbgBreakPoint(VOID) {
    assert(0);
}

UINT WINAPI GetDoubleClickTime(VOID) {
    return SConnMgr::instance()->getConnection()->GetDoubleClickTime();
}

#define TICKSPERSEC 10000000

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    lpFrequency->QuadPart = TICKSPERSEC;
    return TRUE;
}

#define SECS_1601_TO_1970  ((369 * 365 + 89) * (ULONGLONG)86400)

static inline ULONGLONG ticks_from_time_t(time_t time)
{
    if (sizeof(time_t) == sizeof(int))  /* time_t may be signed */
        return ((ULONGLONG)(ULONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
    else
        return ((ULONGLONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    struct timeval now;
    gettimeofday(&now, 0);
    lpPerformanceCount->QuadPart = ticks_from_time_t(now.tv_sec) + now.tv_usec * 10;
    return TRUE;
}


//---------------------------------------------------
UINT
WINAPI
RegisterClipboardFormatA( _In_ LPCSTR lpszFormat) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->RegisterClipboardFormatA(lpszFormat);
}

UINT
WINAPI
RegisterClipboardFormatW(
    _In_ LPCWSTR lpszFormat) {
    std::string str;
    tostring(lpszFormat, -1, str);
    return RegisterClipboardFormatA(str.c_str());
}


BOOL
WINAPI
EmptyClipboard(VOID) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->EmptyClipboard();
}

BOOL
WINAPI
IsClipboardFormatAvailable(_In_ UINT format) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->IsClipboardFormatAvailable(format);
}

BOOL
WINAPI
OpenClipboard(_In_opt_ HWND hWndNewOwner) {
    if(hWndNewOwner){
        tid_t tid = GetWindowThreadProcessId(hWndNewOwner, NULL);
        if (tid != GetCurrentThreadId())
            return FALSE;
    }
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->OpenClipboard(hWndNewOwner);
}

BOOL
WINAPI
CloseClipboard(VOID) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->CloseClipboard();
}

HWND
WINAPI
GetClipboardOwner(VOID) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardOwner();
}

HANDLE
WINAPI
GetClipboardData(_In_ UINT uFormat) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardData(uFormat);
}


HANDLE
WINAPI
SetClipboardData(_In_ UINT uFormat,
    _In_opt_ HANDLE hMem) {
    SConnection* conn = SConnMgr::instance()->getConnection();
    return conn->SetClipboardData(uFormat, hMem);
}

//------------------------------------------------------------
BOOL WINAPI MessageBeep(_In_ UINT uType) {
    //todo:hjx
    return FALSE;
}


DWORD
WINAPI
GetTempPathA(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer
) {
    int len = strlen(lpBuffer);
    if (len + 7 > nBufferLength)
        return len + 7;
    strcpy(lpBuffer + len, "XXXXXX");
    int fd = mkstemp(lpBuffer);
    if (fd != -1) {
        close(fd);
        unlink(lpBuffer);
        return len + 7;
    }
    SetLastError(ERROR_INVALID_ACCESS);
    return 0;
}

DWORD
WINAPI
GetTempPathW(
    _In_ DWORD nBufferLength,
    _Out_writes_to_opt_(nBufferLength, return +1) LPWSTR lpBuffer
) {
    char szPath[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpBuffer, nBufferLength, szPath, MAX_PATH, NULL, NULL))
        return 0;
    DWORD ret = GetTempPathA(MAX_PATH, szPath);
    if (ret == 0)
        return 0;
    int len = MultiByteToWideChar(CP_UTF8, 0, szPath, ret, NULL, 0);
    if (len > nBufferLength) {
        SetLastError(ERROR_INVALID_ACCESS);
        return 0;
    }
    return MultiByteToWideChar(CP_UTF8, 0, szPath, ret, lpBuffer, nBufferLength);
}

BOOL IsValidCodePage(UINT CodePage) {
    //todo:hjx
    return TRUE;
}

UINT WINAPI GetKeyboardLayoutList(int nBuff,
    HKL* lpList
) {
    return 0;
}

HKL ActivateKeyboardLayout(HKL hkl,
    UINT Flags
) {
    return 0;
}

UINT WINAPI GetACP(void) {
    return CP_UTF8;
}

BOOL WINAPI IsDBCSLeadByte(BYTE  c){
    return UTF8CharLength(c)>1;
}

//todo:hjx verify
HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName) {
    if (lpModuleName)
    {
        void* hMod = dlopen(lpModuleName, RTLD_LAZY);
        if (hMod) dlclose(hMod);
        return (HMODULE)hMod;
    }
    else {
        char pathexe[MAX_PATH];
        ssize_t len;
        len = readlink("/proc/self/exe", pathexe, sizeof(pathexe) - 1);
        if (len == -1)
        {
            perror("readlink");
            return 0;
        }
        pathexe[len] = 0;
        FILE* fp;
        char path[1024];
        char line[1024];
        void* module_addr = NULL;
        // 打开 /proc/self/maps 文件
        fp = fopen("/proc/self/maps", "r");
        if (fp == NULL) {
            perror("fopen");
            return 0;
        }

        // 读取文件内容，查找包含当前进程可执行文件路径的行
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, pathexe) != NULL) {  // 替换为你的可执行文件路径
                sscanf(line, "%lx", (UINT_PTR*)&module_addr);  // 从行中读取模块地址
                break;
            }
        }
        fclose(fp);
        return (HMODULE)module_addr;
    }
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName) {
    if (!lpModuleName)
        return GetModuleHandleA(NULL);
    std::string str;
    tostring(lpModuleName, -1, str);
    return GetModuleHandleA(str.c_str());
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
    return setenv(lpName, lpValue, 1);
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue)
{
    std::string name, value;
    tostring(lpName, -1, name);
    tostring(lpValue, -1, value);
    return SetEnvironmentVariableA(name.c_str(), value.c_str());
}

DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
    const char *value = getenv(lpName);
    if (!value)
        return 0;
    size_t len = strlen(value);
    if (len >= nSize)
        return len + 1;
    strcpy(lpBuffer, value);
    return len;
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize)
{
    std::string name;
    tostring(lpName, -1, name);
    const char *value = getenv(name.c_str());
    if (!value)
        return 0;
    std::wstring wstr;
    towstring(value, -1, wstr);
    size_t len = wstr.length();
    if (len >= nSize)
        return len + 1;
    wcscpy(lpBuffer, wstr.c_str());
    return len;
}
