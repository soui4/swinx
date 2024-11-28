#include <windows.h>
#include <winnls.h>
#include <string>
//todo:hjx
HKL GetKeyboardLayout(int idx) {
	return 0;
}

int GetLocaleInfoA(
	LCID   Locale,
	LCTYPE LCType,
	LPSTR  lpLCData,
	int    cchData
) {
	return 0;
}

int GetLocaleInfoW(
	LCID   Locale,
	LCTYPE LCType,
	LPWSTR  lpLCData,
	int    cchData
) {
	return 0;
}

int LCMapStringW(
	LCID    Locale,
	DWORD   dwMapFlags,
	LPCWSTR lpSrcStr,
	int     cchSrc,
	LPWSTR  lpDestStr,
	int     cchDest
) {
	if (cchSrc == cchDest) {
		memcpy(lpDestStr, lpSrcStr, cchDest*sizeof(wchar_t));
	}
	return 1;
}

int LCMapStringA(
	LCID    Locale,
	DWORD   dwMapFlags,
	LPCSTR lpSrcStr,
	int     cchSrc,
	LPSTR  lpDestStr,
	int     cchDest
) {
	if (cchSrc == cchDest) {
		memcpy(lpDestStr, lpSrcStr, cchDest);
	}
	return 1;

}


/******************************************************************************
 *	CompareStringW   (kernelbase.@)
 */
INT WINAPI CompareStringW(LCID lcid, DWORD flags, const WCHAR* str1, int len1,
	const WCHAR* str2, int len2)
{
	std::wstring p1(str1, len1), p2(str2, len2);
	return p1.compare(p2);
}

INT WINAPI CompareStringA(LCID lcid, DWORD flags, const char* str1, int len1,
	const char* str2, int len2)
{
	std::string p1(str1, len1), p2(str2, len2);
	return p1.compare(p2);
}


/***********************************************************************
 *	GetUserDefaultLCID   (kernelbase.@)
 */
LCID WINAPI GetUserDefaultLCID(void)
{
	LCID lcid=0;
	//NtQueryDefaultLocale(TRUE, &lcid);
	return lcid;
}

LCID WINAPI GetSystemDefaultLCID(void) {
	return 0;
}

/***********************************************************************
 *	GetUserDefaultLangID   (kernelbase.@)
 */
LANGID WINAPI GetUserDefaultLangID(void)
{
	return LANGIDFROMLCID(GetUserDefaultLCID());
}

LANGID WINAPI GetSystemDefaultLangID(void) {
	return GetUserDefaultLangID();
}

