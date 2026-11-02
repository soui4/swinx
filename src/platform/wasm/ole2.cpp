/*
 * swinx/platform/wasm/ole2.cpp
 *
 * OLE 拖放的 wasm 实现。引用面来自 SOUI：SHostWnd::EnableDragDrop() 调
 * ::RegisterDragDrop(m_hWnd, GetDropTarget())，SOUI/src/core/Swnd.cpp:4680 的
 * SWindow::RegisterDragDrop 走容器版 SwndContainerImpl::RegisterDragDrop。
 *
 * 两个函数都显式返回失败，不学 mobile/ole2.cpp 那样在没登记点的情况下回 S_OK：
 * 浏览器里的拖放是 HTML5 DnD 事件（dragover/drop），坐标与数据都在 DOM 侧，
 * swinx 的 wasm SConnection 没有对应的入口；返回成功只会让 IDropTarget 永远
 * 收不到 DragEnter，把「不支持」伪装成「支持但坏了」。
 *
 * 补齐路径（阶段 3+）：在 src/platform/wasm/input.cpp 里把 dragover/drop 转成
 * IDropTarget 的 DragEnter/Drop，并让 swinxDrag* 携带 DataTransfer 文本，
 * 那时再把这里改成真实登记。
 */
#include <windows.h>
#include <ole2.h>

HRESULT WINAPI RegisterDragDrop(HWND hWnd, LPDROPTARGET pTarget)
{
    (void)hWnd;
    (void)pTarget;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return E_FAIL;
}

HRESULT WINAPI RevokeDragDrop(HWND hWnd)
{
    (void)hWnd;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return E_FAIL;
}

/* Scintilla 的拖放（ScintillaWin::DoDragDrop）走这里。浏览器侧的拖放是 HTML5 DnD
 * 事件，不是 OLE 的 IDropSource 拉取-放下模型。返回 E_NOTIMPL 并把 *pdwEffect 置为
 * DROPEFFECT_NONE，调用方据此知道没有被接受，不会卡在自己的消息循环里等 DragEnter。 */
HRESULT WINAPI DoDragDrop(LPDATAOBJECT pDataObj, LPDROPSOURCE pDropSource, DWORD dwOKEffects, LPDWORD pdwEffect)
{
    (void)pDataObj;
    (void)pDropSource;
    (void)dwOKEffects;
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return E_NOTIMPL;
}

/* 剪贴板的 IDataObject 通道 —— 与 linux 平台（platform/linux/ole2.cpp）同款
 * 语义：平台只负责 OpenClipboard/SetClipboardData/GetClipboardData 这一组
 * Win32 API（wasm 里由 SClipboard 落在进程内 map + 浏览器桥），Ole 层只做
 * 数据对象的登记与取回，格式协商由数据对象自身完成。
 *
 *  - 复制（Ctrl+C/X）：re41 CopyRangeToClipboard -> OleSetClipboard(pdo)。
 *    OpenClipboard(0) -> SClipboard::setDataObject(pdo)（登记 + 即时抽取纯文本
 *    到进程内存储并推浏览器）-> CloseClipboard。
 *  - 粘贴（Ctrl+V / 浏览器 paste 事件注入）：PasteDataObjectToRange ->
 *    OleGetClipboard：同实例复制取回登记的外部对象；浏览器文本（经
 *    SwinxClipboardSetBrowserText 落库）则返回覆盖进程内存储的内部适配器。
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

/* wasm 没有延迟渲染：setDataObject 已即时抽取，无东西可 flush */
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
