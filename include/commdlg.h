#ifndef __COMMDLG_H__
#define __COMMDLG_H__
#include <ctypes.h>
#include <gdi.h>

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    typedef enum _DlgMode
    {
        OPEN = 0,
        SAVE,
        FOLDER,
    } DlgMode;

#define OFN_READONLY             0x00000001
#define OFN_OVERWRITEPROMPT      0x00000002
#define OFN_HIDEREADONLY         0x00000004
#define OFN_NOCHANGEDIR          0x00000008
#define OFN_SHOWHELP             0x00000010
#define OFN_ENABLEHOOK           0x00000020
#define OFN_ENABLETEMPLATE       0x00000040
#define OFN_ENABLETEMPLATEHANDLE 0x00000080
#define OFN_NOVALIDATE           0x00000100
#define OFN_ALLOWMULTISELECT     0x00000200
#define OFN_EXTENSIONDIFFERENT   0x00000400
#define OFN_PATHMUSTEXIST        0x00000800
#define OFN_FILEMUSTEXIST        0x00001000
#define OFN_CREATEPROMPT         0x00002000
#define OFN_SHAREAWARE           0x00004000
#define OFN_NOREADONLYRETURN     0x00008000
#define OFN_NOTESTFILECREATE     0x00010000
#define OFN_NONETWORKBUTTON      0x00020000
#define OFN_NOLONGNAMES          0x00040000 // force no long names for 4.x modules
#if (WINVER >= 0x0400)
#define OFN_EXPLORER           0x00080000 // new look commdlg
#define OFN_NODEREFERENCELINKS 0x00100000
#define OFN_LONGNAMES          0x00200000 // force long names for 3.x modules
// OFN_ENABLEINCLUDENOTIFY and OFN_ENABLESIZING require
// Windows 2000 or higher to have any effect.
#define OFN_ENABLEINCLUDENOTIFY 0x00400000 // send include message to callback
#define OFN_ENABLESIZING        0x00800000
#endif /* WINVER >= 0x0400 */
#if (_WIN32_WINNT >= 0x0500)
#define OFN_DONTADDTORECENT 0x02000000
#define OFN_FORCESHOWHIDDEN 0x10000000 // Show All files including System and hidden files
#endif                                 // (_WIN32_WINNT >= 0x0500)

// FlagsEx Values
#if (_WIN32_WINNT >= 0x0500)
#define OFN_EX_NOPLACESBAR 0x00000001
#endif // (_WIN32_WINNT >= 0x0500)

    typedef UINT_PTR(CALLBACK *LPOFNHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
    typedef UINT_PTR(CALLBACK *LPCCHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

    typedef struct tagEDITMENU
    {
        HMENU hmenu;
        WORD idEdit;
        WORD idCut;
        WORD idCopy;
        WORD idPaste;
        WORD idClear;
        WORD idUndo;
    } EDITMENU;
    typedef EDITMENU *LPEDITMENU;
    typedef struct tagOFNA
    {
        DWORD lStructSize;
        HWND hwndOwner;
        HINSTANCE hInstance;
        LPCSTR lpstrFilter;
        LPSTR lpstrCustomFilter;
        DWORD nMaxCustFilter;
        DWORD nFilterIndex;
        LPSTR lpstrFile;
        DWORD nMaxFile;
        LPSTR lpstrFileTitle;
        DWORD nMaxFileTitle;
        LPCSTR lpstrInitialDir;
        LPCSTR lpstrTitle;
        DWORD Flags;
        WORD nFileOffset;
        WORD nFileExtension;
        LPCSTR lpstrDefExt;
        LPARAM lCustData;
        LPOFNHOOKPROC lpfnHook;
        LPCSTR lpTemplateName;
        LPEDITMENU lpEditInfo;
        LPCSTR lpstrPrompt;
        void *pvReserved;
        DWORD dwReserved;
        DWORD FlagsEx;
    } OPENFILENAMEA, *LPOPENFILENAMEA;

    typedef struct tagOFNW
    {
        DWORD lStructSize;
        HWND hwndOwner;
        HINSTANCE hInstance;
        LPCWSTR lpstrFilter;
        LPWSTR lpstrCustomFilter;
        DWORD nMaxCustFilter;
        DWORD nFilterIndex;
        LPWSTR lpstrFile;
        DWORD nMaxFile;
        LPWSTR lpstrFileTitle;
        DWORD nMaxFileTitle;
        LPCWSTR lpstrInitialDir;
        LPCWSTR lpstrTitle;
        DWORD Flags;
        WORD nFileOffset;
        WORD nFileExtension;
        LPCWSTR lpstrDefExt;
        LPARAM lCustData;
        LPOFNHOOKPROC lpfnHook;
        LPCWSTR lpTemplateName;
        LPEDITMENU lpEditInfo;
        LPCSTR lpstrPrompt;
        void *pvReserved;
        DWORD dwReserved;
        DWORD FlagsEx;
    } OPENFILENAMEW, *LPOPENFILENAMEW;

#ifdef UNICODE
    typedef OPENFILENAMEW OPENFILENAME;
    typedef LPOPENFILENAMEW LPOPENFILENAME;
#else
typedef OPENFILENAMEA OPENFILENAME;
typedef LPOPENFILENAMEA LPOPENFILENAME;
#endif // UNICODE

    BOOL GetOpenFileNameA(LPOPENFILENAMEA);
    BOOL GetOpenFileNameW(LPOPENFILENAMEW);
#ifdef UNICODE
#define GetOpenFileName GetOpenFileNameW
#else
#define GetOpenFileName GetOpenFileNameA
#endif // !UNICODE
    BOOL GetSaveFileNameA(LPOPENFILENAMEA);
    BOOL GetSaveFileNameW(LPOPENFILENAMEW);
#ifdef UNICODE
#define GetSaveFileName GetSaveFileNameW
#else
#define GetSaveFileName GetSaveFileNameA
#endif // !UNICODE

    typedef struct tagCHOOSECOLORA
    {
        DWORD lStructSize;
        HWND hwndOwner;
        HWND hInstance;
        COLORREF rgbResult;
        COLORREF *lpCustColors;
        DWORD Flags;
        LPARAM lCustData;
        LPCCHOOKPROC lpfnHook;
        LPCSTR lpTemplateName;
    } CHOOSECOLORA, *LPCHOOSECOLORA;
    typedef struct tagCHOOSECOLORW
    {
        DWORD lStructSize;
        HWND hwndOwner;
        HWND hInstance;
        COLORREF rgbResult;
        COLORREF *lpCustColors;
        DWORD Flags;
        LPARAM lCustData;
        LPCCHOOKPROC lpfnHook;
        LPCWSTR lpTemplateName;
    } CHOOSECOLORW, *LPCHOOSECOLORW;

    BOOL ChooseColorA(LPCHOOSECOLORA);
    BOOL ChooseColorW(LPCHOOSECOLORW);
#ifdef UNICODE
#define CHOOSECOLOR CHOOSECOLORW
#define ChooseColor ChooseColorW
#else
#define CHOOSECOLOR CHOOSECOLORA
#define ChooseColor ChooseColorA
#endif // !UNICODE

#define CC_RGBINIT              0x00000001
#define CC_FULLOPEN             0x00000002
#define CC_PREVENTFULLOPEN      0x00000004
#define CC_SHOWHELP             0x00000008
#define CC_ENABLEHOOK           0x00000010
#define CC_ENABLETEMPLATE       0x00000020
#define CC_ENABLETEMPLATEHANDLE 0x00000040
#if (WINVER >= 0x0400)
#define CC_SOLIDCOLOR 0x00000080
#define CC_ANYCOLOR   0x00000100
#endif /* WINVER >= 0x0400 */

    //-------------------------------------------------------------------------
    //
    // PickFolder API
    //
    //
    //-------------------------------------------------------------------------

    typedef struct _browseinfoA
    {
        HWND hwndOwner;
        LPCSTR strlRoot;
        LPSTR pszDisplayName; // Return display name of item selected.
        LPCSTR lpszTitle;     // text to go in the banner over the tree.
        LPSTR lpszPath;
        int nMaxPath;
    } BROWSEINFOA, *PBROWSEINFOA, *LPBROWSEINFOA;

    typedef struct _browseinfoW
    {
        HWND hwndOwner;
        LPCWSTR strlRoot;
        LPWSTR pszDisplayName; // Return display name of item selected.
        LPCWSTR lpszTitle;     // text to go in the banner over the tree.
        LPWSTR lpszPath;
        int nMaxPath;
    } BROWSEINFOW, *PBROWSEINFOW, *LPBROWSEINFOW;

    BOOL WINAPI PickFolderA(_In_ LPBROWSEINFOA lpbi);
    BOOL WINAPI PickFolderW(_In_ LPBROWSEINFOW lpbi);

#ifdef UNICODE
#define BROWSEINFO   BROWSEINFOW
#define PBROWSEINFO  PBROWSEINFOW
#define LPBROWSEINFO LPBROWSEINFOW
#else
#define BROWSEINFO   BROWSEINFOA
#define PBROWSEINFO  PBROWSEINFOA
#define LPBROWSEINFO LPBROWSEINFOA
#endif

#ifdef UNICODE
#define PickFolder PickFolderW
#else
#define PickFolder PickFolderA
#endif

    BOOL WINAPI ChooseFontA(LPCHOOSEFONTA p);
    BOOL WINAPI ChooseFontW(LPCHOOSEFONTW p);

#ifdef UNICODE
#define ChooseFont ChooseFontW
#else
#define ChooseFont ChooseFontA
#endif // !UNICODE

    //-------------------------------------------------------------------------
    //
    // 异步文件选择（swinx 私有扩展，Win32 没有同名 API）
    //
    // 为什么需要它：浏览器主线程不能在 JS→wasm 的这次调用里阻塞等 <input type=file>
    // 的 change 事件 —— 该事件只在 JS 栈返回之后才派发。所以 wasm 给不出
    // GetOpenFileName 的同步对话框契约；iOS 端是用 dispatch_semaphore + 嵌套
    // CFRunLoop 把异步包装成同步（src/platform/ios/dlghelper.mm），浏览器没有可重入
    // 的嵌套消息循环，只能把异步语义交给调用方。
    //
    // 契约：
    //   返回 1 表示请求已受理，结果一律经 pfnDone 给出 —— 桌面/iOS 的本地对话框本来就是
    //     阻塞的，回调在 SwinxPickFileAsyncA 返回之前内联执行（裸金属与移动端没有对话框
    //     能力，走的也是这条内联路径，结果就是 nResult = -1）；wasm 的回调稍后在浏览器
    //     主线程上执行。返回 0 表示请求根本没发起（参数不合法，或 wasm 上的 SAVE/FOLDER
    //     与宿主没装钩子），原因见 GetLastError，pfnDone 不会被调用。
    //   nResult：1=已选中，0=用户取消，-1=失败（含本平台显式拒绝该能力）。
    //   lpszUtf8Path：UTF-8 路径，只在回调调用期间有效，要留存必须自己拷贝；
    //     nResult != 1 时为 NULL。
    //   与同步版 GetOpenFileNameA 的差别：结果不回写调用方的 p->lpstrFile（异步返回时
    //     那个缓冲可能已经不存在），只经回调给出。p 与其字符串字段只在
    //     SwinxPickFileAsyncA 调用期间被读用，返回后即可释放。
    //   只支持单选：Flags 带 OFN_ALLOWMULTISELECT 时显式拒绝。
    //   wasm 只支持 mode == OPEN，且必须在 UI 线程（= 浏览器主线程）上调用：SAVE 要写
    //     磁盘、FOLDER 要枚举目录，都超出 <input type=file> 的能力，返回
    //     0 + ERROR_CALL_NOT_IMPLEMENTED；worker 线程的 JS realm 里没有页面那份 Module，
    //     取宿主钩子同样取不到。选中的文件按字节落到 wasm 文件系统里
    //     （src/platform/wasm/dlghelper.cpp），回调给的路径可以直接 fopen。
    //
    typedef void(CALLBACK *SWINXFILEPICKCALLBACK)(int nResult, LPCSTR lpszUtf8Path, void *pCtx);

    int WINAPI SwinxPickFileAsyncA(_In_ LPOPENFILENAMEA p, _In_ DlgMode mode, _In_ SWINXFILEPICKCALLBACK pfnDone, _In_ void *pCtx);

    //-------------------------------------------------------------------------
    //
    // lpstrFilter -> 浏览器 <input accept>
    //
    // 把 Win32 的筛选串（"描述\0*.png;*.jpg\0\0"，双空结尾、描述与模式交替）压成逗号
    // 分隔的模式串（".png,.jpg"）：描述段丢弃，分号分隔的多个模式逐个收录，重复模式去掉。
    // 单个模式的映射：以 "*. 开头的去掉星号（".png"）；"*" 与 "*.*" 表示接受全部，某组
    // 的模式段为空串同样表示接受全部 —— accept 表达不了"其中一组接受全部"，此时整份结果
    // 为空串（accept 留空即接受所有文件）；其余原样交给浏览器（MIME 型模式走这条）。模式串里的前后空格会被去掉。
    //
    // 返回值：写入的字符数（不含结尾 0），接受全部时为 0；
    //   cchOut == 0 且不写任何字节，返回所需字节数（含结尾 0）—— 与
    //   WideCharToMultiByte 的探长度用法一致，此时 lpszOut 可以为 NULL；
    //   参数不合法（cchOut > 0 而 lpszOut 为 NULL，或缓冲不够）返回 -1 并
    //   SetLastError(ERROR_INVALID_PARAMETER)。
    //
    int WINAPI SwinxFilterToAcceptA(_In_ LPCSTR lpszFilter, _Out_ LPSTR lpszOut, int cchOut);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //__COMMDLG_H__