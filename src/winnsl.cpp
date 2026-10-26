#include <sfr_config.h>
#include <windows.h>
#include <winnls.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "tostring.h"
#include "SConnection.h"

#ifdef __linux__
#include <locale.h>
#include <langinfo.h>
#endif // __linux__

#ifdef __APPLE__
int GetLocal(char *lpCData, int nSize);
#endif // __APPLE__

// Helper function to convert POSIX locale string to Windows LCID format
static LCID PosixLocaleToLCID(const char *localeName)
{
    if (!localeName || localeName[0] == '\0')
        return MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), SORT_DEFAULT);

    std::string loc(localeName);

    // Convert to lowercase for easier comparison
    std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);

    // Extract language part (before underscore or dot)
    std::string lang;
    size_t pos = loc.find('_');
    if (pos != std::string::npos)
        lang = loc.substr(0, pos);
    else
    {
        pos = loc.find('.');
        if (pos != std::string::npos)
            lang = loc.substr(0, pos);
        else
            lang = loc;
    }

    // Map common locale names to LANGIDs
    WORD primaryLang = LANG_ENGLISH; // Default

    if (lang == "zh" || lang == "chinese")
        primaryLang = LANG_CHINESE;
    else if (lang == "en" || lang == "english")
        primaryLang = LANG_ENGLISH;
    else if (lang == "ja" || lang == "japanese")
        primaryLang = LANG_JAPANESE;
    else if (lang == "ko" || lang == "korean")
        primaryLang = LANG_KOREAN;
    else if (lang == "fr" || lang == "french")
        primaryLang = LANG_FRENCH;
    else if (lang == "de" || lang == "german")
        primaryLang = LANG_GERMAN;
    else if (lang == "es" || lang == "spanish")
        primaryLang = LANG_SPANISH;
    else if (lang == "it" || lang == "italian")
        primaryLang = LANG_ITALIAN;
    else if (lang == "ru" || lang == "russian")
        primaryLang = LANG_RUSSIAN;
    else if (lang == "pt" || lang == "portuguese")
        primaryLang = LANG_PORTUGUESE;

    return MAKELCID(MAKELANGID(primaryLang, SUBLANG_DEFAULT), SORT_DEFAULT);
}

// Helper function to get current user locale
static LCID GetUserLocaleLCID()
{
#ifdef __APPLE__
    char szLCData[LOCALE_NAME_MAX_LENGTH] = { 0 };
    int ret = GetLocal(szLCData, LOCALE_NAME_MAX_LENGTH);
    if (ret == 0)
    {
        strcpy(szLCData, "en_US.UTF-8");
    }
    return PosixLocaleToLCID(szLCData);
#else
    const char *locale = setlocale(LC_ALL, NULL);
    if (!locale)
        locale = getenv("LANG");
    if (!locale)
        locale = getenv("LC_ALL");
    if (!locale)
        locale = "en_US.UTF-8"; // Fallback to English

    return PosixLocaleToLCID(locale);
#endif
}

HKL GetKeyboardLayout(int idx)
{
    return SConnMgr::instance()->getConnection()->GetKeyboardLayout((DWORD)idx);
}

int LCMapStringW(LCID Locale __attribute__((unused)), DWORD dwMapFlags __attribute__((unused)), LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest)
{
    if (cchSrc == cchDest)
    {
        memcpy(lpDestStr, lpSrcStr, cchDest * sizeof(wchar_t));
    }
    return 1;
}

int LCMapStringA(LCID Locale __attribute__((unused)), DWORD dwMapFlags __attribute__((unused)), LPCSTR lpSrcStr, int cchSrc, LPSTR lpDestStr, int cchDest)
{
    if (cchSrc == cchDest)
    {
        memcpy(lpDestStr, lpSrcStr, cchDest);
    }
    return 1;
}

/******************************************************************************
 *	CompareStringW   (kernelbase.@)
 */
INT WINAPI CompareStringW(LCID lcid __attribute__((unused)), DWORD flags __attribute__((unused)), const WCHAR *str1, int len1, const WCHAR *str2, int len2)
{
    std::wstring p1(str1, len1), p2(str2, len2);
    return p1.compare(p2);
}

INT WINAPI CompareStringA(LCID lcid __attribute__((unused)), DWORD flags __attribute__((unused)), const char *str1, int len1, const char *str2, int len2)
{
    std::string p1(str1, len1), p2(str2, len2);
    return p1.compare(p2);
}

/* Split a POSIX locale name ("zh_CN.UTF-8", "en_US", "C") into the ISO 639
 * language and ISO 3166 country parts, which is what GetLocaleInfo() returns
 * for LOCALE_SISO639LANGNAME / LOCALE_SISO3166CTRYNAME. */
static void splitLocaleName(const char *locale, std::string &language, std::string &country)
{
    language.clear();
    country.clear();
    if (!locale || !*locale)
        return;

    std::string name(locale);
    // Drop any encoding or modifier suffix (".UTF-8", "@euro", ...).
    size_t suffix = name.find_first_of("._@");
    if (suffix != std::string::npos)
        name.resize(suffix);

    size_t underscore = name.find('_');
    if (underscore == std::string::npos)
    {
        language = name;
        return;
    }
    language = name.substr(0, underscore);
    country = name.substr(underscore + 1);
}

/* LOCALE_RETURN_NUMBER makes GetLocaleInfo() exchange a DWORD instead of a
 * string. Only the numeric locale types WKE asks for are answered here. */
static BOOL numericLocaleInfo(LCID locale, LCTYPE lcType, DWORD *value)
{
    switch (lcType & ~LOCALE_RETURN_NUMBER)
    {
    case LOCALE_ILANGUAGE:
        *value = (DWORD)(locale & 0xFFFF);
        return TRUE;
    case LOCALE_IDEFAULTANSICODEPAGE:
    case LOCALE_IDEFAULTCODEPAGE:
        // POSIX hosts have no legacy ANSI/OEM code page: text is UTF-8.
        *value = CP_UTF8;
        return TRUE;
    default:
        return FALSE;
    }
}

int GetLocaleInfoA(LCID Locale, LCTYPE LCType, LPSTR lpLCData, int cchData)
{
    if (LCType & LOCALE_RETURN_NUMBER)
    {
        DWORD value;
        if (!numericLocaleInfo(Locale, LCType, &value))
            return 0;
        if (cchData < (int)sizeof(DWORD))
            return 0;
        memcpy(lpLCData, &value, sizeof(DWORD));
        return sizeof(DWORD) / sizeof(char);
    }

#ifdef __APPLE__
    char szLCData[LOCALE_NAME_MAX_LENGTH] = { 0 };
    GetLocal(szLCData, LOCALE_NAME_MAX_LENGTH);
    const char *locale = szLCData;
#elif defined(__linux__)
    // For now, return locale name as string
    const char *locale = setlocale(LC_ALL, NULL);
    if (!locale)
        locale = "C";
#elif SFR_PLATFORM
    // bare metal: no OS locale; fixed "C"
    const char *locale = "C";
#else
#error "Unsupported platform"
    return 0;
#endif

    std::string value;
    if (LCType == LOCALE_SISO639LANGNAME || LCType == LOCALE_SISO3166CTRYNAME)
    {
        std::string language, country;
        splitLocaleName(locale, language, country);
        if (LCType == LOCALE_SISO639LANGNAME)
            value = language;
        else
            value = country;
    }
    else
        value = locale;

    // Win32 fails for a locale part that does not exist (e.g. the country of a
    // neutral locale), and callers rely on that to fall back.
    if (value.empty())
        return 0;

    int len = (int)value.size();
    if (len >= cchData)
        return 0; // Buffer too small

    memcpy(lpLCData, value.c_str(), len);
    lpLCData[len] = '\0';
    return len + 1;
}

int GetLocaleInfoW(LCID Locale, LCTYPE LCType, LPWSTR lpLCData, int cchData)
{
    if (LCType & LOCALE_RETURN_NUMBER)
    {
        DWORD value;
        if (!numericLocaleInfo(Locale, LCType, &value))
            return 0;
        if (cchData < (int)(sizeof(DWORD) / sizeof(WCHAR)))
            return 0;
        memcpy(lpLCData, &value, sizeof(DWORD));
        return sizeof(DWORD) / sizeof(WCHAR);
    }

    char szLCData[LOCALE_NAME_MAX_LENGTH];
    int nRet = GetLocaleInfoA(Locale, LCType, szLCData, sizeof(szLCData));
    std::wstring wsLCData;
    towstring(szLCData, nRet, wsLCData);
    if (!lpLCData || cchData <= 0)
    {
        return wsLCData.length() + 1;
    }
    if ((size_t)cchData > wsLCData.length())
    {
        wcscpy(lpLCData, wsLCData.c_str());
        return wsLCData.length() + 1;
    }
    else
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
}

LCID WINAPI GetUserDefaultLCID(void)
{
    return GetUserLocaleLCID();
}

LCID WINAPI GetSystemDefaultLCID(void)
{
    return MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL), SORT_DEFAULT);
}

/***********************************************************************
 *	GetUserDefaultLangID   (kernelbase.@)
 */
LANGID WINAPI GetUserDefaultLangID(void)
{
    LCID lcid = GetUserLocaleLCID();
    return LANGIDFROMLCID(lcid);
}

LANGID WINAPI GetSystemDefaultLangID(void)
{
    LCID lcid = GetSystemDefaultLCID();
    return LANGIDFROMLCID(lcid);
}

LANGID WINAPI GetUserDefaultUILanguage()
{
    return GetUserDefaultLangID();
}
