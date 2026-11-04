/*
 * canvas2d_internal.h -- 内部类型与 JS 胶水声明（route C / wasm GDI 后端）。
 *
 * 该头仅在 __EMSCRIPTEN__ 下被 swinx/src/sdc.h 包含，所有绘制经浏览器 Canvas 2D
 * 完成，从而把 cairo / pixman / freetype / fontconfig / brotli 从 wasm 里彻底移除。
 *
 * 设计要点：
 *   - 一个 HDC 对应一块「绘制目标」：窗口 DC 指向其后备离屏 canvas；内存 DC 选中位
 *     图后指向该位图的 canvas。绘制直接落到 canvas 的 2D context。
 *   - HBITMAP 包装一个 Canvas2DSurface（位图后备 canvas）。窗口后备画布也以 HBITMAP
 *     形式存在（见 SConnection::CreateWindowSurface），present 时由 SConnection 读回
 *     其像素填入 framebuffer（沿用既有 JS 上屏链路，逐字节兼容）。
 *   - 文本走浏览器 fillText，字体为 CSS 字体族（系统 CJK 字体），不再内置字体 blob。
 */
#ifndef SWINX_GDI_CANVAS2D_INTERNAL_H
#define SWINX_GDI_CANVAS2D_INTERNAL_H

#include <ctypes.h>
#include <emscripten/val.h>
#include <vector>
#include <string>
// GRADIENTITEM / GRADIENTTYPE / TILEMODE / LOGFONTW 来自 gdi.h；ToGdiHandle /
// InitHandle / FreeHandlePtr 来自共享的 handle.h。gdi.h 不 include sdc.h，故这里
// include 不会成环（sdc.h 已用守卫包住本文件）。
#include <gdi.h>
#include <handle.h>

/* canvas2d 后端私有：渐变画刷的 style 标记（标准 BS_* 没有渐变项；cairo/apple
 * 后端用各自 GdiBrush 的 gradientDetail 表达，与此常量无关）。值取 BS_BRUSH(10)
 * 之后的下一个空闲号。 */
#define BS_GRADIENT 11

#ifdef __cplusplus
extern "C" {
#endif

/* 路径记录操作（BeginPath/EndPath/StrokePath/FillPath/PathToRegion 用）。 */
struct PathOp
{
    int type;            // PT_MOVETO / PT_LINETO / PT_BEZIERTO / PT_CLOSEFIGURE / PT_ARC_OP
    double x, y;
    double cx1, cy1;     // bezier 控制点 1；PT_ARC_OP 时是半径 rx/ry
    double cx2, cy2;     // bezier 控制点 2；PT_ARC_OP 时是起止角（弧度）
};

/* canvas2d 私有：椭圆弧。GDI 的 Arc/Pie/Chord/Ellipse 在路径录制期间也要进路径，
 * 但 PathOp 只有 6 个 double 槽位，这里借用 —— 圆心 (x,y)、半径 (cx1,cy1)、
 * 起止角 (cx2,cy2)。角度口径与 atan2(dy,dx) 一致（屏幕坐标，y 向下为正），
 * 方向按 GDI 默认的逆时针回放（canvas ellipse 的 counterclockwise=true）。
 * 取值 200 + 一位方向标志，避开 gdi.h 里 PT_* 的 1/2/4/6。 */
#define PT_ARC_OP 200

/* 一块绘制目标（窗口后备画布 / 位图后备画布）。 */
struct Canvas2DSurface
{
    emscripten::val canvas; // HTMLCanvasElement
    emscripten::val ctx;    // CanvasRenderingContext2D
    int width;              // 逻辑像素（== canvas.width）
    int height;             // 逻辑像素（== canvas.height）
    int bitsPixel;          // DIB 位图：每像素位数（通常 32）
    int stride;             // DIB 位图：每行字节数
    uint8_t *bits;          // DIB 位图拥有的 ARGB32 缓冲（GetDIBits/UpdateDIBPixmap 用），可空
    BOOL alpha;             // context 是否带 alpha（窗口画布=false，位图=true）
    /* 本 surface 是否**拥有** ctxId（销毁时要归还给注册表）。
     * 0 尺寸的"占位位图"（CreateCompatibleBitmap(hdc,0,0)）共用全局的 scratch
     * 画布，不拥有下标，置 FALSE。 */
    BOOL ownsCtx;
    /* JS 侧 ctx 注册表下标。热路径上的绘制调用不走 embind（跨边界 3~10µs/次），
     * 而是经 SwxOp/SwxOpStr/SwxBlit 直接调进 JS 的派发器，用这个整数换 ctx
     * （JS 侧 Module.swxCtx[id]），既省掉 val 装箱，也省掉 method name 的字符串
     * 转换。见 js_glue.cpp 顶部"为什么热路径用 EM_JS"。 */
    int ctxId;
};

/* GDI 对象：画笔 / 画刷 / 字体（后端私有结构，经 InitGdiObj 包成 HGDIOBJ）。 */
struct GdiPen
{
    int style;   // PS_SOLID / PS_NULL / PS_DASH / PS_DOT / PS_DASHDOT ...
    int width;
    uint32_t color;
    std::vector<float> dash;
    // 渐变/图案画笔：swinx 扩展的 ExtCreatePen 约定 —— LOGBRUSH.lbStyle == BS_BRUSH
    // 时 lbHatch 存的是画刷句柄（render-gdi 的 DrawGradientPath 用它把扫掠渐变
    // 当作"描边颜色"）。为空表示纯色画笔。
    HBRUSH pattern;
};

struct GdiBrush
{
    int style;             // BS_SOLID / BS_NULL / BS_HATCHED / BS_PATTERN / BS_DIBPATTERN / BS_GRADIENT
    uint32_t color;
    Canvas2DSurface *pattern; // BS_PATTERN / BS_DIBPATTERN：图案位图
    TILEMODE tileMode;        // kTileMode_Clamp / Repeat / Mirror
    // 渐变（CreateGradientBrush / GradientFill）
    GRADIENTTYPE gradType;    // grad_linear / grad_radial / grad_sweep
    std::vector<GRADIENTITEM> gradItems;
    float gradAngle;          // linear: 角度（度）
    float gradRadius;         // radial: 半径（像素，与 cairo 后端同一口径）
    float gradCX, gradCY;     // radial/sweep: 中心（0..1 比例，乘以路径包围盒）
    float gradStart;          // sweep: 起始角（度）
    BYTE gradAlpha;
};

struct GdiFont
{
    LOGFONTW lf;
    std::string cssFont; // 构建好的 ctx.font 字符串
    double sizePx;       // 字号（px，逻辑像素）
    /* 浏览器实测的字体度量（CSS px）。GDI 的 tmAscent/tmDescent/tmHeight 就是这一对：
     *   lineH = ascent + descent（== tmHeight）
     * 以前这里用 sizePx*1.2 估算，会让每一行（以及由行高决定的 wrapContent 高度）
     * 矮 2~3px，页签条、顶栏整体上移；改成浏览器真实度量后与 cairo 版
     * （FreeType ascender/descender）同一口径。ascent<=0 表示浏览器不支持
     * fontBoundingBox 度量，调用方回退到估算值。 */
    double ascent;
    double descent;
    double lineH;        // ascent + descent
    bool bold;
    bool italic;
    bool underline;
    bool strike;
};

/* ---- JS 侧绘制状态缓存（canvas2d 后端，性能关键）----
 * 每次绘制都要跨 wasm→JS 设 fillStyle/font/textAlign/变换/裁剪：实测动画期
 * 一帧有上千次属性设置，embind 每次调用 2~5µs，累计就是整个帧预算。浏览器侧
 * 这些状态"设置即生效、不做脏检查"，所以这里记住"JS 侧现在是什么值"，相同
 * 就跳过那次跨语言调用。
 *
 * 正确性唯一来源：凡是 ctx.save()/ctx.restore() 改变过 JS 侧状态的地方，必须
 * 把整份缓存置回未知（C2dInvalidate），让下一次设置必然重新下发。已知的三处：
 *   - RestoreState（ctx.restore() 会还原变换/裁剪/样式）
 *   - AlphaBlend / 需要临时状态的分支里的 save()+restore() 对
 * 缓存只用于"能精确比较的简单值"：渐变/图案这类 val 对象一律按未知处理。 */
struct C2DStateCache
{
    int64_t fillStyle;   /* 0=未知；否则 (COLORREF)+1（COLORREF 本身可能是 0xFFFFFFFF） */
    int64_t strokeStyle; /* 同上 */
    int64_t fontTag;     /* 0=未知；否则 (GdiFont*)+1 */
    int textAlign;       /* 已应用的 TA_LEFT/TA_CENTER/TA_RIGHT，-1=未知 */
    int baseline;        /* 已应用的 textBaseline，见 TA_BASELINE 口径，-1=未知 */
    int smoothing;       /* imageSmoothingEnabled，-1=未知 */
    int rop;             /* globalCompositeOperation 对应的 DWORD rop，-1=未知 */
    double alpha;        /* globalAlpha，<0=未知 */
    BOOL xfValid;        /* xf 是否可信 */
    double xf[6];        /* 已应用的变换 a,b,c,d,e,f */
    BOOL clipValid;      /* clipRect 是否可信 */
    RECT clipRect;       /* 已应用的裁剪矩形（逻辑坐标） */

    C2DStateCache()
        : fillStyle(0), strokeStyle(0), fontTag(0), textAlign(-1), baseline(-1),
          smoothing(-1), rop(-1), alpha(-1.0), xfValid(FALSE), clipValid(FALSE)
    {
        xf[0] = 1; xf[1] = 0; xf[2] = 0; xf[3] = 1; xf[4] = 0; xf[5] = 0;
        clipRect.left = clipRect.top = clipRect.right = clipRect.bottom = 0;
    }
};

/* SaveDC/RestoreDC 保存的 DC 状态子集（其余由 ctx.save/restore 处理）。 */
struct DCState
{
    COLORREF crText;
    COLORREF crBk;
    HPEN pen;
    HBRUSH brush;
    HFONT hfont;
    int bkMode;
    UINT textAlign;
    int rop2;
    int polyFillMode;
    int graphicsMode;
    POINT brushOrg;
    POINT ptOrigin;      // 当前画笔位置（MoveToEx/LineTo），不是视图原点
    POINT ptViewport;    // 视图原点（SetViewportOrgEx/OffsetViewportOrgEx）
    POINT ptWinOrg;      // 窗口原点（SetWindowOrgEx）
    /* 世界变换（SetWorldTransform / GM_ADVANCED）。设备坐标 = World(逻辑) + 视图原点。
     * render-gdi 的 DrawSVG（linux 分支）靠它把 SVG 的原始坐标缩放到目标矩形
     * （见 components/render-gdi/linux/render-gdi.cpp 的 DrawSVG），是空实现时
     * 所有 src="svg:..." 的皮肤都画在原始坐标上：消消乐棋子整块消失、LED 数字只剩
     * 一块底。 */
    XFORM xfWorld;
    /* 裁剪区（设备坐标包围盒），与 HDC__::rcClip 同一份账。
     * canvas 的 clip 由 ctx.save/restore 管，HDC 侧只需记住包围盒好回答 GetClipBox。 */
    RECT rcClip;
    /* 裁剪区的精确形状（设备坐标，与 HDC__::hClipRgn 同一坐标基准）。SaveState
     * 存的是**本状态私有副本**（区域在 save 之后可能继续被改），RestoreState 时
     * 所有权移交给 HDC（见 RestoreState 的移交注释）。NULL 表示 save 时还未设过裁剪。 */
    HRGN hClipRgn;
    /* 同一时刻的 JS 侧状态缓存（save 存、restore 取）。ctx.restore() 还原的正是
     * save 时的整套 canvas 状态，所以缓存可以跟着还原，而不是整体作废 ——
     * 作废会让 restore 之后每一次 fillStyle/font/smoothing 设置都重设一遍，
     * 实测整窗重绘一帧因此多出 400 次跨语言调用。 */
    C2DStateCache c2d;
};

/* ---- JS 胶水（在 js_glue.cpp 用 EM_JS 实现）---- */

/* ---------- 热路径派发器（EM_JS 直接导入，不经 embind）----------
 * 实测（tools/diag/measure_span.cjs）整窗重绘一帧约 2300 次 Canvas 2D 调用，
 * 每次经 embind 的 val::call/val::set 边界成本 3~10µs，合计 9ms 以上 —— 这一项
 * 就吃掉了 60fps 的全部预算（cairo 后端同样的绘制量是纯 C 调用，几乎不花时间）。
 * 所以把"每次绘制都要发生"的那几类操作改成普通 wasm import：
 *   - SwxOp     ：几何/状态类，1 个整数 op + 6 个 double，JS 侧一个 switch 分派；
 *   - SwxOpStr  ：需要字符串的（fillText/font），传 wasm 内存里的 UTF-8 指针；
 *   - SwxBlit   ：drawImage（10 个参数 + 目标/源 canvas 下标）；
 *   - SwxXform  ：setTransform（6 个 double）。
 * 这些函数**立即执行**，不做缓冲，所以绘制顺序与改造前完全一致。
 * 字符串/对象类操作（渐变、图案、measureText）仍走 embind，它们是冷路径。 */
enum SwxOpCode
{
    SWX_SAVE = 1,
    SWX_RESTORE,
    SWX_CLIP_RECT,   /* (l,t,r,b)     beginPath+rect+clip，一次调用干完 */
    SWX_RECT_PATH,   /* (x,y,w,h)     beginPath+rect */
    SWX_BEGIN_PATH,
    SWX_MOVE_TO,     /* (x,y) */
    SWX_LINE_TO,     /* (x,y) */
    SWX_BEZIER,      /* (c1x,c1y,c2x,c2y,x,y) */
    SWX_CLOSE_PATH,
    SWX_FILL,
    SWX_STROKE,
    SWX_FILL_RECT,   /* (x,y,w,h) */
    SWX_CLEAR_RECT,  /* (x,y,w,h) */
    SWX_STROKE_RECT, /* (x,y,w,h) */
    SWX_FILL_SOLID,  /* (COLORREF)    fillStyle */
    SWX_STROKE_SOLID,/* (COLORREF)    strokeStyle */
    SWX_TEXT_ALIGN,  /* (TA_*) */
    SWX_BASELINE,    /* ()           alphabetic */
    SWX_XFORM,       /* (a,b,c,d,e,f) */
    SWX_ALPHA,       /* (0..1) */
    SWX_SMOOTH,      /* (0/1) */
    SWX_ROP,         /* (rop) */
    SWX_LINE_WIDTH,  /* (w) */
    SWX_DASH_NONE,
    SWX_ROUND_RECT,  /* (x,y,w,h,r) */
    SWX_FILL_TEXT,          /* 经 SwxOpStr：(utf8,len) + (x,y) */
    SWX_STROKE_TEXT,
    SWX_FONT,               /* 经 SwxOpStr：(utf8,len) */
};

/* ctx 注册表：创建 canvas 时登记一次，返回的整数就是 Canvas2DSurface::ctxId。
 * 下标**必须**在 surface 销毁时经 SwinxCtxUnregister 归还：注册表是 JS 侧的数组，
 * 一直持有 ctx（ctx 又持有 canvas），不归还就等于把整块画布连同显存/光栅缓冲
 * 永久钉在内存里。游戏里位图是反复创建销毁的（特效/精灵），漏掉这一步的表现
 * 就是「玩得越久越卡」，而不是立刻出错。 */
int SwinxCtxRegister(emscripten::val ctx);
void SwinxCtxUnregister(int ctxId);

void SwxOp(int ctxId, int op, double a, double b, double c, double d, double e, double f);
void SwxOpStr(int ctxId, int op, const char *utf8, int len, double a, double b, double c, double d);
void SwxBlit(int ctxId, int srcId, double dx, double dy, double dw, double dh,
             double sx, double sy, double sw, double sh, int smooth);
void SwxXform(int ctxId, double a, double b, double c, double d, double e, double f);

/* 直接把离屏窗口画布合成到显示画布（drawImage），不经过帧缓冲。
 * 这是 canvas2d 后端的"上屏"：改造前是 getImageData 读回 fbmemcpy 进 fb 再
 * putImageData 上传，一帧两次全幅像素搬运（实测占空闲帧 time 的 16%）。
 * 参数是 canvas 坐标（1:1，DPI 已在布局层放大）：源 (sx,sy,w,h) → 目标 (dx,dy)。 */
void SwxPresentSurface(int srcCtxId, double sx, double sy, double w, double h,
                       double dx, double dy);

emscripten::val SwinxCanvasCreate(int w, int h, int alpha);
void SwinxCanvasResize(emscripten::val canvas, int w, int h);
void SwinxCanvasReadPixels(emscripten::val canvas, int sx, int sy, int sw, int sh,
                           uint32_t *dst, int dstStride);
/* 同上，但**不做 RGBA→ARGB 重排**：getImageData 的 RGBA 字节原样落进 dst
 * （dst 按字节寻址，dstStride 以字节为单位）。上屏路径用它 —— 帧缓冲从此按
 * 浏览器原生的 RGBA 字节序存储，省掉 C++ 侧每帧一次全屏逐像素换序，JS 侧也能
 * 直接用同一块内存构造 ImageData 零拷贝 putImageData。 */
void SwinxCanvasReadPixelsRaw(emscripten::val canvas, int sx, int sy, int sw, int sh,
                              uint8_t *dst, int dstStrideBytes);
/* 把 WASM 堆里的 ARGB32 位图（srcPtr，w×h 像素）putImageData 到 canvas（DIB 回写）。 */
void SwinxCanvasPutPixels(emscripten::val canvas, int srcPtr, int w, int h);
/* 带目标偏移的上传（逐行写任意像素格式时用；GDI 的 UpdateDIBPixmap 非 32 位分支） */
void SwinxCanvasPutPixelsAt(emscripten::val canvas, int srcPtr, int w, int h, int x, int y);
double SwinxCanvasDpr(void);

/* 把一段 UTF-8 字节转成真正的 JS 字符串（用 TextDecoder 显式解码）。
 *
 * 为什么不直接把 std::string 当参数传进 val::call：embind 给 std::string 注册的
 * wire 类型在 C++→JS 方向按 Latin-1 逐字节落成字符，中文会变成
 *   "å_å°±ä¸_" 这种乱码（每个 UTF-8 字节一个字符），fillText 于是画出乱码。
 * TextDecoder('utf-8').decode(Uint8Array) 按 UTF-8 正确解码，浏览器与 node 都有。 */
emscripten::val SwinxJsText(const char *utf8, int len);

/* 浏览器实测字体度量：返回 [ascent, descent]（CSS px，均可能为 0 表示不可用）。
 * 参数是 CSS font 字符串（UTF-8，可能含中文族名）。GDI 的 tmAscent/tmDescent 与
 * DrawText 的 DT_VCENTER 都基于这一对，用它取代 sizePx*1.2 的估算。 */
emscripten::val SwinxFontMetrics(const char *cssFontUtf8, int len);

/* 创建一个 canvas 后备的 HBITMAP（窗口后备画布 / 内存 DC 位图 共用）。 */
HBITMAP SwinxCanvasCreateBitmap(int w, int h, BOOL alpha);

/* 仅创建裸 Canvas2DSurface（不包 HBITMAP）：供 SConnection::CreateWindowSurface
 * 返回，再由 base 的 InitWndDC 用 InitGdiObj(OBJ_BITMAP, surface) 包一次，
 * 与 cairo 后端「CreateWindowSurface 返回裸 surface 指针」保持一致，避免双重包裹。 */
Canvas2DSurface *SwinxCanvasCreateSurfaceRaw(int w, int h, BOOL alpha);

#ifdef __cplusplus
}
#endif

#endif // SWINX_GDI_CANVAS2D_INTERNAL_H
