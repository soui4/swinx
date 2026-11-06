#include <windows.h>
#include <commdlg.h>
#include "tostring.h"
#include "SwinxUtils.h"
#include "log.h"
#include <new>
#include <string>
#include <vector>
#define kLogTag "commondlg"

extern BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode);
extern BOOL SChooseColor(HWND parent, const COLORREF initClr[16], COLORREF *out);

/* lpstrFilter 的分组上限。这个串是"描述\0模式\0…\0\0"，长度没有任何字段可以查，
 * 只能靠双空结尾判定终点；留一个上限是为了调用方给出一份没闭合的串时不要把整段内存
 * 当筛选表读完（与 src/platform/linux/dlghelper.cpp 的解析口径一致，那边只靠双空）。 */
static const int kMaxFilterGroups = 64;

static std::string _trim_spaces(const std::string &strIn)
{
    size_t b = strIn.find_first_not_of(" \t");
    if (b == std::string::npos)
        return std::string();
    size_t e = strIn.find_last_not_of(" \t");
    return strIn.substr(b, e - b + 1);
}

int SwinxFilterToAcceptA(LPCSTR lpszFilter, LPSTR lpszOut, int cchOut)
{
    if (cchOut < 0 || (cchOut > 0 && lpszOut == NULL))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    std::vector<std::string> items;
    bool bAcceptAll = (lpszFilter == NULL);
    const char *p = lpszFilter;

    for (int iGroup = 0; p != NULL && *p != 0 && iGroup < kMaxFilterGroups; ++iGroup)
    {
        const char *pszDesc = p;
        p += strlen(pszDesc) + 1;
        if (*p == 0)
            break; // 双空结尾：筛选表到此为止
        const char *pszPatterns = p;
        p += strlen(pszPatterns) + 1;

        std::string strPatterns(pszPatterns);
        if (strPatterns.empty())
        {
            /* Win32 用"模式段为空串"表示这组接受全部文件。accept 表达不了"其中一组接受
             * 全部"，整份筛选只能按接受全部处理。 */
            bAcceptAll = true;
            break;
        }

        size_t start = 0;
        while (start < strPatterns.length())
        {
            size_t semi = strPatterns.find(';', start);
            if (semi == std::string::npos)
                semi = strPatterns.length();
            std::string strItem = _trim_spaces(strPatterns.substr(start, semi - start));
            start = semi + 1;
            if (strItem.empty())
                continue;
            /* "*" 与 "*.*" 在 Win32 里都表示"这一组接受全部"。accept 表达不了"其中一组
             * 接受全部"，整份筛选只能按接受全部处理（空 accept 就是接受所有文件）。 */
            if (strItem == "*" || strItem == "*.*")
            {
                bAcceptAll = true;
                break;
            }
            /* Win32 的 "*.png" 在这里去掉星号、留下 ".png"：浏览器只认以点开头的
             * 扩展名，其余形态（比如 MIME 型模式）原样交给浏览器。 */
            if (strItem.length() > 1 && strItem[0] == '*' && strItem[1] == '.')
                strItem.erase(0, 1);
            bool bDup = false;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (items[i] == strItem)
                {
                    bDup = true;
                    break;
                }
            }
            if (!bDup)
                items.push_back(strItem);
        }
        if (bAcceptAll)
            break;
    }

    std::string strAccept;
    if (!bAcceptAll)
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i != 0)
                strAccept += ',';
            strAccept += items[i];
        }
    }

    if (cchOut == 0)
        return (int)strAccept.length() + 1; // 探长度：含结尾 0，不写任何字节
    if (cchOut <= (int)strAccept.length())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    memcpy(lpszOut, strAccept.c_str(), strAccept.length() + 1);
    return (int)strAccept.length();
}

/* 一次异步请求的私有状态。
 *
 * 路径缓冲是 swinx 自有的：同步版的 lpstrFile 由调用方持有、调用返回后立刻读；异步版
 * 返回时对话框还没出现，等回调发生时那块内存可能早就不在了。所以这里按调用方给的
 * nMaxFile 复制一份缓冲，钩子把结果写到这里，回调期间字符串有效、回调返回即释放。
 * 代价是调用方的 p->lpstrFile 不再被回写 —— 这是异步契约与同步版唯一的差别。 */
struct SWinxPickRequest
{
    OPENFILENAMEA of; // 调用方字段的副本，lpstrFile 指向 pszPath
    LPSTR pszPath;
    SWINXFILEPICKCALLBACK pfnUser;
    void *pUserCtx;
};

static void CALLBACK _SwinxPickForward(int nResult, LPCSTR lpszUtf8Path, void *pCtx)
{
    SWinxPickRequest *pReq = (SWinxPickRequest *)pCtx;
    /* 钩子想给别的串（例如自己落盘的路径）就用它的，否则读那份共享缓冲。 */
    LPCSTR psz = lpszUtf8Path;
    if (psz == NULL && nResult == 1)
        psz = pReq->pszPath;
    pReq->pfnUser(nResult, psz, pReq->pUserCtx);
    delete[] pReq->pszPath;
    delete pReq;
}

int SwinxPickFileAsyncA(LPOPENFILENAMEA p, DlgMode mode, SWINXFILEPICKCALLBACK pfnDone, void *pCtx)
{
    if (p == NULL || pfnDone == NULL || p->lpstrFile == NULL || p->nMaxFile == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (p->Flags & OFN_ALLOWMULTISELECT)
    {
        /* 异步契约一次只回一条路径。多选要的是"一条缓冲里若干路径"的另一种打包，
         * 不在这里悄悄降级成单选 —— 显式拒绝。 */
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }

    SWinxPickRequest *pReq = new (std::nothrow) SWinxPickRequest;
    if (pReq == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    pReq->pszPath = new (std::nothrow) char[p->nMaxFile];
    if (pReq->pszPath == NULL)
    {
        delete pReq;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }

    pReq->of = *p;
    pReq->of.lpstrFile = pReq->pszPath;
    pReq->pszPath[0] = 0;
    /* 不回写调用方的标题缓冲，FileTitle 于异步契约没有意义；各平台钩子都做了
     * lpstrFileTitle && nMaxFileTitle > 0 的判空，所以置空是安全的。 */
    pReq->of.lpstrFileTitle = NULL;
    pReq->of.nMaxFileTitle = 0;
    pReq->pfnUser = pfnDone;
    pReq->pUserCtx = pCtx;

    int nAccepted = swinx_pick_file_async(&pReq->of, mode, _SwinxPickForward, pReq);
    if (nAccepted == 0)
    {
        delete[] pReq->pszPath;
        delete pReq;
    }
    return nAccepted;
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
    OPENFILENAMEA of = {};

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
    OPENFILENAMEW of = {};
    of.nMaxFile = lpbi->nMaxPath;
    of.lpstrFile = lpbi->lpszPath;
    of.hwndOwner = lpbi->hwndOwner;
    of.lpstrInitialDir = lpbi->strlRoot;
    return _GetOpenFileNameW(&of, FOLDER);
}