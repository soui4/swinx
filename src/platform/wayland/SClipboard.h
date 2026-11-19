/*
 * SClipboard.h -- Wayland 后端剪贴板（跨进程，wl_data_device）。
 *
 * Win32 语义的句柄式存储仍在进程内，但 setClipboardData 会同时把它发布成
 * wl_data_source / selection；getClipboardData 在本进程没有副本时用
 * wl_data_offer.receive 从当前 owner 读一次并缓存。mime 与 CF_* 的映射、
 * UTF-8 <-> wchar_t 转换都在 SConnection.cpp 的剪贴板辅助函数里。
 *
 * 与 X11 后端的差别：X11 直接读 X selection property；Wayland 的数据传输是
 * fd + 事件泵，读取是同步嵌套 dispatch（最长 3 秒）。
 */
#ifndef _SWINX_WAYLAND_SCLIPBOARD_H_
#define _SWINX_WAYLAND_SCLIPBOARD_H_

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
    //    并立即抽取纯文本到 m_data（没有外部剪贴板服务可延迟 flush）。
    //  - getDataObject：OleGetClipboard 取回。同实例复制粘贴期间直接返回
    //    登记的外部对象（保真 RTF 等全部格式）；否则返回覆盖 m_data 的内部
    //    适配器。
    BOOL setDataObject(IDataObject *pDo);
    IDataObject *getDataObject();
    BOOL isCurrentClipboard(IDataObject *pDo);

    static UINT RegisterClipboardFormatA(LPCSTR pszName);
    // 只查表、不分配新 id：mime -> CF_* 解析时必须用它，否则每个未知 mime
    // 都会注册出一个新格式，污染格式表且数字 id 兜底永远走不到。
    static UINT FormatIdA(LPCSTR pszName);
    static std::string FormatName(UINT fmt);   // 注册格式名；未知格式返回空串
    static std::vector<std::pair<std::string, UINT> > RegisteredFormats();

    // wl_data_source.send 的字节源（分块读取，直到返回 0）
    int readForSelection(const char *mime, void *buf, int cap);

private:
    std::recursive_mutex m_mutex;
    HWND m_hOwner;
    std::atomic<bool> m_bOpen;
    std::map<UINT, HANDLE> m_data;   // format -> HGLOBAL（剪贴板持有，替换时释放旧值）
    unsigned int m_selGeneration;   // 上次同步到的 wl selection 代次
    bool m_bOwnsSelection;          // 当前 selection 是否由本客户端发布
    std::vector<unsigned char> m_publishBytes; // wl_data_source.send 的分块缓冲
    std::string m_publishMime;      // 当前缓冲对应的 mime（换 mime 时重新取整块）
    size_t m_publishPos;
    IDataObject *m_doExClip;         // OleSetClipboard 登记的外部数据对象（AddRef 持有）
    SClipboardDataObject *m_doClip;  // 内部适配器（惰性创建，只读视图）

    // registered (non-standard) format name table.  Function-local statics,
    // NOT namespace-scope members: 进程级状态不要依赖命名空间全局对象的构造。
    struct FmtTable
    {
        std::recursive_mutex mutex;
        std::map<std::string, UINT> names;
        UINT nextFmt = 0xC000;
    };
    static FmtTable &fmtTable();
    void publishSelection();   // 把 m_data 作为 selection 发布
    void syncSelection();      // 按 wl selection 代次失效远程缓存
};

#endif // _SWINX_WAYLAND_SCLIPBOARD_H_
