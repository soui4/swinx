#ifndef __TOSTRING_H_
#define __TOSTRING_H_
#include <windows.h>
#include <string>

bool tostring(LPCWSTR pszText, int cLen, std::string& str);

bool towstring(LPCSTR pszText, int cLen,std::wstring &str);

// 特殊的过滤器字符串转换函数，处理双NULL终止的字符串
bool tostring_filter(LPCWSTR pszFilter, std::string& str);

#endif//__TOSTRING_H_