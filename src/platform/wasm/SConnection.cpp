/*
 * SConnection.cpp -- WebAssembly platform connection implementation.
 *
 * Forked from src/platform/freertos/SConnection.cpp, whose message-queue
 * semantics come from src/platform/mobile/SConnection.cpp (WM_PAINT /
 * WM_MOUSEMOVE / WM_TIMER coalescing, msg stack, callback tasks).  What
 * changes on wasm:
 *   - the blocking primitives are the POSIX-port Win32-compat events
 *     (src/platform/posix/sysobjs.cpp) rather than FreeRTOS-backed ones, so
 *     waitMsg() blocks in WaitForSingleObject(m_hQueueEvt), which is a
 *     std::condition_variable under the hood;
 *   - timers are driven inside updateMsgQueue() from GetTickCount().  A
 *     browser tab has no thread to spare: the JS host pumps from
 *     requestAnimationFrame, so that pump must use a finite timeout (or
 *     peekMsg) -- blocking the main thread would stop rAF and freeze the page
 *   - rendering: window canvases are cairo image surfaces (software), and
 *     commitCanvas() blits dirty regions into the framebuffer
 *     (framebuffer.h), whose present callback hands pixels to JS.
 */
#include "SConnection.h"
#include "wndobj.h"
#include "framebuffer.h"
#include "lowmem.h"
#include <gdi.h>
#ifdef SWINX_WASM_CANVAS2D
#include "gdi/canvas2d/js_glue.h"
#else
#include <cairo.h>
#endif
#include <log.h>
#include <algorithm>
#include <functional>
#include <sched.h> // sched_yield(): replaces the fork's FreeRTOS taskYIELD() in the wait-poll loop
#include <emscripten.h> // EM_ASM：PushTextToBrowser 把剪贴板文本推给页面
#if defined(__wasm_simd128__)
#include <wasm_simd128.h> // wasm_v8x16_swizzle：wasm 版 pshufb，移植 soui-android-lib 的 BGRA→RGBA 换序
#endif

#define kLogTag "SConnection"

// Plain-value snapshot of the window registry (kept from the freertos fork:
// to navigate parent / child / sibling without locking every other window).  See
// WndMgr::enumerate().
namespace {
struct WndBasicInfo
{
    HWND hwnd;
    HWND parent;
    HWND owner;
    BOOL bDestroyed;
};
} // namespace

static UINT s_nextRegisteredMessage = WM_USER + 100000;

SClipboard::FmtTable &SClipboard::fmtTable()
{
    // leak-on-purpose: format names live for the process lifetime
    static FmtTable *s_table = new FmtTable();
    return *s_table;
}

//=============================================================================
// SClipboard (in-memory)
//=============================================================================

SClipboard::SClipboard()
    : m_hOwner(0)
    , m_bOpen(false)
    , m_doExClip(NULL)
    , m_doClip(NULL)
{
}

SClipboard::~SClipboard()
{
    if (m_doExClip)
        m_doExClip->Release();
    if (m_doClip)
        m_doClip->Release();
}

UINT SClipboard::RegisterClipboardFormatA(LPCSTR pszName)
{
    if (!pszName || !*pszName)
        return 0;
    FmtTable &table = fmtTable();
    std::lock_guard<std::recursive_mutex> lock(table.mutex);
    std::string name(pszName);
    auto it = table.names.find(name);
    if (it != table.names.end())
        return it->second;
    UINT fmt = table.nextFmt++;
    table.names[name] = fmt;
    return fmt;
}

BOOL SClipboard::emptyClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_doExClip)
    {
        m_doExClip->Release();
        m_doExClip = NULL;
    }
    for (std::map<UINT, HANDLE>::iterator it = m_data.begin(); it != m_data.end(); ++it)
    {
        if (it->second)
            GlobalFree(it->second);
    }
    m_data.clear();
    return TRUE;
}

BOOL SClipboard::hasFormat(UINT format)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::map<UINT, HANDLE>::iterator it = m_data.find(format);
    return it != m_data.end() && it->second != NULL;
}

BOOL SClipboard::openClipboard(HWND hWndNewOwner)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_bOpen)
        return FALSE;
    m_bOpen = true;
    m_hOwner = hWndNewOwner;
    return TRUE;
}

BOOL SClipboard::closeClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_bOpen)
        return FALSE;
    m_bOpen = false;
    return TRUE;
}

HWND SClipboard::getClipboardOwner()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_hOwner;
}

HANDLE SClipboard::getClipboardData(UINT uFormat)
{
    // Win32 语义：返回的句柄归剪贴板所有，调用方不释放；句柄在数据被替换
    // （setClipboardData/emptyClipboard）前一直有效。
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::map<UINT, HANDLE>::iterator it = m_data.find(uFormat);
    return it != m_data.end() ? it->second : NULL;
}

// 把文本格式的内容推给页面（Module.souixClipboardWrite -> navigator.clipboard）。
// wasm 构建全模块一致用 emscripten 原生 wchar_t（4 字节，-DWCHAR_SIZE=4，
// 无 -fshort-wchar），所以 CF_UNICODETEXT 实际是 UTF-32LE —— JS 侧按 u32
// 代码点取字符；CF_TEXT 由 re41 现场渲染时不可靠（见 setDataObject 注释），
// 这里只兜底处理 setBrowserText 自己落的 UTF-8。
static void PushTextToBrowser(UINT uFormat, HANDLE hMem)
{
    const void *pData = GlobalLock(hMem);
    if (!pData)
        return;
    size_t cb = GlobalSize(hMem);
    if (uFormat == CF_UNICODETEXT)
    {
        // trim trailing NULs so JS sees the real length
        const wchar_t *pw = (const wchar_t *)pData;
        size_t cwch = cb / sizeof(wchar_t);
        while (cwch > 0 && pw[cwch - 1] == 0)
            cwch--;
        if (cwch)
            EM_ASM({
                if (typeof Module.souixClipboardWrite === "function") {
                    const dv = new DataView(Module.HEAPU8.buffer);
                    let s = "";
                    for (let i = 0; i < $1; i++) {
                        const cp = dv.getUint32($0 + i * 4, true);
                        if (cp >= 0x110000) continue;
                        s += String.fromCodePoint(cp);
                    }
                    Module.souixClipboardWrite(s);
                }
            }, (uintptr_t)pData, (int)cwch);
    }
    else if (uFormat == CF_TEXT)
    {
        const char *pc = (const char *)pData;
        size_t cch = cb;
        while (cch > 0 && pc[cch - 1] == 0)
            cch--;
        if (cch)
            EM_ASM({
                if (typeof Module.souixClipboardWrite === "function")
                    Module.souixClipboardWrite(UTF8ToString($0, $1));
            }, (uintptr_t)pData, (int)cch);
    }
    GlobalUnlock(hMem);
}

HANDLE SClipboard::setClipboardData(UINT uFormat, HANDLE hMem)
{
    // Win32 语义：成功后句柄归剪贴板所有（NULL hMem 表示延迟渲染，本端口
    // 不支持，按失败返回 0 处理，调用方会 GlobalFree 自己的句柄）。
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!hMem)
        return NULL;
    std::map<UINT, HANDLE>::iterator it = m_data.find(uFormat);
    if (it != m_data.end() && it->second && it->second != hMem)
        GlobalFree(it->second);
    m_data[uFormat] = hMem;
    // 只推 CF_UNICODETEXT：re41 渲染的 CF_TEXT 实际装的是 UTF-16 字节（swinx 的
    // WideCharToMultiByte(CP_ACP) 在 wasm 上没有真实的 ACP 转换），按 UTF-8 解读会
    // 得到"字\u0000符\u0000"的乱码，先写一次坏内容靠后一次覆盖不可靠。
    // setBrowserText 写入的 CF_TEXT 是我们自己构造的 UTF-8，但浏览器剪贴板以
    // CF_UNICODETEXT 为权威来源即可，无需双推。
    if (uFormat == CF_UNICODETEXT)
        PushTextToBrowser(uFormat, hMem);
    return hMem;
}

void SClipboard::setBrowserText(const char *pszUtf8)
{
    if (!pszUtf8 || !*pszUtf8)
        return;
    // 浏览器剪贴板是新的权威内容：作废 OleSetClipboard 登记的旧数据对象，
    // 否则 getDataObject 仍会优先返回它，Ctrl+V 贴出的是复制时的旧文本。
    emptyClipboard();
    // UTF-8 -> 宽字符（MultiByteToWideChar 按宿主 wchar_t 走：wasm 上是
    // 4 字节 UTF-32LE，与 re41 的 -DWCHAR_SIZE=4 一致）
    int cwch = MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, NULL, 0);
    if (cwch <= 0)
        return;
    HANDLE hW = GlobalAlloc(GMEM_MOVEABLE, (size_t)cwch * sizeof(wchar_t));
    HANDLE hA = GlobalAlloc(GMEM_MOVEABLE, strlen(pszUtf8) + 1);
    if (!hW || !hA)
    {
        if (hW) GlobalFree(hW);
        if (hA) GlobalFree(hA);
        return;
    }
    wchar_t *pw = (wchar_t *)GlobalLock(hW);
    MultiByteToWideChar(CP_UTF8, 0, pszUtf8, -1, pw, cwch);
    GlobalUnlock(hW);
    void *pa = GlobalLock(hA);
    memcpy(pa, pszUtf8, strlen(pszUtf8) + 1);
    GlobalUnlock(hA);

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // 直接落库（不走 setClipboardData：外部文本不需要再推回浏览器）
    std::map<UINT, HANDLE>::iterator it;
    it = m_data.find(CF_UNICODETEXT);
    if (it != m_data.end() && it->second)
        GlobalFree(it->second);
    m_data[CF_UNICODETEXT] = hW;
    it = m_data.find(CF_TEXT);
    if (it != m_data.end() && it->second)
        GlobalFree(it->second);
    m_data[CF_TEXT] = hA;
}

// 页面把浏览器剪贴板文本（'paste' 事件里的 clipboardData，同步可得）推进来。
// 之所以走事件而不走 navigator.clipboard.readText()：后者要权限且是异步的，
// 而 SOUI 编辑框的 WM_PASTE 是同步取 GetClipboardData —— paste 事件先落库，
// 再由页面注入 Ctrl+V，顺序就对了。
extern "C" int SwinxClipboardSetBrowserText(const char *pszUtf8)
{
    SConnMgr::instance()->getConnection()->getClipboard()->setBrowserText(pszUtf8);
    return 1;
}

//-----------------------------------------------------------------------------
// IDataObject 桥（linux 平台同款语义，见 platform/linux/SClipboard.cpp）
//-----------------------------------------------------------------------------

// GetData 返回副本：调用方（re41 粘贴路径）会 ReleaseStgMedium 释放它，
// 剪贴板持有的原件必须不受影响（linux SDataObjectProxy::GetData 同款处理）。
HRESULT SClipboardDataObject::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    if (!pformatetcIn || !pmedium)
        return E_INVALIDARG;
    pmedium->tymed = TYMED_NULL;
    pmedium->hGlobal = NULL;
    pmedium->pUnkForRelease = NULL;
    if (pformatetcIn->tymed != TYMED_HGLOBAL)
        return DV_E_TYMED;
    HANDLE h = m_pClip->getClipboardData(pformatetcIn->cfFormat);
    if (!h)
        return DV_E_FORMATETC;
    SIZE_T cb = GlobalSize(h);
    HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, cb ? cb : 1);
    if (!hCopy)
        return STG_E_MEDIUMFULL;
    void *psrc = GlobalLock(h);
    if (psrc)
    {
        void *pdst = GlobalLock(hCopy);
        memcpy(pdst, psrc, cb);
        GlobalUnlock(hCopy);
        GlobalUnlock(h);
    }
    pmedium->tymed = TYMED_HGLOBAL;
    pmedium->hGlobal = hCopy;
    return S_OK;
}

HRESULT SClipboardDataObject::QueryGetData(FORMATETC *pformatetc)
{
    if (!pformatetc)
        return E_INVALIDARG;
    if (pformatetc->tymed != TYMED_HGLOBAL)
        return DV_E_TYMED;
    return m_pClip->hasFormat(pformatetc->cfFormat) ? S_OK : DV_E_FORMATETC;
}

BOOL SClipboard::setDataObject(IDataObject *pDo)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_bOpen)
        return FALSE;
    // 先按 Win32 语义清空当前剪贴板（顺带释放旧的外部对象）
    emptyClipboard();
    if (!pDo)
        return TRUE;

    m_doExClip = pDo;
    m_doExClip->AddRef();

    // wasm 特有：没有 OS 剪贴板可延迟 flush，浏览器剪贴板（PushTextToBrowser）
    // 就是外部终点，纯文本必须立即抽取落库。顺带让 ::GetClipboardData 兜底
    // 路径（re41 的 ole-less 分支、IsClipboardFormatAvailable）能读到这份复制。
    // 只抽 CF_UNICODETEXT：re41 现场渲染的 CF_TEXT 走 WideCharToMultiByte(CP_ACP)，
    // wasm 上没有真实 ACP 转换，内容是 UTF-16 字节伪 ANSI，存进来反而会在
    // 粘贴按格式序尝试时先命中 CF_TEXT 而贴出乱码。
    FORMATETC fmt;
    STGMEDIUM med;
    memset(&fmt, 0, sizeof(fmt));
    fmt.cfFormat = CF_UNICODETEXT;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;
    if (pDo->GetData(&fmt, &med) == S_OK && med.hGlobal)
    {
        SIZE_T cb = GlobalSize(med.hGlobal);
        void *psrc = GlobalLock(med.hGlobal);
        if (psrc && cb)
        {
            size_t cwch = cb / sizeof(wchar_t);
            const wchar_t *pw = (const wchar_t *)psrc;
            while (cwch > 0 && pw[cwch - 1] == 0)
                cwch--;
            HANDLE hCopy = GlobalAlloc(GMEM_MOVEABLE, (cwch + 1) * sizeof(wchar_t));
            if (hCopy)
            {
                wchar_t *pdst = (wchar_t *)GlobalLock(hCopy);
                memcpy(pdst, psrc, cwch * sizeof(wchar_t));
                pdst[cwch] = 0;
                GlobalUnlock(hCopy);
                setClipboardData(CF_UNICODETEXT, hCopy);
            }
            GlobalUnlock(med.hGlobal);
        }
        // 副本已拷出；med 里的 hGlobal 是数据对象给的临时缓冲，按所有权约定释放
        ReleaseStgMedium(&med);
    }
    return TRUE;
}

IDataObject *SClipboard::getDataObject()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_doExClip)
    {
        // 同实例复制粘贴：直接回登记的数据对象，RTF 等全部格式保真
        m_doExClip->AddRef();
        return m_doExClip;
    }
    if (!m_doClip)
        m_doClip = new SClipboardDataObject(this);
    m_doClip->AddRef();
    return m_doClip;
}

BOOL SClipboard::isCurrentClipboard(IDataObject *pDo)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return pDo && (pDo == m_doExClip || pDo == m_doClip);
}

//=============================================================================
// SConnection Implementation
//=============================================================================

SConnection::SConnection(int screenNum)
    : m_msgPeek(nullptr)
    , m_bMsgNeedFree(false)
    , m_bBlockTimer(false)
    , m_nextTimerId(1)
    , m_tsLastMsg(0)
    , m_bQuit(false)
    , m_tid(GetCurrentThreadId())
    , m_deskDC(new _SDC(0))
    , m_deskBmp(nullptr)
    , m_screenNum(screenNum)
    , m_hWndCapture(0)
    , m_hFocus(0)
    , m_hActive(0)
    , m_hForeground(0)
    , m_nextRegMsg(WM_USER + 100000)
    , m_cursorCount(1)
    /* 虚拟屏幕 = 帧缓冲（物理分辨率）：DPI 缩放由 SOUI 的 SetScale 在布局层
     * 完成，应用坐标 == 物理像素，wasm 宿主窗占满整个 fb，弹出窗的上下翻转判定
     * （SComboBase::CalcPopupRect -> GetMonitorInfo）以此为基准。原来写死的
     * 320x240 会让大宿主里的下拉框一律误判"下方放不下"而弹到上方。 */
    , m_screenW(SwinxFbWidth())
    , m_screenH(SwinxFbHeight())
    , m_msgRecursion(0)
    , m_bScreenDirty(false)
    , m_caretBlinkTime(TS_CARET)
    , m_clipboard(new SClipboard())
{
    /* 第二条自检入口：只画图、不建窗口的宿主（以及先建 DC 后注册类的顺序）在这里
     * 拿到同一个结论，见 lowmem.h。 */
    swinx_wasmCheckLowMemoryFree();
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    memset(m_keyboardState, 0, sizeof(m_keyboardState));
    m_hQueueEvt = CreateEventA(nullptr, FALSE /*auto reset*/, FALSE, nullptr);
#ifdef SWINX_WASM_CANVAS2D
    /* 告诉 JS 上屏链路：canvas2d 后端不用帧缓冲承载像素，脏矩形由 C++ 侧直接
     * drawImage 合成到显示画布（见 presentPending）。soui.host.js 见到这个标记就
     * 只做耗时统计、不再读 HEAPU8 搬像素。cairo 后端走下面的 'rgba' 零拷贝路径。
     * 用 run_script 而不是 val::module_property：后者返回右值，无法赋值。 */
    emscripten_run_script("Module.fbFormat = 'canvas';");
#else
    /* cairo 后端：presentPending 在把 cairo ARGB32 脏区拷贝进帧缓冲时已经用
     * SwinxBgraToRgba 就地换成 RGBA（SIMD 版移植自 soui-android-lib），所以帧
     * 缓冲里就是浏览器需要的 RGBA。这里把 fbFormat 设成 'rgba'，让 soui-host.js
     * 走零拷贝快路径（presentDirtyRect 直接 putImageData），不再在 JS 里每帧逐像素
     * 换序（原 fallback 路径 ~3.7ms/帧）。颜色正确、且是正式可用的优化路径。
     * 用 run_script 而不是 val::module_property：后者返回右值，无法赋值。 */
    emscripten_run_script("Module.fbFormat = 'rgba';");
#endif
    SLOG_FMTI("SConnection created for thread %d", m_tid);
}

SConnection::~SConnection()
{
    std::unique_lock<CountMutex> lock(m_mutex);
    CloseHandle(m_hQueueEvt);
    delete m_clipboard;
    for (auto msg : m_msgStack)
        delete msg;
    for (auto msg : m_msgQueue)
        delete msg;
    for (auto task : m_lstCallbackTask)
        task->Release();
    if (m_msgPeek && m_bMsgNeedFree)
        delete m_msgPeek;
    DeleteDC(m_deskDC);
    DeleteObject(m_deskBmp);
    SLOG_FMTI("SConnection destroyed");
}

void SConnection::BeforeProcMsg(HWND, UINT, WPARAM, LPARAM)
{
    ++m_msgRecursion;
}

void SConnection::AfterProcMsg(HWND, UINT, WPARAM, LPARAM, LRESULT)
{
    // 只在最外层那次派发结束后上屏：一次 WM_PAINT 内部还有 SendMessage
    // （WM_ERASEBKGND / WM_NCPAINT 等）递归回到这里，逐层上屏就等于没合并。
    if (m_msgRecursion > 0)
        --m_msgRecursion;
    if (m_msgRecursion == 0)
        flushPendingCommits();
}

SHORT SConnection::GetKeyState(int vk)
{
    if (vk >= 0 && vk < 256)
    {
        SHORT state = 0;
        if (m_keyboardState[vk] & 0x80)
            state |= (SHORT)0x8000;
        if (m_keyboardState[vk] & 0x01)
            state |= 0x0001;
        return state;
    }
    return 0;
}

void SConnection::SetKeyDown(UINT vk, bool down)
{
    if (vk >= 256)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    if (down)
        m_keyboardState[vk] |= 0x80;
    else
        m_keyboardState[vk] &= (BYTE)~0x80;
}

// flip the toggle (low) bit of a Lock key; the 0x80 transition bit is owned by
// SetKeyDown, so the two bits never fight over the same byte
void SConnection::ToggleKeyState(UINT vk)
{
    if (vk >= 256)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    m_keyboardState[vk] ^= 0x01;
}

void SConnection::SetCursorPos(int x, int y)
{
    // bare metal: the input driver is the single writer; 32-bit aligned
    // stores are atomic on ARM, no lock taken here
    m_curX = x;
    m_curY = y;
}

BOOL SConnection::GetKeyboardState(PBYTE lpKeyState)
{
    if (!lpKeyState)
        return FALSE;
    memset(lpKeyState, 0, 256);
    for (int i = 0; i < 256; ++i)
        lpKeyState[i] = m_keyboardState[i];
    return TRUE;
}

SHORT SConnection::GetAsyncKeyState(int vk)
{
    return GetKeyState(vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT uMapType) const
{
    switch (uMapType)
    {
    case MAPVK_VK_TO_VSC:
    case MAPVK_VK_TO_VSC_EX:
        return uCode;
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        return uCode;
    case MAPVK_VK_TO_CHAR:
        if (uCode >= 'A' && uCode <= 'Z')
            return uCode;
        if (uCode >= '0' && uCode <= '9')
            return uCode;
        return 0;
    default:
        return 0;
    }
}

UINT SConnection::GetDoubleClickTime() const
{
    return 400;
}

LONG SConnection::GetMsgTime() const
{
    return (LONG)m_tsLastMsg;
}

DWORD SConnection::GetMsgPos() const
{
    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    return MAKELONG(pt.x, pt.y);
}

DWORD SConnection::GetQueueStatus(UINT flags)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    if (flags == QS_ALLINPUT)
        return MAKELONG((DWORD)m_msgQueue.size(), 0);
    for (auto msg : m_msgQueue)
    {
        if ((flags & QS_PAINT) && msg->message == WM_PAINT)
            return MAKELONG(0, msg->message);
        if ((flags & QS_TIMER) && msg->message == WM_TIMER)
            return MAKELONG(0, msg->message);
        if ((flags & QS_KEY) && (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP))
            return MAKELONG(0, msg->message);
        if ((flags & QS_MOUSEMOVE) && msg->message == WM_MOUSEMOVE)
            return MAKELONG(0, msg->message);
        if ((flags & QS_MOUSEBUTTON) && msg->message >= WM_LBUTTONDOWN && msg->message <= WM_XBUTTONDBLCLK)
            return MAKELONG(0, msg->message);
        if (flags & QS_ALLPOSTMESSAGE)
            return MAKELONG(0, msg->message);
    }
    return 0;
}

// fire due timers into the message queue (tick based, connection-internal)
void SConnection::updateMsgQueue(DWORD dwTimeout)
{
    (void)dwTimeout;
    if (m_bBlockTimer)
        return;
    uint64_t now = GetTickCount();

    std::unique_lock<CountMutex> lock(m_mutex);
    bool bPosted = false;
    for (auto &ti : m_lstTimer)
    {
        if (now - (uint64_t)ti.fireRemain >= ti.elapse || (uint64_t)ti.fireRemain > now)
        {
            ti.fireRemain = (UINT)now;
            Msg *msg = new Msg;
            msg->hwnd = ti.hWnd;
            msg->message = WM_TIMER;
            msg->wParam = (WPARAM)ti.id;
            msg->lParam = (LPARAM)ti.proc;
            msg->time = (DWORD)now;
            bPosted = true;
            m_msgQueue.push_back(msg);
        }
    }
    if (bPosted)
        SetEvent(m_hQueueEvt);
}

bool SConnection::waitMsg(UINT timeOut)
{
    updateMsgQueue(0);   // fire due timers before computing the wait bound

    // wait bound: user timeout, or until the next timer is due
    uint64_t waitMs = timeOut;
    {
        std::unique_lock<CountMutex> lock(m_mutex);
        if (!m_msgQueue.empty())
            return true;
    }
    if (!m_bBlockTimer && !m_lstTimer.empty())
    {
        uint64_t now = GetTickCount();
        for (auto &ti : m_lstTimer)
        {
            uint64_t due = (uint64_t)ti.fireRemain + ti.elapse;
            uint64_t delta = due > now ? due - now : 0;
            if (delta < waitMs)
                waitMs = delta;
        }
    }
    if (waitMs > 0x7fffffff)
        waitMs = 0x7fffffff;

    uint64_t start = GetTickCount();
    for (;;)
    {
        DWORD wait = WaitForSingleObject(m_hQueueEvt, (DWORD)waitMs);
        // timers are driven inside waitMsg: a due timer enqueues WM_TIMER
        // and sets the queue event, waking this very loop
        updateMsgQueue(0);
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return true;
        }
        // spurious wake or timeout: recompute the remainder
        uint64_t elapsed = GetTickCount() - start;
        if (elapsed >= (uint64_t)timeOut)
            return false;
        waitMs = (uint64_t)timeOut - elapsed;
    }
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL fWaitAll, DWORD dwWaitMask)
{
    // wasm port: poll objects then the queue; the objects are Win32-compat
    // events/mutexes/semaphores from src/platform/posix, safe to poll
    // non-blocking.  Blocking on them inside a browser main loop would stall
    // the render tick, so this stays a poll loop.
    uint64_t start = GetTickCount();
    for (;;)
    {
        // 0. 每次让出前先补上屏：这个轮询循环可能长时间不返回消息，pending 的
        //    改动不能等到下一次派发才见画面。
        flushPendingCommits();
        // 1. any object ready?
        for (int i = 0; i < nCount; i++)
        {
            DWORD wait = WaitForSingleObject(handles[i], 0);
            if (wait == WAIT_OBJECT_0)
            {
                if (fWaitAll)
                    continue;
                return WAIT_OBJECT_0 + i;
            }
            else if (wait == WAIT_ABANDONED)
            {
                if (fWaitAll)
                    continue;
                return WAIT_ABANDONED + i;
            }
        }
        if (fWaitAll && nCount > 0)
        {
            bool bAll = true;
            for (int i = 0; i < nCount; i++)
            {
                if (WaitForSingleObject(handles[i], 0) != WAIT_OBJECT_0)
                {
                    bAll = false;
                    break;
                }
            }
            if (bAll)
                return WAIT_OBJECT_0;
        }
        // 2. any message?
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return (int)(WAIT_OBJECT_0 + nCount);
        }
        // 3. timeout?
        if (timeout != INFINITE)
        {
            uint64_t now = GetTickCount();
            if (now - start >= (uint64_t)timeout)
                return WAIT_TIMEOUT;
        }
        updateMsgQueue(1);
        sched_yield();
    }
    // not reached
    (void)dwWaitMask;
}

// Win32 的 TranslateMessage 只给"这个键确实代表一个字符"的按键补 WM_CHAR：
// 方向键、F1..F24、修饰键、Insert/Delete 这些都不会产生字符。浏览器里没有
// 键盘布局可查，所以这张表就是 Win32 在 US 布局上的那一套；表里没有的 vk
// 一律不产生字符，需要布局才能确定的键（死键、IME 组合输入）也走同一条
// "不猜"的路——那属于组合输入桥，不在这里补一个看似正确的猜测。
// 返回 0 表示不产生字符（按键永远不会产生 NUL，所以 0 可以当哨兵）。
static WORD vkToChar(UINT vk, bool bShift, bool bCapsLock)
{
    switch (vk)
    {
    case VK_BACK:   return 0x08;
    case VK_TAB:    return 0x09;
    case VK_RETURN: return 0x0D;
    case VK_ESCAPE: return 0x1B;
    case VK_SPACE:  return ' ';
    case VK_MULTIPLY: return '*';
    case VK_ADD:    return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL: return '.';
    case VK_DIVIDE: return '/';
    default: break;
    }

    // winuser.h 的注释：VK_A..VK_Z 与 ASCII 'A'..'Z' 同值（该头没有定义这两个宏）
    if (vk >= 'A' && vk <= 'Z')
        return (WORD)((bShift != bCapsLock) ? vk : vk - ('A' - 'a'));

    // VK_0..VK_9 同理与 ASCII 数字同值；Shift 给 US 布局的上档字符
    if (vk >= '0' && vk <= '9')
    {
        static const char s_shiftDigits[] = ")!@#$%^&*(";
        return (WORD)(bShift ? s_shiftDigits[vk - '0'] : vk);
    }

    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (WORD)('0' + (vk - VK_NUMPAD0));

    // US 布局的成对符号键：{ vk, 未 Shift, Shift }
    struct SymPair { UINT vk; char plain; char shifted; };
    static const SymPair s_pairs[] = {
        { VK_OEM_1,     ';', ':'  },
        { VK_OEM_PLUS,  '=', '+'  },
        { VK_OEM_COMMA, ',', '<'  },
        { VK_OEM_MINUS, '-', '_'  },
        { VK_OEM_PERIOD,'.', '>'  },
        { VK_OEM_2,     '/', '?'  },
        { VK_OEM_3,     '`', '~'  },
        { VK_OEM_4,     '[', '{'  },
        { VK_OEM_5,     '\\', '|' },
        { VK_OEM_6,     ']', '}'  },
        { VK_OEM_7,     '\'', '"' },
    };
    for (unsigned i = 0; i < sizeof(s_pairs) / sizeof(s_pairs[0]); ++i)
    {
        if (vk == s_pairs[i].vk)
            return (WORD)(bShift ? s_pairs[i].shifted : s_pairs[i].plain);
    }
    return 0;
}

BOOL SConnection::TranslateMessage(const MSG *pMsg)
{
    if (pMsg && (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN))
    {
        bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool bCapsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool bAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        WORD ch = vkToChar((UINT)pMsg->wParam, bShift, bCapsLock);
        if (ch)
        {
            if (bCtrl && !bAlt)
            {
                // Ctrl+字母/数字/符号组合在 Win32 下被 TranslateMessage 翻译成
                // "控制字符"（vk & 0x1F，落在 0x00~0x1F 区间），而不是字面字符。
                // richedit41 的 OnTxChar 只对 >= 0x20 的可打印字符做插入
                // （edit.cpp 的 `vkey >= ' '` 分支），控制字符被忽略。这样
                // Ctrl+C/V/X/A 等快捷键既能触发 richedit 的复制/粘贴/剪切/全选，
                // 又不会把 c/v 当普通字符插进文档（本端口此前直接生成字面 'c'/'v'，
                // 于是 Ctrl+C 选中的字被 c 替换、Ctrl+V 末尾多出一个 v）。
                // Ctrl+TAB / Ctrl+ENTER 等本身 < 0x20 的键不属于"可打印组合"，
                // vkToChar 对它们返回的就是原值，这里不改动，行为不变。
                if (ch >= ' ')
                    ch = (WORD)((UINT)pMsg->wParam & 0x1F);
            }
            // Alt 按下时 Win32 发的是 WM_SYSCHAR，不是 WM_CHAR
            UINT msgChar = bAlt ? WM_SYSCHAR : WM_CHAR;
            postMsg(pMsg->hwnd, msgChar, ch, pMsg->lParam);
            return TRUE;
        }
    }
    return FALSE;
}

static bool isSameTimerMsg(const Msg *msg, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    return msg && msg->message == WM_TIMER && msg->hwnd == hWnd && msg->wParam == (WPARAM)id && msg->lParam == (LPARAM)proc;
}

static bool hasQueuedTimerMsg(const std::list<Msg *> &queue, HWND hWnd, UINT_PTR id, TIMERPROC proc)
{
    for (auto msg : queue)
    {
        if (isSameTimerMsg(msg, hWnd, id, proc))
            return true;
    }
    return false;
}

static bool hasQueuedPaintMsg(const std::list<Msg *> &queue, HWND hWnd)
{
    for (auto msg : queue)
    {
        if (msg && msg->message == WM_PAINT && msg->hwnd == hWnd)
            return true;
    }
    return false;
}

BOOL SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
    // 先补上屏：派发之外的绘制（回调任务、UpdateWindow 式直接 GetDC/ReleaseDC）
    // 也要在应用准备阻塞或返回"没有消息"之前到达画面。
    flushPendingCommits();
    // Win32 generates WM_TIMER for expired timers from inside PeekMessage too,
    // so a PeekMessage-only pump (the simulator) gets timer ticks.  Called
    // before taking m_mutex: updateMsgQueue() locks it as well.
    updateMsgQueue(0);

    std::unique_lock<CountMutex> lock(m_mutex);
    // Win32 semantics: (0,0) means "no filter"
    bool bNoFilter = (wMsgFilterMin == 0 && wMsgFilterMax == 0);
    for (auto it = m_msgQueue.begin(); it != m_msgQueue.end(); ++it)
    {
        Msg *msg = *it;
        if (hWnd && hWnd != msg->hwnd && msg->hwnd != NULL)
            continue;
        UINT id = msg->message;
        if (!bNoFilter)
        {
            if (wMsgFilterMin <= wMsgFilterMax && (id < wMsgFilterMin || id > wMsgFilterMax))
                continue;
            if (wMsgFilterMin > wMsgFilterMax && (id > wMsgFilterMax && id < wMsgFilterMin))
                continue;   // wrapped range (Win32): only messages outside (max, min)
        }
        if (msg->message == WM_TIMER && msg->lParam)
        {
            // 带 TIMERPROC 的 SetTimer：Win32 由 PeekMessage 内部直接回调，不把
            // WM_TIMER 交给 DispatchMessage。这类消息的 hwnd 恒为 NULL（SOUI 的
            // STimerGenerator::SetTimer 就是 ::SetTimer(NULL, 0, elapse, _TimerProc)），
            // 而 DispatchMessage 见 pMsg->hwnd==NULL 直接返回 FALSE —— 队列抽干、
            // 计数照涨，回调却一次都不跑，脚本层的 ITimer（方块重力/消行倒计时）
            // 于是静默失效。语义与 src/platform/linux/SConnection.cpp 的同名分支一致。
            HWND hTimer = msg->hwnd;
            WPARAM wpTimer = msg->wParam;
            DWORD tTimer = msg->time;
            TIMERPROC proc = (TIMERPROC)msg->lParam;
            m_msgQueue.erase(it);
            delete msg;
            // 放锁再回调：回调里会 SetTimer/KillTimer/postMsg（同一把 m_mutex），
            // 还可能 SendMessage 等别的线程回包，持锁调用等于自己堵住那条回包路径。
            lock.unlock();
            proc(hTimer, WM_TIMER, wpTimer, tTimer);
            lock.lock();
            return peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
        }
        memcpy((void *)pMsg, (MSG *)msg, sizeof(MSG));
        if (wRemoveMsg & PM_REMOVE)
        {
            m_msgQueue.erase(it);
            delete msg;
        }
        return TRUE;
    }
    return FALSE;
}

BOOL SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    // GetMessage 语义上就是 PM_REMOVE 的 PeekMessage，委托给它：过滤规则和
    // WM_TIMER(TIMERPROC) 的回调只写一处（linux 后端正是这个形状）。在这里再扫一遍
    // 队列会让两条入口分叉，带回调的计时器就会从其中一条里静默消失。
    // wasm 不能阻塞浏览器主线程，所以队列为空时直接返回 FALSE，不进 waitMsg。
    return peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE);
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp)
{
    Msg *msg = new Msg;
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    msg->pt.x = 0;
    msg->pt.y = 0;
    postMsg(msg);
}

void SConnection::postMsg(Msg *pMsg)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    if (pMsg->message == WM_PAINT && hasQueuedPaintMsg(m_msgQueue, pMsg->hwnd))
    {
        delete pMsg;
        return;
    }
    if (pMsg->message == WM_MOUSEMOVE)
    {
        for (auto it = m_msgQueue.begin(); it != m_msgQueue.end();)
        {
            Msg *queued = *it;
            if (queued && queued->message == WM_MOUSEMOVE && queued->hwnd == pMsg->hwnd)
            {
                it = m_msgQueue.erase(it);
                delete queued;
            }
            else
            {
                ++it;
            }
        }
    }
    if (pMsg->message == WM_TIMER && hasQueuedTimerMsg(m_msgQueue, pMsg->hwnd, pMsg->wParam, (TIMERPROC)pMsg->lParam))
    {
        delete pMsg;
        return;
    }
    m_msgQueue.push_back(pMsg);
    SetEvent(m_hQueueEvt);
}

void SConnection::postMsg2(BOOL, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply)
{
    Msg *msg = new Msg(reply);
    msg->hwnd = hWnd;
    msg->message = message;
    msg->wParam = wp;
    msg->lParam = lp;
    msg->time = GetTickCount();
    msg->pt.x = 0;
    msg->pt.y = 0;
    postMsg(msg);
}

void SConnection::postCallbackTask(CbTask *pTask)
{
    if (!pTask)
        return;
    std::unique_lock<CountMutex> lock(m_mutex);
    pTask->AddRef();
    m_lstCallbackTask.push_back(pTask);
}

UINT_PTR SConnection::SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    // reuse: replace same (hWnd,id,proc)
    for (auto &ti : m_lstTimer)
    {
        if (ti.hWnd == hWnd && ti.id == id && ti.proc == proc)
        {
            ti.elapse = uElapse;
            ti.fireRemain = (UINT)GetTickCount();
            return id;
        }
    }
    if (id == 0)
        id = m_nextTimerId++;
    TimerInfo ti;
    ti.hWnd = hWnd;
    ti.id = id;
    ti.elapse = uElapse;
    ti.fireRemain = (UINT)GetTickCount();
    ti.proc = proc;
    m_lstTimer.push_back(ti);
    return id;
}

BOOL SConnection::KillTimer(HWND hWnd, UINT_PTR id)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); ++it)
    {
        if (it->hWnd == hWnd && it->id == id)
        {
            m_lstTimer.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

void SConnection::KillWindowTimer(HWND hWnd)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end();)
    {
        if (it->hWnd == hWnd)
            it = m_lstTimer.erase(it);
        else
            ++it;
    }
}

HDC SConnection::GetDC()
{
    return m_deskDC;
}

BOOL SConnection::ReleaseDC(HDC)
{
    return TRUE;
}

HWND SConnection::SetCapture(HWND hCapture)
{
    HWND old = m_hWndCapture;
    m_hWndCapture = hCapture;
    return old;
}

BOOL SConnection::ReleaseCapture()
{
    m_hWndCapture = 0;
    return TRUE;
}

HWND SConnection::GetCapture() const
{
    return m_hWndCapture;
}

HCURSOR SConnection::SetCursor(HWND hWnd, HCURSOR cursor)
{
    HCURSOR old = 0;
    auto it = m_wndCursor.find(hWnd);
    if (it != m_wndCursor.end())
        old = it->second;
    m_wndCursor[hWnd] = cursor;
    return old;
}

HCURSOR SConnection::GetCursor()
{
    auto it = m_wndCursor.find(m_hFocus);
    return it == m_wndCursor.end() ? 0 : it->second;
}

BOOL SConnection::DestroyCursor(HCURSOR)
{
    return TRUE;
}

void SConnection::SetTimerBlock(bool bBlock)
{
    std::unique_lock<CountMutex> lock(m_mutex);
    m_bBlockTimer = bBlock;
}

HWND SConnection::GetActiveWnd() const
{
    return m_hActive;
}

BOOL SConnection::SetActiveWindow(HWND hWnd)
{
    HWND old = m_hActive;
    m_hActive = hWnd;
    return old != 0;
}

HWND SConnection::WindowFromPoint(POINT, HWND hWnd) const
{
    // single-window traversal depth: wasm windows are virtual objects
    // owned by _Window; hit-test refinement lands with the renderer (TODO).
    return hWnd;
}

BOOL SConnection::IsWindow(HWND hWnd) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    return wndObj ? !wndObj->bDestroyed : FALSE;
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const
{
    // virtual windows: _Window::rc is updated by the core WM_MOVE handler
    // (src/wnd.cpp CallWindowProcPriv); the platform only has to note that the
    // screen layout changed so the next flush recomposes every shell window.
    (void)hWnd;
    (void)x;
    (void)y;
    m_bScreenDirty = true;
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const
{
    (void)hWnd;
    (void)cx;
    (void)cy;
    m_bScreenDirty = true;
}

BOOL SConnection::MoveWindow(HWND, int, int, int, int) const
{
    return TRUE;
}

BOOL SConnection::GetCursorPos(LPPOINT ppt) const
{
    if (ppt)
    {
        ppt->x = m_curX;
        ppt->y = m_curY;
    }
    return TRUE;
}

int SConnection::GetDpi(BOOL) const
{
    /* wasm 没有 per-monitor DPI：档位来自页面启动时 SwinxSetCanvasScale(浏览器
     * devicePixelRatio 归一化到 SOUI 内置档位，见 soui-wasm main()）。按 96 基准
     * 报告，SDpiHandler::ScaleHost 经 GetWindowScale(hWnd)=dpi*100/96 正好还原
     * 档位（1.5 倍屏 → 144dpi → 150%）—— 与桌面 SDpiHandler 链路共用同一入口。 */
    return (int)(96.0 * SwinxGetCanvasScale() + 0.5);
}

HWND SConnection::GetForegroundWindow()
{
    return m_hForeground;
}

BOOL SConnection::SetForegroundWindow(HWND hWnd)
{
    m_hForeground = hWnd;
    return TRUE;
}

BOOL SConnection::BringWindowToTop(HWND hWnd)
{
    (void)hWnd;
    return TRUE;
}

BOOL SConnection::SetWindowOpacity(HWND, BYTE)
{
    return TRUE;
}

BOOL SConnection::SetWindowRgn(HWND, HRGN)
{
    return TRUE;
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl)
{
    HKL old = m_hkl;
    m_hkl = hKl;
    return old;
}

HKL SConnection::GetKeyboardLayout(DWORD)
{
    return m_hkl;
}

UINT SConnection::GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    if (lpList && nBuff > 0)
        lpList[0] = m_hkl;
    return 1;
}

HBITMAP SConnection::GetDesktopBitmap()
{
    return m_deskBmp;
}

HWND SConnection::GetFocus() const
{
    return m_hFocus;
}

BOOL SConnection::SetFocus(HWND hWnd)
{
    HWND old = m_hFocus;
    m_hFocus = hWnd;
    return old != 0;
}

BOOL SConnection::IsDropTarget(HWND)
{
    return FALSE;
}

BOOL SConnection::FlashWindowEx(PFLASHWINFO info)
{
    (void)info;
    return TRUE;
}

int SConnection::OnGetClassName(HWND, LPSTR lpClassName, int nMaxCount)
{
    if (!lpClassName || nMaxCount <= 0)
        return 0;
    strncpy(lpClassName, "SWINX", nMaxCount - 1);
    lpClassName[nMaxCount - 1] = 0;
    return (int)strlen(lpClassName);
}

BOOL SConnection::OnSetWindowText(HWND, _Window *, LPCSTR lpszString)
{
    (void)lpszString;
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND)
{
    return 0;
}

int SConnection::OnGetWindowTextLengthW(HWND)
{
    return 0;
}

int SConnection::OnGetWindowTextA(HWND, char *buf, int bufLen)
{
    if (buf && bufLen > 0)
        buf[0] = 0;
    return 0;
}

int SConnection::OnGetWindowTextW(HWND, wchar_t *buf, int bufLen)
{
    if (buf && bufLen > 0)
        buf[0] = 0;
    return 0;
}

HWND SConnection::OnFindWindowEx(HWND, HWND, LPCSTR, LPCSTR)
{
    return NULL;
}

BOOL SConnection::OnEnumWindows(HWND, HWND, WNDENUMPROC lpEnumFunc, LPARAM lParam)
{
    (void)lpEnumFunc;
    (void)lParam;
    return TRUE;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags)
{
    if (gaFlags == GA_ROOT || gaFlags == GA_ROOTOWNER)
        return hwnd;
    return NULL;
}

HMONITOR SConnection::MonitorFromWindow(HWND, DWORD)
{
    return (HMONITOR)1;
}

HMONITOR SConnection::MonitorFromPoint(POINT, DWORD)
{
    return (HMONITOR)1;
}

HMONITOR SConnection::MonitorFromRect(LPCRECT, DWORD)
{
    return (HMONITOR)1;
}

int SConnection::GetMonitorCount() const
{
    return 1;
}

HMONITOR SConnection::GetMonitor(int index) const
{
    return index == 0 ? (HMONITOR)1 : NULL;
}

HMONITOR SConnection::GetPrimaryMonitor() const
{
    return (HMONITOR)1;
}

bool SConnection::GetMonitorRect(HMONITOR, RECT *prc) const
{
    if (prc)
    {
        prc->left = 0;
        prc->top = 0;
        prc->right = m_screenW;
        prc->bottom = m_screenH;
    }
    return true;
}

bool SConnection::GetMonitorWorkRect(HMONITOR hMonitor, RECT *prc) const
{
    return GetMonitorRect(hMonitor, prc);
}

bool SConnection::IsPrimaryMonitor(HMONITOR hMonitor) const
{
    return hMonitor == (HMONITOR)1;
}

int SConnection::GetScreenWidth(HMONITOR) const
{
    return m_screenW;
}

int SConnection::GetScreenHeight(HMONITOR) const
{
    return m_screenH;
}

HWND SConnection::GetScreenWindow() const
{
    return NULL;
}

void SConnection::UpdateWindowIcon(HWND, _Window *)
{
}

uint32_t SConnection::GetVisualID(BOOL) const
{
    return 0;
}

uint32_t SConnection::GetCmap() const
{
    return 0;
}

void SConnection::SetZOrder(HWND, _Window *, HWND)
{
}

void SConnection::OnStyleChanged(HWND, _Window *, DWORD, DWORD)
{
}

void SConnection::OnExStyleChanged(HWND, _Window *, DWORD, DWORD)
{
}

void SConnection::SendClientMessage(HWND, uint32_t, uint32_t *, int)
{
}

uint32_t SConnection::GetIpcAtom() const
{
    return 0;
}

#ifdef SWINX_WASM_CANVAS2D
cairo_surface_t *SConnection::CreateWindowSurface(HWND, uint32_t, int cx, int cy)
{
    // route C (canvas2d): 每个窗口绘制进自己的离屏 HTMLCanvasElement 后备画布；
    // commitCanvas() 把脏矩形读回 framebuffer，由既有 JS 上屏链路复制到可见 canvas。
    // 返回裸 Canvas2DSurface*（经 InitGdiObj(OBJ_BITMAP,...) 包成 HBITMAP，见 wnd.cpp
    // InitWndDC），与 cairo 后端「返回裸 surface 指针」语义一致。cairo_surface_t 仅作
    // 不透明返回类型前向声明。
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    return (cairo_surface_t *)SwinxCanvasCreateSurfaceRaw(cx, cy, 0);
}

cairo_surface_t *SConnection::ResizeSurface(cairo_surface_t *surface, HWND, uint32_t, int cx, int cy)
{
    // canvas 改变尺寸会清空内容，与 cairo surface resize 语义一致；同一指针原地缩放，
    // HBITMAP 包裹不变（wnd.cpp 的 linux/wasm 分支随后 SetGdiObjPtr + 重建 DC 重绑）。
    Canvas2DSurface *surf = (Canvas2DSurface *)surface;
    if (!surf)
        return CreateWindowSurface(NULL, 0, cx, cy);
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    SwinxCanvasResize(surf->canvas, cx, cy);
    surf->width = cx;
    surf->height = cy;
    /* 改尺寸会把 ctx 的状态连 JS save/restore 栈一起清空（和 ctx.reset() 同一套
     * 语义），栈底那枚未裁剪的锚点已经不存在了 —— 不作废的话，下一次绑定会
     * restore 回一个根本没有的快照，裁剪就再也撤不开（见 clipClearCanvas）。 */
    surf->clipAnchor = FALSE;
    return surface;
}
#else
cairo_surface_t *SConnection::CreateWindowSurface(HWND, uint32_t, int cx, int cy)
{
    // software renderer: every window paints into its own ARGB32 image
    // surface; commitCanvas() blits dirty regions into the framebuffer.
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cx, cy);
}

cairo_surface_t *SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, uint32_t visualId, int cx, int cy)
{
    if (surface)
        cairo_surface_destroy(surface);
    return CreateWindowSurface(hWnd, visualId, cx, cy);
}
#endif

DWORD SConnection::GetWndProcessId(HWND)
{
    return 1;
}

HWND SConnection::WindowFromPoint(POINT pt)
{
    // 顶层窗口命中测试，z 序从顶到底取第一个命中的可见窗口。弹出窗
    // （SDropDownWnd，WS_POPUP，parent=宿主窗）比主窗后建、在 m_zTop 末尾，
    // 所以盖住主窗的部分先命中弹出窗——Win32 同名 API 的语义。
    for (int i = (int)m_zTop.size() - 1; i >= 0; --i)
    {
        WndObj wndObj = WndMgr::fromHwnd(m_zTop[i]);
        if (!wndObj || wndObj->bDestroyed)
            continue;
        if ((wndObj->dwStyle & WS_VISIBLE) == 0)
            continue;
        const RECT &rc = wndObj->rc;
        if (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom)
            return m_zTop[i];
    }
    return NULL;
}

BOOL SConnection::GetClientRect(HWND, RECT *pRc)
{
    if (pRc)
    {
        pRc->left = 0;
        pRc->top = 0;
        pRc->right = m_screenW;
        pRc->bottom = m_screenH;
    }
    return TRUE;
}

void SConnection::SendSysCommand(HWND, int)
{
}

BOOL SConnection::IsWindowVisible(HWND hWnd)
{
    // Win32 semantics: WS_VISIBLE on the window AND on every ancestor.
    // (This port used to answer TRUE unconditionally, which silently disabled
    // WM_SHOWWINDOW transitions and every "is it worth painting" check.)
    HWND h = hWnd;
    while (h)
    {
        WndObj wndObj = WndMgr::fromHwnd(h);
        if (!wndObj || (wndObj->dwStyle & WS_VISIBLE) == 0)
            return FALSE;
        HWND hParent = wndObj->parent;
        if (hParent == h)
            break;
        h = hParent;
    }
    return hWnd != NULL;
}

/* Window navigation.
 *
 * This port has no OS window manager: an HWND is a swinx _Window tracked in
 * WndMgr, so parent/owner relations only exist in _Window.  The query used to
 * return hWnd unconditionally, which is not "no relative window" -- the shared
 * WM_DESTROY path in wnd.cpp walks the child list with
 *
 *     hChild = GetWindow(hWnd, GW_CHILDLAST);
 *     while (hChild) { ... DestroyWindow(hChild); hChild = GetWindow(...); }
 *
 * so it kept getting the window being destroyed and re-entered DestroyWindow()
 * from inside that window's own WM_DESTROY, recursing until the window locks
 * dead-locked -- the app froze on close.  GW_HWNDNEXT has the same contract
 * (GetDlgItem(), the WM_PRINT children walk): it must return 0 at the end of
 * the list instead of repeating the last window forever.
 *
 * Sibling order is by HWND value.  There is no z-order on this port (windows
 * are virtual), and the callers that matter only need "iterate all of them
 * exactly once, then stop".
 */
HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd)
{
    if (!wndObj)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (uCmd == GW_OWNER)
        return wndObj->owner;
    if (uCmd == GW_PARENT)
        return wndObj->parent;

    const HWND hParent = wndObj->parent;
    std::vector<WndBasicInfo> all;
    WndMgr::enumerate([&](HWND hwnd, _Window *wnd) {
        all.push_back({hwnd, wnd->parent, wnd->owner, wnd->bDestroyed});
    });

    // windows sharing hWnd's parent (hWnd itself included, at its sorted place)
    std::vector<HWND> sibs;
    std::vector<HWND> children;
    sibs.reserve(all.size());
    for (size_t i = 0; i < all.size(); i++)
    {
        const WndBasicInfo &info = all[i];
        // a window already marked destroyed is on its way out of the registry:
        // skipping it here is what makes the destroy loops terminate
        if (info.hwnd != hWnd && info.bDestroyed)
            continue;
        if (info.parent == hParent)
            sibs.push_back(info.hwnd);
        if (info.parent == hWnd)
            children.push_back(info.hwnd);
    }
    std::sort(sibs.begin(), sibs.end());
    std::sort(children.begin(), children.end());

    switch (uCmd)
    {
    case GW_CHILD: // == GW_CHILDFIRST
        return children.empty() ? 0 : children.front();
    case GW_CHILDLAST:
        return children.empty() ? 0 : children.back();
    case GW_HWNDFIRST:
        return sibs.empty() ? 0 : sibs.front();
    case GW_HWNDLAST:
        return sibs.empty() ? 0 : sibs.back();
    case GW_HWNDNEXT:
    {
        std::vector<HWND>::const_iterator it = std::find(sibs.begin(), sibs.end(), hWnd);
        if (it == sibs.end())
            return 0;
        ++it;
        return it == sibs.end() ? 0 : *it;
    }
    case GW_HWNDPREV:
    {
        std::vector<HWND>::const_iterator it = std::find(sibs.begin(), sibs.end(), hWnd);
        if (it == sibs.end() || it == sibs.begin())
            return 0;
        --it;
        return *it;
    }
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
}

UINT SConnection::RegisterMessage(LPCSTR lpString)
{
    if (!lpString || !*lpString)
        return 0;
    std::lock_guard<CountMutex> lock(m_mutex);
    std::string name(lpString);
    for (auto &pair : m_regMessages)
    {
        if (pair.first == name)
            return pair.second;
    }
    UINT id = m_nextRegMsg++;
    m_regMessages[name] = id;
    return id;
}

UINT SConnection::RegisterClipboardFormatA(LPCSTR pszName)
{
    return SClipboard::RegisterClipboardFormatA(pszName);
}

BOOL SConnection::NotifyIcon(DWORD, PNOTIFYICONDATAA)
{
    return FALSE;
}

HMONITOR SConnection::GetScreen(DWORD) const
{
    return (HMONITOR)1;
}

void SConnection::updateWindow(HWND hWnd, const RECT &)
{
    // synchronous paint of the current invalid region (same as the X11 port)
    SendMessageA(hWnd, WM_PAINT, 0, 0);
}

static int RectArea(const RECT *rc)
{
    return (rc->right - rc->left) * (rc->bottom - rc->top);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc)
{
    // Resolve, right now, the part of the work that is time-sensitive: which
    // window canvas we are talking about and which region of it is worth
    // showing.  The blit + JS callback are deferred to flushPendingCommits(),
    // so one WM_PAINT that ends with three ReleaseDC() commits one rect.
    //
    // Why "right now": the update region is cleared at the end of the WM_PAINT
    // dispatch (src/wnd.cpp:1401, SetRectRgn after WM_NCPAINT), which is before
    // the flush point -- asking later would see an empty region and fall back to
    // the whole clip box, so every commit would dirty the full canvas.
    SwinxFbCountRequest();
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    // 登记画过至少一帧的窗口：整屏合成时跳过没画过的（画布全 0，弹窗首帧前
    // 不能整幅 blit 出一块黑）。首帧同时置 screenDirty：新顶层窗的出生帧走
    // 底→顶整幅重组，杜绝局部上屏路径在 z 序上的任何缝隙。
    if (m_committed.insert(hWnd).second)
        m_bScreenDirty = true;
#ifdef SWINX_WASM_CANVAS2D
    Canvas2DSurface *surface = (Canvas2DSurface *)GetGdiObjPtr(wndObj->bmp);
    if (!surface)
        return;

    int sw = surface->width;
    int sh = surface->height;
#else
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return;
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return;

    int sw = cairo_image_surface_get_width(surface);
    int sh = cairo_image_surface_get_height(surface);
#endif

    // clip rc to the window canvas (window-local coordinates)
    RECT rcSrc = rc;
    if (rcSrc.left < 0)
        rcSrc.left = 0;
    if (rcSrc.top < 0)
        rcSrc.top = 0;
    if (rcSrc.right > sw)
        rcSrc.right = sw;
    if (rcSrc.bottom > sh)
        rcSrc.bottom = sh;
    if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
        return;

    // Restrict the commit to the window's update region.  ReleaseDC() hands us
    // GetClipBox() *after* RestoreDC(-1), which drops both the clip the framework
    // selects before WM_ERASEBKGND (src/wnd.cpp) and anything BeginPaint could add,
    // so the DC clip alone is always the whole canvas.  With no update region
    // pending (a plain GetDC + draw, e.g. UpdateWindow-style painting) keep the
    // clip box, so nothing that reached the screen before stops reaching it now.
    RECT rcUpd = {};
    if (GetUpdateRect(hWnd, &rcUpd, FALSE) && !IsRectEmpty(&rcUpd))
    {
        if (rcSrc.left < rcUpd.left)
            rcSrc.left = rcUpd.left;
        if (rcSrc.top < rcUpd.top)
            rcSrc.top = rcUpd.top;
        if (rcSrc.right > rcUpd.right)
            rcSrc.right = rcUpd.right;
        if (rcSrc.bottom > rcUpd.bottom)
            rcSrc.bottom = rcUpd.bottom;
        if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
            return;
    }

    // queue (window-local): merge into whatever this window already owes the
    // screen.  Keeping only local rects is what makes the merge safe — a
    // screen-space pair would lose the src/fb size correspondence as soon as
    // two commits get clipped against the framebuffer edge differently.
    std::map<HWND, PendingCommit>::iterator it = m_pendingCommits.find(hWnd);
    if (it == m_pendingCommits.end())
    {
        PendingCommit pc;
        pc.rcSrc = rcSrc;
        m_pendingCommits.insert(std::make_pair(hWnd, pc));
        return;
    }
    RECT rcMerge;
    UnionRect(&rcMerge, &it->second.rcSrc, &rcSrc);
    // 合并只对真正重叠的矩形划算：两块互不相交的区域并起来是个更大的包围盒，
    // 拷贝面积反而上升（阶段 0 裸窗口演示实测 +54%），而宿主收到的上屏次数不变。
    // 不相交时先把欠的那块上屏，再重新记账，面积与修改前一致、次数只可能更少。
    if (RectArea(&rcMerge) <= RectArea(&it->second.rcSrc) + RectArea(&rcSrc))
    {
        it->second.rcSrc = rcMerge;
        return;
    }
    RECT rcOld = it->second.rcSrc;
    m_pendingCommits.erase(it);
    presentPending(hWnd, rcOld);
    PendingCommit pcNew;
    pcNew.rcSrc = rcSrc;
    m_pendingCommits.insert(std::make_pair(hWnd, pcNew));
}

#ifndef SWINX_WASM_CANVAS2D
// BGRA→RGBA：cairo CAIRO_FORMAT_ARGB32 在小端内存里布局为 BGRA，浏览器 ImageData /
// Canvas2D 需要 RGBA。把 soui-android-lib/SouiSurfaceProxy.cpp 的 SSSE3
// _mm_shuffle_epi8 通道交换原样移植到 wasm：wasm_v8x16_swizzle 就是 pshufb 的等效
// 指令，控制向量 [2,1,0,3,...] 与 Android 端完全一致。逐行把 cairo 脏区拷贝进帧
// 缓冲并就地换序，于是 JS 侧 Module.fbFormat='rgba' 零拷贝快路径上传的就是正确的
// RGBA，不再需要在 JS 里每帧逐像素换序（旧兜底路径 ~3.7ms/帧）。未开 -msimd128
// 时退回逐字节标量交换（逻辑一致、仅慢一点）。
static inline void SwinxBgraToRgba(uint8_t *dst, const uint8_t *src, int pixels)
{
    int x = 0;
#if defined(__wasm_simd128__)
    alignas(16) static const uint8_t kBGRA2RGBA[16] = {
        2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15
    };
    const v128_t vPerm = wasm_v128_load(kBGRA2RGBA);
    const int wSimd = pixels & ~3;
    for (; x < wSimd; x += 4)
    {
        v128_t v = wasm_v128_load(src + (size_t)x * 4);
        v128_t o = wasm_v8x16_swizzle(v, vPerm);
        wasm_v128_store(dst + (size_t)x * 4, o);
    }
#endif
    for (; x < pixels; ++x)
    {
        const uint8_t *q = src + (size_t)x * 4;
        uint8_t *p = dst + (size_t)x * 4;
        uint8_t t = q[0]; p[0] = q[2]; p[2] = t;   // 交换 B / R
        p[1] = q[1]; p[3] = q[3];                  // G、A 原样保留
    }
}
#endif

void SConnection::presentPending(HWND hWnd, const RECT &rcPending)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;  // 窗口已经销毁，画布不存在，这块改动自然也不用上屏
#ifdef SWINX_WASM_CANVAS2D
    Canvas2DSurface *surface = (Canvas2DSurface *)GetGdiObjPtr(wndObj->bmp);
    if (!surface)
        return;

    int sw = surface->width;
    int sh = surface->height;

    RECT rcSrc = rcPending;
    if (rcSrc.left < 0)
        rcSrc.left = 0;
    if (rcSrc.top < 0)
        rcSrc.top = 0;
    if (rcSrc.right > sw)
        rcSrc.right = sw;
    if (rcSrc.bottom > sh)
        rcSrc.bottom = sh;
    if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
        return;

    // screen-space dirty rect, clipped to the framebuffer
    // （坐标 1:1：SOUI 已按 DPI 档位在布局层缩放，应用坐标 == 物理像素）
    int winBaseX = wndObj->rc.left;
    int winBaseY = wndObj->rc.top;
    int sx = rcSrc.left;
    int sy = rcSrc.top;
    int ex = rcSrc.right;
    int ey = rcSrc.bottom;
    /* clamp 到 canvas */
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (ex > sw) ex = sw;
    if (ey > sh) ey = sh;
    if (sx >= ex || sy >= ey)
        return;

    int fx = winBaseX + sx;
    int fy = winBaseY + sy;
    int w = ex - sx;
    int h = ey - sy;
    if (fx < 0)
    {
        sx -= fx;
        w += fx;
        fx = 0;
    }
    if (fy < 0)
    {
        sy -= fy;
        h += fy;
        fy = 0;
    }
    if (fx + w > SwinxFbWidth())
        w = SwinxFbWidth() - fx;
    if (fy + h > SwinxFbHeight())
        h = SwinxFbHeight() - fy;
    if (w <= 0 || h <= 0)
        return;

    // 上屏：把这块离屏画布的脏矩形**直接 drawImage 合成到显示画布**。
    // 合成在这里与替换等价：窗口后备画布按 alpha:false 建（CreateWindowSurface →
    // SwinxCanvasCreateSurfaceRaw(..., 0)），源永不透明；可能含透明的 memRT→窗口画布
    // 那一跳才需要替换语义，见 canvas2d/gdi.cpp 的 blitRop。
    // fb（SwinxFbBits）在 canvas2d 下只作为"有新帧"的信令载体，不再承载像素；
    // JS 侧按 Module.fbFormat==='canvas' 跳过像素搬运（见 soui.html 的 presentDirtyRect）。
    SwxPresentSurface(surface->ctxId, sx, sy, w, h, fx, fy);
    RECT dirty = { fx, fy, fx + w, fy + h };
#else
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return;
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return;
    // the canvas may have been drawn into again after the request was queued
    cairo_surface_flush(surface);
    uint32_t *srcBase = (uint32_t *)cairo_image_surface_get_data(surface);
    if (!srcBase)
        return;
    int stride = cairo_image_surface_get_stride(surface) / 4;

    RECT rcSrc = rcPending;
    if (rcSrc.right > cairo_image_surface_get_width(surface))
        rcSrc.right = cairo_image_surface_get_width(surface);
    if (rcSrc.bottom > cairo_image_surface_get_height(surface))
        rcSrc.bottom = cairo_image_surface_get_height(surface);
    if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom)
        return;

    // screen-space dirty rect, clipped to the framebuffer
    int ox = wndObj->rc.left, oy = wndObj->rc.top;
    RECT rcFb = { ox + rcSrc.left, oy + rcSrc.top, ox + rcSrc.right, oy + rcSrc.bottom };
    if (rcFb.left < 0)
    {
        rcSrc.left -= rcFb.left;
        rcFb.left = 0;
    }
    if (rcFb.top < 0)
    {
        rcSrc.top -= rcFb.top;
        rcFb.top = 0;
    }
    if (rcFb.right > SwinxFbWidth())
    {
        rcSrc.right -= (rcFb.right - SwinxFbWidth());
        rcFb.right = SwinxFbWidth();
    }
    if (rcFb.bottom > SwinxFbHeight())
    {
        rcSrc.bottom -= (rcFb.bottom - SwinxFbHeight());
        rcFb.bottom = SwinxFbHeight();
    }
    if (rcFb.left >= rcFb.right || rcFb.top >= rcFb.bottom)
        return;

    int w = rcFb.right - rcFb.left;
    uint8_t *fbBits = (uint8_t *)SwinxFbBits();
    const uint8_t *srcBits = (const uint8_t *)srcBase;
    for (int y = 0; y < rcFb.bottom - rcFb.top; ++y)
    {
        // cairo ARGB32 在小端内存里是 BGRA；拷贝进帧缓冲的同时换成 RGBA，
        // 这样 JS 零拷贝快路径上传的就是浏览器需要的 RGBA（见 SwinxBgraToRgba）。
        uint8_t *dst = fbBits + ((size_t)(rcFb.top + y) * SwinxFbStride() + rcFb.left) * 4;
        const uint8_t *src = srcBits + ((size_t)(rcSrc.top + y) * stride + rcSrc.left) * 4;
        SwinxBgraToRgba(dst, src, w);
    }
    RECT dirty = rcFb;
#endif
    swinxFbPresent(&dirty);
}

void SConnection::flushPendingCommits()
{
    if (m_pendingCommits.empty() && !m_bScreenDirty)
        return;
    // swap first: the present callback runs JS, and a window being destroyed
    // during that callback could queue a new commit; iterating the live map
    // would then either re-enter or invalidate the iterator.
    std::map<HWND, PendingCommit> pending;
    pending.swap(m_pendingCommits);
    bool screenDirty = m_bScreenDirty;
    m_bScreenDirty = false;

    // 多顶层窗口合成：按 z 序（创建序，底→顶）逐窗上屏。普通 flush 只上各窗
    // 自己的脏区；screenDirty（弹出窗显隐/移动/销毁）时每个可见窗口整幅重画
    // ——每窗的画布保留着最近一帧，底→顶重新 blit 就同时恢复了 z 序遮挡与
    // 弹出窗关闭后的曝露区，不需要为底层窗口补 WM_PAINT。
    std::vector<HWND> z;
    if (screenDirty)
        z = m_zTop;
    for (size_t i = 0; i < z.size(); ++i)
    {
        WndObj wndObj = WndMgr::fromHwnd(z[i]);
        if (!wndObj || wndObj->bDestroyed || (wndObj->dwStyle & WS_VISIBLE) == 0)
            continue;
        // 没画过的窗口（弹出窗首帧之前）画布是全 0，跳过，下一帧它的
        // WM_PAINT 提交后自然出现
        if (m_committed.find(z[i]) == m_committed.end())
            continue;
        RECT rcFull = {0, 0, 0x7FFFFFFF, 0x7FFFFFFF};
        presentPending(z[i], rcFull);
        pending.erase(z[i]);
    }
    if (screenDirty)
        pending.clear();
    // 非 screenDirty：只上各窗自己的脏区。但底层窗口的脏矩形可能与上层已上屏
    // 窗口（弹出窗）重叠——直接 memcpy 会把上层像素擦掉（浏览器里 rAF 交错的
    // 泵时序下必然发生：弹窗打开后主窗任何一次重绘都会烧穿弹层）。按 z 序
    // 底→顶处理 pending，每上屏一块矩形就把位于其上、已画过、可见的窗口按
    // 交集重新 blit，维持 z 序遮挡。
    for (size_t i = 0; i < m_zTop.size(); ++i)
    {
        std::map<HWND, PendingCommit>::iterator it = pending.find(m_zTop[i]);
        if (it == pending.end())
            continue;
        presentPending(it->first, it->second.rcSrc);
        WndObj wndSelf = WndMgr::fromHwnd(m_zTop[i]);
        if (wndSelf)
        {
            RECT rcSelf = it->second.rcSrc;
            RECT rcScreen = { wndSelf->rc.left + rcSelf.left, wndSelf->rc.top + rcSelf.top,
                              wndSelf->rc.left + rcSelf.right, wndSelf->rc.top + rcSelf.bottom };
            if (rcScreen.left < 0)
                rcScreen.left = 0;
            if (rcScreen.top < 0)
                rcScreen.top = 0;
            if (rcScreen.right > SwinxFbWidth())
                rcScreen.right = SwinxFbWidth();
            if (rcScreen.bottom > SwinxFbHeight())
                rcScreen.bottom = SwinxFbHeight();
            if (rcScreen.left < rcScreen.right && rcScreen.top < rcScreen.bottom)
            {
                for (size_t j = i + 1; j < m_zTop.size(); ++j)
                {
                    if (m_committed.find(m_zTop[j]) == m_committed.end())
                        continue;
                    WndObj wndAbove = WndMgr::fromHwnd(m_zTop[j]);
                    if (!wndAbove || wndAbove->bDestroyed || (wndAbove->dwStyle & WS_VISIBLE) == 0)
                        continue;
                    RECT rcInter = { std::max(rcScreen.left, wndAbove->rc.left),
                                     std::max(rcScreen.top, wndAbove->rc.top),
                                     std::min(rcScreen.right, wndAbove->rc.right),
                                     std::min(rcScreen.bottom, wndAbove->rc.bottom) };
                    if (rcInter.left >= rcInter.right || rcInter.top >= rcInter.bottom)
                        continue;
                    RECT rcLocal = { rcInter.left - wndAbove->rc.left, rcInter.top - wndAbove->rc.top,
                                     rcInter.right - wndAbove->rc.left, rcInter.bottom - wndAbove->rc.top };
                    presentPending(m_zTop[j], rcLocal);
                }
            }
        }
        pending.erase(it);
    }
    for (std::map<HWND, PendingCommit>::iterator it = pending.begin(); it != pending.end(); ++it)
        presentPending(it->first, it->second.rcSrc);
}

void SConnection::EnableWindow(HWND, BOOL)
{
}

BOOL SConnection::IsIconic(HWND) const
{
    return FALSE;
}

BOOL SConnection::IsZoomed(HWND) const
{
    return FALSE;
}

int SConnection::ShowCursor(BOOL bShow)
{
    if (bShow)
        m_cursorCount++;
    else
        m_cursorCount--;
    return m_cursorCount;
}

UINT SConnection::GetRawInputDeviceList(PRAWINPUTDEVICELIST pRawInputDeviceList, PUINT puiNumDevices, UINT cbSize)
{
    if (!puiNumDevices)
        return (UINT)-1;
    if (pRawInputDeviceList && *puiNumDevices < 1)
        return (UINT)-1;
    *puiNumDevices = 0;
    (void)pRawInputDeviceList;
    (void)cbSize;
    return 0;
}

UINT SConnection::GetRawInputDeviceInfoA(HRAWINPUT, UINT, LPVOID, PUINT)
{
    return (UINT)-1;
}

UINT SConnection::GetRawInputDeviceInfoW(HRAWINPUT, UINT, LPVOID, PUINT)
{
    return (UINT)-1;
}

BOOL SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    m_caretInfo.hOwner = hWnd;
    m_caretInfo.hBmp = hBitmap;
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    return TRUE;
}

BOOL SConnection::DestroyCaret()
{
    std::lock_guard<CountMutex> lock(m_mutex);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    return TRUE;
}

BOOL SConnection::ShowCaret(HWND hWnd)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (m_caretInfo.hOwner != hWnd)
        return FALSE;
    m_caretInfo.nVisible++;
    return TRUE;
}

BOOL SConnection::HideCaret(HWND hWnd)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (m_caretInfo.hOwner != hWnd)
        return FALSE;
    if (m_caretInfo.nVisible > 0)
        m_caretInfo.nVisible--;
    return TRUE;
}

BOOL SConnection::SetCaretPos(int X, int Y)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (!m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    return TRUE;
}

BOOL SConnection::GetCaretPos(LPPOINT lpPoint)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    if (lpPoint)
    {
        lpPoint->x = m_caretInfo.x;
        lpPoint->y = m_caretInfo.y;
    }
    return m_caretInfo.hOwner != 0;
}

const SConnection::CaretInfo *SConnection::GetCaretInfo() const
{
    return &m_caretInfo;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime)
{
    std::lock_guard<CountMutex> lock(m_mutex);
    m_caretBlinkTime = blinkTime;
}

UINT SConnection::GetCaretBlinkTime() const
{
    return m_caretBlinkTime;
}

void SConnection::GetWorkArea(HMONITOR hMonitor, RECT *prc) const
{
    GetMonitorWorkRect(hMonitor, prc);
}

SClipboard *SConnection::getClipboard()
{
    return m_clipboard;
}

BOOL SConnection::EmptyClipboard()
{
    return m_clipboard->emptyClipboard();
}

BOOL SConnection::IsClipboardFormatAvailable(UINT format)
{
    return m_clipboard->hasFormat(format);
}

BOOL SConnection::OpenClipboard(HWND hWndNewOwner)
{
    return m_clipboard->openClipboard(hWndNewOwner);
}

BOOL SConnection::CloseClipboard()
{
    return m_clipboard->closeClipboard();
}

HWND SConnection::GetClipboardOwner()
{
    return m_clipboard->getClipboardOwner();
}

HANDLE SConnection::GetClipboardData(UINT uFormat)
{
    return m_clipboard->getClipboardData(uFormat);
}

HANDLE SConnection::SetClipboardData(UINT uFormat, HANDLE hMem)
{
    return m_clipboard->setClipboardData(uFormat, hMem);
}

void SConnection::EnableDragDrop(HWND, BOOL)
{
}

HRESULT SConnection::DoDragDrop(IDataObject *, IDropSource *, DWORD dwOKEffect, DWORD *pdwEffect)
{
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    (void)dwOKEffect;
    return DRAGDROP_S_CANCEL;
}

HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth)
{
    // virtual windows: swinx creates the _Window object itself; the
    // connection only tracks it.  See mobile's ENABLE_VIRTUAL_HWND path.
    (void)cs;
    (void)depth;
    // z 序 = 创建序：主窗先建在底，弹出窗（SDropDownWnd 等）后建在顶。
    // 同一线程的连接只会有一个 UI 线程在跑，这里不需要额外加锁。
    if (wnd)
        m_zTop.push_back((HWND)wnd);
    return (HWND)wnd;
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *)
{
    // Win32 语义：销毁 capture 窗口时自动释放 capture（弹出下拉窗销毁后
    // 不留悬空 capture，否则鼠标路由永远指向已死的窗口）。capture 是
    // SConnection 的状态，清理归平台层，核心 DestroyWindow 不掺和。
    if (m_hWndCapture == hWnd)
        ReleaseCapture();
    std::vector<HWND>::iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it != m_zTop.end())
        m_zTop.erase(it);
    m_committed.erase(hWnd);
    m_pendingCommits.erase(hWnd);
    // 弹出窗销毁会曝露主窗上被盖住的区域：要求整屏重新合成
    m_bScreenDirty = true;
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, BOOL bVisible, int)
{
    if (!wndObj)
    {
        WndObj tmp = WndMgr::fromHwnd(hWnd);
        wndObj = tmp.data();
    }
    if (!wndObj)
        return;

    if (bVisible)
    {
        if (wndObj->dwStyle & WS_VISIBLE)
            return; // already visible
        wndObj->dwStyle |= WS_VISIBLE;
        // A window that just became visible needs one full repaint: it was
        // skipped by SendExposeEvent() while hidden.
        InvalidateRect(hWnd, NULL, TRUE);
        // 弹出窗显示：显示前的整屏合成发生在它首帧之前会闪黑块，置脏后
        // 由 flushPendingCommits() 在它自己的首帧 blit 时一起重排
        m_bScreenDirty = true;
    }
    else
    {
        if ((wndObj->dwStyle & WS_VISIBLE) == 0)
            return; // already hidden
        wndObj->dwStyle &= ~WS_VISIBLE;
        // 隐藏（弹出窗收起）：被盖住的主窗区域没有收到 WM_PAINT 的机会，
        // 置脏让下一次 flush 按各窗画布整幅重排
        m_bScreenDirty = true;
    }
}

void SConnection::SetParent(HWND, _Window *, HWND)
{
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc, BOOL bForce)
{
    // Windows semantics: InvalidateRect() only *marks* the region and asks for
    // a later WM_PAINT; nothing paints synchronously here.
    //
    // Without this, SOUI's whole Invalidate()/ModifyState() path is a no-op on
    // this port: the first frame appears via the explicit UpdateWindow() at
    // startup and every later change (button hover/push state, animation,
    // caret, resize) never reaches the framebuffer.
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed)
        return;
    if (!IsWindowVisible(hWnd))
        return;

    // The invalid region is already accumulated in wndObj->invalid by
    // InvalidateRect(); rc is only a hint, so it is not needed to paint.
    if (bForce)
    { // synchronous paint, same path as UpdateWindow()
        if (wndObj->nPainting)
            return; // never re-enter the painter
        RECT rcBox;
        GetRgnBox(wndObj->invalid.hRgn, &rcBox);
        if (IsRectEmpty(&rcBox))
            return;
        updateWindow(hWnd, rcBox);
        return;
    }

    // Asynchronous: one pending WM_PAINT per window (postMsg coalesces), so a
    // burst of invalidations during one frame costs exactly one repaint.
    postMsg(hWnd, WM_PAINT, 0, 0);
}

void SConnection::SetWindowMsgTransparent(HWND, _Window *, BOOL)
{
}

void SConnection::AssociateHIMC(HWND, _Window *, HIMC)
{
}

void SConnection::flush()
{
}

void SConnection::sync()
{
}

BOOL SConnection::IsScreenComposited() const
{
    return TRUE;
}

_Window *SConnection::CreateVirtualWindowObject()
{
    // TODO: virtual _Window factory lands with the window glue
    return NULL;
}

//=============================================================================
// SConnMgr
//=============================================================================

SConnMgr::SConnMgr()
    : m_hHeap(NULL)
{
}

SConnMgr::~SConnMgr()
{
    for (auto &pair : m_conns)
        delete pair.second;
}

SConnMgr *SConnMgr::instance()
{
    static SConnMgr *s_mgr = new SConnMgr(); // leak-on-purpose, never destructed
    return s_mgr;
}

SConnection *SConnMgr::getConnection(tid_t tid, int screenNum)
{
    if (tid == 0)
        tid = GetCurrentThreadId();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_conns.find(tid);
    if (it != m_conns.end())
        return it->second;
    SConnection *pConn = new SConnection(screenNum);
    m_conns[tid] = pConn;
    return pConn;
}

HANDLE SConnMgr::getProcessHeap()
{
    /* 这里不能调全局 ::GetProcessHeap()：wasm 编译的是 src/platform/posix/memory.cpp，
     * 它把 GetProcessHeap 实现成 SConnMgr::instance()->getProcessHeap()，两边互调就是
     * 无限递归。freertos 那份照抄没问题，因为它的 sysbridge.cpp 自己实现了一个静态
     * 进程堆。wasm 走 posix 的 HeapCreate/HeapAlloc，所以进程堆由这里持有。 */
    return m_hHeap ? m_hHeap : (m_hHeap = HeapCreate(0, 0, 0));
}
