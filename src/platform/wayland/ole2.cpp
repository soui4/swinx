/*
 * swinx/platform/wayland/ole2.cpp
 *
 * OLE 拖放入口。SHostWnd::EnableDragDrop() 调 ::RegisterDragDrop(m_hWnd,
 * GetDropTarget())，SOUI/src/core/Swnd.cpp:4680 的 SWindow::RegisterDragDrop
 * 走容器版 SwndContainerImpl::RegisterDragDrop。
 *
 * 这里只做转发：IDropTarget 的登记、wl_data_offer -> IDataObject 的桥、
 * IDropSource 的 QueryContinueDrag 询价都在 SConnection（SConnection.cpp）。
 */
#include <windows.h>
#include <ole2.h>
#include <string.h>
#include "SConnection.h"

HRESULT CoInitialize(LPVOID pvReserved)
{
    return CoInitializeEx(pvReserved, 0);
}

HRESULT CoInitializeEx(void *pvReserved __attribute__((unused)), DWORD dwCoInit __attribute__((unused)))
{
    return S_OK;
}

void CoUninitialize(void)
{
}

HRESULT WINAPI RegisterDragDrop(HWND hWnd, LPDROPTARGET pTarget)
{
    return SConnMgr::instance()->getConnection()->RegisterDragDrop(hWnd, pTarget);
}

HRESULT WINAPI RevokeDragDrop(HWND hWnd)
{
    return SConnMgr::instance()->getConnection()->RevokeDragDrop(hWnd);
}

/* Scintilla 的拖放（ScintillaWin::DoDragDrop）走这里；SConnection 用
 * wl_data_device.start_drag 启动，并在嵌套事件泵里询价 IDropSource。 */
HRESULT WINAPI DoDragDrop(LPDATAOBJECT pDataObj, LPDROPSOURCE pDropSource, DWORD dwOKEffects, LPDWORD pdwEffect)
{
    return SConnMgr::instance()->getConnection()->DoDragDrop(
        pDataObj, pDropSource, dwOKEffects, pdwEffect);
}

/* 剪贴板的 IDataObject 通道 —— 与 linux 平台（platform/linux/ole2.cpp）同款
 * 语义：平台只负责 OpenClipboard/SetClipboardData/GetClipboardData 这一组
 * Win32 API（本端口由 SClipboard 落在进程内 map），Ole 层只做
 * 数据对象的登记与取回，格式协商由数据对象自身完成。
 *
 *  - 复制（Ctrl+C/X）：re41 CopyRangeToClipboard -> OleSetClipboard(pdo)。
 *    OpenClipboard(0) -> SClipboard::setDataObject(pdo)（登记 + 即时抽取纯文本
 *    到进程内存储）-> CloseClipboard。
 *  - 粘贴（Ctrl+V）：PasteDataObjectToRange -> OleGetClipboard：
 *    同实例复制取回登记的外部对象；跨实例则返回覆盖进程内存储的内部适配器。
 *
 * ReleaseStgMedium 不是能力问题而是内存所有权约定，必须真实现，否则每个
 * STGMEDIUM 都会泄漏。 */
#include "SConnection.h"

HRESULT WINAPI OleSetClipboard(IDataObject *pdo)
{
    if (!OpenClipboard(0))
        return CLIPBRD_E_CANT_OPEN;
    SConnection *pconn = SConnMgr::instance()->getConnection();
    pconn->getClipboard()->setDataObject(pdo);
    CloseClipboard();
    return S_OK;
}

HRESULT WINAPI OleGetClipboard(IDataObject **ppdo)
{
    if (!ppdo)
        return E_INVALIDARG;
    SConnection *pconn = SConnMgr::instance()->getConnection();
    *ppdo = pconn->getClipboard()->getDataObject();
    return S_OK;
}

/* 阶段 0 没有延迟渲染：setDataObject 已即时抽取，无东西可 flush */
HRESULT WINAPI OleFlushClipboard(void)
{
    return S_OK;
}

HRESULT WINAPI OleIsCurrentClipboard(IDataObject *pdo)
{
    SConnection *pconn = SConnMgr::instance()->getConnection();
    return pconn->getClipboard()->isCurrentClipboard(pdo) ? S_OK : S_FALSE;
}

void WINAPI ReleaseStgMedium(STGMEDIUM *pmedium)
{
    if (!pmedium)
        return;

    switch (pmedium->tymed)
    {
    case TYMED_HGLOBAL:
        if (!pmedium->pUnkForRelease && pmedium->hGlobal)
            GlobalFree(pmedium->hGlobal);
        break;
    case TYMED_FILE:
        if (pmedium->lpszFileName)
        {
            if (!pmedium->pUnkForRelease)
                DeleteFileW(pmedium->lpszFileName);
            CoTaskMemFree(pmedium->lpszFileName);
        }
        break;
    case TYMED_ISTREAM:
        if (pmedium->pstm)
            pmedium->pstm->Release();
        break;
    case TYMED_ISTORAGE:
        if (pmedium->pstg)
            pmedium->pstg->Release();
        break;
    default:
        break;
    }

    if (pmedium->pUnkForRelease)
        pmedium->pUnkForRelease->Release();

    pmedium->tymed = TYMED_NULL;
    pmedium->pUnkForRelease = NULL;
    pmedium->hGlobal = NULL;
}

HANDLE WINAPI OleDuplicateData(HANDLE hSrc, CLIPFORMAT cfFormat, UINT uiFlags)
{
    // richedit41 用 CF_METAFILEPICT 等 HGLOBAL 格式调这里，要一份独立副本。
    // 位图格式需要 GDI 对象引用计数，而本端口的剪贴板只保存 HGLOBAL 数据，
    // 按不支持返回；其余格式按 GlobalSize 原样复制。
    if (!hSrc || cfFormat == CF_BITMAP)
        return NULL;
    HGLOBAL hSrcMem = (HGLOBAL)hSrc;
    SIZE_T cb = GlobalSize(hSrcMem);
    if (cb == 0)
        return NULL;
    HGLOBAL hDup = GlobalAlloc(uiFlags, cb);
    if (!hDup)
        return NULL;
    void *psrc = GlobalLock(hSrcMem);
    void *pdst = GlobalLock(hDup);
    if (!psrc || !pdst)
    {
        GlobalUnlock(hDup);
        GlobalUnlock(hSrcMem);
        GlobalFree(hDup);
        return NULL;
    }
    memcpy(pdst, psrc, cb);
    GlobalUnlock(hDup);
    GlobalUnlock(hSrcMem);
    return hDup;
}
