#include <windows.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/inotify.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <mach-o/dyld.h>
#include <sys/event.h>
#include <crt_externs.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <iconv.h>
#include <setjmp.h>
#include <dirent.h>
#include "SConnection.h"
#include "wnd.h"
#include "uimsg.h"
#include "uniconv.h"
#include "synhandle.h"
#include "tostring.hpp"
#include "debug.h"
#include "sysapi.h"
#include "cursormgr.h"
#define kLogTag "sysapi"

using namespace swinx;
/*
ANSI_X3.4-1968 ANSI_X3.4-1986 ASCII CP367 IBM367 ISO-IR-6 ISO646-US ISO_646.IRV:1991 US US-ASCII CSASCII
UTF-8 UTF8
UTF-8-MAC UTF8-MAC
ISO-10646-UCS-2 UCS-2 CSUNICODE
UCS-2BE UNICODE-1-1 UNICODEBIG CSUNICODE11
UCS-2LE UNICODELITTLE
ISO-10646-UCS-4 UCS-4 CSUCS4
UCS-4BE
UCS-4LE
UTF-16
UTF-16BE
UTF-16LE
UTF-32
UTF-32BE
UTF-32LE
UNICODE-1-1-UTF-7 UTF-7 CSUNICODE11UTF7
UCS-2-INTERNAL
UCS-2-SWAPPED
UCS-4-INTERNAL
UCS-4-SWAPPED
C99
JAVA
CP819 IBM819 ISO-8859-1 ISO-IR-100 ISO8859-1 ISO_8859-1 ISO_8859-1:1987 L1 LATIN1 CSISOLATIN1
ISO-8859-2 ISO-IR-101 ISO8859-2 ISO_8859-2 ISO_8859-2:1987 L2 LATIN2 CSISOLATIN2
ISO-8859-3 ISO-IR-109 ISO8859-3 ISO_8859-3 ISO_8859-3:1988 L3 LATIN3 CSISOLATIN3
ISO-8859-4 ISO-IR-110 ISO8859-4 ISO_8859-4 ISO_8859-4:1988 L4 LATIN4 CSISOLATIN4
CYRILLIC ISO-8859-5 ISO-IR-144 ISO8859-5 ISO_8859-5 ISO_8859-5:1988 CSISOLATINCYRILLIC
ARABIC ASMO-708 ECMA-114 ISO-8859-6 ISO-IR-127 ISO8859-6 ISO_8859-6 ISO_8859-6:1987 CSISOLATINARABIC
ECMA-118 ELOT_928 GREEK GREEK8 ISO-8859-7 ISO-IR-126 ISO8859-7 ISO_8859-7 ISO_8859-7:1987 ISO_8859-7:2003 CSISOLATINGREEK
HEBREW ISO-8859-8 ISO-IR-138 ISO8859-8 ISO_8859-8 ISO_8859-8:1988 CSISOLATINHEBREW
ISO-8859-9 ISO-IR-148 ISO8859-9 ISO_8859-9 ISO_8859-9:1989 L5 LATIN5 CSISOLATIN5
ISO-8859-10 ISO-IR-157 ISO8859-10 ISO_8859-10 ISO_8859-10:1992 L6 LATIN6 CSISOLATIN6
ISO-8859-11 ISO8859-11 ISO_8859-11
ISO-8859-13 ISO-IR-179 ISO8859-13 ISO_8859-13 L7 LATIN7
ISO-8859-14 ISO-CELTIC ISO-IR-199 ISO8859-14 ISO_8859-14 ISO_8859-14:1998 L8 LATIN8
ISO-8859-15 ISO-IR-203 ISO8859-15 ISO_8859-15 ISO_8859-15:1998 LATIN-9
ISO-8859-16 ISO-IR-226 ISO8859-16 ISO_8859-16 ISO_8859-16:2001 L10 LATIN10
KOI8-R CSKOI8R
KOI8-U
KOI8-RU
CP1250 MS-EE WINDOWS-1250
CP1251 MS-CYRL WINDOWS-1251
CP1252 MS-ANSI WINDOWS-1252
CP1253 MS-GREEK WINDOWS-1253
CP1254 MS-TURK WINDOWS-1254
CP1255 MS-HEBR WINDOWS-1255
CP1256 MS-ARAB WINDOWS-1256
CP1257 WINBALTRIM WINDOWS-1257
CP1258 WINDOWS-1258
850 CP850 IBM850 CSPC850MULTILINGUAL
862 CP862 IBM862 CSPC862LATINHEBREW
866 CP866 IBM866 CSIBM866
MAC MACINTOSH MACROMAN CSMACINTOSH
MACCENTRALEUROPE
MACICELAND
MACCROATIAN
MACROMANIA
MACCYRILLIC
MACUKRAINE
MACGREEK
MACTURKISH
MACHEBREW
MACARABIC
MACTHAI
HP-ROMAN8 R8 ROMAN8 CSHPROMAN8
NEXTSTEP
ARMSCII-8
GEORGIAN-ACADEMY
GEORGIAN-PS
KOI8-T
CP154 CYRILLIC-ASIAN PT154 PTCP154 CSPTCP154
MULELAO-1
CP1133 IBM-CP1133
ISO-IR-166 TIS-620 TIS620 TIS620-0 TIS620.2529-1 TIS620.2533-0 TIS620.2533-1
CP874 WINDOWS-874
VISCII VISCII1.1-1 CSVISCII
TCVN TCVN-5712 TCVN5712-1 TCVN5712-1:1993
ISO-IR-14 ISO646-JP JIS_C6220-1969-RO JP CSISO14JISC6220RO
JISX0201-1976 JIS_X0201 X0201 CSHALFWIDTHKATAKANA
ISO-IR-87 JIS0208 JIS_C6226-1983 JIS_X0208 JIS_X0208-1983 JIS_X0208-1990 X0208 CSISO87JISX0208
ISO-IR-159 JIS_X0212 JIS_X0212-1990 JIS_X0212.1990-0 X0212 CSISO159JISX02121990
CN GB_1988-80 ISO-IR-57 ISO646-CN CSISO57GB1988
CHINESE GB_2312-80 ISO-IR-58 CSISO58GB231280
CN-GB-ISOIR165 ISO-IR-165
ISO-IR-149 KOREAN KSC_5601 KS_C_5601-1987 KS_C_5601-1989 CSKSC56011987
EUC-JP EUCJP EXTENDED_UNIX_CODE_PACKED_FORMAT_FOR_JAPANESE CSEUCPKDFMTJAPANESE
MS_KANJI SHIFT-JIS SHIFT_JIS SJIS CSSHIFTJIS
CP932
ISO-2022-JP CSISO2022JP
ISO-2022-JP-1
ISO-2022-JP-2 CSISO2022JP2
CN-GB EUC-CN EUCCN GB2312 CSGB2312
GBK
CP936 MS936 WINDOWS-936
GB18030
ISO-2022-CN CSISO2022CN
ISO-2022-CN-EXT
HZ HZ-GB-2312
EUC-TW EUCTW CSEUCTW
BIG-5 BIG-FIVE BIG5 BIGFIVE CN-BIG5 CSBIG5
CP950
BIG5-HKSCS:1999
BIG5-HKSCS:2001
BIG5-HKSCS BIG5-HKSCS:2004 BIG5HKSCS
EUC-KR EUCKR CSEUCKR
CP949 UHC
CP1361 JOHAB
ISO-2022-KR CSISO2022KR
CP856
CP922
CP943
CP1046
CP1124
CP1129
CP1161 IBM-1161 IBM1161 CSIBM1161
CP1162 IBM-1162 IBM1162 CSIBM1162
CP1163 IBM-1163 IBM1163 CSIBM1163
DEC-KANJI
DEC-HANYU
437 CP437 IBM437 CSPC8CODEPAGE437
CP737
CP775 IBM775 CSPC775BALTIC
852 CP852 IBM852 CSPCP852
CP853
855 CP855 IBM855 CSIBM855
857 CP857 IBM857 CSIBM857
CP858
860 CP860 IBM860 CSIBM860
861 CP-IS CP861 IBM861 CSIBM861
863 CP863 IBM863 CSIBM863
CP864 IBM864 CSIBM864
865 CP865 IBM865 CSIBM865
869 CP-GR CP869 IBM869 CSIBM869
CP1125
EUC-JISX0213
SHIFT_JISX0213
ISO-2022-JP-3
BIG5-2003
ISO-IR-230 TDS565
ATARI ATARIST
RISCOS-LATIN1
*/
#ifdef __APPLE__
#define ICONV_UTF32LE "UTF-32LE"
#define ICONV_UTF16LE "UTF-16LE"
#else
#define ICONV_UTF32LE "UTF32LE"
#define ICONV_UTF16LE "UTF16LE"
#endif //__APPLE__
#define ICONV_WINDOWS_936  "WINDOWS-936"
#define ICONV_WINDOWS_1250 "WINDOWS-1250"
#define ICONV_WINDOWS_1251 "WINDOWS-1251"
#define ICONV_WINDOWS_1252 "WINDOWS-1252"
#define ICONV_WINDOWS_1253 "WINDOWS-1253"
#define ICONV_WINDOWS_1254 "WINDOWS-1254"
#define ICONV_WINDOWS_1255 "WINDOWS-1255"
#define ICONV_WINDOWS_1256 "WINDOWS-1256"
#define ICONV_WINDOWS_1257 "WINDOWS-1257"
#define ICONV_WINDOWS_1258 "WINDOWS-1258"

const char *Cp2IConvCode(int codePage)
{
    switch (codePage)
    {
    case 936:
        return ICONV_WINDOWS_936;
    case 1250:
        return ICONV_WINDOWS_1250;
    case 1251:
        return ICONV_WINDOWS_1251;
    case 1252:
        return ICONV_WINDOWS_1252;
    case 1253:
        return ICONV_WINDOWS_1253;
    case 1254:
        return ICONV_WINDOWS_1254;
    case 1255:
        return ICONV_WINDOWS_1255;
    case 1256:
        return ICONV_WINDOWS_1256;
    case 1257:
        return ICONV_WINDOWS_1257;
    case 1258:
        return ICONV_WINDOWS_1258;
    default:
        return NULL;
    }
}

int to_mb(const wchar_t *input, size_t input_len, int codePage, std::string &out)
{
    const char *toCode = Cp2IConvCode(codePage);
    if (!toCode)
        return 0;
#if WCHAR_SIZE == 4
    iconv_t cd = iconv_open(toCode, ICONV_UTF32LE);
#else
    iconv_t cd = iconv_open(toCode, ICONV_UTF16LE);
#endif
    if (cd == (iconv_t)-1)
    {
        SLOG_STMW() << "iconv_open failed, codePage=" << codePage;
        return 0;
    }
    size_t output_len = (input_len + 1) * 4; // UTF-32 字符一般比 GBK 长
    out.resize(output_len);
    char *output = (char *)out.c_str();
    char *inbuf = (char *)input;
    char *outbuf = output;
    size_t inbytesleft = input_len * sizeof(wchar_t);
    size_t outbytesleft = output_len;

    size_t ret = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (ret != -1)
    {
        out.resize(output_len - outbytesleft);
        // out = out.substr(0,output_len-outbytesleft);
    }
    else
    {
        out.clear();
    }

    iconv_close(cd);
    return out.length();
}

int to_unicode(const char *input, size_t input_len, int codePage, std::wstring &out)
{
    const char *fromCode = Cp2IConvCode(codePage);
    if (!fromCode)
        return 0;
#if WCHAR_SIZE == 4
    iconv_t cd = iconv_open(ICONV_UTF32LE, fromCode);
#else
    iconv_t cd = iconv_open(ICONV_UTF16LE, fromCode);
#endif
    if (cd == (iconv_t)-1)
    {
        SLOG_STMW() << "iconv_open failed, codePage=" << codePage;
        return 0;
    }
    size_t output_len = (input_len + 1) * sizeof(wchar_t); // UTF-32 字符一般比 GBK 长
    out.resize(output_len);
    char *output = (char *)out.c_str();
    char *inbuf = (char *)input;
    char *outbuf = output;
    size_t inbytesleft = input_len;
    size_t outbytesleft = output_len;

    size_t ret = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (ret != -1)
    {
        out.resize((output_len - outbytesleft) / sizeof(wchar_t));
        // out=out.substr(0,(output_len-outbytesleft)/sizeof(wchar_t));
    }
    else
    {
        out.clear();
    }

    iconv_close(cd);
    return out.length();
}

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

    if (len < 0)
        len = strlen(src) + 1;
    if (cp != CP_ACP && cp != CP_UTF8)
    {
        std::wstring str;
        int ret = to_unicode(src, len, cp, str); // using iconv to support 936
        if (!dst)
            return str.length();
        else if (dstLen < ret)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        else
        {
            memcpy(dst, str.c_str(), ret * sizeof(wchar_t));
            if (ret < dstLen)
                dst[ret] = 0;
            return ret;
        }
    }

#if (WCHAR_SIZE == 2)
    assert(sizeof(wchar_t) == 2);
    // handle for utf16
    int bufRequire = UTF16Length(src, len);
    if (!dst)
    {
        return bufRequire;
    }
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF16FromUTF8(src, len, (uint16_t *)dst, dstLen);
    }
#else
    assert(sizeof(wchar_t) == 4);
    // handle for utf32
    int bufRequire = UTF32Length(src, len);
    if (!dst)
    {
        return bufRequire;
    }
    if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF32FromUTF8(src, len, (uint32_t *)dst, dstLen);
    }
#endif
}

int WideCharToMultiByte(int cp, int flags, const wchar_t *src, int len, char *dst, int dstLen, LPCSTR p1, BOOL *p2)
{
    assert(src);
    const wchar_t *ptr = src;
    if (len < 0)
        len = wcslen(src) + 1;
    const wchar_t *stop = src + len;
    size_t i = 0;
    if (cp == CP_OEMCP)
        cp = CP_UTF8; // todo:hjx
    if (cp != CP_ACP && cp != CP_UTF8)
    {
        std::string str;
        int ret = to_mb(src, len, cp, str);
        if (ret == 0)
            return 0;
        if (ret > dstLen)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(dst, str.c_str(), ret);
        if (ret < dstLen)
            dst[ret] = 0;
        return ret;
    }

#if (WCHAR_SIZE == 2)
    assert(sizeof(wchar_t) == 2);
    int bufRequire = UTF16toUTF8Length((const uint16_t *)src, len);
    if (!dst)
    {
        return bufRequire;
    }
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF8FromUTF16((const uint16_t *)src, len, dst, dstLen);
    }
#else
    assert(sizeof(wchar_t) == 4);
    int bufRequire = UTF32toUTF8Length((const uint32_t *)src, len);
    if (!dst)
        return bufRequire;
    else if (bufRequire > dstLen)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    else
    {
        SetLastError(NO_ERROR);
        return UTF8FromUTF32((const uint32_t *)src, len, dst, dstLen);
    }
#endif
}

#ifdef __APPLE__
// 获取当前进程的 RPATH 列表
static int get_current_rpaths(std::list<std::string> &rpaths)
{
    // 1. 获取当前可执行文件的 Mach-O 头
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (header == nullptr)
    {
        std::cerr << "Failed to get Mach-O header." << std::endl;
        return 0;
    }

    // 2. 遍历加载命令
    uintptr_t cmd_ptr = (uintptr_t)(header + 1); // 第一个加载命令的地址
    for (uint32_t i = 0; i < header->ncmds; i++)
    {
        const struct load_command *cmd = (const struct load_command *)cmd_ptr;

        // 检查是否为 RPATH 命令
        if (cmd->cmd == LC_RPATH)
        {
            const struct rpath_command *rpath_cmd = (const struct rpath_command *)cmd;
            // 获取 RPATH 字符串（紧跟在命令结构体后）
            const char *rpath = (const char *)(rpath_cmd + 1);
            rpaths.push_back(rpath);
        }

        cmd_ptr += cmd->cmdsize; // 移动到下一个加载命令
    }

    return rpaths.size();
}
#endif //__APPLE__

class DllLoader {
  public:
    DllLoader()
    {
        // search app dir
        char szPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, szPath, MAX_PATH);
        char *p = strrchr(szPath, '/');
        assert(p);
        p[1] = 0;
#ifdef __APPLE__
        const char rpaths[] = "@executable_path/";
        std::list<std::string> lstRPaths;
        get_current_rpaths(lstRPaths);
        for (auto it : lstRPaths)
        {
            std::string path = it;
            if (path.find(rpaths) == 0)
            {
                path.replace(0, sizeof(rpaths) - 1, szPath);
            }
            path += "/";
            m_lstDirs.push_back(path);
        }
#endif //__APPLE__
        m_lstDirs.push_back(szPath);
    }

    BOOL SetDllDirectoryA(LPCSTR lpPathName)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (lpPathName)
        {
            if ((GetFileAttributesA(lpPathName) & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return FALSE;
            m_userDllDir = lpPathName;
            if (*m_userDllDir.rbegin() != '/')
                m_userDllDir += '/';
            return TRUE;
        }
        else
        {
            m_userDllDir = "";
            return TRUE;
        }
    }

    DWORD GetDllDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_userDllDir.length() > nBufferLength)
        {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return 0;
        }
        memcpy(lpBuffer, m_userDllDir.c_str(), m_userDllDir.length());
        if (nBufferLength > m_userDllDir.length())
            lpBuffer[m_userDllDir.length()] = 0;
        return m_userDllDir.length();
    }

    DWORD GetDllDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return MultiByteToWideChar(CP_UTF8, 0, m_userDllDir.c_str(), -1, lpBuffer, nBufferLength);
    }

    HMODULE LoadDll(LPCSTR lpFileName, int mode)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_userDllDir.empty())
        {
            std::string path = m_userDllDir + lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        for (auto it : m_lstDirs)
        {
            std::string path = it + lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        {
            // search current dir
            char szPath[MAX_PATH] = { 0 };
            int len = GetCurrentDirectoryA(MAX_PATH, szPath);
            if (szPath[len - 1] != '/')
            {
                szPath[len++] = '/';
                szPath[len] = 0;
            }
            std::string path = szPath;
            path += lpFileName;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return mydlopen(path.c_str(), mode);
        }
        return NULL;
    }

    static HMODULE mydlopen(LPCSTR lpFileName, int mode)
    {
        HMODULE ret = dlopen(lpFileName, mode);
        if (ret)
            return ret;
        SLOG_FMTE("LoadLibraryA: dlopen failed for %s, error: %s", lpFileName, dlerror());
        return NULL;
    }

  private:
    std::mutex m_mutex;
    std::list<std::string> m_lstDirs;
    std::string m_userDllDir;
};

static DllLoader s_dllLoader;

HMODULE WINAPI LoadLibraryA(LPCSTR lpFileName)
{

    if (!lpFileName)
        return NULL;
    HMODULE ret = 0;
    do
    {
        ret = dlopen(lpFileName, RTLD_NOW);
        if (ret)
            break;
        if (GetFileAttributesA(lpFileName) != INVALID_FILE_ATTRIBUTES)
        {
            SLOG_FMTE("LoadLibraryA: dlopen failed for %s, error: %s", lpFileName, dlerror());
            break;
        }
        ret = s_dllLoader.LoadDll(lpFileName, RTLD_NOW);
        if (ret)
            break;
        char szPath[MAX_PATH];
        const char *ext = strrchr(lpFileName, '.');
        if (!ext)
        {
// no ext, add so as the extend name
#ifdef __APPLE__
            sprintf(szPath, "%s.dylib", lpFileName);
#else
            sprintf(szPath, "%s.so", lpFileName);
#endif
        }
        else if (stricmp(ext, ".dll") == 0)
        {
            // windows dll name pattern, change to libxxx.so
            sprintf(szPath, "lib%s", lpFileName);
#ifdef __APPLE__
            strcpy(szPath + 3 + (ext - lpFileName), ".dylib");
#else
            strcpy(szPath + 3 + (ext - lpFileName), ".so");
#endif
        }
        else
        {
            break;
        }
        ret = LoadLibraryA(szPath);
    } while (false);
    return ret;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR lpFileName)
{
    char szName[MAX_PATH];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, szName, MAX_PATH, NULL, NULL))
        return 0;
    return LoadLibraryA(szName);
}

DWORD WINAPI GetDllDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
    return s_dllLoader.GetDllDirectoryA(nBufferLength, lpBuffer);
}

BOOL WINAPI SetDllDirectoryA(LPCSTR lpPathName)
{
    return s_dllLoader.SetDllDirectoryA(lpPathName);
}

DWORD WINAPI GetDllDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    return s_dllLoader.GetDllDirectoryW(nBufferLength, lpBuffer);
}

BOOL WINAPI SetDllDirectoryW(LPCWSTR lpPathName)
{
    if (!lpPathName)
        return SetDllDirectoryA(NULL);
    std::string str;
    tostring(lpPathName, -1, str);
    return SetDllDirectoryA(str.c_str());
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

BOOL StrToInt64ExA(const char *str, DWORD flags, LONGLONG *ret)
{
    std::wstring wstr;
    towstring(str, -1, wstr);
    return StrToInt64ExW(wstr.c_str(), flags, ret);
}

BOOL StrToIntExA(const char *str, DWORD flags, INT *ret)
{
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
    pSysTime->wYear = now->tm_year + 1900;
    pSysTime->wMonth = now->tm_mon + 1;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = tv.tv_usec / 1000;
}

void GetSystemTime(SYSTEMTIME *pSysTime)
{
    struct timeval tvNow;
    gettimeofday(&tvNow, 0);

    struct tm *now = localtime(&tvNow.tv_sec);
    pSysTime->wYear = now->tm_year + 1900;
    pSysTime->wMonth = now->tm_mon + 1;
    pSysTime->wDayOfWeek = now->tm_wday;
    pSysTime->wDay = now->tm_mday;
    pSysTime->wHour = now->tm_hour;
    pSysTime->wMinute = now->tm_min;
    pSysTime->wSecond = now->tm_sec;
    pSysTime->wMilliseconds = tvNow.tv_usec / 1000;
}

static const int64_t EPOCH = ((int64_t)116444736000000000LL); //  1601-01-01 00:00:00
static void TimeT2FileTime(time_t t, FILETIME *pft)
{
    long long int ll = (long long int)t * 10000000LL + EPOCH;
    pft->dwLowDateTime = (uint32_t)ll;
    pft->dwHighDateTime = (uint32_t)(ll >> 32);
}

static time_t FileTime2TimeT(const FILETIME &ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // 减去偏移量并转换为秒
    ULARGE_INTEGER qwFileTime;
    qwFileTime.QuadPart = uli.QuadPart - EPOCH;
    return qwFileTime.QuadPart / 10000000;
}

BOOL LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime)
{
    time_t localTime = FileTime2TimeT(*lpLocalFileTime);
    // 转换为UTC time_t
    struct tm *utcTm = gmtime(&localTime);
    if (!utcTm)
    {
        return FALSE;
    }
    time_t utcTime = mktime(utcTm);
    if (utcTime == (time_t)-1)
    {
        return FALSE;
    }

    TimeT2FileTime(utcTime, lpFileTime);
    return TRUE;
}

BOOL FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
    time_t utcTime = FileTime2TimeT(*lpFileTime);
    struct tm *utcTm = localtime(&utcTime);
    if (!utcTm)
    {
        return FALSE;
    }
    time_t localTime = mktime(utcTm);
    if (localTime == (time_t)-1)
    {
        return FALSE;
    }
    TimeT2FileTime(localTime, lpLocalFileTime);
    return TRUE;
}

time_t _mkgmtime(struct tm *_Tm)
{
    return mktime(_Tm);
}

int _localtime64_s(struct tm *ptm, const __time64_t *ptime)
{
    *ptm = *localtime((time_t *)ptime);
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

LONG InterlockedDecrement(LONG volatile *v)
{
    return __atomic_fetch_sub(v, 1, __ATOMIC_SEQ_CST) - 1;
}

LONG InterlockedIncrement(LONG volatile *v)
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
    BOOL bRet = conn->getMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (bRet)
    {
        if (CallHook(WH_GETMESSAGE, HC_ACTION, 1, (LPARAM)lpMsg))
            return GetMessage(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }
    return bRet;
}

BOOL PeekMessage(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    if (!conn)
        return FALSE;
    BOOL bRet = conn->peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (bRet)
    {
        if (CallHook(WH_GETMESSAGE, HC_ACTION, wRemoveMsg & PM_REMOVE, (LPARAM)pMsg))
            return PeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }
    return bRet;
}

int GetSystemScale()
{
    int dpi = GetDpiForWindow(0);
    return dpi * 100 / 96;
}

int GetSystemMetrics(int nIndex)
{
    int ret = 0;
    SConnection *conn = SConnMgr::instance()->getConnection();
    HMONITOR hMonitor = conn->GetScreen(0);
    switch (nIndex)
    {
    case SM_CXSCREEN:
        return conn->GetScreenWidth(hMonitor);
    case SM_CYSCREEN:
        return conn->GetScreenHeight(hMonitor);
    case SM_CXBORDER:
    case SM_CYBORDER:
    case SM_CXEDGE:
    case SM_CYEDGE:
        return 1;
    case SM_CXSMICON:
        return 16;
    case SM_CYSMICON:
        return 16;
    case SM_CXICON:
        ret = 32;
        break;
    case SM_CYICON:
        ret = 32;
        break;
    case SM_CXCURSOR:
        ret = 24;
        break;
    case SM_CYCURSOR:
        ret = 24;
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
        ret = 4;
        break;
    case SM_CXMINTRACK:
    case SM_CYMINTRACK:
        ret = 6;
        break;
    default:
        printf("unknown index for GetSystemMetrics, index=%d\n", nIndex);
        break;
    }
#ifdef __APPLE__
    if (nIndex == SM_CXCURSOR || nIndex == SM_CYCURSOR)
        return ret;
#endif
    return ret * GetSystemScale() / 100;
}

#define PROC_EVENT_FMT "proc_event_E23A140E-2711-44CE-AE4E-67D55217FF7A_%u"

class ChildStatusMgr {
  public:
    ChildStatusMgr()
    {
    }

    void setPidCode(pid_t pid, int status)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_child_status[pid] = status;
    }

    BOOL getExitCode(pid_t pid, LPDWORD lpExitCode)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_child_status.find(pid);
        if (it == m_child_status.end())
            return FALSE;
        int status = it->second;

        if (WIFEXITED(status))
        {
            *lpExitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            *lpExitCode = -1;
        }
        return TRUE;
    }

  private:
    std::map<pid_t, int> m_child_status;
    std::mutex m_mutex;
} s_child_status_mgr;

static void sigchld_handler(int signo)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // notify child process was stopped.
        s_child_status_mgr.setPidCode(pid, status);
        char szName[100];
        sprintf(szName, PROC_EVENT_FMT, pid);
        HANDLE hEvent = CreateEventA(NULL, TRUE, FALSE, szName);
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }
}

static std::mutex s_mutex_sigchild;
static bool s_sigchild_flag = false;
int install_sigchld_handler()
{
    std::unique_lock<std::mutex> lock(s_mutex_sigchild);
    if (s_sigchild_flag)
        return 0;
    s_sigchild_flag = true;
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // 选项可以根据需要调整
    sigaction(SIGCHLD, &sa, NULL);
    return 1;
}

BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
    if (!lpExitCode)
        return FALSE;

    pid_t pid = GetProcessId(hProcess);
    if (!pid)
        return FALSE;
    return s_child_status_mgr.getExitCode(pid, lpExitCode);
}

#define PROC_STATUS "/proc/%d/status"
// 读取指定PID的进程状态文件，获取其有效用户ID
int WINAPI get_process_uid(int pid)
{
    char filename[64];
    FILE *file;
    char line[256];
    int uid = -1;

    // 构造状态文件路径
    snprintf(filename, sizeof(filename), PROC_STATUS, pid);

    // 打开状态文件
    file = fopen(filename, "r");
    if (!file)
    {
        perror("无法打开进程状态文件");
        return -1;
    }

    // 逐行读取文件内容，查找Uid字段
    while (fgets(line, sizeof(line), file))
    {
        if (strncmp(line, "Uid:", 4) == 0)
        {
            // 解析Uid字段，获取有效用户ID
            sscanf(line, "Uid:\t%d", &uid);
            break;
        }
    }

    fclose(file);
    return uid;
}

pid_t WINAPI GetCurrentProcessId()
{
    return getpid();
}

pid_t WINAPI GetProcessId(HANDLE hProcess)
{
    if (hProcess == INVALID_HANDLE_VALUE)
        return getpid();
    char szName[1001];
    if (!GetHandleName(hProcess, szName))
        return 0;
    pid_t pid;
    if (1 != sscanf(szName, PROC_EVENT_FMT, &pid))
        return 0;
    return pid;
}

HANDLE WINAPI GetCurrentProcess_Priv(void)
{
    return INVALID_HANDLE_VALUE; // return a pseudo handle
}

static void RedirectFd(HANDLE h, int fd2)
{
    if (!h)
        return;
    int fd = _open_osfhandle(h, 0);
    if (fd == -1)
        return;
    dup2(fd, fd2);
}

BOOL WINAPI CreateProcessAsUserA(HANDLE hToken, LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    if (!lpApplicationName)
    {
        if (!lpCommandLine)
            return FALSE;
        lpApplicationName = lpCommandLine;
        if (lpApplicationName[0] == '\"')
            lpCommandLine = strstr(lpCommandLine, "\" ");
        else
            lpCommandLine = strchr(lpCommandLine, ' ');
        if (lpCommandLine)
        {
            lpCommandLine += lpApplicationName[0] == '\"' ? 2 : 1;
            lpCommandLine[-1] = '\0';
        }
    }
    install_sigchld_handler();

    std::list<char *> lstArg;
    lstArg.push_back((char *)lpApplicationName);
    while (lpCommandLine)
    {
        char *pArgEnd = nullptr;
        if (lpCommandLine[0] == '\"')
        {
            lstArg.push_back(lpCommandLine + 1);
            pArgEnd = strstr(lpCommandLine + 1, "\" ");
            if (pArgEnd)
            {
                pArgEnd[0] = 0;
                pArgEnd += 2;
            }
            else
            {
                pArgEnd = strrchr(lpCommandLine + 1, '\"');
                if (pArgEnd)
                {
                    // for last param
                    pArgEnd[0] = 0;
                    pArgEnd = NULL;
                }
            }
        }
        else
        {
            lstArg.push_back(lpCommandLine);
            pArgEnd = strchr(lpCommandLine, ' ');
            if (pArgEnd)
            {
                pArgEnd[0] = 0;
                pArgEnd++;
            }
        }
        lpCommandLine = pArgEnd;
    }
    if (lstArg.size() > 1000)
        return FALSE; // 子进程退出
    pid_t pid = fork();
    if (pid == -1)
    {
        // fork 失败
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        // prepare env
        std::list<std::string> lstEnv;
        char szDisplay[200];
        char szAuth[200];
        GetEnvironmentVariableA("DISPLAY", szDisplay, 200);
        GetEnvironmentVariableA("XAUTHORITY", szAuth, 200);
        std::string strDisplay, strAuth;
        {
            std::stringstream ss;
            ss << "DISPLAY=" << szDisplay;
            strDisplay = ss.str();
        }
        {
            std::stringstream ss;
            ss << "XAUTHORITY=" << szAuth;
            strAuth = ss.str();
        }

        if (lpCurrentDirectory)
        {
            SetCurrentDirectoryA(lpCurrentDirectory);
        }
        if (lpEnvironment)
        {
            if (dwCreationFlags & CREATE_UNICODE_ENVIRONMENT)
            {
                LPCWSTR pszEnv = (LPCWSTR)lpEnvironment;
                while (*pszEnv)
                {
                    size_t len = wcslen(pszEnv);
                    std::string strEnv;
                    tostring(pszEnv, -1, strEnv);
                    lstEnv.push_back(strEnv);
                    pszEnv += len + 1;
                }
            }
            else
            {
                LPCSTR pszEnv = (LPCSTR)lpEnvironment;
                while (*pszEnv)
                {
                    size_t len = strlen(pszEnv);
                    lstEnv.push_back(pszEnv);
                    pszEnv += len + 1;
                }
            }
        }
        // notify parent that child process
        char szName[100];
        sprintf(szName, PROC_EVENT_FMT, getpid());
        HANDLE hProcess = CreateEventA(NULL, TRUE, FALSE, szName);
        SetEvent(hProcess);
        CloseHandle(hProcess);

        char *args[1024] = { 0 };
        size_t i = 0;
        std::string command;
        if ((UINT_PTR)hToken == Verb_RunAs)
        {
            const char *hosts[] = { "/usr/bin/pkexec", "/usr/bin/kdesu", "/usr/bin/gksu" };
            for (int j = 0; j < ARRAYSIZE(hosts); j++)
            {
                if (GetFileAttributesA(hosts[j]) != INVALID_FILE_ATTRIBUTES)
                {
                    command = hosts[j];
                    break;
                }
            }
            if (command.empty())
            {
                perror("no host found!");
                exit(EXIT_FAILURE);
            }
            args[i++] = (char *)command.c_str();
            static char szEnv[] = "env";
            args[i++] = szEnv;
            args[i++] = (char *)strDisplay.c_str();
            args[i++] = (char *)strAuth.c_str();
        }
        for (auto it = lstArg.begin(); it != lstArg.end(); it++)
        {
            args[i++] = *it;
        }
        char *envs[1001] = { 0 };
        i = 0;
        if ((UINT_PTR)hToken != Verb_RunAs)
        {
            envs[i++] = (char *)strDisplay.c_str();
            envs[i++] = (char *)strAuth.c_str();
        }
        for (auto it = lstEnv.begin(); it != lstEnv.end(); it++)
        {
            envs[i++] = (char *)(*it).c_str();
            if (i >= 1000)
                break;
        }
        if (lpStartupInfo)
        {
            // redirect stdin/out/err
            RedirectFd(lpStartupInfo->hStdInput, STDIN_FILENO);
            RedirectFd(lpStartupInfo->hStdOutput, STDOUT_FILENO);
            RedirectFd(lpStartupInfo->hStdError, STDERR_FILENO);
        }

        // child process
        execve(args[0], args, envs); // 替换子进程的代码为新程序
        perror("execvp failed");     // 如果 execvp 失败，打印错误信息
        exit(EXIT_FAILURE);          // 子进程退出
    }
    else
    {
        char szName[100];
        sprintf(szName, "proc_event_%u", pid);
        HANDLE hProcess = CreateEventA(NULL, TRUE, FALSE, szName);
        BOOL bRet = WaitForSingleObject(hProcess, 100) == WAIT_OBJECT_0;
        if (bRet)
        {
            if (lpProcessInformation)
            {
                lpProcessInformation->hProcess = hProcess;
                lpProcessInformation->hThread = INVALID_HANDLE_VALUE;
                lpProcessInformation->dwProcessId = pid;
                lpProcessInformation->dwThreadId = 0;
            }
        }
        else
        {
            CloseHandle(hProcess);
        }
        return bRet;
    }
}

BOOL WINAPI CreateProcessAsUserW(HANDLE hToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    std::string strApp, strCmd, strDir;
    tostring(lpApplicationName, -1, strApp);
    tostring(lpCommandLine, -1, strCmd);
    tostring(lpCurrentDirectory, -1, strDir);
    std::string strDesktop, strTitle;
    STARTUPINFOA startupINfoA;
    STARTUPINFOA *pStartInfoA = nullptr;
    if (lpStartupInfo)
    {
        pStartInfoA = &startupINfoA;
        if (lpStartupInfo->lpDesktop)
            tostring(lpStartupInfo->lpDesktop, -1, strDesktop);
        if (lpStartupInfo->lpTitle)
            tostring(lpStartupInfo->lpTitle, -1, strTitle);
        startupINfoA.cb = sizeof(startupINfoA);
        startupINfoA.lpReserved = NULL;
        startupINfoA.lpDesktop = lpStartupInfo->lpDesktop ? (char *)strDesktop.c_str() : nullptr;
        startupINfoA.lpTitle = lpStartupInfo->lpTitle ? (char *)strTitle.c_str() : nullptr;
        startupINfoA.dwX = lpStartupInfo->dwX;
        startupINfoA.dwY = lpStartupInfo->dwY;
        startupINfoA.dwXSize = lpStartupInfo->dwXSize;
        startupINfoA.dwYSize = lpStartupInfo->dwYSize;
        startupINfoA.dwXCountChars = lpStartupInfo->dwXCountChars;
        startupINfoA.dwYCountChars = lpStartupInfo->dwYCountChars;
        startupINfoA.dwFillAttribute = lpStartupInfo->dwFillAttribute;
        startupINfoA.dwFlags = lpStartupInfo->dwFlags;
        startupINfoA.wShowWindow = lpStartupInfo->wShowWindow;
        startupINfoA.cbReserved2 = 0;
        startupINfoA.lpReserved2 = nullptr;
        startupINfoA.hStdInput = lpStartupInfo->hStdInput;
        startupINfoA.hStdOutput = lpStartupInfo->hStdOutput;
        startupINfoA.hStdError = lpStartupInfo->hStdError;
    }
    return CreateProcessAsUserA(hToken, lpApplicationName ? strApp.c_str() : nullptr, lpCommandLine ? (char *)strCmd.c_str() : nullptr, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory ? strDir.c_str() : nullptr, pStartInfoA, lpProcessInformation);
}

BOOL WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    return CreateProcessAsUserA(0, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL WINAPI CreateProcessW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
    return CreateProcessAsUserW(0, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL GetKeyboardState(PBYTE lpKeyState)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyboardState(lpKeyState);
}

SHORT GetKeyState(int nVirtKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetKeyState(nVirtKey);
}

SHORT WINAPI GetAsyncKeyState(int vKey)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetAsyncKeyState(vKey);
}

UINT MapVirtualKey(UINT uCode, UINT uMapType)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->MapVirtualKey(uCode, uMapType);
}

UINT MapVirtualKeyEx(UINT uCode, UINT uMapType, HKL dwhkl)
{
    // todo:hjx
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
    if (CallHook(WH_SYSMSGFILTER, nCode, 0, (LPARAM)lpMsg))
        return TRUE;
    return CallHook(WH_MSGFILTER, nCode, 0, (LPARAM)lpMsg);
}

__time64_t _mktime64(const tm *ptime)
{
    return mktime((struct tm *)ptime);
}

__time64_t _time64(__time64_t *_Time)
{
#ifdef __x86_64
    return time((time_t *)_Time);
#else
    time_t tmp = _Time ? (*_Time) : 0;
    time_t ret = time(&tmp);
    if (_Time)
        *_Time = tmp;
    return ret;
#endif //__x86_64
}

HCURSOR LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName)
{
    return CursorMgr::loadCursor(lpCursorName);
}

HCURSOR LoadCursorW(HINSTANCE hInstance, LPCWSTR lpCursorName)
{
    if (IS_INTRESOURCE(lpCursorName))
        return LoadCursorA(hInstance, (LPCSTR)lpCursorName);
    std::string str;
    tostring(lpCursorName, -1, str);
    return CursorMgr::loadCursor(str.c_str());
}

BOOL DestroyCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    BOOL bRet = conn->DestroyCursor(hCursor);
    if (!bRet)
        return FALSE;
    return CursorMgr::destroyCursor(hCursor);
}

HCURSOR SetCursor(HCURSOR hCursor)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetCursor(0, hCursor);
}

HCURSOR GetCursor(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetCursor();
}

static __thread sigjmp_buf jump_buffer;

static void sigsegv_handler(int sig)
{
    //    printf("Caught signal %d\n", sig);
    siglongjmp(jump_buffer, 1);
}

BOOL IsBadReadPtr(const void *ptr, size_t size)
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
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1)
    {
        perror("sigaction");
        return FALSE;
    }
    BOOL bRet = FALSE;
    if (sigsetjmp(jump_buffer, 1) == 0)
    {
        volatile const char *p = (const char *)ptr;
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
    else
    {
        bRet = TRUE;
    }
    if (sigaction(SIGSEGV, &old_action, NULL) == -1)
    {
        perror("sigaction");
    }
    return bRet; // Invalid pointer
}

BOOL IsBadWritePtr(const void *ptr, size_t size)
{
    if (ptr == NULL || size == 0)
    {
        return 1; // Invalid pointer or size
    }
    struct sigaction new_action, old_action;
    new_action.sa_handler = sigsegv_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    if (sigaction(SIGSEGV, &new_action, &old_action) == -1)
    {
        perror("sigaction");
        return FALSE;
    }
    BOOL bRet = FALSE;
    if (sigsetjmp(jump_buffer, 1) == 0)
    {

        volatile char *p = (char *)ptr;
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
    else
    {
        bRet = TRUE;
    }
    if (sigaction(SIGSEGV, &old_action, NULL) == -1)
    {
        perror("sigaction");
    }
    // printf("IsBadWritePtr %p,len=%d,ret=%d\n",ptr,(int)size,bRet);
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
        char path[MAX_PATH] = { 0 };
        ssize_t len;
#ifdef __APPLE__
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) != 0)
        {
            perror("readlink");
            return 0;
        }
        len = strlen(path);
#else
        len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len == -1)
        {
            perror("readlink");
            return 0;
        }
#endif

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

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
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
    struct timeval usleep_tv;
    usleep_tv.tv_sec = 0;
    usleep_tv.tv_usec = dwMilliseconds * 1000;
    select(0, 0, 0, 0, &usleep_tv);
}

LONG CompareFileTime(const FILETIME *ft1, const FILETIME *ft2)
{
    if (ft1->dwHighDateTime < ft2->dwHighDateTime)
        return -1;
    if (ft1->dwHighDateTime > ft2->dwHighDateTime)
        return 1;
    if (ft1->dwLowDateTime < ft2->dwLowDateTime)
        return -1;
    if (ft1->dwLowDateTime > ft2->dwLowDateTime)
        return 1;
    return 0;
}

BOOL WINAPI SystemParametersInfoA(UINT action, UINT val, void *ptr, UINT winini)
{
    // todo:hjx
    switch (action)
    {
    case SPI_GETWHEELSCROLLLINES:
    {
        unsigned int *pv = (unsigned int *)ptr;
        *pv = 3;
        return TRUE;
    }
    default:
        return FALSE;
    }
}

BOOL WINAPI SystemParametersInfoW(UINT action, UINT val, void *ptr, UINT winini)
{
    return SystemParametersInfoA(action, val, ptr, winini);
}

void WINAPI DebugBreak(void)
{
    DbgBreakPoint();
}

VOID WINAPI DbgBreakPoint(VOID)
{
    assert(0);
}

UINT WINAPI GetDoubleClickTime(VOID)
{
    return SConnMgr::instance()->getConnection()->GetDoubleClickTime();
}

#define TICKSPERSEC 10000000

BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    lpFrequency->QuadPart = TICKSPERSEC;
    return TRUE;
}

#define SECS_1601_TO_1970 ((369 * 365 + 89) * (ULONGLONG)86400)

static inline ULONGLONG ticks_from_time_t(time_t time)
{
    if (sizeof(time_t) == sizeof(int)) /* time_t may be signed */
        return ((ULONGLONG)(ULONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
    else
        return ((ULONGLONG)time + SECS_1601_TO_1970) * TICKSPERSEC;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    struct timeval now;
    gettimeofday(&now, 0);
    lpPerformanceCount->QuadPart = ticks_from_time_t(now.tv_sec) + now.tv_usec * 10;
    return TRUE;
}

//---------------------------------------------------
UINT WINAPI RegisterClipboardFormatA(_In_ LPCSTR lpszFormat)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->RegisterClipboardFormatA(lpszFormat);
}

UINT WINAPI RegisterClipboardFormatW(_In_ LPCWSTR lpszFormat)
{
    std::string str;
    tostring(lpszFormat, -1, str);
    return RegisterClipboardFormatA(str.c_str());
}

BOOL WINAPI EmptyClipboard(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->EmptyClipboard();
}

BOOL WINAPI IsClipboardFormatAvailable(_In_ UINT format)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->IsClipboardFormatAvailable(format);
}

BOOL WINAPI OpenClipboard(_In_opt_ HWND hWndNewOwner)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->OpenClipboard(hWndNewOwner);
}

BOOL WINAPI CloseClipboard(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->CloseClipboard();
}

HWND WINAPI GetClipboardOwner(VOID)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardOwner();
}

HANDLE
WINAPI
GetClipboardData(_In_ UINT uFormat)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->GetClipboardData(uFormat);
}

HANDLE
WINAPI
SetClipboardData(_In_ UINT uFormat, _In_opt_ HANDLE hMem)
{
    SConnection *conn = SConnMgr::instance()->getConnection();
    return conn->SetClipboardData(uFormat, hMem);
}

//------------------------------------------------------------
BOOL WINAPI MessageBeep(_In_ UINT uType)
{
    // todo:hjx
    return FALSE;
}

DWORD
WINAPI
GetTempPathA(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPSTR lpBuffer)
{
    if (nBufferLength < 5)
        return 0;
    strcpy(lpBuffer, "/tmp");
    return 5;
}

DWORD
WINAPI
GetTempPathW(_In_ DWORD nBufferLength, _Out_writes_to_opt_(nBufferLength, return +1) LPWSTR lpBuffer)
{
    if (nBufferLength < 5)
        return 0;
    wcscpy(lpBuffer, L"/tmp");
    return 5;
}

UINT WINAPI GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName)
{
    std::string strPath, strPrefix;
    char szTmpFileName[MAX_PATH] = { 0 };
    tostring(lpPathName, -1, strPath);
    tostring(lpPrefixString, -1, strPrefix);
    int ret = GetTempFileNameA(strPath.c_str(), strPrefix.c_str(), uUnique, szTmpFileName);
    if (!ret)
        return 0;
    MultiByteToWideChar(CP_UTF8, 0, szTmpFileName, -1, lpTempFileName, MAX_PATH);
    return ret;
}

UINT WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT unique, LPSTR buffer)
{
    DWORD attr = GetFileAttributesA(lpPathName);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        SetLastError(ERROR_DIRECTORY);
        return 0;
    }

    strcpy(buffer, lpPathName);
    char *p = buffer + strlen(buffer);

    /* add a \, if there isn't one  */
    if ((p == buffer) || (p[-1] != '/'))
        *p++ = '/';

    if (lpPrefixString)
        for (int i = 3; (i > 0) && (*lpPrefixString); i--)
            *p++ = *lpPrefixString++;

    unique &= 0xffff;
    if (unique)
        sprintf_s(p, MAX_PATH - (p - buffer), "%x.tmp", unique);
    else
    {
        /* get a "random" unique number and try to create the file */
        HANDLE handle;
        UINT num = GetTickCount() & 0xffff;
        static UINT last;

        /* avoid using the same name twice in a short interval */
        if (last - num < 10)
            num = last + 1;
        if (!num)
            num = 1;
        unique = num;
        do
        {
            sprintf_s(p, MAX_PATH - (p - buffer), "%x.tmp", unique);
            handle = CreateFileA(buffer, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
            if (handle != INVALID_HANDLE_VALUE)
            { /* We created it */
                CloseHandle(handle);
                last = unique;
                break;
            }
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_SHARING_VIOLATION)
                break; /* No need to go on */
            if (!(++unique & 0xffff))
                unique = 1;
        } while (unique != num);
    }
    return unique;
}

BOOL IsValidCodePage(UINT CodePage)
{
    // todo:hjx
    return TRUE;
}

UINT WINAPI GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    return 0;
}

HKL ActivateKeyboardLayout(HKL hkl, UINT Flags)
{
    return 0;
}

UINT WINAPI GetACP(void)
{
    return CP_UTF8;
}

BOOL WINAPI IsDBCSLeadByte(BYTE c)
{
    return UTF8CharLength(c) > 1;
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName)
{
    if (lpModuleName)
    {
        void *hMod = dlopen(lpModuleName, RTLD_LAZY);
        if (hMod)
            dlclose(hMod);
        return (HMODULE)hMod;
    }
    else
    {
#ifdef __APPLE__
        uint32_t count = _dyld_image_count();
        assert(count >= 1);
        const struct mach_header *header = _dyld_get_image_header(0);
        if (!header)
        {
            return 0;
        }
        return (HMODULE)header;
#else
        char pathexe[MAX_PATH];
        GetModuleFileNameA(NULL, pathexe, MAX_PATH);
        FILE *fp;
        char path[1024];
        char line[1024];
        void *module_addr = NULL;

        // 打开 /proc/self/maps 文件
        fp = fopen("/proc/self/maps", "r");
        if (fp == NULL)
        {
            perror("fopen");
            return 0;
        }
        // 读取文件内容，查找包含当前进程可执行文件路径的行
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, pathexe) != NULL)
            {                                                  // 替换为你的可执行文件路径
                sscanf(line, "%lx", (UINT_PTR *)&module_addr); // 从行中读取模块地址
                break;
            }
        }
        fclose(fp);
        return (HMODULE)module_addr;
#endif //__APPLE__
    }
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName)
{
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
    {
        SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
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
    {
        SetLastError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    std::wstring wstr;
    towstring(value, -1, wstr);
    size_t len = wstr.length();
    if (len >= nSize)
        return len + 1;
    wcscpy(lpBuffer, wstr.c_str());
    return len;
}

//-----------------------------------------------------------
struct FdHandle : _SynHandle
{
    int fd;
    FdHandle(int _fd)
        : fd(_fd)
    {
        type = HFdHandle;
    }

    ~FdHandle()
    {
    }

    int getReadFd() override
    {
        return fd;
    }

    int getWriteFd() override
    {
        return fd;
    }

    bool init(LPCSTR pszName, void *initData) override
    {
        return false;
    }
    void lock() override
    {
    }
    void unlock() override
    {
    }
    void *getData() override
    {
        return nullptr;
    }
    LPCSTR getName() const override
    {
        return nullptr;
    }
};

class SOsHandleMgr {
    enum
    {
        kSpan_CheckFd = 1000, // time interval for check fd
    };

  public:
    SOsHandleMgr()
        : m_ts(0)
    {
    }

    ~SOsHandleMgr()
    {
        cleanup();
    }

    bool isValidFd(int fd)
    {
        int new_fd = dup(fd);
        if (new_fd == -1)
        {
            return false;
        }
        else
        {
            close(new_fd);
            return true;
        }
    }

    HANDLE fd2Handle(int fd)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        uint64_t now = GetTickCount64();
        if (m_ts != 0 && now - m_ts > kSpan_CheckFd)
        {
            m_ts = now;
            // time for check fd validation.
            auto it = m_fdMap.begin();
            while (it != m_fdMap.end())
            {
                auto it_bk = it++;
                if (!isValidFd(it_bk->first))
                {
                    CloseHandle(it_bk->second);
                    m_fdMap.erase(it_bk);
                }
            }
        }
        auto it = m_fdMap.find(fd);
        if (it != m_fdMap.end())
            return it->second;
        HANDLE hRet = NewSynHandle(new FdHandle(fd));
        m_fdMap.insert(std::make_pair(fd, hRet));
        return hRet;
    }

    void cleanup()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_fdMap.begin();
        while (it != m_fdMap.end())
        {
            CloseHandle(it->second);
            it++;
        }
        m_fdMap.clear();
    }

  private:
    std::map<int, HANDLE> m_fdMap;
    std::mutex m_mutex;
    uint64_t m_ts;
};

static SOsHandleMgr s_osHandleMgr;

HANDLE WINAPI _get_osfhandle(int fd)
{
    return s_osHandleMgr.fd2Handle(fd);
}

LPSTR WINAPI GetCommandLineA(void)
{
    static char cmdline[1024] = { 0 };
    if (cmdline[0] != 0)
        return cmdline;
#ifdef __APPLE__
    int argc = *_NSGetArgc();
    char ***argv = _NSGetArgv();
    char *p = cmdline;
    for (int i = 0; i < argc; i++)
    {
        size_t arg_len = strlen((*argv)[i]);

        if (strchr((*argv)[i], ' '))
        {
            if (p + arg_len + 2 - cmdline >= sizeof(cmdline) - 1)
                break;
            *p++ = '\"';
            strcpy(p, (*argv)[i]);
            p += arg_len;
            *p++ = '\"';
        }
        else
        {
            if (p + arg_len - cmdline >= sizeof(cmdline) - 1)
                break;
            strcpy(p, (*argv)[i]);
            p += arg_len;
        }
        if (i < argc - 1)
        {
            if (p - cmdline < sizeof(cmdline) - 1)
            {
                *p++ = ' ';
            }
        }
    }
    return cmdline;
#else
    FILE *file = fopen("/proc/self/cmdline", "r");
    if (!file)
    {
        perror("Failed to open cmdline file");
        return cmdline;
    }
    if (fgets(cmdline, sizeof(cmdline), file))
    {
        for (char *p = cmdline; *p; p++)
        {
            if (*p == '\0')
                *p = ' ';
        }
    }
    fclose(file);
    return cmdline;
#endif
}

LPWSTR WINAPI GetCommandLineW(void)
{
    static wchar_t cmdline[1024] = { 0 };
    if (cmdline[0] != 0)
        return cmdline;
    char *tmp = GetCommandLineA();
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, cmdline, 1024);
    return cmdline;
}

//------------------------------------------------------------------------------
HANDLE WINAPI FindFirstChangeNotificationW(LPCWSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    if (!lpPathName)
        return INVALID_HANDLE_VALUE;
    std::string str;
    tostring(lpPathName, -1, str);
    return FindFirstChangeNotificationA(str.c_str(), bWatchSubtree, dwNotifyFilter);
}

#ifdef __linux__
struct NotifyHandle : FdHandle
{
    std::list<int> lstWd;

    NotifyHandle(int _fd)
        : FdHandle(_fd)
    {
        type = HNotifyHandle;
    }

    ~NotifyHandle()
    {
        for (auto wd : lstWd)
        {
            inotify_rm_watch(fd, wd);
        }
        lstWd.clear();
        close(fd);
    }
};

static void add_path_watch(NotifyHandle *pHandle, const char *path, BOOL bWatchSubtree, uint32_t mask)
{
    int wd = inotify_add_watch(pHandle->fd, path, mask);
    if (wd == -1)
    {
        return;
    }
    pHandle->lstWd.push_back(wd);
    if (!bWatchSubtree)
        return;
    // 监视子目录
    DIR *dir = opendir(path);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 忽略 "." 和 ".."
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (entry->d_type == DT_DIR)
            {
                // 递归监视子目录
                add_path_watch(pHandle, full_path, TRUE, mask);
            }
        }
    }

    closedir(dir);
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    uint32_t mask = 0;
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_FILE_NAME)
    {
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_DIR_NAME)
    {
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_SIZE)
    {
        mask |= IN_MODIFY;
    }
    if (dwNotifyFilter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
    {
        mask |= IN_MODIFY;
    }

    if (!lpPathName || mask == 0)
        return INVALID_HANDLE_VALUE;
    int fd = inotify_init();
    if (fd == -1)
    {
        return INVALID_HANDLE_VALUE;
    }
    NotifyHandle *notifyHandle = new NotifyHandle(fd);
    add_path_watch(notifyHandle, lpPathName, bWatchSubtree, mask);
    if (notifyHandle->lstWd.empty())
    {
        delete notifyHandle;
        return INVALID_HANDLE_VALUE;
    }
    return NewSynHandle(notifyHandle);
}

BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle)
{
    _SynHandle *synHandle = GetSynHandle(hChangeHandle);
    if (!synHandle || synHandle->getType() != HNotifyHandle)
        return FALSE;
    NotifyHandle *notifyHandle = (NotifyHandle *)synHandle;

    struct inotify_event evt;
    int evt_len = FIELD_OFFSET(struct inotify_event, name);
    if (read(notifyHandle->getReadFd(), &evt, evt_len) == evt_len)
    {
        char szBuf[1024];
        int remain = evt.len;
        for (; remain > 0;)
        {
            int len = std::min(1024, remain);
            int readed = read(notifyHandle->getReadFd(), szBuf, len);
            assert(readed == len);
            remain -= readed;
        }
        return TRUE;
    }
    return FALSE;
}

#elif defined(__APPLE__) && defined(__MACH__)
struct NotifyHandle : FdHandle
{
    std::vector<int> watchedFds; // 监控的文件描述符列表
    std::string rootPath;        // 根路径
    BOOL watchSubtree;           // 是否监控子目录
    DWORD notifyFilter;          // 通知过滤器

    NotifyHandle(int kq)
        : FdHandle(kq)
    {
        type = HNotifyHandle;
    }

    ~NotifyHandle()
    {
        // 关闭所有监控的文件描述符
        for (int watchFd : watchedFds)
        {
            close(watchFd);
        }
        watchedFds.clear();

        // 关闭kqueue
        if (fd != -1)
        {
            close(fd);
        }
    }
};

// 添加单个路径到kqueue监控
static bool add_path_to_kqueue(NotifyHandle *handle, const char *path)
{
    int pathFd = open(path, O_RDONLY);
    if (pathFd == -1)
    {
        return false;
    }

    struct kevent kev;
    uint32_t fflags = 0;

    // 根据通知过滤器设置kqueue事件标志
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_FILE_NAME)
    {
        fflags |= NOTE_DELETE | NOTE_RENAME;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_DIR_NAME)
    {
        fflags |= NOTE_DELETE | NOTE_RENAME;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_ATTRIBUTES)
    {
        fflags |= NOTE_ATTRIB;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_SIZE)
    {
        fflags |= NOTE_EXTEND | NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_LAST_WRITE)
    {
        fflags |= NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_LAST_ACCESS)
    {
        fflags |= NOTE_WRITE; // macOS kqueue没有直接的访问时间监控
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_CREATION)
    {
        fflags |= NOTE_WRITE;
    }
    if (handle->notifyFilter & FILE_NOTIFY_CHANGE_SECURITY)
    {
        fflags |= NOTE_ATTRIB;
    }

    // 如果没有指定任何过滤器，监控所有变化
    if (fflags == 0)
    {
        fflags = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_RENAME;
    }

    EV_SET(&kev, pathFd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, (void *)(LONG_PTR)pathFd);

    if (kevent(handle->fd, &kev, 1, NULL, 0, NULL) == -1)
    {
        close(pathFd);
        return false;
    }

    handle->watchedFds.push_back(pathFd);
    return true;
}

// 递归添加目录及其子目录到监控
static void add_directory_watch(NotifyHandle *handle, const char *dirPath)
{
    // 添加当前目录
    if (!add_path_to_kqueue(handle, dirPath))
    {
        return;
    }

    // 如果不需要监控子目录，直接返回
    if (!handle->watchSubtree)
    {
        return;
    }

    // 遍历子目录
    DIR *dir = opendir(dirPath);
    if (!dir)
    {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 跳过 "." 和 ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char fullPath[PATH_MAX];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);

        struct stat statBuf;
        if (stat(fullPath, &statBuf) == 0 && S_ISDIR(statBuf.st_mode))
        {
            // 递归添加子目录
            add_directory_watch(handle, fullPath);
        }
    }

    closedir(dir);
}

HANDLE WINAPI FindFirstChangeNotificationA(LPCSTR lpPathName, BOOL bWatchSubtree, DWORD dwNotifyFilter)
{
    if (!lpPathName)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    // 创建kqueue
    int kq = kqueue();
    if (kq == -1)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_HANDLE_VALUE;
    }

    // 创建通知句柄
    NotifyHandle *notifyHandle = new NotifyHandle(kq);
    notifyHandle->rootPath = lpPathName;
    notifyHandle->watchSubtree = bWatchSubtree;
    notifyHandle->notifyFilter = dwNotifyFilter;

    // 检查路径是否存在
    struct stat statBuf;
    if (stat(lpPathName, &statBuf) != 0)
    {
        delete notifyHandle;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    // 根据路径类型添加监控
    if (S_ISDIR(statBuf.st_mode))
    {
        // 目录
        add_directory_watch(notifyHandle, lpPathName);
    }
    else
    {
        // 文件
        if (!add_path_to_kqueue(notifyHandle, lpPathName))
        {
            delete notifyHandle;
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
    }

    // 检查是否成功添加了任何监控
    if (notifyHandle->watchedFds.empty())
    {
        delete notifyHandle;
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }

    return NewSynHandle(notifyHandle);
}

BOOL WINAPI FindNextChangeNotification(HANDLE hChangeHandle)
{
    _SynHandle *synHandle = GetSynHandle(hChangeHandle);
    if (!synHandle || synHandle->getType() != HNotifyHandle)
        return FALSE;

    NotifyHandle *notifyHandle = (NotifyHandle *)synHandle;

    // 读取并处理kqueue事件
    struct kevent events[10];
    struct timespec timeout = { 0, 0 }; // 非阻塞读取

    int nEvents = kevent(notifyHandle->fd, NULL, 0, events, 10, &timeout);
    if (nEvents <= 0)
    {
        return FALSE; // 没有事件或出错
    }

    // 处理所有事件，清空事件队列
    for (int i = 0; i < nEvents; i++)
    {
        struct kevent &event = events[i];

        // 检查是否是我们监控的文件描述符
        bool isWatchedFd = false;
        for (int watchFd : notifyHandle->watchedFds)
        {
            if (event.ident == (uintptr_t)watchFd)
            {
                isWatchedFd = true;
                break;
            }
        }

        if (isWatchedFd && (event.fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB)))
        {
            // 找到了匹配的文件系统变化事件
            // 继续读取剩余事件以清空队列，但不处理
            continue;
        }
    }

    return TRUE; // 成功处理了事件
}

#else
#pragma message("unsupport os")
#endif
BOOL WINAPI FindCloseChangeNotification(HANDLE hChangeHandle)
{
    return CloseHandle(hChangeHandle);
}

struct ThreadInfo
{
    ThreadInfo()
    {
        hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
        hEventResume = CreateEventA(NULL, FALSE, FALSE, NULL);
    }
    ~ThreadInfo()
    {
        CloseHandle(hEvent);
        CloseHandle(hEventResume);
    }
    HANDLE hEvent;
    HANDLE hEventResume;
};

struct ThreadObj
    : PipeSynHandle
    , ThreadInfo
{
    pthread_t thread;
    ThreadObj()
        : thread(0)
    {
        type = HThread;
        init(NULL, NULL);
    }
    ~ThreadObj()
    {
        if (thread != 0)
        {
            pthread_join(thread, nullptr);
        }
    }
    void *getData() override
    {
        return (ThreadInfo *)this;
    }
};

struct ThreadParam
{
    ThreadParam(LPTHREAD_START_ROUTINE _lpStartAddress, LPVOID _lpParameter, DWORD _dwCreationFlags, tid_t *_lpThreadId)
        : lpStartAddress(_lpStartAddress)
        , lpParameter(_lpParameter)
        , dwCreationFlags(_dwCreationFlags)
        , lpThreadId(_lpThreadId)
    {
    }
    LPTHREAD_START_ROUTINE lpStartAddress;
    LPVOID lpParameter;
    DWORD dwCreationFlags;
    tid_t *lpThreadId;
    ThreadObj *info;
};

static void *Swinx_ThreadProc(void *p)
{
    ThreadParam *param = (ThreadParam *)p;
    if (param->lpThreadId)
        *param->lpThreadId = GetCurrentThreadId();
    SetEvent(param->info->hEvent);
    if (param->dwCreationFlags & CREATE_SUSPENDED)
    {
        // SLOG_STMI() << "waiting for resume";
        WaitForSingleObject(param->info->hEventResume, INFINITE);
        // SLOG_STMI() << "waiting for resume done";
    }
    param->lpStartAddress(param->lpParameter);
    delete param;
    param->info->writeSignal(); // wakeup waitings for the thread object.
    return nullptr;
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, tid_t *lpThreadId)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (dwStackSize != 0)
    {
        pthread_attr_setstacksize(&attr, dwStackSize);
    }

    ThreadParam *param = new ThreadParam(lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
    ThreadObj *trdObj = new ThreadObj();
    param->info = trdObj;
    if (pthread_create(&trdObj->thread, &attr, Swinx_ThreadProc, param) != 0)
    {
        SLOG_STME() << "Failed to create thread";
        delete param;
        delete trdObj;
        return INVALID_HANDLE_VALUE;
    }
    WaitForSingleObject(trdObj->hEvent, INFINITE);
    return NewSynHandle(trdObj);
}

// only support resume thread that was created with flag CREATE_SUSPENDED
DWORD WINAPI ResumeThread(HANDLE hThread)
{
    if (hThread == INVALID_HANDLE_VALUE)
        return -1;
    _SynHandle *synHandle = GetSynHandle(hThread);
    if (!synHandle || synHandle->getType() != HThread)
        return -1;
    ThreadObj *threadObj = (ThreadObj *)synHandle;
    SetEvent(threadObj->hEventResume);
    // SLOG_STMI() << "resume thread done";
    return 0;
}
