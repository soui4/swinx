/*
 * Profile functions
 *
 * Copyright 1993 Miguel de Icaza
 * Copyright 1996 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <windows.h>
#include <sys/time.h>
#include "tostring.hpp"
#include "debug.h"
#define kLogTag "profile"

static const unsigned char bom_utf8[] = { 0xEF, 0xBB, 0xBF };
static const unsigned char bom_utfle[] = { 0xFF, 0xFE };
static const unsigned char bom_utfbe[] = { 0xFE, 0xFF };

typedef enum
{
    ENCODING_ANSI = 1,
    ENCODING_UTF8,
    ENCODING_UTF16LE,
    ENCODING_UTF16BE
} ENCODING;

typedef struct tagPROFILEKEY
{
    WCHAR *value;
    struct tagPROFILEKEY *next;
    WCHAR name[1];
} PROFILEKEY;

typedef struct tagPROFILESECTION
{
    struct tagPROFILEKEY *key;
    struct tagPROFILESECTION *next;
    WCHAR name[1];
} PROFILESECTION;

typedef struct
{
    BOOL changed;
    PROFILESECTION *section;
    WCHAR *filename;
    FILETIME LastWriteTime;
    ENCODING encoding;
} PROFILE;

#define N_CACHED_PROFILES 10

/* Cached profile files */
static PROFILE *MRUProfile[N_CACHED_PROFILES] = { NULL };

#define CurProfile (MRUProfile[0])

/* Check for comments in profile */
#define IS_ENTRY_COMMENT(str) ((str)[0] == ';')

class CProfileCS : public CRITICAL_SECTION {
  public:
    CProfileCS()
    {
        InitializeCriticalSection(this);
    }
    ~CProfileCS()
    {
        DeleteCriticalSection(this);
    }
};
static CProfileCS PROFILE_CritSect;

static const char hex[] = "0123456789ABCDEF";

/***********************************************************************
 *           PROFILE_CopyEntry
 *
 * Copy the content of an entry into a buffer, removing quotes, and possibly
 * translating environment variables.
 */
static void PROFILE_CopyEntry(LPWSTR buffer, LPCWSTR value, int len)
{
    WCHAR quote = '\0';

    if (!buffer)
        return;

    if (*value == '\'' || *value == '\"')
    {
        if (value[1] && (value[lstrlenW(value) - 1] == *value))
            quote = *value++;
    }

    lstrcpynW(buffer, value, len);
    if (quote && (len >= lstrlenW(value)))
        buffer[lstrlenW(buffer) - 1] = '\0';
}

/* byte-swaps shorts in-place in a buffer. len is in WCHARs */
static inline USHORT RtlUshortByteSwap(USHORT &data)
{
    return ((data & 0xff) << 8) | ((data & 0xff00) >> 8);
}

static inline void PROFILE_ByteSwapShortBuffer(WCHAR *buffer, int len)
{
    int i;
    USHORT *shortbuffer = (USHORT *)buffer;
    for (i = 0; i < len; i++)
        shortbuffer[i] = RtlUshortByteSwap(shortbuffer[i]);
}

/* writes any necessary encoding marker to the file */
static inline void PROFILE_WriteMarker(HANDLE hFile, ENCODING encoding)
{
    DWORD dwBytesWritten;
    WCHAR bom;
    switch (encoding)
    {
    case ENCODING_ANSI:
        break;
    case ENCODING_UTF8:
        WriteFile(hFile, bom_utf8, sizeof(bom_utf8), &dwBytesWritten, NULL);
        break;
    case ENCODING_UTF16LE:
        bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &dwBytesWritten, NULL);
        break;
    case ENCODING_UTF16BE:
        bom = 0xFFFE;
        WriteFile(hFile, &bom, sizeof(bom), &dwBytesWritten, NULL);
        break;
    }
}

static void PROFILE_WriteLine(HANDLE hFile, WCHAR *szLine, int len, ENCODING encoding)
{
    char *write_buffer;
    int write_buffer_len;
    DWORD dwBytesWritten;

    switch (encoding)
    {
    case ENCODING_ANSI:
        write_buffer_len = WideCharToMultiByte(CP_ACP, 0, szLine, len, NULL, 0, NULL, NULL);
        write_buffer = (char *)HeapAlloc(GetProcessHeap(), 0, write_buffer_len);
        if (!write_buffer)
            return;
        len = WideCharToMultiByte(CP_ACP, 0, szLine, len, write_buffer, write_buffer_len, NULL, NULL);
        WriteFile(hFile, write_buffer, len, &dwBytesWritten, NULL);
        HeapFree(GetProcessHeap(), 0, write_buffer);
        break;
    case ENCODING_UTF8:
        write_buffer_len = WideCharToMultiByte(CP_UTF8, 0, szLine, len, NULL, 0, NULL, NULL);
        write_buffer = (char *)HeapAlloc(GetProcessHeap(), 0, write_buffer_len);
        if (!write_buffer)
            return;
        len = WideCharToMultiByte(CP_UTF8, 0, szLine, len, write_buffer, write_buffer_len, NULL, NULL);
        WriteFile(hFile, write_buffer, len, &dwBytesWritten, NULL);
        HeapFree(GetProcessHeap(), 0, write_buffer);
        break;
    case ENCODING_UTF16LE:
        WriteFile(hFile, szLine, len * sizeof(WCHAR), &dwBytesWritten, NULL);
        break;
    case ENCODING_UTF16BE:
        PROFILE_ByteSwapShortBuffer(szLine, len);
        WriteFile(hFile, szLine, len * sizeof(WCHAR), &dwBytesWritten, NULL);
        break;
    default:
        FIXME("encoding type %d not implemented\n", encoding);
    }
}

/***********************************************************************
 *           PROFILE_Save
 *
 * Save a profile tree to a file.
 */
static void PROFILE_Save(HANDLE hFile, const PROFILESECTION *section, ENCODING encoding)
{
    PROFILEKEY *key;
    WCHAR *buffer, *p;

    PROFILE_WriteMarker(hFile, encoding);

    for (; section; section = section->next)
    {
        int len = 0;

        if (section->name[0])
            len += lstrlenW(section->name) + 4;

        for (key = section->key; key; key = key->next)
        {
            len += lstrlenW(key->name) + 2;
            if (key->value)
                len += lstrlenW(key->value) + 1;
        }

        buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if (!buffer)
            return;

        p = buffer;
        if (section->name[0])
        {
            *p++ = '[';
            lstrcpyW(p, section->name);
            p += lstrlenW(p);
            *p++ = ']';
            *p++ = '\r';
            *p++ = '\n';
        }

        for (key = section->key; key; key = key->next)
        {
            lstrcpyW(p, key->name);
            p += lstrlenW(p);
            if (key->value)
            {
                *p++ = '=';
                lstrcpyW(p, key->value);
                p += lstrlenW(p);
            }
            *p++ = '\r';
            *p++ = '\n';
        }
        PROFILE_WriteLine(hFile, buffer, len, encoding);
        HeapFree(GetProcessHeap(), 0, buffer);
    }
}

/***********************************************************************
 *           PROFILE_Free
 *
 * Free a profile tree.
 */
static void PROFILE_Free(PROFILESECTION *section)
{
    PROFILESECTION *next_section;
    PROFILEKEY *key, *next_key;

    for (; section; section = next_section)
    {
        for (key = section->key; key; key = next_key)
        {
            next_key = key->next;
            HeapFree(GetProcessHeap(), 0, key->value);
            HeapFree(GetProcessHeap(), 0, key);
        }
        next_section = section->next;
        HeapFree(GetProcessHeap(), 0, section);
    }
}

/* returns TRUE if a whitespace character, else FALSE */
static inline BOOL PROFILE_isspaceW(WCHAR c)
{
    /* ^Z (DOS EOF) is a space too  (found on CD-ROMs) */
    return (c >= 0x09 && c <= 0x0d) || c == 0x1a || c == 0x20;
}

static inline ENCODING PROFILE_DetectTextEncoding(const void *buffer, int *len)
{
    if (*len >= sizeof(bom_utf8) && !memcmp(buffer, bom_utf8, sizeof(bom_utf8)))
    {
        *len = sizeof(bom_utf8);
        return ENCODING_UTF8;
    }
    if (*len >= sizeof(bom_utfle) && !memcmp(buffer, bom_utfle, sizeof(bom_utfle)))
    {
        *len = sizeof(bom_utfle);
        return ENCODING_UTF16LE;
    }
    if (*len >= sizeof(bom_utfbe) && !memcmp(buffer, bom_utfbe, sizeof(bom_utfbe)))
    {
        *len = sizeof(bom_utfbe);
        return ENCODING_UTF16BE;
    }
    *len = 0;
    return ENCODING_ANSI;
}

/***********************************************************************
 *           PROFILE_Load
 *
 * Load a profile tree from a file.
 */
static PROFILESECTION *PROFILE_Load(HANDLE hFile, ENCODING *pEncoding)
{
    char *buffer_base, *pBuffer;
    WCHAR *szFile;
    const WCHAR *szLineStart, *szLineEnd;
    const WCHAR *szValueStart, *szEnd, *next_line;
    int len;
    PROFILESECTION *section, *first_section;
    PROFILESECTION **next_section;
    PROFILEKEY *key, *prev_key, **next_key;
    DWORD dwFileSize;

    TRACE("%p\n", hFile);

    dwFileSize = GetFileSize(hFile, NULL);
    if (dwFileSize == INVALID_FILE_SIZE || dwFileSize == 0)
        return NULL;

    buffer_base = (char *)HeapAlloc(GetProcessHeap(), 0, dwFileSize);
    if (!buffer_base)
        return NULL;

    if (!ReadFile(hFile, buffer_base, dwFileSize, &dwFileSize, NULL))
    {
        HeapFree(GetProcessHeap(), 0, buffer_base);
        WARN("Error %d reading file\n", GetLastError());
        return NULL;
    }
    len = dwFileSize;
    *pEncoding = PROFILE_DetectTextEncoding(buffer_base, &len);
    /* len is set to the number of bytes in the character marker.
     * we want to skip these bytes */
    pBuffer = (char *)buffer_base + len;
    dwFileSize -= len;
    switch (*pEncoding)
    {
    case ENCODING_ANSI:
        TRACE("ANSI encoding\n");

        len = MultiByteToWideChar(CP_ACP, 0, pBuffer, dwFileSize, NULL, 0);
        szFile = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if (!szFile)
        {
            HeapFree(GetProcessHeap(), 0, buffer_base);
            return NULL;
        }
        MultiByteToWideChar(CP_ACP, 0, pBuffer, dwFileSize, szFile, len);
        szEnd = szFile + len;
        break;
    case ENCODING_UTF8:
        TRACE("UTF8 encoding\n");

        len = MultiByteToWideChar(CP_UTF8, 0, pBuffer, dwFileSize, NULL, 0);
        szFile = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
        if (!szFile)
        {
            HeapFree(GetProcessHeap(), 0, buffer_base);
            return NULL;
        }
        MultiByteToWideChar(CP_UTF8, 0, pBuffer, dwFileSize, szFile, len);
        szEnd = szFile + len;
        break;
    case ENCODING_UTF16LE:
        TRACE("UTF16 Little Endian encoding\n");
        szFile = (WCHAR *)pBuffer;
        szEnd = (WCHAR *)((char *)pBuffer + dwFileSize);
        break;
    case ENCODING_UTF16BE:
        TRACE("UTF16 Big Endian encoding\n");
        szFile = (WCHAR *)pBuffer;
        szEnd = (WCHAR *)((char *)pBuffer + dwFileSize);
        PROFILE_ByteSwapShortBuffer(szFile, dwFileSize / sizeof(WCHAR));
        break;
    default:
        FIXME("encoding type %d not implemented\n", *pEncoding);
        HeapFree(GetProcessHeap(), 0, buffer_base);
        return NULL;
    }

    first_section = (PROFILESECTION *)HeapAlloc(GetProcessHeap(), 0, sizeof(*section));
    if (first_section == NULL)
    {
        if (szFile != (WCHAR *)pBuffer)
            HeapFree(GetProcessHeap(), 0, szFile);
        HeapFree(GetProcessHeap(), 0, buffer_base);
        return NULL;
    }
    first_section->name[0] = 0;
    first_section->key = NULL;
    first_section->next = NULL;
    next_section = &first_section->next;
    next_key = &first_section->key;
    prev_key = NULL;
    next_line = szFile;

    while (next_line < szEnd)
    {
        szLineStart = next_line;
        while (next_line < szEnd && *next_line != '\n' && *next_line != '\r')
            next_line++;
        while (next_line < szEnd && (*next_line == '\n' || *next_line == '\r'))
            next_line++;
        szLineEnd = next_line;

        /* get rid of white space */
        while (szLineStart < szLineEnd && PROFILE_isspaceW(*szLineStart))
            szLineStart++;
        while ((szLineEnd > szLineStart) && PROFILE_isspaceW(szLineEnd[-1]))
            szLineEnd--;

        if (szLineStart >= szLineEnd)
            continue;

        if (*szLineStart == '[') /* section start */
        {
            for (len = szLineEnd - szLineStart; len > 0; len--)
                if (szLineStart[len - 1] == ']')
                    break;
            if (!len)
            {
                SLOG_STMW() << "Invalid section header:" << szLineStart;
            }
            else
            {
                szLineStart++;
                len -= 2;
                /* no need to allocate +1 for NULL terminating character as
                 * already included in structure */
                if (!(section = (PROFILESECTION *)HeapAlloc(GetProcessHeap(), 0, sizeof(*section) + len * sizeof(WCHAR))))
                    break;
                memcpy(section->name, szLineStart, len * sizeof(WCHAR));
                section->name[len] = '\0';
                section->key = NULL;
                section->next = NULL;
                *next_section = section;
                next_section = &section->next;
                next_key = &section->key;
                prev_key = NULL;

                continue;
            }
        }

        /* get rid of white space after the name and before the start
         * of the value */
        len = szLineEnd - szLineStart;
        for (szValueStart = szLineStart; szValueStart < szLineEnd; szValueStart++)
            if (*szValueStart == '=')
                break;
        if (szValueStart < szLineEnd)
        {
            const WCHAR *szNameEnd = szValueStart;
            while ((szNameEnd > szLineStart) && PROFILE_isspaceW(szNameEnd[-1]))
                szNameEnd--;
            len = szNameEnd - szLineStart;
            szValueStart++;
            while (szValueStart < szLineEnd && PROFILE_isspaceW(*szValueStart))
                szValueStart++;
        }
        else
            szValueStart = NULL;

        if (len || !prev_key || *prev_key->name)
        {
            /* no need to allocate +1 for NULL terminating character as
             * already included in structure */
            if (!(key = (PROFILEKEY *)HeapAlloc(GetProcessHeap(), 0, sizeof(*key) + len * sizeof(WCHAR))))
                break;
            memcpy(key->name, szLineStart, len * sizeof(WCHAR));
            key->name[len] = '\0';
            if (szValueStart)
            {
                len = (int)(szLineEnd - szValueStart);
                key->value = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
                memcpy(key->value, szValueStart, len * sizeof(WCHAR));
                key->value[len] = '\0';
            }
            else
                key->value = NULL;

            key->next = NULL;
            *next_key = key;
            next_key = &key->next;
            prev_key = key;
        }
    }
    if (szFile != (WCHAR *)pBuffer)
        HeapFree(GetProcessHeap(), 0, szFile);
    HeapFree(GetProcessHeap(), 0, buffer_base);
    return first_section;
}

/***********************************************************************
 *           PROFILE_DeleteKey
 *
 * Delete a key from a profile tree.
 */
static BOOL PROFILE_DeleteKey(PROFILESECTION **section, LPCWSTR section_name, LPCWSTR key_name)
{
    while (*section)
    {
        if (!wcsicmp((*section)->name, section_name))
        {
            PROFILEKEY **key = &(*section)->key;
            while (*key)
            {
                if (!wcsicmp((*key)->name, key_name))
                {
                    PROFILEKEY *to_del = *key;
                    *key = to_del->next;
                    HeapFree(GetProcessHeap(), 0, to_del->value);
                    HeapFree(GetProcessHeap(), 0, to_del);
                    return TRUE;
                }
                key = &(*key)->next;
            }
        }
        section = &(*section)->next;
    }
    return FALSE;
}

/***********************************************************************
 *           PROFILE_DeleteAllKeys
 *
 * Delete all keys from a profile tree.
 */
static void PROFILE_DeleteAllKeys(LPCWSTR section_name)
{
    PROFILESECTION **section = &CurProfile->section;
    while (*section)
    {
        if (!wcsicmp((*section)->name, section_name))
        {
            PROFILEKEY **key = &(*section)->key;
            while (*key)
            {
                PROFILEKEY *to_del = *key;
                *key = to_del->next;
                HeapFree(GetProcessHeap(), 0, to_del->value);
                HeapFree(GetProcessHeap(), 0, to_del);
                CurProfile->changed = TRUE;
            }
        }
        section = &(*section)->next;
    }
}

/***********************************************************************
 *           PROFILE_Find
 *
 * Find a key in a profile tree, optionally creating it.
 */
static PROFILEKEY *PROFILE_Find(PROFILESECTION **section, LPCWSTR section_name, LPCWSTR key_name, BOOL create, BOOL create_always)
{
    LPCWSTR p;
    int seclen = 0, keylen = 0;

    while (PROFILE_isspaceW(*section_name))
        section_name++;
    if (*section_name)
    {
        p = section_name + lstrlenW(section_name) - 1;
        while ((p > section_name) && PROFILE_isspaceW(*p))
            p--;
        seclen = p - section_name + 1;
    }

    while (PROFILE_isspaceW(*key_name))
        key_name++;
    if (*key_name)
    {
        p = key_name + lstrlenW(key_name) - 1;
        while ((p > key_name) && PROFILE_isspaceW(*p))
            p--;
        keylen = p - key_name + 1;
    }

    while (*section)
    {
        if (!wcsnicmp((*section)->name, section_name, seclen) && ((*section)->name)[seclen] == '\0')
        {
            PROFILEKEY **key = &(*section)->key;

            while (*key)
            {
                /* If create_always is FALSE then we check if the keyname
                 * already exists. Otherwise we add it regardless of its
                 * existence, to allow keys to be added more than once in
                 * some cases.
                 */
                if (!create_always)
                {
                    if ((!(wcsnicmp((*key)->name, key_name, keylen))) && (((*key)->name)[keylen] == '\0'))
                        return *key;
                }
                key = &(*key)->next;
            }
            if (!create)
                return NULL;
            if (!(*key = (PROFILEKEY *)HeapAlloc(GetProcessHeap(), 0, sizeof(PROFILEKEY) + lstrlenW(key_name) * sizeof(WCHAR))))
                return NULL;
            lstrcpyW((*key)->name, key_name);
            (*key)->value = NULL;
            (*key)->next = NULL;
            return *key;
        }
        section = &(*section)->next;
    }
    if (!create)
        return NULL;
    *section = (PROFILESECTION *)HeapAlloc(GetProcessHeap(), 0, sizeof(PROFILESECTION) + lstrlenW(section_name) * sizeof(WCHAR));
    if (*section == NULL)
        return NULL;
    lstrcpyW((*section)->name, section_name);
    (*section)->next = NULL;
    if (!((*section)->key = (PROFILEKEY *)HeapAlloc(GetProcessHeap(), 0, sizeof(PROFILEKEY) + lstrlenW(key_name) * sizeof(WCHAR))))
    {
        HeapFree(GetProcessHeap(), 0, *section);
        return NULL;
    }
    lstrcpyW((*section)->key->name, key_name);
    (*section)->key->value = NULL;
    (*section)->key->next = NULL;
    return (*section)->key;
}

/***********************************************************************
 *           PROFILE_FlushFile
 *
 * Flush the current profile to disk if changed.
 */
static BOOL PROFILE_FlushFile(void)
{
    HANDLE hFile = NULL;
    FILETIME LastWriteTime;

    if (!CurProfile)
    {
        WARN("No current profile!\n");
        return FALSE;
    }

    if (!CurProfile->changed)
        return TRUE;

    hFile = CreateFileW(CurProfile->filename, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        SLOG_STMI() << "could not save profile file " << CurProfile->filename << " error was " << GetLastError();
        return FALSE;
    }
    SLOG_STMD() << "Saving " << CurProfile->filename;

    PROFILE_Save(hFile, CurProfile->section, CurProfile->encoding);
    if (GetFileTime(hFile, NULL, NULL, &LastWriteTime))
        CurProfile->LastWriteTime = LastWriteTime;
    CloseHandle(hFile);
    CurProfile->changed = FALSE;
    return TRUE;
}

/***********************************************************************
 *           PROFILE_ReleaseFile
 *
 * Flush the current profile to disk and remove it from the cache.
 */
static void PROFILE_ReleaseFile(void)
{
    PROFILE_FlushFile();
    PROFILE_Free(CurProfile->section);
    HeapFree(GetProcessHeap(), 0, CurProfile->filename);
    CurProfile->changed = FALSE;
    CurProfile->section = NULL;
    CurProfile->filename = NULL;
    CurProfile->encoding = ENCODING_ANSI;
    ZeroMemory(&CurProfile->LastWriteTime, sizeof(CurProfile->LastWriteTime));
}

#define TICKSPERSEC       10000000
#define SECS_1601_TO_1970 ((369 * 365 + 89) * (ULONGLONG)86400)

static inline ULONGLONG ticks_from_time_t(time_t time)
{
    if (sizeof(time_t) == sizeof(int)) /* time_t may be signed */
        return ((ULONGLONG)(ULONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
    else
        return ((ULONGLONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
}

static void NtQuerySystemTime(LARGE_INTEGER *time)
{
    struct timeval now;
    gettimeofday(&now, 0);
    time->QuadPart = ticks_from_time_t(now.tv_sec) + now.tv_usec * 10;
}
/***********************************************************************
 *
 * Compares a file time with the current time. If the file time is
 * at least 2.1 seconds in the past, return true.
 *
 * Intended as cache safety measure: The time resolution on FAT is
 * two seconds, so files that are not at least two seconds old might
 * keep their time even on modification, so don't cache them.
 */
static BOOL is_not_current(FILETIME *ft)
{
    LARGE_INTEGER now;
    LONGLONG ftll;
    NtQuerySystemTime(&now);
    ftll = ((LONGLONG)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
    SLOG_STMD() << ftll << ";" << now.QuadPart;
    return ftll + 21000000 < now.QuadPart;
}

/***********************************************************************
 *           PROFILE_Open
 *
 * Open a profile file, checking the cached file first.
 */
static BOOL PROFILE_Open(LPCWSTR filename, BOOL write_access)
{
    WCHAR buffer[MAX_PATH];
    HANDLE hFile = INVALID_HANDLE_VALUE;
    FILETIME LastWriteTime;
    int i, j;
    PROFILE *tempProfile;

    ZeroMemory(&LastWriteTime, sizeof(LastWriteTime));

    /* First time around */

    if (!CurProfile)
        for (i = 0; i < N_CACHED_PROFILES; i++)
        {
            MRUProfile[i] = (PROFILE *)HeapAlloc(GetProcessHeap(), 0, sizeof(PROFILE));
            if (MRUProfile[i] == NULL)
                break;
            MRUProfile[i]->changed = FALSE;
            MRUProfile[i]->section = NULL;
            MRUProfile[i]->filename = NULL;
            MRUProfile[i]->encoding = ENCODING_ANSI;
            ZeroMemory(&MRUProfile[i]->LastWriteTime, sizeof(FILETIME));
        }

    if (!filename)
        filename = L"win.ini";

    GetFullPathNameW(filename, ARRAYSIZE(buffer), buffer, NULL);
    hFile = CreateFileW(buffer, GENERIC_READ | (write_access ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        //        SLOG_STMW() << "Error " << GetLastError() << " opening file " << filename;
        return FALSE;
    }

    for (i = 0; i < N_CACHED_PROFILES; i++)
    {
        if ((MRUProfile[i]->filename && !wcsicmp(buffer, MRUProfile[i]->filename)))
        {
            if (i)
            {
                PROFILE_FlushFile();
                tempProfile = MRUProfile[i];
                for (j = i; j > 0; j--)
                    MRUProfile[j] = MRUProfile[j - 1];
                CurProfile = tempProfile;
            }

            if (hFile != INVALID_HANDLE_VALUE)
            {
                GetFileTime(hFile, NULL, NULL, &LastWriteTime);
                if (!memcmp(&CurProfile->LastWriteTime, &LastWriteTime, sizeof(FILETIME)) && is_not_current(&LastWriteTime))
                {
                    SLOG_STMD() << buffer << "already opened (mru" << i << ")";
                }
                else
                {
                    PROFILE_Free(CurProfile->section);
                    CurProfile->section = PROFILE_Load(hFile, &CurProfile->encoding);
                    CurProfile->LastWriteTime = LastWriteTime;
                }
                CloseHandle(hFile);
                return TRUE;
            }
        }
    }

    /* Flush the old current profile */
    PROFILE_FlushFile();

    /* Make the oldest profile the current one only in order to get rid of it */
    if (i == N_CACHED_PROFILES)
    {
        tempProfile = MRUProfile[N_CACHED_PROFILES - 1];
        for (i = N_CACHED_PROFILES - 1; i > 0; i--)
            MRUProfile[i] = MRUProfile[i - 1];
        CurProfile = tempProfile;
    }
    if (CurProfile->filename)
        PROFILE_ReleaseFile();

    /* OK, now that CurProfile is definitely free we assign it our new file */
    CurProfile->filename = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(buffer) + 1) * sizeof(WCHAR));
    lstrcpyW(CurProfile->filename, buffer);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        CurProfile->section = PROFILE_Load(hFile, &CurProfile->encoding);
        GetFileTime(hFile, NULL, NULL, &CurProfile->LastWriteTime);
        CloseHandle(hFile);
    }
    else
    {
        /* Does not exist yet, we will create it in PROFILE_FlushFile */
        SLOG_STMW() << "profile file " << buffer << " not found";
    }
    return TRUE;
}

/***********************************************************************
 *           PROFILE_GetSection
 *
 * Returns all keys of a section.
 * If return_values is TRUE, also include the corresponding values.
 */
static INT PROFILE_GetSection(const WCHAR *filename, LPCWSTR section_name, LPWSTR buffer, UINT len, BOOL return_values)
{
    PROFILESECTION *section;
    PROFILEKEY *key;

    if (!buffer)
        return 0;

    EnterCriticalSection(&PROFILE_CritSect);

    if (!PROFILE_Open(filename, FALSE))
    {
        LeaveCriticalSection(&PROFILE_CritSect);
        buffer[0] = 0;
        return 0;
    }

    for (section = CurProfile->section; section; section = section->next)
    {
        if (!wcsicmp(section->name, section_name))
        {
            UINT oldlen = len;
            for (key = section->key; key; key = key->next)
            {
                if (len <= 2)
                    break;
                if (!*key->name && !key->value)
                    continue; /* Skip empty lines */
                if (IS_ENTRY_COMMENT(key->name))
                    continue; /* Skip comments */
                if (!return_values && !key->value)
                    continue; /* Skip lines w.o. '=' */
                lstrcpynW(buffer, key->name, len - 1);
                len -= lstrlenW(buffer) + 1;
                buffer += lstrlenW(buffer) + 1;
                if (len < 2)
                    break;
                if (return_values && key->value)
                {
                    buffer[-1] = '=';
                    lstrcpynW(buffer, key->value, len - 1);
                    len -= lstrlenW(buffer) + 1;
                    buffer += lstrlenW(buffer) + 1;
                }
            }
            *buffer = '\0';

            LeaveCriticalSection(&PROFILE_CritSect);

            if (len <= 1)
            /*If either lpszSection or lpszKey is NULL and the supplied
              destination buffer is too small to hold all the strings,
              the last string is truncated and followed by two null characters.
              In this case, the return value is equal to cchReturnBuffer
              minus two. */
            {
                buffer[-1] = '\0';
                return oldlen - 2;
            }
            return oldlen - len;
        }
    }
    buffer[0] = buffer[1] = '\0';

    LeaveCriticalSection(&PROFILE_CritSect);

    return 0;
}

static BOOL PROFILE_DeleteSection(const WCHAR *filename, const WCHAR *name)
{
    PROFILESECTION **section;

    EnterCriticalSection(&PROFILE_CritSect);

    if (!PROFILE_Open(filename, TRUE))
    {
        LeaveCriticalSection(&PROFILE_CritSect);
        return FALSE;
    }

    for (section = &CurProfile->section; *section; section = &(*section)->next)
    {
        if (!wcsicmp((*section)->name, name))
        {
            PROFILESECTION *to_del = *section;
            *section = to_del->next;
            to_del->next = NULL;
            PROFILE_Free(to_del);
            CurProfile->changed = TRUE;
            PROFILE_FlushFile();
            break;
        }
    }

    LeaveCriticalSection(&PROFILE_CritSect);
    return TRUE;
}

/* See GetPrivateProfileSectionNamesA for documentation */
static INT PROFILE_GetSectionNames(LPWSTR buffer, UINT len)
{
    LPWSTR buf;
    UINT buflen, tmplen;
    PROFILESECTION *section;

    TRACE("(%p, %d)\n", buffer, len);

    if (!buffer || !len)
        return 0;
    if (len == 1)
    {
        *buffer = '\0';
        return 0;
    }

    buflen = len - 1;
    buf = buffer;
    section = CurProfile->section;
    while ((section != NULL))
    {
        if (section->name[0])
        {
            tmplen = lstrlenW(section->name) + 1;
            if (tmplen >= buflen)
            {
                if (buflen > 0)
                {
                    memcpy(buf, section->name, (buflen - 1) * sizeof(WCHAR));
                    buf += buflen - 1;
                    *buf++ = '\0';
                }
                *buf = '\0';
                return len - 2;
            }
            memcpy(buf, section->name, tmplen * sizeof(WCHAR));
            buf += tmplen;
            buflen -= tmplen;
        }
        section = section->next;
    }
    *buf = '\0';
    return buf - buffer;
}

/***********************************************************************
 *           PROFILE_SetString
 *
 * Set a profile string.
 */
static BOOL PROFILE_SetString(LPCWSTR section_name, LPCWSTR key_name, LPCWSTR value, BOOL create_always)
{
    if (!value) /* Delete a key */
    {
        CurProfile->changed |= PROFILE_DeleteKey(&CurProfile->section, section_name, key_name);
        return TRUE; /* same error handling as above */
    }
    else /* Set the key value */
    {
        PROFILEKEY *key = PROFILE_Find(&CurProfile->section, section_name, key_name, TRUE, create_always);
        if (!key)
            return FALSE;

        /* strip the leading spaces. We can safely strip \n\r and
         * friends too, they should not happen here anyway. */
        while (PROFILE_isspaceW(*value))
            value++;

        if (key->value)
        {
            if (!wcscmp(key->value, value))
            {
                TRACE("  no change needed\n");
                return TRUE; /* No change needed */
            }
            HeapFree(GetProcessHeap(), 0, key->value);
        }
        else
            TRACE("  creating key\n");
        key->value = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(value) + 1) * sizeof(WCHAR));
        lstrcpyW(key->value, value);
        CurProfile->changed = TRUE;
    }
    return TRUE;
}

/********************* API functions **********************************/

/***********************************************************************
 *           GetProfileIntA   (KERNEL32.@)
 */
UINT WINAPI GetProfileIntA(LPCSTR section, LPCSTR entry, INT def_val)
{
    return GetPrivateProfileIntA(section, entry, def_val, "win.ini");
}

/***********************************************************************
 *           GetProfileIntW   (KERNEL32.@)
 */
UINT WINAPI GetProfileIntW(LPCWSTR section, LPCWSTR entry, INT def_val)
{
    return GetPrivateProfileIntW(section, entry, def_val, L"win.ini");
}

static DWORD get_section(const WCHAR *filename, const WCHAR *section, WCHAR *buffer, DWORD size, BOOL return_values)
{
    return PROFILE_GetSection(filename, section, buffer, size, return_values);
}

/***********************************************************************
 *           GetPrivateProfileStringW   (KERNEL32.@)
 */
INT WINAPI GetPrivateProfileStringW(LPCWSTR section, LPCWSTR entry, LPCWSTR def_val, LPWSTR buffer, UINT len, LPCWSTR filename)
{
    int ret;
    LPWSTR defval_tmp = NULL;
    const WCHAR *p;

    if (!buffer || !len)
        return 0;
    if (!def_val)
        def_val = L"";
    if (!section)
        return GetPrivateProfileSectionNamesW(buffer, len, filename);
    if (!entry)
    {
        ret = get_section(filename, section, buffer, len, FALSE);
        if (!buffer[0])
        {
            PROFILE_CopyEntry(buffer, def_val, len);
            ret = lstrlenW(buffer);
        }
        return ret;
    }

    /* strip any trailing ' ' of def_val. */
    p = def_val + lstrlenW(def_val) - 1;

    while (p > def_val && *p == ' ')
        p--;

    if (p >= def_val)
    {
        int vlen = (int)(p - def_val) + 1;

        defval_tmp = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (vlen + 1) * sizeof(WCHAR));
        memcpy(defval_tmp, def_val, vlen * sizeof(WCHAR));
        defval_tmp[vlen] = '\0';
        def_val = defval_tmp;
    }

    {
        EnterCriticalSection(&PROFILE_CritSect);

        if (PROFILE_Open(filename, FALSE))
        {
            PROFILEKEY *key = PROFILE_Find(&CurProfile->section, section, entry, FALSE, FALSE);
            PROFILE_CopyEntry(buffer, (key && key->value) ? key->value : def_val, len);
            ret = lstrlenW(buffer);
        }
        else
        {
            lstrcpynW(buffer, def_val, len);
            ret = lstrlenW(buffer);
        }

        LeaveCriticalSection(&PROFILE_CritSect);
    }

    HeapFree(GetProcessHeap(), 0, defval_tmp);

    return ret;
}

/***********************************************************************
 *           GetPrivateProfileStringA   (KERNEL32.@)
 */
INT WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR entry, LPCSTR def_val, LPSTR buffer, UINT len, LPCSTR filename)
{
    std::wstring section_tmp, entry_tmp, defval_tmp, filname_tmp;
    if (towstring(section, -1, section_tmp) && towstring(entry, -1, entry_tmp) && towstring(def_val, -1, defval_tmp) && towstring(filename, -1, filname_tmp))
    {
        wchar_t *buffer_tmp = new wchar_t[len];
        INT ret = GetPrivateProfileStringW(section_tmp.c_str(), entry_tmp.c_str(), defval_tmp.c_str(), buffer_tmp, len, filname_tmp.c_str());
        if (ret)
        {
            ret = WideCharToMultiByte(CP_UTF8, 0, buffer_tmp, ret, buffer, len, NULL, NULL);
        }
        delete[] buffer_tmp;
        return ret;
    }
    return 0;
}

/***********************************************************************
 *           GetProfileStringA   (KERNEL32.@)
 */
INT WINAPI GetProfileStringA(LPCSTR section, LPCSTR entry, LPCSTR def_val, LPSTR buffer, UINT len)
{
    return GetPrivateProfileStringA(section, entry, def_val, buffer, len, "win.ini");
}

/***********************************************************************
 *           GetProfileStringW   (KERNEL32.@)
 */
INT WINAPI GetProfileStringW(LPCWSTR section, LPCWSTR entry, LPCWSTR def_val, LPWSTR buffer, UINT len)
{
    return GetPrivateProfileStringW(section, entry, def_val, buffer, len, L"win.ini");
}

/***********************************************************************
 *           WriteProfileStringA   (KERNEL32.@)
 */
BOOL WINAPI WriteProfileStringA(LPCSTR section, LPCSTR entry, LPCSTR string)
{
    return WritePrivateProfileStringA(section, entry, string, "win.ini");
}

/***********************************************************************
 *           WriteProfileStringW   (KERNEL32.@)
 */
BOOL WINAPI WriteProfileStringW(LPCWSTR section, LPCWSTR entry, LPCWSTR string)
{
    return WritePrivateProfileStringW(section, entry, string, L"win.ini");
}

/***********************************************************************
 *           GetPrivateProfileIntW   (KERNEL32.@)
 */
UINT WINAPI GetPrivateProfileIntW(LPCWSTR section, LPCWSTR entry, INT def_val, LPCWSTR filename)
{
    WCHAR buffer[30] = { 0 };
    ULONG result;

    if (GetPrivateProfileStringW(section, entry, L"", buffer, ARRAYSIZE(buffer), filename) == 0)
        return def_val;
    if (!buffer[0])
        return (UINT)def_val;
    result = wcstoul(buffer, NULL, 10);
    return result;
}

/***********************************************************************
 *           GetPrivateProfileIntA   (KERNEL32.@)
 */
UINT WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR entry, INT def_val, LPCSTR filename)
{
    std::wstring section_tmp, entry_tmp, defval_tmp, filname_tmp;
    if (towstring(section, -1, section_tmp) && towstring(entry, -1, entry_tmp) && towstring(filename, -1, filname_tmp))
    {
        return GetPrivateProfileIntW(section_tmp.c_str(), entry_tmp.c_str(), def_val, filname_tmp.c_str());
    }
    return def_val;
}

/***********************************************************************
 *           GetPrivateProfileSectionW   (KERNEL32.@)
 */
INT WINAPI GetPrivateProfileSectionW(LPCWSTR section, LPWSTR buffer, DWORD len, LPCWSTR filename)
{
    if (!section || !buffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    return get_section(filename, section, buffer, len, TRUE);
}

/***********************************************************************
 *           GetPrivateProfileSectionA   (KERNEL32.@)
 */
INT WINAPI GetPrivateProfileSectionA(LPCSTR section, LPSTR buffer, DWORD len, LPCSTR filename)
{
    std::wstring section_tmp, filname_tmp;
    if (towstring(section, -1, section_tmp) && towstring(filename, -1, filname_tmp))
    {
        WCHAR *buffer_tmp = new wchar_t[len];
        int ret = GetPrivateProfileSectionW(section_tmp.c_str(), buffer_tmp, len, filname_tmp.c_str());
        if (ret)
        {
            ret = WideCharToMultiByte(CP_UTF8, 0, buffer_tmp, ret, buffer, len, NULL, NULL);
        }
        delete[] buffer_tmp;
        return ret;
    }
    return 0;
}

/***********************************************************************
 *           GetProfileSectionA   (KERNEL32.@)
 */
INT WINAPI GetProfileSectionA(LPCSTR section, LPSTR buffer, DWORD len)
{
    return GetPrivateProfileSectionA(section, buffer, len, "win.ini");
}

/***********************************************************************
 *           GetProfileSectionW   (KERNEL32.@)
 */
INT WINAPI GetProfileSectionW(LPCWSTR section, LPWSTR buffer, DWORD len)
{
    return GetPrivateProfileSectionW(section, buffer, len, L"win.ini");
}

/***********************************************************************
 *           WritePrivateProfileStringW   (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileStringW(LPCWSTR section, LPCWSTR entry, LPCWSTR string, LPCWSTR filename)
{
    BOOL ret = FALSE;
    EnterCriticalSection(&PROFILE_CritSect);

    if (PROFILE_Open(filename, TRUE))
    {
        if (!section)
            SetLastError(ERROR_FILE_NOT_FOUND);
        else
            ret = PROFILE_SetString(section, entry, string, FALSE);
        if (ret)
            ret = PROFILE_FlushFile();
    }

    LeaveCriticalSection(&PROFILE_CritSect);
    return ret;
}

/***********************************************************************
 *           WritePrivateProfileStringA   (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR entry, LPCSTR string, LPCSTR filename)
{
    std::wstring section_tmp, entry_tmp, string_tmp, filname_tmp;
    if (towstring(section, -1, section_tmp) && towstring(entry, -1, entry_tmp) && towstring(string, -1, string_tmp) && towstring(filename, -1, filname_tmp))
    {
        return WritePrivateProfileStringW(section_tmp.c_str(), entry_tmp.c_str(), string_tmp.c_str(), filname_tmp.c_str());
    }
    return FALSE;
}

/***********************************************************************
 *           WritePrivateProfileSectionW   (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR section, LPCWSTR string, LPCWSTR filename)
{
    BOOL ret = FALSE;
    EnterCriticalSection(&PROFILE_CritSect);

    if (PROFILE_Open(filename, TRUE))
    {
        PROFILE_DeleteAllKeys(section);
        ret = TRUE;
        LPWSTR p;
        while (*string && ret)
        {
            WCHAR *buf = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lstrlenW(string) + 1) * sizeof(WCHAR));
            lstrcpyW(buf, string);
            if ((p = wcschr(buf, '=')))
            {
                *p = '\0';
                ret = PROFILE_SetString(section, buf, p + 1, TRUE);
            }
            HeapFree(GetProcessHeap(), 0, buf);
            string += lstrlenW(string) + 1;
        }
        if (ret)
            ret = PROFILE_FlushFile();
    }

    LeaveCriticalSection(&PROFILE_CritSect);
    return ret;
}

/***********************************************************************
 *           WritePrivateProfileSectionA   (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileSectionA(LPCSTR section, LPCSTR string, LPCSTR filename)
{
    std::wstring section_tmp, string_tmp, filname_tmp;
    if (towstring(section, -1, section_tmp) && towstring(string, -1, string_tmp) && towstring(filename, -1, filname_tmp))
    {
        return WritePrivateProfileSectionW(section_tmp.c_str(), string_tmp.c_str(), filname_tmp.c_str());
    }
    return FALSE;
}

/***********************************************************************
 *           WriteProfileSectionA   (KERNEL32.@)
 */
BOOL WINAPI WriteProfileSectionA(LPCSTR section, LPCSTR keys_n_values)

{
    return WritePrivateProfileSectionA(section, keys_n_values, "win.ini");
}

/***********************************************************************
 *           WriteProfileSectionW   (KERNEL32.@)
 */
BOOL WINAPI WriteProfileSectionW(LPCWSTR section, LPCWSTR keys_n_values)
{
    return WritePrivateProfileSectionW(section, keys_n_values, L"win.ini");
}

/***********************************************************************
 *           GetPrivateProfileSectionNamesW  (KERNEL32.@)
 *
 * Returns the section names contained in the specified file.
 * FIXME: Where do we find this file when the path is relative?
 * The section names are returned as a list of strings with an extra
 * '\0' to mark the end of the list. Except for that the behavior
 * depends on the Windows version.
 *
 * Win95:
 * - if the buffer is 0 or 1 character long then it is as if it was of
 *   infinite length.
 * - otherwise, if the buffer is too small only the section names that fit
 *   are returned.
 * - note that this means if the buffer was too small to return even just
 *   the first section name then a single '\0' will be returned.
 * - the return value is the number of characters written in the buffer,
 *   except if the buffer was too small in which case len-2 is returned
 *
 * Win2000:
 * - if the buffer is 0, 1 or 2 characters long then it is filled with
 *   '\0' and the return value is 0
 * - otherwise if the buffer is too small then the first section name that
 *   does not fit is truncated so that the string list can be terminated
 *   correctly (double '\0')
 * - the return value is the number of characters written in the buffer
 *   except for the trailing '\0'. If the buffer is too small, then the
 *   return value is len-2
 * - Win2000 has a bug that triggers when the section names and the
 *   trailing '\0' fit exactly in the buffer. In that case the trailing
 *   '\0' is missing.
 *
 * Wine implements the observed Win2000 behavior (except for the bug).
 *
 * Note that when the buffer is big enough then the return value may be any
 * value between 1 and len-1 (or len in Win95), including len-2.
 */
DWORD WINAPI GetPrivateProfileSectionNamesW(LPWSTR buffer, DWORD size, LPCWSTR filename)
{
    DWORD ret = 0;
    EnterCriticalSection(&PROFILE_CritSect);

    if (PROFILE_Open(filename, FALSE))
        ret += PROFILE_GetSectionNames(buffer + ret, size - ret);

    LeaveCriticalSection(&PROFILE_CritSect);

    return ret;
}

/***********************************************************************
 *           GetPrivateProfileSectionNamesA  (KERNEL32.@)
 */
DWORD WINAPI GetPrivateProfileSectionNamesA(LPSTR buffer, DWORD size, LPCSTR filename)
{
    std::wstring filename_tmp;
    if (towstring(filename, -1, filename_tmp))
    {
        wchar_t *tmp = new wchar_t[size];
        DWORD ret = GetPrivateProfileSectionNamesW(tmp, size, filename_tmp.c_str());
        if (ret)
        {
            ret = WideCharToMultiByte(CP_UTF8, 0, tmp, ret, buffer, size, NULL, NULL);
        }
        delete[] tmp;
        return ret;
    }
    return 0;
}

static int get_hex_byte(const WCHAR *p)
{
    int val;

    if (*p >= '0' && *p <= '9')
        val = *p - '0';
    else if (*p >= 'A' && *p <= 'Z')
        val = *p - 'A' + 10;
    else if (*p >= 'a' && *p <= 'z')
        val = *p - 'a' + 10;
    else
        return -1;
    val <<= 4;
    p++;
    if (*p >= '0' && *p <= '9')
        val += *p - '0';
    else if (*p >= 'A' && *p <= 'Z')
        val += *p - 'A' + 10;
    else if (*p >= 'a' && *p <= 'z')
        val += *p - 'a' + 10;
    else
        return -1;
    return val;
}

/***********************************************************************
 *           GetPrivateProfileStructW (KERNEL32.@)
 *
 * Should match Win95's behaviour pretty much
 */
BOOL WINAPI GetPrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID buf, UINT len, LPCWSTR filename)
{
    BOOL ret = FALSE;
    LPBYTE data = (LPBYTE)buf;
    BYTE chksum = 0;
    int val;
    WCHAR *p, *buffer;

    if (!(buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (2 * len + 3) * sizeof(WCHAR))))
        return FALSE;

    if (GetPrivateProfileStringW(section, key, NULL, buffer, 2 * len + 3, filename) != 2 * len + 2)
        goto done;

    for (p = buffer; len; p += 2, len--)
    {
        if ((val = get_hex_byte(p)) == -1)
            goto done;
        *data++ = val;
        chksum += val;
    }
    /* retrieve stored checksum value */
    if ((val = get_hex_byte(p)) == -1)
        goto done;
    ret = ((BYTE)val == chksum);

done:
    HeapFree(GetProcessHeap(), 0, buffer);
    return ret;
}

/***********************************************************************
 *           GetPrivateProfileStructA (KERNEL32.@)
 */
BOOL WINAPI GetPrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID buffer, UINT len, LPCSTR filename)
{
    std::wstring section_tmp, key_tmp, filename_tmp;
    if (towstring(section, -1, section_tmp) && towstring(key, -1, key_tmp) && towstring(filename, -1, filename_tmp))
    {
        return GetPrivateProfileStructW(section_tmp.c_str(), key_tmp.c_str(), buffer, len, filename_tmp.c_str());
    }
    return FALSE;
}

/***********************************************************************
 *           WritePrivateProfileStructW (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID buf, UINT bufsize, LPCWSTR filename)
{
    BOOL ret = FALSE;
    LPBYTE binbuf;
    LPWSTR outstring, p;
    DWORD sum = 0;

    if (!section && !key && !buf) /* flush the cache */
        return WritePrivateProfileStringW(NULL, NULL, NULL, filename);

    /* allocate string buffer for hex chars + checksum hex char + '\0' */
    outstring = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (bufsize * 2 + 2 + 1) * sizeof(WCHAR));
    p = outstring;
    for (binbuf = (LPBYTE)buf; binbuf < (LPBYTE)buf + bufsize; binbuf++)
    {
        *p++ = hex[*binbuf >> 4];
        *p++ = hex[*binbuf & 0xf];
        sum += *binbuf;
    }
    /* checksum is sum & 0xff */
    *p++ = hex[(sum & 0xf0) >> 4];
    *p++ = hex[sum & 0xf];
    *p++ = '\0';

    ret = WritePrivateProfileStringW(section, key, outstring, filename);
    HeapFree(GetProcessHeap(), 0, outstring);
    return ret;
}

/***********************************************************************
 *           WritePrivateProfileStructA (KERNEL32.@)
 */
BOOL WINAPI WritePrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID buf, UINT bufsize, LPCSTR filename)
{
    std::wstring section_tmp, key_tmp, filename_tmp;
    if (towstring(section, -1, section_tmp) && towstring(key, -1, key_tmp) && towstring(filename, -1, filename_tmp))
    {
        return WritePrivateProfileStructW(section_tmp.c_str(), key_tmp.c_str(), buf, bufsize, filename_tmp.c_str());
    }
    return FALSE;
}
