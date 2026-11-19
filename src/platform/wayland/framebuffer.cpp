/*
 * framebuffer.cpp -- Wayland 后端：每窗口 wl_surface + SHM 双缓冲 + 帧回调。
 *
 * 模型对齐 GTK 3.24.48 gdkwindow-wayland.c / gdkdisplay-wayland.c：
 *   - 每个窗口一块 wl_surface；主窗口是 xdg_toplevel，其余窗口（MessageBox、
 *     下拉窗、WS_CHILD）是父 surface 的 wl_subsurface（GTK 的
 *     should_map_as_subsurface 路径），坐标沿用 SOUI 的屏幕矩形。
 *   - 每窗独立 SHM buffer 池（最多 3 块，按需分配、release 回收）。每块 buffer
 *     各自记住"自上次同步以来累积的脏区"，因此交替 buffer 时只拷贝各自欠的
 *     那部分，不需要整幅 memcpy（等价于 GTK 的 read_back_cairo_surface）。
 *   - 每窗一个 wl_surface_frame；回调未回来时本帧至多 commit 一次（GTK 的
 *     on_frame_clock_after_paint 冻结/解冻语义）。
 *   - 全局帧缓冲 s_fb 只作为"虚拟屏幕尺寸"查询 shim，不再承载上屏像素。
 */
#define _GNU_SOURCE 1
#include "framebuffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <string>
#include <time.h>

#ifdef SWINX_HAVE_WAYLAND_CLIENT
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
#include "text-input-unstable-v3-client-protocol.h"
#endif
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#endif
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
/* wl_pointer.button 用 evdev 键码（GTK 见 gdkdevice-wayland.c 的
 * linux/input.h 与 BUTTON_BASE）。只列三个鼠标键，避免拉进整套内核头。 */
#ifndef BTN_LEFT
#define BTN_LEFT   0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT  0x111
#endif
#ifndef BTN_MIDDLE
#define BTN_MIDDLE 0x112
#endif
#endif

#define SWINX_WL_DEFAULT_W 1280
#define SWINX_WL_DEFAULT_H 720
#define SWINX_WL_MAX_BUFS 3
#define SWINX_WL_MAX_SEATS 4
#define SWINX_WL_MAX_OUTPUTS 4

/* Win32 WM_NCHITTEST 命中码（与 include/winuser.h 一致）。framebuffer.cpp 不
 * 引 windows.h，BeginDrag 需要把 htCode 映射到 xdg resize edge，这里本地定义。 */
#ifndef HTCAPTION
#define HTCAPTION     2
#define HTLEFT        10
#define HTRIGHT       11
#define HTTOP         12
#define HTTOPLEFT     13
#define HTTOPRIGHT    14
#define HTBOTTOM      15
#define HTBOTTOMLEFT  16
#define HTBOTTOMRIGHT 17
#endif

// ---------------------------------------------------------------------------
// 全局虚拟屏幕 shim
// ---------------------------------------------------------------------------

static uint32_t *s_fb = NULL;
static int s_width = SWINX_WL_DEFAULT_W;
static int s_height = SWINX_WL_DEFAULT_H;
static int s_requests = 0;
static int s_presents = 0;
static SwinxWlEventCb s_eventCb = NULL;

static void fb_alloc(int w, int h)
{
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (s_fb && w == s_width && h == s_height)
        return;
    uint32_t *nb = (uint32_t *)calloc((size_t)w * (size_t)h, 4);
    if (!nb)
        return;
    free(s_fb);
    s_fb = nb;
    s_width = w;
    s_height = h;
}

#ifdef SWINX_HAVE_WAYLAND_CLIENT

/* 协议类型前向声明：老版本 wayland-protocols 没有 viewporter /
 * fractional-scale，SWINX_HAVE_FRACTIONAL_SCALE 不会定义。让这四个字段
 * 始终存在于结构体里（未启用时恒为 NULL），这样 null 检查 / 恒等变换路径
 * 在两种构建配置下都能编译；真正的协议调用仍全部在宏保护块内。 */
struct wp_viewport;
struct wp_fractional_scale_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;

// ---------------------------------------------------------------------------
// Wayland 对象
// ---------------------------------------------------------------------------

struct WlBuf
{
    struct wl_buffer *buffer;
    uint32_t *bits;
    size_t bytes;
    int busy;             // 合成器尚未 release
    RECT damage;          // 自上次同步以来这块 buffer 欠的脏区（窗口局部坐标）
    int hasDamage;
};

struct SwinxWlSurface
{
    void *hwnd;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xdg_popup *popup;
    struct wl_subsurface *subsurface;
    struct wp_viewport *viewport;       /* 物理 buffer -> 逻辑 surface 的映射；未启用协议时恒为 NULL */
    struct wp_fractional_scale_v1 *fractionalScale;
    int scale120;                       /* 本 surface 采用的倍率（120=1.0） */
    int scaleFromFractional;            /* 1=收到了 preferred_scale 事件 */
    int vpDstW, vpDstH;                 /* 上次下发的 viewport destination，避免每帧刷协议 */
    struct SwinxWlSurface *parent;   /* subsurface 的父 surface */
    struct SwinxWlSurface *popParent;/* popup 的父 xdg_surface（对齐 GTK popup_parent）*/
    SwinxWlSurface *next;

    int role;
    int x, y;             // 相对父 surface 原点（subsurface）
    int w, h;
    int visible;
    int configured;
    int inputTransparent; // 空 input region：tooltip 等不该接收指针事件
    int inL, inT, inR, inB; // CSD 边缘让给装饰窗的输入内缩

    int cfgW, cfgH;       // toplevel.configure 缓存
    /* popup 定位缓存（父窗口局部坐标）。xdg_popup 创建后位置不可改，
     * 几何变化时比较这份缓存决定是否销毁重建（GTK reposition workaround）。 */
    int popAx, popAy, popAw, popAh;
    int popX, popY, popW, popH;
    int popHasAnchor;

    WlBuf bufs[SWINX_WL_MAX_BUFS];
    int nBufs;

    /* 当前窗口画布（wnd.cpp 的 cairo image surface），present 时更新。
     * frame callback 回来后可能要补一次提交，届时从这里重新拷贝。 */
    const uint32_t *srcBits;
    int srcStride;

    unsigned int toplevelStates; // 最近一次 xdg_toplevel.configure 的状态
    int needFull;         // resize/首次提交：整幅 damage

    struct wl_callback *frameCb;
    int framePending;     // 已 commit，等 wl_callback.done
};

/* wl_data_offer 是我们自己持有的对象：mimes 随 offer 事件累积，读数据时
 * 用 wl_data_offer.receive 走管道，读到 EOF 才算完成。 */
struct WlDataSource;
struct WlDataOffer
{
    struct wl_data_offer *offer;
    uint32_t serial;              // enter 的 serial（v1/v2 accept 需要）
    std::vector<std::string> mimes;
    uint32_t sourceActions;
    uint32_t action;
    int accepted;       // wl_data_offer.accept issued (prerequisite for finish)
    int reading;   // receive->pipe 进行中：leave 不能释放 seat 那份引用
    int refs;      // seat(dnd/selection) + 进行中的 read 各持一份
};

struct WlDataSource
{
    void *ctx;
    SwinxWlDataSourceRead read;
    std::vector<std::string> mimes;
    struct wl_data_source *src;
    int isDrag;
};

struct WlSeat
{
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    uint32_t caps;
    void *focus;          // 当前指针所在的窗口 HWND
    int fx, fy;           // 相对 focus surface 的坐标
    int state;            // SWINX_WL_STATE_*
    uint32_t pressSerial; // 最近一次 button press 的 serial（xdg move/resize 用）
    /* 光标是 seat 级状态（GTK gdkwaylanddevice.c 同款）：所有窗口共用一个
     * cursor surface，边界变化时必须用最近一次 enter serial 重设。 */
    uint32_t enterSerial;
    struct wl_surface *cursorSurface;
    WlBuf cursorBuf;
    int cursorW, cursorH, hotX, hotY;
    int hasCursor, cursorHidden;
    /* leave 先挂起、本批事件分发结束再定案（见 leave_finalize）：
     * 合成器重新计算指针焦点时，会在**同一个窗口**上先 leave 再 enter（repick，
     * mutter 在 subsurface 提交/栈序变化后就会这么干，两块事件甚至同坐标）。
     * 立刻按"离开窗口"处理会误清 MK_LBUTTON（正在拖动时中断）、误发
     * WM_MOUSELEAVE（tooltip 重计时、hover 丢失）、还把光标交还合成器造成
     * 一次可见的闪烁——现象就是"指针还在窗口内，形状却变了"。 */
    int leavePending;
    /* 合成器还持有 buffer 时不能改它的像素：暂存最近一次请求，release 后补交。 */
    uint32_t *pendingBits;
    int pendingW, pendingH, pendingHotX, pendingHotY;

    /* wl_data_device（剪贴板 + 拖放）是 seat 级对象。 */
    struct wl_data_device *dataDevice;
    WlDataOffer *selectionOffer;  // 合成器当前 selection 的 offer
    WlDataOffer *dndOffer;        // 正在投递给本客户端的拖放 offer
    void *dndTarget;              // enter 命中的 HWND（motion/drop 没有 surface 参数）
    WlDataSource *selectionSource;
    WlDataSource *dragSource;
    int dragDone;                 // dnd_finished / cancel 已到达
    int dragDropped;
    uint32_t dragEffect;          // 目标选定的 DROPEFFECT_*
    SwinxWlDragQuery dragQuery;
    void *dragCtx;
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    struct zwp_text_input_v3 *textInput;
    char *imeCommit;      // commit_string 暂存，done 时回投
    int imeRectX, imeRectY, imeRectW, imeRectH; // enter 时重发光标矩形
#endif
};

struct WlOutput
{
    struct wl_output *output;
    int x, y;             // 逻辑位置（wl_output.geometry）
    int w, h;             // 当前 mode 的物理像素；按 transform 换成逻辑方向
    int scale;            // wl_output.scale（整数倍率）
    int scale120;         // 有效倍率：优先 fractional_scale 上报值，否则 scale*120
    int transform;
    int hasMode;
};

struct WaylandGlobals
{
    int tried;
    int ready;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_data_device_manager *dataDeviceManager;
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    struct zwp_text_input_manager_v3 *textInputManager;
#endif
    struct wp_viewporter *viewporter;   /* 未启用协议时恒为 NULL */
    struct wp_fractional_scale_manager_v1 *fractionalScaleManager;
    unsigned int selectionGeneration;
    int selectionOwned;           // selection 事件来自我们自己的 source
    int selectionAwaitOwn;        // set_selection 已发出，等合成器回告
    WlSeat seats[SWINX_WL_MAX_SEATS];
    int nSeats;
    WlOutput outputs[SWINX_WL_MAX_OUTPUTS];
    int nOutputs;
    SwinxWlSurface *surfaces;
    uint32_t lastSerial;
};

static WaylandGlobals g;

static void surface_flush(SwinxWlSurface *s);
static void emit_event(int type, void *win, int a, int b, int c, int d,
                       unsigned int time = 0);
static void applyInputRegion(SwinxWlSurface *s);
static int scaleScalar(SwinxWlSurface *s, int v, bool toPhysical = false);
static void subsurfaceSetPosition(SwinxWlSurface *s, int x, int y);
static void emit_dnd_event(int type, void *win, int a, int b, int c, WlDataOffer *o);
static void destroy_buf(WlBuf *b);
static int surface_has_damage(SwinxWlSurface *s);
static SwinxWlSurface *find_surface(void *hwnd);
static SwinxWlSurface *find_popup_parent(SwinxWlSurface *s);
static void popup_role_destroy(SwinxWlSurface *s, bool recreate);
static void data_devices_ensure(void);
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
static void ime_ensure(struct WlSeat *seat);
static void emit_ime_event(int type, void *win, char *text, int serial);
#endif
static void dnd_offer_destroy(WlDataOffer *o);
static int read_offer_to_heap(WlDataOffer *o, const char *mime, void **outBuf);

/* 诊断开关：SWINX_WL_TRACE=1 时把每次上屏的尺寸/脏区/画布采样打到 stderr。
 * 用于区分"SOUI 没画"和"画了但没同步到 SHM buffer"。默认关闭，不影响性能。 */
static int wl_trace(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("SWINX_WL_TRACE") ? 1 : 0;
    return on;
}

// ---------------------------------------------------------------------------
// 窗口图标：xdg_toplevel.app_id（Wayland 没有 per-window 位图协议）
//
// 合成器/taskbar 用 app_id 去 applications\*.desktop 查 Icon=，所以这里
// 只维护一个字符串，建 toplevel 时下发；真正的图标由随应用安装的 desktop
// 文件提供。推导顺序见 framebuffer.h 的 SwinxWlSetAppId 注释。
// ---------------------------------------------------------------------------
static char s_appId[128] = "";
static int  s_appIdExplicit = 0;

static void deriveAppId(char *out, size_t cap)
{
    const char *envId = getenv("SOUI_WL_APP_ID");
    if (envId && *envId)
    {
        snprintf(out, cap, "%s", envId);
        return;
    }
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0)
    {
        exe[n] = 0;
        const char *base = strrchr(exe, '/');
        base = base ? base + 1 : exe;
        if (*base)
        {
            snprintf(out, cap, "%s", base);
            return;
        }
    }
    snprintf(out, cap, "soui");
}

const char *SwinxWlGetAppId(void)
{
    if (!s_appId[0])
        deriveAppId(s_appId, sizeof(s_appId));
    return s_appId;
}

/* 缩放总开关：SWINX_WL_SCALE=0 完全关闭（返回 120=1.0，viewport 不下发），
 * 便于在缩放路径出问题时一键退回旧的整数缩放行为。 */
static int scaleEnabled(void)
{
    static int on = -1;
    if (on < 0)
    {
        const char *v = getenv("SWINX_WL_SCALE");
        on = (v && strcmp(v, "0") == 0) ? 0 : 1;
    }
    return on;
}

#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
static int s_probeScale120 = 0; // 启动探针拿到的主屏倍率（120=1.0）
#endif

/* 取 output 的有效倍率：主屏用 fractional_scale 探针值，其余用 wl_output.scale。
 * SWINX_WL_SCALE=0 时恒为 120。 */
static int outputScale120(const WlOutput *o)
{
    if (!o || !scaleEnabled())
        return 120;
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    /* 150% 下 wl_output.scale=2，但合成器逻辑坐标空间实际是 /1.5；用探针
     * 倍率算 monitor rect 才不会把 2560x1440 报成 1280x720（应为 1707x960）。 */
    if (s_probeScale120 > 0 && o == &g.outputs[0])
        return s_probeScale120;
#endif
    int s = o->scale > 0 ? o->scale : 1;
    return s * 120;
}

void SwinxWlSetAppId(const char *appId)
{
    if (appId && *appId)
    {
        snprintf(s_appId, sizeof(s_appId), "%s", appId);
        s_appIdExplicit = 1;
    }
    else if (!s_appIdExplicit)
    {
        /* NULL = 重新推导；已经显式设置过时不覆盖。 */
        deriveAppId(s_appId, sizeof(s_appId));
    }
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    /* 已存在的 toplevel 就地更新（含 CSD 装饰窗；popup/subsurface 没有
     * xdg_toplevel 对象，不需要也不能设）。 */
    for (SwinxWlSurface *s = g.surfaces; s; s = s->next)
        if (s->toplevel)
            xdg_toplevel_set_app_id(s->toplevel, s_appId);
#endif
    if (wl_trace())
        fprintf(stderr, "[wl-icon] app_id=%s\n", s_appId);
}

// ---------------------------------------------------------------------------
// buffer release：必须先于 create_buf（listener 注册用到）
// ---------------------------------------------------------------------------

static void cursor_apply(WlSeat *seat);

/* 拖动图标：SConnection 在 dragQuery→GiveFeedback 里把按目标反馈选出的
 * IDC_* 位图交给 SwinxWlDragIconImage，由这里 attach 上屏。
 * GNOME 在有 drag icon 时把指针光标让给图标（DnD grab 期间 wl_pointer
 * .set_cursor 被合成器忽略），所以 copy/move/link/nodrop 只能靠图标自身表达。
 *
 * 关键点：**不能原地改一块已 attach 的 buffer**。mutter 会把 drag icon 的
 * buffer 一直攥到拖动结束（它不是逐帧合成的窗口 surface），busy 永不复位，
 * 于是"复用同一块 buffer + pending 补画"的方案永远不会重绘——画面上只剩
 * start_drag 时那块初始图标。必须每次换状态都 attach 一块**全新的** buffer，
 * 旧 buffer 等 release 后回收（一次拖动里最多切换十几次，24x24x4=2.3KB/块）。 */
#define WL_DRAG_ICON_SLOTS 8
static struct wl_surface *s_dragIcon = NULL;
static WlBuf s_dragIconBufs[WL_DRAG_ICON_SLOTS];
static int s_dragIconNSlots = 0;         // 该数组里被 create_buf 过的 slot 数
static WlBuf *s_dragIconLive = NULL;     // 当前 attach 的 slot（不可改像素）
static int s_dragIconW = 0, s_dragIconH = 0;
static int s_dragIconHotX = 0, s_dragIconHotY = 0; // 位图热点（去重用）

static void buffer_release(void *data, struct wl_buffer *)
{
    WlBuf *b = (WlBuf *)data;
    b->busy = 0;
    /* drag icon 的 buffer 不属于任何窗口 surface，必须在窗口循环之前识别。
     * 这里**只清 busy**（上面的 b->busy = 0），不销毁对象：release 回调里
     * wl_buffer_destroy 虽然合法，但会让 libwayland 立刻回收该对象 id，与
     * 合成器随后的事件存在竞争；slot 由 drag_icon_take_slot 复用、拖动结束
     * 时统一销毁，代价只是多留几块 24x24 的 shm。 */
    for (int i = 0; i < s_dragIconNSlots; ++i)
        if (&s_dragIconBufs[i] == b)
            return;
    /* 光标 buffer 不属于任何 surface：上面的窗口循环不会处理它，在这里补交
     * busy 期间收到的最新光标（否则快速换光标时停在旧图案）。 */
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *seat = &g.seats[i];
        if (b != &seat->cursorBuf)
            continue;
        if (seat->pendingBits)
        {
            if (seat->pendingW == seat->cursorW && seat->pendingH == seat->cursorH &&
                seat->cursorBuf.bits)
            {
                memcpy(seat->cursorBuf.bits, seat->pendingBits,
                       (size_t)seat->cursorW * seat->cursorH * 4);
                seat->hotX = seat->pendingHotX;
                seat->hotY = seat->pendingHotY;
                /* pending 是"还没有真正上屏的图案"：补交之后必须置位，否则
                 * cursor_apply 会认为"从没有过图案"而直接返回，光标永远不显示。 */
                seat->hasCursor = 1;
            }
            free(seat->pendingBits);
            seat->pendingBits = NULL;
        }
        cursor_apply(seat);
        return;
    }
    // 归还时不清 damage：它 busy 期间可能漏掉了若干次改动，下次被选中时
    // 必须按累积脏区补齐（等价于 GTK 的 backfill）。
    // 关键：若上一次 surface_flush 因"三块 buffer 全 busy"放弃，必须在这里
    // 重试；否则该 surface 会永久卡在旧画面（表现为窗口只画出一部分）。
    for (SwinxWlSurface *s = g.surfaces; s; s = s->next)
    {
        for (int i = 0; i < s->nBufs; ++i)
        {
            if (&s->bufs[i] == b)
            {
                if (s->visible && s->srcBits && surface_has_damage(s))
                    surface_flush(s);
                return;
            }
        }
    }
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release,
};

// ---------------------------------------------------------------------------
// SHM buffer：memfd 优先，shm_open 回退（GTK gdkdisplay-wayland.c 同款）
// ---------------------------------------------------------------------------

static int make_shm_fd(size_t size, char **nameOut)
{
    *nameOut = NULL;
#ifdef SYS_memfd_create
    int fd = (int)syscall(SYS_memfd_create, "swinx-wayland", MFD_CLOEXEC);
    if (fd >= 0 && ftruncate(fd, (off_t)size) == 0)
        return fd;
    if (fd >= 0)
        close(fd);
#endif
    static int seq = 0;
    char name[80];
    snprintf(name, sizeof(name), "/swinx-wl-%d-%d", (int)getpid(), ++seq);
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;
    shm_unlink(name);
    if (ftruncate(fd, (off_t)size) < 0)
    {
        close(fd);
        return -1;
    }
    *nameOut = strdup(name);
    return fd;
}

static void destroy_buf(WlBuf *b)
{
    if (b->buffer)
    {
        wl_buffer_destroy(b->buffer);
        b->buffer = NULL;
    }
    if (b->bits)
    {
        // 合成器仍持有的映射不能 munmap（wl_buffer 销毁后 release 不再到达）。
        // leak-on-purpose：与 GTK destroy_buffers 的处理一致。
        if (!b->busy)
            munmap(b->bits, b->bytes);
        b->bits = NULL;
    }
    b->bytes = 0;
    b->busy = 0;
    b->hasDamage = 0;
}

static int create_buf(WlBuf *b, int w, int h)
{
    int stride = w * 4;
    size_t size = (size_t)stride * (size_t)h;
    if (size == 0)
        return 0;
    char *name = NULL;
    int fd = make_shm_fd(size, &name);
    if (fd < 0)
    {
        free(name);
        return 0;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED)
    {
        close(fd);
        free(name);
        return 0;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(g.shm, fd, (int)size);
    if (!pool)
    {
        munmap(p, size);
        close(fd);
        free(name);
        return 0;
    }
    b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                          WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    free(name);
    if (!b->buffer)
    {
        munmap(p, size);
        return 0;
    }
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    b->bits = (uint32_t *)p;
    b->bytes = size;
    b->busy = 0;
    b->hasDamage = 0;
    memset(b->bits, 0, size);
    return 1;
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------

static SwinxWlSurface *find_surface(void *hwnd)
{
    for (SwinxWlSurface *s = g.surfaces; s; s = s->next)
        if (s->hwnd == hwnd)
            return s;
    return NULL;
}

static void mark_dirty(WlBuf *b, const RECT *rc, int w, int h)
{
    int x0 = rc ? rc->left : 0, y0 = rc ? rc->top : 0;
    int x1 = rc ? rc->right : w, y1 = rc ? rc->bottom : h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 >= x1 || y0 >= y1)
        return;
    if (!b->hasDamage)
    {
        b->damage.left = x0; b->damage.top = y0;
        b->damage.right = x1; b->damage.bottom = y1;
        b->hasDamage = 1;
        return;
    }
    if (x0 < b->damage.left) b->damage.left = x0;
    if (y0 < b->damage.top) b->damage.top = y0;
    if (x1 > b->damage.right) b->damage.right = x1;
    if (y1 > b->damage.bottom) b->damage.bottom = y1;
}

static void copy_rect(WlBuf *b, const uint32_t *src, int stride,
                      const RECT *rc, int w, int h)
{
    int x0 = rc->left, y0 = rc->top, x1 = rc->right, y1 = rc->bottom;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int y = y0; y < y1; ++y)
    {
        memcpy(b->bits + (size_t)y * (size_t)w + x0,
               src + (size_t)y * (size_t)stride + x0,
               (size_t)(x1 - x0) * 4);
    }
}

static int surface_has_damage(SwinxWlSurface *s)
{
    if (s->needFull)
        return 1;
    for (int i = 0; i < s->nBufs; ++i)
        if (s->bufs[i].hasDamage)
            return 1;
    return 0;
}

// ---------------------------------------------------------------------------
// 光标：每个 seat 一个共享 cursor surface（GTK gdkwaylanddevice.c 的设计）
// ---------------------------------------------------------------------------

static void cursor_ensure_surface(WlSeat *seat)
{
    if (seat->cursorSurface || !g.compositor)
        return;
    seat->cursorSurface = wl_compositor_create_surface(g.compositor);
}

static void cursor_destroy(WlSeat *seat)
{
    if (!seat)
        return;
    free(seat->pendingBits);
    seat->pendingBits = NULL;
    destroy_buf(&seat->cursorBuf);
    if (seat->cursorSurface)
        wl_surface_destroy(seat->cursorSurface);
    seat->cursorSurface = NULL;
    seat->hasCursor = 0;
    seat->cursorW = seat->cursorH = 0;
}

static void cursor_apply(WlSeat *seat)
{
    if (!seat || !seat->pointer)
        return;
    if (seat->cursorHidden)
    {
        /* 协议里 wl_pointer.set_cursor(surface=NULL) 的语义是**隐藏光标**，
         * 不是"交还合成器"——指针离开本 surface 后合成器自己会用默认光标，
         * 不需要我们做任何事。所以只有 ShowCursor(FALSE) 才发 NULL。 */
        wl_pointer_set_cursor(seat->pointer, seat->enterSerial, NULL, 0, 0);
        return;
    }
    if (!seat->hasCursor || !seat->cursorSurface || !seat->cursorBuf.buffer)
        return; // 还没有任何图案：什么都不做，让合成器用它的默认光标
    /* 合成器还持有上一帧 buffer 时不能改它的内容。此时 surface 上是**旧图案**，
     * 而 hotX/hotY 也只在真正换图案时才更新（见 SwinxWlSetCursorImage），两者
     * 仍然自洽，可以安全地重新下发。这一步很关键：指针跨 surface（比如从主窗
     * 进入对话框）时合成器**不会**自动沿用我们的图案，必须重新 set_cursor，
     * 否则新窗口上看到的是上一个 surface（别的应用）的光标。 */
    if (seat->cursorBuf.busy)
    {
        wl_pointer_set_cursor(seat->pointer, seat->enterSerial,
                              seat->cursorSurface, seat->hotX, seat->hotY);
        return;
    }
    /* 协议要求 set_cursor 引用的 surface 必须已经 attach 过 buffer
     * (wl_pointer.set_cursor: "The surface must have a buffer before the cursor
     * is set")。先 commit 再 set_cursor，首次进入窗口也能立刻用上我们的图案。 */
    wl_surface_attach(seat->cursorSurface, seat->cursorBuf.buffer, 0, 0);
    wl_surface_damage(seat->cursorSurface, 0, 0, seat->cursorW, seat->cursorH);
    wl_surface_commit(seat->cursorSurface);
    seat->cursorBuf.busy = 1;
    wl_pointer_set_cursor(seat->pointer, seat->enterSerial,
                          seat->cursorSurface, seat->hotX, seat->hotY);
}

void SwinxWlSetCursorImage(const uint32_t *bits, int w, int h,
                           int hotspotX, int hotspotY)
{
    if (!g.ready || !bits || w < 1 || h < 1)
        return;
    /* 光标诊断：图案尺寸 + 热点。合成器永远把光标画在"真实指针 - 热点"处，
     * 所以"箭头看起来比指针靠内一截"要么是这里的热点不对（例如把 32x32 的
     * 拉伸光标 hotspot(10,6) 当成了箭头），要么根本不是光标问题（窗口
     * surface 的边界和看到的不一致）。只在变化时打，避免每条 motion 刷屏。 */
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *seat = &g.seats[i];
        if (!seat->pointer)
            continue;
        cursor_ensure_surface(seat);
        if (!seat->cursorSurface)
            continue;
        if (!seat->cursorBuf.buffer || seat->cursorW != w || seat->cursorH != h)
        {
            destroy_buf(&seat->cursorBuf);
            free(seat->pendingBits);
            seat->pendingBits = NULL;
            if (!create_buf(&seat->cursorBuf, w, h))
                continue;
            seat->cursorW = w;
            seat->cursorH = h;
        }
        if (seat->cursorBuf.busy)
        {
            size_t n = (size_t)w * (size_t)h * 4;
            uint32_t *pending = (uint32_t *)realloc(seat->pendingBits, n);
            if (!pending)
                continue;
            seat->pendingBits = pending;
            memcpy(pending, bits, n);
            seat->pendingW = w;
            seat->pendingH = h;
            seat->pendingHotX = hotspotX;
            seat->pendingHotY = hotspotY;
            seat->hasCursor = 1; // 图案已排队，release 补交后直接生效
            continue; // release 回调里补交
        }
        /* 热点与图案必须同时生效：busy 时只记 pending（上面分支），这里的赋值
         * 保证 hotX/hotY 永远对应 cursorBuf.bits 里的这份图案。 */
        seat->hotX = hotspotX;
        seat->hotY = hotspotY;
        memcpy(seat->cursorBuf.bits, bits, (size_t)w * (size_t)h * 4);
        seat->hasCursor = 1;
        cursor_apply(seat);
    }
}

void SwinxWlShowCursor(int visible)
{
    for (int i = 0; i < g.nSeats; ++i)
    {
        g.seats[i].cursorHidden = !visible;
        cursor_apply(&g.seats[i]);
    }
}

void SwinxWlRefreshCursor(void)
{
    for (int i = 0; i < g.nSeats; ++i)
        cursor_apply(&g.seats[i]);
}

uint32_t SwinxWlDragEffect(void)
{
    /* 只回 DROPEFFECT_COPY/MOVE/NONE：核心协议 dnd_action 还有 ask=4，
     * 但 DROPEFFECT_* 里没有对应位（见 data_source_action 的说明），
     * 落到 GiveFeedback 只会被当成未知值。 */
    for (int i = 0; i < g.nSeats; ++i)
        if (g.seats[i].dragSource)
            return g.seats[i].dragEffect & 3u;
    return 1; // DROPEFFECT_COPY
}

/* 24x24 的圆形徽标：底色表示动作，白色笔画表示 copy(+)/move(→)/link(∞)/
 * nodrop(斜杠)。只画不透明像素，ARGB8888 预乘语义下无需处理 alpha。 */
static void drag_icon_paint(uint32_t *p, int w, int h, unsigned effect)
{
    unsigned act = effect & 7u;
    uint32_t bg = 0xFF1E88E5u;               // copy: 蓝
    if (act & 2u)      bg = 0xFF43A047u;     // move: 绿
    else if (act & 4u) bg = 0xFF00838Fu;     // link: 青
    else if (!act)     bg = 0xFFE53935u;     // nodrop: 红
    const int cx = w / 2, cy = h / 2;
    int r = (w < h ? w : h) / 2 - 1;
    const uint32_t fg = 0xFFFFFFFFu;
    const int ri2 = r * r;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            uint32_t c = (d2 <= ri2) ? bg : 0u;
            if (c)
            {
                int on = 0;
                if (!act)
                    on = (dx + dy <= 2 && dx + dy >= -2);       // 斜杠 /
                else if (act & 1u)
                    on = (dx >= -6 && dx <= 6 && dy >= -2 && dy <= 2) ||
                         (dy >= -6 && dy <= 6 && dx >= -2 && dx <= 2); // 十字
                else if (act & 2u)
                    on = (dy >= -2 && dy <= 2 && dx >= -6 && dx <= 2) ||  // 箭杆
                         (dx >= 1 && dx <= 7 && dy >= -(8 - dx) && dy <= (8 - dx));
                else
                    on = ((dx + 3) * (dx + 3) + dy * dy <= 14 &&
                          (dx + 3) * (dx + 3) + dy * dy >= 4) ||
                         ((dx - 3) * (dx - 3) + dy * dy <= 14 &&
                          (dx - 3) * (dx - 3) + dy * dy >= 4);        // 双环
                if (on)
                    c = fg;
            }
            p[y * w + x] = c;
        }
}

/* 取一个可以安全写像素的 slot：尚未创建、或合成器已归还且不是当前图。
 * 8 块全忙（合成器压着没 release）时返回 NULL，调用方本轮跳过、下轮再试。 */
static WlBuf *drag_icon_take_slot(void)
{
    for (int i = 0; i < WL_DRAG_ICON_SLOTS; ++i)
    {
        WlBuf *b = &s_dragIconBufs[i];
        if (i >= s_dragIconNSlots)
        {
            if (!create_buf(b, s_dragIconW, s_dragIconH))
                return NULL;
            s_dragIconNSlots = i + 1;
            return b;
        }
        if (!b->buffer)
        {
            if (create_buf(b, s_dragIconW, s_dragIconH))
                return b;
            continue;
        }
        if (!b->busy && b != s_dragIconLive)
            return b;
    }
    return NULL;
}

static int drag_icon_commit(struct wl_surface *surface, WlBuf *b, int ox, int oy)
{
    if (!surface || !b || !b->buffer)
        return 0;
    /* drag-icon 没有 hotspot 参数：用 attach 的负偏移把热点对准指针。 */
    wl_surface_attach(surface, b->buffer, ox, oy);
    wl_surface_damage(surface, 0, 0, s_dragIconW, s_dragIconH);
    wl_surface_commit(surface);
    b->busy = 1;
    s_dragIconLive = b;
    return 1;
}

/* 拖放图标必须用全新无角色的 wl_surface：start_drag 会把它赋成 drag-icon
 * 角色，复用 cursor surface 或窗口 surface 会触发 wl_surface.role 协议错误。
 * 一次性图标：创建后 attach+commit，再交给 start_drag；拖动结束后销毁。 */
static struct wl_surface *drag_icon_create(int w, int h)
{
    if (!g.compositor || !g.shm)
        return NULL;
    s_dragIconW = w;
    s_dragIconH = h;
    s_dragIconNSlots = 0;
    s_dragIconLive = NULL;
    s_dragIconHotX = 0;
    s_dragIconHotY = 0;
    memset(s_dragIconBufs, 0, sizeof(s_dragIconBufs));
    struct wl_surface *surface = wl_compositor_create_surface(g.compositor);
    if (!surface)
        return NULL;
    WlBuf *b = drag_icon_take_slot();
    if (!b)
    {
        wl_surface_destroy(surface);
        return NULL;
    }
    drag_icon_paint(b->bits, w, h, 1u); // 起始占位：copy 徽标
    if (!drag_icon_commit(surface, b, -w / 2, -h / 2))
    {
        wl_surface_destroy(surface);
        return NULL;
    }
    return surface;
}

static void drag_icon_destroy(struct wl_surface *surface)
{
    if (surface)
        wl_surface_destroy(surface);
    for (int i = 0; i < s_dragIconNSlots; ++i)
        destroy_buf(&s_dragIconBufs[i]);
    s_dragIconNSlots = 0;
    s_dragIconLive = NULL;
}

/* X11 的 give_feedback() 用 SetCursor(IDC_COPY/MOVE/LINK/NODROP) 表达目标反馈；
 * Wayland DnD grab 期间合成器忽略 wl_pointer.set_cursor，唯一可见的载体是
 * drag icon，所以这里把同一份系统拖放光标位图交给图标显示，热点用 attach
 * 偏移对齐——语义与 X11 完全一致，只是载体从 pointer cursor 换成 drag icon。
 * 返回值：1 = 已经上屏（或本就相同），0 = 本帧没送出去（调用方下轮重试）。 */
int SwinxWlDragIconImage(const uint32_t *bits, int w, int h, int hotX, int hotY)
{
    if (!s_dragIcon || !g.ready || !bits || w < 1 || h < 1)
        return 0;
    if (w != s_dragIconW || h != s_dragIconH)
    {
        /* 不同 IDC 光标尺寸可能不同：整组 slot 重建（旧 surface 内容会由
         * 下一次 attach 覆盖）。 */
        for (int i = 0; i < s_dragIconNSlots; ++i)
            destroy_buf(&s_dragIconBufs[i]);
        memset(s_dragIconBufs, 0, sizeof(s_dragIconBufs));
        s_dragIconNSlots = 0;
        s_dragIconLive = NULL;
        s_dragIconW = w;
        s_dragIconH = h;
    }
    WlBuf *live = s_dragIconLive;
    size_t n = (size_t)w * (size_t)h * 4;
    if (live && live->bits && live->bytes == n &&
        s_dragIconHotX == hotX && s_dragIconHotY == hotY &&
        memcmp(live->bits, bits, n) == 0)
        return 1; // 图与位置都没变：不打扰合成器
    WlBuf *b = drag_icon_take_slot();
    if (!b)
        return 0;
    memcpy(b->bits, bits, n);
    s_dragIconHotX = hotX;
    s_dragIconHotY = hotY;
    drag_icon_commit(s_dragIcon, b, -hotX, -hotY);
    wl_display_flush(g.display);
    return 1;
}

// frame callback：commit 后冻结，收到 done 才解冻并补交下一帧（GTK thaw 语义）。
static void frame_done(void *data, struct wl_callback *cb, uint32_t /*time*/)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    if (s->frameCb == cb)
        s->frameCb = NULL;
    wl_callback_destroy(cb);
    s->framePending = 0;
    if (s->visible && s->srcBits && surface_has_damage(s))
        surface_flush(s);
}

static const struct wl_callback_listener frame_listener = {
    frame_done,
};

// 把一块 surface 的累积脏区真正提交。每窗一个 frame callback 节流：
// framePending 期间不重复 commit，等 done 再补。
static void surface_flush(SwinxWlSurface *s)
{
    if (!g.ready || !s || !s->surface || !s->w || !s->h)
        return;
    if (!s->visible || s->framePending || !s->srcBits)
        return;
    /* xdg-shell 协议：首个 attach buffer 的 commit 必须在 ack_configure 之后。
     * 创建时的裸 commit 只用来触发 configure，不 attach。subsurface 无此约束。
     * toplevel 与 popup 都是 xdg_surface 角色，同样要等 configure。 */
    if ((s->role == SWINX_WL_SURF_TOPLEVEL || s->role == SWINX_WL_SURF_POPUP) &&
        !s->configured)
        return;
    /* popup 没有 anchor 之前不能上屏：xdg_popup 的位置一旦创建就固定，
     * 没 anchor 就 commit 会让合成器把 popup 摆在 (0,0) 或整个拒绝。 */
    if (s->role == SWINX_WL_SURF_POPUP && !s->popup)
        return;

    WlBuf *b = NULL;
    for (int i = 0; i < s->nBufs; ++i)
    {
        if (!s->bufs[i].busy)
        {
            b = &s->bufs[i];
            break;
        }
    }
    if (!b && s->nBufs < SWINX_WL_MAX_BUFS)
    {
        b = &s->bufs[s->nBufs];
        if (create_buf(b, s->w, s->h))
            ++s->nBufs;
        else
            b = NULL;
    }
    if (!b)
        return; // 全部在合成器手里：等 release 回调再试

    RECT full = { 0, 0, s->w, s->h };
    const RECT *dirty = s->needFull ? &full : (b->hasDamage ? &b->damage : NULL);
    if (!dirty)
        return;
    copy_rect(b, s->srcBits, s->srcStride, dirty, s->w, s->h);

#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    /* 声明 buffer 在合成器逻辑坐标系里的尺寸 = 物理像素 / 倍率。
     * e.g. 1.5 倍屏幕：SOUI 以 144dpi 渲染出 1200x900 的 buffer，声明
     * destination 800x600，合成器放大 1.5 倍后正好 1:1 落在物理像素上。 */
    if (s->viewport && scaleEnabled() && s->scale120 > 0)
    {
        int dsx = (s->w * 120 + s->scale120 / 2) / s->scale120;
        int dsy = (s->h * 120 + s->scale120 / 2) / s->scale120;
        if (dsx < 1) dsx = 1;
        if (dsy < 1) dsy = 1;
        if (dsx != s->vpDstW || dsy != s->vpDstH)
        {
            wp_viewport_set_destination(s->viewport, dsx, dsy);
            s->vpDstW = dsx;
            s->vpDstH = dsy;
        }
    }
#endif
    wl_surface_attach(s->surface, b->buffer, 0, 0);
    /* damage 是 surface 局部（逻辑）坐标，viewport 生效时要把物理脏区缩小。 */
    wl_surface_damage(s->surface,
                      scaleScalar(s, dirty->left),
                      scaleScalar(s, dirty->top),
                      scaleScalar(s, dirty->right - dirty->left),
                      scaleScalar(s, dirty->bottom - dirty->top));
    if (!s->frameCb)
    {
        s->frameCb = wl_surface_frame(s->surface);
        wl_callback_add_listener(s->frameCb, &frame_listener, s);
    }
    wl_surface_commit(s->surface);
    b->busy = 1;
    b->hasDamage = 0;
    s->needFull = 0;
    s->framePending = 1;
    ++s_presents;
}

// ---------------------------------------------------------------------------
// xdg-shell：configure / ack / close
// ---------------------------------------------------------------------------

static void wm_base_ping(void *, struct xdg_wm_base *wb, uint32_t serial)
{
    xdg_wm_base_pong(wb, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { wm_base_ping };

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    xdg_surface_ack_configure(xs, serial);
    s->configured = 1;
    /* 首个 present 可能早于 configure 到达（创建→WM_PAINT→present 都同步完成，
     * 而 configure 要等下一轮事件泵）。那时 surface_flush 被 configured 闸住，
     * 若不再有绘制就永远不上屏。configure 一到就立刻补一次提交。 */
    surface_flush(s);
    if (s->role == SWINX_WL_SURF_TOPLEVEL)
    {
        int w = s->cfgW > 0 ? s->cfgW : s->w;
        int h = s->cfgH > 0 ? s->cfgH : s->h;
        s->cfgW = 0;
        s->cfgH = 0;
        /* configure 的 w/h 是合成器逻辑坐标（viewport destination 空间）；
         * SOUI 的 SetWindowSize 收的是物理像素，必须乘倍率，否则缩放环境下
         * 窗口每 configure 一次就缩小一次（设了 viewport 才需要换算）。 */
        if (s->vpDstW > 0 && s->viewport)
        {
            w = w * s->scale120 / 120;
            h = h * s->scale120 / 120;
        }
        if (w > 0 && h > 0)
            emit_event(SWINX_WL_EVT_CONFIGURE, s->hwnd, w, h, 0, 0);
    }
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *,
                               int32_t w, int32_t h, struct wl_array *states)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    if (w > 0 && h > 0)
    {
        s->cfgW = w;
        s->cfgH = h;
    }
    unsigned int st = 0;
    if (states)
    {
        const uint32_t *p = (const uint32_t *)states->data;
        for (size_t i = 0; i + sizeof(uint32_t) <= states->size; ++i, ++p)
        {
#ifdef XDG_TOPLEVEL_STATE_MAXIMIZED
            if (*p == XDG_TOPLEVEL_STATE_MAXIMIZED)  st |= SWINX_WL_TOPSTATE_MAXIMIZED;
#endif
#ifdef XDG_TOPLEVEL_STATE_MINIMIZED
            if (*p == XDG_TOPLEVEL_STATE_MINIMIZED)  st |= SWINX_WL_TOPSTATE_MINIMIZED;
#endif
#ifdef XDG_TOPLEVEL_STATE_FULLSCREEN
            if (*p == XDG_TOPLEVEL_STATE_FULLSCREEN) st |= SWINX_WL_TOPSTATE_FULLSCREEN;
#endif
        }
    }
    s->toplevelStates = st;
    emit_event(SWINX_WL_EVT_TOPLEVEL_STATE, s->hwnd, (int)st, 0, 0, 0);
}

static void toplevel_close(void *data, struct xdg_toplevel *)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    emit_event(SWINX_WL_EVT_CLOSE, s->hwnd, 0, 0, 0, 0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close,
};

// xdg_popup：GTK gdkwindow-wayland.c:2267 xdg_popup_configure + :2280 done。
// popup 的 xdg_surface.configure 仍走上面的 xdg_surface_configure（ack + flush），
// 这里只处理 popup 特有的最终位置回告与"合成器要求关闭"。
static void xdg_popup_configure(void *data, struct xdg_popup *,
                                int32_t x, int32_t y, int32_t w, int32_t h)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    /* popup configure 的坐标是父 surface 逻辑坐标，回投给 SOUI 要换物理像素。 */
    SwinxWlSurface *pref = s->popParent ? s->popParent : s;
    emit_event(SWINX_WL_EVT_POPUP_CONFIGURE, s->hwnd,
               scaleScalar(pref, (int)x, true), scaleScalar(pref, (int)y, true),
               scaleScalar(pref, (int)w, true), scaleScalar(pref, (int)h, true));
}

static void xdg_popup_done(void *data, struct xdg_popup *)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    emit_event(SWINX_WL_EVT_POPUP_DONE, s->hwnd, 0, 0, 0, 0);
}

static const struct xdg_popup_listener xdg_popup_listener = {
    xdg_popup_configure,
    xdg_popup_done,
};

// ---------------------------------------------------------------------------
// 输入：wl_seat / wl_pointer / wl_keyboard（GTK gdkdevice-wayland.c 同款路径）
// ---------------------------------------------------------------------------

/* 把挂起的 leave 定案：这是"指针真的离开了这个窗口"的唯一时刻。合成器
 * repick 产生的同窗口 leave 会在 pointer_enter 里被丢弃，所以能走到这里的
 * 都是跨窗口的真实离开。 */
static void leave_finalize(WlSeat *seat)
{
    if (!seat->leavePending)
        return;
    seat->leavePending = 0;
    if (seat->focus)
        emit_event(SWINX_WL_EVT_LEAVE, seat->focus, 0, 0, 0, 0);
    seat->focus = NULL;
    /* xdg_toplevel_move/resize 期间合成器会 grab pointer：客户端先收到
     * wl_pointer.leave，拖动期间的 button release 不保证还会送来；如果保留
     * LEFT/RIGHT/MIDDLE 位，重新 enter 后的 motion 会一直带 MK_LBUTTON，
     * swinx 的 WM_MOUSEMOVE 便跳过 WM_NCHITTEST，htCode 永远停在旧 resize
     * edge，下一次在 caption 上按下仍触发 xdg_toplevel_resize。 */
    seat->state &= ~(SWINX_WL_STATE_LEFT | SWINX_WL_STATE_RIGHT | SWINX_WL_STATE_MIDDLE);
    /* 这里**不能**调 wl_pointer.set_cursor(NULL)：协议里 null surface 的语义是
     * "隐藏光标"，不是"交还合成器"。指针离开本 surface 后，新 surface 的客户端
     * （或合成器默认）自然会接管光标，我们什么都不用做。在 leave 时发 null 会让
     * 光标在窗口交界处闪一下"消失"，正是"鼠标移出后光标不见了"的来源。 */
}

/* 本批事件分发结束：没有配对 enter 的 leave 就是真的离开了窗口。 */
static void pointer_flush_leaves(void)
{
    for (int i = 0; i < g.nSeats; ++i)
        leave_finalize(&g.seats[i]);
}

/* 坐标换算：SOUI 画布/buffer 用物理像素；wp_viewport 生效时合成器的
 * surface 局部坐标是逻辑像素（destination 空间）。
 *   收自合成器（pointer/configure/dnd）：逻辑 -> 物理，乘倍率；
 *   发给合成器（damage/subsurface/input region/positioner）：物理 -> 逻辑，除倍率。
 * 没有 viewport 时 compositor 直接按 buffer 像素走，两个方向都是恒等变换。 */
static void scaleSurfaceCoords(void *hwnd, int &x, int &y, bool toPhysical = true)
{
    if (!scaleEnabled())
        return;
    SwinxWlSurface *s = find_surface(hwnd);
    if (!s || !s->viewport || s->scale120 <= 0)
        return;
    if (toPhysical)
    {
        x = x * s->scale120 / 120;
        y = y * s->scale120 / 120;
    }
    else
    {
        x = x * 120 / s->scale120;
        y = y * 120 / s->scale120;
    }
}

/* 单个标量版本（damage 宽高、inset、anchor 尺寸等）。 */
static int scaleScalar(SwinxWlSurface *s, int v, bool toPhysical)
{
    if (!scaleEnabled() || !s || !s->viewport || s->scale120 <= 0)
        return v;
    return toPhysical ? v * s->scale120 / 120 : v * 120 / s->scale120;
}

/* subsurface 的位置是父 surface 的局部（逻辑）坐标。用 parent 的倍率换算
 * （同一窗口树一般同屏同倍率，父为空时退回自己）。 */
static void subsurfaceSetPosition(SwinxWlSurface *s, int x, int y)
{
    if (!s || !s->subsurface)
        return;
    SwinxWlSurface *ref = s->parent ? s->parent : s;
    wl_subsurface_set_position(s->subsurface,
                               scaleScalar(ref, x), scaleScalar(ref, y));
}

static void pointer_enter(void *data, struct wl_pointer *,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    WlSeat *seat = (WlSeat *)data;
    void *newFocus = surface ? wl_surface_get_user_data(surface) : NULL;
    /* 挂起的 leave 与这次 enter 配成一对再处理：
     * 同一窗口（合成器 repick——mutter 在 subsurface 提交、栈序变化、输入区
     * 更新后都会重算焦点，两块事件甚至同坐标）⇒ 丢弃 leave，指针其实从未
     * 离开，按键状态 / WM_MOUSELEAVE / 光标图案都不该动；
     * 不同窗口 ⇒ 旧窗补发真实的离开。
     * 必须先处理挂起项再改 enterSerial：交还光标要用旧 surface 的 enter serial。 */
    if (seat->leavePending)
    {
        if (seat->focus == newFocus)
        {
            seat->leavePending = 0;
        }
        else
        {
            leave_finalize(seat);
        }
    }
    g.lastSerial = serial;
    /* wl_pointer_set_cursor 必须用最近一次 enter 的 serial；漏了这一句，
     * 严格合成器（Mutter/KWin）会静默忽略所有换光标请求。 */
    seat->enterSerial = serial;
    seat->focus = newFocus;
    seat->fx = (int)wl_fixed_to_double(sx);
    seat->fy = (int)wl_fixed_to_double(sy);
    scaleSurfaceCoords(seat->focus, seat->fx, seat->fy);
    cursor_apply(seat); // 跨 surface 时重设（GTK pointer_handle_enter 同款）
    if (seat->focus)
        emit_event(SWINX_WL_EVT_ENTER, seat->focus, seat->fx, seat->fy, seat->state, 0);
}

static void pointer_leave(void *data, struct wl_pointer *,
                          uint32_t serial, struct wl_surface *)
{
    WlSeat *seat = (WlSeat *)data;
    g.lastSerial = serial;
    /* 只登记、不立刻按"离开窗口"处理：定案有两个时机——同一批事件里紧跟着
     * 来了 enter（pointer_enter 里配对），或者本批分发结束（SwinxWlDispatch
     * 末尾调 leave_finalize）。立刻处理会在合成器 repick 时误清 MK_LBUTTON
     * （拖动中断）、误发 WM_MOUSELEAVE（tooltip 重计时 / hover 丢失），还会
     * 把光标交还合成器造成一次可见闪烁——表现就是"指针还在窗口内，形状却
     * 变了"。**不能**把 leave 的 serial 写进 enterSerial：它不是任何 surface
     * 的 enter serial，之后 cursor_apply() 拿它去 set_cursor 会被严格合成器
     * 拒绝，光标停在旧图案上。 */
    seat->leavePending = 1;
}

static void pointer_motion(void *data, struct wl_pointer *,
                           uint32_t, wl_fixed_t sx, wl_fixed_t sy)
{
    WlSeat *seat = (WlSeat *)data;
    seat->fx = (int)wl_fixed_to_double(sx);
    seat->fy = (int)wl_fixed_to_double(sy);
    scaleSurfaceCoords(seat->focus, seat->fx, seat->fy);
    if (seat->focus)
        emit_event(SWINX_WL_EVT_MOTION, seat->focus, seat->fx, seat->fy, seat->state, 0);
}

static void pointer_button(void *data, struct wl_pointer *, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state)
{
    WlSeat *seat = (WlSeat *)data;
    g.lastSerial = serial;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        seat->pressSerial = serial; // GTK gdkdevice-wayland.c:1734 press_serial
    int btn = button == BTN_LEFT ? 1 : (button == BTN_RIGHT ? 2 :
              (button == BTN_MIDDLE ? 3 : 0));
    int bit = button == BTN_LEFT ? SWINX_WL_STATE_LEFT :
              (button == BTN_RIGHT ? SWINX_WL_STATE_RIGHT :
               (button == BTN_MIDDLE ? SWINX_WL_STATE_MIDDLE : 0));
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        seat->state |= bit;
    else
        seat->state &= ~bit;
    if (seat->focus && btn)
    {
        /* 带上协议时间戳：双击判定在 SConnection 侧做（Wayland 不像 X11 那样
         * 由服务端发 DoubleClick 事件，也不像 Win32 由系统合成 DBLCLK）。 */
        emit_event(SWINX_WL_EVT_BUTTON, seat->focus, btn,
                   state == WL_POINTER_BUTTON_STATE_PRESSED ? 1 : 0,
                   seat->fx, seat->fy, time);
        emit_event(SWINX_WL_EVT_MOTION, seat->focus, seat->fx, seat->fy, seat->state, 0);
    }
}

static void pointer_axis(void *data, struct wl_pointer *, uint32_t,
                         uint32_t axis, wl_fixed_t value)
{
    WlSeat *seat = (WlSeat *)data;
    int v = (int)(wl_fixed_to_double(value) * 120.0);
    if (seat->focus)
    {
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
            emit_event(SWINX_WL_EVT_AXIS, seat->focus, 0, v, seat->fx, seat->fy);
        else
            emit_event(SWINX_WL_EVT_AXIS, seat->focus, v, 0, seat->fx, seat->fy);
    }
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    NULL, // axis_source
    NULL, // axis_stop
    NULL, // axis_discrete
};

#if defined(SWINX_HAVE_TEXT_INPUT_V3)
/* text-input-v3：commit_string 与 preedit_string 一样是按 done 批量提交的
 * 状态机。这里先只桥接 commit_string → WM_IME_CHAR（X11 xim_commit_string
 * 同款路径），预编辑内联留给后续；preedit/delete 事件只清空/忽略。 */
static void text_input_enter(void *data, struct zwp_text_input_v3 *ti,
                             struct wl_surface *)
{
    /* leave 之后合成器会忽略请求，必须在新 surface 上重新 enable 并补光标矩形。 */
    WlSeat *seat = (WlSeat *)data;
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_cursor_rectangle(ti, seat->imeRectX, seat->imeRectY,
                                           seat->imeRectW, seat->imeRectH);
    zwp_text_input_v3_commit(ti);
}
static void text_input_leave(void *data, struct zwp_text_input_v3 *ti,
                             struct wl_surface *)
{
    (void)data;
    zwp_text_input_v3_disable(ti);
    zwp_text_input_v3_commit(ti);
}
static void text_input_preedit(void *data, struct zwp_text_input_v3 *,
                               const char *, int32_t, int32_t) { (void)data; }
static void text_input_commit_string(void *data, struct zwp_text_input_v3 *,
                                     const char *text)
{
    WlSeat *seat = (WlSeat *)data;
    free(seat->imeCommit);
    seat->imeCommit = text ? strdup(text) : NULL;
}
static void text_input_delete_surrounding(void *data, struct zwp_text_input_v3 *,
                                          uint32_t, uint32_t) { (void)data; }
static void text_input_done(void *data, struct zwp_text_input_v3 *, uint32_t serial)
{
    WlSeat *seat = (WlSeat *)data;
    (void)serial; // commit 次数；这里不做 serial 校验
    if (seat->imeCommit && *seat->imeCommit)
        emit_ime_event(SWINX_WL_EVT_IME_COMMIT, seat->focus,
                       seat->imeCommit, (int)serial);
    else
        free(seat->imeCommit);
    seat->imeCommit = NULL;
}
static const struct zwp_text_input_v3_listener text_input_listener = {
    text_input_enter,
    text_input_leave,
    text_input_preedit,
    text_input_commit_string,
    text_input_delete_surrounding,
    text_input_done,
};

static void ime_ensure(WlSeat *seat)
{
    if (!g.textInputManager || !seat || !seat->seat || seat->textInput)
        return;
    seat->textInput = zwp_text_input_manager_v3_get_text_input(
        g.textInputManager, seat->seat);
    if (!seat->textInput)
        return;
    zwp_text_input_v3_add_listener(seat->textInput, &text_input_listener, seat);
    zwp_text_input_v3_enable(seat->textInput);
    zwp_text_input_v3_commit(seat->textInput);
}

void SwinxWlImeSetEnabled(int enabled)
{
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *s = &g.seats[i];
        if (!s->textInput)
            continue;
        if (enabled)
            zwp_text_input_v3_enable(s->textInput);
        else
            zwp_text_input_v3_disable(s->textInput);
        zwp_text_input_v3_commit(s->textInput);
    }
#else
    (void)enabled;
#endif
}

void SwinxWlImeSetCursorRect(int x, int y, int w, int h)
{
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *s = &g.seats[i];
        if (!s->textInput)
            continue;
        s->imeRectX = x; s->imeRectY = y; s->imeRectW = w; s->imeRectH = h;
        zwp_text_input_v3_set_cursor_rectangle(s->textInput, x, y, w, h);
        zwp_text_input_v3_commit(s->textInput);
    }
#else
    (void)x; (void)y; (void)w; (void)h;
#endif
}
#endif // SWINX_HAVE_TEXT_INPUT_V3

static void keyboard_keymap(void *, struct wl_keyboard *, uint32_t, int32_t, uint32_t)
{
    // 静态 evdev scancode -> VK 表在 SConnection 侧；这里不需要 xkbcommon。
}

static void keyboard_enter(void *data, struct wl_keyboard *, uint32_t,
                           struct wl_surface *surface, struct wl_array *)
{
    WlSeat *seat = (WlSeat *)data;
    seat->focus = surface ? wl_surface_get_user_data(surface) : NULL;
}

static void keyboard_leave(void *data, struct wl_keyboard *, uint32_t,
                           struct wl_surface *)
{
    WlSeat *seat = (WlSeat *)data;
    seat->focus = NULL;
}

static void keyboard_key(void *data, struct wl_keyboard *, uint32_t serial,
                         uint32_t, uint32_t key, uint32_t state)
{
    WlSeat *seat = (WlSeat *)data;
    g.lastSerial = serial;
    if (seat->focus)
        emit_event(SWINX_WL_EVT_KEY, seat->focus, (int)key,
                   state == WL_KEYBOARD_KEY_STATE_PRESSED ? 1 : 0, 0, 0);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *, uint32_t serial,
                               uint32_t mods_depressed, uint32_t,
                               uint32_t mods_locked, uint32_t group)
{
    WlSeat *seat = (WlSeat *)data;
    g.lastSerial = serial;
    (void)group;
    int st = 0;
    if (mods_depressed & 1) st |= SWINX_WL_STATE_SHIFT;
    if (mods_depressed & 4) st |= SWINX_WL_STATE_CTRL;
    if (mods_depressed & 8) st |= SWINX_WL_STATE_ALT;
    if (mods_locked & 2) st |= SWINX_WL_STATE_CAPS;
    seat->state = (seat->state & 7) | st; // 低 3 位留给鼠标
    if (seat->focus)
        emit_event(SWINX_WL_EVT_MODIFIERS, seat->focus, st, 0, 0, 0);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    WlSeat *s = (WlSeat *)data;
    s->caps = caps;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer)
    {
        s->pointer = wl_seat_get_pointer(seat);
        wl_pointer_set_user_data(s->pointer, s);
        wl_pointer_add_listener(s->pointer, &pointer_listener, s);
        cursor_ensure_surface(s);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer)
    {
        wl_pointer_destroy(s->pointer);
        s->pointer = NULL;
        cursor_destroy(s);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard)
    {
        s->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_set_user_data(s->keyboard, s);
        wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
    }
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && s->keyboard)
    {
        wl_keyboard_destroy(s->keyboard);
        s->keyboard = NULL;
    }
}

static void seat_name(void *, struct wl_seat *, const char *) {}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

// ---------------------------------------------------------------------------
// wl_output：SOUI 的 CenterWindow / MonitorFromRect 要的是真实输出尺寸。
// 不能用首个窗口的 configure 尺寸冒充屏幕：MessageBox 先创建时屏幕会变成
// 373x212，主窗随后变成 888x650，居中计算全错（对齐 GTK gdkmonitor-wayland.c
// 订阅 geometry/mode/scale 的做法）。
// ---------------------------------------------------------------------------

static void output_geometry(void *data, struct wl_output *,
                            int32_t x, int32_t y, int32_t, int32_t,
                            int32_t, const char *, const char *, int32_t transform)
{
    WlOutput *o = (WlOutput *)data;
    o->x = x; o->y = y; o->transform = transform;
}

static void output_mode(void *data, struct wl_output *, uint32_t flags,
                        int32_t w, int32_t h, int32_t)
{
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;
    WlOutput *o = (WlOutput *)data;
    /* transform 90/270 表示合成器的逻辑坐标系与面板像素方向 swapped。 */
    if (o->transform == WL_OUTPUT_TRANSFORM_90 ||
        o->transform == WL_OUTPUT_TRANSFORM_270 ||
        o->transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
        o->transform == WL_OUTPUT_TRANSFORM_FLIPPED_270)
    {
        int t = w; w = h; h = t;
    }
    o->w = w; o->h = h; o->hasMode = 1;
}

static void output_done(void *, struct wl_output *) {}
static void output_scale(void *data, struct wl_output *, int32_t factor)
{
    ((WlOutput *)data)->scale = factor > 0 ? factor : 1;
}

static const struct wl_output_listener output_listener = {
    output_geometry,
    output_mode,
    output_done,
    output_scale,
};

#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
// wp_fractional_scale_v1.preferred_scale：120=1.0，180=1.5，240=2.0。
// 这是合成器对"这块 surface 应该按多少倍率渲染"的权威回答；GNOME 在
// 125%/150% 下 wl_output.scale 只能报 2，必须靠这里拿真实倍率。
static void fractional_scale_preferred(void *data,
                                        struct wp_fractional_scale_v1 *,
                                        uint32_t scale)
{
    SwinxWlSurface *s = (SwinxWlSurface *)data;
    if (!s || scale < 60)
        return; // 协议规定 >=1，防御 0
    int old = s->scale120;
    s->scale120 = (int)scale;
    s->scaleFromFractional = 1;
    if (wl_trace() && old != s->scale120)
        fprintf(stderr, "[wl-scale] hwnd=%p preferred=%d/120 (%.3f)\n",
                s->hwnd, s->scale120, s->scale120 / 120.0);
    /* 倍率变化后 destination 要按新倍率重算，并把已有 damage 整幅重画，
     * 否则合成器仍按旧 destination 摆放，窗口大小不跟随缩放变化。 */
    if (old != s->scale120)
        s->needFull = 1;
}
static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    fractional_scale_preferred,
};

/* 启动探针：在第一个窗口创建前（SDpiHandler 首次读 DPI 之前）拿到合成器
 * 对主屏的倍率。否则首帧按 wl_output.scale（150% 下是 2）放大到 200%，
 * preferred_scale(180) 到得太晚，窗口就永久停在错误的倍率上。 */
static void probe_preferred_scale(void *, struct wp_fractional_scale_v1 *, uint32_t scale)
{
    if (scale >= 60)
        s_probeScale120 = (int)scale;
}
static const struct wp_fractional_scale_v1_listener probe_scale_listener = {
    probe_preferred_scale,
};
#endif

int SwinxWlGetScreenSize(int *width, int *height)
{
    RECT rc;
    if (!SwinxWlGetOutputRect(0, &rc))
        return 0;
    if (width) *width = rc.right - rc.left;
    if (height) *height = rc.bottom - rc.top;
    return 1;
}

int SwinxWlGetOutputCount(void)
{
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    int n = 0;
    for (int i = 0; i < g.nOutputs; ++i)
        if (g.outputs[i].hasMode)
            ++n;
    return n ? n : 1; // 输出还没到时按单屏兜底（对齐旧行为）
#else
    return 1;
#endif
}

int SwinxWlGetOutputRect(int index, RECT *rc)
{
    if (rc)
    {
        rc->left = rc->top = 0;
        rc->right = SWINX_WL_DEFAULT_W;
        rc->bottom = SWINX_WL_DEFAULT_H;
    }
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    if (index < 0)
        index = 0;
    for (int i = 0; i < g.nOutputs; ++i)
    {
        WlOutput &o = g.outputs[i];
        if (!o.hasMode || o.w <= 0 || o.h <= 0)
            continue;
        if (index-- > 0)
            continue;
        int s120 = outputScale120(&o);
        if (rc)
        {
            /* wl_output.geometry 的 x/y 已是合成器逻辑坐标（不要除倍率），
             * 只有 mode 的 w/h 是物理像素，需 /倍率 换成逻辑尺寸。
             * 用 120 定点数算，避免 1.25/1.5 的浮点截断把 1366 算成 1365。 */
            rc->left = o.x;
            rc->top = o.y;
            rc->right = o.x + o.w * 120 / s120;
            rc->bottom = o.y + o.h * 120 / s120;
        }
        return 1;
    }
#endif
    (void)index;
    return 0;
}

int SwinxWlGetOutputScale120(int index)
{
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    if (index < 0)
        index = 0;
    for (int i = 0; i < g.nOutputs; ++i)
    {
        if (!g.outputs[i].hasMode)
            continue;
        if (index-- > 0)
            continue;
        return outputScale120(&g.outputs[i]);
    }
#endif
    (void)index;
    return 120;
}

int SwinxWlGetSurfaceScale120(void *hwnd)
{
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    SwinxWlSurface *s = find_surface(hwnd);
    /* 必须同时有 viewport：没有 viewport 时合成器按 buffer 像素做整数缩放，
     * SOUI 若按倍率放大就会和合成器叠成两倍（4x 屏上表现为窗口巨大）。
     * 有 viewport 才能安全地按物理像素渲染并声明小一号的 destination。 */
    if (s && s->viewport && s->scale120 > 0)
        return s->scale120;
#endif
    (void)hwnd;
    return 120;
}

int SwinxWlGetSystemScale120(void)
{
#ifdef SWINX_HAVE_WAYLAND_CLIENT
    /* 没有 wp_viewporter 时所有 surface 都拿不到 viewport，缩放只能由
     * 合成器按 wl_output.scale 做，DPI 必须保持 96 才不会双重放大。 */
    if (!g.viewporter)
        return 120;
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    if (s_probeScale120 > 0)
        return s_probeScale120;
#endif
#endif
    return SwinxWlGetOutputScale120(0);
}

int SwinxWlGetDpi(void *hwnd)
{
    if (!scaleEnabled())
        return 96;
    /* SOUI 的 SDpiHandler 用 NormalizeScale 向下贴内置档位（100/125/150/
     * 200/250/300）。直接报 96*1.25=120 -> 归一回 100，1.5=144 -> 归一回
     * 125，都会偏小。这里主动向上贴到最近的**内置**档位，使归一化后不变。 */
    static const int kTiers[] = { 100, 125, 150, 200, 250, 300 };
    int s120 = hwnd ? SwinxWlGetSurfaceScale120(hwnd) : SwinxWlGetSystemScale120();
    if (s120 < 60)
        s120 = 120;
    int pct = (s120 * 100 + 119) / 120; // 实际百分比，向上取整
    for (int i = 0; i < (int)(sizeof(kTiers) / sizeof(kTiers[0])); ++i)
    {
        if (pct <= kTiers[i])
            return kTiers[i] * 96 / 100;
    }
    return 300 * 96 / 100;
}

// ---------------------------------------------------------------------------
// wl_data_device：selection（剪贴板）+ drag & drop
//
// GTK 同款模型：source 由本客户端提供 read 回调（compositor 在 send 事件里
// 给一个 fd，我们分块写）；offer 是别的客户端提供的数据，receive 拿 fd 后
// 必须在事件泵里读到 EOF —— 这是 Wayland 与 X11 最大的差别：GetClipboardData
// 不能再直接读一个 X window property，必须跑一次嵌套 dispatch。
// ---------------------------------------------------------------------------

static void data_source_send(void *data, struct wl_data_source *, const char *mime, int32_t fd)
{
    WlDataSource *src = (WlDataSource *)data;
    char buf[65536];
    /* One send event is one complete transfer. Signal the upstream read
     * callback to restart its chunk state (buf == NULL means reset),
     * otherwise a send that reached EOF would keep replaying the data. */
    if (src && src->read)
        src->read(src->ctx, mime, NULL, 0);
    for (int guard = 0; src && src->read && guard < (1 << 20); ++guard)
    {
        int n = src->read(src->ctx, mime, buf, (int)sizeof(buf));
        if (n <= 0)
            break;
        size_t off = 0;
        while (off < (size_t)n)
        {
            ssize_t w = (ssize_t)write(fd, buf + off, (size_t)n - off);
            if (w < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (w == 0)
                break;
            off += (size_t)w;
        }
        if (off != (size_t)n)
            break;
    }
    close(fd);
}

static void data_source_target(void *, struct wl_data_source *, const char *) {}
static void data_source_cancel(void *data, struct wl_data_source *)
{
    WlDataSource *src = (WlDataSource *)data;
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *s = &g.seats[i];
        if (s->dragSource == src)
        {
            /* mutter 在 finished 之后还会补一个 cancel；此时 drop 已经成功，
             * 不能再把 dragDropped 冲回 0（否则 DoDragDrop 返回 CANCEL）。 */
            if (!s->dragDone)
            {
                s->dragDone = 1;
                s->dragDropped = 0;
            }
        }
        if (s->selectionSource == src)
        {
            s->selectionSource = NULL;
            delete src;
        }
    }
}
#if defined(WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
static void data_source_drop_performed(void *, struct wl_data_source *) {}
static void data_source_finished(void *data, struct wl_data_source *)
{
    WlDataSource *src = (WlDataSource *)data;
    for (int i = 0; i < g.nSeats; ++i)
        if (g.seats[i].dragSource == src)
        {
            g.seats[i].dragDone = 1;
            g.seats[i].dragDropped = 1;
        }
}
static void data_source_action(void *data, struct wl_data_source *source, uint32_t action)
{
    WlDataSource *src = (WlDataSource *)data;
    /* mutter 将目标 set_actions 与修饰键合成后的 action 通过本事件回告源端；
     * 它就是 X11 里 XdndStatus 回写的 *pdwEffect。
     * 没有此回调，dragQuery 无法区分 COPY/MOVE/LINK/NODROP，拖放光标始终是固定的。 */
    uint32_t effect = 0;
    /* 核心协议 wl_data_device_manager.dnd_action 只有 none=0/copy=1/move=2/ask=4
     * —— **4 是 ask，不是 link**（link 是 X11 XdndAction 的概念，核心协议里没有）。
     * ask 表示"询问用户"，不属于 DROPEFFECT_*，映射成 0（禁止放下）。
     * 之前把 4 当 link 映射成 DROPEFFECT_LINK，导致目标端把 preferred 写成 4，
     * mutter 于是把 ASK 当作 current action 回给源端，拖动时显示"链接"光标，
     * 松手时 data_offer.finish 被 mutter 判为 "offer finished with an invalid
     * action"（data_offer_finish 明确拒绝 NONE/ASK），整个连接随之断开。 */
    if (action & 1) effect |= 1; // COPY -> DROPEFFECT_COPY
    if (action & 2) effect |= 2; // MOVE -> DROPEFFECT_MOVE
    for (int i = 0; i < g.nSeats; ++i)
    {
        if (g.seats[i].dragSource == src)
        {
            /* 只记数值：拖动图标的更新由 SConnection::dragQuery 的
             * GiveFeedback 驱动（与 X11 SDragDrop::give_feedback 同一条路径），
             * 这里再改图标会和它互相覆盖。 */
            g.seats[i].dragEffect = effect;
            wl_display_flush(g.display);
        }
    }
    (void)source;
}
#endif

static const struct wl_data_source_listener data_source_listener = {
    data_source_target,
    data_source_send,
    data_source_cancel,
#if defined(WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION)
    data_source_drop_performed,
    data_source_finished,
    data_source_action,
#endif
};

static void data_offer_mime(void *data, struct wl_data_offer *, const char *mime)
{
    WlDataOffer *o = (WlDataOffer *)data;
    if (o && mime && *mime)
        o->mimes.push_back(mime);
}
#if defined(WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
static void data_offer_actions(void *data, struct wl_data_offer *, uint32_t actions)
{
    WlDataOffer *o = (WlDataOffer *)data;
    if (o) o->sourceActions = actions;
}
static void data_offer_action(void *data, struct wl_data_offer *, uint32_t action)
{
    WlDataOffer *o = (WlDataOffer *)data;
    if (o) o->action = action;
}
#endif
static const struct wl_data_offer_listener data_offer_listener = {
    data_offer_mime,
#if defined(WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION)
    data_offer_actions,
    data_offer_action,
#endif
};

/* read_offer_to_heap 会在 receive 期间嵌套 wl_display_dispatch，leave/
 * selection 可能先于调用方返回就到达；单靠 destroyPending 标志无法区分
 * "读取结束时销毁"与"调用方随后还要 finish"两个持有者，曾导致同一
 * wl_data_offer 被 destroy 两次。改为引用计数：seat 与读取各持一份。 */
static void offer_destroy(WlDataOffer *o)
{
    if (!o || --o->refs > 0)
        return;
    if (o->offer)
        wl_data_offer_destroy(o->offer);
    delete o;
}

static void dnd_offer_destroy(WlDataOffer *o)
{
    if (!o)
        return;
    for (int i = 0; i < g.nSeats; ++i)
        if (g.seats[i].dndOffer == o)
        {
            g.seats[i].dndOffer = NULL;
            g.seats[i].dndTarget = NULL;
        }
    offer_destroy(o);
}

static WlDataOffer *data_device_new_offer(struct wl_data_offer *offer)
{
    WlDataOffer *o = new WlDataOffer();
    o->refs = 1;
    o->offer = offer;
    wl_data_offer_set_user_data(offer, o);
    wl_data_offer_add_listener(offer, &data_offer_listener, o);
    return o;
}

static void data_device_offer(void *data, struct wl_data_device *, struct wl_data_offer *offer)
{
    (void)data;
    data_device_new_offer(offer); // selection 事件会接管它；drop 事件亦同
}

static void data_device_selection(void *data, struct wl_data_device *, struct wl_data_offer *offer)
{
    WlSeat *seat = (WlSeat *)data;
    if (seat->selectionOffer)
    {
        offer_destroy(seat->selectionOffer);
        seat->selectionOffer = NULL;
    }
    if (offer)
    {
        WlDataOffer *o = (WlDataOffer *)wl_data_offer_get_user_data(offer);
        seat->selectionOffer = o ? o : data_device_new_offer(offer);
    }
    g.selectionOwned = g.selectionAwaitOwn;
    g.selectionAwaitOwn = 0;
    ++g.selectionGeneration;
}

static int wl_pointer_state_to_win(int state)
{
    int st = 0;
    if (state & SWINX_WL_STATE_LEFT)   st |= 1;  // MK_LBUTTON
    if (state & SWINX_WL_STATE_RIGHT)  st |= 2;
    if (state & SWINX_WL_STATE_MIDDLE) st |= 16;
    if (state & SWINX_WL_STATE_SHIFT)  st |= 4;
    if (state & SWINX_WL_STATE_CTRL)   st |= 8;
    if (state & SWINX_WL_STATE_ALT)    st |= 32;
    return st;
}

static void data_device_enter(void *data, struct wl_data_device *, uint32_t serial,
                              struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy,
                              struct wl_data_offer *offer)
{
    WlSeat *seat = (WlSeat *)data;
    WlDataOffer *o = offer ? (WlDataOffer *)wl_data_offer_get_user_data(offer) : NULL;
    if (!o && offer)
        o = data_device_new_offer(offer);
    if (!o)
        return;
    o->serial = serial;
    seat->dndOffer = o;
    seat->dndTarget = surface ? wl_surface_get_user_data(surface) : NULL;
    int dx = (int)wl_fixed_to_double(sx), dy = (int)wl_fixed_to_double(sy);
    scaleSurfaceCoords(seat->dndTarget, dx, dy);
    emit_dnd_event(SWINX_WL_EVT_DND_ENTER, seat->dndTarget,
                   dx, dy, wl_pointer_state_to_win(seat->state), o);
}

static void data_device_motion(void *data, struct wl_data_device *, uint32_t,
                               wl_fixed_t sx, wl_fixed_t sy)
{
    WlSeat *seat = (WlSeat *)data;
    int dx = (int)wl_fixed_to_double(sx), dy = (int)wl_fixed_to_double(sy);
    scaleSurfaceCoords(seat->dndTarget, dx, dy);
    emit_dnd_event(SWINX_WL_EVT_DND_MOTION, seat->dndTarget,
                   dx, dy, wl_pointer_state_to_win(seat->state), seat->dndOffer);
}

static void data_device_leave(void *data, struct wl_data_device *)
{
    WlSeat *seat = (WlSeat *)data;
    WlDataOffer *o = seat->dndOffer;
    emit_dnd_event(SWINX_WL_EVT_DND_LEAVE, seat->dndTarget, 0, 0, 0, NULL);
    seat->dndTarget = NULL;
    /* 读取 Drop 数据时 read_offer_to_heap 会嵌套 dispatch，leave 会在这里
     * 提前到达；此时只清 target，offer 那份引用留给随后的
     * SwinxWlDndOfferFinish 释放，避免调用方拿到已释放对象。 */
    if (o && !o->reading)
        dnd_offer_destroy(o);
}

static void data_device_drop(void *data, struct wl_data_device *)
{
    WlSeat *seat = (WlSeat *)data;
    emit_dnd_event(SWINX_WL_EVT_DND_DROP, seat->dndTarget, 0, 0, 0, seat->dndOffer);
}

static const struct wl_data_device_listener data_device_listener = {
    data_device_offer,
    data_device_enter,
    data_device_leave,
    data_device_motion,
    data_device_drop,
    data_device_selection,
};

static void data_devices_ensure(void)
{
    if (!g.dataDeviceManager)
        return;
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *s = &g.seats[i];
        if (!s->seat || s->dataDevice)
            continue;
        s->dataDevice = (struct wl_data_device *)wl_data_device_manager_get_data_device(
            g.dataDeviceManager, s->seat);
        if (s->dataDevice)
            wl_data_device_add_listener(s->dataDevice, &data_device_listener, s);
    }
}

static int read_offer_to_heap(WlDataOffer *o, const char *mime, void **outBuf)
{
    *outBuf = NULL;
    if (!o || !o->offer || !mime || !*mime || !g.display)
        return -1;
    ++o->reading;
    ++o->refs;  // read 自己持一份，leave/selection 先到也不会提前销毁
    int fds[2];
    if (pipe(fds) != 0)
    {
        --o->reading;
        offer_destroy(o);
        return -1;
    }
    wl_data_offer_receive(o->offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(g.display);
    std::vector<unsigned char> bytes;
    unsigned char tmp[8192];
    time_t t0 = time(NULL);
    for (;;)
    {
        int elapsed = (int)(time(NULL) - t0);
        if (elapsed >= 3)
            break;
        struct pollfd pfd[2];
        pfd[0].fd = fds[0];   pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = wl_display_get_fd(g.display);
        pfd[1].events = POLLIN; pfd[1].revents = 0;
        int r = poll(pfd, 2, (3 - elapsed) * 1000);
        if (r <= 0)
            break;
        if (pfd[1].revents & POLLIN)
            wl_display_dispatch(g.display);
        if (pfd[0].revents & (POLLIN | POLLHUP))
        {
            ssize_t n = (ssize_t)read(fds[0], tmp, sizeof(tmp));
            if (n > 0)
                bytes.insert(bytes.end(), tmp, tmp + n);
            else if (n == 0)
                break;
            else if (errno != EINTR)
                break;
        }
    }
    close(fds[0]);
    if (bytes.empty())
    {
        --o->reading;
        offer_destroy(o);
        return 0;
    }
    void *buf = NULL;
    if (!bytes.empty())
        buf = malloc(bytes.size());
    int n = buf ? (int)bytes.size() : -1;
    if (buf)
    {
        memcpy(buf, &bytes[0], bytes.size());
        *outBuf = buf;
    }
    --o->reading;
    offer_destroy(o);
    return n;
}

int SwinxWlSelectionSet(void *ctx, const char *const *mimes, int nMimes,
                        SwinxWlDataSourceRead read, unsigned int serial)
{
    if (!g.ready || !g.dataDeviceManager || g.nSeats < 1)
        return 0;
    WlSeat *seat = &g.seats[0];
    if (!seat->dataDevice)
        return 0;
    /* 替换 selection 时不能直接走 SwinxWlSelectionClear()：那会先发一次
     * set_selection(NULL)，合成器回告的 selection(NULL) 可能排在新 selection
     * 之后到达，导致刚发布的数据被误判成"别人抢走了剪贴板"。set_selection
     * 用新 source 覆盖旧的是协议允许的，这里只释放旧 source 对象。 */
    if (seat->selectionSource)
    {
        WlDataSource *oldSrc = seat->selectionSource;
        seat->selectionSource = NULL;
        if (oldSrc->src)
            wl_data_source_destroy(oldSrc->src);
        delete oldSrc;
    }
    WlDataSource *src = new WlDataSource();
    src->ctx = ctx;
    src->read = read;
    for (int i = 0; i < nMimes && mimes && mimes[i]; ++i)
        src->mimes.push_back(mimes[i]);
    src->src = (struct wl_data_source *)wl_data_device_manager_create_data_source(g.dataDeviceManager);
    if (!src->src)
    {
        delete src;
        return 0;
    }
    wl_data_source_add_listener(src->src, &data_source_listener, src);
    for (size_t i = 0; i < src->mimes.size(); ++i)
        wl_data_source_offer(src->src, src->mimes[i].c_str());
    /* wl_data_source.set_actions marks the source as a DnD source. mutter
     * rejects set_selection on such a source with a fatal protocol error
     * "cannot set drag-and-drop source as selection" and disconnects the
     * client. Only drag sources may call set_actions (see SwinxWlStartDrag).
     */
    seat->selectionSource = src;
    g.selectionAwaitOwn = 1;
    wl_data_device_set_selection(seat->dataDevice, src->src, serial ? serial : g.lastSerial);
    return 1;
}

void SwinxWlSelectionClear(void)
{
    if (!g.nSeats)
        return;
    WlSeat *seat = &g.seats[0];
    if (seat->dataDevice)
    {
        g.selectionAwaitOwn = 0;
        g.selectionOwned = 0;
        wl_data_device_set_selection(seat->dataDevice, NULL, g.lastSerial);
    }
    if (seat->selectionSource)
    {
        WlDataSource *src = seat->selectionSource;
        seat->selectionSource = NULL;
        if (src->src)
            wl_data_source_destroy(src->src);
        delete src;
    }
}

int SwinxWlSelectionHasMime(const char *mime)
{
    if (!mime || !*mime || !g.nSeats || !g.seats[0].selectionOffer)
        return 0;
    const std::vector<std::string> &v = g.seats[0].selectionOffer->mimes;
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == mime)
            return 1;
    return 0;
}

int SwinxWlSelectionRead(const char *mime, void **outBuf)
{
    if (!g.nSeats)
        return -1;
    return read_offer_to_heap(g.seats[0].selectionOffer, mime, outBuf);
}

unsigned int SwinxWlSelectionGeneration(void)
{
    return g.selectionGeneration;
}

int SwinxWlSelectionOwned(void)
{
    return g.selectionOwned;
}

void SwinxWlDataFree(void *p)
{
    free(p);
}

int SwinxWlStartDrag(void *ctx, const char *const *mimes, int nMimes,
                     SwinxWlDataSourceRead read, SwinxWlDragQuery query,
                     void *originHwnd, unsigned int serial)
{
    if (!g.ready || !g.dataDeviceManager || !g.nSeats)
        return 0;
    WlSeat *seat = &g.seats[0];
    if (!seat->dataDevice)
        return 0;
    SwinxWlSurface *origin = find_surface(originHwnd);
    if (!origin || !origin->surface)
        return 0;

    WlDataSource *src = new WlDataSource();
    src->ctx = ctx;
    src->read = read;
    src->isDrag = 1;
    for (int i = 0; i < nMimes && mimes && mimes[i]; ++i)
        src->mimes.push_back(mimes[i]);
    src->src = (struct wl_data_source *)wl_data_device_manager_create_data_source(g.dataDeviceManager);
    if (!src->src)
    {
        delete src;
        return 0;
    }
    wl_data_source_add_listener(src->src, &data_source_listener, src);
    for (size_t i = 0; i < src->mimes.size(); ++i)
        wl_data_source_offer(src->src, src->mimes[i].c_str());
#if defined(WL_DATA_SOURCE_SET_ACTIONS_SINCE_VERSION)
    /* 源端只声明 copy|move（3）：核心协议 dnd_action 的 4 是 ask 而不是 link，
     * 而 mutter 的 data_offer_finish 会拒绝 current action 为 NONE/ASK 的
     * finish（"offer finished with an invalid action"）。把 ASK 放进掩码后，
     * 目标端只要把 preferred 写成 4，合成器就会选中 ASK 并回告源端，
     * 松手时 finish 必被拒绝、连接被杀。GDK3 的 gdkdnd-wayland.c 同样只用
     * COPY|MOVE。用数值而不是宏：旧版 wayland 头里没有这几个枚举。 */
    wl_data_source_set_actions(src->src, 1u | 2u);
#endif
    seat->dragSource = src;
    seat->dragDone = 0;
    seat->dragDropped = 0;
    seat->dragEffect = 1; // DROPEFFECT_COPY
    seat->dragQuery = query;
    seat->dragCtx = ctx;
    /* start_drag 必须用 pointer button press 的 serial（GTK gdkdevice-wayland.c
     * 同款）。g.lastSerial 会被 enter/leave/modifiers 覆盖：拖放途中按了 Ctrl
     * 再松鼠标，用它就拿不到有效 grab，合成器静默忽略 start_drag。 */
    uint32_t dragSerial = serial ? serial : seat->pressSerial;
    if (!dragSerial)
        dragSerial = g.lastSerial;
    /* 没有 icon 时 GNOME/KWin 不给任何拖动反馈；图标必须在 start_drag 之前
     * attach+commit（协议要求 surface 已有 buffer），且不能用光标 surface。
     * 起始画面用 copy 徽标占位，第一次 GiveFeedback 就会换成系统拖放光标。 */
    const int iconSize = 24;
    struct wl_surface *iconSurface = drag_icon_create(iconSize, iconSize);
    s_dragIcon = iconSurface;
    wl_data_device_start_drag(seat->dataDevice, src->src, origin->surface,
                              iconSurface, dragSerial);
    wl_display_flush(g.display);

    time_t t0 = time(NULL);
    int displayErr = 0;
    while (!seat->dragDone && (time(NULL) - t0) < 20)
    {
        /* QueryContinueDrag 必须排在 poll 之前，并且紧跟一次 flush：
         * 它内部会排入 set_cursor/attach/commit（拖动光标）。libwayland 只在
         * wl_display_dispatch 入口 flush，而 DnD grab 期间源端收不到 pointer
         * motion，poll 经常一路超时 ⇒ 不显式 flush 的话光标请求整场拖放都
         * 卡在发送队列里，表现就是"拖动时光标毫无变化"。 */
        if (query && query(ctx, wl_pointer_state_to_win(seat->state)) < 0)
        {
            seat->dragDone = 1;
            seat->dragDropped = 0;
            break;
        }
        if (wl_display_flush(g.display) < 0 && errno != EAGAIN)
        {
            displayErr = errno ? errno : EPIPE;
            seat->dragDone = 1;
            seat->dragDropped = 0;
            break;
        }
        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(g.display);
        pfd.events = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 25);
        if (pr > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
        {
            displayErr = EPIPE;
            seat->dragDone = 1;
            seat->dragDropped = 0;
            break;
        }
        if (pr > 0 && (pfd.revents & POLLIN))
        {
            if (wl_display_dispatch(g.display) < 0)
            {
                /* 合成器因协议错误断开（典型：拖动期间发过非法对象 id）。
                 * 不退出的话下面只剩 poll 立刻返回 POLLIN + dispatch 立刻失败
                 * 的忙循环，主线程 100% CPU 且永远等不到 dnd_finished。 */
                displayErr = wl_display_get_error(g.display);
                if (!displayErr)
                    displayErr = EPIPE;
                seat->dragDone = 1;
                seat->dragDropped = 0;
                break;
            }
        }
    }
    s_dragIcon = NULL;
    drag_icon_destroy(iconSurface);
    int dropped = seat->dragDropped;
    seat->dragSource = NULL;
    seat->dragQuery = NULL;
    seat->dragCtx = NULL;
    /* 连接已断时再发请求只会往死管道里写，跳过（对象仍需 delete）。 */
    if (src->src && !displayErr)
        wl_data_source_destroy(src->src);
    delete src;
    return dropped ? 1 : 0;
}

void SwinxWlDndOfferAccept(SwinxWlDataOffer *offer, const char *mime)
{
    if (!offer || !offer->offer)
        return;
    /* GTK3 gdkdnd-wayland.c must call accept(serial, mime) before finish;
     * mutter data_offer_finish() rejects a source whose offer was never
     * accepted with fatal WL_DATA_OFFER_ERROR_INVALID_FINISH.  Pick the
     * first offered mime when the requested one is absent (file drags only
     * carry text/uri-list). */
    const char *acceptMime = NULL;
    if (mime)
    {
        for (size_t i = 0; i < offer->mimes.size(); ++i)
            if (offer->mimes[i] == mime) { acceptMime = mime; break; }
    }
    if (!acceptMime && !offer->mimes.empty())
        acceptMime = offer->mimes[0].c_str();
    offer->accepted = acceptMime != NULL;
    wl_data_offer_accept(offer->offer, offer->serial, acceptMime);
#if defined(WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
    /* 掩码只用 copy|move（3）：协议里 4 是 ask，塞进掩码会让合成器选中 ASK，
     * 随后 finish 被 mutter 以 "offer finished with an invalid action" 拒绝。 */
    uint32_t actions = offer->accepted ? 3u : 0u;
    wl_data_offer_set_actions(offer->offer, actions, offer->accepted ? 1u : 0u);
#endif
}

/* 目标端把 DragEnter/DragOver 算出的 effect 回告合成器：wl_data_offer
 * .set_actions 会让合成器把最终 action 经 wl_data_source.action 送回源端，
 * 源端据此换拖动光标（与 X11 XdndStatus 回写 *pdwEffect 是同一件事）。
 * 注意 dnd_action 的取值是 none=0/copy=1/move=2/ask=4（**没有 link**），
 * 与 DROPEFFECT_* 只有 copy/move 两位能对上；写数值而非宏是为了兼容
 * 只有 COPY/MOVE 的旧版 wayland 头。
 *
 * effect==0（DROPEFFECT_NONE）必须上报 actions=0：只写 preferred=0 会被
 * 合成器当成"没有首选"而在 copy/move 里挑一个，源端永远看不到"禁止放下"
 * （X11 里 XdndStatus 的 action=XCB_NONE 是明确的拒绝）。 */
void SwinxWlDndOfferSetEffect(SwinxWlDataOffer *offer, unsigned int effect)
{
    if (!offer || !offer->offer)
        return;
#if defined(WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
    /* DROPEFFECT_* -> dnd_action：move→2、copy→1、none→0。
     * LINK(0x4) 在核心协议里没有对应位，降级成 copy（数据仍可落下，只是
     * 目标端拿不到"链接"语义；mutter 的 ALL_ACTIONS 也没有 link）。
     * preferred 必须是掩码内的**单一位**，否则 mutter 以
     * "invalid action %x" 拒绝。 */
    uint32_t act = 0;
    if (effect & 2u)
        act = 2u;
    else if (effect & (1u | 4u))
        act = 1u;
    if (!act)
        wl_data_offer_set_actions(offer->offer, 0u, 0u);
    else
        wl_data_offer_set_actions(offer->offer, 3u, act);
#endif
}

int SwinxWlDndOfferHasMime(SwinxWlDataOffer *offer, const char *mime)
{
    if (!offer || !mime || !*mime)
        return 0;
    for (size_t i = 0; i < offer->mimes.size(); ++i)
        if (offer->mimes[i] == mime)
            return 1;
    return 0;
}

int SwinxWlDndOfferRead(SwinxWlDataOffer *offer, const char *mime, void **outBuf)
{
    return read_offer_to_heap(offer, mime, outBuf);
}

void SwinxWlDndOfferFinish(SwinxWlDataOffer *offer)
{
    if (!offer)
        return;
    /* mutter 的 data_offer_finish（meta-wayland-data-offer.c）有两个硬条件：
     *   1. offer->accepted —— 我们 accept 过非空 mime；
     *   2. offer->action_sent —— 合成器已用 wl_data_offer.action 回过一次选定
     *      的 action（我们记在 offer->action 里）；
     * 且当前 action 不能是 NONE/ASK，否则直接
     * post_error(INVALID_OFFER, "offer finished with an invalid action")
     * 并把整条连接杀掉。条件不满足时只 destroy：mutter 在 destroy_data_offer
     * 里按 drop_performed 走 cancel，语义等于"这次放下没被接受"，不会断连。 */
    int canFinish = offer->accepted && offer->action != 0;
#if defined(WL_DATA_OFFER_FINISH_SINCE_VERSION)
    if (offer->offer && canFinish)
        wl_data_offer_finish(offer->offer);
#endif
    dnd_offer_destroy(offer);
}

// ---------------------------------------------------------------------------
// registry
// ---------------------------------------------------------------------------

static void registry_global(void *, struct wl_registry *registry, uint32_t name,
                            const char *iface, uint32_t version)
{
    if (strcmp(iface, wl_compositor_interface.name) == 0)
    {
        g.compositor = (wl_compositor *)wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    }
    else if (strcmp(iface, wl_subcompositor_interface.name) == 0)
    {
        g.subcompositor = (wl_subcompositor *)wl_registry_bind(
            registry, name, &wl_subcompositor_interface, 1);
    }
    else if (strcmp(iface, wl_shm_interface.name) == 0)
    {
        g.shm = (wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
    else if (strcmp(iface, xdg_wm_base_interface.name) == 0)
    {
        g.wm_base = (xdg_wm_base *)wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(g.wm_base, &wm_base_listener, NULL);
    }
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    else if (strcmp(iface, zwp_text_input_manager_v3_interface.name) == 0)
    {
        g.textInputManager = (struct zwp_text_input_manager_v3 *)wl_registry_bind(
            registry, name, &zwp_text_input_manager_v3_interface, 1);
        for (int i = 0; i < g.nSeats; ++i)
            ime_ensure(&g.seats[i]);
    }
#endif
    else if (strcmp(iface, wl_data_device_manager_interface.name) == 0)
    {
        g.dataDeviceManager = (struct wl_data_device_manager *)wl_registry_bind(
            registry, name, &wl_data_device_manager_interface, version < 3 ? version : 3);
        data_devices_ensure();
    }
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    else if (strcmp(iface, wp_viewporter_interface.name) == 0)
    {
        g.viewporter = (struct wp_viewporter *)wl_registry_bind(
            registry, name, &wp_viewporter_interface, 1);
    }
    else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0)
    {
        g.fractionalScaleManager = (struct wp_fractional_scale_manager_v1 *)wl_registry_bind(
            registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    }
#endif
    else if (strcmp(iface, wl_seat_interface.name) == 0 && g.nSeats < SWINX_WL_MAX_SEATS)
    {
        WlSeat *s = &g.seats[g.nSeats++];
        memset(s, 0, sizeof(*s));
        s->seat = (wl_seat *)wl_registry_bind(
            registry, name, &wl_seat_interface, version < 3 ? version : 3);
        wl_seat_set_user_data(s->seat, s);
        wl_seat_add_listener(s->seat, &seat_listener, s);
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
        ime_ensure(s);
#endif
        data_devices_ensure();
    }
    else if (strcmp(iface, wl_output_interface.name) == 0 && g.nOutputs < SWINX_WL_MAX_OUTPUTS)
    {
        WlOutput *o = &g.outputs[g.nOutputs++];
        memset(o, 0, sizeof(*o));
        o->scale = 1;
        o->scale120 = 120;
        o->output = (wl_output *)wl_registry_bind(
            registry, name, &wl_output_interface, version < 2 ? version : 2);
        wl_output_add_listener(o->output, &output_listener, o);
    }
}

static void registry_global_remove(void *, struct wl_registry *, uint32_t) {}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

// ---------------------------------------------------------------------------
// 事件回投
// ---------------------------------------------------------------------------

static void emit_event(int type, void *win, int a, int b, int c, int d,
                       unsigned int time)
{
    if (!s_eventCb)
        return;
    SwinxWlEvent ev = {}; // 零初始化：新增字段不会漏填
    ev.type = type;
    ev.win = win;
    ev.a = a; ev.b = b; ev.c = c; ev.d = d;
    ev.p = NULL;
    ev.time = time;
    s_eventCb(&ev);
}

/* DND 事件要额外带 offer 指针，单独构造，避免给 emit_event 加第 7 个参数。 */
static void emit_dnd_event(int type, void *win, int a, int b, int c, WlDataOffer *o)
{
    if (!s_eventCb)
        return;
    SwinxWlEvent ev = {}; // 零初始化：新增字段不会漏填
    ev.type = type;
    ev.win = win;
    ev.a = a; ev.b = b; ev.c = c; ev.d = 0;
    ev.p = o;
    s_eventCb(&ev);
}

#if defined(SWINX_HAVE_TEXT_INPUT_V3)
static void emit_ime_event(int type, void *win, char *text, int serial)
{
    if (!s_eventCb)
    {
        free(text);
        return;
    }
    SwinxWlEvent ev = {}; // 零初始化：新增字段不会漏填
    ev.type = type;
    ev.win = win;
    ev.a = serial; ev.b = 0; ev.c = 0; ev.d = 0;
    ev.p = text;
    s_eventCb(&ev);
}
#endif

// ---------------------------------------------------------------------------
// 对外 surface API
// ---------------------------------------------------------------------------

/* 新建 wl_surface。popup 隐藏时必须连 wl_surface 一起销毁（GTK
 * gdk_wayland_window_hide_surface 同款）：一个 wl_surface 的 role 一经指定
 * 就不能改，复用旧 surface 重建 xdg_popup 在严格合成器上会被拒绝。 */
static int surface_create_native(SwinxWlSurface *s)
{
    if (!g.ready || !g.compositor || s->surface)
        return !!s->surface;
    s->surface = wl_compositor_create_surface(g.compositor);
    if (!s->surface)
        return 0;
    wl_surface_set_user_data(s->surface, s->hwnd);
    /* 非整数缩放：每个 surface 各建一个 fractional_scale + viewport。
     * fractional_scale 由合成器回告 preferred_scale（120=1.0），viewport 用来
     * 把按"物理像素"渲染的 SHM buffer 声明成合成器逻辑坐标系里的小一号尺寸
     * （destination = bufferSize / scale），这样 1.25/1.5 倍下才不会整体拉伸。 */
    s->scale120 = SwinxWlGetSystemScale120();
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    if (scaleEnabled() && g.fractionalScaleManager)
    {
        s->fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(
            g.fractionalScaleManager, s->surface);
        if (s->fractionalScale)
            wp_fractional_scale_v1_add_listener(s->fractionalScale,
                                                 &fractional_scale_listener, s);
    }
    if (scaleEnabled() && g.viewporter)
        s->viewport = wp_viewporter_get_viewport(g.viewporter, s->surface);
#endif
    if (s->inputTransparent)
    {
        /* 空 region：只有显式 WS_EX_TRANSPARENT 的窗口才输入穿透（X11 同款）。
         * 菜单等普通 popup 必须保留输入，否则 hover / 点击全部落到下层窗口。 */
        struct wl_region *region = wl_compositor_create_region(g.compositor);
        if (region)
        {
            wl_surface_set_input_region(s->surface, region);
            wl_region_destroy(region);
        }
    }
    return 1;
}

SwinxWlSurface *SwinxWlSurfaceCreate(void *hwnd, int w, int h, int role, void *parentHwnd)
{
    if (!g.ready || !g.compositor || !g.shm || !g.wm_base)
        return NULL;
    SwinxWlSurface *s = find_surface(hwnd);
    if (s)
    {
        if (w > 0 && h > 0 && (w != s->w || h != s->h))
            SwinxWlSurfaceSetGeometry(s, s->x, s->y, w, h);
        return s;
    }
    s = (SwinxWlSurface *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->hwnd = hwnd;
    s->role = role;
    s->w = w > 0 ? w : 1;
    s->h = h > 0 ? h : 1;
    /* 创建期不 map：GTK 在 map（show）时才提交首个 xdg_surface.commit。
     * 之前创建即 commit 会让合成器按 CREATESTRUCT 的临时尺寸（MessageBox
     * 是 10x10）摆位，MoveWindow 长大后就停在那个角落，表现为"位置不受控"。 */
    s->visible = 0;
    s->needFull = 1;
    /* popup（菜单/tooltip）默认**接收**指针事件：对齐 X11 后端——只有窗口
     * 显式带 WS_EX_TRANSPARENT 时才由 SetWindowMsgTransparent() 设空输入区域。
     * 早前把所有 role==POPUP 一律设成 inputTransparent=1，导致弹出菜单完全
     * 收不到 WM_MOUSEMOVE / WM_LBUTTON*：hover 不高亮、点菜单项只触发
     * xdg_popup.done 关闭菜单却收不到命令——正是 X11 下不会有的行为。 */
    s->inputTransparent = 0;
    if (!surface_create_native(s))
    {
        free(s);
        return NULL;
    }
    if (role == SWINX_WL_SURF_SUBSURFACE)
    {
        s->parent = find_surface(parentHwnd);
        if (!s->parent || !g.subcompositor)
            s->role = SWINX_WL_SURF_TOPLEVEL; // 父不存在：退化，至少能显示
    }
    if (s->role == SWINX_WL_SURF_POPUP)
    {
        /* popup 的 xdg_surface 与 xdg_popup 不在创建期建立：SOUI 的 tooltip
         * 建窗时 cx/cy=0，位置要等 SetWindowPos 之后才确定；而 xdg_popup 的
         * xdg_positioner 是一次性参数，必须等到 SetPopAnchor 拿到真实位置。
         * GTK 同样在 map（show）时才 create_xdg_popup。 */
        s->popParent = find_surface(parentHwnd);
    }
    if (s->role == SWINX_WL_SURF_SUBSURFACE)
    {
        s->subsurface = wl_subcompositor_get_subsurface(
            g.subcompositor, s->surface, s->parent->surface);
        if (!s->subsurface)
            s->role = SWINX_WL_SURF_TOPLEVEL;
        else
        {
            subsurfaceSetPosition(s, s->x, s->y);
            wl_subsurface_set_desync(s->subsurface); // 弹窗可独立提交
            // wayland 对新建 subsurface 之间的兄弟次序没有规定；按创建序显式
            // 压到同级最上面，保持 SOUI 的 z 序（主窗 → 对话框 → 下拉窗）。
            for (SwinxWlSurface *c = g.surfaces; c; c = c->next)
            {
                if (c->parent == s->parent)
                {
                    wl_subsurface_place_above(s->subsurface, c->surface);
                    break;
                }
            }
        }
    }
    if (s->role == SWINX_WL_SURF_TOPLEVEL)
    {
        s->xdg_surface = xdg_wm_base_get_xdg_surface(g.wm_base, s->surface);
        if (s->xdg_surface)
        {
            xdg_surface_add_listener(s->xdg_surface, &xdg_surface_listener, s);
            s->toplevel = xdg_surface_get_toplevel(s->xdg_surface);
            if (s->toplevel)
            {
                xdg_toplevel_add_listener(s->toplevel, &toplevel_listener, s);
                xdg_toplevel_set_title(s->toplevel, "soui");
                /* app_id 决定 taskbar/dock 图标：优先显式设置，其次环境变量，
                 * 再退到进程名（对齐 X11 WM_CLASS 第一字段）。见 SwinxWlSetAppId。 */
                xdg_toplevel_set_app_id(s->toplevel, SwinxWlGetAppId());
            }
        }
    }
    s->next = g.surfaces;
    g.surfaces = s;
    /* xdg-shell 握手所需的裸 commit 移到 SwinxWlSurfaceSetVisible(TRUE)：
     * 那里窗口尺寸已经是 MoveWindow/CenterWindow 之后的最终值。创建期就
     * commit 会让合成器按临时尺寸摆位（MessageBox 先 10x10 再 373x212），
     * 表现为窗口位置不受控。带 buffer 的 commit 仍由 surface_flush 负责。 */
    return s;
}

/* 找到能作为 xdg_popup 父级的 xdg_surface（GTK get_popup_parent：
 * 沿 transient/父链向上找最近一个已 realize 的 toplevel 或 popup）。
 * 找不到时退到第一个 toplevel——tooltip 的 owner 可能是消息窗/已销毁窗口。 */
static SwinxWlSurface *find_popup_parent(SwinxWlSurface *s)
{
    /* HWND 是 _Window*，销毁后地址会被新窗复用；popParent/parent 可能形成环
     * （日志实证多个 tooltip 互相挂为 parent）。加深度上限兜底，避免无限循环。 */
    SwinxWlSurface *p = s->popParent;
    for (int depth = 0; p && !p->xdg_surface && p != s && depth < 16; ++depth)
        p = p->parent ? p->parent : p->popParent;
    if (p && p->xdg_surface && p != s)
        return p;
    /* 兜底：popParent 没建好（tooltip 在活动窗还不存在时创建）。必须优先选
     * **已 map** 的 toplevel——拿一块还没上屏的 surface（或已隐藏的 popup）
     * 当父 surface，合成器无从定位子 popup，整棵子树会落到 (0,0)。 */
    SwinxWlSurface *mapped = NULL;
    for (p = g.surfaces; p; p = p->next)
    {
        if (p == s || !p->xdg_surface || !(p->toplevel || p->popup))
            continue;
        if (p->toplevel && p->visible)
            return p; // 已映射的 toplevel：最理想
        if (!mapped && p->visible)
            mapped = p; // 已映射的 popup 次之
    }
    if (mapped)
        return mapped;
    for (p = g.surfaces; p; p = p->next)
        if (p->xdg_surface && (p->toplevel || p->popup) && p != s)
            return p;
    return NULL;
}

/* 父 surface 销毁/重建时必须摘掉挂在它上面的 wl_subsurface（MessageBox 的
 * STATIC/BUTTON 就是这种情况）。让悬空的 subsurface 继续存在，或对已销毁的
 * parent 调 get_subsurface，都是协议错误——合成器会直接断开整个 wl_display
 * 连接，表现为"对话框一操作，后面的主窗口再也不显示"。GTK 的
 * gdk_wayland_window_hide_surface 同样是连整棵 surface 子树一起处理。 */
struct SubRebind { SwinxWlSurface *child; };

static void subchildren_unbind(SwinxWlSurface *s, std::vector<SubRebind> &out)
{
    out.clear();
    for (SwinxWlSurface *c = g.surfaces; c; c = c->next)
    {
        if (c->parent != s || !c->subsurface)
            continue;
        SubRebind r = { c };
        out.push_back(r);
        wl_subsurface_destroy(c->subsurface);
        c->subsurface = NULL;
    }
}

static void subchildren_rebind(SwinxWlSurface *s, const std::vector<SubRebind> &saved)
{
    for (size_t i = 0; i < saved.size(); ++i)
    {
        SwinxWlSurface *c = saved[i].child;
        if (!c->surface || !s->surface || !g.subcompositor)
            continue;
        c->subsurface = wl_subcompositor_get_subsurface(
            g.subcompositor, c->surface, s->surface);
        if (!c->subsurface)
            continue;
        subsurfaceSetPosition(c, c->x, c->y);
        wl_subsurface_set_desync(c->subsurface);
        /* 与创建时同款：压到同级最上面，保持 SOUI 的 z 序。 */
        for (SwinxWlSurface *o = g.surfaces; o; o = o->next)
        {
            if (o != c && o->parent == s && o->subsurface)
            {
                wl_subsurface_place_above(c->subsurface, o->surface);
                break;
            }
        }
    }
}

/* 销毁 popup 专属的 xdg 对象。role 一旦赋予就不可改，所以连 wl_surface 一起
 * 销毁（buffer 可跨 surface 复用，不清）；recreate=true 时立刻重建并把子窗
 * 的 subsurface 重新挂上，false 用于 hide（下次 SetPopAnchor 时重建）。
 * GTK gdk_wayland_window_hide_surface 同款顺序。 */
/* viewport/fractional_scale 是 surface 的子对象，必须先于 wl_surface 销毁。 */
static void destroy_scale_objects(SwinxWlSurface *s)
{
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    if (s->fractionalScale)
    {
        wp_fractional_scale_v1_destroy(s->fractionalScale);
        s->fractionalScale = NULL;
    }
    if (s->viewport)
    {
        wp_viewport_destroy(s->viewport);
        s->viewport = NULL;
    }
#endif
    s->vpDstW = s->vpDstH = 0;
}

static void popup_role_destroy(SwinxWlSurface *s, bool recreate)
{
    destroy_scale_objects(s);
    if (s->popup)
    {
        xdg_popup_destroy(s->popup);
        s->popup = NULL;
    }
    if (s->xdg_surface)
    {
        xdg_surface_destroy(s->xdg_surface);
        s->xdg_surface = NULL;
    }
    if (s->frameCb)
    {
        wl_callback_destroy(s->frameCb);
        s->frameCb = NULL;
    }
    std::vector<SubRebind> kids;
    subchildren_unbind(s, kids);
    if (s->surface)
    {
        wl_surface_destroy(s->surface);
        s->surface = NULL;
    }
    if (recreate && surface_create_native(s))
        subchildren_rebind(s, kids);
    s->configured = 0;
    s->framePending = 0;
    s->popHasAnchor = 0;
}

void SwinxWlSurfaceSetPopAnchor(SwinxWlSurface *s, void *parentHwnd,
                                int anchorX, int anchorY, int anchorW, int anchorH,
                                int winX, int winY, int winW, int winH)
{
    if (!g.ready || !g.wm_base || !s || s->role != SWINX_WL_SURF_POPUP)
        return;
    SwinxWlSurface *parent = find_popup_parent(s);
    if (!parent || parent == s)
        return; // 合成器要求 popup 必须挂在已有 toplevel/popup 上
    if (parentHwnd && (!s->popParent || s->popParent->hwnd != parentHwnd))
    {
        SwinxWlSurface *np = find_surface(parentHwnd);
        if (np && np != s)
            s->popParent = np;
    }
    // sanitize：xdg_positioner 不接受 0 宽高的 anchor rect（GTK sanitize_anchor_rect）
    if (anchorW < 1) anchorW = 1;
    if (anchorH < 1) anchorH = 1;
    if (anchorX < 0) anchorX = 0;
    if (anchorY < 0) anchorY = 0;
    if (winW < 1) winW = 1;
    if (winH < 1) winH = 1;
    if (s->popup && s->popHasAnchor &&
        s->popAx == anchorX && s->popAy == anchorY &&
        s->popAw == anchorW && s->popAh == anchorH &&
        s->popX == winX && s->popY == winY &&
        s->popW == winW && s->popH == winH)
        return; // 位置没变，不重复重建（tooltip 每次 present 都会走到这里）
    if (s->popup)
        popup_role_destroy(s, true); // xdg_popup 的 positioner 创建后不可改，只能重建

    s->popAx = anchorX; s->popAy = anchorY; s->popAw = anchorW; s->popAh = anchorH;
    s->popX = winX; s->popY = winY; s->popW = winW; s->popH = winH;
    s->popHasAnchor = 1;

    /* 上一轮 hide（或位置变化触发的重建）已销毁 wl_surface，这里重新建。 */
    if (!surface_create_native(s))
        return;
    s->xdg_surface = xdg_wm_base_get_xdg_surface(g.wm_base, s->surface);
    if (!s->xdg_surface)
        return;
    xdg_surface_add_listener(s->xdg_surface, &xdg_surface_listener, s);

    struct xdg_positioner *pos = xdg_wm_base_create_positioner(g.wm_base);
    if (!pos)
    {
        popup_role_destroy(s, true);
        return;
    }
    /* positioner 的坐标是父 surface 的逻辑坐标；SOUI 传进来的是物理像素，
     * viewport 生效时要除倍率（父窗没建好时退化到 popup 自己的倍率）。 */
    SwinxWlSurface *pref = find_surface(parent->hwnd);
    if (!pref) pref = s;
    xdg_positioner_set_size(pos, scaleScalar(pref, winW), scaleScalar(pref, winH));
    xdg_positioner_set_anchor_rect(pos,
        scaleScalar(pref, anchorX), scaleScalar(pref, anchorY),
        scaleScalar(pref, anchorW), scaleScalar(pref, anchorH));
    // SOUI 已经把 tooltip 的**左上角**算好并转成父窗口局部坐标（SToolTip::
    // ShowTip 的 rcWnd.TopLeft）。1x1 锚点放在那个左上角，anchor=TOP_LEFT +
    // gravity=BOTTOM_RIGHT = "popup 从锚点向右下展开"（即 popup 左上角=锚点），
    // 这正是 GTK create_simple_positioner (gdkwindow-wayland.c:2981) 的写法。
    // 不做 FLIP/SLIDE，让合成器把 SOUI 算好的位置原样落地。
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(pos, 0);

    s->popup = xdg_surface_get_popup(s->xdg_surface, parent->xdg_surface, pos);
    xdg_positioner_destroy(pos);
    if (!s->popup)
    {
        popup_role_destroy(s, true);
        return;
    }
    xdg_popup_add_listener(s->popup, &xdg_popup_listener, s);
    s->needFull = 1;
    /* 隐藏时走的是 popup_role_destroy(false)：挂在它上面的 wl_subsurface 会被
     * 一并摘掉（协议不允许 subsurface 引用已销毁的父 surface）。重新 map 时
     * 必须补回来，否则内容窗的 wl_surface 没有角色，下一次 commit 会被合成器
     * 判成协议错误（客户端装饰窗里的内容窗就是这种子 surface）。 */
    for (SwinxWlSurface *c = g.surfaces; c; c = c->next)
    {
        if (c->parent != s || c->subsurface || !c->surface || !g.subcompositor)
            continue;
        c->subsurface = wl_subcompositor_get_subsurface(g.subcompositor, c->surface, s->surface);
        if (!c->subsurface)
            continue;
        subsurfaceSetPosition(c, c->x, c->y);
        wl_subsurface_set_desync(c->subsurface);
        for (SwinxWlSurface *o = g.surfaces; o; o = o->next)
        {
            if (o != c && o->parent == s && o->subsurface)
            {
                wl_subsurface_place_above(c->subsurface, o->surface);
                break;
            }
        }
    }
    /* xdg-shell 握手：新建 xdg_surface 后必须裸 commit 一次触发 configure，
     * 带 buffer 的 commit 由 xdg_surface_configure → surface_flush 完成。 */
    wl_surface_commit(s->surface);
}

void SwinxWlSurfaceSetTransientParent(SwinxWlSurface *s, void *parentHwnd)
{
    if (!s || !s->toplevel)
        return; // subsurface 的父子由 wl_subcompositor 固定，不走这里
    SwinxWlSurface *p = parentHwnd ? find_surface(parentHwnd) : NULL;
    /* 父是 subsurface 时取其所属 toplevel：xdg_toplevel_set_parent 只接受
     * 另一个 toplevel。找不到就清除关系，绝不能传自己的 surface。
     * 加深度上限 + p!=s：HWND 地址复用可让 parent 链成环。 */
    for (int depth = 0; p && !p->toplevel && p != s && depth < 16; ++depth)
        p = p->parent;
    xdg_toplevel_set_parent(s->toplevel, p && p->toplevel && p != s
                            ? p->toplevel : NULL);
}

int SwinxWlSurfaceGetRole(SwinxWlSurface *s)
{
    return s ? s->role : SWINX_WL_SURF_TOPLEVEL;
}

void *SwinxWlSurfaceGetPopupParentHwnd(SwinxWlSurface *s)
{
    if (!s || s->role != SWINX_WL_SURF_POPUP)
        return NULL;
    SwinxWlSurface *p = find_popup_parent(s);
    return p ? p->hwnd : NULL;
}

int SwinxWlSurfaceSetDialogParent(SwinxWlSurface *s, void *parentHwnd)
{
    if (!g.ready || !s || s->role != SWINX_WL_SURF_TOPLEVEL || !s->toplevel)
        return 0;
    SwinxWlSurface *p = parentHwnd ? find_surface(parentHwnd) : NULL;
    if (!p || p == s || !p->xdg_surface)
    {
        if (wl_trace())
            fprintf(stderr, "[wl] dialog-parent FALLBACK toplevel hwnd=%p parent=%p "
                            "hasSurface=%d hasXdg=%d\n",
                    s->hwnd, parentHwnd, !!p, p ? !!p->xdg_surface : 0);
        return 0; // 没有可用父级：保持 xdg_toplevel（调用方会记下来重试）
    }
    /* xdg_toplevel 没有客户端置位能力；把有 owner 的对话框改成 xdg_popup，
     * 让 xdg_positioner 按 SOUI 算好的屏幕坐标摆放（MessageBox 居中父窗）。
     * role 不能改，必须连 wl_surface 一起重建；重建时把已挂在旧 surface 上的
     * wl_subsurface 一起迁过去，否则合成器报协议错误并断开整个连接。 */
    xdg_toplevel_destroy(s->toplevel);
    s->toplevel = NULL;
    if (s->xdg_surface)
    {
        xdg_surface_destroy(s->xdg_surface);
        s->xdg_surface = NULL;
    }
    if (s->frameCb)
    {
        wl_callback_destroy(s->frameCb);
        s->frameCb = NULL;
    }
    std::vector<SubRebind> kids;
    subchildren_unbind(s, kids);
    if (s->surface)
    {
        wl_surface_destroy(s->surface);
        s->surface = NULL;
    }
    s->role = SWINX_WL_SURF_POPUP;
    s->popParent = p;
    s->inputTransparent = 0; // 对话框要能收到点击，与 tooltip 不同
    /* 立刻建回 wl_surface：MessageBox 的子窗口（STATIC/BUTTON）在 InitMsgBox
     * 里创建，SConnection::SetWindowVisible 会级联给它们发 WM_PAINT；那时
     * 父 surface 若为 NULL，wl_subcompositor.get_subsurface 会直接被合成器
     * 判为协议错误并断开连接，后续主窗口再也无法 map。 */
    surface_create_native(s);
    subchildren_rebind(s, kids);
    /* xdg_popup 要等 SetPopAnchor 拿到实际坐标后才建立；xdg_surface 尚未
     * assign role，这里不能 attach/commit。 */
    s->visible = 0;
    s->configured = 0;
    s->framePending = 0;
    s->needFull = 1;
    s->srcBits = NULL;
    s->popHasAnchor = 0;
    return 1;
}

static void applyInputRegion(SwinxWlSurface *s)
{
    if (!s || !g.ready || !g.compositor || !s->surface)
        return;
    struct wl_region *region = NULL;
    if (s->inputTransparent)
        region = wl_compositor_create_region(g.compositor); // 空区域 = 全穿透
    else if (s->inL || s->inT || s->inR || s->inB)
    {
        int w = s->w - s->inL - s->inR;
        int h = s->h - s->inT - s->inB;
        if (w > 0 && h > 0)
        {
            region = wl_compositor_create_region(g.compositor);
            /* region 用 surface 局部（逻辑）坐标。 */
            wl_region_add(region,
                          scaleScalar(s, s->inL), scaleScalar(s, s->inT),
                          scaleScalar(s, w), scaleScalar(s, h));
        }
    }
    wl_surface_set_input_region(s->surface, region);
    if (region)
        wl_region_destroy(region);
    if (s->xdg_surface)
        wl_surface_commit(s->surface); // xdg_surface 已 configure，可带裸 commit
}

void SwinxWlSurfaceSetInputTransparent(SwinxWlSurface *s, int transparent)
{
    if (!s || s->inputTransparent == transparent)
        return;
    s->inputTransparent = transparent;
    applyInputRegion(s);
}

void SwinxWlSurfaceSetInputInset(SwinxWlSurface *s, int l, int t, int r, int b)
{
    if (!s || (s->inL == l && s->inT == t && s->inR == r && s->inB == b))
        return;
    s->inL = l; s->inT = t; s->inR = r; s->inB = b;
    applyInputRegion(s);
}

SwinxWlSurface *SwinxWlSurfaceGet(void *hwnd)
{
    return find_surface(hwnd);
}

void SwinxWlSurfaceDestroy(void *hwnd)
{
    SwinxWlSurface *s = find_surface(hwnd);
    if (!s)
        return;
    for (SwinxWlSurface **pp = &g.surfaces; *pp; pp = &(*pp)->next)
    {
        if (*pp == s)
        {
            *pp = s->next;
            break;
        }
    }
    for (SwinxWlSurface *c = g.surfaces; c; c = c->next)
    {
        if (c->parent == s)
            c->parent = NULL;
        if (c->popParent == s)
            c->popParent = NULL;
    }
    if (s->frameCb)
        wl_callback_destroy(s->frameCb);
    for (int i = 0; i < s->nBufs; ++i)
        destroy_buf(&s->bufs[i]);
    if (s->subsurface) wl_subsurface_destroy(s->subsurface);
    if (s->toplevel) xdg_toplevel_destroy(s->toplevel);
    if (s->popup) xdg_popup_destroy(s->popup);
    if (s->xdg_surface) xdg_surface_destroy(s->xdg_surface);
    destroy_scale_objects(s);
    if (s->surface) wl_surface_destroy(s->surface); // popup 隐藏后已为 NULL
    free(s);
}

void SwinxWlSurfaceSetGeometry(SwinxWlSurface *s, int x, int y, int w, int h)
{
    if (!s)
        return;
    bool resized = (w > 0 && h > 0 && (w != s->w || h != s->h));
    s->x = x;
    s->y = y;
    if (!resized)
    {
        if (s->subsurface)
            subsurfaceSetPosition(s, x, y);
        return;
    }
    s->w = w;
    s->h = h;
    for (int i = 0; i < s->nBufs; ++i)
        destroy_buf(&s->bufs[i]);
    s->nBufs = 0;
    s->needFull = 1;
    s->framePending = 0; // 旧 buffer 已作废
    s->srcBits = NULL;   // 旧画布已释放：等新的 Present 带来新指针再提交
    applyInputRegion(s); // 内缩输入区随尺寸一起更新
    if (s->subsurface)
        subsurfaceSetPosition(s, x, y);
}

void SwinxWlSurfaceSetVisible(SwinxWlSurface *s, int visible)
{
    if (!s || s->visible == visible)
        return;
    s->visible = visible;
    if (!visible)
    {
        /* popup 隐藏时销毁 xdg_popup + xdg_surface：SOUI 的 tooltip 下次显示
         * 位置会变，而 xdg_popup 的 positioner 创建后不可改。GTK 的
         * gdk_wayland_window_hide_surface 同款（保留 wl_surface 复用画布）。 */
        if (s->role == SWINX_WL_SURF_POPUP)
            popup_role_destroy(s, false); // 内含 wl_surface_destroy，下次 map 重建
        if (s->surface)
        {
            wl_surface_attach(s->surface, NULL, 0, 0); // unmap
            wl_surface_commit(s->surface);
        }
    }
    else
    {
        s->needFull = 1;
        s->framePending = 0;
        if (s->subsurface)
            subsurfaceSetPosition(s, s->x, s->y);
        /* xdg-shell 握手：首次 map 必须先发一次**不带 buffer**的裸 commit，
         * 合成器才会回 xdg_surface.configure；拿到 configure/ack 之后才允许
         * attach buffer。关键：这一步不能放在 `if (s->srcBits) ... else` 的
         * else 分支里——SOUI 的窗口在 ShowWindow 之前就画过（日志里 vis=0
         * 的 present 已把 srcBits 填好），会走 surface_flush，而它被
         * `!configured` 闸住直接 return，裸 commit 就永远轮不到执行，
         * configure 永不到达 → 窗口永远不 map（整窗消失，只有任务栏缩略图）。 */
        if ((s->role == SWINX_WL_SURF_TOPLEVEL || s->role == SWINX_WL_SURF_POPUP) &&
            s->surface && !s->configured)
            wl_surface_commit(s->surface);
        /* 隐藏期间发生的 present 已经在 srcBits/buf damage 里留好了数据，
         * 但当时 surface_flush 被 !visible 挡掉；重新显示时必须主动补一次，
         * 否则"先画后 ShowWindow"的窗口（SOUI 主窗常见）永远停在空壳。 */
        if (s->srcBits)
            surface_flush(s);
        /* 从 attach(NULL) 恢复后 configure 早已 ack，不会再有回调触发 flush；
         * 若此时还没有画布，补一次裸 commit 让合成器重新 map。 */
        else if (s->role == SWINX_WL_SURF_TOPLEVEL && s->surface)
            wl_surface_commit(s->surface);
    }
}

void SwinxWlSurfaceSetTitle(SwinxWlSurface *s, const char *title)
{
    if (s && s->toplevel && title)
        xdg_toplevel_set_title(s->toplevel, title);
}

void SwinxWlSurfaceBeginDrag(SwinxWlSurface *s, unsigned int htCode)
{
    /* 只处理 toplevel：popup/subsurface 没有 xdg_toplevel 对象，
     * xdg_toplevel_move 对它们是协议错误（GTK is_realized_toplevel 同款闸）。 */
    if (!s || !s->toplevel || !g.ready)
        return;
    WlSeat *seat = NULL;
    for (int i = 0; i < g.nSeats; ++i)
        if (g.seats[i].pointer) { seat = &g.seats[i]; break; }
    if (!seat || !seat->pressSerial)
        return; // 没有 press 过的 serial：合成器会拒绝，索性不发

    if (htCode == HTCAPTION)
    {
        xdg_toplevel_move(s->toplevel, seat->seat, seat->pressSerial);
        return;
    }
    unsigned edge = 0;
    switch (htCode)
    {
    case HTLEFT:        edge = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;        break;
    case HTRIGHT:       edge = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;       break;
    case HTTOP:         edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP;         break;
    case HTBOTTOM:      edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;      break;
    case HTTOPLEFT:     edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;    break;
    case HTTOPRIGHT:    edge = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;   break;
    case HTBOTTOMLEFT:  edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; break;
    case HTBOTTOMRIGHT: edge = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;break;
    default: return;
    }
    xdg_toplevel_resize(s->toplevel, seat->seat, seat->pressSerial, edge);
}

void SwinxWlSurfaceSetState(SwinxWlSurface *s, unsigned int stateMask)
{
    if (!s || !s->toplevel || !g.ready)
        return;
    /* mask 是"期望的完整状态集"：未置位的项要显式撤销，否则 SW_RESTORE 后
     * 合成器仍保持 maximized。configure 会把最终状态回投给 SConnection。 */
    if (stateMask & SWINX_WL_TOPSTATE_MAXIMIZED)
        xdg_toplevel_set_maximized(s->toplevel);
    else
        xdg_toplevel_unset_maximized(s->toplevel);

    if (stateMask & SWINX_WL_TOPSTATE_FULLSCREEN)
        xdg_toplevel_set_fullscreen(s->toplevel, NULL);
    else
        xdg_toplevel_unset_fullscreen(s->toplevel);

    if (stateMask & SWINX_WL_TOPSTATE_MINIMIZED)
    {
#if defined(XDG_TOPLEVEL_SET_MINIMIZED_SINCE_VERSION)
        xdg_toplevel_set_minimized(s->toplevel);
#endif
    }
    else if (s->toplevelStates & SWINX_WL_TOPSTATE_MINIMIZED)
    {
        /* 协议只有 set_minimized，没有 unset_minimized；恢复只能请合成器
         * 重新激活（GTK/SDL 同款做法）。需要 pointer/keyboard seat。 */
        for (int i = 0; i < g.nSeats; ++i)
        {
#if defined(XDG_TOPLEVEL_ACTIVATE_SINCE_VERSION)
            if (g.seats[i].seat)
                xdg_toplevel_activate(s->toplevel, g.seats[i].seat);
#endif
        }
    }
}

unsigned int SwinxWlSurfaceGetState(SwinxWlSurface *s)
{
    return s ? s->toplevelStates : 0;
}

void SwinxWlSurfacePresent(SwinxWlSurface *s, const uint32_t *bits, int stride,
                           const RECT *dirty)
{
    if (!s || !bits || stride < s->w)
        return;
    s->srcBits = bits;
    s->srcStride = stride;
    // 每块 buffer 都要记这次改动：它可能在别的上屏周期里正被合成器持有，
    // release 后再次被选中时必须补齐这段时间的全部脏区。
    for (int i = 0; i < s->nBufs; ++i)
        mark_dirty(&s->bufs[i], dirty, s->w, s->h);
    surface_flush(s);
}

void SwinxWlSurfacesFlushFrame(void)
{
    if (!g.ready)
        return;
    for (SwinxWlSurface *s = g.surfaces; s; s = s->next)
        surface_flush(s);
    wl_display_flush(g.display);
}

int SwinxWlInit(void)
{
    if (g.ready)
        return 1;
    if (g.tried)
        return 0;
    g.tried = 1;
    if (!s_fb)
        fb_alloc(SWINX_WL_DEFAULT_W, SWINX_WL_DEFAULT_H);

    g.display = wl_display_connect(NULL);
    if (!g.display)
        return 0;
    g.registry = wl_display_get_registry(g.display);
    wl_registry_add_listener(g.registry, &registry_listener, NULL);
    if (wl_display_roundtrip(g.display) < 0)
        goto fail;
    data_devices_ensure();
    // subcompositor 可选：没有它时所有窗口都退化成独立 toplevel，仍能显示。
    if (!g.compositor || !g.shm || !g.wm_base)
        goto fail;
    g.ready = 1;
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    /* 倍率探针：临时 surface 只为拿 preferred_scale，不 attach buffer、不建
     * xdg_surface 角色（mutter 对无角色 surface 也能回 preferred_scale）。
     * 必须在第二次 roundtrip 里读到，早于应用创建第一个窗口/首次查 DPI。 */
    if (scaleEnabled() && g.compositor && g.fractionalScaleManager)
    {
        struct wl_surface *probe = wl_compositor_create_surface(g.compositor);
        struct wp_fractional_scale_v1 *fs = probe
            ? wp_fractional_scale_manager_v1_get_fractional_scale(
                  g.fractionalScaleManager, probe) : NULL;
        if (fs)
            wp_fractional_scale_v1_add_listener(fs, &probe_scale_listener, NULL);
        if (wl_display_roundtrip(g.display) < 0)
        {
            g.ready = 0;
            goto fail;
        }
        if (fs) wp_fractional_scale_v1_destroy(fs);
        if (probe) wl_surface_destroy(probe);
        if (wl_trace())
            fprintf(stderr, "[wl-scale] probe preferred=%d/120 (%.3f)\n",
                    s_probeScale120, s_probeScale120 / 120.0);
    }
    else
#endif
    if (wl_display_roundtrip(g.display) < 0) // seat capabilities / xdg ping
    {
        g.ready = 0;
        goto fail;
    }
    data_devices_ensure();
    return 1;
fail:
    SwinxWlShutdown();
    return 0;
}

void SwinxWlShutdown(void)
{
    for (SwinxWlSurface *s = g.surfaces; s;)
    {
        SwinxWlSurface *next = s->next;
        SwinxWlSurfaceDestroy(s->hwnd);
        s = next;
    }
    for (int i = 0; i < g.nSeats; ++i)
    {
        WlSeat *s = &g.seats[i];
        if (s->selectionSource)
        {
            WlDataSource *src = s->selectionSource;
            s->selectionSource = NULL;
            if (src->src) wl_data_source_destroy(src->src);
            delete src;
        }
        if (s->dragSource)
        {
            WlDataSource *src = s->dragSource;
            s->dragSource = NULL;
            if (src->src) wl_data_source_destroy(src->src);
            delete src;
        }
        if (s->dndOffer)
            dnd_offer_destroy(s->dndOffer);
        if (s->selectionOffer)
        {
            offer_destroy(s->selectionOffer);
            s->selectionOffer = NULL;
        }
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
        free(s->imeCommit);
        s->imeCommit = NULL;
        if (s->textInput)
            zwp_text_input_v3_destroy(s->textInput);
#endif
        if (s->dataDevice)
            wl_data_device_destroy(s->dataDevice);
        cursor_destroy(s);
        if (s->pointer) wl_pointer_destroy(s->pointer);
        if (s->keyboard) wl_keyboard_destroy(s->keyboard);
        if (s->seat) wl_seat_destroy(s->seat);
    }
    g.nSeats = 0;
    for (int i = 0; i < g.nOutputs; ++i)
        if (g.outputs[i].output) wl_output_destroy(g.outputs[i].output);
    g.nOutputs = 0;
#if defined(SWINX_HAVE_TEXT_INPUT_V3)
    if (g.textInputManager)
        zwp_text_input_manager_v3_destroy(g.textInputManager);
    g.textInputManager = NULL;
#endif
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    if (g.fractionalScaleManager)
        wp_fractional_scale_manager_v1_destroy(g.fractionalScaleManager);
    if (g.viewporter)
        wp_viewporter_destroy(g.viewporter);
#endif
    if (g.dataDeviceManager) wl_data_device_manager_destroy(g.dataDeviceManager);
    if (g.wm_base) xdg_wm_base_destroy(g.wm_base);
    if (g.shm) wl_shm_destroy(g.shm);
    if (g.subcompositor) wl_subcompositor_destroy(g.subcompositor);
    if (g.compositor) wl_compositor_destroy(g.compositor);
    if (g.registry) wl_registry_destroy(g.registry);
    if (g.display) wl_display_disconnect(g.display);
    memset(&g, 0, sizeof(g));
#if defined(SWINX_HAVE_FRACTIONAL_SCALE)
    s_probeScale120 = 0;
#endif
    g.tried = 1;
}

int SwinxWlDispatch(void)
{
    if (!g.display)
        return 0;
    if (wl_display_dispatch_pending(g.display) < 0)
        return -1;
    if (wl_display_flush(g.display) < 0 && errno != EAGAIN)
        return -1;
    struct pollfd pfd;
    pfd.fd = wl_display_get_fd(g.display);
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)))
    {
        if (wl_display_dispatch(g.display) < 0)
            return -1;
    }
    /* 一批事件读完再决定 leave 是否成立：合成器 repick 的 leave→enter 会在
     * 上面被配对丢弃，只有真正没有后续 enter 的 leave 才落到这里定案。 */
    pointer_flush_leaves();
    return 0;
}

int SwinxWlFlush(void)
{
    return g.display ? wl_display_flush(g.display) : 0;
}

int SwinxWlFd(void)
{
    return g.display ? wl_display_get_fd(g.display) : -1;
}

#else // !SWINX_HAVE_WAYLAND_CLIENT

SwinxWlSurface *SwinxWlSurfaceCreate(void *hwnd, int w, int h, int role, void *parent)
{ (void)hwnd; (void)w; (void)h; (void)role; (void)parent; return NULL; }
/* 无 wayland-client 时保留 app_id 字符串状态，但不会有 toplevel 可下发。 */
void SwinxWlSetAppId(const char *appId)
{ (void)appId; }
const char *SwinxWlGetAppId(void) { return "soui"; }
SwinxWlSurface *SwinxWlSurfaceGet(void *) { return NULL; }
void SwinxWlSurfaceDestroy(void *) {}
void SwinxWlSurfaceSetTransientParent(SwinxWlSurface *, void *) {}
void SwinxWlSurfaceSetPopAnchor(SwinxWlSurface *, void *,
                                int, int, int, int, int, int, int, int) {}
void SwinxWlSurfaceSetGeometry(SwinxWlSurface *, int, int, int, int) {}
void SwinxWlSurfaceSetVisible(SwinxWlSurface *, int) {}
void SwinxWlSurfaceSetTitle(SwinxWlSurface *, const char *) {}
int SwinxWlSurfaceGetRole(SwinxWlSurface *) { return SWINX_WL_SURF_TOPLEVEL; }
void *SwinxWlSurfaceGetPopupParentHwnd(SwinxWlSurface *) { return NULL; }
int SwinxWlSurfaceSetDialogParent(SwinxWlSurface *, void *) { return 0; }
void SwinxWlSurfaceSetInputTransparent(SwinxWlSurface *, int) {}
void SwinxWlSurfaceSetInputInset(SwinxWlSurface *, int, int, int, int) {}
void SwinxWlSetCursorImage(const uint32_t *, int, int, int, int) {}
int SwinxWlDragIconImage(const uint32_t *, int, int, int, int) { return 0; }
void SwinxWlShowCursor(int) {}
void SwinxWlRefreshCursor(void) {}
void SwinxWlSurfaceBeginDrag(SwinxWlSurface *, unsigned int) {}
void SwinxWlSurfaceSetState(SwinxWlSurface *, unsigned int) {}
unsigned int SwinxWlSurfaceGetState(SwinxWlSurface *) { return 0; }
int SwinxWlSelectionSet(void *, const char *const *, int, SwinxWlDataSourceRead, unsigned int) { return 0; }
void SwinxWlSelectionClear(void) {}
int SwinxWlSelectionHasMime(const char *) { return 0; }
int SwinxWlSelectionRead(const char *, void **) { return -1; }
unsigned int SwinxWlSelectionGeneration(void) { return 0; }
int SwinxWlSelectionOwned(void) { return 0; }
void SwinxWlDataFree(void *) {}
int SwinxWlStartDrag(void *, const char *const *, int, SwinxWlDataSourceRead,
                     SwinxWlDragQuery, void *, unsigned int) { return 0; }
void SwinxWlDndOfferAccept(SwinxWlDataOffer *, const char *) {}
int SwinxWlDndOfferHasMime(SwinxWlDataOffer *, const char *) { return 0; }
int SwinxWlDndOfferRead(SwinxWlDataOffer *, const char *, void **) { return -1; }
void SwinxWlDndOfferFinish(SwinxWlDataOffer *) {}
void SwinxWlDndOfferSetEffect(SwinxWlDataOffer *, unsigned int) {}
void SwinxWlSurfacePresent(SwinxWlSurface *, const uint32_t *, int, const RECT *) {}
void SwinxWlSurfacesFlushFrame(void) {}
int SwinxWlInit(void) { return 0; }
void SwinxWlShutdown(void) {}
int SwinxWlDispatch(void) { return 0; }
int SwinxWlFlush(void) { return 0; }
int SwinxWlFd(void) { return -1; }

#endif // SWINX_HAVE_WAYLAND_CLIENT

// ---------------------------------------------------------------------------
// 公共 shim 实现
// ---------------------------------------------------------------------------

uint32_t *SwinxFbBits(void)
{
    if (!s_fb)
        fb_alloc(s_width, s_height);
    return s_fb;
}
int SwinxFbWidth(void) { return s_width; }
int SwinxFbHeight(void) { return s_height; }
int SwinxFbStride(void) { return s_width; }
double SwinxGetCanvasScale(void) { return 1.0; }

void SwinxFbClear(uint32_t argb)
{
    if (!s_fb)
        return;
    size_t n = (size_t)s_width * (size_t)s_height;
    for (size_t i = 0; i < n; ++i)
        s_fb[i] = argb;
}

void SwinxFbCountRequest(void) { ++s_requests; }
int SwinxFbRequests(void) { return s_requests; }
int SwinxFbPresents(void) { return s_presents; }

void SwinxWlSetEventCb(SwinxWlEventCb cb) { s_eventCb = cb; }
