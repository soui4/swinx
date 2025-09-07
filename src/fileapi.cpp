#include <fileapi.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <libgen.h>

#include "handle.h"
#include "tostring.hpp"
#include "debug.h"
#define kLogTag "file"

struct _FileData
{
    int fd;
    _FileData()
        : fd(-1)
    {
    }
    ~_FileData()
    {
        if (fd != -1)
        {
            close(fd);
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
    DWORD dwAccess = 0;
    if (dwDesiredAccess | GENERIC_READ)
        dwAccess |= FILE_GENERIC_READ;
    if (dwDesiredAccess | GENERIC_WRITE)
        dwAccess |= FILE_GENERIC_WRITE;
    if (dwDesiredAccess | GENERIC_EXECUTE)
        dwAccess |= FILE_GENERIC_EXECUTE;
    if (dwDesiredAccess | GENERIC_ALL)
        dwAccess |= FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE;

    if ((dwAccess & (FILE_READ_DATA | FILE_WRITE_DATA)) == (FILE_READ_DATA | FILE_WRITE_DATA))
        mode = O_RDWR;
    else if (dwAccess & FILE_WRITE_DATA)
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
    fd->fd = open(lpFileName, mode, 0644);
    if (fd->fd == -1)
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
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    if (h->type == FILE_OBJ)
        return (_FileData *)h->ptr;
    return nullptr;
}

BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
    _FileData *fd = GetFD(hFile);
    if (!fd)
        return FALSE;
    off_t pos = lseek(fd->fd, 0, SEEK_CUR);
    lpFileSize->QuadPart = lseek(fd->fd, 0, SEEK_END);
    lseek(fd->fd, pos, SEEK_SET);
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

static void FileTime2TimeSpec(const LPFILETIME fts, struct timespec &ts)
{
    if (!fts)
        return;
    uint64_t nsec;
    memcpy(&nsec, fts, sizeof(uint64_t));
    ts.tv_sec = nsec / 1000000000;
    ts.tv_nsec = nsec % 1000000000;
}

static void TimeSpec2FileTime(const struct timespec &ts, LPFILETIME fts)
{
    if (!fts)
        return;
    uint64_t nsec = ts.tv_nsec;
    memcpy(fts, &nsec, sizeof(uint64_t));
}

BOOL WINAPI SetFileTime(HANDLE hFile, const LPFILETIME lpCreationTime,const LPFILETIME lpLastAccessTime,const LPFILETIME lpLastWriteTime)
{
    _FileData *fd = GetFD(hFile);
    if (!fd)
        return FALSE;
    struct timespec ts[2]; 
    if(lpLastAccessTime){
        FileTime2TimeSpec(lpLastAccessTime, ts[0]);
    }
    if(lpLastWriteTime){
        FileTime2TimeSpec(lpLastWriteTime, ts[1]);
    }
    
    return 0== utimensat(fd->fd, "", ts, 0);
}

BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
    if (_FileData *fd = GetFD(hFile))
    {
        struct timespec st_atim; /* Time of last access.  */
        struct timespec st_mtim; /* Time of last modification.  */
        struct timespec st_ctim; /* Time of last status change.  */

        struct stat st;
        if (0 != fstat(fd->fd, &st))
            return FALSE;
        #ifdef __APPLE__
        TimeSpec2FileTime(st.st_atimespec, lpLastAccessTime);
        TimeSpec2FileTime(st.st_mtimespec, lpLastWriteTime);
        TimeSpec2FileTime(st.st_ctimespec, lpCreationTime);
        #else
        TimeSpec2FileTime(st.st_ctim, lpCreationTime);
        TimeSpec2FileTime(st.st_mtim, lpLastWriteTime);
        TimeSpec2FileTime(st.st_atim, lpLastAccessTime);
        #endif
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
        if (fd->fd == -1)
            return FALSE;
        int readed = read(fd->fd, lpBuffer, nNumberOfBytesToRead);
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
        if (fd->fd == -1)
            return FALSE;
        int writed = write(fd->fd, lpBuffer, nNumberOfBytesToWrite);
        if (lpNumberOfBytesWritten)
            *lpNumberOfBytesWritten = writed;
        return writed == nNumberOfBytesToWrite;
    }
    return FALSE;
}

int _open_osfhandle(HANDLE hFile, int flags)
{
    _FileData *fd = GetFD(hFile);
    if (!fd)
        return -1;
    return fd->fd;
}

/* extend a file beyond the current end of file */
static bool grow_file(int unix_fd, uint64_t new_size)
{
    static const char zero = 0;
    off_t size = new_size;

    if (sizeof(new_size) > sizeof(size) && size != new_size)
    {
        set_error(STATUS_INVALID_PARAMETER);
        return false;
    }
    /* extend the file one byte beyond the requested size and then truncate it */
    /* this should work around ftruncate implementations that can't extend files */
    if (pwrite(unix_fd, &zero, 1, size) != -1)
    {
        return ftruncate(unix_fd, size) == 0 ;
    }
    return false;
}

static bool set_fd_eof(int fd, uint64_t eof)
{
    struct stat st;

    if (fd == -1)
    {
        return false;
    }
    if (fstat(fd, &st) == -1)
    {
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
    LARGE_INTEGER dist, pos;
    dist.QuadPart = (LONGLONG)distance;
    if (!SetFilePointerEx(file, dist, &pos, method))
        return 0;
    if (highword)
        *highword = pos.HighPart;
    return pos.LowPart;
}

BOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER dist, PLARGE_INTEGER lpNewFilePointer, DWORD method)
{
    _FileData *fd = GetFD(hFile);
    if (!fd)
    {
        return FALSE;
    }
    LARGE_INTEGER newpos = dist;
    switch (method)
    {
    case FILE_BEGIN:
        newpos.QuadPart = dist.QuadPart;
        break;
    case FILE_CURRENT:
        newpos.QuadPart = dist.QuadPart + lseek(fd->fd, 0, SEEK_CUR);
        break;
    case FILE_END:
    {
        struct stat st = { 0 };
        if (fstat(fd->fd, &st) == -1)
        {
            SetLastError(INVALID_SET_FILE_POINTER);
            return FALSE;
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

    off_t nowPos = lseek(fd->fd, newpos.QuadPart, SEEK_SET);
    if (nowPos == (off_t)-1)
    {
        SetLastError(INVALID_SET_FILE_POINTER);
        return FALSE;
    }
    if (lpNewFilePointer)
    {
        lpNewFilePointer->QuadPart = nowPos;
    }
    return TRUE;
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
    pos.QuadPart = lseek(fd->fd, 0, SEEK_CUR);
    return set_fd_eof(fd->fd, pos.QuadPart);
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

BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes){
    if (!lpFileName) 
        return FALSE;
    
    // 获取当前文件状态
    struct stat st;
    if (lstat(lpFileName, &st) != 0) {
        return FALSE;
    }
    
    // 处理只读属性 (在Windows中FILE_ATTRIBUTE_READONLY对应Linux的写权限)
    if (dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
        // 清除写权限
        mode_t new_mode = st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH);
        if (chmod(lpFileName, new_mode) != 0) {
            return FALSE;
        }
    } else {
        // 设置写权限 (如果对象是文件且之前是只读的)
        if (!(st.st_mode & S_IWUSR)) {
            mode_t new_mode = st.st_mode | S_IWUSR;
            if (chmod(lpFileName, new_mode) != 0) {
                return FALSE;
            }
        }
    }
    
    // 处理隐藏属性 (在Linux中以.开头的文件是隐藏文件)
    if (dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
        // 注意：在Linux中，重命名文件以添加前导点比较复杂，
        // 因为需要确保目标文件不存在且需要处理路径。
        // 这里简化处理，只在获取属性时检查文件名是否以.开头。
        // 实际应用中可能需要更复杂的处理。
    }
    
    // 处理目录属性
    if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        // 这个属性通常是只读的，由文件系统决定
        // 不需要特殊处理
    }
    
    return TRUE;
}

BOOL WINAPI SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes){
    std::string str;
    tostring(lpFileName, -1, str);
    return SetFileAttributesA(str.c_str(), dwFileAttributes);
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
    return mkdir(lpPathName, mode) == 0;
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
    UINT wildcard;               /* did the mask contain wildcard characters? */
} FIND_FIRST_INFO;
#define MASK_FILTER_ALL  0x80000000
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
    return FindFirstFileA(strName.c_str(), &dataA);
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
        if (info->wildcard & MASK_FILTER_ALL)
            bMatch = TRUE;
        else if (info->wildcard)
            bMatch = 0 == fnmatch(info->name, entry->d_name, 0);
        else if (info->level == 0)
            bMatch = stricmp(info->name, entry->d_name) == 0;
        if (!bMatch)
            continue;
        struct stat fileStat = { 0 };
        std::stringstream path;
        path << info->path << "/" << entry->d_name;
        if (0 == stat(path.str().c_str(), &fileStat))
        {
            if (S_ISREG(fileStat.st_mode))
            {
                lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL;
            }
            else if (S_ISDIR(fileStat.st_mode))
            {
                lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            }
            if (entry->d_name[0] == '.')
            {
                lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN;
            }
            strcpy(lpFindFileData->cFileName, entry->d_name);
            strcpy(lpFindFileData->cAlternateFileName, "");
            //TimeSpec2FileTime(fileStat.st_ctim, &lpFindFileData->ftCreationTime);
            //TimeSpec2FileTime(fileStat.st_mtim, &lpFindFileData->ftLastWriteTime);
            //TimeSpec2FileTime(fileStat.st_atim, &lpFindFileData->ftLastAccessTime);
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
    if (info->wildcard && strcmp(name, "*.*") == 0)
    {
        info->wildcard |= MASK_FILTER_ALL;
    }
    strcpy(info->path, strName.c_str());
    strcpy(info->name, name);
    return (HANDLE)info;
}

BOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists)
{
    std::string strFrom, strTo;
    tostring(lpExistingFileName, -1, strFrom);
    tostring(lpNewFileName, -1, strTo);
    return CopyFileA(strFrom.c_str(), strTo.c_str(), bFailIfExists);
}

#define BUFFER_SIZE 4096
BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists)
{
    int src_fd, dest_fd;
    size_t bytes_read, bytes_written;
    char buffer[BUFFER_SIZE];

    if (bFailIfExists)
    {
        if (DWORD attr = GetFileAttributesA(lpNewFileName) != INVALID_FILE_ATTRIBUTES)
        {
            if (attr & FILE_ATTRIBUTE_NORMAL)
                return FALSE;
        }
    }
    // 打开源文件
    if ((src_fd = open(lpExistingFileName, O_RDONLY)) == -1)
    {
        return FALSE;
    }

    // 创建目标文件
    if ((dest_fd = open(lpNewFileName, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
    {
        close(src_fd);
        return FALSE;
    }

    // 读取并写入数据
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0)
    {
        bytes_written = write(dest_fd, buffer, bytes_read);
        if (bytes_written != bytes_read)
        {
            close(src_fd);
            close(dest_fd);
            return FALSE;
        }
    }

    // 关闭文件
    close(src_fd);
    close(dest_fd);

    return bytes_read != -1;
}

int CopyDirW(const wchar_t *src_dir, const wchar_t *dest_dir)
{
    std::string strFrom, strTo;
    tostring(src_dir, -1, strFrom);
    tostring(dest_dir, -1, strTo);
    return CopyDirA(strFrom.c_str(), strTo.c_str());
}

static int CopyDirPattern(const char *src_dir, const char *pattern, const char *dest_dir)
{
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;
    // 打开源目录
    if ((dir = opendir(src_dir)) == NULL)
    {
        return -1;
    }

    // 创建目标目录
    if (mkdir(dest_dir, 0755) == -1 && errno != EEXIST)
    {
        closedir(dir);
        return -1;
    }

    // 遍历目录内容
    while ((entry = readdir(dir)) != NULL)
    {
        char src_path[MAX_PATH], dest_path[MAX_PATH];

        // 忽略 `.` 和 `..`
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        if (fnmatch(pattern, entry->d_name, 0) != 0)
        {
            continue;
        }
        // 构造完整路径
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->d_name);

        // 获取文件状态
        if (stat(src_path, &stat_buf) == -1)
        {
            continue;
        }

        // 递归处理目录
        if (S_ISDIR(stat_buf.st_mode))
        {
            if (CopyDirPattern(src_path, pattern, dest_path) == -1)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            // 复制文件
            if (!CopyFileA(src_path, dest_path, FALSE))
            {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

// 递归复制目录
int CopyDirA(const char *src_dir_0, const char *dest_dir)
{
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;
    std::string strSrc(src_dir_0);
    // 打开源目录
    if ((dir = opendir(strSrc.c_str())) == NULL)
    {
        // in case of src_dir appended with *.*
        char *pattern = (char *)strrchr(strSrc.c_str(), '/');
        if (!pattern)
            return -1;
        pattern[0] = 0;
        pattern++;
        if (strcmp(pattern, "*.*") == 0)
        {
            return CopyDirA(strSrc.c_str(), dest_dir);
        }
        else
        {
            return CopyDirPattern(strSrc.c_str(), pattern, dest_dir);
        }
    }

    // 创建目标目录
    if (mkdir(dest_dir, 0755) == -1 && errno != EEXIST)
    {
        closedir(dir);
        return -1;
    }

    // 遍历目录内容
    while ((entry = readdir(dir)) != NULL)
    {
        char src_path[MAX_PATH], dest_path[MAX_PATH];

        // 忽略 `.` 和 `..`
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // 构造完整路径
        snprintf(src_path, sizeof(src_path), "%s/%s", strSrc.c_str(), entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->d_name);

        // 获取文件状态
        if (stat(src_path, &stat_buf) == -1)
        {
            continue;
        }

        // 递归处理目录
        if (S_ISDIR(stat_buf.st_mode))
        {
            if (CopyDirA(src_path, dest_path) == -1)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            // 复制文件
            if (!CopyFileA(src_path, dest_path, FALSE))
            {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

#define TRASH_DIR_FILES "/.local/share/Trash/files/"
#define TRASH_DIR_INFO  "/.local/share/Trash/info/"

// 获取当前用户的 HOME 目录
static const char *get_home_dir()
{
    const char *home = getenv("HOME");
    return home ? home : ".";
}

// 生成唯一的目标路径（防止命名冲突）
static void generate_unique_filename(const char *trash_path, char *unique_path, size_t size)
{
    struct stat st;
    snprintf(unique_path, size, "%s", trash_path);
    int count = 1;

    while (stat(unique_path, &st) == 0)
    {
        snprintf(unique_path, size, "%s_%d", trash_path, count++);
    }
}

// 获取当前时间的 ISO 8601 格式
static void get_iso_time(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", timeinfo);
}

// 移动文件或目录到回收站
static int move_to_trash(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
    {
        perror("stat");
        return -1;
    }

    const char *home = get_home_dir();
    char trash_path[1024], info_path[1024], unique_trash_path[1024];

    // 目标回收站路径
    snprintf(trash_path, sizeof(trash_path), "%s%s%s", home, TRASH_DIR_FILES, basename((char *)path));
    generate_unique_filename(trash_path, unique_trash_path, sizeof(unique_trash_path));

    // 移动文件或文件夹
    if (rename(path, unique_trash_path) == -1)
    {
        perror("rename");
        return -1;
    }

    // 创建 .trashinfo 记录
    snprintf(info_path, sizeof(info_path), "%s%s%s.trashinfo", home, TRASH_DIR_INFO, basename((char *)path));
    FILE *info_file = fopen(info_path, "w");
    if (!info_file)
    {
        perror("fopen");
        return -1;
    }

    char iso_time[32];
    get_iso_time(iso_time, sizeof(iso_time));

    fprintf(info_file, "[Trash Info]\n");
    fprintf(info_file, "Path=%s\n", path);
    fprintf(info_file, "DeletionDate=%s\n", iso_time);
    fclose(info_file);

    printf("Moved to trash: %s -> %s\n", path, unique_trash_path);
    return 0;
}

BOOL WINAPI DeleteFileA(LPCSTR lpFileName)
{
    return remove(lpFileName) == 0;
}

BOOL WINAPI DeleteFileW(LPCWSTR lpFileName)
{
    std::string str;
    tostring(lpFileName, -1, str);
    return DeleteFileA(str.c_str());
}

// 删除文件或空目录
static int delete_file_or_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1)
    {
        return -1;
    }

    if (S_ISDIR(st.st_mode))
    {                       // 如果是目录
        return rmdir(path); // 删除空目录
    }
    else
    {                        // 如果是文件
        return remove(path); // 删除文件
    }
}

// 递归删除非空目录
static int delete_non_empty_dir(const char *dir_path)
{
    DIR *dir;
    struct dirent *entry;
    char full_path[MAX_PATH];
    int ret = 0;

    // 打开目录
    dir = opendir(dir_path);
    if (!dir)
    {
        return -1;
    }

    // 遍历目录中的每个条目
    while ((entry = readdir(dir)) != NULL && ret == 0)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue; // 跳过 "." 和 ".."
        }

        // 构造完整路径
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == -1)
        {
            ret = -1;
            break;
        }

        if (S_ISDIR(st.st_mode))
        {
            // 递归删除子目录或删除文件
            ret = delete_non_empty_dir(full_path);
        }
        else
        {                            // 如果是文件
            ret = remove(full_path); // 删除文件
        }
    }

    // 关闭目录
    closedir(dir);
    if (ret == 0)
    {
        ret = rmdir(dir_path);
    }
    return ret;
}

int WINAPI DelDirA(const char *path, BOOL bAllowUndo)
{
    if (bAllowUndo)
    {
        return move_to_trash(path);
    }
    else
    {
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES)
            return -1;
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            return delete_non_empty_dir(path);
        else
            return remove(path);
    }
}

int WINAPI DelDirW(const wchar_t *src_dir, BOOL bAllowUndo)
{
    std::string str;
    tostring(src_dir, -1, str);
    return DelDirA(str.c_str(), bAllowUndo);
}
