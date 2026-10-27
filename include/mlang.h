#include <sfr_config.h>
#include <windows.h>
#ifndef _MLANG_H_
#define _MLANG_H_

/* mlang.h —— MLang（Multi Language）接口族的可用子集。
 *
 * WebKit 的 win 端口把 MLang 当成两件事用：
 *   1. 「码页判定」：一个字符 / 一段文本属于哪些码页，某个字体覆盖哪些码页
 *      （SimpleFontData::containsCharacters、FontCacheWin 的 CJK 分支）；
 *   2. 「缺字回退」：给定码页掩码与字符，让系统挑一个能画它的字体
 *      （FontCacheWin::createMLangFont）。
 *
 * swinx 没有 Windows 的码页/字体链接数据库，所以这里**不镜像整份 SDK 头**，
 * 只提供上述两条路上真正被调用的接口，并给出可解释的实现：
 *
 *   - 码页掩码是「一本族（family code page）一位」的 32 位集合，由本头定义的
 *     MP_* 位描述；不同码页只要属于不同族就必然不同位，因此
 *     `GetCharCodePages(ch) & CodePageToCodePages(cp)` 的判交语义成立。
 *   - 字体覆盖判定与回退选字体走 fontconfig + FreeType，与 swinx 其它字体 API
 *     同一套数据源（不是另建一张表）。
 *
 * 未列出的 SDK 方法（码页枚举、BSTR / RFC1766 本地化名、IMLangString* 字符串
 * 对象族）在当前编译集里没有任何调用点，因此既不声明也不实现 —— 而不是给出
 * 一堆「编得过但永远返回 E_NOTIMPL」的桩，让调用方以为接口是全的。
 * `IMultiLanguage2` 是例外：它是 IMultiLanguage 的直系扩展，方法都能给出可
 * 解释的实现（见下方接口注释），所以一并提供。
 *
 * ⚠️ 字符串宽度：`GetStrCodePages` 的 pszSrc 是**原生宽字符**串。
 * Win32 上 WCHAR 是 UTF-16 码元，POSIX 上 WCHAR 是 4 字节 wchar_t —— 调用方
 * （SimpleFontDataWin.cpp）已经用 NativeWideString 桥接成原生宽字符，所以
 * 两侧都只写 `const WCHAR*`，不需要像 usp10 那样退回 2 字节类型。
 */

#include <unknwn.h>
#include <guiddef.h>

/* {275C23E1-3747-11D0-9FEA-00AA003F8646} */
DEFINE_GUID(IID_IMultiLanguage, 0x275c23e1, 0x3747, 0x11d0, 0x9f, 0xea, 0x0, 0xaa, 0x0, 0x3f, 0x86, 0x46);
/* {275C23E2-3747-11D0-9FEA-00AA003F8646} */
DEFINE_GUID(CLSID_CMultiLanguage, 0x275c23e2, 0x3747, 0x11d0, 0x9f, 0xea, 0x0, 0xaa, 0x0, 0x3f, 0x86, 0x46);
/* {359F3443-BD4A-11D0-B188-00AA0038C969} */
DEFINE_GUID(IID_IMLangCodePages, 0x359f3443, 0xbd4a, 0x11d0, 0xb1, 0x88, 0x0, 0xaa, 0x0, 0x38, 0xc9, 0x69);
/* {DCCFC162-2B38-11D2-B7EC-00C04F8F5D9A} */
DEFINE_GUID(IID_IMLangFontLink2, 0xdccfc162, 0x2b38, 0x11d2, 0xb7, 0xec, 0x0, 0xc0, 0x4f, 0x8f, 0x5d, 0x9a);
/* {DCCFC164-2B38-11D2-B7EC-00C04F8F5D9A} */
DEFINE_GUID(IID_IMultiLanguage2, 0xdccfc164, 0x2b38, 0x11d2, 0xb7, 0xec, 0x0, 0xc0, 0x4f, 0x8f, 0x5d, 0x9a);

/* IMultiLanguage2 的流式方法用 IStream，只取指针，因此前置声明即可；
 * 需要调用其方法（实现文件）时自行 include <objidl.h>。 */
#ifndef __IStream_FWD_DEFINED__
#define __IStream_FWD_DEFINED__
typedef interface IStream IStream;
#endif

/* DetectInputCodepage / DetectCodepageInIStream 的结果项。 */
typedef struct tagDetectEncodingInfo
{
    ULONG nLangID;
    UINT nCodePage;
    int nDocPercent;
    int nConfidence;
} DetectEncodingInfo, *pDetectEncodingInfo;

/* 检测范围（Detect*Codepage 的 dwFlag）。0 表示不限制。 */
typedef enum tagMLDETECTCP
{
    MLDETECTCP_NONE = 0,
    MLDETECTCP_7BIT = 1,
    MLDETECTCP_8BIT = 2,
    MLDETECTCP_DBCS = 4,
    MLDETECTCP_HTML = 8,
    MLDETECTCP_NUMBER = 16
} MLDETECTCP;

/* ConvertString*Ex 的 dwFlag。本实现兑现下面四个：
 *   NOFALLBACKS —— 忽略 lpFallBack；
 *   USEDEFCHAR / USEDEFCHAR2 —— 转不了时写 '?'；
 *   AUTODETECT —— 用检测结果替掉 dwEncoding。
 * 其余（NOBESTFITCHARS、ENTITIZE 一族）在 POSIX 上没有对应行为。 */
#define MLCONVCHARF_NOBESTFITCHARS 0x00000001
#define MLCONVCHARF_USEDEFCHAR     0x00000002
#define MLCONVCHARF_NOFALLBACKS    0x00000004
#define MLCONVCHARF_AUTODETECT     0x00000008
#define MLCONVCHARF_ENTITIZE       0x00000010
#define MLCONVCHARF_NCR_ENTITIZE   0x00000020
#define MLCONVCHARF_NAME_ENTITIZE  0x00000040
#define MLCONVCHARF_USEDEFCHAR2    0x00000080

/* SetMimeDecodeOptions / GetMimeDecodeOptions 的选项位。 */
#define MIMEDECODEOPT_NONE    0x00000000
#define MIMEDECODEOPT_RFC1522 0x00000001
#define MIMEDECODEOPT_RFC2047 0x00000002
#define MIMEDECODEOPT_ALL     0xFFFFFFFF

/* 码页掩码的位定义。真实的 mlang 用一张内部表把码页映射到某一位，位序未公开；
 * 调用方只做「判交 / 相等」，因此只要同一族的码页落在同一位、不同族落在不同
 * 位，语义就成立。位编号从低到高排列，MSDN 从未规定具体位置，这里也不假装。 */
#define MP_LATIN1    0x00000001u /* 1252 / 28591 / 20127 / 437 / 850 / 65001 */
#define MP_CYRILLIC  0x00000002u /* 1251 / 20866 / 21866 / 855 / 866 */
#define MP_CENTRALEU 0x00000004u /* 1250 / 852 / 895 */
#define MP_GREEK     0x00000008u /* 1253 / 737 / 869 */
#define MP_TURKISH   0x00000010u /* 1254 */
#define MP_HEBREW    0x00000020u /* 1255 / 862 */
#define MP_ARABIC    0x00000040u /* 1256 / 864 / 720 */
#define MP_BALTIC    0x00000080u /* 1257 / 775 */
#define MP_VIETNAM   0x00000100u /* 1258 */
#define MP_THAI      0x00000200u /* 874 */
#define MP_JAPANESE  0x00000400u /* 932 / 51932 / 50220 / 50221 / 50222 / 20932 */
#define MP_SCHINESE  0x00000800u /* 936 / 54936 / 20936 */
#define MP_KOREAN    0x00001000u /* 949 / 51949 / 50225 */
#define MP_TCHINESE  0x00002000u /* 950 / 51950 / 50227 */
#define MP_JOHAB     0x00004000u /* 1361 / 10003 */
#define MP_SYMBOL    0x00008000u /* 42（Symbol） */

/* 所有已定义族的并集：ASCII 字符属于其中每一个。 */
#define MP_ALL 0x0000FFFFu

typedef WORD SCRIPT_ID;

typedef struct tagUNICODERANGE
{
    WCHAR wcFrom;
    WCHAR wcTo;
} UNICODERANGE, *LPUNICODERANGE;

typedef struct tagSCRIPTFONTINFO
{
    DWORD cScripts;
    SCRIPT_ID sScripts[1];
} SCRIPTFONTINFO, *LPSCRIPTFONTINFO;

#ifndef __IMLangCodePages_FWD_DEFINED__
#define __IMLangCodePages_FWD_DEFINED__
typedef struct IMLangCodePages IMLangCodePages;
#endif

#ifndef __IMLangFontLink2_FWD_DEFINED__
#define __IMLangFontLink2_FWD_DEFINED__
typedef struct IMLangFontLink2 IMLangFontLink2;
#endif

#ifndef __IMultiLanguage_FWD_DEFINED__
#define __IMultiLanguage_FWD_DEFINED__
typedef struct IMultiLanguage IMultiLanguage;
#endif

#ifndef __IMultiLanguage2_FWD_DEFINED__
#define __IMultiLanguage2_FWD_DEFINED__
typedef struct IMultiLanguage2 IMultiLanguage2;
#endif

/* 旧版接口（SDK 里存在，但 wke 的编译集只用 IMLangFontLink2）。
 * 给出前置声明是为了让 `IMLangFontLink*` 作为返回类型/成员类型能编过。 */
#ifndef __IMLangFontLink_FWD_DEFINED__
#define __IMLangFontLink_FWD_DEFINED__
typedef struct IMLangFontLink IMLangFontLink;
#endif

#if defined(__cplusplus) && !defined(CINTERFACE)

/* interface IMLangCodePages */
struct IMLangCodePages : public IUnknown
{
    DECLARE_CLASS_SIID(IID_IMLangCodePages)

    virtual HRESULT STDMETHODCALLTYPE GetCharCodePages(WCHAR chSrc, DWORD *pdwCodePages) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetStrCodePages(const WCHAR *pszSrc, long cchSrc, DWORD dwPriorityCodePages, DWORD *pdwCodePages, long *pcchCodePages) = 0;

    virtual HRESULT STDMETHODCALLTYPE CodePageToCodePages(UINT uCodePage, DWORD *pdwCodePages) = 0;

    virtual HRESULT STDMETHODCALLTYPE CodePagesToCodePage(DWORD dwCodePages, UINT uDefaultCodePage, UINT *puCodePage) = 0;
};

/* interface IMLangFontLink2 */
struct IMLangFontLink2 : public IMLangCodePages
{
    DECLARE_CLASS_SIID(IID_IMLangFontLink2)

    virtual HRESULT STDMETHODCALLTYPE GetFontCodePages(HDC hDC, HFONT hFont, DWORD *pdwCodePages) = 0;

    virtual HRESULT STDMETHODCALLTYPE ReleaseFont(HFONT hFont) = 0;

    virtual HRESULT STDMETHODCALLTYPE ResetFontMapping(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE MapFont(HDC hDC, DWORD dwCodePages, WCHAR chSrc, HFONT *pFont) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetFontUnicodeRanges(HDC hDC, UINT *puiRanges, UNICODERANGE *pUranges) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetScriptFontInfo(SCRIPT_ID sid, DWORD dwFlags, UINT *puiFonts, LPSCRIPTFONTINFO pScriptFont) = 0;

    virtual HRESULT STDMETHODCALLTYPE CodePageToScriptID(UINT uiCodePage, SCRIPT_ID *pSid) = 0;
};

/* interface IMultiLanguage */
struct IMultiLanguage : public IUnknown
{
    DECLARE_CLASS_SIID(IID_IMultiLanguage)

    /* 把基类的 (REFGUID, void**) 重载拉回本作用域：下面的模板 QueryInterface
     * 会隐藏它，否则 `ml->QueryInterface(IID_IMultiLanguage, &v2)` 这种标准
     * 二参调用会因"没有匹配的重载"而编译失败。 */
    using IUnknown::QueryInterface;

    virtual HRESULT STDMETHODCALLTYPE GetFamilyCodePage(UINT uiCodePage, UINT *puiFamilyCodePage) = 0;

    virtual HRESULT STDMETHODCALLTYPE IsConvertible(DWORD dwSrcEncoding, DWORD dwDstEncoding) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertStringToUnicode(DWORD *pdwMode, DWORD dwEncoding, CHAR *pSrcStr, UINT *pcSrcSize, WCHAR *pDstStr, UINT *pcDstSize) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertStringFromUnicode(DWORD *pdwMode, DWORD dwEncoding, WCHAR *pSrcStr, UINT *pcSrcSize, CHAR *pDstStr, UINT *pcDstSize) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertStringReset(void) = 0;

    /* MSVC 的 <unknwn.h> 给 IUnknown 挂了一个模板版 QueryInterface，因此
     * FontCacheWin.cpp 里能写 `multiLanguage->QueryInterface(&langFontLink)`；
     * swinx 的 unknwn.h 是 C 风格接口、没有那个模板，这里补一个语义相同的
     * 重载，让同一份源码在两个平台上都能编过（不引入 __uuidof 依赖）。 */
    template <class Q>
    HRESULT QueryInterface(Q **pp)
    {
        if (!pp)
            return E_POINTER;
        // static_cast rather than a qualified call: "IUnknown::QueryInterface(...)"
        // would suppress virtual dispatch and turn into a direct call on the pure
        // virtual (an undefined symbol at link time). Going through a base pointer
        // both dodges the self-recursive name lookup above and dispatches.
        return static_cast<IUnknown *>(this)->QueryInterface(Q::GetIID(), reinterpret_cast<void **>(pp));
    }
};

/* interface IMultiLanguage2
 *
 * IMultiLanguage 之上再加转换/检测/码页信息这四组方法。POSIX 上没有真正的
 * mlang 对象，vtable 顺序由本头定义（与 SDK 的排列一致，方便按 SDK 写的
 * 调用点直接编过）。实现见 src/platform/posix/mlang.cpp：
 *   - 转换：复用 UTF-8 / Latin-1 编解码，lpFallBack / USEDEFCHAR 是真兑现；
 *   - 检测：BOM + UTF-8 合法性 + 首选码页，给出可信度排序；
 *   - 码页信息：描述 / 可安装性 / MIME 选项来自本地表与成员状态。
 * ConvertStringInIStream 是 MIME(RFC1522/2047) 语义，本实现明确返回
 * E_NOTIMPL，见实现处说明。 */
struct IMultiLanguage2 : public IMultiLanguage
{
    DECLARE_CLASS_SIID(IID_IMultiLanguage2)

    virtual HRESULT STDMETHODCALLTYPE ConvertStringInIStream(DWORD *pdwMode, DWORD dwFlag, WCHAR *lpFallBack, IStream *pstmIn, IStream *pstmOut) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertStringToUnicodeEx(DWORD *pdwMode, DWORD dwEncoding, CHAR *pSrcStr, UINT *pcSrcSize, WCHAR *pDstStr, UINT *pcDstSize, DWORD dwFlag, WCHAR *lpFallBack) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertStringFromUnicodeEx(DWORD *pdwMode, DWORD dwEncoding, WCHAR *pSrcStr, UINT *pcSrcSize, CHAR *pDstStr, UINT *pcDstSize, DWORD dwFlag, WCHAR *lpFallBack) = 0;

    virtual HRESULT STDMETHODCALLTYPE DetectCodepageInIStream(DWORD dwFlag, DWORD dwPrefWinCodePage, IStream *pstmIn, DetectEncodingInfo *lpEncoding, ULONG *pnNumEncoding) = 0;

    virtual HRESULT STDMETHODCALLTYPE DetectInputCodepage(DWORD dwFlag, DWORD dwPrefWinCodePage, CHAR *pSrcStr, INT *pcSrcSize, DetectEncodingInfo *lpEncoding, INT *pnNumEncoding) = 0;

    virtual HRESULT STDMETHODCALLTYPE ValidateCodePage(UINT uiCodePage, HWND hwnd) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetCodePageDescription(UINT uiCodePage, LPWSTR lpWideCharStr, int cchWideChar) = 0;

    virtual HRESULT STDMETHODCALLTYPE IsCodePageInstallable(UINT uiCodePage) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetMimeDecodeOptions(DWORD dwMimeDecodeOptions) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetMimeDecodeOptions(DWORD *pdwMimeDecodeOptions) = 0;
};

#else /* C 风格 */

/* C 调用方在本工程里不存在；给出不透明类型即可，方法调用请走 C++。 */
typedef struct IMLangCodePagesVtbl IMLangCodePagesVtbl;
typedef struct IMLangFontLink2Vtbl IMLangFontLink2Vtbl;
typedef struct IMultiLanguageVtbl IMultiLanguageVtbl;
typedef struct IMultiLanguage2Vtbl IMultiLanguage2Vtbl;

#endif /* __cplusplus */

#endif /* _MLANG_H_ */
