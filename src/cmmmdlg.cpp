#include <windows.h>
#include <commdlg.h>
#include "tostring.hpp"
#include "log.h"
#define kLogTag "commondlg"

extern BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode);
extern BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out);
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

static BOOL _GetOpenFileNameW(LPOPENFILENAMEW p, DlgMode mode)
{
    std::string strFilter, strCustomFilter, strFile, strFileTitle, strInitDir, strTitle, strDefExt;
    tostring_filter(p->lpstrFilter, strFilter);
    tostring(p->lpstrCustomFilter, -1, strCustomFilter);
    tostring(p->lpstrFileTitle, -1, strFileTitle);
    tostring(p->lpstrInitialDir, -1, strInitDir);
    tostring(p->lpstrTitle, -1, strTitle);
    tostring(p->lpstrDefExt, -1, strDefExt);
    OPENFILENAMEA openFile;
    openFile.lStructSize = sizeof(openFile);
    openFile.hwndOwner = p->hwndOwner;
    openFile.hInstance = p->hInstance;
    openFile.lpstrFilter = strFilter.empty() ? nullptr : strFilter.c_str();
    openFile.nMaxCustFilter = p->nMaxCustFilter;
    openFile.nFilterIndex = p->nFilterIndex;
    openFile.lpstrFile = new char[p->nMaxFile];
    openFile.lpstrFile[0] = 0;
    openFile.nMaxFile = p->nMaxFile;
    openFile.lpstrFileTitle = new char[p->nMaxFileTitle];
    openFile.lpstrFileTitle[0] = 0;
    openFile.nMaxFileTitle = p->nMaxFileTitle;
    openFile.lpstrInitialDir = strInitDir.c_str();
    openFile.lpstrTitle = strTitle.c_str();
    openFile.Flags = p->Flags;
    openFile.nFileExtension = p->nFileExtension;
    openFile.lpstrDefExt = strDefExt.c_str();
    openFile.lCustData = p->lCustData;
    openFile.FlagsEx = p->FlagsEx;

    BOOL ret = SGetOpenFileNameA(&openFile, mode);
    {
        std::wstring str;
        towstring(openFile.lpstrFileTitle, -1, str);
        delete[] openFile.lpstrFileTitle;
        if (str.length() < p->nMaxFileTitle)
        {
            wcscpy(p->lpstrFileTitle, str.c_str());
        }
    }
    {
        std::wstring str;
        towstring(openFile.lpstrFile, -1, str);
        delete[] openFile.lpstrFile;
        if (str.length() < p->nMaxFile)
        {
            wcscpy(p->lpstrFile, str.c_str());
        }
        else
        {
            ret = FALSE;
        }
        if (openFile.nFileOffset)
        {
            p->nFileOffset = MultiByteToWideChar(CP_UTF8, 0, openFile.lpstrFile, openFile.nFileOffset, nullptr, 0);
        }
    }
    return ret;
}

BOOL GetOpenFileNameA(LPOPENFILENAMEA p)
{
    return SGetOpenFileNameA(p, OPEN);
}

BOOL GetOpenFileNameW(LPOPENFILENAMEW p)
{
    return _GetOpenFileNameW(p, OPEN);
}

BOOL GetSaveFileNameA(LPOPENFILENAMEA p)
{
    return SGetOpenFileNameA(p, SAVE);
}
BOOL GetSaveFileNameW(LPOPENFILENAMEW p)
{
    return _GetOpenFileNameW(p, SAVE);
    ;
}

BOOL ChooseColorA(LPCHOOSECOLORA p)
{
    return SChooseColor(p->hwndOwner, p->lpCustColors, &p->rgbResult);
}

BOOL ChooseColorW(LPCHOOSECOLORW p)
{
    return SChooseColor(p->hwndOwner, p->lpCustColors, &p->rgbResult);
}

BOOL WINAPI PickFolderA(_In_ LPBROWSEINFOA lpbi)
{
    if (!lpbi->nMaxPath || !lpbi->lpszPath)
        return FALSE;
    OPENFILENAMEA of = { 0 };
    ;
    of.nMaxFile = lpbi->nMaxPath;
    of.lpstrFile = lpbi->lpszPath;
    of.hwndOwner = lpbi->hwndOwner;
    of.lpstrInitialDir = lpbi->strlRoot;
    return SGetOpenFileNameA(&of, FOLDER);
}

BOOL WINAPI PickFolderW(_In_ LPBROWSEINFOW lpbi)
{
    if (!lpbi->nMaxPath || !lpbi->lpszPath)
        return FALSE;
    OPENFILENAMEW of = { 0 };
    of.nMaxFile = lpbi->nMaxPath;
    of.lpstrFile = lpbi->lpszPath;
    of.hwndOwner = lpbi->hwndOwner;
    of.lpstrInitialDir = lpbi->strlRoot;
    return _GetOpenFileNameW(&of, FOLDER);
}