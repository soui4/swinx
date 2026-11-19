/*
 * framebuffer.h -- Wayland 后端：每窗口 wl_surface + SHM 双缓冲（GTK3 模型）。
 *
 * 架构（对齐 GTK 3.24.48 gdkwindow-wayland.c）：
 *   - 主窗口 = 一个 xdg_toplevel；其余窗口（MessageBox / 下拉窗 / WS_CHILD）
 *     是父 surface 的 wl_subsurface，坐标沿用 SOUI 的屏幕矩形（相对父原点）。
 *     由合成器做重叠与 z 序，不再在 CPU 侧软件合成整屏。
 *   - 每个窗口有自己的 wl_shm pool + 最多 3 块 wl_buffer（按需分配、release
 *     回收），自己的累积 damage 与自己的 wl_surface_frame 回调：一次回调周期
 *     内至多 attach+damage+commit 一次（GTK on_frame_clock_after_paint）。
 *   - wl_seat 的 pointer/keyboard 事件经 SwinxWlEventCb 回投给 SConnection，
 *     由 SConnection 转成 Win32 消息（GTK gdkdevice-wayland.c 的等价路径）。
 *
 * 保留的全局 SwinxFb* 只是"虚拟屏幕尺寸"查询 shim（原 wasm/freertos 模型的
 * 对外契约），像素不再经过它；真正的上屏走 SwinxWlSurface*。
 *
 * 编译时若找不到 wayland-client，本文件退化为纯内存缓冲：库仍可构建，只是
 * 不会显示窗口。
 */
#ifndef _SWINX_WAYLAND_FRAMEBUFFER_H_
#define _SWINX_WAYLAND_FRAMEBUFFER_H_

#include <ctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 全局虚拟屏幕 shim（仅供 GetSystemMetrics / 弹出窗翻转判定）-------- */

uint32_t *SwinxFbBits(void);
int SwinxFbWidth(void);
int SwinxFbHeight(void);
int SwinxFbStride(void);
void SwinxFbClear(uint32_t argb);
int SwinxFbRequests(void);
int SwinxFbPresents(void);
void SwinxFbCountRequest(void);
double SwinxGetCanvasScale(void);

/* ---- 每窗口 surface ------------------------------------------------------ */

typedef struct SwinxWlSurface SwinxWlSurface;

/* 窗口角色（对齐 GTK 3.24.48 gdk_wayland_window_map 的三分支）：
 *   TOPLEVEL   - 主窗口 / 普通对话框，xdg_toplevel，合成器任务列表可见；
 *   SUBSURFACE - WS_CHILD 子控件 / 无 positioner 的弹窗，wl_subsurface；
 *   POPUP      - tooltip / 菜单，xdg_popup + xdg_positioner。合成器只把
 *                toplevel 当应用窗口，所以 popup 天然不进任务列表；位置由
 *                positioner 的 anchor_rect 决定（GTK 的 tooltip 走同一路径）。
 * TOPLEVEL 时 parent 忽略；SUBSURFACE/POPUP 必须给出父窗口 HWND。 */
enum {
    SWINX_WL_SURF_TOPLEVEL = 0,
    SWINX_WL_SURF_SUBSURFACE = 1,
    SWINX_WL_SURF_POPUP = 2,
};

/* 创建（幂等：同一 hwnd 已有 surface 时直接返回旧对象并按需更新尺寸）。
 * w/h 来自 CREATESTRUCT 的 cx/cy，画布随后由 wnd.cpp 以同尺寸创建。 */
SwinxWlSurface *SwinxWlSurfaceCreate(void *hwnd, int w, int h, int role, void *parentHwnd);
SwinxWlSurface *SwinxWlSurfaceGet(void *hwnd);
void SwinxWlSurfaceDestroy(void *hwnd);

/* ---- 窗口图标 / app_id ---------------------------------------------------
 * Wayland 没有 X11 的 _NET_WM_ICON 那种"按窗口传位图"的协议：合成器/taskbar
 * 拿到的是 xdg_toplevel.app_id，再用它去 $XDG_DATA_DIRS/applications\*.desktop
 * 查 Icon=。因此这里维护一个全局 app_id，创建每个 xdg_toplevel 时下发。
 *
 * 取值优先级：
 *   1) SwinxWlSetAppId() 显式设置（应用可在一开始调用）；
 *   2) 环境变量 SOUI_WL_APP_ID（部署时为没有 desktop 文件的二进制指定）；
 *   3) /proc/self/exe 的 basename（对齐 X11 的 WM_CLASS 第一字段 = 进程名）；
 *   4) 兜底 "soui"。
 * 要让图标真正出现，还需随应用安装同名的 <app_id>.desktop（Icon= 指向图标）。
 *
 * setAppId 会就地更新所有已存在的 toplevel（含 CSD 装饰窗），传 NULL 表示
 * 重新从环境/进程名推导。getAppId 返回内部静态缓冲，调用方不得释放。 */
void SwinxWlSetAppId(const char *appId);
const char *SwinxWlGetAppId(void);

/* 设置 toplevel 的 transient parent（xdg_toplevel_set_parent，对齐 GTK
 * gdkwindow-wayland.c:1300）。SOUI 的普通对话框是无 WS_CHILD 的独立顶层窗，
 * 必须告诉合成器它的归属：否则每个都成为平级 xdg_toplevel，合成器的任务
 * 列表会把它们全列成独立"主窗口"（实测 10+ 个）。parentHwnd=NULL 清除关系。
 * 只对 toplevel 有效；subsurface 的父子关系在创建时由 wl_subcompositor 固定，
 * popup 的父子关系在 setPopAnchor 里由 xdg_surface_get_popup 固定。 */
void SwinxWlSurfaceSetTransientParent(SwinxWlSurface *s, void *parentHwnd);

/* POPUP 角色专用：设置 popup 相对其父窗口的定位并提交（对齐 GTK
 * gdk_wayland_window_map 的 create_xdg_popup + xdg_positioner 路径）。
 *   anchorX/anchorY/anchorW/anchorH = 父窗口局部坐标里的锚矩形；SOUI 的
 *   tooltip 直接把目标位置当作 1x1 锚点（等价于 GTK create_simple_positioner）。
 *   winW/winH = popup 尺寸，交给 xdg_positioner.set_size 让合成器做边界约束；
 *   winX/winY 与锚点重合（anchor=TOP_LEFT / gravity=BOTTOM_RIGHT / offset=0）。
 * 只在 POPUP 角色有效；popup 的位置在 xdg_popup 创建后不可改，位置变化时
 * 内部会销毁并重建 xdg_popup（GTK bug#772505 同款 workaround）。 */
void SwinxWlSurfaceSetPopAnchor(SwinxWlSurface *s, void *parentHwnd,
                                int anchorX, int anchorY, int anchorW, int anchorH,
                                int winX, int winY, int winW, int winH);

/* x/y 是相对父窗口原点的位置（subsurface），toplevel 忽略。
 * 若当前正在隐藏，show=1 会重新挂上最近一帧 buffer。 */
void SwinxWlSurfaceSetGeometry(SwinxWlSurface *s, int x, int y, int w, int h);
void SwinxWlSurfaceSetVisible(SwinxWlSurface *s, int visible);
void SwinxWlSurfaceSetTitle(SwinxWlSurface *s, const char *title);

/* 角色查询：SConnection 需要知道"真正作为 popup 父 surface 的 HWND"才能把
 * SOUI 的屏幕坐标换成 xdg_positioner 要的父局部坐标（父可能是 subsurface，
 * framebuffer 会沿父链找到最近的有 xdg_surface 的祖先；两边必须选同一个）。 */
int SwinxWlSurfaceGetRole(SwinxWlSurface *s);
void *SwinxWlSurfaceGetPopupParentHwnd(SwinxWlSurface *s);

/* owned 对话框（如 swinx 原生 MessageBox：WS_POPUP + SetParent(owner)）转成
 * POPUP 角色：Wayland 的 xdg_toplevel 没有客户端设置位置的协议，只有
 * xdg_popup + xdg_positioner 能让 SOUI 按 Win32 语义算好的坐标落地。
 * 必须在对话框的子窗口创建之前调用——转换会销毁旧 wl_surface，而子窗的
 * wl_subsurface 是绑定到具体 wl_surface 的。
 * 返回 1 = 已转成 popup；0 = 条件不满足（父窗口的 xdg_surface 还没建好、
 * role 已经不是 toplevel 等），调用方应记下来等 surface 就绪后重试。 */
int SwinxWlSurfaceSetDialogParent(SwinxWlSurface *s, void *parentHwnd);

/* 输入穿透：tooltip 不能吃鼠标事件。SOUI 的 STipCtrl::PreTranslateMessage
 * 会把落在 tooltip 自己窗口上的 WM_MOUSEMOVE 当成"离开目标"，立即隐藏
 * tooltip；GTK 的 tooltip 不受此影响是因为它没有这层预处理。用空
 * wl_region 让合成器把指针事件继续投给下面的窗口。 */
void SwinxWlSurfaceSetInputTransparent(SwinxWlSurface *s, int transparent);
/* 把输入区收缩到 surface 内框（CSD：让边缘的指针事件落到装饰窗，从而能
 * 映射成 xdg_toplevel_resize）。l/t/r/b 是要让出的像素数。 */
void SwinxWlSurfaceSetInputInset(SwinxWlSurface *s, int l, int t, int r, int b);

/* wl_output 上报的真实输出尺寸（逻辑像素）。SOUI 的 CenterWindow /
 * MonitorFromRect / tooltip 翻转都依赖它；不能用首个窗口的 configure 尺寸
 * 冒充屏幕尺寸。返回 0 表示还没有收到 output.mode。 */
int SwinxWlGetScreenSize(int *width, int *height);

/* ---- 每显示器 DPI / 非整数缩放 -------------------------------------------
 * scale 统一用 1/120 定点数：120=1.0、150=1.25、180=1.5、240=2.0。
 * wp_fractional_scale_v1 上报的就是这个单位。
 *   GetOutputCount/GetOutputRect/GetOutputScale120：按 wl_output 顺序枚举
 *     （index 0 为主屏），rect 是逻辑像素（物理像素 / 倍率）。
 *   GetSurfaceScale120：某窗口 surface 实际采用的倍率；没有 fractional_scale
 *     协议时退到所在（或首个）output 的整数 scale*120。返回 120 表示未缩放。
 *   GetSystemScale120：全局默认（主屏）倍率，供无窗口上下文的 GetDpi 使用。
 * 这些查询都是元数据，不改变窗口坐标：坐标换算在 framebuffer 的事件回投里
 * 按 surface 倍率完成（见 pointer/configure 路径的注释）。 */
int SwinxWlGetOutputCount(void);
int SwinxWlGetOutputRect(int index, RECT *rc);
int SwinxWlGetOutputScale120(int index);
int SwinxWlGetSurfaceScale120(void *hwnd);
int SwinxWlGetSystemScale120(void);
/* 由 SOUI 的 GetDpi/BuildDpi 调用：把倍率换算成 96 基准 DPI（四舍五入到档位）。
 * 应用可设 SWINX_WL_SCALE=0 关闭所有缩放（返回 96、viewport 不生效）。 */
int SwinxWlGetDpi(void *hwnd);

/* 客户端自绘 NC 的交互式移动/缩放（对齐 GTK
 * gdk_wayland_window_begin_move_drag/resize_drag）：Wayland 没有"标题栏"概念，
 * 合成器不会自动移动自绘 NC 的 xdg_toplevel，客户端必须在 pointer press 拿到
 * serial 后主动发 xdg_toplevel_move/resize。htCode 用 Win32 WM_NCHITTEST 值
 * （HTCAPTION / HTLEFT..HTBOTTOMRIGHT）；serial 取 framebuffer 缓存的最近一次
 * pointer press serial——用错 serial 合成器会静默拒绝。 */
void SwinxWlSurfaceBeginDrag(SwinxWlSurface *s, unsigned int htCode);

/* 把窗口 cairo image surface 的一块脏区上传到自己的 SHM buffer。
 * bits 指向 cairo ARGB32 像素，stride 以像素计（cairo stride/4）。
 * 内部只累积 damage；frame callback 未回来时本帧至多提交一次。 */
void SwinxWlSurfacePresent(SwinxWlSurface *s, const uint32_t *bits, int stride,
                           const RECT *dirty);

/* 帧末统一 flush：把所有本帧累积了 damage 的 surface 在一次
 * wl_display_flush() 里送出去。SConnection::flushPendingCommits 末尾调用。 */
void SwinxWlSurfacesFlushFrame(void);

/* ---- xdg_toplevel 状态（最大化/最小化/全屏）-----------------------------
 * mask 位：1=maximized, 2=minimized, 4=fullscreen；未置位的项保持原样。
 * 合成器 configure 回来的状态经 SWINX_WL_EVT_TOPLEVEL_STATE 回投。 */
enum {
    SWINX_WL_TOPSTATE_MAXIMIZED = 1,
    SWINX_WL_TOPSTATE_MINIMIZED = 2,
    SWINX_WL_TOPSTATE_FULLSCREEN = 4,
};
void SwinxWlSurfaceSetState(SwinxWlSurface *s, unsigned int stateMask);
unsigned int SwinxWlSurfaceGetState(SwinxWlSurface *s);

/* ---- 剪贴板 / 拖放数据通道（wl_data_device）------------------------------
 * 这一层只管协议与字节，Win32 格式（CF_UNICODETEXT 等）的转换在 SClipboard。
 *   read(ctx, mime, buf, cap) 返回本次写入的字节数；0 = 数据结束，-1 = 失败。
 *   调用方会反复调用直到返回 0，因此大对象不需要一次性装进内存。
 *   每次 wl_data_source.send 开始前会以 buf==NULL 调用一次表示重置，
 *   回调必须把分块 offset 归零；读完后必须保持 EOF 状态返回 0。
 *   query(ctx, keyState) 在拖动过程中被周期性调用：1=继续，0=放下，-1=取消。 */
typedef struct WlDataOffer SwinxWlDataOffer;
typedef int (*SwinxWlDataSourceRead)(void *ctx, const char *mime, void *buf, int cap);
typedef int (*SwinxWlDragQuery)(void *ctx, int keyState);

/* 发布 selection；mimes 与 read 回调的生命周期由调用方负责（直到被替换）。
 * serial 必须是最近一次用户事件的 serial；0 时用缓存的上一个 serial。 */
int SwinxWlSelectionSet(void *ctx, const char *const *mimes, int nMimes,
                        SwinxWlDataSourceRead read, unsigned int serial);
void SwinxWlSelectionClear(void);
int SwinxWlSelectionHasMime(const char *mime);
/* 返回读到的字节数（-1 失败）；输出缓冲用 SwinxWlDataFree 释放。 */
int SwinxWlSelectionRead(const char *mime, void **outBuf);
/* selection 每次变化自增：SClipboard 用它判定本地缓存是否失效。 */
unsigned int SwinxWlSelectionGeneration(void);
int SwinxWlSelectionOwned(void);  // 当前 selection 是否由本客户端发布
void SwinxWlDataFree(void *p);

/* 启动拖动，阻塞到合成器报告 dnd_finished / 取消 / 超时（20s）。
 * 返回 1=已放下，0=取消。originHwnd 必须已创建 wl_surface。 */
int SwinxWlStartDrag(void *ctx, const char *const *mimes, int nMimes,
                     SwinxWlDataSourceRead read, SwinxWlDragQuery query,
                     void *originHwnd, unsigned int serial);
/* 目标侧：在 enter/motion 事件里接受一个 mime（v3 用 set_actions，旧版用 accept）。*/
void SwinxWlDndOfferAccept(SwinxWlDataOffer *offer, const char *mime);
int SwinxWlDndOfferHasMime(SwinxWlDataOffer *offer, const char *mime);
int SwinxWlDndOfferRead(SwinxWlDataOffer *offer, const char *mime, void **outBuf);
void SwinxWlDndOfferFinish(SwinxWlDataOffer *offer);
void SwinxWlDndOfferSetEffect(SwinxWlDataOffer *offer, unsigned int effect);

/* ---- 回投给 SConnection 的事件 ------------------------------------------ */

enum {
    SWINX_WL_EVT_CONFIGURE = 1,   /* win, a=w, b=h */
    SWINX_WL_EVT_CLOSE = 2,       /* win */
    SWINX_WL_EVT_MOTION = 3,      /* win, a=x, b=y, c=state(bitmask) */
    SWINX_WL_EVT_BUTTON = 4,      /* win, a=button(1左2右3中), b=down, c=x, d=y, time=合成器时间戳 */
    SWINX_WL_EVT_ENTER = 5,       /* win, a=x, b=y, c=state */
    SWINX_WL_EVT_LEAVE = 6,       /* win */
    SWINX_WL_EVT_AXIS = 7,        /* win, a=dx, b=dy(像素，已经乘过 120) */
    SWINX_WL_EVT_KEY = 8,         /* win, a=xkb scancode, b=down */
    SWINX_WL_EVT_MODIFIERS = 9,   /* win, a=state(bitmask) */
    SWINX_WL_EVT_POPUP_DONE = 10, /* win; xdg_popup.done，合成器要求关闭弹窗 */
    SWINX_WL_EVT_POPUP_CONFIGURE = 11, /* win, a=x, b=y, c=w, d=h（父窗口局部坐标）*/
    SWINX_WL_EVT_DND_ENTER = 12,       /* win=目标, a=x, b=y, c=keyState, p=offer */
    SWINX_WL_EVT_DND_MOTION = 13,      /* 同上 */
    SWINX_WL_EVT_DND_LEAVE = 14,       /* win=目标, p=NULL */
    SWINX_WL_EVT_DND_DROP = 15,        /* win=目标, p=offer（读完后 offer 失效）*/
    SWINX_WL_EVT_TOPLEVEL_STATE = 16,  /* win, a=SWINX_WL_TOPSTATE_* */
    SWINX_WL_EVT_IME_COMMIT = 17,      /* win=焦点窗口, a=serial, p=SwinxWlImeCommit*（接收方负责 free）*/
};

/* text-input-v3 的 commit_string：UTF-8 字节，接收方转成 WM_IME_CHAR。 */
typedef struct {
    char *utf8;
    int serial;
} SwinxWlImeCommit;

/* state bitmask: bit0=LEFT, bit1=RIGHT, bit2=MIDDLE,
 *                bit3=SHIFT, bit4=CTRL, bit5=ALT, bit6=CAPS */
enum {
    SWINX_WL_STATE_LEFT   = 1 << 0,
    SWINX_WL_STATE_RIGHT  = 1 << 1,
    SWINX_WL_STATE_MIDDLE = 1 << 2,
    SWINX_WL_STATE_SHIFT  = 1 << 3,
    SWINX_WL_STATE_CTRL   = 1 << 4,
    SWINX_WL_STATE_ALT    = 1 << 5,
    SWINX_WL_STATE_CAPS   = 1 << 6,
};

typedef struct {
    int type;
    void *win;                 /* HWND（ subsurface 命中时是子窗口自己） */
    int a, b, c, d;
    void *p;                   /* DND_* 事件里的 SwinxWlDataOffer* */
    /* 合成器时间戳（wl_pointer/wl_keyboard 的 time，毫秒），0 = 调用方没填、
     * 接收方按本地时钟兜底。双击判定必须用它：消息泵时钟会被队列积压扭曲，
     * 把两次独立点击压成"相邻"，而协议时间戳始终是事件真实发生的时刻。 */
    unsigned int time;
} SwinxWlEvent;

typedef void (*SwinxWlEventCb)(const SwinxWlEvent *ev);
void SwinxWlSetEventCb(SwinxWlEventCb cb);

/* 输入法：commit_string 由事件回投；启用状态与光标矩形由这里下发。 */
void SwinxWlImeSetEnabled(int enabled);
void SwinxWlImeSetCursorRect(int x, int y, int w, int h);

/* ---- 指针光标（seat 级，对齐 GTK gdkwaylanddevice.c）--------------------
 * bits 是 cairo ARGB32（预乘、内存序 B,G,R,A），由 SConnection 从 HCURSOR
 * 的 32bpp 位图取出后复制进来；framebuffer 自己持有 SHM buffer，调用方
 * 可以在返回后释放 bits。serial 用最近一次 pointer enter 的，每次跨 surface
 * 都必须重设，否则严格合成器会忽略请求。 */
void SwinxWlSetCursorImage(const uint32_t *bits, int w, int h,
                           int hotspotX, int hotspotY);
/* 拖动图标：DnD grab 期间合成器忽略 set_cursor，拖动光标只能靠 drag icon
 * 表达。SConnection 在 GiveFeedback 里把与 X11 SetCursor 相同的 IDC_* 位图
 * 交给这里显示（热点用 attach 偏移对齐）。返回 0 表示本帧没送出去，需重试。 */
int SwinxWlDragIconImage(const uint32_t *bits, int w, int h,
                         int hotspotX, int hotspotY);
/* 当前拖放目标选定的 effect (DROPEFFECT_*)，由 wl_data_source.action 事件回写。
 * 没有收到任何 action 时为 DROPEFFECT_COPY。 */
uint32_t SwinxWlDragEffect(void);
void SwinxWlShowCursor(int visible); /* ShowCursor 计数 <= 0 时交还合成器 */
void SwinxWlRefreshCursor(void);

/* ---- 连接与事件泵 -------------------------------------------------------- */

int SwinxWlInit(void);
void SwinxWlShutdown(void);
int SwinxWlDispatch(void);
int SwinxWlFlush(void);
int SwinxWlFd(void);

#ifdef __cplusplus
}
#endif

#endif // _SWINX_WAYLAND_FRAMEBUFFER_H_
