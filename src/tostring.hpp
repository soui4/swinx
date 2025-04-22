
#include <string>
#include <sysapi.h>

static bool tostring(LPCWSTR pszText, int cLen, std::string& str) {
    if (!pszText)
        return false;
    if (cLen < 0) cLen = wcslen(pszText);
    int len = WideCharToMultiByte(CP_UTF8, 0, pszText, cLen, nullptr, 0, nullptr, nullptr);
    str.resize(len);
    WideCharToMultiByte(CP_UTF8, 0, pszText, cLen, (char*)str.data(), len, nullptr, nullptr);
    return true;
}

static bool towstring(LPCSTR pszText, int cLen,std::wstring &str){
    if (!pszText)
        return false;
    if (cLen < 0) cLen = strlen(pszText);
    int len = MultiByteToWideChar(CP_UTF8, 0, pszText, cLen, nullptr, 0);
    str.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, pszText, cLen, (wchar_t*)str.data(), len);
    return true;
}