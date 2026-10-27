#ifndef __SHELL_OBJ_H__
#define __SHELL_OBJ_H__
#include <windows.h>
#include <shellapi.h>
#include <unknwn.h>
#include <guiddef.h>

// {4657278A-411B-11d2-839A-00C04FD918D0}
DEFINE_GUID(CLSID_DragDropHelper, 0x4657278a, 0x411b, 0x11d2, 0x83, 0x9a, 0x0, 0xc0, 0x4f, 0xd9, 0x18, 0xd0);

//
// format of CF_HDROP and CF_PRINTERS, in the HDROP case the data that follows
// is a double null terinated list of file names, for printers they are printer
// friendly names
//
typedef struct _DROPFILES
{
    DWORD pFiles; // offset of file list
    POINT pt;     // drop point (client coords)
    BOOL fNC;     // is it on NonClient area
                  // and pt is in screen coords
    BOOL fWide;   // WIDE character switch
} DROPFILES, *LPDROPFILES;

/*
 * FILEDESCRIPTOR / FILEGROUPDESCRIPTOR — the CFSTR_FILEDESCRIPTOR payload used
 * by drag-and-drop and clipboard file transfer (shlobj.h of the Windows SDK).
 * WebCore's ClipboardWin/ClipboardUtilitiesWin build and parse these blobs.
 */
typedef struct _FILEDESCRIPTORA
{
    DWORD dwFlags;
    CLSID clsid;
    SIZEL sizel;
    POINTL pointl;
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    CHAR cFileName[MAX_PATH];
} FILEDESCRIPTORA, *LPFILEDESCRIPTORA;

typedef struct _FILEDESCRIPTORW
{
    DWORD dwFlags;
    CLSID clsid;
    SIZEL sizel;
    POINTL pointl;
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    WCHAR cFileName[MAX_PATH];
} FILEDESCRIPTORW, *LPFILEDESCRIPTORW;

typedef struct _FILEGROUPDESCRIPTORA
{
    UINT cItems;
    FILEDESCRIPTORA fgd[1];
} FILEGROUPDESCRIPTORA, *LPFILEGROUPDESCRIPTORA;

typedef struct _FILEGROUPDESCRIPTORW
{
    UINT cItems;
    FILEDESCRIPTORW fgd[1];
} FILEGROUPDESCRIPTORW, *LPFILEGROUPDESCRIPTORW;

#ifdef UNICODE
typedef FILEDESCRIPTORW FILEDESCRIPTOR, *LPFILEDESCRIPTOR;
typedef FILEGROUPDESCRIPTORW FILEGROUPDESCRIPTOR, *LPFILEGROUPDESCRIPTOR;
#else
typedef FILEDESCRIPTORA FILEDESCRIPTOR, *LPFILEDESCRIPTOR;
typedef FILEGROUPDESCRIPTORA FILEGROUPDESCRIPTOR, *LPFILEGROUPDESCRIPTOR;
#endif

#define FD_CLSID      0x00000001
#define FD_SIZEPOINT  0x00000002
#define FD_ATTRIBUTES 0x00000004
#define FD_CREATETIME 0x00000008
#define FD_ACCESSTIME 0x00000010
#define FD_WRITESTIME 0x00000020
#define FD_FILESIZE   0x00000040
#define FD_PROGRESSUI 0x00004000
#define FD_LINKUI     0x00008000

/* Clipboard/drag format names (shlobj.h of the SDK). */
#define CFSTR_SHELLIDLIST                TEXT("Shell IDList Array")
#define CFSTR_SHELLIDLISTOFFSET          TEXT("Shell Object Offsets")
#define CFSTR_NETRESOURCES               TEXT("Net Resource")
#define CFSTR_FILEDESCRIPTORA            TEXT("FileGroupDescriptor")
#define CFSTR_FILEDESCRIPTORW            TEXT("FileGroupDescriptorW")
#define CFSTR_FILECONTENTS               TEXT("FileContents")
#define CFSTR_FILENAMEA                  TEXT("FileName")
#define CFSTR_FILENAMEW                  TEXT("FileNameW")
#define CFSTR_PRINTERGROUP               TEXT("PrinterFriendlyName")
#define CFSTR_FILENAMEMAPA               TEXT("FileNameMap")
#define CFSTR_FILENAMEMAPW               TEXT("FileNameMapW")
#define CFSTR_SHELLURL                   TEXT("UniformResourceLocator")
#define CFSTR_INETURLA                   CFSTR_SHELLURL
#define CFSTR_INETURLW                   TEXT("UniformResourceLocatorW")
#define CFSTR_PREFERREDDROPEFFECT        TEXT("Preferred DropEffect")
#define CFSTR_PERFORMEDDROPEFFECT        TEXT("Performed DropEffect")
#define CFSTR_PASTESUCCEEDED             TEXT("Paste Succeeded")
#define CFSTR_INDRAGLOOP                 TEXT("InShellDragLoop")
#define CFSTR_MOUNTEDVOLUME              TEXT("MountedVolume")
#define CFSTR_PERSISTEDDATAOBJECT        TEXT("PersistedDataObject")
#define CFSTR_TARGETCLSID                TEXT("TargetCLSID")
#define CFSTR_LOGICALPERFORMEDDROPEFFECT TEXT("Logical Performed DropEffect")
#define CFSTR_AUTOPLAY_SHELLIDLISTS      TEXT("Autoplay Enumerated IDList Array")
#define CFSTR_UNTRUSTEDDRAGDROP          TEXT("UntrustedDragDrop")
#define CFSTR_FILE_ATTRIBUTES_ARRAY      TEXT("File Attributes Array")
#define CFSTR_INVOKECOMMAND_DROPPOSITION TEXT("InvokeCommand DropPosition")
#define CFSTR_SHELLDROPHANDLER           TEXT("ShellDropHandler")
#define CFSTR_DROPDESCRIPTION            TEXT("DropDescription")
#define CFSTR_ZONEIDENTIFIER             TEXT("ZoneIdentifier")

#ifdef UNICODE
#define CFSTR_FILEDESCRIPTOR CFSTR_FILEDESCRIPTORW
#define CFSTR_FILENAME       CFSTR_FILENAMEW
#define CFSTR_FILENAMEMAP    CFSTR_FILENAMEMAPW
#define CFSTR_INETURL        CFSTR_INETURLW
#else
#define CFSTR_FILEDESCRIPTOR CFSTR_FILEDESCRIPTORA
#define CFSTR_FILENAME       CFSTR_FILENAMEA
#define CFSTR_FILENAMEMAP    CFSTR_FILENAMEMAPA
#define CFSTR_INETURL        CFSTR_INETURLA
#endif

#ifndef __IDragSourceHelper_FWD_DEFINED__
#define __IDragSourceHelper_FWD_DEFINED__
typedef struct IDragSourceHelper IDragSourceHelper;

#endif /* __IDragSourceHelper_FWD_DEFINED__ */

#pragma pack(push, 8)
typedef struct SHDRAGIMAGE
{
    SIZE sizeDragImage;
    POINT ptOffset;
    HBITMAP hbmpDragImage;
    COLORREF crColorKey;
} SHDRAGIMAGE;

typedef struct SHDRAGIMAGE *LPSHDRAGIMAGE;
#pragma pack(pop)

#ifndef __IDragSourceHelper_INTERFACE_DEFINED__
#define __IDragSourceHelper_INTERFACE_DEFINED__

/* interface IDragSourceHelper */
/* [object][unique][local][uuid] */

EXTERN_C const IID IID_IDragSourceHelper;

#if defined(__cplusplus) && !defined(CINTERFACE)

DEFINE_GUID(IID_IDragSourceHelper, 0xDE5BF786, 0x477A, 0x11d2, 0x83, 0x9d, 0, 0xc0, 0x4f, 0xd9, 0x18, 0xd0);
struct IDragSourceHelper : public IUnknown
{
  public:
    DECLARE_CLASS_SIID(IID_IDragSourceHelper)

    virtual HRESULT STDMETHODCALLTYPE InitializeFromBitmap(
        /* [annotation][in] */
        _In_ LPSHDRAGIMAGE pshdi,
        /* [annotation][in] */
        _In_ IDataObject *pDataObject)
        = 0;

    virtual HRESULT STDMETHODCALLTYPE InitializeFromWindow(
        /* [annotation][unique][in] */
        _In_opt_ HWND hwnd,
        /* [annotation][unique][in] */
        _In_opt_ POINT *ppt,
        /* [annotation][in] */
        _In_ IDataObject *pDataObject)
        = 0;
};

#else /* C style interface */

typedef struct IDragSourceHelperVtbl
{
    BEGIN_INTERFACE

    DECLSPEC_XFGVIRT(IUnknown, QueryInterface)
    HRESULT(STDMETHODCALLTYPE *QueryInterface)
    (IDragSourceHelper *This,
     /* [in] */ REFIID riid,
     /* [annotation][iid_is][out] */
     _COM_Outptr_ void **ppvObject);

    DECLSPEC_XFGVIRT(IUnknown, AddRef)
    ULONG(STDMETHODCALLTYPE *AddRef)(IDragSourceHelper *This);

    DECLSPEC_XFGVIRT(IUnknown, Release)
    ULONG(STDMETHODCALLTYPE *Release)(IDragSourceHelper *This);

    DECLSPEC_XFGVIRT(IDragSourceHelper, InitializeFromBitmap)
    HRESULT(STDMETHODCALLTYPE *InitializeFromBitmap)
    (IDragSourceHelper *This,
     /* [annotation][in] */
     _In_ LPSHDRAGIMAGE pshdi,
     /* [annotation][in] */
     _In_ IDataObject *pDataObject);

    DECLSPEC_XFGVIRT(IDragSourceHelper, InitializeFromWindow)
    HRESULT(STDMETHODCALLTYPE *InitializeFromWindow)
    (IDragSourceHelper *This,
     /* [annotation][unique][in] */
     _In_opt_ HWND hwnd,
     /* [annotation][unique][in] */
     _In_opt_ POINT *ppt,
     /* [annotation][in] */
     _In_ IDataObject *pDataObject);

    END_INTERFACE
} IDragSourceHelperVtbl;

interface IDragSourceHelper
{
    CONST_VTBL struct IDragSourceHelperVtbl *lpVtbl;
};

#ifdef COBJMACROS

#define IDragSourceHelper_QueryInterface(This, riid, ppvObject) ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IDragSourceHelper_AddRef(This) ((This)->lpVtbl->AddRef(This))

#define IDragSourceHelper_Release(This) ((This)->lpVtbl->Release(This))

#define IDragSourceHelper_InitializeFromBitmap(This, pshdi, pDataObject) ((This)->lpVtbl->InitializeFromBitmap(This, pshdi, pDataObject))

#define IDragSourceHelper_InitializeFromWindow(This, hwnd, ppt, pDataObject) ((This)->lpVtbl->InitializeFromWindow(This, hwnd, ppt, pDataObject))

#endif /* COBJMACROS */

#endif /* C style interface */

#endif /* __IDragSourceHelper_INTERFACE_DEFINED__ */

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus
    HRESULT WINAPI SHCreateStdEnumFmtEtc(_In_ UINT cfmt, _In_reads_(cfmt) const FORMATETC afmt[], _Outptr_ IEnumFORMATETC **ppenumFormatEtc);

#define CSIDL_DESKTOP                 0x0000         // <desktop>
#define CSIDL_INTERNET                0x0001         // Internet Explorer (icon on desktop)
#define CSIDL_PROGRAMS                0x0002         // Start Menu\Programs
#define CSIDL_CONTROLS                0x0003         // My Computer\Control Panel
#define CSIDL_PRINTERS                0x0004         // My Computer\Printers
#define CSIDL_PERSONAL                0x0005         // My Documents
#define CSIDL_FAVORITES               0x0006         // <user name>\Favorites
#define CSIDL_STARTUP                 0x0007         // Start Menu\Programs\Startup
#define CSIDL_RECENT                  0x0008         // <user name>\Recent
#define CSIDL_SENDTO                  0x0009         // <user name>\SendTo
#define CSIDL_BITBUCKET               0x000a         // <desktop>\Recycle Bin
#define CSIDL_STARTMENU               0x000b         // <user name>\Start Menu
#define CSIDL_MYDOCUMENTS             CSIDL_PERSONAL //  Personal was just a silly name for My Documents
#define CSIDL_MYMUSIC                 0x000d         // "My Music" folder
#define CSIDL_MYVIDEO                 0x000e         // "My Videos" folder
#define CSIDL_DESKTOPDIRECTORY        0x0010         // <user name>\Desktop
#define CSIDL_DRIVES                  0x0011         // My Computer
#define CSIDL_NETWORK                 0x0012         // Network Neighborhood (My Network Places)
#define CSIDL_NETHOOD                 0x0013         // <user name>\nethood
#define CSIDL_FONTS                   0x0014         // windows\fonts
#define CSIDL_TEMPLATES               0x0015
#define CSIDL_COMMON_STARTMENU        0x0016 // All Users\Start Menu
#define CSIDL_COMMON_PROGRAMS         0X0017 // All Users\Start Menu\Programs
#define CSIDL_COMMON_STARTUP          0x0018 // All Users\Startup
#define CSIDL_COMMON_DESKTOPDIRECTORY 0x0019 // All Users\Desktop
#define CSIDL_APPDATA                 0x001a // <user name>\Application Data
#define CSIDL_PRINTHOOD               0x001b // <user name>\PrintHood

#ifndef CSIDL_LOCAL_APPDATA
#define CSIDL_LOCAL_APPDATA 0x001c // <user name>\Local Settings\Applicaiton Data (non roaming)
#endif                             // CSIDL_LOCAL_APPDATA

#define CSIDL_ALTSTARTUP        0x001d // non localized startup
#define CSIDL_COMMON_ALTSTARTUP 0x001e // non localized common startup
#define CSIDL_COMMON_FAVORITES  0x001f

#ifndef _SHFOLDER_H_
#define CSIDL_INTERNET_CACHE 0x0020
#define CSIDL_COOKIES        0x0021
#define CSIDL_HISTORY        0x0022
#define CSIDL_COMMON_APPDATA 0x0023 // All Users\Application Data
#define CSIDL_WINDOWS        0x0024 // GetWindowsDirectory()
#define CSIDL_SYSTEM         0x0025 // GetSystemDirectory()
#define CSIDL_PROGRAM_FILES  0x0026 // C:\Program Files
#define CSIDL_MYPICTURES     0x0027 // C:\Program Files\My Pictures
#endif                              // _SHFOLDER_H_

#define CSIDL_PROFILE 0x0028 // USERPROFILE

/* CSIDL_* 的高字节是「标志位」，nFolder 必须先用 CSIDL_FLAG_MASK 掩掉才能比较。 */
#define CSIDL_FLAG_PER_USER_INIT 0x0800
#define CSIDL_FLAG_NO_ALIAS      0x1000
#define CSIDL_FLAG_DONT_UNEXPAND 0x2000
#define CSIDL_FLAG_DONT_VERIFY   0x4000
#define CSIDL_FLAG_CREATE        0x8000
#define CSIDL_FLAG_MASK          0xFF00

    BOOL WINAPI SHGetSpecialFolderPathA(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate);
    BOOL WINAPI SHGetSpecialFolderPathW(HWND hwndOwner, LPWSTR lpszPath, int nFolder, BOOL fCreate);

#ifdef UNICODE
#define SHGetSpecialFolderPath SHGetSpecialFolderPathW
#else
#define SHGetSpecialFolderPath SHGetSpecialFolderPathA
#endif // UNICODE

/* SHGetFolderPath 的 dwFlags。swinx 两者等价（没有「默认用户」的概念）。 */
#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1

    /* 取 CSIDL_* 对应的目录路径。
     * 返回值与 Win32 一致：成功 S_OK，失败是 HRESULT_FROM_WIN32(错误码)。
     * nFolder 里的 CSIDL_FLAG_CREATE 会被解释为「目录不存在时创建」。 */
    HRESULT WINAPI SHGetFolderPathA(HWND hwndOwner, int nFolder, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);
    HRESULT WINAPI SHGetFolderPathW(HWND hwndOwner, int nFolder, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);

#ifdef UNICODE
#define SHGetFolderPath SHGetFolderPathW
#else
#define SHGetFolderPath SHGetFolderPathA
#endif // UNICODE

    /* 递归创建目录（含所有中间目录），成功返回 ERROR_SUCCESS。
     * hwnd 只用于失败时弹窗，swinx 不弹窗；psa 忽略（无 ACL 概念）。
     * 返回值是 Win32 错误码，不是 BOOL —— 与 SHGetSpecialFolderPath 不同。 */
    int WINAPI SHCreateDirectoryExA(HWND hwnd, LPCSTR pszPath, const SECURITY_ATTRIBUTES *psa);
    int WINAPI SHCreateDirectoryExW(HWND hwnd, LPCWSTR pszPath, const SECURITY_ATTRIBUTES *psa);

#ifdef UNICODE
#define SHCreateDirectoryEx SHCreateDirectoryExW
#else
#define SHCreateDirectoryEx SHCreateDirectoryExA
#endif // UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //__SHELL_OBJ_H__
