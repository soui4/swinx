#include <sfr_config.h>
#include <windows.h>
#ifndef _WINVER_H_
#define _WINVER_H_

/* winver.h —— VERSION.dll 的版本资源查询。
 *
 * Windows 的版本信息存在 PE 的 RT_VERSION 资源里，是一棵 VS_VERSIONINFO 树：
 * 根节点带一个 VS_FIXEDFILEINFO 定长块，子节点按 StringFileInfo\<langcp>\<名>
 * 再按 Translation 分组。swinx 复用 coffparser 的 PE 资源目录解析，不另写
 * 一份 PE 解析。
 *
 * 注意一处宽度差异：资源里的字符串恒为 UTF-16，而 swinx 的 WCHAR 在 POSIX 上
 * 是 4 字节。所以字符串值不能直接指向资源内存在返回，必须先解码——见
 * src/platform/posix/version.cpp 的说明。
 */

#define VS_FFI_SIGNATURE     0xFEEF04BDL
#define VS_FFI_STRUCVERSION  0x00010000L
#define VS_FFI_FILEFLAGSMASK 0x0000003FL

#define VS_FF_DEBUG        0x00000001L
#define VS_FF_PRERELEASE   0x00000002L
#define VS_FF_PATCHED      0x00000004L
#define VS_FF_PRIVATEBUILD 0x00000008L
#define VS_FF_INFOINFERRED 0x00000010L
#define VS_FF_SPECIALBUILD 0x00000020L

#define VOS_UNKNOWN    0x00000000L
#define VOS_DOS        0x00010000L
#define VOS_NT         0x00040000L
#define VOS__WINDOWS16 0x00000001L
#define VOS__WINDOWS32 0x00000004L
#define VOS_OS216      0x00020000L
#define VOS_OS232      0x00030000L
#define VOS__PM16      0x00000002L
#define VOS__PM32      0x00000003L

/* 组合形式（SDK 里就是这几条的按位或，但调用方经常直接用名字） */
#define VOS_DOS_WINDOWS16 0x00010001L
#define VOS_DOS_WINDOWS32 0x00010004L
#define VOS_NT_WINDOWS16  0x00040001L
#define VOS_NT_WINDOWS32  0x00040004L

#define VFT_UNKNOWN    0x00000000L
#define VFT_APP        0x00000001L
#define VFT_DLL        0x00000002L
#define VFT_DRV        0x00000003L
#define VFT_FONT       0x00000004L
#define VFT_VXD        0x00000005L
#define VFT_STATIC_LIB 0x00000007L

typedef struct tagVS_FIXEDFILEINFO
{
    DWORD dwSignature;     // VS_FFI_SIGNATURE
    DWORD dwStrucVersion;  // VS_FFI_STRUCVERSION
    DWORD dwFileVersionMS; // 主版本 << 16 | 次版本
    DWORD dwFileVersionLS; // 构建号 << 16 | 修订号
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
    DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);

    BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
    BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);

    BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);
    BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);

#ifdef __cplusplus
}
#endif //__cplusplus

#ifdef UNICODE
#define GetFileVersionInfoSize GetFileVersionInfoSizeW
#define GetFileVersionInfo     GetFileVersionInfoW
#define VerQueryValue          VerQueryValueW
#else
#define GetFileVersionInfoSize GetFileVersionInfoSizeA
#define GetFileVersionInfo     GetFileVersionInfoA
#define VerQueryValue          VerQueryValueA
#endif

#endif //_WINVER_H_
