#ifndef _SDC_H_
#define _SDC_H_
#include <windows.h>
#if defined(__APPLE__)
#undef interface
typedef struct CGContext *CGContextRef;
struct CGAffineTransform;
typedef const struct CGPath *CGPathRef;
#elif defined(__EMSCRIPTEN__)
// route C（canvas2d）：不依赖 cairo，故不 include <cairo.h>；cairo_surface_t 等
// 不透明返回类型在下方 __EMSCRIPTEN__ 分支前向声明（base 代码把它当不透明指针）。
#else
#include <cairo.h>
#endif

#include <region.h>
#include <vector>
#include <memory>

#if defined(__EMSCRIPTEN__)
// route C (canvas2d) GDI 后端：所有绘制经浏览器 Canvas 2D 完成，不依赖 cairo。
// 前向声明让 base 代码（wnd.cpp 把 CreateWindowSurface 的返回值当不透明指针
// 包进 HBITMAP）在缺少 <cairo.h> 时仍可通过编译；真实类型在 canvas2d 后端里。
struct cairo_surface_t;
struct DCState;
#include "gdi/canvas2d/js_glue.h"
struct HDC__
{
    HWND hwnd;
    // Canvas 2D 引用：窗口 DC 指向其后备离屏 canvas；内存 DC 选中位图后指向该
    // 位图的 canvas。坐标均为「逻辑像素」，Retina 缩放由上层（framebuffer 物理尺寸）
    // 统一处理，此处保持 1:1，便于与既有上屏链路逐字节兼容。
    emscripten::val canvas;   // HTMLCanvasElement（窗口后备画布 / 位图后备画布）
    emscripten::val ctx;      // CanvasRenderingContext2D
    int surfW;                // canvas.width（逻辑像素）
    int surfH;                // canvas.height
    // 路径记录（BeginPath/EndPath/StrokePath/FillPath/PathToRegion）。
    std::vector<PathOp> recordedPath;
    // 路径里是否已经有当前点。BeginPath 之后、首个 MoveToEx 之前为 FALSE：
    // 这期间的 LineTo 必须先补 moveTo，否则 canvas 会从上一个子路径连一条长线
    // （SWaveProgress 的折线错乱就是这么来的）。
    BOOL pathHasPos;
    FLOAT miterLimit;         // SetMiterLimit/GetMiterLimit
    int stretchBltMode;       // SetStretchBltMode/GetStretchBltMode（StretchBlt 抗锯齿开关）
    // 视图原点（SetViewportOrgEx/OffsetViewportOrgEx）。GDI 里它与「当前画笔位置」
    // ptOrigin 是两个互不相干的东西，不能共用一个字段：STreeCtrl 每画一行都要
    // OffsetViewportOrg 定位行内容，画完再减回去，与 MoveToEx/LineTo 的当前点完全无关。
    POINT ptViewport;
    // 窗口原点（SetWindowOrgEx）。设备坐标 = 逻辑坐标 - 窗口原点 + 视图原点。
    POINT ptWinOrg;
    // 世界变换（SetWorldTransform / GM_ADVANCED）：设备 = World(逻辑) + 视图原点。
    // render-gdi 的 DrawSVG 靠它把 SVG 原始坐标映射到目标矩形。
    XFORM xfWorld;
    // 当前裁剪区的**设备坐标包围盒**，即 GetClipBox 的返回值。
    // canvas 无法反查 clip 状态，只能自己记账：IntersectClipRect / SelectClipRgn /
    // ExcludeClipRect / SaveDC / RestoreDC / SelectObject(位图) 都要同步维护它。
    // 为什么必须有：SHostWnd::OnPrint 末尾会用 GetClipBox(dc) 把窗口 DC 的裁剪框并进
    // 脏矩形（shostwnd.cpp:746 `rcInvalid = rcInvalid | rcUpdate`）。若这里无条件返回
    // 整块画布，SOUI 就认为"整屏都脏"，随后 UpdatePresenter 把整幅 memRT 贴到窗口
    // 画布 —— 而 memRT 在该帧只更新了局部，于是任何一次局部重绘都会把其余区域刷成
    // 纯背景：鼠标划过一片空白、多刷新几次文字全没。
    RECT rcClip;
    /* 当前裁剪区的**精确形状**（设备坐标，本 DC 持有，NULL=尚未设过裁剪）。
     * ExtSelectClipRgn 的 RGN_DIFF 等模式需要把「现有裁剪区」作为区域参与布尔
     * 运算（SGridCtrl 用 PushClipRect(rcFixed, RGN_DIFF) 挖掉左上角固定区再画
     * 行头/列头），只记包围盒没法做差。GetClipBox 仍以 rcClip 包围盒为准。
     * 存设备坐标的原因：SOUI 的 item 绘制（SOsrPanel::Draw）先取裁剪区再
     * OffsetViewportOrg，之后用 item 相对坐标与之比较 —— 存「设置当时的逻辑
     * 坐标」会随视图原点漂移；GetClipBox/GetClipRgn 导出时按当前变换逆映射回
     * 逻辑空间（等价真 GDI 的动态重表达，见 gdi_canvas2d.cpp 裁剪记账一节）。 */
    HRGN hClipRgn;

    /* 见 js_glue.h 的 C2DStateCache（性能关键的状态缓存）。 */
    C2DStateCache c2d;
    /* 当前 ctx 在 JS 派发器注册表里的下标（见 js_glue.h 的 SwxOp）。
     * 热路径绘制全用它换 ctx，不再把 val 句柄搬来搬去。 */
    int ctxId;
#elif defined(__APPLE__)
    // APPLE (macOS/iOS) 平台：使用 CoreGraphics 的 CGContext 实现 GDI 方法，替代 cairo。
    // 成员均为指针类型，配合前置声明，避免在头文件中包含 <CoreGraphics/CoreGraphics.h>
    CGContextRef cgCtx;        // 若指向窗口系统传入的 drawRect 上下文则为非拥有引用；
                                    // 若为内存 DC（SelectObject(hBmp)）创建的 CGBitmapContext 则为拥有引用，需 CGContextRelease。
    BOOL cgCtxOwned;                // TRUE 表示需要 CGContextRelease(cgCtx)
    CGAffineTransform *worldMtx;  // 使用指针避免需要完整类型定义
    CGPathRef recordedPath;   // BeginPath/EndPath/PathToRegion 等路径记录
    FLOAT miterLimit;               // SetMiterLimit/GetMiterLimit. CGContext doesn't persist across re-creation; keep in HDC.
#else
    cairo_t *cairo;
    cairo_matrix_t mtx;
#endif
    POINT ptOrigin;
    HBITMAP bmp;
    COLORREF crText;
    COLORREF crBk;
    HPEN pen;
    HBRUSH brush;
    HFONT hfont;
    UINT    uGetFlags;
    int bkMode;
    int nSave;
    UINT textAlign;
    int rop2;
    int polyFillMode;
    int graphicsMode;    // SetGraphicsMode/GetGraphicsMode: GM_COMPATIBLE / GM_ADVANCED
    POINT brushOrg;      // SetBrushOrgEx/GetBrushOrgEx: pattern brush origin (device coords)
#ifdef __OHOS__
    BOOL hasDirty;
    RECT dirtyRect;
#endif

    // Path support
    BOOL pathRecording;     // TRUE if path recording is active
#if defined(__EMSCRIPTEN__)
    // 路径操作记录在 recordedPath（见 canvas2d_internal.h），不持有 cairo 类型
#elif !defined(__APPLE__)
    cairo_path_t *currentPath;  // Current path being recorded (non-APPLE keeps cairo_path_t)
#endif

    // State stack for SaveDC/RestoreDC
    std::vector<std::shared_ptr<DCState>> stateStack;

    // EMF 录制句柄：非 NULL 表示本 DC 由 CreateEnhMetaFile 创建，
    // SelectObject(OBJ_FONT) 会把一条 EMR_EXTCREATEFONTINDIRECTW 追加进去
    // （见 src/metafile.cpp）。CloseEnhMetaFile 取走该句柄后置回 NULL；
    // 若 DC 未关闭就被析构，则由 ~_SDC 负责释放。
    HENHMETAFILE recording;

    HDC__(HWND _hwnd);
    ~HDC__();
    
    int SaveState();
    BOOL RestoreState(int nState);
};

/* 既有实现代码里的名称，保持不变。 */
typedef HDC__ _SDC;

#endif //_SDC_H_
