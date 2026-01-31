
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

// 特殊的过滤器字符串转换函数，处理双NULL终止的字符串
static bool tostring_filter(LPCWSTR pszFilter, std::string& str) {
    if (!pszFilter) return false;

    str.clear();
    const wchar_t* p = pszFilter;

    // 计算整个过滤器字符串的长度（包括中间的NULL字符）
    int totalLen = 0;
    while (true) {
        int segLen = wcslen(p);
        if (segLen == 0) break; // 遇到双NULL终止
        totalLen += segLen + 1; // +1 for the NULL terminator
        p += segLen + 1;
    }

    if (totalLen > 0) {
        // 转换整个过滤器字符串
        int len = WideCharToMultiByte(CP_UTF8, 0, pszFilter, totalLen, nullptr, 0, nullptr, nullptr);
        str.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, pszFilter, totalLen, (char*)str.data(), len, nullptr, nullptr);
    }

    return true;
}