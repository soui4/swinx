#include <fileapi.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "handle.h"
#include "tostring.hpp"
#include "debug.h"
#define kLogTag "file"

struct _FileData
{
    int f;
    _FileData()
        : f(-1)
    {
    }
    ~_FileData()
    {
        if (f != -1)
        {
            close(f);
        }
    }
};

void FreeFileData(void *ptr)
{
    delete (_FileData *)ptr;
}

HANDLE
WINAPI
CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    _FileData *fd = new _FileData;
    int mode = O_RDONLY;
    if ((dwDesiredAccess & (FILE_READ_DATA | FILE_WRITE_DATA)) == (FILE_READ_DATA | FILE_WRITE_DATA))
        mode = O_RDWR;
    else if (dwDesiredAccess & FILE_WRITE_DATA)
        mode = O_WRONLY;
    if (!(dwShareMode & (FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE)))
        mode |= O_EXCL;
    if (dwCreationDisposition == CREATE_NEW)
    {
        mode |= O_CREAT;
    }
    else if (dwCreationDisposition == CREATE_ALWAYS)
    {
        struct stat st;
        if (-1 == stat(lpFileName, &st))
        {
            mode |= O_CREAT;
        }
    }
    else if (dwCreationDisposition == TRUNCATE_EXISTING)
        mode |= O_TRUNC;
    fd->f = open(lpFileName, mode, 0644);
    if (fd->f == -1)
    {
        delete fd;
        return INVALID_HANDLE_VALUE;
    }
    return InitHandle(FILE_OBJ, fd, FreeFileData);
}

HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    char szPath[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, szPath, MAX_PATH, nullptr, nullptr))
        return INVALID_HANDLE_VALUE;
    return CreateFileA(szPath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static _FileData *GetFD(HANDLE h)
{
    if (h->type == FILE_OBJ)
        return (_FileData *)h->ptr;
    return nullptr;
}

BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
    _FileData *fd = GetFD(hFile);
    if (!fd)
        return FALSE;
    lpFileSize->QuadPart = lseek(fd->f, 0, SEEK_CUR);
    return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    if (GetFileSizeEx(hFile, &sz))
    {
        if (lpFileSizeHigh)
            *lpFileSizeHigh = sz.HighPart;
        return sz.LowPart;
    }
    return 0;
}

static void TimeSpec2FileTime(const struct timespec &ts, LPFILETIME fts)
{
    if (!fts)
        return;
    uint64_t nsec = ts.tv_nsec;
    memcpy(fts, &nsec, sizeof(uint64_t));
}

BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
    if (_FileData *fd = GetFD(hFile))
    {
        struct timespec st_atim; /* Time of last access.  */
        struct timespec st_mtim; /* Time of last modification.  */
        struct timespec st_ctim; /* Time of last status change.  */

        struct stat64 st;
        if (0 != fstat64(fd->f, &st))
            return FALSE;
        TimeSpec2FileTime(st.st_ctim, lpCreationTime);
        TimeSpec2FileTime(st.st_mtim, lpLastWriteTime);
        TimeSpec2FileTime(st.st_atim, lpLastAccessTime);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    if (_FileData *fd = GetFD(hFile))
    {
        if (fd->f == -1)
            return FALSE;
        int readed = read(fd->f, lpBuffer, nNumberOfBytesToRead);
        if (lpNumberOfBytesRead)
            *lpNumberOfBytesRead = readed;
        return readed == nNumberOfBytesToRead;
    }
    return FALSE;
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    if (_FileData *fd = GetFD(hFile))
    {
        if (fd->f == -1)
            return FALSE;
        int writed = write(fd->f, lpBuffer, nNumberOfBytesToWrite);
        if (lpNumberOfBytesWritten)
            *lpNumberOfBytesWritten = writed;
        return writed == nNumberOfBytesToWrite;
    }
    return FALSE;
}

/* extend a file beyond the current end of file */
static int grow_file(int unix_fd, uint64_t new_size)
{
    static const char zero = 0;
    off_t size = new_size;

    if (sizeof(new_size) > sizeof(size) && size != new_size)
    {
        set_error(STATUS_INVALID_PARAMETER);
        return 0;
    }
    /* extend the file one byte beyond the requested size and then truncate it */
    /* this should work around ftruncate implementations that can't extend files */
    if (pwrite(unix_fd, &zero, 1, size) != -1)
    {
        ftruncate(unix_fd, size);
        return 1;
    }
    return 0;
}

static bool set_fd_eof(int fd, uint64_t eof)
{
    struct stat st;

    if (fd == -1)
    {
        // set_error(fd->no_fd_status);
        return false;
    }
    if (fstat(fd, &st) == -1)
    {
        // file_set_error();
        return false;
    }
    if (eof < st.st_size)
    {
        return -1 != ftruncate(fd, eof);
    }
    else
    {
        return grow_file(fd, eof);
    }
}

/***********************************************************************
 *	SetFilePointer   (kernelbase.@)
 */
DWORD WINAPI SetFilePointer(HANDLE file, LONG distance, LONG *highword, DWORD method)
{
    _FileData *fd = GetFD(file);
    if (!fd)
    {
        return 0;
    }
    LARGE_INTEGER dist, newpos;

    if (highword)
    {
        dist.u.LowPart = distance;
        dist.u.HighPart = *highword;
    }
    else
        dist.QuadPart = distance;

    newpos = dist;
    switch (method)
    {
    case FILE_BEGIN:
        newpos.QuadPart = dist.QuadPart;
        break;
    case FILE_CURRENT:
        newpos.QuadPart = dist.QuadPart + lseek(fd->f, 0, SEEK_CUR);
        break;
    case FILE_END:
    {
        struct stat st = { 0 };
        if (fstat(fd->f, &st) == -1)
        {
            return INVALID_SET_FILE_POINTER;
        }
        newpos.QuadPart = dist.QuadPart + st.st_size;
        break;
    }
    }
    if (newpos.QuadPart < 0)
    {
        SetLastError(ERROR_NEGATIVE_SEEK);
        return FALSE;
    }

    if (lseek(fd->f, newpos.QuadPart, SEEK_SET) == (off_t)-1)
        return INVALID_SET_FILE_POINTER;

    if (highword)
        *highword = newpos.u.HighPart;
    if (newpos.u.LowPart == INVALID_SET_FILE_POINTER)
        SetLastError(0);
    return newpos.u.LowPart;
}

/**************************************************************************
 *	SetEndOfFile   (kernelbase.@)
 */
BOOL WINAPI SetEndOfFile(HANDLE file)
{
    _FileData *fd = GetFD(file);
    if (!fd)
    {
        return 0;
    }
    LARGE_INTEGER pos;
    pos.QuadPart = lseek(fd->f, 0, SEEK_CUR);
    return set_fd_eof(fd->f, pos.QuadPart) != -1;
}

typedef short CSHORT;
typedef struct _TIME_FIELDS
{
    CSHORT Year;
    CSHORT Month;
    CSHORT Day;
    CSHORT Hour;
    CSHORT Minute;
    CSHORT Second;
    CSHORT Milliseconds;
    CSHORT Weekday;
} TIME_FIELDS, *PTIME_FIELDS;

#define TICKSPERSEC              10000000
#define TICKSPERMSEC             10000
#define SECSPERDAY               86400
#define SECSPERHOUR              3600
#define SECSPERMIN               60
#define MINSPERHOUR              60
#define HOURSPERDAY              24
#define EPOCHWEEKDAY             1 /* Jan 1, 1601 was Monday */
#define DAYSPERWEEK              7
#define MONSPERYEAR              12
#define DAYSPERQUADRICENTENNIUM  (365 * 400 + 97)
#define DAYSPERNORMALQUADRENNIUM (365 * 4 + 1)

/* 1601 to 1970 is 369 years plus 89 leap days */
#define SECS_1601_TO_1970  ((369 * 365 + 89) * (ULONGLONG)SECSPERDAY)
#define TICKS_1601_TO_1970 (SECS_1601_TO_1970 * TICKSPERSEC)
/* 1601 to 1980 is 379 years plus 91 leap days */
#define SECS_1601_TO_1980  ((379 * 365 + 91) * (ULONGLONG)SECSPERDAY)
#define TICKS_1601_TO_1980 (SECS_1601_TO_1980 * TICKSPERSEC)

static const int MonthLengths[2][MONSPERYEAR] = { { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }, { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 } };

static inline BOOL IsLeapYear(int Year)
{
    return Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
}

/******************************************************************************
 *       RtlTimeToTimeFields [NTDLL.@]
 *
 * Convert a time into a TIME_FIELDS structure.
 *
 * PARAMS
 *   liTime     [I] Time to convert.
 *   TimeFields [O] Destination for the converted time.
 *
 * RETURNS
 *   Nothing.
 */
VOID WINAPI RtlTimeToTimeFields(const LARGE_INTEGER *liTime, PTIME_FIELDS TimeFields)
{
    int SecondsInDay;
    long int cleaps, years, yearday, months;
    long int Days;
    LONGLONG Time;

    /* Extract millisecond from time and convert time into seconds */
    TimeFields->Milliseconds = (CSHORT)((liTime->QuadPart % TICKSPERSEC) / TICKSPERMSEC);
    Time = liTime->QuadPart / TICKSPERSEC;

    /* The native version of RtlTimeToTimeFields does not take leap seconds
     * into account */

    /* Split the time into days and seconds within the day */
    Days = Time / SECSPERDAY;
    SecondsInDay = Time % SECSPERDAY;

    /* compute time of day */
    TimeFields->Hour = (CSHORT)(SecondsInDay / SECSPERHOUR);
    SecondsInDay = SecondsInDay % SECSPERHOUR;
    TimeFields->Minute = (CSHORT)(SecondsInDay / SECSPERMIN);
    TimeFields->Second = (CSHORT)(SecondsInDay % SECSPERMIN);

    /* compute day of week */
    TimeFields->Weekday = (CSHORT)((EPOCHWEEKDAY + Days) % DAYSPERWEEK);

    /* compute year, month and day of month. */
    cleaps = (3 * ((4 * Days + 1227) / DAYSPERQUADRICENTENNIUM) + 3) / 4;
    Days += 28188 + cleaps;
    years = (20 * Days - 2442) / (5 * DAYSPERNORMALQUADRENNIUM);
    yearday = Days - (years * DAYSPERNORMALQUADRENNIUM) / 4;
    months = (64 * yearday) / 1959;
    /* the result is based on a year starting on March.
     * To convert take 12 from Januari and Februari and
     * increase the year by one. */
    if (months < 14)
    {
        TimeFields->Month = months - 1;
        TimeFields->Year = years + 1524;
    }
    else
    {
        TimeFields->Month = months - 13;
        TimeFields->Year = years + 1525;
    }
    /* calculation of day of month is based on the wonderful
     * sequence of INT( n * 30.6): it reproduces the
     * 31-30-31-30-31-31 month lengths exactly for small n's */
    TimeFields->Day = yearday - (1959 * months) / 64;
    return;
}

/******************************************************************************
 *       RtlTimeFieldsToTime [NTDLL.@]
 *
 * Convert a TIME_FIELDS structure into a time.
 *
 * PARAMS
 *   ftTimeFields [I] TIME_FIELDS structure to convert.
 *   Time         [O] Destination for the converted time.
 *
 * RETURNS
 *   Success: TRUE.
 *   Failure: FALSE.
 */
BOOL WINAPI RtlTimeFieldsToTime(PTIME_FIELDS tfTimeFields, PLARGE_INTEGER Time)
{
    int month, year, cleaps, day;

    /* FIXME: normalize the TIME_FIELDS structure here */
    /* No, native just returns 0 (error) if the fields are not */
    if (tfTimeFields->Milliseconds < 0 || tfTimeFields->Milliseconds > 999 || tfTimeFields->Second < 0 || tfTimeFields->Second > 59 || tfTimeFields->Minute < 0 || tfTimeFields->Minute > 59 || tfTimeFields->Hour < 0 || tfTimeFields->Hour > 23 || tfTimeFields->Month < 1 || tfTimeFields->Month > 12 || tfTimeFields->Day < 1 || tfTimeFields->Day > MonthLengths[tfTimeFields->Month == 2 || IsLeapYear(tfTimeFields->Year)][tfTimeFields->Month - 1] || tfTimeFields->Year < 1601)
        return FALSE;

    /* now calculate a day count from the date
     * First start counting years from March. This way the leap days
     * are added at the end of the year, not somewhere in the middle.
     * Formula's become so much less complicate that way.
     * To convert: add 12 to the month numbers of Jan and Feb, and
     * take 1 from the year */
    if (tfTimeFields->Month < 3)
    {
        month = tfTimeFields->Month + 13;
        year = tfTimeFields->Year - 1;
    }
    else
    {
        month = tfTimeFields->Month + 1;
        year = tfTimeFields->Year;
    }
    cleaps = (3 * (year / 100) + 3) / 4;  /* nr of "century leap years"*/
    day = (36525 * year) / 100 - cleaps + /* year * dayperyr, corrected */
        (1959 * month) / 64 +             /* months * daypermonth */
        tfTimeFields->Day -               /* day of the month */
        584817;                           /* zero that on 1601-01-01 */
                                          /* done */

    Time->QuadPart = (((((LONGLONG)day * HOURSPERDAY + tfTimeFields->Hour) * MINSPERHOUR + tfTimeFields->Minute) * SECSPERMIN + tfTimeFields->Second) * 1000 + tfTimeFields->Milliseconds) * TICKSPERMSEC;

    return TRUE;
}

/***********************************************************************
 *           DosDateTimeToFileTime   (KERNEL32.@)
 */
BOOL WINAPI DosDateTimeToFileTime(WORD fatdate, WORD fattime, FILETIME *ft)
{
    TIME_FIELDS fields;
    LARGE_INTEGER time;

    fields.Year = (fatdate >> 9) + 1980;
    fields.Month = ((fatdate >> 5) & 0x0f);
    fields.Day = (fatdate & 0x1f);
    fields.Hour = (fattime >> 11);
    fields.Minute = (fattime >> 5) & 0x3f;
    fields.Second = (fattime & 0x1f) * 2;
    fields.Milliseconds = 0;
    if (!RtlTimeFieldsToTime(&fields, &time))
        return FALSE;
    ft->dwLowDateTime = time.u.LowPart;
    ft->dwHighDateTime = time.u.HighPart;
    return TRUE;
}

/***********************************************************************
 *           FileTimeToDosDateTime   (KERNEL32.@)
 */
BOOL WINAPI FileTimeToDosDateTime(const FILETIME *ft, WORD *fatdate, WORD *fattime)
{
    TIME_FIELDS fields;
    LARGE_INTEGER time;

    if (!fatdate || !fattime)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    time.u.LowPart = ft->dwLowDateTime;
    time.u.HighPart = ft->dwHighDateTime;
    RtlTimeToTimeFields(&time, &fields);
    if (fields.Year < 1980)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *fattime = (fields.Hour << 11) + (fields.Minute << 5) + (fields.Second / 2);
    *fatdate = ((fields.Year - 1980) << 9) + (fields.Month << 5) + fields.Day;
    return TRUE;
}

DWORD GetFileAttributesA(LPCSTR lpFileName)
{
    struct stat st;
    if (0 != stat(lpFileName, &st))
        return INVALID_FILE_ATTRIBUTES;
    DWORD ret = 0;
    if (S_ISDIR(st.st_mode))
        ret |= FILE_ATTRIBUTE_DIRECTORY;
    else
    {
        ret |= FILE_ATTRIBUTE_NORMAL;
    }
    if (S_IWUSR & st.st_mode)
    {
        ret |= FILE_ATTRIBUTE_READONLY;
    }
    if (strrchr(lpFileName, '.') != nullptr)
    {
        ret |= FILE_ATTRIBUTE_HIDDEN;
    }
    return ret;
}

DWORD GetFileAttributesW(LPCWSTR lpFileName)
{
    std::string str;
    tostring(lpFileName, -1, str);
    return GetFileAttributesA(str.c_str());
}

BOOL WINAPI DeleteFileA(LPCSTR lpFileName)
{
    return unlink(lpFileName) == 0;
}

BOOL WINAPI DeleteFileW(LPCWSTR lpFileName)
{
    std::string str;
    tostring(lpFileName, -1, str);
    return DeleteFileA(str.c_str());
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName)
{
    std::string strPath = lpPathName;
    if (strPath[strPath.length() - 1] == '/')
        strPath = strPath.substr(0, strPath.length() - 1);
    return chdir(strPath.c_str()) == 0;
}

BOOL WINAPI SetCurrentDirectoryW(LPCWSTR lpPathName)
{
    std::string str;
    tostring(lpPathName, -1, str);
    return SetCurrentDirectoryA(str.c_str());
}

DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
    char *ret = getcwd(lpBuffer, nBufferLength);
    if (!ret)
        return 0;
    return strlen(ret);
}

DWORD WINAPI GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    char cwd[PATH_MAX * 2];
    if (GetCurrentDirectoryA(PATH_MAX * 2, cwd) == 0)
        return 0;
    std::wstring wpath;
    towstring(cwd, -1, wpath);
    if (!lpBuffer)
        return wpath.length() + 1;
    if (nBufferLength <= wpath.length())
        return 0;
    memcpy(lpBuffer, wpath.c_str(), (wpath.length() + 1) * sizeof(wchar_t));
    return wpath.length() + 1;
}

BOOL CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    mode_t mode = 0755;
    return mkdir(lpPathName, mode);
}

BOOL CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    char szPath[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpPathName, -1, szPath, MAX_PATH, nullptr, nullptr))
        return FALSE;
    return CreateDirectoryA(szPath, lpSecurityAttributes);
}

/* info structure for FindFirstFile handle */
typedef struct
{
    DWORD magic; /* magic number */
    DIR *dir;    /* handle to directory */
    char path[MAX_PATH];
    char name[MAX_PATH];         /* NT path used to open the directory */
    CRITICAL_SECTION cs;         /* crit section protecting this structure */
    FINDEX_SEARCH_OPS search_op; /* Flags passed to FindFirst.  */
    FINDEX_INFO_LEVELS level;    /* Level passed to FindFirst */
    BOOL wildcard;               /* did the mask contain wildcard characters? */
} FIND_FIRST_INFO;

#define FIND_FIRST_MAGIC 0xc0ffee11

static void file_name_AtoW(const char *name, wchar_t *buf, int len)
{
    MultiByteToWideChar(CP_UTF8, 0, name, -1, buf, len);
}

HANDLE
WINAPI
FindFirstFileA(_In_ LPCSTR lpFileName, _Out_ LPWIN32_FIND_DATAA lpFindFileData)
{
    return FindFirstFileExA(lpFileName, FindExInfoStandard, lpFindFileData, FindExSearchNameMatch, nullptr, 0);
}

HANDLE
WINAPI
FindFirstFileW(_In_ LPCWSTR lpFileName, _Out_ LPWIN32_FIND_DATAW lpFindFileData)
{
    std::string strName;
    tostring(lpFileName, -1, strName);
    WIN32_FIND_DATAA dataA;
    HANDLE ret = FindFirstFileA(strName.c_str(), &dataA);
    if (ret)
    {
        lpFindFileData->dwFileAttributes = dataA.dwFileAttributes;
        lpFindFileData->ftCreationTime = dataA.ftCreationTime;
        lpFindFileData->ftLastWriteTime = dataA.ftLastWriteTime;
        lpFindFileData->ftLastAccessTime = dataA.ftLastAccessTime;
        lpFindFileData->nFileSizeLow = dataA.nFileSizeLow;
        lpFindFileData->nFileSizeHigh = dataA.nFileSizeHigh;
        file_name_AtoW(dataA.cFileName, lpFindFileData->cFileName, MAX_PATH);
        file_name_AtoW(dataA.cAlternateFileName, lpFindFileData->cAlternateFileName, 14);
    }
    return ret;
}

BOOL WINAPI FindNextFileA(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAA lpFindFileData)
{
    if (hFindFile == INVALID_HANDLE_VALUE)
        return FALSE;

    FIND_FIRST_INFO *info = (FIND_FIRST_INFO *)hFindFile;
    if (info->magic != FIND_FIRST_MAGIC)
        return FALSE;
    BOOL bMatch = FALSE;
    EnterCriticalSection(&info->cs);
    for (;;)
    {
        struct dirent *entry = readdir(info->dir);
        if (!entry)
            break;

        if (info->wildcard)
            bMatch = 0 == fnmatch(info->name, entry->d_name, 0);
        else if (info->level == 0)
            bMatch = stricmp(info->name, entry->d_name) == 0;
        if (!bMatch)
            continue;
        struct stat64 fileStat = { 0 };
        std::stringstream path;
        path << info->path << "/" << entry->d_name;
        if (0 == stat64(path.str().c_str(), &fileStat))
        {
            if (S_ISREG(fileStat.st_mode))
            {
                lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL;
            }
            else if (S_ISDIR(fileStat.st_mode))
            {
                lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            }
            strcpy(lpFindFileData->cFileName, entry->d_name);
            strcpy(lpFindFileData->cAlternateFileName, "");
            TimeSpec2FileTime(fileStat.st_ctim, &lpFindFileData->ftCreationTime);
            TimeSpec2FileTime(fileStat.st_mtim, &lpFindFileData->ftLastWriteTime);
            TimeSpec2FileTime(fileStat.st_atim, &lpFindFileData->ftLastAccessTime);
            lpFindFileData->nFileSizeLow = fileStat.st_size & 0xffffffff;
            lpFindFileData->nFileSizeHigh = (fileStat.st_size & 0xffffffff00000000) >> 32;
            break;
        }
    }
    LeaveCriticalSection(&info->cs);
    return bMatch;
}

BOOL WINAPI FindNextFileW(_In_ HANDLE hFindFile, _Out_ LPWIN32_FIND_DATAW lpFindFileData)
{
    WIN32_FIND_DATAA dataA;
    BOOL ret = FindNextFileA(hFindFile, &dataA);
    if (ret)
    {
        lpFindFileData->dwFileAttributes = dataA.dwFileAttributes;
        lpFindFileData->ftCreationTime = dataA.ftCreationTime;
        lpFindFileData->ftLastWriteTime = dataA.ftLastWriteTime;
        lpFindFileData->ftLastAccessTime = dataA.ftLastAccessTime;
        lpFindFileData->nFileSizeLow = dataA.nFileSizeLow;
        lpFindFileData->nFileSizeHigh = dataA.nFileSizeHigh;
        file_name_AtoW(dataA.cFileName, lpFindFileData->cFileName, MAX_PATH);
        file_name_AtoW(dataA.cAlternateFileName, lpFindFileData->cAlternateFileName, 14);
    }
    return ret;
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
    if (hFindFile == INVALID_HANDLE_VALUE)
        return FALSE;
    FIND_FIRST_INFO *info = (FIND_FIRST_INFO *)hFindFile;
    if (info->magic != FIND_FIRST_MAGIC)
        return FALSE;
    closedir(info->dir);
    HeapFree(GetProcessHeap(), 0, info);
    return TRUE;
}

/******************************************************************************
 *	FindFirstFileExA   (kernelbase.@)
 */
HANDLE WINAPI FindFirstFileExW(const wchar_t *filename, FINDEX_INFO_LEVELS level, void *data, FINDEX_SEARCH_OPS search_op, void *filter, DWORD flags)
{
    std::string str;
    tostring(filename, -1, str);

    WIN32_FIND_DATAW *dataW = (WIN32_FIND_DATAW *)data;
    WIN32_FIND_DATAA dataA;
    HANDLE handle = FindFirstFileExA(str.c_str(), level, &dataA, search_op, filter, flags);
    if (handle == INVALID_HANDLE_VALUE)
        return handle;

    dataW->dwFileAttributes = dataA.dwFileAttributes;
    dataW->ftCreationTime = dataA.ftCreationTime;
    dataW->ftLastAccessTime = dataA.ftLastAccessTime;
    dataW->ftLastWriteTime = dataA.ftLastWriteTime;
    dataW->nFileSizeHigh = dataA.nFileSizeHigh;
    dataW->nFileSizeLow = dataA.nFileSizeLow;

    file_name_AtoW(dataA.cFileName, dataW->cFileName, ARRAYSIZE(dataW->cFileName));
    file_name_AtoW(dataA.cAlternateFileName, dataW->cAlternateFileName, ARRAYSIZE(dataW->cAlternateFileName));
    return handle;
}

HANDLE WINAPI FindFirstFileExA(LPCSTR filename, FINDEX_INFO_LEVELS level, LPVOID data, FINDEX_SEARCH_OPS search_op, LPVOID filter, DWORD flags)
{
    if (!filename)
        return INVALID_HANDLE_VALUE;
    std::string strName(filename);
    FIND_FIRST_INFO *info = NULL;
    char *name = (char *)strrchr(strName.c_str(), '/');
    if (!name || strlen(name) >= MAX_PATH + 2)
    {
        SLOG_FMTD("search_op not implemented 0x%08x\n", search_op);
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    *name = 0;
    name++;
    DIR *dir = opendir(strName.c_str());
    if (!dir)
    {
        SLOG_FMTD("search_op not implemented 0x%08x\n", search_op);
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    if (flags & ~FIND_FIRST_EX_LARGE_FETCH)
    {
        SLOG_FMTW("flags not implemented 0x%08x\n", flags);
    }
    if (search_op != FindExSearchNameMatch && search_op != FindExSearchLimitToDirectories)
    {
        SLOG_FMTW("search_op not implemented 0x%08x\n", search_op);
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    if (level != FindExInfoStandard && level != FindExInfoBasic)
    {
        SLOG_FMTW("info level %d not implemented\n", level);
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    void *buf = HeapAlloc(GetProcessHeap(), 0, sizeof(FIND_FIRST_INFO));
    if (!buf)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        closedir(dir);
        return INVALID_HANDLE_VALUE;
    }
    info = (FIND_FIRST_INFO *)buf;
    InitializeCriticalSection(&info->cs);
    info->magic = FIND_FIRST_MAGIC;
    info->dir = dir;
    info->search_op = search_op;
    info->level = level;
    info->wildcard = strpbrk(name, "*?") != NULL;
    strcpy(info->path, strName.c_str());
    strcpy(info->name, name);
    HANDLE hFind = (HANDLE)info;
    FindNextFileA(hFind, (LPWIN32_FIND_DATAA)data);
    return hFind;
}
