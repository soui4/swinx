#ifndef __TOSTRING_H_
#define __TOSTRING_H_
#include <windows.h>
#include <string>

bool tostring(LPCWSTR pszText, int cLen, swinx_stl::string& str);

bool towstring(LPCSTR pszText, int cLen,std::wstring &str);

// 特殊的过滤器字符串转换函数，处理双NULL终止的字符串
bool tostring_filter(LPCWSTR pszFilter, swinx_stl::string& str);

#endif//__TOSTRING_H_