/*
 * SClipboard.h -- in-memory clipboard for the wasm port.  A browser page has
 * no OS clipboard service, so formats live in a per-connection map; the
 * browser clipboard itself is bridged by the page (Module.souixClipboardWrite
 * on writes; the 'paste' event pushes browser text back through
 * SwinxClipboardSetBrowserText).  Interface mirrors SConnection's clipboard
 * methods (which forward here), same as the mobile layout.
 */
#ifndef _SWINX_WASM_SCLIPBOARD_H_
#define _SWINX_WASM_SCLIPBOARD_H_

#include <windows.h>
#include "objidl.h"
#include "SUnkImpl.h"
#include <map>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

class SClipboard;

// 内部 IDataObject 适配器：把 SClipboard 的句柄式存储（m_data）暴露成
// IDataObject，供 OleGetClipboard 返回、re41 的 PasteDataObjectToRange
// 走 GetData 读取。GetData 一律返回副本（linux SDataObjectProxy 同款语义：
// 调用方 ReleaseStgMedium 释放副本，剪贴板持有的原件不受影响）。
// 与 linux 的 SMimeData 相比砍掉了 EnumFormatEtc/SetData 等协商面 —— re41
// 的纯文本粘贴路径只按 FORMATETC 逐个 GetData，用不到枚举。
class SClipboardDataObject : public SUnkImpl<IDataObject> {
  public:
    explicit SClipboardDataObject(SClipboard *pClip)
        : m_pClip(pClip)
    {
    }

    HRESULT WINAPI GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override;
    HRESULT WINAPI QueryGetData(FORMATETC *pformatetc) override;

    HRESULT WINAPI GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium) override
    {
        (void)pformatetc;
        (void)pmedium;
        return E_NOTIMPL;
    }
    HRESULT WINAPI SetData(FORMATETC *pformatetc, STGMEDIUM *pmedium, BOOL fRelease) override
    {
        (void)pformatetc;
        (void)pmedium;
        (void)fRelease;
        return E_NOTIMPL;
    }
    HRESULT WINAPI GetCanonicalFormatEtc(FORMATETC *pformatectIn, FORMATETC *pformatetcOut) override
    {
        (void)pformatectIn;
        (void)pformatetcOut;
        return E_NOTIMPL;
    }
    HRESULT WINAPI EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override
    {
        (void)dwDirection;
        (void)ppenumFormatEtc;
        return E_NOTIMPL;
    }
    HRESULT WINAPI DAdvise(FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection) override
    {
        (void)pformatetc;
        (void)advf;
        (void)pAdvSink;
        (void)pdwConnection;
        return E_NOTIMPL;
    }
    HRESULT WINAPI DUnadvise(DWORD dwConnection) override
    {
        (void)dwConnection;
        return E_NOTIMPL;
    }
    HRESULT WINAPI EnumDAdvise(IEnumSTATDATA **ppenumAdvise) override
    {
        (void)ppenumAdvise;
        return E_NOTIMPL;
    }

    IUNKNOWN_BEGIN(IDataObject)
    IUNKNOWN_END()

  private:
    SClipboard *m_pClip;
};

class SClipboard {
public:
    SClipboard();
    ~SClipboard();

    BOOL emptyClipboard();
    BOOL hasFormat(UINT format);
    BOOL openClipboard(HWND hWndNewOwner);
    BOOL closeClipboard();
    HWND getClipboardOwner();
    HANDLE getClipboardData(UINT uFormat);
    HANDLE setClipboardData(UINT uFormat, HANDLE hMem);

    // IDataObject 桥（linux 平台同款语义，见 platform/linux/SClipboard.h）：
    //  - setDataObject：OleSetClipboard 登记 re41 传来的数据对象（AddRef 持有），
    //    并立即抽取纯文本到 m_data —— wasm 没有 OS 剪贴板可延迟 flush，浏览器
    //    剪贴板（PushTextToBrowser）就是外部终点，抽取必须即时完成。
    //  - getDataObject：OleGetClipboard 取回。同实例复制粘贴期间直接返回
    //    登记的外部对象（保真 RTF 等全部格式）；否则返回覆盖 m_data 的内部
    //    适配器（浏览器 paste 事件经 setBrowserText 落库的文本走这条路）。
    BOOL setDataObject(IDataObject *pDo);
    IDataObject *getDataObject();
    BOOL isCurrentClipboard(IDataObject *pDo);

    // 浏览器剪贴板文本（UTF-8）落到进程内剪贴板：存成 CF_UNICODETEXT
    // （richedit 内部按 wide 请求）和 CF_TEXT（本构建 ANSI 串即 UTF-8 字节）。
    void setBrowserText(const char *pszUtf8);

    static UINT RegisterClipboardFormatA(LPCSTR pszName);

private:
    std::recursive_mutex m_mutex;
    HWND m_hOwner;
    std::atomic<bool> m_bOpen;
    std::map<UINT, HANDLE> m_data;   // format -> HGLOBAL（剪贴板持有，替换时释放旧值）
    IDataObject *m_doExClip;         // OleSetClipboard 登记的外部数据对象（AddRef 持有）
    SClipboardDataObject *m_doClip;  // 内部适配器（惰性创建，只读视图）

    // registered (non-standard) format name table.  Function-local statics,
    // NOT namespace-scope members: bare metal never runs __libc_init_array,
    // so a namespace-scope mutex/map would be constructed only by accident.
    struct FmtTable
    {
        std::recursive_mutex mutex;
        std::map<std::string, UINT> names;
        UINT nextFmt = 0xC000;
    };
    static FmtTable &fmtTable();
};

#endif // _SWINX_WASM_SCLIPBOARD_H_
