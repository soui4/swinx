/*
 * SConnection.cpp -- Wayland 平台连接实现。
 *
 * 模型对齐 GTK 3.24.48 的 Wayland 后端：
 *   - 窗口是客户端侧对象，HWND 由 WndMgr 分配的虚拟句柄表示；
 *   - 每个窗口一块 cairo image surface 画布 + 一个独立的 wl_surface，主窗是
 *     xdg_toplevel，其余窗口（MessageBox / 下拉窗 / WS_CHILD）是父 surface 的
 *     wl_subsurface；commitCanvas() 只把脏区交给该窗口自己的 SHM buffer，
 *     重叠与 z 序交给合成器，不再做 CPU 软件合成；
 *   - wl_display 的事件由 peekMsg()/waitMsg() 里的非阻塞事件泵派发，
 *     configure/close 与 wl_seat 的指针/键盘事件经 SwinxWlEventThunk 回投。
 *
 * popup（tooltip / 有 owner 的对话框）走 xdg_popup + xdg_positioner；
 * 剪贴板/拖放走 wl_data_device；仍未接入：每显示器缩放。
 */
#include "SConnection.h"
#include "wndobj.h"
#include "framebuffer.h"
#include "wlframewnd.h"
#include "tostring.h"
#include "synhandle.h" // GetSynHandle(): 取 m_hQueueEvt 的 pipe fd
#include <oleidl.h>
#include <shlobj.h> // DROPFILES for CF_HDROP file drag
#include <gdi.h>
#include <cairo.h>
#include <log.h>
#include <algorithm>
#include <functional>
#include <poll.h>   // waitWakeup(): 同时等 wl_display 与消息队列
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define kLogTag "SConnection"

// 与 framebuffer.cpp 的 SWINX_WL_TRACE 同一个开关：这里负责报告"绘制有没有
// 走到平台层的提交点"，framebuffer 负责报告"提交有没有真的上屏"。两者结合
// 才能区分 SOUI 没画、脏区被裁掉、surface 被挡住三类问题。
static int wlDiag()
{
    static int on = -1;
    if (on < 0)
        on = getenv("SWINX_WL_TRACE") ? 1 : 0;
    return on;
}

/* timer 日志单独开关：SWINX_WL_TRACE=1 下每帧都有大量窗口 timer 的
 * set/fire，把真正关心的窗口/tooltip 日志淹没，且平台层日志缺少 SOUI 语义
 * （分不清哪个是 tooltip 的 TIMERID_DELAY）。默认关闭，需要专门排查 timer
 * 时再显式设 SWINX_WL_TRACE_TIMER=1。tooltip 全链路诊断请改用 SOUI 层的
 * SOUI_TIP_TRACE（见 SOUI/src/helper/SToolTip.cpp）。 */
static int wlTimerDiag()
{
    static int on = -1;
    if (on < 0)
        on = getenv("SWINX_WL_TRACE_TIMER") ? 1 : 0;
    return on;
}

/* 装饰窗矩形跟随内容窗：frame 是 (x, y-titleH, x+w, y+h)。x/y/w/h 一律是
 * 内容窗的**应用坐标**（还没被 normalizeWlToplevelOrigin() 归一的那份），
 * 因为 xdg_popup 的 positioner 要靠它算锚点（对话框就是用它居中的）。
 * frame 不存在时直接返回，不影响只走 toplevel 的窗口。
 *
 * 只在能一次拿到完整几何的地方用（MoveWindow）。SetWindowPos / SetWindowSize
 * 是成对回调、各带一半，必须走 wlframewnd 的 Move / Resize 单边入口。 */
static void wlPlaceFrame(HWND hContent, int x, int y, int cw, int ch)
{
    if (!hContent || cw < 1 || ch < 1)
        return;
    WndObj wndObj = WndMgr::fromHwnd(hContent);
    if (!wndObj || !wndObj->plat.frame)
        return;
    SwinxWlFrameSetRect(wndObj->plat.frame, x, y, cw, ch);
}

/* 判断一个窗口的 "位置" 在 Wayland 下是否由合成器独占：
 *   - 自己的 surface 是 xdg_toplevel → 是；
 *   - 被装饰窗包着、而装饰窗是 xdg_toplevel → 也是（内容只是 subsurface）；
 *   - 装饰窗是 xdg_popup 的对话框 → 不是，位置是 positioner 的锚点，必须保留。
 * 只有 positionless 的窗口才把 rc 原点归零：指针事件给的是 surface 局部坐标，
 * 两者口径必须一致，否则 SNcPainter::OnNcHitTest 会把 caption 判成边框。 */
static bool wlIsPositionless(HWND hWnd)
{
    SwinxWlSurface *wl = SwinxWlSurfaceGet(hWnd);
    if (!wl)
        return false;
    int role = SwinxWlSurfaceGetRole(wl);
    if (role == SWINX_WL_SURF_TOPLEVEL)
        return true;
    if (role == SWINX_WL_SURF_SUBSURFACE)
    {
        WndObj wndObj = WndMgr::fromHwnd(hWnd);
        if (wndObj && wndObj->plat.frame)
        {
            SwinxWlSurface *fw = SwinxWlSurfaceGet(wndObj->plat.frame);
            if (fw && SwinxWlSurfaceGetRole(fw) == SWINX_WL_SURF_TOPLEVEL)
                return true;
        }
    }
    return false;
}

/* xdg_toplevel 的原点由合成器独占：Wayland 协议不把窗口位置回告客户端
 * （GTK 3.24 的 gdkwindow-wayland.c:3676 也把每个 toplevel 当作独立根
 * 坐标系，window->x/y 恒为 0）。swinx 的 wndObj->rc 若保留 SetWindowPos
 * 写入的旧屏幕坐标，合成器完成 xdg_toplevel_move 后它就与真实位置脱节，
 * SNcPainter::OnNcHitTest 会把 caption 判成边框 → 光标变拉伸、拖动变缩放。
 * 把这类窗口的 rc 原点归一到 (0,0)，与 wl_pointer 事件的 surface 局部坐标
 * 保持同一口径；popup（含被 popup 装饰窗包着的对话框）的 rc 是 positioner
 * 的锚点，必须保留应用写的坐标，不能动。 */
static void normalizeWlToplevelOrigin(SConnection *conn, HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    if ((wndObj->dwStyle & WS_CHILD) || (wndObj->dwExStyle & WS_EX_TOOLWINDOW))
        return;
    /* 被装饰窗包着的窗口：它在 frame surface 里恒位于 (0, titleH)，位置完全由
     * 装饰窗承载。它自己的 rc 是**应用坐标**（对话框的 xdg_positioner 锚点就
     * 靠这份 rc 算出来），归零会让每个居中的对话框贴到父窗左上角。 */
    if (wndObj->plat.frame)
        return;
    /* 归零的唯一目的：让 _Window::rc 的原点与指针事件的坐标系同口径。
     * xdg_toplevel 的原点由合成器独占，应用写的 CenterWindow 坐标在 Wayland
     * 下没有任何效果；留着它只会让 WM_NCHITTEST 拿 **surface 局部坐标** 去比
     * **屏幕坐标**的 rc —— 表现为点标题栏变成拉窗口边框、标题栏上的按钮全部
     * 落进非客户区分支（点不动）。
     * 所以判据只能看 **surface 角色**（wlIsPositionless），不能看 parent/owner：
     * SHostWnd::Create() 会把 owner 记成调用方的活动窗口（demo 里就是
     * GetActiveWindow()，而那个句柄往往指向已经销毁的 MessageBox），一旦
     * "有 owner 就跳过"，应用主窗也会被跳过，整窗命中测试全部错位。 */
    if (!wlIsPositionless(hWnd))
        return; // 已经是 popup 角色：rc 是 xdg_positioner 的锚点，必须保留
    if (conn && conn->isPendingDialog(hWnd))
        return; // 待转 popup 的对话框：锚点稍后要用这份 rc 算，别归零
    if (wndObj->rc.left == 0 && wndObj->rc.top == 0)
        return;
    OffsetRect(&wndObj->rc, -wndObj->rc.left, -wndObj->rc.top);
}

// Plain-value snapshot of the window registry (kept from the freertos fork:
// to navigate parent / child / sibling without locking every other window).  See
// WndMgr::enumerate().
/* dragQuery 需按 HCURSOR 取内置光标像素；定义在文件后段，此处前置到全局作用域。 */
static bool wlCursorPixels(HCURSOR cursor, std::vector<uint32_t> &out,
                           int &w, int &h, int &hotX, int &hotY);
static void applyWlCursor(SConnection *conn, HWND hWnd);

namespace {
struct WndBasicInfo
{
    HWND hwnd;
    HWND parent;
    HWND owner;
    HWND frameContent; // 非 0 = 这是平台内部的装饰窗（CSD frame）
    BOOL bDestroyed;
};
} // namespace

// Wayland 的事件只在 UI 线程派发：framebuffer 的 configure/close 回调通过
// 这个连接把事件转成 Win32 消息。非 UI 线程创建的 SConnection 不接管显示
// （阶段 0 只支持单 UI 线程；多线程连接仍可收发消息，但不驱动窗口）。
static SConnection *s_pWlConn = NULL;

static void SwinxWlEventThunk(const SwinxWlEvent *ev);

static UINT s_nextRegisteredMessage = WM_USER + 100000;

//=============================================================================
// SClipboard（Win32 句柄语义 + wl_data_device 跨进程通道）
//=============================================================================

SClipboard::FmtTable &SClipboard::fmtTable()
{
    // leak-on-purpose: format names live for the process lifetime
    static FmtTable *s_table = new FmtTable();
    return *s_table;
}

std::vector<std::pair<std::string, UINT> > SClipboard::RegisteredFormats()
{
    std::vector<std::pair<std::string, UINT> > out;
    FmtTable &table = fmtTable();
    std::lock_guard<std::recursive_mutex> lock(table.mutex);
    for (std::map<std::string, UINT>::iterator it = table.names.begin();
         it != table.names.end(); ++it)
        out.push_back(std::make_pair(it->first, it->second));
    return out;
}

std::string SClipboard::FormatName(UINT fmt)
{
    FmtTable &table = fmtTable();
    std::lock_guard<std::recursive_mutex> lock(table.mutex);
    for (std::map<std::string, UINT>::iterator it = table.names.begin();
         it != table.names.end(); ++it)
        if (it->second == fmt)
            return it->first;
    return std::string();
}

namespace {

const char kMimeUtf8[] = "text/plain;charset=utf-8";
const char kMimeUtf8Alt[] = "UTF8_STRING";
const char kMimeText[] = "text/plain";
const char kMimeCustom[] = "application/x-swinx-format-";
// GTK/Nautilus 文件拖放用 text/uri-list（每行一个 file:// URI）。
const char kMimeUriList[] = "text/uri-list";

bool clipIsTextFormat(UINT fmt)
{
    return fmt == CF_TEXT || fmt == CF_UNICODETEXT;
}
bool clipIsFileFormat(UINT fmt)
{
    return fmt == CF_HDROP;
}

void clipTextMimes(std::vector<std::string> &out)
{
    if (std::find(out.begin(), out.end(), kMimeUtf8) == out.end())
        out.push_back(kMimeUtf8);
    out.push_back(kMimeUtf8Alt);   // GTK 优先请求这个
    out.push_back(kMimeText);
}

/* 一个 CF_* 对应哪些 mime：文本同时发布三个别名，兼容 GTK/Qt；注册格式用
 * "application/x-swinx-format-<注册名>" 与另一个 swinx 进程互通，未注册的
 * 数字格式用 id 兜底（同一进程内复制粘贴总能命中）。 */
std::string clipFormatMime(UINT fmt)
{
    if (clipIsTextFormat(fmt))
        return kMimeUtf8;
    if (clipIsFileFormat(fmt))
        return kMimeUriList;
    std::string name = SClipboard::FormatName(fmt);
    if (!name.empty())
        return std::string(kMimeCustom) + name;
    return std::string(kMimeCustom) + std::to_string(fmt);
}

void clipFormatMimes(UINT fmt, std::vector<std::string> &out)
{
    if (clipIsTextFormat(fmt))
    {
        clipTextMimes(out);
        return;
    }
    out.push_back(clipFormatMime(fmt));
}

UINT clipMimeFormat(const char *mime)
{
    if (!mime)
        return 0;
    if (strcmp(mime, kMimeUtf8) == 0 || strcmp(mime, kMimeUtf8Alt) == 0 ||
        strcmp(mime, kMimeText) == 0)
        return CF_UNICODETEXT;
    if (strcmp(mime, kMimeUriList) == 0)
        return CF_HDROP;
    size_t n = strlen(kMimeCustom);
    if (strncmp(mime, kMimeCustom, n) != 0)
        return 0;
    const char *name = mime + n;
    if (!*name)
        return 0;
    // RegisterClipboardFormatA 对任何名字都会分配 >=0xC000 的 id，不能用它的
    // 返回值判断"是否已注册"：否则数字 id 兜底永远走不到，还会污染格式表。
    UINT fmt = SClipboard::FormatIdA(name);
    if (fmt >= 0xC000)
        return fmt;
    char *end = NULL;
    unsigned long v = strtoul(name, &end, 10);
    if (end && !*end && v > 0 && v < 0xC000)
        return (UINT)v;
    return SClipboard::RegisterClipboardFormatA(name);
}

/* HGLOBAL -> UTF-8（文本）或原始字节（自定义格式）。 */
bool clipHandleBytes(HANDLE h, UINT fmt, std::vector<unsigned char> &out)
{
    if (!h)
        return false;
    SIZE_T cb = GlobalSize(h);
    void *p = GlobalLock(h);
    if (!p)
        return false;
    if (fmt == CF_UNICODETEXT)
    {
        std::string utf8;
        tostring((LPCWSTR)p, (int)(cb / sizeof(wchar_t)), utf8);
        out.assign(utf8.begin(), utf8.end());
    }
    else
    {
        const unsigned char *b = (const unsigned char *)p;
        out.assign(b, b + cb);
    }
    GlobalUnlock(h);
    return true;
}

/* 读到的字节 -> HGLOBAL。CF_UNICODETEXT 要把 UTF-8 转成本平台 wchar_t
 * （Linux 上 wchar_t 是 4 字节，与 X11 后端同一套 towstring 语义）。 */
HANDLE clipBytesHandle(UINT fmt, const void *p, size_t n)
{
    if (fmt == CF_UNICODETEXT)
    {
        std::wstring w;
        if (n)
            towstring((LPCSTR)p, (int)n, w);
        size_t cb = (w.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, cb);
        if (!h)
            return NULL;
        wchar_t *dst = (wchar_t *)GlobalLock(h);
        memcpy(dst, w.c_str(), w.size() * sizeof(wchar_t));
        dst[w.size()] = 0;
        GlobalUnlock(h);
        return h;
    }
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n + (fmt == CF_TEXT ? 1 : 0));
    if (!h)
        return NULL;
    void *dst = GlobalLock(h);
    if (n)
        memcpy(dst, p, n);
    if (fmt == CF_TEXT)
        ((char *)dst)[n] = 0;
    GlobalUnlock(h);
    return h;
}

// text/uri-list (UTF-8, CRLF/LF 分隔, 可选 # 注释行) -> CF_HDROP 的
// DROPFILES + 双 0 结尾宽字符文件名表。file:// 前缀去掉，%XX 解码，
// 与 X11 后端 readSelection(CF_TEXT) -> DROPFILES 语义对齐。
HGLOBAL clipUriListToHDrop(const char *p, size_t n)
{
    std::vector<std::wstring> files;
    std::string line;
    for (size_t i = 0; i <= n; ++i)
    {
        char c = (i < n) ? p[i] : '\n';
        if (c == '\r' || c == '\n')
        {
            if (!line.empty() && line[0] != '#')
            {
                std::string uri = line;
                if (uri.size() > 7 && uri.compare(0, 7, "file://") == 0)
                    uri = uri.substr(7);
                std::string path;
                for (size_t k = 0; k < uri.size(); ++k)
                {
                    if (uri[k] == '%' && k + 2 < uri.size())
                    {
                        char hi = uri[k+1], lo = uri[k+2];
                        int v = 0;
                        if (hi >= '0' && hi <= '9') v += (hi - '0') << 4;
                        else if (hi >= 'a' && hi <= 'f') v += (hi - 'a' + 10) << 4;
                        else if (hi >= 'A' && hi <= 'F') v += (hi - 'A' + 10) << 4;
                        if (lo >= '0' && lo <= '9') v += (lo - '0');
                        else if (lo >= 'a' && lo <= 'f') v += (lo - 'a' + 10);
                        else if (lo >= 'A' && lo <= 'F') v += (lo - 'A' + 10);
                        path.push_back((char)v);
                        k += 2;
                    }
                    else
                        path.push_back(uri[k]);
                }
                if (!path.empty())
                {
                    std::wstring w;
                    towstring(path.c_str(), (int)path.size(), w);
                    files.push_back(w);
                }
            }
            line.clear();
        }
        else
            line.push_back(c);
    }
    if (files.empty())
        return NULL;
    size_t cbFiles = 0;
    for (size_t i = 0; i < files.size(); ++i)
        cbFiles += (files[i].size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + cbFiles + sizeof(wchar_t));
    if (!h)
        return NULL;
    DROPFILES *df = (DROPFILES *)GlobalLock(h);
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = -1; df->pt.y = -1;
    df->fNC = FALSE;
    df->fWide = TRUE;
    wchar_t *dst = (wchar_t *)((char *)df + sizeof(DROPFILES));
    for (size_t i = 0; i < files.size(); ++i)
    {
        memcpy(dst, files[i].c_str(), files[i].size() * sizeof(wchar_t));
        dst += files[i].size();
        *dst++ = 0;
    }
    *dst = 0; // 双 0 结尾
    GlobalUnlock(h);
    return h;
}

/* X11 SDataObjectProxy::GetData 的兼容兜底：源只给文本而目标只问 CF_HDROP
 * （demo 的 CTestDropTarget1 就是这种目标）时，把文本当成一个 ANSI 文件名
 * 包进 DROPFILES。X11 下拖文本进这个文件目标能显示，靠的就是这一段。 */
HGLOBAL clipTextToHDrop(const void *p, size_t n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + n + 2);
    if (!h)
        return NULL;
    DROPFILES *df = (DROPFILES *)GlobalLock(h);
    if (!df)
    {
        GlobalFree(h);
        return NULL;
    }
    memset(df, 0, sizeof(DROPFILES));
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = -1;
    df->pt.y = -1;
    df->fNC = FALSE;
    df->fWide = FALSE;
    char *dst = (char *)df + sizeof(DROPFILES);
    if (n)
        memcpy(dst, p, n);
    dst[n] = 0;
    dst[n + 1] = 0;
    GlobalUnlock(h);
    return h;
}

int clipSourceRead(void *ctx, const char *mime, void *buf, int cap)
{
    SClipboard *clip = (SClipboard *)ctx;
    return clip ? clip->readForSelection(mime, buf, cap) : -1;
}

} // namespace

SClipboard::SClipboard()
    : m_hOwner(0)
    , m_bOpen(false)
    , m_selGeneration(SwinxWlSelectionGeneration())
    , m_bOwnsSelection(false)
    , m_publishPos(0)
    , m_doExClip(NULL)
    , m_doClip(NULL)
{
}

SClipboard::~SClipboard()
{
    if (m_bOwnsSelection)
        SwinxWlSelectionClear();
    if (m_doExClip)
        m_doExClip->Release();
    if (m_doClip)
        m_doClip->Release();
    for (std::map<UINT, HANDLE>::iterator it = m_data.begin(); it != m_data.end(); ++it)
        if (it->second)
            GlobalFree(it->second);
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

UINT SClipboard::FormatIdA(LPCSTR pszName)
{
    if (!pszName || !*pszName)
        return 0;
    FmtTable &table = fmtTable();
    std::lock_guard<std::recursive_mutex> lock(table.mutex);
    auto it = table.names.find(std::string(pszName));
    return it == table.names.end() ? 0 : it->second;
}

void SClipboard::syncSelection()
{
    unsigned int gen = SwinxWlSelectionGeneration();
    if (gen == m_selGeneration)
        return;
    m_selGeneration = gen;
    if (SwinxWlSelectionOwned())
        return;                    // 是我们自己刚发布的，本地副本仍有效
    if (m_bOwnsSelection)
    {
        // 别的进程抢走了 selection：本地句柄与外部 IDataObject 都失效
        for (std::map<UINT, HANDLE>::iterator it = m_data.begin(); it != m_data.end(); ++it)
            if (it->second)
                GlobalFree(it->second);
        m_data.clear();
        if (m_doExClip)
        {
            m_doExClip->Release();
            m_doExClip = NULL;
        }
        m_bOwnsSelection = false;
    }
}

void SClipboard::publishSelection()
{
    std::vector<std::string> mimes;
    for (std::map<UINT, HANDLE>::iterator it = m_data.begin(); it != m_data.end(); ++it)
    {
        if (!it->second)
            continue;
        clipFormatMimes(it->first, mimes);
    }
    if (mimes.empty())
    {
        if (m_bOwnsSelection)
            SwinxWlSelectionClear();
        m_bOwnsSelection = false;
        return;
    }
    std::vector<const char *> ptrs;
    for (size_t i = 0; i < mimes.size(); ++i)
        ptrs.push_back(mimes[i].c_str());
    if (SwinxWlSelectionSet(this, &ptrs[0], (int)ptrs.size(), clipSourceRead, 0))
        m_bOwnsSelection = true;
}

int SClipboard::readForSelection(const char *mime, void *buf, int cap)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // buf==NULL 是 framebuffer 的 data_source_send 在新 send 事务开始时
    // 发出的 reset 信号：把分块状态归零，让同一 mime 的再次请求从头重发。
    if (!buf || cap <= 0)
    {
        m_publishPos = 0;
        m_publishMime.clear();
        m_publishBytes.clear();
        return 0;
    }
    if (!mime)
        return -1;
    // 同一个 selection 会被请求多个 mime（GTK 先问 UTF8_STRING，再问
    // text/plain），wl_data_source.send 对每个 mime 各回调一次。分块缓冲
    // 必须按 mime 隔离，否则第二份数据会接着第一份的 offset 继续写。
    if (m_publishPos == 0 || m_publishMime != mime)
    {
        m_publishBytes.clear();
        m_publishPos = 0;
        m_publishMime = mime;
        UINT fmt = clipMimeFormat(mime);
        HANDLE h = getClipboardData(fmt);
        if (!h && fmt == CF_UNICODETEXT)
            h = getClipboardData(CF_TEXT);
        if (!h || !clipHandleBytes(h, fmt, m_publishBytes))
            return -1;
    }
    if (m_publishPos >= m_publishBytes.size())
        return 0;
    size_t n = std::min(m_publishBytes.size() - m_publishPos, (size_t)cap);
    memcpy(buf, &m_publishBytes[m_publishPos], n);
    m_publishPos += n;
    // 不要在这里清空状态：下次调用必须命中 pos>=size 返回 0 (EOF)，
    // 否则会被误判成新请求重新装载数据，data_source_send 永不结束。
    return (int)n;
}

BOOL SClipboard::emptyClipboard()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_bOwnsSelection)
    {
        SwinxWlSelectionClear();
        m_bOwnsSelection = false;
    }
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
    m_publishBytes.clear();
    m_publishMime.clear();
    m_publishPos = 0;
    return TRUE;
}

BOOL SClipboard::hasFormat(UINT format)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    syncSelection();
    std::map<UINT, HANDLE>::iterator it = m_data.find(format);
    if (it != m_data.end() && it->second)
        return TRUE;
    // 远端 selection：CF_UNICODETEXT 与 CF_TEXT 共享同一份 UTF-8 mime
    if (clipIsTextFormat(format))
        return SwinxWlSelectionHasMime(kMimeUtf8) ||
               SwinxWlSelectionHasMime(kMimeUtf8Alt);
    return SwinxWlSelectionHasMime(clipFormatMime(format).c_str());
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
    if (!m_bOwnsSelection && !m_data.empty())
        publishSelection();
    return TRUE;
}

HWND SClipboard::getClipboardOwner()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_hOwner;
}

HANDLE SClipboard::getClipboardData(UINT uFormat)
{
    // Win32 语义：返回的句柄归剪贴板所有，调用方不释放；句柄在数据被替换前有效。
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    syncSelection();
    std::map<UINT, HANDLE>::iterator it = m_data.find(uFormat);
    if (it != m_data.end())
        return it->second;

    // 远端 owner：按请求格式挑一个 mime，同步读一次并缓存。
    std::vector<std::string> mimes;
    clipFormatMimes(uFormat, mimes);
    for (size_t i = 0; i < mimes.size(); ++i)
    {
        void *buf = NULL;
        int cb = SwinxWlSelectionRead(mimes[i].c_str(), &buf);
        if (cb > 0 && buf)
        {
            HANDLE h = clipBytesHandle(uFormat, buf, (size_t)cb);
            SwinxWlDataFree(buf);
            if (h)
            {
                m_data[uFormat] = h;   // 远程缓存，syncSelection 会失效它
                return h;
            }
        }
        else if (buf)
        {
            SwinxWlDataFree(buf);
        }
    }
    return NULL;
}

HANDLE SClipboard::setClipboardData(UINT uFormat, HANDLE hMem)
{
    // Win32 语义：成功后句柄归剪贴板所有（NULL hMem = 延迟渲染，不支持）。
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!hMem)
        return NULL;
    std::map<UINT, HANDLE>::iterator it = m_data.find(uFormat);
    if (it != m_data.end() && it->second && it->second != hMem)
        GlobalFree(it->second);
    m_data[uFormat] = hMem;
    if (m_bOpen)
        publishSelection();
    return hMem;
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
    // 先按 Win32 语义清空当前剪贴板（顺带释放旧的外部对象）。
    // 不能调 emptyClipboard()：它会 set_selection(NULL)，与紧随其后的
    // publishSelection() 产生两个回告，乱序时 selectionAwaitOwn 被反转，
    // syncSelection() 会把刚发布的数据误判成被抢走而清空。
    if (m_doExClip) { m_doExClip->Release(); m_doExClip = NULL; }
    for (std::map<UINT, HANDLE>::iterator it = m_data.begin(); it != m_data.end(); ++it)
        if (it->second) GlobalFree(it->second);
    m_data.clear();
    m_publishBytes.clear(); m_publishMime.clear(); m_publishPos = 0;
    if (!pDo)
        return TRUE;

    m_doExClip = pDo;
    m_doExClip->AddRef();

    // 外部 IDataObject 不能跨进程直接暴露给 wl_data_source。X11 后端在
    // OleFlushClipboard 里延迟抽取；Wayland 没有延迟渲染，必须在
    // OleSetClipboard 返回前把每个 TYMED_HGLOBAL 格式抽成 HGLOBAL 副本，
    // 否则对端请求 send 时我们无字节可写（表现就是"复制没反应"）。
    IEnumFORMATETC *en = NULL;
    if (pDo->EnumFormatEtc(DATADIR_GET, &en) == S_OK && en)
    {
        FORMATETC f;
        ULONG got = 0;
        while (en->Next(1, &f, &got) == S_OK && got == 1)
        {
            if (f.tymed != TYMED_HGLOBAL || !f.cfFormat)
                continue;
            STGMEDIUM med;
            memset(&med, 0, sizeof(med));
            if (pDo->GetData(&f, &med) != S_OK || !med.hGlobal)
                continue;
            SIZE_T cb = GlobalSize(med.hGlobal);
            void *psrc = GlobalLock(med.hGlobal);
            HGLOBAL copy = cb ? GlobalAlloc(GMEM_MOVEABLE, cb) : NULL;
            if (psrc && copy)
            {
                void *pdst = GlobalLock(copy);
                memcpy(pdst, psrc, cb);
                GlobalUnlock(copy);
                if (m_data[f.cfFormat] && m_data[f.cfFormat] != copy)
                    GlobalFree(m_data[f.cfFormat]);
                m_data[f.cfFormat] = copy;
            }
            if (psrc)
                GlobalUnlock(med.hGlobal);
            ReleaseStgMedium(&med);
        }
        en->Release();
    }
    // EnumFormatEtc 不可用时退回文本兜底（少数数据对象只实现 QueryGetData）
    if (m_data.empty())
    {
        FORMATETC fmt;
        STGMEDIUM med;
        memset(&fmt, 0, sizeof(fmt));
        memset(&med, 0, sizeof(med));
        fmt.cfFormat = CF_UNICODETEXT;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        if (pDo->GetData(&fmt, &med) == S_OK && med.hGlobal)
        {
            SIZE_T cb = GlobalSize(med.hGlobal);
            void *psrc = GlobalLock(med.hGlobal);
            HGLOBAL copy = cb ? GlobalAlloc(GMEM_MOVEABLE, cb) : NULL;
            if (psrc && copy)
            {
                void *pdst = GlobalLock(copy);
                memcpy(pdst, psrc, cb);
                GlobalUnlock(copy);
                m_data[CF_UNICODETEXT] = copy;
            }
            if (psrc)
                GlobalUnlock(med.hGlobal);
            ReleaseStgMedium(&med);
        }
    }
    // 统一发布一次：逐格式调 setClipboardData 会发多次 set_selection，
    // 合成器回告顺序一乱就把刚发布的误判成"被别人抢走"。
    publishSelection();
    return TRUE;
}

IDataObject *SClipboard::getDataObject()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // X11 后端在这里查 selection owner；Wayland 没有全局 owner 快照，只能
    // 按代次同步。漏掉这一步时，别的进程抢走剪贴板后本进程仍会拿到旧的
    // m_doExClip，表现就是"粘贴没反应 / 粘出旧内容"。
    syncSelection();
    if (m_doExClip)
    {
        // 同实例复制粘贴：直接回登记的数据对象，RTF 等全部格式保真
        m_doExClip->AddRef();
        return m_doExClip;
    }
    // 远端 owner：先探一次文本，触发 offer.receive 并缓存到 m_data，
    // 否则内部适配器的 QueryGetData 会报"没有任何格式"而让 re41 直接放弃。
    if (m_data.empty())
        getClipboardData(CF_UNICODETEXT);
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
// 拖放（wl_data_device）
//=============================================================================

namespace {

class SWaylandFormatEnum : public SUnkImpl<IEnumFORMATETC>
{
  public:
    explicit SWaylandFormatEnum(const std::vector<UINT> &fmts)
        : m_fmts(fmts), m_pos(0)
    {
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) override
    {
        ULONG n = 0;
        while (n < celt && m_pos < m_fmts.size())
        {
            memset(&rgelt[n], 0, sizeof(FORMATETC));
            rgelt[n].cfFormat = (CLIPFORMAT)m_fmts[m_pos++];
            rgelt[n].dwAspect = DVASPECT_CONTENT;
            rgelt[n].lindex = -1;
            rgelt[n].tymed = TYMED_HGLOBAL;
            ++n;
        }
        if (pceltFetched) *pceltFetched = n;
        return n == celt ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override
    {
        m_pos = std::min(m_fmts.size(), m_pos + celt);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset(void) override { m_pos = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC **ppenum) override
    {
        if (!ppenum) return E_INVALIDARG;
        *ppenum = new SWaylandFormatEnum(m_fmts);
        return S_OK;
    }
    IUNKNOWN_BEGIN(IEnumFORMATETC)
    IUNKNOWN_END()
  private:
    std::vector<UINT> m_fmts;
    size_t m_pos;
};

/* 拖放目标的 IDataObject：mime 清单来自对方的 wl_data_offer，GetData 时才
 * 用 receive 拉字节（等价于 X11 的 SDataObjectProxy）。 */
class SWaylandDropDataObject : public SUnkImpl<IDataObject>
{
  public:
    explicit SWaylandDropDataObject(SwinxWlDataOffer *offer)
        : m_offer(offer)
    {
        static const UINT kText[] = { CF_UNICODETEXT, CF_TEXT };
        for (size_t i = 0; i < sizeof(kText) / sizeof(kText[0]); ++i)
        {
            std::vector<std::string> mimes;
            clipFormatMimes(kText[i], mimes);
            for (size_t j = 0; j < mimes.size(); ++j)
                if (SwinxWlDndOfferHasMime(offer, mimes[j].c_str()))
                {
                    m_formats.push_back(kText[i]);
                    break;
                }
        }
        // 文件拖放：Nautilus/GTK 提供 text/uri-list
        if (SwinxWlDndOfferHasMime(offer, kMimeUriList))
            m_formats.push_back(CF_HDROP);
        // 自定义格式：注册名 -> cfFormat（对端必须是同一份格式表）
        std::vector<std::pair<std::string, UINT> > regs = SClipboard::RegisteredFormats();
        for (size_t i = 0; i < regs.size(); ++i)
        {
            std::string mime = std::string(kMimeCustom) + regs[i].first;
            if (SwinxWlDndOfferHasMime(offer, mime.c_str()))
                m_formats.push_back(regs[i].second);
        }
    }

    HRESULT WINAPI GetData(FORMATETC *f, STGMEDIUM *med) override
    {
        if (!f || !med)
            return E_INVALIDARG;
        memset(med, 0, sizeof(STGMEDIUM));
        if (f->tymed != TYMED_HGLOBAL)
            return DV_E_TYMED;
        UINT fmt = f->cfFormat;
        std::vector<std::string> mimes;
        clipFormatMimes(fmt, mimes);
        // 与 X11 SDataObjectProxy::GetData 对齐：目标只问 CF_HDROP 而 offer 里
        // 只有文本 mime 时，降级成 CF_TEXT 读字节，再包装成 ANSI DROPFILES。
        bool textAsFiles = false;
        if (fmt == CF_HDROP)
        {
            bool hasUriList = false;
            for (size_t i = 0; i < mimes.size(); ++i)
                if (SwinxWlDndOfferHasMime(m_offer, mimes[i].c_str()))
                {
                    hasUriList = true;
                    break;
                }
            if (!hasUriList)
            {
                mimes.clear();
                clipFormatMimes(CF_TEXT, mimes);
                textAsFiles = true;
            }
        }
        for (size_t i = 0; i < mimes.size(); ++i)
        {
            if (!SwinxWlDndOfferHasMime(m_offer, mimes[i].c_str()))
                continue;
            void *buf = NULL;
            int cb = SwinxWlDndOfferRead(m_offer, mimes[i].c_str(), &buf);
            if (cb > 0 && buf)
            {
                HGLOBAL h = NULL;
                if (textAsFiles)
                    h = clipTextToHDrop(buf, (size_t)cb);
                else if (fmt == CF_HDROP)
                    h = clipUriListToHDrop((const char *)buf, (size_t)cb);
                else
                    h = (HGLOBAL)clipBytesHandle(fmt, buf, (size_t)cb);
                SwinxWlDataFree(buf);
                if (h)
                {
                    med->tymed = TYMED_HGLOBAL;
                    med->hGlobal = h;
                    return S_OK;
                }
            }
            else if (buf)
                SwinxWlDataFree(buf);
        }
        return DV_E_FORMATETC;
    }
    HRESULT WINAPI QueryGetData(FORMATETC *f) override
    {
        HRESULT hr = DV_E_TYMED;
        if (f && f->tymed == TYMED_HGLOBAL)
        {
            hr = DV_E_FORMATETC;
            for (size_t i = 0; i < m_formats.size(); ++i)
                if (m_formats[i] == f->cfFormat)
                {
                    hr = S_OK;
                    break;
                }
        }
        return hr;
    }
    HRESULT WINAPI EnumFormatEtc(DWORD dir, IEnumFORMATETC **pp) override
    {
        if (dir != DATADIR_GET || !pp)
            return E_NOTIMPL;
        *pp = new SWaylandFormatEnum(m_formats);
        return S_OK;
    }
    HRESULT WINAPI GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
    HRESULT WINAPI SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT WINAPI GetCanonicalFormatEtc(FORMATETC *, FORMATETC *) override { return E_NOTIMPL; }
    HRESULT WINAPI DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override { return E_NOTIMPL; }
    HRESULT WINAPI DUnadvise(DWORD) override { return E_NOTIMPL; }
    HRESULT WINAPI EnumDAdvise(IEnumSTATDATA **) override { return E_NOTIMPL; }
    IUNKNOWN_BEGIN(IDataObject)
    IUNKNOWN_END()
  private:
    SwinxWlDataOffer *m_offer;
    std::vector<UINT> m_formats;
};

struct WlDragSourceCtx
{
    IDataObject *pdo;
    IDropSource *pds;
    UINT fmt;
    std::vector<unsigned char> bytes;
    size_t pos;
    int dropReported;
    DWORD lastEffect;         // 上次已设置的拖放光标
};

int dragSourceRead(void *ctx, const char *mime, void *buf, int cap)
{
    WlDragSourceCtx *c = (WlDragSourceCtx *)ctx;
    if (!c)
        return -1;
    if (!buf || cap <= 0)
    {
        c->pos = 0;
        c->bytes.clear();
        return 0;
    }
    if (!c->pdo || !mime)
        return -1;
    if (c->pos == 0)
    {
        UINT fmt = clipMimeFormat(mime);
        FORMATETC f;
        STGMEDIUM med;
        memset(&f, 0, sizeof(f));
        memset(&med, 0, sizeof(med));
        f.cfFormat = (CLIPFORMAT)fmt;
        f.dwAspect = DVASPECT_CONTENT;
        f.lindex = -1;
        f.tymed = TYMED_HGLOBAL;
        if (c->pdo->GetData(&f, &med) != S_OK || !med.hGlobal)
            return -1;
        clipHandleBytes(med.hGlobal, fmt, c->bytes);
        ReleaseStgMedium(&med);
        c->fmt = fmt;
    }
    if (c->pos >= c->bytes.size())
        return 0;
    size_t n = std::min(c->bytes.size() - c->pos, (size_t)cap);
    memcpy(buf, &c->bytes[c->pos], n);
    c->pos += n;
    // 保留 pos==size 作为 EOF，不能清回 0（否则 send 循环永远不结束）。
    return (int)n;
}

int dragQuery(void *ctx, int keyState)
{
    // ctx 是 WlDragSourceCtx*，不是 IDropSource*。直接 cast 会把 pdo
    // （IDataObject 的 vtable）当成 IDropSource 的 vtable，虚调用跳错函数。
    WlDragSourceCtx *c = (WlDragSourceCtx *)ctx;
    if (!c || !c->pds)
        return -1;
    // MK_* 不含 Esc；re41 的 CDropSource 靠 fEscapePressed 返回 CANCEL。
    BOOL esc = (::GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    HRESULT hr = c->pds->QueryContinueDrag(esc, (DWORD)keyState);
    if (hr == DRAGDROP_S_CANCEL)
    {
        return -1;
    }
    if (hr == DRAGDROP_S_DROP || hr == S_OK)
    {
        if (hr == DRAGDROP_S_DROP && !c->dropReported)
        {
            c->dropReported = 1;
        }
        /* 对齐 X11 SDragdrop.cpp give_feedback()：
         * re41 的 CDropSource::GiveFeedback 恒返 DRAGDROP_S_USEDEFAULTCURSORS，
         * 要求按 effect 显示系统拖放光标（MOVE/LINK/COPY/NODROP 四态）。
         * Wayland 的 DnD grab 期间合成器**忽略 wl_pointer.set_cursor**，真正
         * 可见的只有 drag icon，所以同一份 IDC 位图要同时喂给光标和图标：
         * 前者在不抢 grab 的合成器上生效，后者是 GNOME/KWin 下的实际画面。 */
        DWORD fbEffect = SwinxWlDragEffect();
        HRESULT fbHr = c->pds->GiveFeedback(fbEffect);
        if (fbHr == DRAGDROP_S_USEDEFAULTCURSORS && fbEffect != c->lastEffect)
        {
            LPCTSTR res = IDC_NODROP;
            if (fbEffect & DROPEFFECT_MOVE)      res = IDC_MOVE;
            else if (fbEffect & DROPEFFECT_LINK) res = IDC_LINK;
            else if (fbEffect & DROPEFFECT_COPY) res = IDC_COPY;
            HCURSOR hcur = ::LoadCursor(NULL, res);
            std::vector<uint32_t> pixels;
            int cw = 0, ch = 0, hx = 0, hy = 0;
            if (wlCursorPixels(hcur, pixels, cw, ch, hx, hy))
            {
                SwinxWlSetCursorImage(&pixels[0], cw, ch, hx, hy);
                int iconOk = SwinxWlDragIconImage(&pixels[0], cw, ch, hx, hy);
                if (iconOk)
                    c->lastEffect = fbEffect; // 没上屏就留到下轮重试
            }
            else
            {
                c->lastEffect = fbEffect;
            }
        }
    }
    return 1; // 放下由合成器在 button release 时触发 dnd_finished
}

IDropTarget *findDropTarget(SConnection *conn, HWND hWnd)
{
    HWND cur = hWnd;
    for (int depth = 0; cur && depth < 64; ++depth)
    {
        IDropTarget *t = conn->getDropTarget(cur);
        if (t)
            return t;
        // CSD：wl_data_device.enter 给的 surface 可能是装饰窗（xdg_toplevel），
        // 而 SOUI 把 IDropTarget 登记在内容窗上；两半都要找。
        WndObj w = WndMgr::fromHwnd(cur);
        if (w)
        {
            HWND alt = (cur == w->plat.frameContent) ? w->plat.frame : w->plat.frameContent;
            if (alt && alt != cur)
            {
                t = conn->getDropTarget(alt);
                if (t)
                    return t;
            }
        }
        cur = ::GetParent(cur);
    }
    return NULL;
}

POINTL dndPoint(HWND hWnd, int x, int y)
{
    POINTL pt = { x, y };
    RECT rc;
    if (hWnd && ::GetWindowRect(hWnd, &rc))
    {
        pt.x += rc.left;
        pt.y += rc.top;
    }
    return pt;
}

} // namespace

HRESULT SConnection::RegisterDragDrop(HWND hWnd, IDropTarget *pTarget)
{
    if (!hWnd || !pTarget)
        return E_INVALIDARG;
    RevokeDragDrop(hWnd);
    pTarget->AddRef();
    m_dropTargets[hWnd] = pTarget;
    return S_OK;
}

HRESULT SConnection::RevokeDragDrop(HWND hWnd)
{
    std::map<HWND, IDropTarget *>::iterator it = m_dropTargets.find(hWnd);
    if (it == m_dropTargets.end())
        return E_INVALIDARG;
    if (it->second)
        it->second->Release();
    m_dropTargets.erase(it);
    return S_OK;
}

IDropTarget *SConnection::getDropTarget(HWND hWnd) const
{
    std::map<HWND, IDropTarget *>::const_iterator it = m_dropTargets.find(hWnd);
    return it == m_dropTargets.end() ? NULL : it->second;
}

void SConnection::onWlDndEnter(HWND hWnd, int x, int y, int keyState, void *offer)
{
    SwinxWlDataOffer *o = (SwinxWlDataOffer *)offer;
    m_dndTarget = findDropTarget(this, hWnd);
    m_dndOffer = offer;
    m_dndKeyState = keyState;
    m_dndX = x;
    m_dndY = y;
    m_dndEffect = DROPEFFECT_NONE;
    if (!m_dndTarget)
    {
        // 没有登记 IDropTarget：接受第一个 mime，避免合成器一直等 accept
        std::vector<std::string> mimes;
        clipTextMimes(mimes);
        for (size_t i = 0; i < mimes.size(); ++i)
            if (SwinxWlDndOfferHasMime(o, mimes[i].c_str()))
            {
                SwinxWlDndOfferAccept(o, mimes[i].c_str());
                break;
            }
        return;
    }
    m_dndTarget->AddRef();
    m_dndDataObject = new SWaylandDropDataObject(o);
    POINTL pt = dndPoint(hWnd, x, y);
    m_dndTarget->DragEnter(m_dndDataObject, (DWORD)keyState, pt, &m_dndEffect);
    SwinxWlDndOfferAccept(o, kMimeUtf8);
    /* 把目标的判定回告合成器，源端（可能就是本进程）才能换拖动光标。 */
    SwinxWlDndOfferSetEffect(o, m_dndEffect);
}

void SConnection::onWlDndMotion(HWND hWnd, int x, int y, int keyState, void *offer)
{
    m_dndKeyState = keyState;
    m_dndX = x;
    m_dndY = y;
    if (!m_dndTarget)
        return;
    POINTL pt = dndPoint(hWnd, x, y);
    DWORD prevEffect = m_dndEffect;
    m_dndTarget->DragOver((DWORD)keyState, pt, &m_dndEffect);
    /* 反馈变化才回告：每次 motion 都 set_actions 会白刷协议请求。 */
    if (m_dndEffect != prevEffect)
        SwinxWlDndOfferSetEffect((SwinxWlDataOffer *)offer, m_dndEffect);
}

void SConnection::onWlDndLeave(HWND)
{
    if (m_dndTarget)
    {
        m_dndTarget->DragLeave();
        m_dndTarget->Release();
        m_dndTarget = NULL;
    }
    if (m_dndDataObject)
    {
        m_dndDataObject->Release();
        m_dndDataObject = NULL;
    }
    m_dndOffer = NULL;
}

void SConnection::onWlDndDrop(HWND hWnd, void *offer)
{
    SwinxWlDataOffer *o = (SwinxWlDataOffer *)offer;
    if (m_dndTarget && m_dndDataObject)
    {
        POINTL pt = dndPoint(hWnd, m_dndX, m_dndY);
        m_dndTarget->Drop(m_dndDataObject, (DWORD)m_dndKeyState, pt, &m_dndEffect);
    }
    onWlDndLeave(hWnd);
    SwinxWlDndOfferFinish(o);
}

/* text-input-v3 commit_string → 逐字符 WM_IME_CHAR，与 X11
 * SConnection::xim_commit_string 同一条路径；SOUI 容器会按当前焦点把消息
 * 派给真正的 SWND（richedit / edit），所以目标取 m_hFocus 而不是 surface HWND。 */
void SConnection::onWlImeCommit(HWND hWnd, const char *utf8, int serial)
{
    (void)serial;
    if (!utf8 || !*utf8)
        return;
    HWND target = IsWindow(m_hFocus) ? m_hFocus : hWnd;
    if (!target)
        return;
    std::wstring buf;
    towstring(utf8, (int)strlen(utf8), buf);
    for (size_t i = 0; i < buf.size(); ++i)
    {
        Msg *pMsg = new Msg;
        pMsg->hwnd = target;
        pMsg->message = WM_IME_CHAR;
        pMsg->wParam = (WPARAM)buf[i];
        pMsg->lParam = 0;
        pMsg->time = GetTickCount();
        postMsg(pMsg);
    }
}

HRESULT SConnection::DoDragDrop(IDataObject *pDataObject, IDropSource *pDropSource,
                                DWORD dwOKEffects, DWORD *pdwEffect)
{
    if (pdwEffect)
        *pdwEffect = DROPEFFECT_NONE;
    if (!pDataObject || !pDropSource)
        return DRAGDROP_S_CANCEL;
    HWND origin = ::GetActiveWindow();
    if (!origin || !SwinxWlSurfaceGet(origin))
        return DRAGDROP_S_CANCEL;

    std::vector<UINT> fmts;
    IEnumFORMATETC *en = NULL;
    if (pDataObject->EnumFormatEtc(DATADIR_GET, &en) == S_OK && en)
    {
        FORMATETC f;
        ULONG got = 0;
        while (en->Next(1, &f, &got) == S_OK && got == 1)
            fmts.push_back(f.cfFormat);
        en->Release();
    }
    else
    {
        fmts.push_back(CF_UNICODETEXT);
        fmts.push_back(CF_TEXT);
    }
    std::vector<std::string> mimes;
    for (size_t i = 0; i < fmts.size(); ++i)
        clipFormatMimes(fmts[i], mimes);

    WlDragSourceCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pdo = pDataObject;
    ctx.pds = pDropSource;
    /* 0 也是合法 effect（DROPEFFECT_NONE）：初值不能是 0，否则目标一开始就
     * 拒绝时不会触发第一次图标更新，画面上留在 24x24 占位徽标。 */
    ctx.lastEffect = 0xFFFFFFFFu;
    std::vector<const char *> ptrs;
    for (size_t i = 0; i < mimes.size(); ++i)
        ptrs.push_back(mimes[i].c_str());
    // EnumFormatEtc 可能成功但返回 0 个格式；空 vector 取 &ptrs[0] 是 UB，
    // 且没有 offer 的 drag source 合成器根本不会发 drop。
    if (ptrs.empty())
    {
        mimes.push_back(kMimeUtf8);
        ptrs.push_back(mimes[0].c_str());
    }

    (void)dwOKEffects;
    int dropped = SwinxWlStartDrag(&ctx, &ptrs[0], (int)ptrs.size(),
                                   dragSourceRead, dragQuery, origin, 0);
    /* dragQuery 把 seat 级光标 surface 换成了 copy/link/nodrop 图案；Wayland
     * 没有 X11 那种窗口级光标属性可以自然回弹，这里按当前鼠标所在窗口
     * 重新解析一次光标（对齐 X11 SetCursor 随窗口恢复的语义）。 */
    applyWlCursor(this, m_hMouseFocus ? m_hMouseFocus : origin);
    if (pdwEffect)
        *pdwEffect = dropped ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return dropped ? DRAGDROP_S_DROP : DRAGDROP_S_CANCEL;
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
    , m_hMouseFocus(0)
    , m_hActive(0)
    , m_hForeground(0)
    , m_nextRegMsg(WM_USER + 100000)
    , m_cursorCount(1)
    /* 虚拟屏幕尺寸：仍以帧缓冲的初始尺寸为基准（DPI 缩放由 SOUI 的 SetScale
     * 在布局层完成，应用坐标 == 物理像素），供弹出窗上下翻转判定
     * （SComboBase::CalcPopupRect -> GetMonitorInfo）使用；主窗口 configure
     * 上报新尺寸后由 onWlConfigure() 更新。每窗像素本身不再经过全局帧缓冲。 */
    , m_screenW(SwinxFbWidth())
    , m_screenH(SwinxFbHeight())
    , m_msgRecursion(0)
    , m_bScreenDirty(false)
    , m_caretBlinkTime(TS_CARET)
    , m_clipboard(new SClipboard())
#ifdef SWINX_HAVE_SNI
    , m_trayMgr(NULL)
#endif
    , m_dndTarget(NULL)
    , m_dndDataObject(NULL)
    , m_dndOffer(NULL)
    , m_dndEffect(DROPEFFECT_NONE)
    , m_dndKeyState(0)
    , m_dndX(0)
    , m_dndY(0)
{
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
    memset(m_keyboardState, 0, sizeof(m_keyboardState));
    m_hQueueEvt = CreateEventA(nullptr, FALSE /*auto reset*/, FALSE, nullptr);
    if (!s_pWlConn)
    {
        s_pWlConn = this;
        SwinxWlSetEventCb(SwinxWlEventThunk);
        if (!SwinxWlInit())
            SLOG_FMTW("Wayland display unavailable; running with an offscreen framebuffer");
        else
        {
            m_screenW = SwinxFbWidth();
            m_screenH = SwinxFbHeight();
        }
    }
    SLOG_FMTI("SConnection created for thread %d", m_tid);
}

SConnection::~SConnection()
{
    if (s_pWlConn == this)
    {
        // leak-on-purpose：退出期不再断开 wl_display（见 swinx/AGENTS.md §5），
        // 只摘掉回调指针，避免后续事件回调到已销毁的连接。
        s_pWlConn = NULL;
        SwinxWlSetEventCb(NULL);
    }
    std::unique_lock<CountMutex> lock(m_mutex);
    CloseHandle(m_hQueueEvt);
    if (m_dndTarget)
        m_dndTarget->Release();
    if (m_dndDataObject)
        m_dndDataObject->Release();
    for (std::map<HWND, IDropTarget *>::iterator it = m_dropTargets.begin();
         it != m_dropTargets.end(); ++it)
        if (it->second)
            it->second->Release();
    m_dropTargets.clear();
    delete m_clipboard;
#ifdef SWINX_HAVE_SNI
    delete m_trayMgr;
    m_trayMgr = NULL;
#endif
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

void SConnection::AfterProcMsg(HWND hWnd, UINT msg, WPARAM, LPARAM, LRESULT)
{
    /* SetWindowPos 会先发 WM_MOVE 让核心把 rc 平移到应用指定坐标，但对
     * xdg_toplevel 这个坐标没有任何协议意义（合成器可能把它摆到别处）。
     * 事件处理完立刻归零，保证后续 WM_NCHITTEST / tooltip anchor 与真实的
     * surface 局部坐标同一口径。 */
    if (msg == WM_MOVE && hWnd)
        normalizeWlToplevelOrigin(this, hWnd);

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
    /* X11 读根窗口的 _NET_DOUBLE_CLICK_TIME；Wayland 没有等价通道
     * （GNOME 的 org.gnome.desktop.peripherals.mouse double-click 要拉 gsettings），
     * 这里用 GTK 的默认值 400ms，需要时用 SWINX_WL_DOUBLE_CLICK_MS 覆盖
     * （慢速双击的虚拟机、自动化测试都靠它）。 */
    static UINT span = 0;
    if (!span)
    {
        const char *e = getenv("SWINX_WL_DOUBLE_CLICK_MS");
        UINT v = (e && *e) ? (UINT)atoi(e) : 0;
        span = v ? v : 400;
    }
    return span;
}

uint32_t SConnection::OnButtonPress(int idx, uint32_t t)
{
    if (idx < 0 || idx > 2 || !t)
        return (uint32_t)-1;
    uint32_t prev = m_tsPrevPress[idx];
    m_tsPrevPress[idx] = t;
    if (!prev)
        return (uint32_t)-1;      // 该键的第一次按下
    return t - prev;              // 无符号相减：时间戳回绕也成立
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
            /* timer 日志默认关闭（见 wlTimerDiag），需要时设
             * SWINX_WL_TRACE_TIMER=1；这里仍只记带 HWND 的窗口 timer，
             * 跳过 hwnd==NULL 的脚本 TIMERPROC。tooltip 诊断优先用 SOUI 层
             * 的 SOUI_TIP_TRACE。 */
            if (wlTimerDiag() && ti.hWnd)
                fprintf(stderr, "[wl] timer-fire hwnd=%p id=%lu elapse=%u proc=%p\n",
                        ti.hWnd, (unsigned long)ti.id, ti.elapse, (void *)ti.proc);
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

/* 睡到"有活干"或超时，给 waitMsg / waitMutliObjectAndMsg 共用。
 *
 * 两个唤醒源：wl_display 的 fd（协议事件：输入、configure、frame 回调）和
 * m_hQueueEvt 的 fd（postMsg / updateMsgQueue 投递）。只等 m_hQueueEvt 的话
 * wl_display 事件最多要等到下一轮超时，输入会明显发粘，所以必须一起 poll。
 *
 * 附带责任：把 m_hQueueEvt 上积压的信号读走（auto-reset 的 onWaitDone 会读空）。
 * m_hQueueEvt 是 pipe 实现的 event，写入方每次 SetEvent 追加 1 字节，而只有
 * 这里（以及 waitMsg 入口）会读；读的一方一旦缺席，pipe 涨到 64KB 后
 * _SynHandle::writeSignal() 的 write() 就永久阻塞，整个消息泵死在里面。 */
void SConnection::waitWakeup(DWORD ms)
{
    struct pollfd pfds[2];
    nfds_t nfds = 0;
    int wlFd = SwinxWlFd();
    if (wlFd >= 0)
    {
        pfds[nfds].fd = wlFd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;
    }
    _SynHandle *queueSyn = GetSynHandle(m_hQueueEvt);
    int queueFd = queueSyn ? queueSyn->getReadFd() : -1;
    if (queueFd >= 0)
    {
        pfds[nfds].fd = queueFd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;
    }
    if (nfds > 0)
        poll(pfds, nfds, (int)ms);
    /* 没有 wl_display（离屏模式）时退回在 event 上睡满 ms；有 fd 时只做
     * 非阻塞的一次"读空"。 */
    WaitForSingleObject(m_hQueueEvt, nfds > 0 ? 0 : ms);
}

bool SConnection::waitMsg(UINT timeOut)
{
    /* 进来先清一次唤醒信号。m_hQueueEvt 是 auto-reset event（pipe 实现），
     * postMsg()/updateMsgQueue() 每投递一次就往里写 1 字节，而这里可能在
     * "队列非空"时直接返回、本轮根本没读到它。SOUI 主循环在持续有消息时每轮
     * 都会进来，写多读少 ⇒ pipe 一路涨到 64KB，随后 writeSignal() 的 write()
     * 阻塞，表现就是"切走一段时间再回来，鼠标一动就卡死"。
     * 这个信号只是"可能有消息"的提示，真正的消息在 m_msgQueue 里，读掉它
     * 不影响正确性。 */
    WaitForSingleObject(m_hQueueEvt, 0);
    // 事件源是 wl_display（独立 fd）加上消息队列，所以每轮都要先派发
    // wl_display 再看队列；等不到东西时用 waitWakeup() 阻塞，最多 10ms
    // 一轮（timeOut 更小时以它为准），configure/close 不会被拖延。
    uint64_t start = GetTickCount();
    for (;;)
    {
        if (SwinxWlDispatch() < 0)
        {
            // 显示服务器断开（EPIPE / POLLHUP）：没有窗口可再服务，按退出处理，
            // 否则消息循环只会在 10ms 轮询里空转。
            m_bQuit = true;
            return false;
        }
#ifdef SWINX_HAVE_SNI
        // 托盘的 SNI 点击/属性请求走独立的 session D-Bus 连接，与 wl_display
        // 是两套 fd。每轮派发一次，点击会经 PostMessage 落进 m_msgQueue，
        // 随即被下面的 updateMsgQueue 收进派发流。
        if (m_trayMgr)
            m_trayMgr->pump();
#endif
        updateMsgQueue(0);
        {
            std::unique_lock<CountMutex> lock(m_mutex);
            if (!m_msgQueue.empty())
                return true;
        }
        if (m_bQuit)
            return false;
        DWORD slice = 10;
        if (timeOut != INFINITE)
        {
            uint64_t elapsed = GetTickCount() - start;
            if (elapsed >= (uint64_t)timeOut)
                return false;
            uint64_t remain = (uint64_t)timeOut - elapsed;
            if (remain < slice)
                slice = (DWORD)remain;
        }
        if (slice == 0)
            slice = 1;
        waitWakeup(slice);
    }
    return false;
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, BOOL fWaitAll, DWORD dwWaitMask)
{
    /* SOUI 主循环的形态就是 nCount==0 + QS_ALLINPUT（SMsgLoop::RunIdle 和
     * SMsgLoop::WaitMsg 都传 NULL），直接交给 waitMsg：它把 wl_display 与
     * m_hQueueEvt 一起 poll，并负责读掉唤醒信号。
     * 原来这条路走下面的 sched_yield 忙转：一是空闲时烧满一核，二是从不读
     * m_hQueueEvt ⇒ pipe 只写不读，累积到 64KB 后 writeSignal() 的 write()
     * 永久阻塞（切换出虚拟机一段时间再回来、鼠标一动就卡死，栈顶正是
     * SetEvent → writeSignal → write）。 */
    if (nCount <= 0 && (dwWaitMask & QS_ALLINPUT) == QS_ALLINPUT)
        return waitMsg((UINT)timeout) ? (int)WAIT_OBJECT_0 : (int)WAIT_TIMEOUT;
    /* 带等待对象时同样要先消费唤醒信号：下面每轮 updateMsgQueue() 都会写。 */
    WaitForSingleObject(m_hQueueEvt, 0);
    // Wayland port: poll objects then the queue; the objects are Win32-compat
    // events/mutexes/semaphores from src/platform/posix, safe to poll
    // non-blocking.  The wl_display fd is separate from these handles, so each
    // iteration also pumps display events.
    uint64_t start = GetTickCount();
    for (;;)
    {
        /* 显示连接已断（协议错误/合成器退出）时 SwinxWlDispatch 恒返回 -1，
         * 若继续等下去，消息泵只会白等并且永远等不到消息——表现就是
         * "释放鼠标后卡死"。按退出处理，与 waitMsg 一致。 */
        if (SwinxWlDispatch() < 0)
        {
            m_bQuit = true;
            return (int)WAIT_FAILED;
        }
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
        /* 有界阻塞等待，替掉原来的 sched_yield() 忙转：wl_display 或
         * m_hQueueEvt 任一到货立即醒，都不来则最多睡 slice 毫秒（timeout
         * 剩余更少时以它为准）。waitWakeup() 顺带把信号读空，这条路径同样
         * 不会把 pipe 写满。 */
        DWORD slice = 10;
        if (timeout != INFINITE)
        {
            uint64_t elapsed = GetTickCount() - start;
            if (elapsed >= (uint64_t)timeout)
                return WAIT_TIMEOUT;
            uint64_t remain = (uint64_t)timeout - elapsed;
            if (remain < (uint64_t)slice)
                slice = (DWORD)remain;
        }
        waitWakeup(slice);
    }
    // not reached
    (void)dwWaitMask;
}

// Win32 的 TranslateMessage 只给"这个键确实代表一个字符"的按键补 WM_CHAR：
// 方向键、F1..F24、修饰键、Insert/Delete 这些都不会产生字符。阶段 0 没有
// wl_seat/键盘布局可查，所以这张表就是 Win32 在 US 布局上的那一套；表里没有的 vk
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
    // 先取显示服务器事件：configure/close 会在这里被转成 WM_SIZE/WM_CLOSE
    // 投进队列，之后才轮到应用看到的"有没有消息"。
    SwinxWlDispatch();
    // 再补上屏：派发之外的绘制（回调任务、UpdateWindow 式直接 GetDC/ReleaseDC）
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
    // 与 wasm 的差别是"等"：队列为空时进入 waitMsg()，由它切片派发 wl_display
    // 事件并睡眠，维持 GetMessage 的阻塞语义。
    for (;;)
    {
        if (peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE))
            return lpMsg->message != WM_QUIT;
        if (m_bQuit)
            return FALSE;
        waitMsg();
    }
    return FALSE;
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
    if (wlTimerDiag() && hWnd)
        fprintf(stderr, "[wl] timer-set hwnd=%p id=%lu elapse=%u proc=%p\n",
                hWnd, (unsigned long)id, uElapse, (void *)proc);
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

/* HCURSOR -> cairo ARGB32（预乘，内存序 B,G,R,A，与 wl_shm ARGB8888 一致）。
 * 系统光标由 CursorMgr 从内置 PNG 解码成 32bpp DIB，直接拷 bm.bmBits 即可；
 * 非 32bpp 的自定义光标显式放弃（与 X11 getXcbCursor 的口径一致），不伪造
 * 一个画错的图案。 */
static bool wlCursorPixels(HCURSOR cursor, std::vector<uint32_t> &out,
                           int &w, int &h, int &hotX, int &hotY)
{
    if (!cursor)
        return false;
    ICONINFO info = {};
    if (!GetIconInfo(cursor, &info) || !info.hbmColor)
        return false;
    BITMAP bm = {};
    bool ok = GetObject(info.hbmColor, sizeof(bm), &bm) != 0 &&
              bm.bmBitsPixel == 32 && bm.bmBits && bm.bmWidth > 0 && bm.bmHeight > 0;
    if (ok)
    {
        w = bm.bmWidth;
        h = bm.bmHeight < 0 ? -bm.bmHeight : bm.bmHeight;
        hotX = (int)info.xHotspot;
        hotY = (int)info.yHotspot;
        out.resize((size_t)w * (size_t)h);
        memcpy(&out[0], bm.bmBits, out.size() * sizeof(uint32_t));
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return ok;
}

/* 按 Win32 语义沿父链找最近的"设过光标"的窗口（GTK gdk/wdkwindow.c
 * update_cursor 同款）。光标本身是 seat 级的，不能给每个 subsurface 各设一份。 */
static void applyWlCursor(SConnection *conn, HWND hWnd)
{
    std::vector<uint32_t> pixels;
    int w = 0, h = 0, hotX = 0, hotY = 0;
    HCURSOR cursor = 0;
    HWND cur = hWnd;
    /* 装饰窗（CSD frame）不是应用的窗口，只是承载标题栏/边框的容器：指针
     * 一旦落在它身上（标题栏、或 frame 比内容 surface 宽出来的那条边），
     * 按它自己解析光标必然落到"窗口类光标 = IDC_ARROW"，于是画面上的箭头
     * 会从应用主题光标突然变成系统默认箭头——看起来就像"光标换成了别的
     * 窗口的"。这里直接沿用它包裹的内容窗的语义。 */
    WndObj fw = WndMgr::fromHwnd(cur);
    if (fw && fw->plat.frameContent && IsWindow(fw->plat.frameContent))
        cur = fw->plat.frameContent;
    for (int depth = 0; cur && depth < 64; ++depth)
    {
        HCURSOR found = conn->CursorForWindow(cur);
        if (found)
        {
            cursor = found;
            break;
        }
        HWND parent = ::GetParent(cur);
        cur = (parent && parent != cur) ? parent : 0;
    }
    if (!cursor)
        cursor = ::LoadCursor(NULL, IDC_ARROW);
    if (wlCursorPixels(cursor, pixels, w, h, hotX, hotY))
        SwinxWlSetCursorImage(&pixels[0], w, h, hotX, hotY);
    else
        SwinxWlRefreshCursor(); // 没有可用像素时至少把默认光标用新 serial 重设
}

HCURSOR SConnection::SetCursor(HWND hWnd, HCURSOR cursor)
{
    HCURSOR old = 0;
    auto it = m_wndCursor.find(hWnd);
    if (it != m_wndCursor.end())
        old = it->second;
    m_wndCursor[hWnd] = cursor;
    /* hWnd==NULL 的 ::SetCursor() 作用于当前指针下的窗口（X11 的
     * m_hWndLastMouseMove 语义）；这里同样优先用指针焦点，再退到活动窗。 */
    HWND target = hWnd ? hWnd : (m_hMouseFocus ? m_hMouseFocus : GetActiveWnd());
    applyWlCursor(this, target);
    return old;
}

HCURSOR SConnection::GetCursor()
{
    HWND hWnd = m_hMouseFocus ? m_hMouseFocus : GetActiveWnd();
    auto it = m_wndCursor.find(hWnd);
    return it == m_wndCursor.end() ? 0 : it->second;
}

void SConnection::SetMouseFocus(HWND hWnd)
{
    m_hMouseFocus = hWnd;
}

HWND SConnection::MouseFocusHwnd() const
{
    return m_hMouseFocus;
}

HCURSOR SConnection::CursorForWindow(HWND hWnd) const
{
    auto it = m_wndCursor.find(hWnd);
    return it == m_wndCursor.end() ? 0 : it->second;
}

BOOL SConnection::DestroyCursor(HCURSOR cursor)
{
    /* 与 X11 后端一致：还有窗口在用就不能销毁（CursorMgr 的标准光标有单独
     * 守卫，这里只挡自定义光标）。framebuffer 复制了像素，不持有 HICON。 */
    for (auto &it : m_wndCursor)
        if (it.second == cursor)
            return FALSE;
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
    // single-window traversal depth: Wayland windows are virtual objects
    // owned by _Window; hit-test refinement lands with the renderer (TODO).
    return hWnd;
}

BOOL SConnection::IsWindow(HWND hWnd) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    return wndObj ? !wndObj->bDestroyed : FALSE;
}

/* xdg_toplevel 没有"把窗口摆到 (x,y)"的请求：xdg-shell 的 configure 只回尺寸，
 * 位置是合成器的独占权利（GNOME 的 mutter 按 org.gnome.mutter
 * center-new-windows 决定居中还是从工作区左上角级联）。所以对这类窗口，
 * SetWindowPos/MoveWindow 的坐标只能记进 _Window::rc 供应用自己用，屏幕上不会
 * 有任何变化。这里把"位置请求被丢弃"显式打一次——否则调用方（和三个月后的
 * 我们）几乎一定会把它误判成 swinx 的坐标计算 bug。
 * 有 owner 的 WS_POPUP 对话框不在此列：它们在 SetParent 里被转成 xdg_popup，
 * 由 xdg_positioner 精确落地，位置真能生效。 */
static void wlWarnToplevelPlacement(HWND hWnd, int x, int y)
{
    static bool warned = false;
    if (warned || !wlDiag())
        return;
    warned = true;
    fprintf(stderr, "[wl] top-level placement ignored hwnd=%p req=(%d,%d): "
                    "xdg-shell has no client-side position request, the "
                    "compositor decides. GNOME: gsettings set "
                    "org.gnome.mutter center-new-windows true\n",
            hWnd, x, y);
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const
{
    // virtual windows: _Window::rc is updated by the core WM_MOVE handler
    // (src/wnd.cpp CallWindowProcPriv); the platform only has to note that the
    // screen layout changed so the next flush recomposes every shell window.
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    {
        /* 真正的 xdg 对象可能挂在装饰窗上，先解析出"实际承载位置的 surface" */
        HWND hEff = (wndObj && wndObj->plat.frame) ? wndObj->plat.frame : hWnd;
        SwinxWlSurface *wlEff = SwinxWlSurfaceGet(hEff);
        if (wlEff && SwinxWlSurfaceGetRole(wlEff) == SWINX_WL_SURF_TOPLEVEL)
            wlWarnToplevelPlacement(hWnd, x, y);
    }
    if (wndObj && wndObj->plat.frame)
    {
        /* 被装饰窗包着的窗口：真正要移动的是装饰窗（它是 xdg_toplevel /
         * xdg_popup）。这里拿到的是应用刚写入的新坐标，早于 WM_MOVE 的 rc
         * 更新与归一，所以是唯一能拿到"应用想要的位置"的时机。
         * 尺寸不动：本回调只负责移动，紧随其后的 SetWindowSize 会带上新尺寸。 */
        SwinxWlFrameMove(wndObj->plat.frame, x, y);
    }
    (void)hWnd;
    (void)x;
    (void)y;
    m_bScreenDirty = true;
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj && wndObj->plat.frame)
    {
        /* 只改尺寸：原点由 SetWindowPos 那一次回调负责，两者在 WM_WINDOWPOSCHANGED
         * 里成对出现，各自只更新自己那一边，避免互相覆盖。 */
        SwinxWlFrameResize(wndObj->plat.frame, cx, cy);
    }
    (void)hWnd;
    (void)cx;
    (void)cy;
    m_bScreenDirty = true;
}

BOOL SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const
{
    // 虚拟窗口：_Window::rc 平时由核心的 WM_MOVE 处理器维护，但 xdg_toplevel
    // 的 configure 是从平台层直接改宿主窗尺寸、没有走 SendMessage 链路，
    // 所以这里显式写入并触发整屏重合成。
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    wndObj->rc.left = x;
    wndObj->rc.top = y;
    wndObj->rc.right = x + cx;
    wndObj->rc.bottom = y + cy;
    if (wndObj->plat.frame)
        wlPlaceFrame(hWnd, x, y, cx, cy);
    m_bScreenDirty = true;
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
    /* 对齐 X11：GetDpiForWindow(hwnd) 与 GetDpi(FALSE) 都返回窗口所在显示器
     * 的 DPI。framebuffer 侧用 wp_fractional_scale_v1（120=1.0）取真实倍率，
     * 向上贴到 SOUI 的 100/125/150/200/250/300 档，避免 SDpiHandler 的
     * NormalizeScale 向下取整把 150% 打回 125%。当前焦点窗没有 surface 时
     * 退回主屏倍率。SWINX_WL_SCALE=0 可关闭（恒 96dpi）。 */
    HWND hwnd = m_hActive ? m_hActive : m_hFocus;
    return SwinxWlGetDpi(hwnd);
}

HWND SConnection::GetForegroundWindow()
{
    return m_hForeground;
}

BOOL SConnection::SetForegroundWindow(HWND hWnd)
{
    /* 对齐 X11（linux/SConnection.cpp:2401）：前台窗口同时是活动窗口。
     * GetActiveWnd() 是 tooltip / 菜单挑 popup 父窗的依据——一直返回 0 会让
     * 它们挂到"最新创建的那块 surface"上（可能是一个对话框的装饰窗甚至另一个
     * popup），anchor 坐标系整个错位，表现为弹窗跑到左上角。 */
    m_hForeground = hWnd;
    SetActiveWindow(hWnd);
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

/* Wayland 后端没有 xkbcommon 的布局对象（键码走静态 evdev→VK 表），系统里
 * 只有唯一可用布局。这里必须照 Win32 在**单布局系统**上的语义实现：
 *   - GetKeyboardLayout 恒返回那个唯一布局的句柄（SWINX_HKL_BASE，不能是 0）；
 *   - HKL_NEXT / HKL_PREV 是"切到下一个/上一个"的魔法值(1/0)，不是布局句柄，
 *     单布局下等价于原地不动 —— 绝不能像旧实现那样把它直接存进 m_hkl
 *     （否则 NEXT 后 GetKeyboardLayout() 变成 1，与切换前不相等，
 *      fun_test 的 KeyboardLayoutTest.NextPrevCycleRestores 就是这么挂的）。 */
HKL SConnection::GetKeyboardLayout(DWORD)
{
    if (!m_hkl)
        m_hkl = (HKL)SWINX_HKL_BASE;
    return m_hkl;
}

UINT SConnection::GetKeyboardLayoutList(int nBuff, HKL *lpList)
{
    if (lpList && nBuff > 0)
        lpList[0] = (HKL)SWINX_HKL_BASE;
    return 1;
}

HKL SConnection::ActivateKeyboardLayout(HKL)
{
    // 单布局：NEXT/PREV 与任意句柄都落在同一个布局上，只回报上一句柄
    HKL prev = GetKeyboardLayout(0);
    m_hkl = (HKL)SWINX_HKL_BASE;
    return prev;
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

BOOL SConnection::OnSetWindowText(HWND hWnd, _Window *pWnd, LPCSTR lpszString)
{
    if (!pWnd)
    {
        WndObj tmp = WndMgr::fromHwnd(hWnd);
        pWnd = tmp.data();
    }
    if (!pWnd)
        return FALSE;
    /* 窗口名就是 STATIC/BUTTON 的显示文本：wnd.cpp:3141 在 WM_NCCREATE 里把
     * CREATESTRUCT::lpszName 送进来。原实现是空桩，wndObj->title 永远为空，
     * CStatic::OnPaint/CButton::OnPaint 画的就是这个空串 —— 表现为 MessageBox
     * 白底、图标、按钮边框都在，正文和按钮文字全没了（而 CalcTextRectA 直接
     * 用 lpText 测量，尺寸反而是对的）。对齐 X11 的 OnSetWindowText。 */
    pWnd->title = lpszString ? lpszString : "";
    /* 顶层窗同步合成器标题。WM_NCCREATE 早于 OnWindowCreate，那里 surface 还
     * 不存在；OnWindowCreate 会用 wnd->title 再补一次。subsurface 无标题。 */
    SwinxWlSurfaceSetTitle(SwinxWlSurfaceGet(hWnd), pWnd->title.c_str());
    /* 有装饰窗时，任务列表/概览里显示的是装饰窗的标题，标题栏也画在它上面。 */
    if (pWnd->plat.frame)
        SwinxWlFrameSetTitle(pWnd->plat.frame, pWnd->title.c_str());
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    return wndObj ? (int)wndObj->title.length() : 0;
}

int SConnection::OnGetWindowTextLengthW(HWND hWnd)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(),
                               (int)wndObj->title.length(), NULL, 0);
}

int SConnection::OnGetWindowTextA(HWND hWnd, char *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !buf || bufLen <= 0)
        return 0;
    // 留出结尾的 '\0'：X11 那份用 < 判断会在 bufLen == length 时写成非终止串
    if (bufLen <= (int)wndObj->title.length())
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(buf, wndObj->title.c_str(), wndObj->title.length() + 1);
    return (int)wndObj->title.length();
}

int SConnection::OnGetWindowTextW(HWND hWnd, wchar_t *buf, int bufLen)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return 0;
    return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(),
                               (int)wndObj->title.length(), buf, bufLen);
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

/* 显示器几何：本后端只跟踪第一个 wl_output（SwinxWlGetScreenSize），所以模型
 * 就是"一台显示器，矩形 (0,0)-(w,h)"。但三个 MonitorFrom* 必须照 Win32 的
 * flag 语义收敛，不能无条件返回主屏——否则桌面外的点/矩形也算"命中"，
 * SOUI 的弹窗翻转、fun_test 的 swinx_multimon.* 全部跑偏。 */
static HMONITOR wlMonitorFallback(HMONITOR hPrimary, DWORD dwFlags)
{
    // MONITOR_DEFAULTTONULL(0) 才允许返回 NULL；其余（最近/主屏）单屏下都是它
    return dwFlags == MONITOR_DEFAULTTONULL ? NULL : hPrimary;
}

static bool wlRectsIntersect(const RECT &a, const RECT &b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

HMONITOR SConnection::MonitorFromWindow(HWND hWnd, DWORD dwFlags)
{
    HMONITOR hPrimary = GetPrimaryMonitor();
    if (!hWnd)
        return wlMonitorFallback(hPrimary, dwFlags);
    RECT rcWin;
    if (!screenRect(hWnd, &rcWin))
        return wlMonitorFallback(hPrimary, dwFlags);
    return MonitorFromRect(&rcWin, dwFlags);
}

HMONITOR SConnection::MonitorFromPoint(POINT pt, DWORD dwFlags)
{
    int n = SwinxWlGetOutputCount();
    for (int i = 0; i < n; ++i)
    {
        RECT rcMon;
        if (!GetMonitorRect((HMONITOR)(uintptr_t)(i + 1), &rcMon))
            continue;
        // Win32 用左闭右开区间：右/下边界上的点属于下一个显示器
        if (pt.x >= rcMon.left && pt.x < rcMon.right &&
            pt.y >= rcMon.top && pt.y < rcMon.bottom)
            return (HMONITOR)(uintptr_t)(i + 1);
    }
    return wlMonitorFallback(GetPrimaryMonitor(), dwFlags);
}

HMONITOR SConnection::MonitorFromRect(LPCRECT lprc, DWORD dwFlags)
{
    HMONITOR hPrimary = GetPrimaryMonitor();
    if (!lprc)
        return hPrimary; // Win32: 空矩形按"当前/主屏"处理
    int n = SwinxWlGetOutputCount();
    HMONITOR best = NULL;
    long long bestArea = 0;
    for (int i = 0; i < n; ++i)
    {
        HMONITOR h = (HMONITOR)(uintptr_t)(i + 1);
        RECT rcMon;
        if (!GetMonitorRect(h, &rcMon) || !wlRectsIntersect(*lprc, rcMon))
            continue;
        long long area = (long long)(std::min(lprc->right, rcMon.right) - std::max(lprc->left, rcMon.left)) *
                         (std::min(lprc->bottom, rcMon.bottom) - std::max(lprc->top, rcMon.top));
        if (area > bestArea)
        {
            bestArea = area;
            best = h;
        }
    }
    return best ? best : wlMonitorFallback(hPrimary, dwFlags);
}

int SConnection::GetMonitorCount() const
{
    return SwinxWlGetOutputCount();
}

HMONITOR SConnection::GetMonitor(int index) const
{
    if (index < 0 || index >= SwinxWlGetOutputCount())
        return NULL;
    return (HMONITOR)(uintptr_t)(index + 1);
}

HMONITOR SConnection::GetPrimaryMonitor() const
{
    return (HMONITOR)1;
}

bool SConnection::GetMonitorRect(HMONITOR hMon, RECT *prc) const
{
    int index = hMon ? (int)(uintptr_t)hMon - 1 : 0;
    if (SwinxWlGetOutputRect(index, prc))
        return true;
    if (!hMon && prc)
    {
        /* wl_output 还没到时的兜底：用帧缓冲初始尺寸，绝不用首个窗口的
         * configure 尺寸（MessageBox 会把它设成 373x212）。 */
        prc->left = prc->top = 0;
        prc->right = m_screenW;
        prc->bottom = m_screenH;
        return true;
    }
    return false;
}

bool SConnection::GetMonitorWorkRect(HMONITOR hMonitor, RECT *prc) const
{
    return GetMonitorRect(hMonitor, prc);
}

bool SConnection::IsPrimaryMonitor(HMONITOR hMonitor) const
{
    return hMonitor == (HMONITOR)1;
}

int SConnection::GetScreenWidth(HMONITOR hMon) const
{
    RECT rc;
    return GetMonitorRect(hMon, &rc) ? (rc.right - rc.left) : m_screenW;
}

int SConnection::GetScreenHeight(HMONITOR hMon) const
{
    RECT rc;
    return GetMonitorRect(hMon, &rc) ? (rc.bottom - rc.top) : m_screenH;
}

HWND SConnection::GetScreenWindow() const
{
    return NULL;
}

void SConnection::UpdateWindowIcon(HWND, _Window *)
{
    /* Wayland 没有 X11 _NET_WM_ICON 那种按窗口下电位图的协议：合成器/taskbar
     * 只看 xdg_toplevel.app_id，再查 <app_id>.desktop 的 Icon=。这里不传 HICON
     * 位图（传了 compositor 也会忽略），只确保 app_id 由进程名/环境推导好并
     * 下发到已存在的 toplevel。应用若要指定，可在启动早期调 SwinxWlSetAppId
     * 或设 SOUI_WL_APP_ID 环境变量。见 framebuffer.cpp 的 app_id 实现。 */
    SwinxWlSetAppId(NULL);
}

uint32_t SConnection::GetVisualID(BOOL) const
{
    return 0;
}

uint32_t SConnection::GetCmap() const
{
    return 0;
}

void SConnection::SetZOrder(HWND hWnd, _Window *, HWND hWndInsertAfter)
{
    // m_zTop 是唯一记录"谁盖着谁"的地方（合成器按它底→顶展平），所以 z 序
    // 操作必须同步到这里。这个端口没有窗口管理器，默认序就是创建序：
    // HWND_TOP / HWND_TOPMOST（以及 NULL，等价于 HWND_TOP）挪到末尾，HWND_BOTTOM
    // 挪到开头，指定窗口则插到它后面。
    //
    // 只动顶层窗口：WS_CHILD 的子窗口不在 m_zTop 里（见 m_children），合成时
    // 永远紧跟在父窗口之后，所以提升一个窗口会连着它自己的子控件一起上来
    // ——这正是 Win32 的语义：子窗口的 z 序相对父客户区独立，不可能被父窗口盖住。
    std::vector<HWND>::iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it == m_zTop.end())
        return;
    // HWND_NOTOPMOST：本端口没有 topmost 带，所有窗口同属一带，"取消置顶"就没
    // 有可落脚的位置 —— 按 no-op 处理。不能落到下面的"指定窗口"分支，那会把
    // (HWND)-2 当成句柄去查，查不到就 push_back，反而把窗口提到了最上面。
    if (hWndInsertAfter == HWND_NOTOPMOST)
        return;
    // 已经在最顶层且目标也是最顶层：不必重排，省一次整屏重贴
    if (it + 1 == m_zTop.end()
        && (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == NULL))
        return;

    m_zTop.erase(it);
    if (hWndInsertAfter == HWND_BOTTOM)
    {
        m_zTop.insert(m_zTop.begin(), hWnd);
    }
    else if (hWndInsertAfter == HWND_TOP || hWndInsertAfter == HWND_TOPMOST || hWndInsertAfter == NULL)
    {
        m_zTop.push_back(hWnd);
    }
    else
    {
        std::vector<HWND>::iterator pos = std::find(m_zTop.begin(), m_zTop.end(), hWndInsertAfter);
        if (pos == m_zTop.end())
            m_zTop.push_back(hWnd);
        else
            m_zTop.insert(pos + 1, hWnd);
    }

    // 遮挡关系变了：屏幕上已有的像素是按旧 z 序叠出来的，必须按新顺序重贴。
    // 影响面不止 hWnd 自己的矩形（提升一个窗口会同时盖住一片、降低一个窗口会
    // 曝露一片），而整屏重贴只是同一帧内的内存拷贝，代价可控，
    // 所以这里直接置整屏脏，交给下一次 flush 统一按新 z 序合成。
    m_bScreenDirty = true;
}

void SConnection::OnStyleChanged(HWND, _Window *wndObj, DWORD, DWORD)
{
    /* CSD 装饰窗的按钮可见性取自内容窗当前 dwStyle。SOUI 的
     * SHostWnd::InitFromXml 是在窗口创建之后用 ModifyStyle 追加
     * WS_MINIMIZEBOX/WS_MAXIMIZEBOX 的，所以必须在样式落地时刷新按钮，
     * 否则这些窗口会一直只显示关闭按钮。 */
    if (wndObj && wndObj->plat.frame)
        SwinxWlFrameOnContentStyleChanged(wndObj->plat.frame);
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

cairo_surface_t *SConnection::CreateWindowSurface(HWND, int cx, int cy)
{
    // software renderer: every window paints into its own ARGB32 image
    // surface; commitCanvas() blits dirty regions into the framebuffer
    // (framebuffer.cpp), which wl_shm then attaches to the xdg_toplevel.
    if (cx < 1)
        cx = 1;
    if (cy < 1)
        cy = 1;
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cx, cy);
}

cairo_surface_t *SConnection::ResizeSurface(cairo_surface_t *surface, HWND hWnd, int cx, int cy)
{
    if (surface)
        cairo_surface_destroy(surface);
    return CreateWindowSurface(hWnd, cx, cy);
}

DWORD SConnection::GetWndProcessId(HWND)
{
    return 1;
}

HWND SConnection::WindowFromPoint(POINT pt)
{
    // 顶层窗口命中测试，z 序从顶到底取第一个命中的可见窗口。弹出窗
    // （SDropDownWnd，WS_POPUP，parent=宿主窗）比主窗后建、在 m_zTop 末尾，
    // 所以盖住主窗的部分先命中弹出窗——Win32 同名 API 的语义。
    // 命中深度只到顶层：SOUI 的控件树在窗口内部，由它自己的 SwndFromPoint 走。
    // 矩形用 screenRect() 取（顶层窗口的 rc 本来就是屏幕坐标，这里只是统一口径）。
    for (int i = (int)m_zTop.size() - 1; i >= 0; --i)
    {
        WndObj wndObj = WndMgr::fromHwnd(m_zTop[i]);
        if (!wndObj || wndObj->bDestroyed)
            continue;
        if ((wndObj->dwStyle & WS_VISIBLE) == 0)
            continue;
        RECT rc;
        if (!screenRect(m_zTop[i], &rc))
            continue;
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

void SConnection::SendSysCommand(HWND hWnd, int nCmd)
{
    /* ShowWindow(SW_MAXIMIZE/...) -> WM_SYSCOMMAND -> 这里。xdg_toplevel 的
     * 状态只能由合成器确认：请求后它会在 configure 的 states 数组里回告，
     * framebuffer 已经把回告缓存并转成 SWINX_WL_EVT_TOPLEVEL_STATE。 */
    WndObj w = WndMgr::fromHwnd(hWnd);
    HWND hSurfaceWnd = (w && w->plat.frame && IsWindow(w->plat.frame)) ? w->plat.frame : hWnd;
    SwinxWlSurface *surf = SwinxWlSurfaceGet(hSurfaceWnd);
    if (!surf)
        return;
    unsigned int st = 0;
    switch (nCmd)
    {
    case SC_MAXIMIZE:   st = SWINX_WL_TOPSTATE_MAXIMIZED;  break;
    case SC_MINIMIZE:   st = SWINX_WL_TOPSTATE_MINIMIZED;  break;
    case SC_FULLSCREEN: st = SWINX_WL_TOPSTATE_FULLSCREEN; break;
    case SC_RESTORE:    st = 0;                            break;
    default: return;
    }
    SwinxWlSurfaceSetState(surf, st);
}

BOOL SConnection::IsWindowVisible(HWND hWnd)
{
    // 与 X11 端口 (src/platform/linux/SConnection.cpp:4118) 对齐：只看窗口自身
    // 的 WS_VISIBLE，不遍历祖先链。
    //
    // 这里按 Win32 语义逐祖先检查曾是"更正确"的写法，但与 wlSyncWindowSurface()
    // 组合后是灾难：demo 用 Create(GetActiveWindow(), ...) 建主窗，MessageBox
    // 销毁后那个 owner 已不存在/不可见，于是即使主窗自己 ShowWindow(SW_SHOW) 了，
    // IsWindowVisible() 仍返回 FALSE -> SwinxWlSurfaceSetVisible(wl,0) ->
    // wl_surface_attach(NULL) 反复 unmap，surface 在任务列表里只剩个空壳。
    // X11 下同样的调用链没问题，正是因为 X11 这里只查自身；Wayland 没有 X server
    // 的级联 map 语义，更不能额外加严。子窗显隐由 subsurface 随父 surface 自动
    // 决定，不需要在这里替合成器做判断。
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
        return (wndObj->dwStyle & WS_VISIBLE) != 0;
    return FALSE;
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
 * Sibling order is by HWND value, not by z-order: this port's z-order
 * (m_zTop / m_children) exists only for compositing, and the callers that
 * matter here only need "iterate all of them
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
        all.push_back({hwnd, wnd->parent, wnd->owner, wnd->plat.frameContent, wnd->bDestroyed});
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
        /* 装饰窗是平台内部对象（等价 X11 的 WM frame，不是应用的窗口）：
         * 从兄弟/子窗口遍历里排除，避免应用的 EnumWindows 类逻辑看到它。 */
        if (info.frameContent && info.hwnd != hWnd)
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

BOOL SConnection::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
#ifdef SWINX_HAVE_SNI
    if (!m_trayMgr)
        m_trayMgr = new STrayIconMgr(this);
    return m_trayMgr->NotifyIcon(dwMessage, lpData);
#else
    (void)dwMessage;
    (void)lpData;
    return FALSE;
#endif
}

BOOL SConnection::SetTrayMenuProvider(HWND hWnd, UINT uID, const TRAYMENUPROVIDER *pProvider)
{
#ifdef SWINX_HAVE_SNI
    if (!m_trayMgr)
        m_trayMgr = new STrayIconMgr(this);
    return m_trayMgr->SetMenuProvider(hWnd, uID, pProvider);
#else
    (void)hWnd;
    (void)uID;
    (void)pProvider;
    return FALSE;
#endif
}

BOOL SConnection::UpdateTrayMenu(HWND hWnd, UINT uID)
{
#ifdef SWINX_HAVE_SNI
    if (!m_trayMgr)
        return FALSE;
    return m_trayMgr->UpdateMenu(hWnd, uID);
#else
    (void)hWnd;
    (void)uID;
    return FALSE;
#endif
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
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
    {
        if (wlDiag())
            fprintf(stderr, "[wl] commit-canvas DROP hwnd=%p no-image-surface\n", hWnd);
        return;
    }
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
        if (wlDiag())
            fprintf(stderr, "[wl] commit-canvas DROP hwnd=%p bad-surface\n", hWnd);
        return;
    }

    int sw = cairo_image_surface_get_width(surface);
    int sh = cairo_image_surface_get_height(surface);

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
    {
        if (wlDiag())
            fprintf(stderr, "[wl] commit-canvas DROP hwnd=%p empty-clip\n", hWnd);
        return;
    }

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
        {
            if (wlDiag())
                fprintf(stderr, "[wl] commit-canvas DROP hwnd=%p upd=(%d,%d,%d,%d) clipped-empty\n",
                        hWnd, rcUpd.left, rcUpd.top, rcUpd.right, rcUpd.bottom);
            return;
        }
    }

    // queue (window-local): merge into whatever this window already owes the
    // screen.  Keeping only local rects is what makes the merge safe — a
    // screen-space pair would lose the src/fb size correspondence as soon as
    // two commits get clipped against the framebuffer edge differently.
    // GTK 模型下每块 SHM buffer 自己还记着"自上次同步以来的脏区"，所以这里
    // 直接取并集即可：包围盒多出来的那点拷贝由每窗的 damage 累加器吸收，
    // 不再需要软件合成器那套"不相交就先上屏"的记账。
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
    it->second.rcSrc = rcMerge;
}

// cairo CAIRO_FORMAT_ARGB32 在小端内存里布局为 BGRA（预乘 ARGB），Wayland 的
// WL_SHM_FORMAT_ARGB8888 也是同一布局（预乘 ARGB，内存 BGRA），所以这里只需
// 按行搬运，不做通道交换。曾经的 wasm 端口需要 BGRA→RGBA 换序，那是浏览器
// ImageData 的要求，与本后端无关。
static inline void SwinxCopyRow(uint8_t *dst, const uint8_t *src, int pixels)
{
    memcpy(dst, src, (size_t)pixels * 4);
}

/* ===========================================================================
 *  合成器（多顶层窗口 + 子窗口）
 * ===========================================================================
 * 这个端口没有合成器：谁最后把像素写进显示面，那块就归谁。于是"z 序"只能是
 * SConnection 自己维护的一张表，而且**每一次上屏都得按同一张表贴**。三条规则：
 *
 *   - m_zTop 只存顶层窗口，底→顶 = 创建序（SetZOrder 重排的就是它）；
 *   - m_children 存 WS_CHILD 子窗口，合成时紧跟在父窗口之后——整组随父移动；
 *   - 屏幕矩形一律走 screenRect()，子窗口的 rc 只是父客户区坐标。
 *
 * 上屏只有两种时机：整屏重组（z 序/位置/显隐真的变了）与单窗补贴（某个窗口
 * 自己重画了）。后者必须先贴它自己、再把压在它上面的窗口按交集贴回来，否则
 * 下层窗口的一次 WM_PAINT 就会在下拉列表上烧出一个洞。
 */

BOOL SConnection::screenRect(HWND hWnd, RECT *prc) const
{
    if (!prc)
        return FALSE;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return FALSE;
    RECT rc = wndObj->rc;
    // 只有 WS_CHILD 的 rc 是父客户区坐标，要一层层加祖先原点；WS_POPUP
    // （SDropDownWnd 用 CalcPopupRect 算好屏幕矩形再建窗）和普通顶层窗口的 rc
    // 已经是屏幕坐标，再加偏移就把它们推出屏幕。深度上限只为防环。
    HWND hCur = hWnd;
    for (int depth = 0; depth < 64; ++depth)
    {
        WndObj cur = WndMgr::fromHwnd(hCur);
        if (!cur || (cur->dwStyle & WS_CHILD) == 0)
            break;
        HWND hParent = cur->parent;
        if (!hParent || hParent == hCur)
            break;
        WndObj parent = WndMgr::fromHwnd(hParent);
        if (!parent)
            break;
        OffsetRect(&rc, parent->rc.left, parent->rc.top);
        hCur = hParent;
    }
    *prc = rc;
    return TRUE;
}

void SConnection::buildComposeOrder(std::vector<HWND> &order) const
{
    order.clear();
    // 深度优先展平：父在前、子在后（= Win32 "子窗口盖在父客户区上"），兄弟按
    // 创建序。显式栈而不是递归，窗口链再深也不会把消息线程的栈压穿。
    // HWND_MESSAGE 窗口（SNotifyReceiver / msgWnd）从不绘制，OnWindowCreate
    // 已给它们建 surface；这里的懒建路径必须跳过，否则合成器任务列表会多出
    // 一批标题 "soui"、没有缩略图的幽灵窗口。
    std::vector<HWND> stack;
    stack.reserve(m_zTop.size() * 2);
    for (size_t i = m_zTop.size(); i-- > 0;)
    {
        HWND h = m_zTop[i];
        if (m_messageOnly.count(h))
            continue;
        stack.push_back(h);
    }
    while (!stack.empty())
    {
        HWND h = stack.back();
        stack.pop_back();
        order.push_back(h);
        std::map<HWND, std::vector<HWND>>::const_iterator it = m_children.find(h);
        if (it == m_children.end())
            continue;
        for (size_t i = it->second.size(); i-- > 0;)
        {
            HWND c = it->second[i];
            if (!m_messageOnly.count(c))
                stack.push_back(c);
        }
    }
}

/* xdg_popup 的定位：SOUI 用 SetWindowPos 算好的是**屏幕坐标**，而
 * xdg_positioner 的 anchor_rect 必须是父 xdg_surface 的局部坐标。必须先拿到
 * framebuffer 实际选中的 popup 父（subsurface 会上溯到 toplevel），再用它的
 * 矩形做减法；两边选错父会让 anchor 偏掉，表现为 tooltip 不显示/位置不对。
 * 对齐 GTK create_simple_positioner（anchor=TOP_LEFT/gravity=BOTTOM_RIGHT）。 */
static void syncPopupAnchor(SConnection *conn, HWND hWnd, SwinxWlSurface *wl,
                            int sw, int sh)
{
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || !wl)
        return;
    /* 装饰窗自己不带 Win32 父子关系，从被装饰的内容窗取：对话框的 owner 就在
     * 内容窗的 parent/owner 上，positioner 的锚点也是相对它算的。 */
    HWND owner = wndObj->plat.frameContent;
    HWND parent = wndObj->parent;
    if (!parent && owner)
    {
        WndObj cw = WndMgr::fromHwnd(owner);
        if (cw)
            parent = cw->parent ? cw->parent : cw->owner;
    }
    if (!parent)
        parent = conn->GetActiveWnd();
    if (!parent || parent == hWnd || !::IsWindow(parent))
    {
        parent = 0;
        std::vector<HWND> tops = conn->shellZOrder();
        for (std::vector<HWND>::const_iterator it = tops.begin();
             it != tops.end(); ++it)
        {
            if (*it != hWnd) { parent = *it; break; }
        }
    }
    /* 关键：anchor_rect 的坐标系是 framebuffer 最终选中的那个 xdg_surface，
     * 不一定是 SOUI 传进来的 parent。tooltip 建窗时 GetActiveWnd() 可能是
     * MessageBox 的 BUTTON 子窗（subsurface），若拿它的 rc 做原点，算出的
     * anchor 会整体偏移一个子窗原点，表现为 tooltip 跑到别处甚至被约束掉。
     * framebuffer 的 find_popup_parent 会沿 subsurface 链上溯到最近的
     * xdg_surface，这里先问它要真实父窗，再用同一个父窗算 anchor。 */
    void *actual = SwinxWlSurfaceGetPopupParentHwnd(wl);
    if (actual && actual != hWnd && ::IsWindow((HWND)actual))
        parent = (HWND)actual;
    else
    {
        for (int depth = 0; parent && parent != hWnd && depth < 16; ++depth)
        {
            WndObj pw = WndMgr::fromHwnd(parent);
            if (!pw || !(pw->dwStyle & WS_CHILD) || !pw->parent)
                break;
            parent = pw->parent;
        }
    }

    RECT rcWin, rcParent = { 0, 0, 0, 0 };
    if (!conn->screenRect(hWnd, &rcWin))
        return;
    int ax = rcWin.left, ay = rcWin.top;
    if (parent)
    {
        if (conn->screenRect(parent, &rcParent))
        {
            ax -= rcParent.left;
            ay -= rcParent.top;
        }
        /* 父 surface 是**装饰窗**时，内容窗在 frame surface 里位于 (0, titleH)，
         * 而 app 的坐标是相对内容窗左上角的 ⇒ y 要补一条标题栏，否则每个弹窗
         * 都比预期高 30px。装饰窗自己是 xdg_popup（对话框）时两边都是绝对
         * 坐标，不能补。 */
        SwinxWlSurface *pws = SwinxWlSurfaceGet(parent);
        WndObj pw = WndMgr::fromHwnd(parent);
        if (pws && pw && pw->plat.frameContent &&
            SwinxWlSurfaceGetRole(pws) == SWINX_WL_SURF_TOPLEVEL)
            ay += SwinxWlFrameTitleHeightFor(pw->plat.frameContent);
    }
    SwinxWlSurfaceSetPopAnchor(wl, parent, ax, ay, 1, 1, ax, ay, sw, sh);
}

/* 把一块窗口画布交给它自己的 wl_surface（GTK 模型：合成器负责重叠，平台
 * 不再把多个窗口软件合成到同一块帧缓冲）。surface 不存在时按窗口当前矩形
 * 懒创建——窗口在 OnWindowCreate 时画布还没建立，这里以 cairo surface 的
 * 实际尺寸为准，避免出现"surface 尺寸和画布尺寸不一致"的残缺画面。 */
static SwinxWlSurface *wlSyncWindowSurface(SConnection *conn, HWND hWnd,
                                            uint32_t **bits, int *stride)
{
    *bits = NULL;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return NULL;
    cairo_surface_t *surface = (cairo_surface_t *)GetGdiObjPtr(wndObj->bmp);
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE ||
        cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return NULL;
    cairo_surface_flush(surface);
    *bits = (uint32_t *)cairo_image_surface_get_data(surface);
    if (!*bits)
        return NULL;
    int sw = cairo_image_surface_get_width(surface);
    int sh = cairo_image_surface_get_height(surface);
    *stride = cairo_image_surface_get_stride(surface) / 4;

    bool isToolWindow = (wndObj->dwExStyle & WS_EX_TOOLWINDOW) && !(wndObj->dwStyle & WS_CHILD);
    SwinxWlSurface *wl = SwinxWlSurfaceGet(hWnd);
    /* 建窗时父窗口的 xdg_surface 还没就绪、被推迟的"对话框转 popup"，
     * 在这里补做。转换会重建 wl_surface，所以必须发生在后续同步之前；
     * 失败（父级仍未就绪）就这次先不上屏，等下一次。 */
    if (!conn->tryPendingDialogParent(hWnd))
        return NULL;
    wl = SwinxWlSurfaceGet(hWnd);
    if (!wl)
    {
        int role = (wndObj->dwStyle & WS_CHILD) ? SWINX_WL_SURF_SUBSURFACE
                 : (wndObj->plat.frame ? SWINX_WL_SURF_SUBSURFACE
                    : (isToolWindow ? SWINX_WL_SURF_POPUP : SWINX_WL_SURF_TOPLEVEL));
        HWND parentHwnd = wndObj->parent ? wndObj->parent
                        : (isToolWindow && conn->GetActiveWnd() ? conn->GetActiveWnd() : wndObj->owner);
        if (wndObj->plat.frame)
            parentHwnd = wndObj->plat.frame;
        wl = SwinxWlSurfaceCreate(hWnd, sw, sh, role, parentHwnd);
    }
    if (!wl)
        return NULL;

    // 位置：subsurface 的原点是父窗口原点；toplevel 由合成器摆放，x/y 忽略。
    // 被装饰窗包着的内容窗：坐标系是装饰窗，内容贴在标题栏下面（(0, titleH)），
    // 与 SOUI 的屏幕坐标无关（那份坐标只用来摆装饰窗）。
    int x = 0, y = 0;
    if (wndObj->plat.frame)
    {
        y = SwinxWlFrameTitleHeightFor(hWnd);
    }
    else if (wndObj->parent)
    {
        RECT rcWin, rcParent;
        if (conn->screenRect(hWnd, &rcWin) && conn->screenRect(wndObj->parent, &rcParent))
        {
            x = rcWin.left - rcParent.left;
            y = rcWin.top - rcParent.top;
        }
    }
    SwinxWlSurfaceSetGeometry(wl, x, y, sw, sh);

    BOOL nowVisible = conn->IsWindowVisible(hWnd);
    if (nowVisible && wndObj->plat.frame && sw >= 8 && sh >= 8)
    {
        /* 装饰窗的 map 时机：内容窗真正有像素（尺寸也够了）才 map，合成器才会
         * 按最终尺寸给它摆位。SetWindowVisible() 里那次可能因为窗口刚建、尺寸
         * 还是 0x0 被推迟，这里在第一次带真实尺寸的 present 补上。 */
        WndObj fw = WndMgr::fromHwnd(wndObj->plat.frame);
        if (fw && !(fw->dwStyle & WS_VISIBLE))
            ShowWindow(wndObj->plat.frame, SW_SHOW);
    }
    if (nowVisible && SwinxWlSurfaceGetRole(wl) == SWINX_WL_SURF_POPUP)
        syncPopupAnchor(conn, hWnd, wl, sw, sh);
    SwinxWlSurfaceSetVisible(wl, nowVisible);
    return wl;
}

static void wlPresentWindow(SConnection *conn, HWND hWnd, const RECT *dirty)
{
    uint32_t *bits = NULL;
    int stride = 0;
    SwinxWlSurface *wl = wlSyncWindowSurface(conn, hWnd, &bits, &stride);
    if (wl && bits)
        SwinxWlSurfacePresent(wl, bits, stride, dirty);
}

void SConnection::flushPendingCommits()
{
    if (m_pendingCommits.empty() && !m_bScreenDirty)
        return;
    std::map<HWND, PendingCommit> pending;
    pending.swap(m_pendingCommits);
    bool screenDirty = m_bScreenDirty;
    m_bScreenDirty = false;

    if (screenDirty)
    {
        // z 序/位置/显隐变了：只同步每块 surface 的几何与显隐（subsurface 的
        // 位置、尺寸变化由合成器立即生效），不要重贴画布——没有新像素可贴，
        // 重贴只会白白整幅 memcpy。后续各窗自己的 WM_PAINT 照常带脏区提交。
        std::vector<HWND> order;
        buildComposeOrder(order);
        for (size_t i = 0; i < order.size(); ++i)
        {
            uint32_t *bits = NULL;
            int stride = 0;
            wlSyncWindowSurface(this, order[i], &bits, &stride);
        }
    }
    for (std::map<HWND, PendingCommit>::iterator it = pending.begin(); it != pending.end(); ++it)
        wlPresentWindow(this, it->first, &it->second.rcSrc);

    // 帧末统一 flush：内部每窗一个 frame callback，一帧只 attach/commit 一次。
    SwinxWlSurfacesFlushFrame();
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
    /* Wayland 没有 XFixes HideCursor：隐藏/恢复靠 wl_pointer_set_cursor(NULL)
     * 交还合成器。第一次从显示变隐藏时发 NULL，恢复时把缓存的光标重设。 */
    if (m_cursorCount <= 0)
    {
        m_cursorCount = 0;
        SwinxWlShowCursor(0);
    }
    else
    {
        SwinxWlShowCursor(1); // 必须先清 hidden，否则换图案也出不来
        if (m_cursorCount == 1)
            applyWlCursor(this, m_hMouseFocus ? m_hMouseFocus : GetActiveWnd());
    }
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
    /* 输入法候选窗/预编辑的定位点；text-input-v3 要的是 surface 局部坐标。 */
    SwinxWlImeSetCursorRect(X, Y, 1,
                            m_caretInfo.nHeight > 0 ? m_caretInfo.nHeight : 16);
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



HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth)
{
    // virtual windows: swinx creates the _Window object itself; the
    // connection only tracks it.  See mobile's ENABLE_VIRTUAL_HWND path.
    (void)depth;
    if (!wnd)
        return NULL;
    /* 应用可能拿一个已经销毁的句柄当父窗/属主（典型来源：GetActiveWindow()
     * 返回了刚关掉的启动 MessageBox）。wnd.cpp 的 WIN_CreateWindowEx 对 hParent
     * 不做存活校验，直接写进 parent/owner，于是这个"幽灵父窗"会一路影响
     * 后面的判断：wlSyncWindowSurface 以为要走 subsurface 坐标换算、
     * normalizeWlToplevelOrigin 以为这是有主的对话框。平台层先自己清干净，
     * 让 Win32 语义回到"没有父窗"。HWND_MESSAGE 不是真窗口，必须排除。 */
    if (wnd->parent && wnd->parent != HWND_MESSAGE && !::IsWindow(wnd->parent))
        wnd->parent = 0;
    if (wnd->owner && !::IsWindow(wnd->owner))
        wnd->owner = 0;
    // 每窗口一块 wl_surface（GTK 模型）：不再把首个窗口拉伸到整个虚拟屏幕，
    // 也不再把多个顶层窗软件合成到同一块帧缓冲。画布尺寸就是 CREATESTRUCT
    // 的 cx/cy，wnd.cpp 随后以同尺寸建 cairo surface；这里提前建好 surface，
    // 让首次 WM_PAINT 就能直接上屏。
    // HWND_MESSAGE 窗口（SNotifyReceiver 等）只是消息泵，从来没有像素：给它建
    // xdg_toplevel 只会在合成器的活动窗口列表里多出一个空白"主窗口"。X11 下
    // 这类窗口也不会 map，Wayland 必须显式跳过。
    bool isMessageOnly = cs && cs->hwndParent == HWND_MESSAGE;
    if (isMessageOnly)
        m_messageOnly.insert((HWND)wnd);
    /* 客户端装饰（CSD）：合成器不给标题栏（xdg-decoration 的 SERVER_SIDE 在
     * GNOME 上会被强制回 CLIENT_SIDE），带 WS_CAPTION 的窗口由平台补一个外层
     * 装饰窗——等价 X11 窗口管理器的 reparent：frame 是真正的 xdg_toplevel /
     * xdg_popup，内容窗的 wl_surface 变成它的 subsurface，定位于 (0, titleH)。
     * 内容窗的 dwStyle / parent / rc 一概不动，SOUI 看到的窗口语义与 X11 一致。
     * 判断用 cs->style（原始样式）：wnd.cpp 已经把 WS_CAPTION 里的 WS_BORDER
     * 位清掉了，用 wnd->dwStyle 会漏。 */
    if (!isMessageOnly && !wnd->plat.frame && cs &&
        SwinxWlFrameShouldDecorate(cs->style, wnd->dwExStyle, cs->hwndParent))
    {
        wnd->plat.frame = SwinxWlFrameCreate((HWND)wnd,
                                        cs->lpszName ? cs->lpszName : "",
                                        wnd->rc.left, wnd->rc.top,
                                        wnd->rc.right - wnd->rc.left,
                                        wnd->rc.bottom - wnd->rc.top);
    }
    /* GTK gdk_wayland_window_map 三分支：WS_CHILD → subsurface；
     * WS_EX_TOOLWINDOW（SOUI tooltip / 菜单）→ xdg_popup（天然不进任务列表，
     * 位置由 positioner 决定）；其余 → xdg_toplevel。
     * 不能像上一版那样把 tooltip 当 toplevel + transient parent：GNOME 仍会把
     * transient toplevel 列进活动窗口，而且 toplevel 的 x/y 被合成器忽略，
     * SOUI 用 SetWindowPos 算好的屏幕坐标就完全失效。
     * 带装饰窗的窗口同理走 subsurface：它的 frame 才是那个 toplevel/popup。 */
    bool isToolWindow = (wnd->dwExStyle & WS_EX_TOOLWINDOW) && !(wnd->dwStyle & WS_CHILD);
    int role = (wnd->dwStyle & WS_CHILD) ? SWINX_WL_SURF_SUBSURFACE
             : (wnd->plat.frame ? SWINX_WL_SURF_SUBSURFACE
                : (isToolWindow ? SWINX_WL_SURF_POPUP : SWINX_WL_SURF_TOPLEVEL));
    /* popup 的父 xdg_surface 用 owner（SOUI tooltip 的 hwndParent=NULL），
     * 没有时用当前活动窗；framebuffer 里还有"第一个 toplevel"兜底。
     * 关键：m_hActive 是裸 HWND（实为 _Window*），窗口销毁后地址会被新窗复用，
     * 日志实证 tooltip 建窗时 GetActiveWnd() 恰好等于新窗自己（parent=(self)）。
     * 把 self 传给 find_popup_parent 会让它退到"第一个 toplevel"，而那个可能
     * 是刚销毁的 msgbox 残影；这里直接排除 self，交给 framebuffer 选活着的
     * toplevel（find_popup_parent 会跳过已 unmap 的 surface）。 */
    HWND parentHwnd = wnd->parent ? wnd->parent
                    : (isToolWindow && GetActiveWnd() ? GetActiveWnd() : wnd->owner);
    if (parentHwnd == (HWND)wnd || (parentHwnd && !IsWindow(parentHwnd)))
        parentHwnd = 0;
    void *surfaceParent = (void *)(wnd->plat.frame ? wnd->plat.frame : parentHwnd);
    SwinxWlSurface *wl = isMessageOnly ? NULL
        : SwinxWlSurfaceCreate((HWND)wnd, cs ? (int)cs->cx : 0,
                               cs ? (int)cs->cy : 0,
                               role, surfaceParent);
    /* WM_NCCREATE 时 surface 还没建，OnSetWindowText 里的 SetTitle 落空；
     * 这里用已经存好的 wnd->title 补上（只对 toplevel 有效）。 */
    if (wl && !wnd->title.empty())
        SwinxWlSurfaceSetTitle(wl, wnd->title.c_str());
    /* tooltip（TTS_ALWAYSTIP）要输入穿透：STipCtrl 的 RelayEvent 是消息过滤器，
     * 处理队列里所有窗口的 WM_MOUSEMOVE；若鼠标移到 tooltip 表面使其获得 pointer
     * focus，tooltip 会收到以自身为目标的 WM_MOUSEMOVE（坐标相对 tooltip），
     * 判定不在 m_rcTarget 内就立刻隐藏——表现为刚弹出就消失。菜单（同为
     * xdg_popup 但无 TTS 位）绝不能穿透，否则 hover/点击全落到下层窗口。
     * X11 后端靠 XFixes + WS_EX_TRANSPARENT，Wayland 没有等价层级，改用样式位。 */
    if (wl && role == SWINX_WL_SURF_POPUP &&
        cs && (cs->style & (TTS_ALWAYSTIP | TTS_NOPREFIX)))
        SwinxWlSurfaceSetInputTransparent(wl, 1);
    /* 直接以 owner 建出来的对话框（SHostDialog 走 CreateWindowEx(hParent=owner)，
     * 不走 ::SetParent）：它的 xdg 对象在装饰窗上，而装饰窗此刻还是平级的
     * xdg_toplevel —— 合成器会把对话框摆到工作区原点（左上角），SOUI 用
     * CenterWindow 算好的坐标一个像素都不生效。创建期就转成 xdg_popup，
     * 位置交给 positioner（anchor 由随后的 present 里的 syncPopupAnchor 给出）。
     * 判断用 cs->style：wnd->dwStyle 已被 wnd.cpp 去掉 WS_BORDER，位不全。 */
    if (wl && !(wnd->dwStyle & WS_CHILD) && (wnd->dwStyle & WS_POPUP) &&
        cs && (cs->style & WS_CAPTION) == WS_CAPTION && cs->hwndParent &&
        cs->hwndParent != HWND_MESSAGE)
    {
        HWND hDialog = wnd->plat.frame ? wnd->plat.frame : (HWND)wnd;
        SwinxWlSurface *dw = SwinxWlSurfaceGet(hDialog);
        if (dw && SwinxWlSurfaceGetRole(dw) == SWINX_WL_SURF_TOPLEVEL &&
            !SwinxWlSurfaceSetDialogParent(dw, cs->hwndParent))
            m_pendingDialogParent[hDialog] = cs->hwndParent;
    }
    /* WS_VISIBLE 建窗不会再有单独的 ShowWindow：surface 创建期不 map（等最终
     * 尺寸），这里必须显式补一次 map，否则这类窗口永远停在空壳。 */
    if (wl && cs && (cs->style & WS_VISIBLE))
        SwinxWlSurfaceSetVisible(wl, 1);
    // 合成顺序 = 创建序：主窗先建在底，弹出窗（SDropDownWnd 等）后建在顶。
    // WS_CHILD 子窗口不进 m_zTop —— 它的 rc 是父客户区坐标，而且提升父窗口
    // （ShowWindow → SetWindowPos(HWND_TOP)）时若把父挪到它自己子窗口后面，
    // 父窗口就会盖住自己的子控件。子窗口单独按父内创建序存，合成时紧跟父窗口。
    // 没有父窗口的 WS_CHILD（畸形输入）退回顶层表，否则它会被存进一个永远走不到
    // 的 m_children[0] 里、既不显示也不释放。
    // 被装饰窗包着的窗口同理挂在装饰窗下面：它的 surface 是装饰窗 surface 的
    // subsurface，几何/显隐都跟着装饰窗走。
    // 同一线程的连接只会有一个 UI 线程在跑，这里不需要额外加锁。
    if (wnd->plat.frame)
        m_children[wnd->plat.frame].push_back((HWND)wnd);
    else if ((wnd->dwStyle & WS_CHILD) && wnd->parent)
        m_children[wnd->parent].push_back((HWND)wnd);
    else
        m_zTop.push_back((HWND)wnd);
    return (HWND)wnd;
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *wnd)
{
    /* 活动窗/前台窗/焦点都是裸 HWND（实为 _Window*），窗口销毁后这块内存会被
     * 新窗口复用。不清掉的话 GetActiveWindow() 会把一个悬垂句柄交给应用：
     * demo 就是 `dlgMain.Create(GetActiveWindow(), ...)`，于是主窗的 owner 变成
     * 已经销毁的启动 MessageBox，SetParent 拿它当父级、normalize 把它当"有
     * owner 的对话框"，一连串语义都会错（命中测试错位就是其中最直观的一个）。
     * 这几个字段只用于"谁是当前窗口"的判断，销毁即失效，清 0 是唯一正确值。 */
    if (m_hActive == hWnd)
        m_hActive = 0;
    if (m_hForeground == hWnd)
        m_hForeground = 0;
    if (m_hFocus == hWnd)
        m_hFocus = 0;
    if (m_hMouseFocus == hWnd)
        m_hMouseFocus = 0;
    // Win32 语义：销毁 capture 窗口时自动释放 capture（弹出下拉窗销毁后
    // 不留悬空 capture，否则鼠标路由永远指向已死的窗口）。capture 是
    // SConnection 的状态，清理归平台层，核心 DestroyWindow 不掺和。
    if (m_hWndCapture == hWnd)
        ReleaseCapture();
    /* 待转 popup 的登记表：别留下悬空 HWND（地址会被新窗复用）。既清掉
     * "自己待转"的那条，也清掉"自己的 owner 待转"的其它条目。 */
    m_pendingDialogParent.erase(hWnd);
    for (std::map<HWND, HWND>::iterator dit = m_pendingDialogParent.begin();
         dit != m_pendingDialogParent.end();)
    {
        if (dit->second == hWnd)
            m_pendingDialogParent.erase(dit++);
        else
            ++dit;
    }
    /* 装饰窗先摘链接再销毁：防止 DestroyWindow(frame) 里的 WM_DESTROY 回程
     * 又走一遍这里（frame 的 frameContent 已清空）。
     *   - 内容窗先死：装饰窗没有存在意义，一起销毁；
     *   - 装饰窗先死：只清内容窗的 frame 指针，内容窗继续按普通窗口工作
     *     （退化成没有标题栏的 toplevel/subsurface，不会崩）。 */
    if (wnd)
    {
        if (wnd->plat.frame)
        {
            HWND hFrame = wnd->plat.frame;
            wnd->plat.frame = 0;
            WndObj fw = WndMgr::fromHwnd(hFrame);
            if (fw)
                fw->plat.frameContent = 0;
            SwinxWlFrameDestroy(hFrame);
        }
        if (wnd->plat.frameContent)
        {
            HWND hContent = wnd->plat.frameContent;
            wnd->plat.frameContent = 0;
            WndObj cw = WndMgr::fromHwnd(hContent);
            if (cw)
            {
                cw->plat.frame = 0;
                /* 装饰窗先走（例如合成器把整棵 surface 树一起销毁）：内容窗
                 * 退回顶层表，继续按普通窗口活着，不至于从合成顺序里消失。 */
                if (std::find(m_zTop.begin(), m_zTop.end(), hContent) == m_zTop.end())
                    m_zTop.push_back(hContent);
            }
        }
    }
    std::vector<HWND>::iterator it = std::find(m_zTop.begin(), m_zTop.end(), hWnd);
    if (it != m_zTop.end())
        m_zTop.erase(it);
    m_messageOnly.erase(hWnd);
    // 子窗口：从父窗口的子表里摘掉；它自己名下的子表也一并丢弃（父先死时，
    // 子窗口随后各自还会走到这里，重复擦除无害）。
    for (std::map<HWND, std::vector<HWND>>::iterator kids = m_children.begin(); kids != m_children.end(); ++kids)
    {
        std::vector<HWND> &v = kids->second;
        v.erase(std::remove(v.begin(), v.end(), hWnd), v.end());
    }
    m_children.erase(hWnd);
    m_committed.erase(hWnd);
    m_pendingCommits.erase(hWnd);
    SwinxWlSurfaceDestroy(hWnd);
    // 窗口销毁后合成器会把曝露区交还给下面的窗口；subsurface 消失即生效，
    // 下面窗口无需整屏重贴（它们各自的画布下一帧照常提交）。
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, BOOL bVisible, int nCmdShow)
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
        /* 活动窗口跟踪（对齐 X11 linux/SConnection.cpp:1556）：第一个被
         * 激活显示的非子窗口就是活动窗口。tooltip / 菜单建窗时靠它挑 popup
         * 父窗；没有这一步 GetActiveWnd() 永远是 0，只能退到"最新 surface"
         * 兜底，anchor 坐标系会跟着乱。 */
        if (nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA &&
            !(wndObj->dwStyle & WS_CHILD) && GetActiveWnd() == 0)
            SetActiveWindow(hWnd);
        // A window that just became visible needs one full repaint: it was
        // skipped by SendExposeEvent() while hidden.
        InvalidateRect(hWnd, NULL, TRUE);
        // X11 的 X server 在 map 父窗时会给所有子窗补发 expose；Wayland 没有
        // 这个机制。子窗（MessageBox 的 STATIC/BUTTON）在父窗 show 之前绘制时，
        // wlSyncWindowSurface() 的 IsWindowVisible() 因祖先不可见把它 unmap 了，
        // 若不再触发重绘，subsurface 会永远保持未映射。这里级联一次同步重绘。
        {
            std::vector<HWND> stack;
            std::map<HWND, std::vector<HWND>>::iterator kids = m_children.find(hWnd);
            if (kids != m_children.end())
                stack = kids->second;
            while (!stack.empty())
            {
                HWND child = stack.back();
                stack.pop_back();
                WndObj cw = WndMgr::fromHwnd(child);
                if (!cw || cw->bDestroyed || !(cw->dwStyle & WS_VISIBLE))
                    continue;
                InvalidateRect(child, NULL, TRUE);
                SendExposeEvent(child, NULL, TRUE);
                kids = m_children.find(child);
                if (kids != m_children.end())
                    stack.insert(stack.end(), kids->second.begin(), kids->second.end());
            }
        }
        // 重新挂上 surface：GTK 的 map 路径，随后首帧 WM_PAINT 上传完整画布。
        /* 带装饰窗的窗口：map 的是装饰窗（它才是 xdg_toplevel / xdg_popup），
         * 内容窗的 subsurface 随父 surface 一起出现。 */
        SwinxWlSurface *wl = SwinxWlSurfaceGet(hWnd);
        if (wl && wndObj->plat.frame)
        {
            WndObj fw = WndMgr::fromHwnd(wndObj->plat.frame);
            int cw = wndObj->rc.right - wndObj->rc.left;
            int ch = wndObj->rc.bottom - wndObj->rc.top;
            if (fw && !(fw->dwStyle & WS_VISIBLE) && cw >= 8 && ch >= 8)
            {
                /* 首次 map 上报的尺寸就是合成器"认识"这个窗口时的尺寸：
                 * xdg_toplevel 的位置在第一次 commit 时由合成器定下，之后再改
                 * 尺寸只会从那个锚点长出去。所以这条日志里的 size 必须等于
                 * 应用最终要的尺寸，否则窗口一定不在预期位置（也解释了为什么
                 * 打开 center-new-windows 仍然看着偏）。 */
                ShowWindow(wndObj->plat.frame, SW_SHOW);
            }
        }
        if (wl)
        {
            if (SwinxWlSurfaceGetRole(wl) == SWINX_WL_SURF_POPUP)
            {
                /* popup 隐藏时 xdg_popup + xdg_surface + wl_surface 都销毁了
                 * （GTK hide_surface 同款）。重新显示必须先按当前位置重建
                 * positioner，否则 surface_flush 被 !popup 闸住，tooltip 再
                 * 也上不了屏。当前画布指针仍在，先同步一帧即可。 */
                wlPresentWindow(this, hWnd, NULL);
            }
            else if (SwinxWlSurfaceGetRole(wl) == SWINX_WL_SURF_TOPLEVEL)
            {
                /* 尺寸还没定下来的窗口先不 map：SOUI 的 SHostWnd::Create() 用
                 * 0x0 建宿主窗，真实尺寸要等布局算完（随后一次 SetWindowPos）
                 * 才写进 rc。此时若按 1x1 提交首个 buffer，合成器就把窗口摆在
                 * 屏幕中间的那一个像素上，之后长大就变成"窗口不在屏幕中间"。
                 * 推迟到第一次带真实尺寸的 present：wlSyncWindowSurface() 会用
                 * 画布尺寸重新调 SetVisible(1)，那一次的 map 才是最终尺寸。 */
                int w = wndObj->rc.right - wndObj->rc.left;
                int h = wndObj->rc.bottom - wndObj->rc.top;
                if (w < 8 || h < 8)
                {
                    return;
                }
                /* 和上面 frame 分支同理：这是合成器第一次"看见"这块 toplevel
                 * 的尺寸，位置就在此刻定下。 */
                SwinxWlSurfaceSetVisible(wl, 1);
            }
            else
                SwinxWlSurfaceSetVisible(wl, 1);
        }
    }
    else
    {
        if ((wndObj->dwStyle & WS_VISIBLE) == 0)
            return; // already hidden
        wndObj->dwStyle &= ~WS_VISIBLE;
        if (wndObj->plat.frame)
            ShowWindow(wndObj->plat.frame, SW_HIDE);
        // wl_surface_attach(NULL) + commit 让合成器立即停止显示这块 surface，
        // 曝露区由合成器用下面窗口的上一帧自动补齐，无需软件重贴。
        SwinxWlSurface *wl = SwinxWlSurfaceGet(hWnd);
        if (wl)
            SwinxWlSurfaceSetVisible(wl, 0);
    }
}

void SConnection::SetParent(HWND hWnd, _Window *wnd, HWND parent)
{
    /* xdg_toplevel 的 transient parent（对齐 X11 的 WM_TRANSIENT_FOR / GTK
     * gdk_wayland_window_set_transient_for）。无 WS_CHILD 的顶层窗（MessageBox、
     * SOUI 弹出 host）必须告诉合成器自己属于谁，否则每个都是平级 xdg_toplevel，
     * 合成器任务栏会把一个应用的十几个窗口全列成独立"主窗口"。
     * subsurface 的父子已在创建时用 wl_subcompositor 固定，不受这里影响。 */
    /* tooltip（WS_EX_TOOLWINDOW）现在走 xdg_popup，没有 toplevel，
     * SwinxWlSurfaceSetTransientParent 内部会无视；popup 的从属关系在
     * SetPopAnchor 里由 xdg_surface_get_popup 固定，不经过这里。 */
    /* 已经销毁的 owner：应用可能从 GetActiveWindow() 拿到一个刚死掉的句柄。
     * 拿它当父级会让 surface 挂到不存在的对象上（transient parent 只接受活着的
     * xdg_surface），而且下面"无 parent 的 WS_POPUP 退到活动窗"的兜底也会被
     * 这个假 parent 挡掉。统一按"没有 parent"处理。 */
    if (parent && parent != HWND_MESSAGE && !::IsWindow(parent))
        parent = 0;
    if (hWnd && parent != hWnd)
    {
        /* 有装饰窗时，xdg 对象挂在装饰窗上：位置（xdg_positioner 锚点）与
         * transient 关系都属于那个 toplevel/popup，内容窗只是 subsurface。 */
        HWND hTarget = (wnd && wnd->plat.frame) ? wnd->plat.frame : hWnd;
        SwinxWlSurface *wl = SwinxWlSurfaceGet(hTarget);
        /* 没有显式 owner 的对话框（MessageBox(NULL, ...)、SHostDialog 不带父窗
         * 的路径）：不挂任何 xdg 父级就只能当平级的 xdg_toplevel，合成器会把它
         * 扔到工作区原点——也就是"弹出窗全在左上角"。退一步挂到当前活动窗：
         * 对话框的语义本来就是相对它的父窗摆放，positioner 才有锚点可用。 */
        if (!parent && wnd && (wnd->dwStyle & WS_CHILD) == 0 &&
            (wnd->dwStyle & WS_POPUP) && (wnd->dwStyle & WS_CAPTION))
        {
            HWND hActive = GetActiveWnd();
            if (hActive && hActive != hWnd && ::IsWindow(hActive))
                parent = hActive;
        }
        /* WS_POPUP 对话框（MessageBox 的 CreateWindow+SetParent(owner) 路径、
         * SHostDialog 的 CreateWindowEx(hParent=owner) 路径）一律转 xdg_popup：
         * xdg_toplevel 的 x/y 由合成器全权决定，客户端写 MoveWindow / CenterWindow
         * 一点用都没有，对话框就只能落在合成器给的默认位置。转成 popup 后由
         * xdg_positioner 按 SOUI 算好的坐标（相对 owner）精确落地。
         * 注意不能再用 wnd->parent == NULL 作为判据：wnd.cpp::WIN_CreateWindowEx
         * 先把 hParent 写进 pWnd->parent（第 436 行）再调本函数，SHostDialog 这类
         * 建窗即带 owner 的窗口 parent 恒非空，用旧判据会全部漏掉（换肤窗口就是
         * 这么变成平级 toplevel、被合成器摆在左上角的）。
         * 转换会销毁旧 wl_surface 并重建，必须在子窗口创建之前完成：子窗的
         * wl_subsurface 绑定的是具体 surface。SetParent 在 WM_CREATE 之前调用，
         * 满足这个前提。 */
        if (wnd && (wnd->dwStyle & WS_CHILD) == 0 && (wnd->dwStyle & WS_POPUP) &&
            wl && SwinxWlSurfaceGetRole(wl) == SWINX_WL_SURF_TOPLEVEL && parent)
        {
            /* 父窗口的 xdg_surface 可能还没建（对话框在父窗 WM_CREATE 里创建、
             * 或父窗尚未上屏）。此时不能把对话框留在平级 toplevel——合成器
             * 会把它摆到工作区原点，而且之后没人再纠正。记下来，等它自己的
             * surface 第一次同步时重试（见 wlSyncWindowSurface）。 */
            if (!SwinxWlSurfaceSetDialogParent(wl, parent))
                m_pendingDialogParent[hTarget] = parent;
        }
        else
            SwinxWlSurfaceSetTransientParent(wl, parent);
    }
}

bool SConnection::isPendingDialog(HWND hWnd) const
{
    return hWnd && m_pendingDialogParent.find(hWnd) != m_pendingDialogParent.end();
}

int SConnection::tryPendingDialogParent(HWND hWnd)
{
    std::map<HWND, HWND>::iterator it = m_pendingDialogParent.find(hWnd);
    if (it == m_pendingDialogParent.end())
        return 1;
    SwinxWlSurface *wl = SwinxWlSurfaceGet(hWnd);
    if (!wl || SwinxWlSurfaceGetRole(wl) != SWINX_WL_SURF_TOPLEVEL)
    {
        /* 已经不是平级 toplevel 了（例如它本来就被当作 popup 建出来），
         * 或者窗口已经销毁：没有什么要补的。 */
        m_pendingDialogParent.erase(it);
        return 1;
    }
    HWND hParent = it->second;
    /* 父窗没了、或是个永远不会有 surface 的消息窗、或还没显示：不能再等，
     * 让对话框按平级 toplevel 活着（位置由合成器决定），总比永远不上屏好。 */
    bool parentReady = false;
    if (hParent && !m_messageOnly.count(hParent))
    {
        WndObj pw = WndMgr::fromHwnd(hParent);
        parentReady = pw && !pw->bDestroyed && (pw->dwStyle & WS_VISIBLE);
    }
    if (!parentReady)
    {
        m_pendingDialogParent.erase(it);
        return 1;
    }
    if (!SwinxWlSurfaceSetDialogParent(wl, hParent))
        return 0; // 父窗口的 xdg_surface 还没建好，等下一次同步
    m_pendingDialogParent.erase(hWnd);
    return 1;
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
    // 只检查窗口自身的 WS_VISIBLE，与 X11 端口 src/platform/linux/SConnection.cpp
    // 的 SendExposeEvent 一致。不能用 IsWindowVisible()：它会向上检查所有祖先，
    // 而 swinx 原生 MessageBox（src/cmnctl32/msgbox.cpp）先创建带 WS_VISIBLE 的
    // STATIC/BUTTON 子窗口、之后才 ShowWindow 父窗口——子窗口首次 InvalidateRect
    // 时父窗口尚未可见，用 IsWindowVisible() 会丢弃它的 WM_PAINT；X11 下由 X
    // server 在 map 父窗时补发 expose 所以看不出，Wayland 没有这个机制， subsurface
    // 便永远拿不到 buffer，表现为对话框只有图标没有文字/按钮。
    if ((wndObj->dwStyle & WS_VISIBLE) == 0)
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

void SConnection::SetWindowMsgTransparent(HWND hWnd, _Window *, BOOL bTransparent)
{
    /* WS_EX_TRANSPARENT / tooltip：用空 wl_region 让指针事件落到下层窗口。
     * 不实现的话 SOUI 的 STipCtrl 会收到自己窗口上的 WM_MOUSEMOVE 并立刻
     * 隐藏 tooltip（X11 下没有这层合成的窗口层级，所以只在 Wayland 暴露）。 */
    SwinxWlSurfaceSetInputTransparent(SwinxWlSurfaceGet(hWnd), bTransparent);
}

void SConnection::AssociateHIMC(HWND, _Window *, HIMC hIMC)
{
    /* SHostWnd::EnableIME 在 HIMC 变化时调用这里；同步 text-input 的启用状态。 */
    SwinxWlImeSetEnabled(hIMC ? 1 : 0);
}

/* evdev/XKB scancode -> Win32 VK。Wayland 的 wl_keyboard.key 给的是 evdev
 * keycode，与 X11 的 KeyCode 不同，这里按 Linux input-event-codes.h 映射。
 * 只覆盖 SOUI 控件与 MessageBox 实际需要的键；未列出的键原样透传。 */
static UINT wlScancodeToVk(int code)
{
    /* Linux input-event-codes.h 的 evdev keycode 按 QWERTY 物理位置排布，
     * 不是字母表连续顺序：KEY_Q=16, KEY_W=17, KEY_E=18...KEY_A=30, KEY_S=31...
     * KEY_Z=44, KEY_X=45, KEY_C=46, KEY_V=47。旧实现用 'A'+(code-30) 推算，
     * 导致打字错位（test→usbu）、Ctrl+C/V 变成 '\' / ']' 而失效。这里改显式表。 */
    static const UINT s_vk[] = {
        0,             /* 0 */
        VK_ESCAPE,     /* 1  KEY_ESC */
        '1','2','3','4','5','6','7','8','9','0', /* 2..11 KEY_1..KEY_0 */
        VK_OEM_MINUS, VK_OEM_PLUS, VK_BACK, VK_TAB, /* 12..15 */
        'Q','W','E','R','T','Y','U','I','O','P', /* 16..25 */
        VK_OEM_4, VK_OEM_6, VK_RETURN, VK_CONTROL, /* 26..29 */
        'A','S','D','F','G','H','J','K','L',     /* 30..38 */
        VK_OEM_1, VK_OEM_7, VK_OEM_3, VK_SHIFT, VK_OEM_5, /* 39..43 */
        'Z','X','C','V','B','N','M',             /* 44..50 */
        VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2,   /* 51..53 */
        VK_SHIFT, VK_MULTIPLY, VK_MENU, VK_SPACE, VK_CAPITAL, /* 54..58 */
        VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, /* 59..66 */
        VK_F9, VK_F10,                           /* 67..68 */
        VK_NUMLOCK, VK_SCROLL,                   /* 69..70 */
        VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, VK_SUBTRACT, /* 71..74 */
        VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, VK_ADD, /* 75..78 */
        VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD0, /* 79..82 */
        VK_DECIMAL,                              /* 83 */
        0, 0, 0,                                 /* 84..86 */
        VK_F11, VK_F12,                          /* 87..88 */
        0, 0, 0, 0, 0, 0, 0, 0,                  /* 89..96 */
        VK_CONTROL, 0, 0, VK_MENU,               /* 97..100 (RCTRL/RALT) */
        0, VK_HOME, VK_UP, VK_PRIOR, VK_LEFT,    /* 101..105 */
        VK_RIGHT, VK_END, VK_DOWN, VK_NEXT,      /* 106..109 */
        VK_INSERT, VK_DELETE,                    /* 110..111 */
    };
    if (code < 0 || code >= (int)(sizeof(s_vk) / sizeof(s_vk[0])))
        return 0;
    return s_vk[code];
}

static UINT wlMouseState(int st)
{
    UINT mk = 0;
    if (st & SWINX_WL_STATE_LEFT)   mk |= MK_LBUTTON;
    if (st & SWINX_WL_STATE_RIGHT)  mk |= MK_RBUTTON;
    if (st & SWINX_WL_STATE_MIDDLE) mk |= MK_MBUTTON;
    if (st & SWINX_WL_STATE_SHIFT)  mk |= MK_SHIFT;
    if (st & SWINX_WL_STATE_CTRL)   mk |= MK_CONTROL;
    return mk;
}

// 合成器输入事件的修饰键状态：framebuffer 的 MOTION/BUTTON 会带上它，KEY
// 事件不带，这里留一份供按键消息填 MK_*（与 Win32 GetMessage 的语义一致）。
static int s_wlInputState = 0;

// X11 在 button press/release 里 setKeyState(VK_xBUTTON)、motion 里
// onMouseEvent(state) 全量对账；Wayland 的 GetAsyncKeyState 转发 GetKeyState，
// 后者读 m_keyboardState —— richedit41 的 CTxtEdit::OnTxMouseMove 正是靠
// GetAsyncKeyState(VK_LBUTTON) < 0 决定是否扩展选区，所以这里必须把
// wl_pointer 的物理按钮状态同步进来（wasm/input.cpp 的 SyncMouseButtonState 同理）。
static void syncWlMouseButtonState(SConnection *conn, int st)
{
    conn->SetKeyDown(VK_LBUTTON, (st & SWINX_WL_STATE_LEFT) != 0);
    conn->SetKeyDown(VK_RBUTTON, (st & SWINX_WL_STATE_RIGHT) != 0);
    conn->SetKeyDown(VK_MBUTTON, (st & SWINX_WL_STATE_MIDDLE) != 0);
}


static void SwinxWlEventThunk(const SwinxWlEvent *ev)
{
    SConnection *conn = s_pWlConn;
    if (!conn || !ev)
        return;
    if (!ev->win && ev->type != SWINX_WL_EVT_DND_LEAVE)
        return;
    HWND hWnd = (HWND)ev->win;
    /* 键盘焦点在 Wayland 里跟着 xdg_toplevel 走，而自绘装饰的窗口里 toplevel 是
     * 装饰窗：键事件必须转投内容窗（SOUI 的宿主窗口），否则按键/输入法全会
     * 落在装饰窗上。指针事件不转：标题栏本来就是装饰窗的。 */
    if (ev->type == SWINX_WL_EVT_KEY || ev->type == SWINX_WL_EVT_MODIFIERS)
    {
        WndObj w = WndMgr::fromHwnd(hWnd);
        if (w && w->plat.frameContent && IsWindow(w->plat.frameContent))
            hWnd = w->plat.frameContent;
    }
    switch (ev->type)
    {
    case SWINX_WL_EVT_CONFIGURE:
        conn->onWlConfigure(hWnd, ev->a, ev->b);
        break;
    case SWINX_WL_EVT_CLOSE:
        conn->onWlClose(hWnd);
        break;
    case SWINX_WL_EVT_ENTER:
        /* 指针焦点与键盘焦点是两回事：前者只用于光标解析与 GetCursorPos。
         * framebuffer 在进入时已缓存新的 enter serial，这里按 Win32 的父链
         * 规则重新解析一次光标（GTK update_cursor 同款），否则跨窗口后光标
         * 会停在上一块 surface 设置的图案上。 */
        conn->SetMouseFocus(hWnd);
        applyWlCursor(conn, hWnd);
        // fall through
    case SWINX_WL_EVT_MOTION:
        /* xdg_toplevel_move 由合成器独占移动，Wayland 不回告窗口位置；只要
         * wndObj->rc 被任何路径写入非零原点，WM_NCHITTEST 就会拿 surface 局部
         * 坐标去比屏幕坐标的 rc，把 caption 判成边框（光标变拉伸、拖动变缩放）。
         * configure/WM_MOVE 的归一覆盖不到"合成器移动后没有再发这两类事件"的
         * 情形，所以在每次指针事件入口兜底归一，保证 hit-test 同一口径。 */
        normalizeWlToplevelOrigin(conn, hWnd);
        s_wlInputState = ev->c;
        conn->SetCursorPos(ev->a, ev->b);
        syncWlMouseButtonState(conn, ev->c);
        conn->postMsg(hWnd, WM_MOUSEMOVE, wlMouseState(ev->c), MAKELPARAM(ev->a, ev->b));
        break;
    case SWINX_WL_EVT_LEAVE:
        /* 与 framebuffer.cpp::pointer_leave 对应：合成器独占 move/resize 时会
         * 吞掉 release，这里同步清掉平台层的按键缓存，否则后续 WM_MOUSEMOVE
         * 带残留 MK_LBUTTON，wnd.cpp 不刷新 htCode，caption 会被当成旧边。 */
        s_wlInputState &= ~(SWINX_WL_STATE_LEFT | SWINX_WL_STATE_RIGHT | SWINX_WL_STATE_MIDDLE);
        syncWlMouseButtonState(conn, s_wlInputState);
        /* 边界诊断：framebuffer 侧打的是"离开那一刻的 surface 局部坐标"，这里
         * 打窗口自己的 rc。两者一比就能判定"指针是不是在窗口边界上离开的"：
         *   last=(w-1, y) + rc=(0,0 w h) ⇒ 边界与可见窗口一致，光标若看起来
         *   偏内，就只能是合成器/邻居窗口（XWayland）的问题；
         *   last=(w-10, y) ⇒ 我们的 surface 比可见窗口窄，是真 bug。 */
#ifdef WM_MOUSELEAVE
        conn->postMsg(hWnd, WM_MOUSELEAVE, 0, MAKELPARAM(0, 0));
#endif
        break;
    case SWINX_WL_EVT_BUTTON:
    {
        UINT down = (UINT)ev->b;
        static UINT s_downMsg[4] = { 0, WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN };
        static UINT s_dblMsg[4]  = { 0, WM_LBUTTONDBLCLK, WM_RBUTTONDBLCLK, WM_MBUTTONDBLCLK };
        static UINT s_upMsg[4]   = { 0, WM_LBUTTONUP,   WM_RBUTTONUP,   WM_MBUTTONUP };
        static UINT s_btnVk[4] = { 0, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
        if (ev->a < 1 || ev->a > 3)
            break;
        normalizeWlToplevelOrigin(conn, hWnd); // 与 MOTION 同理：按下瞬间也要归一 rc 原点
        conn->SetCursorPos(ev->c, ev->d);
        conn->SetKeyDown(s_btnVk[ev->a], down != 0);
        UINT mk = wlMouseState(s_wlInputState);
        UINT msg = down ? s_downMsg[ev->a] : s_upMsg[ev->a];
        if (down)
        {
            /* 双击：对齐 X11 XCB_BUTTON_PRESS 的 m_tsPrevPress 判定 + Win32 的
             * CS_DBLCLKS 开关。Wayland 不会替客户端合成 DBLCLK，缺了这一步
             * richedit 双击选词、SListView 双击打开之类的操作全部失效。
             * 时间戳优先用协议给的（事件真实发生时刻）；只有旧合成器/回投被
             * 裁剪时才退回 GetTickCount。 */
            WndObj wndObj = WndMgr::fromHwnd(hWnd);
            BOOL bAutoDblClick = wndObj ? wndObj->bAutoDblClick : TRUE;
            uint32_t now = ev->time ? (uint32_t)ev->time : (uint32_t)GetTickCount();
            uint32_t span = conn->OnButtonPress(ev->a - 1, now);
            if (bAutoDblClick && span != (uint32_t)-1 &&
                span <= conn->GetDoubleClickTime())
                msg = s_dblMsg[ev->a];
        }
        conn->postMsg(hWnd, msg, mk, MAKELPARAM(ev->c, ev->d));
        break;
    }
    case SWINX_WL_EVT_AXIS:
        if (ev->a)
            conn->postMsg(hWnd, WM_MOUSEHWHEEL,
                          MAKEWPARAM(0, (WORD)(short)(ev->a > 0 ? 120 : -120)),
                          MAKELPARAM(ev->c, ev->d));
        if (ev->b)
            conn->postMsg(hWnd, WM_MOUSEWHEEL,
                          MAKEWPARAM(0, (WORD)(short)(ev->b > 0 ? 120 : -120)),
                          MAKELPARAM(ev->c, ev->d));
        break;
    case SWINX_WL_EVT_KEY:
    {
        UINT vk = wlScancodeToVk(ev->a);
        if (!vk)
            break;
        /* Wayland 没有 WM_ACTIVATE：键盘焦点落在哪个窗口上，哪个窗口就是活动窗
         * （用户点了别的窗口，合成器把焦点挪过去，这里跟着更新）。getActiveWnd
         * 是 tooltip / 菜单挑 popup 父窗的依据，必须真实。popup 自身
         * （TOOLWINDOW）不算活动窗，否则下一层弹窗会挂到弹窗上。 */
        {
            WndObj aw = WndMgr::fromHwnd(hWnd);
            if (aw && !(aw->dwStyle & WS_CHILD) && !(aw->dwExStyle & WS_EX_TOOLWINDOW))
                conn->SetActiveWindow(hWnd);
        }
        bool down = ev->b != 0;
        conn->SetKeyDown(vk, down);
        // X11 把按键消息投给 m_hFocus（真正的焦点控件，如 richedit 子窗），
        // re41 靠自己的 WM_KEYDOWN 维护内部 Ctrl/Shift 标志；若投给宿主窗，
        // GetKeyboardFlag(CTRL) 永远为假，Ctrl+C/V 直接失效。
        HWND hFocus = conn->GetFocus();
        HWND hKeyTarget = (hFocus && IsWindow(hFocus)) ? hFocus : hWnd;
        // WM_KEYDOWN 的 HIWORD 低字节才是扫描码（对齐 X11 的 XCB_KEY_PRESS）；
        // 原写法 (ev->a << 16) 经 (WORD) 截断后扫描码恒为 0。
        conn->postMsg(hKeyTarget, down ? WM_KEYDOWN : WM_KEYUP, vk,
                      MAKELPARAM(1, (WORD)(ev->a & 0xff)));
        break;
    }
    case SWINX_WL_EVT_POPUP_DONE:
        conn->onWlPopupDone(hWnd);
        break;
    case SWINX_WL_EVT_POPUP_CONFIGURE:
        conn->onWlPopupConfigure(hWnd, ev->a, ev->b, ev->c, ev->d);
        break;
    case SWINX_WL_EVT_DND_ENTER:
        conn->onWlDndEnter(hWnd, ev->a, ev->b, ev->c, ev->p);
        break;
    case SWINX_WL_EVT_DND_MOTION:
        conn->onWlDndMotion(hWnd, ev->a, ev->b, ev->c, ev->p);
        break;
    case SWINX_WL_EVT_DND_LEAVE:
        conn->onWlDndLeave(hWnd);
        break;
    case SWINX_WL_EVT_DND_DROP:
        conn->onWlDndDrop(hWnd, ev->p);
        break;
    case SWINX_WL_EVT_IME_COMMIT:
        conn->onWlImeCommit((HWND)ev->win, (const char *)ev->p, ev->a);
        free(ev->p);
        break;
    case SWINX_WL_EVT_TOPLEVEL_STATE:
        // 尺寸已由 configure 事件更新；状态缓存留在 framebuffer 供 SW_RESTORE 用
        break;
    case SWINX_WL_EVT_MODIFIERS:
        s_wlInputState = (s_wlInputState & 7) | ev->a;
        conn->SetKeyDown(VK_SHIFT,  (ev->a & SWINX_WL_STATE_SHIFT) != 0);
        conn->SetKeyDown(VK_CONTROL,(ev->a & SWINX_WL_STATE_CTRL)  != 0);
        conn->SetKeyDown(VK_MENU,   (ev->a & SWINX_WL_STATE_ALT)   != 0);
        if (ev->a & SWINX_WL_STATE_CAPS)
            conn->ToggleKeyState(VK_CAPITAL);
        break;
    default:
        break;
    }
}

void SConnection::onWlConfigure(HWND hWnd, int cx, int cy)
{
    if (cx < 1 || cy < 1 || !hWnd)
        return;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    /* 装饰窗的 configure = 合成器在改这个窗口的大小（用户拖边框、平铺、
     * maximize）。真正拥有画布的是**内容窗**，所以把尺寸减去标题栏后转发给
     * 内容窗，走正常的 resize 链路；装饰窗自己的画布由 SwinxWlFrameResize()
     * 跟着内容窗重新对齐（两者一致时是空操作）。 */
    if (wndObj && wndObj->plat.frameContent)
    {
        HWND hContent = wndObj->plat.frameContent;
        WndObj cw = WndMgr::fromHwnd(hContent);
        int inner = cy - SwinxWlFrameTitleHeightFor(hContent);
        if (inner < 1)
            inner = 1;
        if (cw && !cw->bDestroyed &&
            (cw->rc.right - cw->rc.left != cx || cw->rc.bottom - cw->rc.top != inner))
            ::SetWindowPos(hContent, NULL, cw->rc.left, cw->rc.top, cx, inner,
                           SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        return;
    }
    /* 首次 configure 时也要归一：CreateWindowEx 阶段 rc 来自 CREATESTRUCT
     * （CenterWindow 前可能是 CW_USEDEFAULT 换算出的屏幕坐标），与 compositor
     * 实际摆位无关。统一到 (0,0) 后才能用 pointer 的 surface 局部坐标做
     * WM_NCHITTEST。WM_SIZE 会在同一个 rc 上写回宽高，不受原点影响。 */
    normalizeWlToplevelOrigin(this, hWnd);
    // 每窗口一条 configure：只给目标窗口投 WM_SIZE 更新其 rc 与 cairo 画布。
    // 不先改 rc —— wnd.cpp 的 WM_SIZE 处理器只在 sz 与 rc 不同时才 ResizeSurface。
    postMsg(hWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(cx, cy));
    /* 不再把"首个顶层窗的尺寸"当成屏幕尺寸：MessageBox 的 configure 会把
     * 屏幕写成 373x212，demo 主窗再写成 888x650，CenterWindow 与 tooltip
     * 翻转全错。屏幕尺寸只认 wl_output（见 GetMonitorRect/GetScreenWidth）。 */
}

void SConnection::onWlClose(HWND hWnd)
{
    // 合成器关闭窗口 = Win32 的 WM_CLOSE：交给应用决定是否真的销毁。
    if (!hWnd)
        return;
    /* 装饰窗没有应用逻辑：关闭请求归内容窗（它的 WM_CLOSE 才是 SOUI/模态
     * 循环等待的那个）。 */
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj && wndObj->plat.frameContent)
    {
        postMsg(wndObj->plat.frameContent, WM_CLOSE, 0, 0);
        return;
    }
    postMsg(hWnd, WM_CLOSE, 0, 0);
}

void SConnection::onWlPopupDone(HWND hWnd)
{
    /* xdg_popup.done：合成器要求关闭弹窗（用户点到别处 / 另一个 popup 抢栈）。
     * tooltip/菜单（WS_EX_TOOLWINDOW）走 ShowWindow(SW_HIDE)，连同 SOUI 的
     * STipCtrl 状态（m_strTip / timer）一起复位，否则下次鼠标进来
     * IsWindowVisible 还是 TRUE，tooltip 再也不会重新计时弹出。
     *
     * 但模态对话框（swinx 的 MessageBox 被转成 popup 以便定位）不能只隐藏：
     * 它的模态循环还在 GetMessage 里等按钮消息，隐藏后按钮永远点不到，
     * 进程就卡在对话框里，后面的主窗口根本走不到 Create/ShowWindow。
     * 这里按 GTK 对 dialog 的处理发 WM_CLOSE，让模态循环正常退出。 */
    if (!hWnd)
        return;
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj || wndObj->bDestroyed || !(wndObj->dwStyle & WS_VISIBLE))
        return;
    if (wndObj->dwExStyle & WS_EX_TOOLWINDOW)
        ShowWindow(hWnd, SW_HIDE);
    else if (wndObj->plat.frameContent)
        postMsg(wndObj->plat.frameContent, WM_CLOSE, 0, 0); // 装饰窗不是应用窗口
    else
        postMsg(hWnd, WM_CLOSE, 0, 0);
}

void SConnection::beginWlDrag(HWND hWnd, UINT htCode)
{
    /* 自绘 NC 的交互式移动/缩放：Wayland 合成器不会帮自绘标题栏的 toplevel
     * 移动窗口，必须在 pointer press 拿到 serial 后主动 xdg_toplevel_move。
     * 只处理真正的顶层窗（有 xdg_toplevel 的）；popup/subsurface/消息窗忽略。
     * 转发给 framebuffer 时用 fd 事件泵里缓存的 press serial。 */
    if (!hWnd)
        return;
    /* 有装饰窗时 xdg_toplevel 属于装饰窗：内容窗的 caption/边框命中必须转成
     * 装饰窗的 move/resize 请求，否则合成器不知道要动哪个窗口。 */
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    HWND hTarget = (wndObj && wndObj->plat.frame) ? wndObj->plat.frame : hWnd;
    SwinxWlSurface *wl = SwinxWlSurfaceGet(hTarget);
    if (!wl)
        return;
    SwinxWlSurfaceBeginDrag(wl, htCode);
}

void SConnection::onWlPopupConfigure(HWND hWnd, int x, int y, int w, int h)
{
    /* 合成器按 xdg_positioner 约束后的最终位置（父窗口局部坐标）。我们用
     * anchor=TOP_LEFT/gravity=BOTTOM_RIGHT/no-constraint，正常情况下 x/y 就是
     * SetPopAnchor 传入的值。这里**不回写 wndObj->rc**：SOUI 的 tooltip 位置是
     * 它自己按鼠标算的，回写会让下一次 present 算出的 anchor 又变、触发
     * xdg_popup 重建，形成 configure ↔ 重建的振荡。只留诊断输出。 */
}

void SConnection::flush()
{
    SwinxWlFlush();
}

void SConnection::sync()
{
    SwinxWlDispatch();
    SwinxWlFlush();
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
    /* 这里不能调全局 ::GetProcessHeap()：posix 的 memory.cpp 把它实现成
     * SConnMgr::instance()->getProcessHeap()，两边互调就是无限递归。本后端
     * 与 posix 共用 HeapCreate/HeapAlloc，所以进程堆由这里持有。 */
    return m_hHeap ? m_hHeap : (m_hHeap = HeapCreate(0, 0, 0));
}
