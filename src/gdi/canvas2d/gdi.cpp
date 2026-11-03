/*
 * canvas2d/gdi.cpp -- route C：基于浏览器 Canvas 2D 的 swinx GDI 后端（仅 wasm）。
 *
 * 所有绘制直接落到 Canvas 2D context（窗口后备画布 / 位图后备画布都是离屏
 * HTMLCanvasElement），从而把 cairo / pixman / freetype / fontconfig / brotli
 * 从 wasm 构建里彻底移除；文本走浏览器 fillText（系统 CJK 字体），不再内置字体 blob。
 *
 * 维护者注（与 cairo 后端对照）：
 *   - 句柄管理（InitHandle/ToGdiHandle）复用共享的 swinx/src/handle.cpp，这里只提供
 *     InitGdiObj 与各类型的释放回调。
 *   - HDC = _SDC*（sdc.h 在 __EMSCRIPTEN__ 下挂 canvas/ctx 成员），DC 状态字段（crText
 *     等）沿用 sdc.h 已有成员；SaveDC/RestoreDC 经 ctx.save/restore + DCState 栈。
 *   - 坐标保持逻辑像素 1:1（Retina 由 framebuffer 物理尺寸在上屏层统一处理）。
 *   - 复杂区域（HRGN 非矩形）、锥形渐变、ROP 组合运算、位图颜色键等为近似实现，已在
 *     各函数处注释；矩形剪裁、线性/径向渐变、SRCCOPY/AlphaBlend、fillText 为精确路径。
 */
#include <ctypes.h>
#include <gdi.h>
#include <sdc.h>
#include <region.h>
#include <emscripten/val.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include "js_glue.h"

#ifndef GetRValue
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)(((WORD)(c)) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#endif

using emscripten::val;

/* ---------- 小工具 ---------- */
/* 渐变停靠点用的颜色串：必须**始终**保留 alpha（含 a==0）。
 * COLORREF 的 alpha 在高字节（SOUI 的 RGBA 宏就是 (b<<24|b<<16|g<<8|r)）。
 * 渐变停靠点的 alpha 都是 SVG/nanosvg 显式给定的：a==0 是**合法的透明段**
 * （例如 main_window_bg.svg 的 innerGlow：#FFD700 0.10 → #FFD700 0.0，本意是
 * 顶部一道渐隐的微光）。它必须保持透明，否则画布会把"淡金"一路插值成"实心金"，
 * 九宫格一拉伸就成了整条亮金带。所以这里不像 GDI 实色那样把 a==0 当成"不透明"
 * （GDI 的 RGB()/GetSysColor 高字节永远是 0，那是另一回事），而是永远出 rgba()。 */
static std::string cssColorStop(COLORREF c)
{
    char buf[40];
    unsigned a = (unsigned)GetAValue(c);
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.4f)",
             (int)GetRValue(c), (int)GetGValue(c), (int)GetBValue(c), a / 255.0);
    return std::string(buf);
}

/* ---------- JS 侧绘制状态缓存 ----------
 * 实测：动画期一帧有 2400+ 次 Canvas 2D 调用，其中 65% 是"把同样的 fillStyle /
 * font / textAlign / 变换 / 裁剪再设一遍"。浏览器侧每次设置都要跨 wasm→JS 边界
 * （embind，2~5µs），一帧就是几毫秒。这里按 sdc.h 的 HDC__::C2DStateCache 做脏
 * 检查：值没变就跳过那次调用。
 *
 * 缓存"代表值"的约定：
 *   - COLORREF 用 (int64)cr + 1 存，0 表示未知（COLORREF 自身可以是 0xFFFFFFFF，
 *     不能拿它当哨兵）。
 *   - 字体用 (GdiFont*) + 1 存在 int64，0 表示未知。
 *   - 渐变/图案这类 val 对象每个都是新对象、无法比较，直接写并把缓存置未知。 */
static inline void C2dInvalidate(HDC__ *hdc)
{
    hdc->c2d = C2DStateCache();
}

/* 热路径绘制调用：走 js_glue 里的 EM_JS 派发器（见 js_glue.h 的说明），
 * 不再经过 embind 的 val::call/val::set。参数一律 double，全部操作立即执行。 */
static inline void Op(HDC__ *hdc, int op, double a = 0, double b = 0, double c = 0,
                      double d = 0, double e = 0, double f = 0)
{
    SwxOp(hdc->ctxId, op, a, b, c, d, e, f);
}
/* 文本类 op（需要 UTF-8 串） */
static inline void OpText(HDC__ *hdc, int op, const std::string &s, double a = 0, double b = 0)
{
    SwxOpStr(hdc->ctxId, op, s.data(), (int)s.size(), a, b, 0, 0);
}
/* GDI 的 TA_* → canvas textAlign：0=left 1=center 2=right。
 * 与改造前的判断顺序保持一致（先看 TA_CENTER 再看 TA_RIGHT）。 */
static inline int c2dTextAlignIndex(UINT ta)
{
    if (ta & TA_CENTER) return 1;
    if (ta & TA_RIGHT) return 2;
    return 0;
}

static inline void C2dFill(HDC hdc, COLORREF cr)
{
    int64_t tag = (int64_t)(uint32_t)cr + 1;
    if (hdc->c2d.fillStyle == tag)
        return;
    Op(hdc, SWX_FILL_SOLID, (double)(uint32_t)cr);
    hdc->c2d.fillStyle = tag;
}

/* 渐变/图案画刷：每次都新建 val，无从比较，写完把缓存标记成"未知" */
static inline void C2dFillValue(HDC hdc, const emscripten::val &v)
{
    hdc->ctx.set(std::string("fillStyle"), v);
    hdc->c2d.fillStyle = 0;
}

static inline void C2dStroke(HDC hdc, COLORREF cr)
{
    int64_t tag = (int64_t)(uint32_t)cr + 1;
    if (hdc->c2d.strokeStyle == tag)
        return;
    Op(hdc, SWX_STROKE_SOLID, (double)(uint32_t)cr);
    hdc->c2d.strokeStyle = tag;
}

static inline void C2dStrokeValue(HDC hdc, const emscripten::val &v)
{
    hdc->ctx.set(std::string("strokeStyle"), v);
    hdc->c2d.strokeStyle = 0;
}

static inline void C2dTextAlign(HDC hdc, UINT ta)
{
    if (hdc->c2d.textAlign == (int)ta)
        return;
    Op(hdc, SWX_TEXT_ALIGN, (double)c2dTextAlignIndex(ta));
    hdc->c2d.textAlign = (int)ta;
}

static inline void C2dBaseline(HDC hdc)
{
    if (hdc->c2d.baseline == 1)
        return;
    Op(hdc, SWX_BASELINE);
    hdc->c2d.baseline = 1;
}

static inline void C2dSmoothing(HDC hdc, BOOL on)
{
    int v = on ? 1 : 0;
    if (hdc->c2d.smoothing == v)
        return;
    Op(hdc, SWX_SMOOTH, v);
    hdc->c2d.smoothing = v;
}

static inline void C2dAlpha(HDC hdc, double a)
{
    if (hdc->c2d.alpha >= 0.0 && hdc->c2d.alpha == a)
        return;
    Op(hdc, SWX_ALPHA, a);
    hdc->c2d.alpha = a;
}

/* rop → globalCompositeOperation。返回 FALSE 表示 rop 未知（缓存里记 -1，
 * 下一次照样重新下发）。 */
static inline void C2dRop(HDC hdc, DWORD rop)
{
    if (hdc->c2d.rop == (int)rop)
        return;
    int idx = 0; /* source-over */
    if (rop == SRCAND)
        idx = 1; /* source-in，近似 */
    else if (rop == SRCINVERT)
        idx = 2; /* xor，近似（canvas 无 XOR） */
    Op(hdc, SWX_ROP, idx);
    hdc->c2d.rop = (int)rop;
}

static std::string toUtf8(const wchar_t *w)
{
    std::string out;
    if (!w)
        return out;
    for (; *w; ++w)
    {
        unsigned cp = (unsigned)*w;
        if (cp < 0x80)
            out.push_back((char)cp);
        else if (cp < 0x800)
        {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

/* A 版 GDI 的入参在本工程里是 UTF-8 字节：wasm 构建是 MBCS（SOUI_USE_UNICODE=OFF，
 * TCHAR=char），XML 里取出的文本以 UTF-8 传给 DrawTextA/TextOutA；cairo 后端也是
 * 直接把 LPCSTR 交给 cairo_show_text（cairo 只认 UTF-8）。所以这里必须按 UTF-8 解码，
 * 按字节 1:1 宽化的话中文会变成 "æ ¸å¿" 这种乱码（实测症状）。 */
static std::wstring utf8ToW(const char *p, int n)
{
    std::wstring out;
    if (!p)
        return out;
    if (n < 0)
        n = (int)strlen(p);
    if (n <= 0)
        return out;
    int need = MultiByteToWideChar(CP_UTF8, 0, p, n, NULL, 0);
    if (need <= 0)
        return out;
    out.resize((size_t)need);
    MultiByteToWideChar(CP_UTF8, 0, p, n, &out[0], need);
    return out;
}


static std::string buildCssFont(const GdiFont *f)
{
    std::string s;
    if (f->italic)
        s += "italic ";
    s += f->bold ? "700 " : "400 ";
    double sz = f->sizePx > 0 ? f->sizePx : 12;
    s += std::to_string((int)sz) + "px ";
    std::string face = toUtf8(f->lf.lfFaceName);
    if (face.empty())
        s += "\"PingFang SC\",\"Microsoft YaHei\",\"Noto Sans CJK SC\",sans-serif";
    else
        s += "\"" + face + "\",\"PingFang SC\",\"Microsoft YaHei\",\"Noto Sans CJK SC\",sans-serif";
    return s;
}

static double cssLineWidth(HDC hdc)
{
    GdiPen *p = (GdiPen *)GetGdiObjPtr(hdc->pen);
    if (!p || p->width <= 0)
        return 1.0;
    return (double)p->width;
}

/* 字体度量取值（带兜底）。GdiFont::ascent/descent 是浏览器实测值；
 * hfont 无效或度量不可用时按字号估一个，避免除零/负值。 */
static inline double fontAscent(const GdiFont *f)
{
    if (f && f->lineH > 0)
        return f->ascent;
    return (f ? f->sizePx : 12) * 0.95;
}
static inline double fontDescent(const GdiFont *f)
{
    if (f && f->lineH > 0)
        return f->descent;
    return (f ? f->sizePx : 12) * 0.25;
}
static inline double fontLineH(const GdiFont *f) { return fontAscent(f) + fontDescent(f); }

/* 把 HDC 的变换（世界变换 + 视图/窗口原点）落到 canvas 上。定义在文件后部（视图原点
 * 那一节），但构造/存档/裁剪都要用，故前置声明。 */
static void applyCtxTransform(HDC__ *hdc);
/* 以设备坐标建立 canvas 裁剪（先临时把变换清成单位矩阵），定义见世界变换一节。 */
static void clipRectDevice(HDC__ *hdc, int l, int t, int r, int b);
/* 裁剪记账（GetClipBox 用），定义同在那一节。 */
static void logicRectToDevice(HDC__ *hdc, int l, int t, int r, int b, RECT &out);
static void clipBoxIntersect(HDC__ *hdc, const RECT &rcDev);

/* ---------- DPI（route C，v2）----------
 * GDI 层**不做任何缩放**：canvas 的像素编址 == 传入坐标（1:1）。
 * HiDPI 由 SOUI 自己的方案完成——宿主窗口建在物理分辨率上，SHostWnd::SetScale
 * 广播 UM_SETSCALE 让布局/字体/皮肤按档位放大，绘制坐标天然就是物理像素。
 * 帧缓冲物理尺寸仍由 SwinxSetCanvasScale（framebuffer.cpp，main() 开头调一次）
 * 决定，这里不再读它。 */


/* 前向声明：渐变画笔要在 applyPen 里生成 fillStyle/strokeStyle，而 buildGradient
 * 定义在 applyBrush 旁边（在 applyPen 之后）。 */
static val buildGradient(val &c, GdiBrush *b, double x, double y, double w, double h);
static void syncSurfaceBits(Canvas2DSurface *s);

static bool applyPen(HDC hdc, val &c, double x = 0, double y = 0, double w = 0, double h = 0)
{
    GdiPen *p = (GdiPen *)GetGdiObjPtr(hdc->pen);
    if (!p || p->style == PS_NULL)
        return false;
    c.set(std::string("lineWidth"), cssLineWidth(hdc));
    /* 一律用 call<...>() 而不是 c["setLineDash"](...)：val::operator() 走的是
     * 「脱离接收者」的调用模式（生成的 JS 是 toValue(handle)(args)），DOM 方法
     * 丢了 this 会抛 TypeError: Illegal invocation；call() 生成的是
     * toValue(handle)[name](args)，this 正确。 */
    if (!p->dash.empty())
        c.call<void>("setLineDash", val::array(p->dash));
    else
        c.call<void>("setLineDash", val::array());
    c.set(std::string("lineJoin"),std::string( "miter"));
    c.set(std::string("lineCap"),std::string( "butt"));
    if (p->pattern)
    {
        /* 渐变/图案画笔。第一次实现只认 p->color，而 ExtCreatePen 走这条路时
         * lbColor 是 0 —— 整条彩色圆弧（SSpeedMeter）被描成纯黑。 */
        GdiBrush *b = (GdiBrush *)GetGdiObjPtr(p->pattern);
        if (b && b->style == BS_GRADIENT)
        {
            val g = buildGradient(c, b, x, y, w, h);
            if (!g.isUndefined())
            {
                C2dStrokeValue(hdc, g);
                return true;
            }
        }
        else if (b && (b->style == BS_SOLID || b->style == BS_HATCHED))
        {
            C2dStroke(hdc, b->color);
            return true;
        }
    }
    C2dStroke(hdc, p->color);
    return true;
}

/* 线性渐变的两个端点：沿角度方向与包围盒边界的交点（不是简单的
 * 中心 ± cos/sin * 宽高）。与 cairo 后端的 calc_linear_endpoint 逐行对齐，
 * 否则非正方形的控件（按钮药丸底、图表柱）渐变跨度会明显不同。 */
static void linearEndpoints(double angle, double wid, double hei, double &x0, double &y0, double &x1, double &y1)
{
    const double halfWid = wid / 2, halfHei = hei / 2;
    double a = fmod(angle, 360.0);
    if (a < 0)
        a += 360.0;
    const double eps = 1e-6;
    if (fabs(a - 90.0) < eps || fabs(a - 270.0) < eps)
    {
        x0 = halfWid; y0 = 0; x1 = halfWid; y1 = hei;
        return;
    }
    if (fabs(a) < eps || fabs(a - 180.0) < eps)
    {
        x0 = 0; y0 = halfHei; x1 = wid; y1 = halfHei;
        return;
    }
    int quadrant = (int)(a / 90.0);
    double rad = (a - quadrant * 90.0) * M_PI / 180.0;
    double tanAngle = tan(rad);
    double cotAngle = 1.0 / tanAngle;
    double p1x, p1y, p2x, p2y;
    double yRight = halfWid * tanAngle;
    if (fabs(yRight) <= halfHei + 1e-9)
    {
        p1x = -halfWid; p1y = -halfWid * tanAngle;
        p2x = halfWid;  p2y = yRight;
    }
    else
    {
        p1x = -halfHei * cotAngle; p1y = -halfHei;
        p2x = halfHei * cotAngle;  p2y = halfHei;
    }
    switch (quadrant)
    {
    case 1: p1x = -p1x; p2x = -p2x; break;
    case 2: p1x = -p1x; p1y = -p1y; p2x = -p2x; p2y = -p2y; break;
    case 3: p1y = -p1y; p2y = -p2y; break;
    default: break;
    }
    x0 = p1x + halfWid; y0 = p1y + halfHei;
    x1 = p2x + halfWid; y1 = p2y + halfHei;
}

static val buildGradient(val &c, GdiBrush *b, double x, double y, double w, double h)
{
    if (b->gradItems.empty())
        return val::undefined();
    int n = (int)b->gradItems.size();
    if (b->gradType == grad_linear)
    {
        // 角度 0 => 左->右；顺时针递增（与 GDI+ / SOUI 一致）
        double rx0, ry0, rx1, ry1;
        linearEndpoints(b->gradAngle, w, h, rx0, ry0, rx1, ry1);
        val g = c.call<val>("createLinearGradient", x + rx0, y + ry0, x + rx1, y + ry1);
        for (int i = 0; i < n; ++i)
            g.call<void>("addColorStop", (double)b->gradItems[i].pos, cssColorStop(b->gradItems[i].cr));
        return g;
    }
    else if (b->gradType == grad_radial)
    {
        /* radius 是**像素**，不是 0..1 的比例：SOUI 侧两种来源都已经换算成像素
         * （SSkin 用 m_radius.toPixelSize()，SVG 用 nsvgConvertToPixels），
         * cairo 后端也是直接把它当像素用（cairo_pattern_create_radial）。
         * 早先这里乘了一次 max(w,h)，半径被放大几十倍 —— 整个图形都落在渐变
         * 最内圈，消消乐棋子的苹果就成了一片浅粉（看起来像"多了一层透明"）。 */
        double r = b->gradRadius > 0 ? (double)b->gradRadius : (double)std::max(w, h) * 0.5;
        double cx = x + b->gradCX * w, cy = y + b->gradCY * h;
        val g = c.call<val>("createRadialGradient", cx, cy, 0.0, cx, cy, r);
        for (int i = 0; i < n; ++i)
            g.call<void>("addColorStop", (double)b->gradItems[i].pos, cssColorStop(b->gradItems[i].cr));
        return g;
    }
    else
    {
        // sweep/conic：用 createConicGradient（较新浏览器）；失败退化为 radial
        val g = c.call<val>("createConicGradient", b->gradStart * M_PI / 180.0, x + w / 2, y + h / 2);
        for (int i = 0; i < n; ++i)
            g.call<void>("addColorStop", (double)b->gradItems[i].pos, cssColorStop(b->gradItems[i].cr));
        return g;
    }
}

static bool applyBrush(HDC hdc, val &c, double x, double y, double w, double h)
{
    GdiBrush *b = (GdiBrush *)GetGdiObjPtr(hdc->brush);
    if (!b || b->style == BS_NULL || b->style == BS_HOLLOW)
        return false;
    if (b->style == BS_SOLID || b->style == BS_HATCHED)
    {
        C2dFill(hdc, b->color);
        return true;
    }
    if (b->style == BS_PATTERN || b->style == BS_DIBPATTERN)
    {
        if (b->pattern)
        {
            val pat = c.call<val>("createPattern", b->pattern->canvas,
                                  (b->tileMode == kTileMode_Repeat) ? std::string("repeat") : std::string("no-repeat"));
            C2dFillValue(hdc, pat);
            return true;
        }
        return false;
    }
    if (b->style == BS_GRADIENT)
    {
        val g = buildGradient(c, b, x, y, w, h);
        if (g.isUndefined())
            return false;
        C2dFillValue(hdc, g);
        return true;
    }
    return false;
}

static void fillStroke(HDC hdc, val &c, double x, double y, double w, double h)
{
    bool bf = applyBrush(hdc, c, x, y, w, h);
    bool pf = applyPen(hdc, c, x, y, w, h);
    if (bf)
        Op(hdc, SWX_FILL);
    if (pf)
        Op(hdc, SWX_STROKE);
}

/* 文本参数转成 JS 字符串。
 * 纯 ASCII 直接交给 embind（std::string 的 wire 类型对 ASCII 一定正确）；
 * 含非 ASCII 时走 TextDecoder 显式解码 —— 不依赖 embind 对 std::string 用的是
 * UTF-8 还是 Latin-1，避免换工具链后中文又变成 "æ  ¸å¿" 这类乱码。 */
static inline val jsText(const std::string &s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if ((unsigned char)s[i] >= 0x80)
            return SwinxJsText(s.data(), (int)s.size());
    return val(s);
}

/* ---------- measureText 记忆化 ----------
 * 实测（tools/diag/measure_span.cjs：整窗重绘一帧 2317 次 Canvas 调用）里
 * measureText 占 329 次，是全后端最热的单项。它不只是"一次函数调用"：
 *   1. 跨 wasm→JS 边界（embind，每次 3~10µs）；
 *   2. 浏览器要按 font 做一次文本整形；
 *   3. 返回值是被打包成 val 的 TextMetrics 对象，再 ["width"] 查一次属性才拿到数。
 * 而排一次版同一个串会被反复量（wrapContent 量一遍、DT_CALCRECT 再量、
 * 行内逐字前进量又量），所以按 (字体, 文本) 记忆化几乎全部命中。
 *
 * 键用 cssFont 而不是 HFONT：HFONT 指向的 GdiFont 释放后地址会被复用，
 * 而 cssFont（字号/粗细/斜体/字族）完整决定了 measureText 的结果。 */
static std::unordered_map<std::string, double> s_measureCache;
static double measureWidth(HDC__ *hdc, const std::string &text)
{
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    std::string key;
    if (f)
    {
        key.reserve(f->cssFont.size() + 1 + text.size());
        key += f->cssFont;
    }
    key.push_back('\x01');
    key += text;
    auto it = s_measureCache.find(key);
    if (it != s_measureCache.end())
        return it->second;
    double w = hdc->ctx.call<val>("measureText", jsText(text))["width"].as<double>();
    /* 粗粒度淘汰：文本组合数有限（界面上的字就那么些），超了直接清空重来，
     * 比维护 LRU 便宜得多，也不会让内存无限涨。 */
    if (s_measureCache.size() > 8192)
        s_measureCache.clear();
    s_measureCache.emplace(std::move(key), w);
    return w;
}

static void applyFont(HDC hdc, val &c)
{
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    /* 字体对象没换就不重设：cssFont 字符串每次都经 embind 装箱，是热路径上最贵的
     * 一次调用之一（DrawText 一帧几十上百次）。 */
    int64_t fontTag = f ? (int64_t)(intptr_t)f + 1 : 0;
    if (f && hdc->c2d.fontTag != fontTag)
    {
        /* 字体名可能含中文：派发器按 UTF-8 长度解码，不经过 embind 的字符串装箱 */
        OpText(hdc, SWX_FONT, f->cssFont);
        hdc->c2d.fontTag = fontTag;
    }
    C2dFill(hdc, hdc->crText);
    C2dStroke(hdc, hdc->crText);
    C2dTextAlign(hdc, hdc->textAlign);
    /* 基线口径：一律 alphabetic，由各绘制函数自己把「GDI 的 y」换算成基线
     * （TA_TOP 时 y 是字符格顶 ⇒ 基线 = y + ascent）。不用 canvas 的 "top"：
     * 那是 em 盒顶，与 GDI 的 ascent 顶不一致，会让文字整体偏低 1~3px。 */
    C2dBaseline(hdc);
}

/* ---------- 句柄基元（复用共享 handle.cpp 的 InitHandle/ToGdiHandle）---------- */
/* 底层对象的销毁统一在这里完成（由 _Handle 引用计数归零时回调），
 * DeleteObject 自己不再 delete 一遍 —— 否则同一指针被释放两次。 */
static void gdi_bmp_free(void *p)
{
    Canvas2DSurface *s = (Canvas2DSurface *)p;
    if (s)
    {
        if (s->bits)
            delete[] s->bits;
        delete s;
    }
}
static void gdi_pen_free(void *p) {
     delete (GdiPen *)p; 
}
static void gdi_brush_free(void *p)
{
    delete (GdiBrush *)p;
}
static void gdi_font_free(void *p)
{
    delete (GdiFont *)p;
}

extern "C" HGDIOBJ InitGdiObj(int type, void *ptr)
{
    FreeHandlePtr cb = nullptr;
    switch (type)
    {
    case OBJ_BITMAP: cb = gdi_bmp_free; break;
    case OBJ_PEN: cb = gdi_pen_free; break;
    case OBJ_BRUSH: cb = gdi_brush_free; break;
    case OBJ_FONT: cb = gdi_font_free; break;
    }
    return InitHandle(type, ptr, cb);
}
extern "C" int GetObjectType(HGDIOBJ h) { return ToGdiHandle(h)->type; }
extern "C" void *GetGdiObjPtr(HGDIOBJ h) { return ToGdiHandle(h)->ptr; }
extern "C" void SetGdiObjPtr(HGDIOBJ h, void *p) { ToGdiHandle(h)->ptr = p; }
extern "C" HGDIOBJ RefGdiObj(HGDIOBJ h) { return AddHandleRef(ToGdiHandle(h)); }

/* ---------- 对象构造辅助 ---------- */
static GdiPen *makePen(int style, int width, COLORREF color)
{
    GdiPen *p = new GdiPen();
    p->style = style;
    p->width = width;
    p->color = color;
    p->pattern = NULL;
    if (style == PS_DASH)
        p->dash = {6, 4};
    else if (style == PS_DOT)
        p->dash = {2, 2};
    else if (style == PS_DASHDOT)
        p->dash = {6, 4, 2, 4};
    else if (style == PS_DASHDOTDOT)
        p->dash = {6, 4, 2, 4, 2, 4};
    return p;
}
/* 这些 Gdi* 结构体没有自定义构造函数，`new T()` 是值初始化（先全零再构造成员），
 * 所以不需要（也不应该）再 memset：GdiBrush/GdiPen 里有 std::vector、GdiFont 里有
 * std::string，对非平凡可拷贝类型 memset 是 UB，编译器也会警告。 */
static GdiBrush *makeSolidBrush(COLORREF color)
{
    GdiBrush *b = new GdiBrush();
    b->style = BS_SOLID;
    b->color = color;
    return b;
}
static GdiFont *makeFont(int height, const wchar_t *face, int weight, int italic, int underline, int strike)
{
    GdiFont *f = new GdiFont();
    f->lf.lfHeight = height;
    f->lf.lfWeight = weight;
    f->lf.lfItalic = (BYTE)italic;
    f->lf.lfUnderline = (BYTE)underline;
    f->lf.lfStrikeOut = (BYTE)strike;
    if (face)
        wcsncpy(f->lf.lfFaceName, face, LF_FACESIZE - 1);
    f->sizePx = (height < 0) ? -height : (height > 0 ? height : 12);
    f->bold = weight >= 700;
    f->italic = italic != 0;
    f->underline = underline != 0;
    f->strike = strike != 0;
    f->cssFont = buildCssFont(f);
    /* 浏览器实测字体度量（GDI tmAscent/tmDescent 口径）。拿不到（老环境或返回 NaN）
     * 时退回经验估值 —— 估值只作为兜底，真实度量才能让 DT_VCENTER 与 wrapContent
     * 行高和 cairo 版（FreeType ascender/descender）对齐。 */
    {
        /* 同一 cssFont 字符串的度量只问浏览器一次（CreateFontIndirect 会被频繁调用）。 */
        static std::map<std::string, std::pair<double, double>> s_metrics;
        auto it = s_metrics.find(f->cssFont);
        double a, d;
        if (it != s_metrics.end())
        {
            a = it->second.first;
            d = it->second.second;
        }
        else
        {
            val m = SwinxFontMetrics(f->cssFont.c_str(), (int)f->cssFont.size());
            a = m[0].as<double>();
            d = m[1].as<double>();
            if (!(a > 0) || a != a || !(d >= 0) || d != d)
            {
                a = f->sizePx * 0.95;
                d = f->sizePx * 0.25;
            }
            s_metrics[f->cssFont] = std::make_pair(a, d);
        }
        f->ascent = ceil(a);
        f->descent = ceil(d);
        f->lineH = f->ascent + f->descent;
    }
    return f;
}

/* ---------- canvas 后备位图 ---------- */
extern "C" Canvas2DSurface *SwinxCanvasCreateSurfaceRaw(
int w, int h, BOOL alpha)
{
    /* 1:1：canvas 物理分辨率 == 请求尺寸。DPI 缩放在 SOUI 布局层完成。 */
    val canvas = SwinxCanvasCreate(w, h, alpha ? 1 : 0);
    val ctx = canvas["_swinxC2dCtx"]; // 缓存的 2D context（见 HDC__ 构造）
    if (ctx.isUndefined())
    {
        ctx = canvas.call<val>("getContext",std::string( "2d"), val::object());
        canvas.set(std::string("_swinxC2dCtx"), ctx);
    }
    /* 把 ctx 登记进 JS 派发器的注册表，拿一个整数下标（同一块 canvas 复用同一个
     * 下标：ctx 缓存在 canvas 的 _swinxC2dCtx 上，_swxId 与它一一对应）。 */
    val idv = canvas["_swxId"];
    int ctxId;
    if (!idv.isUndefined() && !idv.isNull())
        ctxId = idv.as<int>();
    else
    {
        ctxId = SwinxCtxRegister(ctx);
        canvas.set(std::string("_swxId"), ctxId);
    }
    Canvas2DSurface *s = new Canvas2DSurface();
    s->canvas = canvas;
    s->ctx = ctx;
    s->ctxId = ctxId;
    s->width = w;
    s->height = h;
    s->bitsPixel = 32;
    s->stride = w * 4;
    s->bits = nullptr;
    s->alpha = alpha;
    return s;
}

extern "C" HBITMAP SwinxCanvasCreateBitmap(int w, int h, BOOL alpha)
{
    return (HBITMAP)InitGdiObj(OBJ_BITMAP, SwinxCanvasCreateSurfaceRaw(w, h, alpha));
}

/* ===================== DC 生命周期（HDC__ 方法 + 创建/销毁）===================== */
HDC__::HDC__(HWND _hwnd)
{
    hwnd = _hwnd;
    // 默认 1x1 内存画布；窗口 DC 随后 SelectObject(bmp) 换成窗口后备画布
    val canvas = SwinxCanvasCreate(1, 1, 1);
    val ctx = canvas.call<val>("getContext",std::string( "2d"), val::object());
    canvas.set(std::string("_swinxC2dCtx"), ctx);
    this->canvas = canvas;
    this->ctx = ctx;
    /* 1x1 的占位 ctx 也要登记（SelectObject 换位图时才换成真正的目标） */
    this->ctxId = SwinxCtxRegister(ctx);
    this->surfW = 1;
    this->surfH = 1;
    crText = 0;
    crBk = 0x00FFFFFF;
    pen = (HPEN)GetStockObject(BLACK_PEN);
    brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    hfont = (HFONT)GetStockObject(SYSTEM_FONT);
    bkMode = OPAQUE;
    nSave = 0;
    textAlign = TA_LEFT | TA_TOP;
    rop2 = R2_COPYPEN;
    polyFillMode = ALTERNATE;
    graphicsMode = GM_COMPATIBLE;
    brushOrg.x = brushOrg.y = 0;
    ptOrigin.x = ptOrigin.y = 0;
    ptViewport.x = ptViewport.y = 0;
    ptWinOrg.x = ptWinOrg.y = 0;
    xfWorld.eM11 = xfWorld.eM22 = 1.0f;
    xfWorld.eM12 = xfWorld.eM21 = 0.0f;
    xfWorld.eDx = xfWorld.eDy = 0.0f;
    rcClip.left = rcClip.top = 0;
    rcClip.right = surfW;
    rcClip.bottom = surfH;
    hClipRgn = NULL;
    pathRecording = FALSE;
    pathHasPos = FALSE;
    miterLimit = 10.0f;
    stretchBltMode = COLORONCOLOR;
    recording = NULL;
}

HDC__::~HDC__()
{
    // canvas/ctx 的 emscripten::val 析构时减引用，浏览器随后 GC
    for (auto &sp : stateStack)
        if (sp->hClipRgn)
            DeleteObject(sp->hClipRgn);
    if (hClipRgn)
        DeleteObject(hClipRgn);
    if (recording)
        CloseEnhMetaFile((HDC)this); // 见下方 stub
}

/* 复制一份区域（CombineRgn RGN_COPY；失败/空源都落成空区域，调用方按需判空） */
static HRGN clipRgnCopy(HRGN src)
{
    HRGN dst = CreateRectRgn(0, 0, 0, 0);
    if (src)
        CombineRgn(dst, src, NULL, RGN_COPY);
    return dst;
}

/* ---- 裁剪记账的坐标基准 ----------------------------------------------------
 * hClipRgn / rcClip 统一存**设备坐标**（画布像素，全 DC 唯一不变的基准）。
 *
 * 原因：SOUI 的 SOsrPanel::Draw（listview/tileview/treeview 的 item 绘制）会先
 * GetClipRegion/GetClipBox 取出「当前裁剪区」，再 OffsetViewportOrg(rcItem) 把视
 * 图原点挪到 item 原点，之后 item 子树里的窗口全部用 **item 相对坐标** 与这个裁
 * 剂区做 RectInRegion/GetRgnBox 比较。真 GDI 的 GetClipBox/GetClipRgn 返回的是
 * **当前映射下的逻辑坐标**（视图原点一动，同一块裁剪就重新表达一遍）；cairo 后
 * 端靠 cairo 把 clip 换算成 user 空间得到同样行为。canvas 的 clip 固化在设备空间，
 * 如果记账也存「设置当时的逻辑坐标」，视图原点一变这些值就变成另一套坐标系的数
 * ——MVC 页 item 全部被判在裁剪外，列表整页空白。存设备坐标 + 导出时按当前变换
 * 逆映射回逻辑，等价于真 GDI 的「动态重表达」。 */

/* 与 applyCtxTransform / srcRectDevice 同一公式的正向映射（逻辑 → 设备）。
 * m[0..5] = a b c d e f：x' = a*x + c*y + e, y' = b*x + d*y + f。 */
static void clipTransformOf(HDC__ *hdc, double m[6])
{
    const XFORM &w = hdc->xfWorld;
    m[0] = w.eM11;
    m[1] = w.eM12;
    m[2] = w.eM21;
    m[3] = w.eM22;
    m[4] = (double)w.eDx + ((double)hdc->ptViewport.x - ((double)hdc->ptWinOrg.x * w.eM11 + (double)hdc->ptWinOrg.y * w.eM21));
    m[5] = (double)w.eDy + ((double)hdc->ptViewport.y - ((double)hdc->ptWinOrg.x * w.eM12 + (double)hdc->ptWinOrg.y * w.eM22));
}

/* 仿射逆矩阵（SOUI 只用缩放/平移，det 恒非 0；即便奇异也安全落成恒等） */
static void clipTransformInv(const double m[6], double im[6])
{
    double det = m[0] * m[3] - m[1] * m[2];
    if (det == 0)
    {
        im[0] = im[3] = 1;
        im[1] = im[2] = im[4] = im[5] = 0;
        return;
    }
    im[0] = m[3] / det;
    im[1] = -m[1] / det;
    im[2] = -m[2] / det;
    im[3] = m[0] / det;
    im[4] = (m[2] * m[5] - m[3] * m[4]) / det;
    im[5] = (m[1] * m[4] - m[0] * m[5]) / det;
}

/* 按 2x3 仿射映射矩形（四角取包围盒，floor/ceil 向外扩；恒等映射是精确的）。 */
static void mapRectByMatrix(const RECT &rcIn, const double m[6], RECT &rcOut)
{
    double x[4] = {
        m[0] * rcIn.left + m[2] * rcIn.top + m[4],
        m[0] * rcIn.right + m[2] * rcIn.top + m[4],
        m[0] * rcIn.left + m[2] * rcIn.bottom + m[4],
        m[0] * rcIn.right + m[2] * rcIn.bottom + m[4],
    };
    double y[4] = {
        m[1] * rcIn.left + m[3] * rcIn.top + m[5],
        m[1] * rcIn.right + m[3] * rcIn.top + m[5],
        m[1] * rcIn.left + m[3] * rcIn.bottom + m[5],
        m[1] * rcIn.right + m[3] * rcIn.bottom + m[5],
    };
    double xmin = x[0], xmax = x[0], ymin = y[0], ymax = y[0];
    for (int i = 1; i < 4; i++)
    {
        if (x[i] < xmin) xmin = x[i];
        if (x[i] > xmax) xmax = x[i];
        if (y[i] < ymin) ymin = y[i];
        if (y[i] > ymax) ymax = y[i];
    }
    rcOut.left = (LONG)floor(xmin);
    rcOut.top = (LONG)floor(ymin);
    rcOut.right = (LONG)ceil(xmax);
    rcOut.bottom = (LONG)ceil(ymax);
}

/* 把区域整体映射到另一套坐标（GetRegionData → 逐矩形映射 → ExtCreateRegion）。
 * 返回新区域（调用方负责 DeleteObject）；失败/空返回 NULL。 */
static HRGN mapRgnByMatrix(HDC__ *hdc, HRGN src, const double m[6])
{
    if (!src) return NULL;
    DWORD need = GetRegionData(src, 0, NULL);
    if (!need) return NULL;
    std::vector<uint8_t> buf(need);
    RGNDATA *rd = (RGNDATA *)&buf[0];
    if (GetRegionData(src, need, rd) != need || rd->rdh.nCount == 0)
        return NULL;
    std::vector<RECT> rcs(rd->rdh.nCount);
    RECT *prcSrc = (RECT *)&rd->Buffer;
    for (DWORD i = 0; i < rd->rdh.nCount; i++)
        mapRectByMatrix(prcSrc[i], m, rcs[i]);
    /* ExtCreateRegion 只看 rdh.nCount（swinx region.cpp 逐矩形 Union），
     * 复用同一块 header，只把 nCount 的矩形逐个替换 */
    RECT *prcDst = (RECT *)&rd->Buffer;
    for (DWORD i = 0; i < rd->rdh.nCount; i++)
        prcDst[i] = rcs[i];
    return ExtCreateRegion(NULL, need, rd);
}

/* 把「记账区域」（设备坐标）重新表达成**当前逻辑空间**；无记账返回 NULL。 */
static HRGN clipRgnAsLogical(HDC__ *hdc)
{
    if (!hdc->hClipRgn) return NULL;
    double m[6], im[6];
    clipTransformOf(hdc, m);
    clipTransformInv(m, im);
    HRGN rgnLog = mapRgnByMatrix(hdc, hdc->hClipRgn, im);
    return rgnLog ? rgnLog : clipRgnCopy(hdc->hClipRgn);
}

/* 把调用方给的（当前逻辑坐标的）区域存入记账：统一换算成设备坐标。
 * 视觉裁剪已经用逻辑坐标在当前变换下应用到 canvas，这里只负责账本。 */
static void clipRgnStore(HDC__ *hdc, HRGN logicalSrc)
{
    double m[6];
    clipTransformOf(hdc, m);
    HRGN rgnDev = mapRgnByMatrix(hdc, logicalSrc, m);
    if (hdc->hClipRgn)
        DeleteObject(hdc->hClipRgn);
    /* 映射失败兜底存原样（GetClipBox 会退回旧行为，好过把裁剪报成"无"） */
    hdc->hClipRgn = rgnDev ? rgnDev : clipRgnCopy(logicalSrc);
}

int HDC__::SaveState()
{
    DCState st;
    st.crText = crText;
    st.crBk = crBk;
    st.pen = pen;
    st.brush = brush;
    st.hfont = hfont;
    st.bkMode = bkMode;
    st.textAlign = textAlign;
    st.rop2 = rop2;
    st.polyFillMode = polyFillMode;
    st.graphicsMode = graphicsMode;
    st.brushOrg = brushOrg;
    st.ptOrigin = ptOrigin;
    st.ptViewport = ptViewport;
    st.ptWinOrg = ptWinOrg;
    st.xfWorld = xfWorld;
    st.rcClip = rcClip;
    /* 裁剪区存私有副本：save 之后裁剪区可能继续被改（GDI 语义），不能共享句柄 */
    st.hClipRgn = hClipRgn ? clipRgnCopy(hClipRgn) : NULL;
    /* JS 侧状态缓存一并入栈：ctx.save() 存的正是这一整套状态，restore 时取回这份
     * 快照比"整份作废"精确，也不必让 restore 之后每个属性设置都重设一遍。 */
    st.c2d = c2d;
    stateStack.push_back(std::make_shared<DCState>(st));
    Op(this, SWX_SAVE);
    return (int)stateStack.size();
}

BOOL HDC__::RestoreState(int nState)
{
    /* Win32 语义：正数是 SaveDC 返回的标识，负数是**相对当前**的偏移
     * （-1 = 最近一次保存的状态）。SRenderTarget_GDI::PopClip() 正是用 -1，
     * 所以这条路径必须走"弹到栈顶"而不是"清空整个栈" —— 以前把 -1 当清空处理，
     * ctx 那边靠 restore 抹平了，但 HDC 自己的字段（视图原点、世界变换、裁剪
     * 记账 rcClip）全部丢掉，之后 GetClipBox 会一直返回被裁小甚至空的框，SOUI
     * 据此算出"这块已经画过"，屏幕就不再更新（点标签页没反应就是这么来的）。 */
    if (nState < 0 && !stateStack.empty())
        nState = (int)stateStack.size() + 1 + nState;
    if (nState <= 0)
    {
        // 恢复到最初（清空整个栈）
        C2DStateCache lastC2d;
        while (!stateStack.empty())
        {
            if (stateStack.back()->hClipRgn)
                DeleteObject(stateStack.back()->hClipRgn);
            lastC2d = stateStack.back()->c2d;
            stateStack.pop_back();
            /* ctx.restore 回滚到的正是这次 save 时的快照，缓存直接取这份快照 */
            Op(this, SWX_RESTORE);
            c2d = lastC2d;
        }
        /* 裁剪区回到 DC 创建时（未设过裁剪）。canvas 侧的 clip 已由 restore 链抹平 */
        if (hClipRgn)
        {
            DeleteObject(hClipRgn);
            hClipRgn = NULL;
        }
        nSave = 0;
        return TRUE;
    }
    if (nState > (int)stateStack.size())
        return FALSE;
    int discard = (int)stateStack.size() - nState;
    while (discard-- > 0)
    {
        if (stateStack.back()->hClipRgn)
            DeleteObject(stateStack.back()->hClipRgn);
        stateStack.pop_back();
        Op(this, SWX_RESTORE);
    }
    /* nState>=1，上面最多弹到还剩一项，真正"回滚到"的那一档在下面弹出，
     * 缓存快照在那里一并取回（c2d = st.c2d）。 */
    if (!stateStack.empty())
    {
        DCState &st = *stateStack.back();
        crText = st.crText;
        crBk = st.crBk;
        pen = st.pen;
        brush = st.brush;
        hfont = st.hfont;
        bkMode = st.bkMode;
        textAlign = st.textAlign;
        rop2 = st.rop2;
        polyFillMode = st.polyFillMode;
        graphicsMode = st.graphicsMode;
        brushOrg = st.brushOrg;
        ptOrigin = st.ptOrigin;
        ptViewport = st.ptViewport;
        ptWinOrg = st.ptWinOrg;
        xfWorld = st.xfWorld;
        rcClip = st.rcClip;
        /* 裁剪区所有权从快照移交回 DC（快照随后被丢弃，不能让句柄悬空/泄漏） */
        if (hClipRgn)
            DeleteObject(hClipRgn);
        hClipRgn = st.hClipRgn;
        st.hClipRgn = NULL;
        /* 这一档才是真正回滚到的目标状态：ctx.restore() 之后 canvas 侧恰好就是
         * 它 save 时的样子，所以缓存直接取这份快照（相当于 C2dInvalidate 的
         * 精确版，省掉 restore 之后每个属性的重设）。 */
        c2d = st.c2d;
        stateStack.pop_back();
        Op(this, SWX_RESTORE);
        /* ctx.restore() 已经把变换拉回 save 时的值，但我们所有变换都是绝对 setTransform
         * 写入的，这里再落一次保证「HDC 字段」与「ctx 实际变换」永远一致。 */
        applyCtxTransform(this);
    }
    return TRUE;
}

/* ---------- DC 创建/选择/销毁 ---------- */
extern "C" HDC WINAPI CreateCompatibleDC(HDC hdc)
{
    return (HDC) new HDC__(hdc ? hdc->hwnd : NULL);
}

extern "C" BOOL WINAPI DeleteDC(HDC hdc)
{
    if (!hdc)
        return FALSE;
    delete (_SDC *)hdc;
    return TRUE;
}

extern "C" HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h)
{
    if (!hdc || !h)
        return NULL;
    int type = GetObjectType(h);
    HGDIOBJ old = NULL;
    switch (type)
    {
    case OBJ_BITMAP:
    {
        Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(h);
        old = hdc->bmp;
        hdc->bmp = (HBITMAP)h;
        hdc->canvas = s->canvas;
        hdc->ctx = s->ctx;
        hdc->ctxId = s->ctxId;
        hdc->surfW = s->width;
        hdc->surfH = s->height;
        /* 换了绘制目标 ⇒ 裁剪区重新从整块目标算起（与 Win32 一致：SelectObject
         * 换位图后原来的裁剪区对新表面没有意义）。 */
        hdc->rcClip.left = hdc->rcClip.top = 0;
        hdc->rcClip.right = s->width;
        hdc->rcClip.bottom = s->height;
        if (hdc->hClipRgn)
        {
            DeleteObject(hdc->hClipRgn);
            hdc->hClipRgn = NULL;
        }
        /* 换了一块 canvas ⇒ 新 ctx 的样式/变换/裁剪全是默认值，旧缓存一概作废 */
        C2dInvalidate(hdc);
        break;
    }
    case OBJ_PEN: old = hdc->pen; hdc->pen = (HPEN)h; break;
    case OBJ_BRUSH: old = hdc->brush; hdc->brush = (HBRUSH)h; break;
    case OBJ_FONT: old = hdc->hfont; hdc->hfont = (HFONT)h; break;
    default: return NULL;
    }
    return old;
}

extern "C" HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type)
{
    if (!hdc)
        return NULL;
    switch (type)
    {
    case OBJ_PEN: return hdc->pen;
    case OBJ_BRUSH: return hdc->brush;
    case OBJ_FONT: return hdc->hfont;
    case OBJ_BITMAP: return hdc->bmp;
    default: return NULL;
    }
}

/* ---------- 画笔 / 画刷 / 字体 ---------- */
extern "C" HPEN WINAPI CreatePen(int iStyle, int cWidth, COLORREF color)
{
    return (HPEN)InitGdiObj(OBJ_PEN, makePen(iStyle, cWidth, color));
}
extern "C" HPEN WINAPI CreatePenIndirect(CONST LOGPEN *plpen)
{
    if (!plpen)
        return NULL;
    return (HPEN)InitGdiObj(OBJ_PEN, makePen(plpen->lopnStyle, plpen->lopnWidth.x, plpen->lopnColor));
}
/* 扩展画笔：cairo 后端用 LOGPENEX（含 patternBrush/自定义 dash）。canvas2d 的
 * 画笔模型就是 GdiPen(style/width/color/dash)，这里把常用路径映射过去：
 *   - BS_NULL/BS_HOLLOW   → PS_NULL 画笔
 *   - BS_SOLID 且无自定义 dash → 等价 CreatePen
 *   - 其余（含 PS_USERSTYLE+自定义 dash）→ USERSTYLE 画笔，dash 交给 applyPen 的
 *     ctx.setLineDash 渲染（图案画笔不建模，退化为实心，不影响布局绘制）。 */
extern "C" HPEN WINAPI ExtCreatePen(DWORD iPenStyle, DWORD cWidth, CONST LOGBRUSH *plbrush, DWORD cStyle, CONST DWORD *pstyle)
{
    if (!plbrush)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (plbrush->lbStyle == BS_NULL || plbrush->lbStyle == BS_HOLLOW)
        return CreatePen(PS_NULL, cWidth, plbrush->lbColor);
    if (plbrush->lbStyle == BS_SOLID && cStyle == 0)
        return CreatePen(iPenStyle & PS_STYLE_MASK, cWidth, plbrush->lbColor);
    GdiPen *p = makePen((iPenStyle & ~PS_STYLE_MASK) | PS_USERSTYLE, cWidth, plbrush->lbColor);
    /* lbStyle == BS_BRUSH 是 swinx 自己的扩展：lbHatch 直接放画刷句柄，
     * render-gdi 的 DrawGradientPath 用它给路径描上扫掠渐变。 */
    if (plbrush->lbStyle == BS_BRUSH)
        p->pattern = (HBRUSH)plbrush->lbHatch;
    if (cStyle > 0 && pstyle)
    {
        p->dash.resize(cStyle);
        for (DWORD i = 0; i < cStyle; i++)
            p->dash[i] = (float)pstyle[i];
    }
    return (HPEN)InitGdiObj(OBJ_PEN, p);
}
extern "C" HBRUSH WINAPI CreateSolidBrush(COLORREF color)
{
    return (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(color));
}
extern "C" HBRUSH WINAPI CreatePatternBrush(HBITMAP bmp)
{
    return CreatePatternBrush2(bmp, kTileMode_Repeat);
}
extern "C" HBRUSH WINAPI CreatePatternBrush2(HBITMAP bmp, TILEMODE tileMode)
{
    Canvas2DSurface *s = bmp ? (Canvas2DSurface *)GetGdiObjPtr(bmp) : nullptr;
    GdiBrush *b = new GdiBrush();
    b->style = BS_PATTERN;
    b->pattern = s;
    b->tileMode = tileMode;
    return (HBRUSH)InitGdiObj(OBJ_BRUSH, b);
}
extern "C" HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL, UINT) { return (HBRUSH)GetStockObject(NULL_BRUSH); }
extern "C" HBRUSH WINAPI CreateDIBPatternBrushPt(CONST VOID *, UINT)
{
    return (HBRUSH)GetStockObject(NULL_BRUSH);
}
extern "C" HBRUSH WINAPI CreateGradientBrush(const GRADIENTITEM *pGradients, int nCount,
                                             const GRADIENTINFO *info, BYTE byAlpha, TILEMODE tileMode)
{
    GdiBrush *b = new GdiBrush();
    b->style = BS_GRADIENT;
    b->gradAlpha = byAlpha;
    b->tileMode = tileMode;
    if (info)
    {
        b->gradType = info->type;
        if (info->type == grad_linear)
            b->gradAngle = info->angle;
        else if (info->type == grad_radial)
        {
            b->gradRadius = info->radial.radius;
            b->gradCX = info->radial.centerX;
            b->gradCY = info->radial.centerY;
        }
        else
        {
            b->gradStart = info->sweep.startAngle;
            b->gradCX = info->sweep.centerX;
            b->gradCY = info->sweep.centerY;
        }
    }
    for (int i = 0; i < nCount; ++i)
        b->gradItems.push_back(pGradients[i]);
    return (HBRUSH)InitGdiObj(OBJ_BRUSH, b);
}
extern "C" HFONT WINAPI CreateFontIndirectW(CONST LOGFONTW *lplf)
{
    if (!lplf)
        return (HFONT)GetStockObject(SYSTEM_FONT);
    return (HFONT)InitGdiObj(OBJ_FONT, makeFont(lplf->lfHeight, lplf->lfFaceName, lplf->lfWeight,
                                               lplf->lfItalic, lplf->lfUnderline, lplf->lfStrikeOut));
}
extern "C" HFONT WINAPI CreateFontIndirectA(CONST LOGFONTA *lplf)
{
    if (!lplf)
        return (HFONT)GetStockObject(SYSTEM_FONT);
    std::wstring wf = utf8ToW(lplf->lfFaceName, -1);
    const wchar_t *wface = wf.c_str();
    return (HFONT)InitGdiObj(OBJ_FONT, makeFont(lplf->lfHeight, wface, lplf->lfWeight,
                                               lplf->lfItalic, lplf->lfUnderline, lplf->lfStrikeOut));
}
extern "C" HFONT WINAPI CreateFontW(int cHeight, int cWidth, int cEsc, int cOri, int cWeight,
                                   DWORD bItalic, DWORD bUnderline, DWORD bStrike, DWORD iCharSet,
                                   DWORD iOut, DWORD iClip, DWORD iQuality, DWORD iPitch, LPCWSTR face)
{
    return (HFONT)InitGdiObj(OBJ_FONT, makeFont(cHeight, face ? face : L"", cWeight,
                                               bItalic, bUnderline, bStrike));
}
extern "C" HFONT WINAPI CreateFontA(int cHeight, int cWidth, int cEsc, int cOri, int cWeight,
                                   DWORD bItalic, DWORD bUnderline, DWORD bStrike, DWORD iCharSet,
                                   DWORD iOut, DWORD iClip, DWORD iQuality, DWORD iPitch, LPCSTR face)
{
    std::wstring wf = utf8ToW(face, -1);
    const wchar_t *wface = wf.c_str();
    return (HFONT)InitGdiObj(OBJ_FONT, makeFont(cHeight, wface, cWeight, bItalic, bUnderline, bStrike));
}

extern "C" BOOL WINAPI DeleteObject(HGDIOBJ ho)
{
    if (!ho)
        return FALSE;
    /* 只减引用。底层画笔/画刷/字体/位图由 InitGdiObj 注册的 cbFree 在引用归零时
     * 释放（见 gdi_brush_free 等）。这里若再 delete 一次 GetGdiObjPtr(ho)，随后
     * CloseHandle 让 nRef 归零又会回调 cbFree 删第二遍 —— 双击释放，wasm 里表现为
     * free() 断言 abort，栈：DeleteObject → CloseHandle → ~_Handle → gdi_brush_free
     * → operator delete → free → abort。cairo 后端一直是 `CloseHandle` 单路径。 */
    CloseHandle((HANDLE)ho);
    return TRUE;
}

extern "C" int WINAPI GetObjectA(HGDIOBJ h, int c, LPVOID pv)
{
    if (!h || !pv)
        return 0;
    switch (GetObjectType(h))
    {
    case OBJ_BITMAP:
    {
        if (c < (int)sizeof(BITMAP))
            return 0;
        Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(h);
        BITMAP *b = (BITMAP *)pv;
        syncSurfaceBits(s);
        b->bmType = 0;
        b->bmWidth = s->width;
        b->bmHeight = s->height;
        b->bmWidthBytes = s->stride;
        b->bmPlanes = 1;
        b->bmBitsPixel = s->bitsPixel;
        b->bmBits = s->bits;
        return (int)sizeof(BITMAP);
    }
    case OBJ_PEN:
    {
        if (c < (int)sizeof(LOGPEN))
            return 0;
        GdiPen *p = (GdiPen *)GetGdiObjPtr(h);
        LOGPEN *lp = (LOGPEN *)pv;
        lp->lopnStyle = p->style;
        lp->lopnWidth.x = p->width;
        lp->lopnColor = p->color;
        return (int)sizeof(LOGPEN);
    }
    case OBJ_BRUSH:
    {
        if (c < (int)sizeof(LOGBRUSH))
            return 0;
        GdiBrush *b = (GdiBrush *)GetGdiObjPtr(h);
        LOGBRUSH *lb = (LOGBRUSH *)pv;
        lb->lbStyle = b->style;
        lb->lbColor = b->color;
        lb->lbHatch = 0;
        return (int)sizeof(LOGBRUSH);
    }
    case OBJ_FONT:
    {
        GdiFont *f = (GdiFont *)GetGdiObjPtr(h);
        /* GetObjectA 的调用方缓冲按 LOGFONTA 准备（UNICODE 未定义时
         * LOGFONT==LOGFONTA：20+8+32=60 字节；而 emscripten 的 wchar_t 是
         * 4 字节，LOGFONTW=20+8+128=156）。Win32 语义：cb 不够就少拷/不拷，
         * 绝不多写 —— 曾无视 c 整块 memcpy LOGFONTW，把 STipCtrl::Create 的
         * LOGFONT lf 踩穿（ASAN stack-buffer-overflow，WRITE of size 156；
         * 症状是 tooltip 首次创建时 memory access out of bounds，消消乐
         * 玩几局必崩）。与 cairo 后端 GetObjectA 的保护同一口径。 */
        LOGFONTA lfa;
        memcpy(&lfa, &f->lf, FIELD_OFFSET(LOGFONTW, lfFaceName));
        WideCharToMultiByte(CP_UTF8, 0, f->lf.lfFaceName, -1, lfa.lfFaceName, LF_FACESIZE, NULL, NULL);
        if (c < (int)sizeof(LOGFONTA))
            return 0;
        memcpy(pv, &lfa, sizeof(LOGFONTA));
        return (int)sizeof(LOGFONTA);
    }
    }
    return 0;
}
/* GetObjectW 的 FONT 分支返回 LOGFONTW（UNICODE 调用方的 LOGFONT 即
 * LOGFONTW）；不能转发 GetObjectA —— 那会往 LOGFONTW 缓冲里写 LOGFONTA，
 * 且 faceName 编码错。其余对象两版结构相同，转 GetObjectA。 */
extern "C" int WINAPI GetObjectW(HGDIOBJ h, int c, LPVOID pv)
{
    if (!h || !pv)
        return 0;
    if (GetObjectType(h) == OBJ_FONT)
    {
        if (c < (int)sizeof(LOGFONTW))
            return 0;
        GdiFont *f = (GdiFont *)GetGdiObjPtr(h);
        memcpy(pv, &f->lf, sizeof(LOGFONTW));
        return (int)sizeof(LOGFONTW);
    }
    return GetObjectA(h, c, pv);
}

/* ---------- Stock / System 颜色 ---------- */
extern "C" HGDIOBJ WINAPI GetStockObject(int i)
{
    static HBRUSH hNullBrush = 0, hWhiteBrush = 0, hBlackBrush = 0, hGrayBrush = 0, hLtGray = 0, hDkGray = 0;
    static HPEN hNullPen = 0, hBlackPen = 0, hWhitePen = 0;
    static HFONT hSysFont = 0;
    switch (i)
    {
    case NULL_BRUSH: /* HOLLOW_BRUSH == NULL_BRUSH，合并 */
        if (!hNullBrush) hNullBrush = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0)), ((GdiBrush *)GetGdiObjPtr(hNullBrush))->style = BS_NULL;
        return hNullBrush;
    case WHITE_BRUSH: if (!hWhiteBrush) hWhiteBrush = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0xFFFFFF)); return hWhiteBrush;
    case BLACK_BRUSH: if (!hBlackBrush) hBlackBrush = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0)); return hBlackBrush;
    case GRAY_BRUSH: if (!hGrayBrush) hGrayBrush = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0x808080)); return hGrayBrush;
    case LTGRAY_BRUSH: if (!hLtGray) hLtGray = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0xC0C0C0)); return hLtGray;
    case DKGRAY_BRUSH: if (!hDkGray) hDkGray = (HBRUSH)InitGdiObj(OBJ_BRUSH, makeSolidBrush(0x404040)); return hDkGray;
    case NULL_PEN: if (!hNullPen) hNullPen = (HPEN)InitGdiObj(OBJ_PEN, makePen(PS_NULL, 0, 0)); return hNullPen;
    case BLACK_PEN: if (!hBlackPen) hBlackPen = (HPEN)InitGdiObj(OBJ_PEN, makePen(PS_SOLID, 1, 0)); return hBlackPen;
    case WHITE_PEN: if (!hWhitePen) hWhitePen = (HPEN)InitGdiObj(OBJ_PEN, makePen(PS_SOLID, 1, 0xFFFFFF)); return hWhitePen;
    case SYSTEM_FONT:
    case DEFAULT_GUI_FONT:
    case ANSI_VAR_FONT:
    case DEVICE_DEFAULT_FONT:
        if (!hSysFont) hSysFont = (HFONT)InitGdiObj(OBJ_FONT, makeFont(12, L"", FW_NORMAL, 0, 0, 0));
        return hSysFont;
    default: return NULL;
    }
}

extern "C" COLORREF WINAPI GetSysColor(int i)
{
    switch (i)
    {
    case COLOR_WINDOW: return 0x00FFFFFF;
    case COLOR_BTNFACE: /* COLOR_3DFACE == COLOR_BTNFACE，合并 */ return 0x00F0F0F0;
    case COLOR_BTNTEXT:
    case COLOR_WINDOWTEXT: return 0x00000000;
    case COLOR_HIGHLIGHT: return 0x003169E0;
    case COLOR_GRAYTEXT: return 0x00808080;
    case COLOR_HOTLIGHT: return 0x00A630C0;
    case COLOR_MENU: return 0x00FFFFFF;
    case COLOR_SCROLLBAR: return 0x00C8C8C8;
    default: return 0x00FFFFFF;
    }
}
extern "C" HBRUSH WINAPI GetSysColorBrush(int i) { return CreateSolidBrush(GetSysColor(i)); }
extern "C" HPEN WINAPI GetSysColorPen(int i) { return CreatePen(PS_SOLID, 1, GetSysColor(i)); }

/* ---------- 图元 ---------- */

/* 椭圆弧的"参数角"。GDI 传进来的是弧两端落在椭圆上的点，不一定是坐标轴端点，
 * 所以要把点归一化到单位圆再取 atan2 —— 直接用 atan2(dy,dx) 在 rx≠ry 时角度不对。
 * 角度口径与 canvas 一致（+x 为 0、y 向下为正），省掉坐标翻转。 */
static double arcAngle(double px, double py, double cx, double cy, double rx, double ry)
{
    double dx = (rx != 0.0) ? (px - cx) / rx : (px - cx);
    double dy = (ry != 0.0) ? (py - cy) / ry : (py - cy);
    return atan2(dy, dx);
}

/* 画一段椭圆弧。GDI 的 Arc/Pie/Chord 默认逆时针；在 canvas 的 y-向下口径里
 * "逆时针"= 角度递减，对应 ellipse(..., counterclockwise=true)。第一版用了默认的
 * 顺时针，于是 0°→90° 的弧被画成 270°——进度环只剩一小段、仪表盘糊成一团都是它。 */
static void emitArc(val &c, double cx, double cy, double rx, double ry, double a0, double a1)
{
    if (rx < 0)
        rx = -rx;
    if (ry < 0)
        ry = -ry;
    c.call<void>("ellipse", cx, cy, rx, ry, 0.0, a0, a1, true);
}

/* BeginPath..EndPath 期间的图元必须记账而不是直接画：render-gdi 的 FillPath/
 * StrokePath 就是用 MoveToEx/LineTo/PolyBezierTo 把路径搭出来再一次性回放的。 */
static inline void recMoveTo(HDC hdc, double x, double y)
{
    hdc->recordedPath.push_back({PT_MOVETO, x, y, 0, 0, 0, 0});
    hdc->pathHasPos = TRUE;
}
static inline void recLineTo(HDC hdc, double x, double y)
{
    if (!hdc->pathHasPos)
    {
        recMoveTo(hdc, x, y);
        return;
    }
    hdc->recordedPath.push_back({PT_LINETO, x, y, 0, 0, 0, 0});
}
static inline void recArc(HDC hdc, double cx, double cy, double rx, double ry, double a0, double a1)
{
    hdc->recordedPath.push_back({PT_ARC_OP, cx, cy, rx, ry, a0, a1});
    hdc->pathHasPos = TRUE;
}

extern "C" BOOL WINAPI Rectangle(HDC hdc, int left, int top, int right, int bottom)
{
    if (!hdc)
        return FALSE;
    if (hdc->pathRecording)
    {
        recMoveTo(hdc, left, top);
        recLineTo(hdc, right, top);
        recLineTo(hdc, right, bottom);
        recLineTo(hdc, left, bottom);
        hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0});
        hdc->ptOrigin.x = right;
        hdc->ptOrigin.y = bottom;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    c.call<void>("rect", (double)left, (double)top, (double)(right - left), (double)(bottom - top));
    fillStroke(hdc, c, left, top, right - left, bottom - top);
    hdc->ptOrigin.x = right;
    hdc->ptOrigin.y = bottom;
    return TRUE;
}
extern "C" BOOL WINAPI RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height)
{
    if (!hdc)
        return FALSE;
    if (hdc->pathRecording)
    {
        /* 圆角矩形按直角矩形记账：路径只用于填充/裁剪，四角的差异肉眼不可见，
         * 而 PathOp 没有表达圆角的槽位。 */
        return Rectangle(hdc, left, top, right, bottom);
    }
    val &c = hdc->ctx;
    double r = (double)(width > height ? height : width) / 2.0;
    if (r < 0)
        r = 0;
    Op(hdc, SWX_BEGIN_PATH);
    c.call<void>("roundRect", (double)left, (double)top, (double)(right - left), (double)(bottom - top), r, r);
    fillStroke(hdc, c, left, top, right - left, bottom - top);
    hdc->ptOrigin.x = right;
    hdc->ptOrigin.y = bottom;
    return TRUE;
}
extern "C" BOOL WINAPI Ellipse(HDC hdc, int left, int top, int right, int bottom)
{
    if (!hdc)
        return FALSE;
    double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    double rx = (right - left) / 2.0, ry = (bottom - top) / 2.0;
    if (hdc->pathRecording)
    {
        recArc(hdc, cx, cy, rx, ry, 0.0, 2 * M_PI);
        hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0});
        hdc->ptOrigin.x = right;
        hdc->ptOrigin.y = (int)cy;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    emitArc(c, cx, cy, rx, ry, 0.0, 2 * M_PI);
    fillStroke(hdc, c, left, top, right - left, bottom - top);
    hdc->ptOrigin.x = right;
    hdc->ptOrigin.y = (int)cy;
    return TRUE;
}
extern "C" BOOL WINAPI Arc(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    if (!hdc)
        return FALSE;
    double cx = (x1 + x2) / 2.0, cy = (y1 + y2) / 2.0;
    double rx = (x2 - x1) / 2.0, ry = (y2 - y1) / 2.0;
    double a0 = arcAngle(x3, y3, cx, cy, rx, ry), a1 = arcAngle(x4, y4, cx, cy, rx, ry);
    if (hdc->pathRecording)
    {
        recArc(hdc, cx, cy, rx, ry, a0, a1);
        hdc->ptOrigin.x = x4;
        hdc->ptOrigin.y = y4;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    emitArc(c, cx, cy, rx, ry, a0, a1);
    if (applyPen(hdc, c, (double)x1, (double)y1, (double)(x2 - x1), (double)(y2 - y1)))
        c.call<void>("stroke");
    return TRUE;
}
extern "C" BOOL WINAPI Pie(HDC hdc, int left, int top, int right, int bottom, int xr1, int yr1, int xr2, int yr2)
{
    if (!hdc)
        return FALSE;
    double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    double rx = (right - left) / 2.0, ry = (bottom - top) / 2.0;
    double a0 = arcAngle(xr1, yr1, cx, cy, rx, ry), a1 = arcAngle(xr2, yr2, cx, cy, rx, ry);
    if (hdc->pathRecording)
    {
        recMoveTo(hdc, cx, cy);
        recArc(hdc, cx, cy, rx, ry, a0, a1);
        hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0});
        hdc->ptOrigin.x = xr2;
        hdc->ptOrigin.y = yr2;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    Op(hdc, SWX_MOVE_TO, cx, cy);
    emitArc(c, cx, cy, rx, ry, a0, a1);
    Op(hdc, SWX_CLOSE_PATH);
    fillStroke(hdc, c, left, top, right - left, bottom - top);
    return TRUE;
}
extern "C" BOOL WINAPI Chord(HDC hdc, int left, int top, int right, int bottom, int xr1, int yr1, int xr2, int yr2)
{
    if (!hdc)
        return FALSE;
    double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
    double rx = (right - left) / 2.0, ry = (bottom - top) / 2.0;
    double a0 = arcAngle(xr1, yr1, cx, cy, rx, ry), a1 = arcAngle(xr2, yr2, cx, cy, rx, ry);
    if (hdc->pathRecording)
    {
        recArc(hdc, cx, cy, rx, ry, a0, a1);
        hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0});
        hdc->ptOrigin.x = xr2;
        hdc->ptOrigin.y = yr2;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    emitArc(c, cx, cy, rx, ry, a0, a1);
    Op(hdc, SWX_CLOSE_PATH);
    fillStroke(hdc, c, left, top, right - left, bottom - top);
    return TRUE;
}
extern "C" BOOL WINAPI Polygon_Priv(HDC hdc, const POINT *apt, int cpt)
{
    if (!hdc || !apt || cpt < 1)
        return FALSE;
    if (hdc->pathRecording)
    {
        recMoveTo(hdc, apt[0].x, apt[0].y);
        for (int i = 1; i < cpt; ++i)
            recLineTo(hdc, apt[i].x, apt[i].y);
        hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0});
        hdc->ptOrigin.x = apt[0].x;
        hdc->ptOrigin.y = apt[0].y;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    Op(hdc, SWX_MOVE_TO, (double)apt[0].x, (double)apt[0].y);
    for (int i = 1; i < cpt; ++i)
        Op(hdc, SWX_LINE_TO, (double)apt[i].x, (double)apt[i].y);
    Op(hdc, SWX_CLOSE_PATH);
    fillStroke(hdc, c, apt[0].x, apt[0].y, 0, 0);
    hdc->ptOrigin.x = apt[0].x;
    hdc->ptOrigin.y = apt[0].y;
    return TRUE;
}
extern "C" BOOL WINAPI Polyline(HDC hdc, CONST POINT *apt, int cpt)
{
    if (!hdc || !apt || cpt < 1)
        return FALSE;
    if (hdc->pathRecording)
    {
        recMoveTo(hdc, apt[0].x, apt[0].y);
        for (int i = 1; i < cpt; ++i)
            recLineTo(hdc, apt[i].x, apt[i].y);
        hdc->ptOrigin.x = apt[cpt - 1].x;
        hdc->ptOrigin.y = apt[cpt - 1].y;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    Op(hdc, SWX_MOVE_TO, (double)apt[0].x, (double)apt[0].y);
    for (int i = 1; i < cpt; ++i)
        Op(hdc, SWX_LINE_TO, (double)apt[i].x, (double)apt[i].y);
    if (applyPen(hdc, c, (double)apt[0].x, (double)apt[0].y,
                 (double)(apt[cpt - 1].x - apt[0].x), (double)(apt[cpt - 1].y - apt[0].y)))
        c.call<void>("stroke");
    hdc->ptOrigin.x = apt[cpt - 1].x;
    hdc->ptOrigin.y = apt[cpt - 1].y;
    return TRUE;
}

/* 三次贝塞尔：apt 里每 3 个点是一段（两个控制点 + 终点）。cpt 必须是 3 的倍数。
 * 第一版把 PolyBezierTo 直接转成 Polyline，等于把控制点当折点连折线 —— 曲线控件
 * （水位波浪、仪表盘阴影）画出来就是一堆硬折角。 */
static void bezierSegments(HDC hdc, const POINT *apt, DWORD cpt, BOOL firstIsStart)
{
    DWORD i = 0;
    if (firstIsStart)
    {
        if (hdc->pathRecording)
            recMoveTo(hdc, apt[0].x, apt[0].y);
        hdc->ptOrigin.x = apt[0].x;
        hdc->ptOrigin.y = apt[0].y;
        i = 1;
    }
    for (; i + 2 < cpt; i += 3)
    {
        if (hdc->pathRecording)
        {
            if (!hdc->pathHasPos)
                recMoveTo(hdc, hdc->ptOrigin.x, hdc->ptOrigin.y);
            hdc->recordedPath.push_back({PT_BEZIERTO, (double)apt[i + 2].x, (double)apt[i + 2].y,
                                         (double)apt[i].x, (double)apt[i].y,
                                         (double)apt[i + 1].x, (double)apt[i + 1].y});
        }
        else
        {
            val &c = hdc->ctx;
            Op(hdc, SWX_BEGIN_PATH);
            Op(hdc, SWX_MOVE_TO, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y);
            Op(hdc, SWX_BEZIER, (double)apt[i].x, (double)apt[i].y,
                         (double)apt[i + 1].x, (double)apt[i + 1].y,
                         (double)apt[i + 2].x, (double)apt[i + 2].y);
            if (applyPen(hdc, c, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y,
                         (double)(apt[i + 2].x - hdc->ptOrigin.x), (double)(apt[i + 2].y - hdc->ptOrigin.y)))
                c.call<void>("stroke");
        }
        hdc->ptOrigin.x = apt[i + 2].x;
        hdc->ptOrigin.y = apt[i + 2].y;
    }
}
extern "C" BOOL WINAPI PolyBezier(HDC hdc, const POINT *apt, DWORD cpt)
{
    if (!hdc || !apt || cpt < 4)
        return FALSE;
    bezierSegments(hdc, apt, cpt, TRUE);
    return TRUE;
}
extern "C" BOOL WINAPI PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt)
{
    if (!hdc || !apt || cpt < 3)
        return FALSE;
    bezierSegments(hdc, apt, cpt, FALSE);
    return TRUE;
}
extern "C" BOOL WINAPI PolyDraw(HDC hdc, LPPOINT lppt, LPBYTE lpbTypes, int cpt)
{
    if (!hdc || !lppt || !lpbTypes || cpt < 1)
        return FALSE;
    int i = 0;
    while (i < cpt)
    {
        if (lpbTypes[i] & PT_MOVETO)
        {
            hdc->ptOrigin.x = lppt[i].x;
            hdc->ptOrigin.y = lppt[i].y;
            if (hdc->pathRecording)
                recMoveTo(hdc, lppt[i].x, lppt[i].y);
            i++;
        }
        else if ((lpbTypes[i] & PT_BEZIERTO) && i + 2 < cpt)
        {
            POINT seg[3] = {lppt[i], lppt[i + 1], lppt[i + 2]};
            bezierSegments(hdc, seg, 3, FALSE);
            i += 3;
        }
        else
        {
            if (hdc->pathRecording)
                recLineTo(hdc, lppt[i].x, lppt[i].y);
            else
            {
                val &c = hdc->ctx;
                Op(hdc, SWX_BEGIN_PATH);
                Op(hdc, SWX_MOVE_TO, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y);
                Op(hdc, SWX_LINE_TO, (double)lppt[i].x, (double)lppt[i].y);
                if (applyPen(hdc, c, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y,
                             (double)(lppt[i].x - hdc->ptOrigin.x), (double)(lppt[i].y - hdc->ptOrigin.y)))
                    c.call<void>("stroke");
            }
            hdc->ptOrigin.x = lppt[i].x;
            hdc->ptOrigin.y = lppt[i].y;
            i++;
        }
    }
    return TRUE;
}
extern "C" BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT lpPoint)
{
    if (!hdc)
        return FALSE;
    if (lpPoint)
    {
        lpPoint->x = hdc->ptOrigin.x;
        lpPoint->y = hdc->ptOrigin.y;
    }
    hdc->ptOrigin.x = x;
    hdc->ptOrigin.y = y;
    if (hdc->pathRecording)
        recMoveTo(hdc, x, y);
    return TRUE;
}
extern "C" BOOL WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT lpPoint)
{
    if (!hdc || !lpPoint)
        return FALSE;
    lpPoint->x = hdc->ptOrigin.x;
    lpPoint->y = hdc->ptOrigin.y;
    return TRUE;
}
extern "C" BOOL WINAPI LineTo(HDC hdc, int nXEnd, int nYEnd)
{
    if (!hdc)
        return FALSE;
    if (hdc->pathRecording)
    {
        recLineTo(hdc, nXEnd, nYEnd);
        hdc->ptOrigin.x = nXEnd;
        hdc->ptOrigin.y = nYEnd;
        return TRUE;
    }
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    Op(hdc, SWX_MOVE_TO, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y);
    Op(hdc, SWX_LINE_TO, (double)nXEnd, (double)nYEnd);
    if (applyPen(hdc, c, (double)hdc->ptOrigin.x, (double)hdc->ptOrigin.y,
                 (double)(nXEnd - hdc->ptOrigin.x), (double)(nYEnd - hdc->ptOrigin.y)))
        c.call<void>("stroke");
    hdc->ptOrigin.x = nXEnd;
    hdc->ptOrigin.y = nYEnd;
    return TRUE;
}

/* ---------- 矩形填充 / 框 / 反色 ---------- */
extern "C" int WINAPI FillRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr)
{
    if (!hDC || !lprc)
        return 0;
    val &c = hDC->ctx;

    /* FillRect 只填充、绝不描边（cairo 参考实现同样只 cairo_fill 一次）。第一版把它
     * 转成了 Rectangle()，而 Rectangle 是「填充 + 用当前画笔描边」两步 —— 于是每一次
     * FillRect 都会多描一圈 1px 的画笔色。表现就是：没写 colorBorder 的面板/卡片/页签
     * 全都多出一圈深色边框，相邻面板之间还有 1px 深色接缝。 */
    HBRUSH old = (HBRUSH)SelectObject(hDC, hbr ? hbr : (HBRUSH)GetStockObject(NULL_BRUSH));
    int ret = 0;
    if (applyBrush(hDC, c, lprc->left, lprc->top, lprc->right - lprc->left, lprc->bottom - lprc->top))
    {
        Op(hDC, SWX_FILL_RECT, (double)lprc->left, (double)lprc->top,
           (double)(lprc->right - lprc->left), (double)(lprc->bottom - lprc->top));
        ret = 1;
    }
    SelectObject(hDC, old);
    return ret;
}
extern "C" int WINAPI ClearRect(HDC hdc, const RECT *lprc, COLORREF cr)
{
    if (!hdc || !lprc) return 0;
    val &c = hdc->ctx;
    /* SOUI 的语义（win 版用 SColor(cr) 直写 32bpp 位、cairo 版用
     * CAIRO_OPERATOR_SOURCE）：把矩形**替换**成 cr（alpha 取高位字节，0=全透明）
     * —— 分层窗口的 OnPaint 用 ClearRect(&rcWnd, 0) 清出透明底、画完内容再
     * OnCommitSurface 贴回宿主。canvas 的默认 source-over 只会把不透明色叠上去，
     * "透明底"变成了黑底：消消乐棋子窗口（stack 的 fade 页面 alpha<255 走分层
     * 路径）在选中动画期间整格变黑就是它。
     * 用 copy 算子替换像素（连 alpha 一起）；必须先用 clip 限制作用范围，
     * 否则 copy 会把矩形之外的整块画布全部清空。 */
    char buf[40];
    unsigned a = (unsigned)((cr >> 24) & 0xFF);
    snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.4f)",
             (int)GetRValue(cr), (int)GetGValue(cr), (int)GetBValue(cr), a / 255.0);
    C2DStateCache st0 = hdc->c2d; /* save/restore 配对：结束后缓存整体取回 */
    Op(hdc, SWX_SAVE);
    Op(hdc, SWX_CLIP_RECT, (double)lprc->left, (double)lprc->top,
       (double)lprc->right, (double)lprc->bottom);
    c.set(std::string("globalCompositeOperation"), std::string("copy"));
    c.set(std::string("fillStyle"), std::string(buf));
    Op(hdc, SWX_FILL_RECT, (double)lprc->left, (double)lprc->top,
       (double)(lprc->right - lprc->left), (double)(lprc->bottom - lprc->top));
    Op(hdc, SWX_RESTORE); /* globalCompositeOperation 一并还原 */
    hdc->c2d = st0;
    return 1;
}
extern "C" int WINAPI FrameRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr)
{
    if (!hDC || !lprc) return 0;
    // 仅描边（用画刷颜色）
    GdiBrush *b = hbr ? (GdiBrush *)GetGdiObjPtr(hbr) : nullptr;
    val &c = hDC->ctx;
    COLORREF col = b ? b->color : 0;
    C2dStroke(hDC, col);
    c.set(std::string("lineWidth"), 1.0);
    c.call<void>("setLineDash", val::array());
    c.call<void>("strokeRect", (double)lprc->left + 0.5, (double)lprc->top + 0.5,
                 (double)(lprc->right - lprc->left) - 1, (double)(lprc->bottom - lprc->top) - 1);
    return 1;
}
extern "C" BOOL WINAPI InvertRect(HDC hDC, CONST RECT *lprc)
{
    if (!hDC || !lprc) return FALSE;
    val &c = hDC->ctx;
    C2DStateCache st0 = hDC->c2d;
    Op(hDC, SWX_SAVE);
    c.set(std::string("globalCompositeOperation"),std::string( "difference"));
    c.set(std::string("fillStyle"),std::string( "#ffffff"));
    Op(hDC, SWX_FILL_RECT, (double)lprc->left, (double)lprc->top,
       (double)(lprc->right - lprc->left), (double)(lprc->bottom - lprc->top));
    Op(hDC, SWX_RESTORE);
    hDC->c2d = st0;
    return TRUE;
}

/* ---------- 位图混合 / 拷贝 ---------- */
/* rop → globalCompositeOperation 的映射统一在 C2dRop（文件头部）里做，带状态缓存。 */

/* 把「源 DC 的逻辑坐标矩形」换算成「源 canvas 的像素矩形」。
 *
 * GDI 的 BitBlt / StretchBlt / AlphaBlend / TransparentBlt 的源矩形参数是**源 DC
 * 的逻辑坐标**，要经过源 DC 的窗口原点 / 视图原点（以及世界变换）才落到像素上。
 * 只看 canvas 像素坐标会漏掉这次换算 —— 而这个换算恰恰是 SOUI 分层窗口绘制
 * （SWindow::OnPaint 里 IsLayeredWindow 那条分支）的关键：
 *     源 RT 建好后先 OffsetViewportOrg(-rcWnd.left, -rcWnd.top)，
 *     随后 OnCommitSurface 用**同一个** rcWnd 既当目标矩形又当源矩形。
 * 于是逻辑坐标 (rcWnd.left, rcWnd.top) 在源 RT 里正好落在像素 (0,0)。
 * 少了这次换算，源矩形整个落在小位图外面 —— 症状是「动画一开始，被动画的控件
 * 整块消失」（补间 alpha/rotate/translate 三个色块、模态表单遮罩等都是这条路）。
 *
 * 顺带处理源 DC 上的世界变换缩放：把矩形的两个对角点都映射一遍再取包围盒，
 * SVG 之类给源 DC 设过缩放时尺寸也才对得上。 */
static void srcRectDevice(HDC hdc, int x1, int y1, int w, int h,
                          double &sx, double &sy, double &sw, double &sh)
{
    const XFORM &m = hdc->xfWorld;
    double ox = (double)hdc->ptViewport.x - ((double)hdc->ptWinOrg.x * m.eM11 + (double)hdc->ptWinOrg.y * m.eM21);
    double oy = (double)hdc->ptViewport.y - ((double)hdc->ptWinOrg.x * m.eM12 + (double)hdc->ptWinOrg.y * m.eM22);
    double xa = (double)x1 * m.eM11 + (double)y1 * m.eM21 + m.eDx + ox;
    double ya = (double)x1 * m.eM12 + (double)y1 * m.eM22 + m.eDy + oy;
    double xb = (double)(x1 + w) * m.eM11 + (double)(y1 + h) * m.eM21 + m.eDx + ox;
    double yb = (double)(x1 + w) * m.eM12 + (double)(y1 + h) * m.eM22 + m.eDy + oy;
    sx = xa < xb ? xa : xb;
    sy = ya < yb ? ya : yb;
    sw = fabs(xb - xa);
    sh = fabs(yb - ya);
}

extern "C" BOOL WINAPI BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop)
{
    if (!hdc || !hdcSrc) return FALSE;
    if (cx <= 0 || cy <= 0) return TRUE;
    /* 源矩形是源 DC 的逻辑坐标，先换算到源 canvas 的像素坐标（见 srcRectDevice） */
    double sx, sy, sw, sh;
    srcRectDevice(hdcSrc, x1, y1, cx, cy, sx, sy, sw, sh);
    val &c = hdc->ctx;
    /* 不再无条件 save/restore：那对调用除了多两次跨语言开销，还会把 ctx 状态整体
     * 回滚 —— 属性缓存（C2dStateCache）每次都要作废重来，等于白记。rop 是持久状态，
     * 用缓存化的 C2dRop 设；默认 SRCCOPY 时若已经是 source-over 就一次调用都不发。
     * BitBlt 是一帧里最频繁的绘制（皮肤九宫格贴图），98 次/帧。 */
    C2dRop(hdc, rop);
    /* 不碰 imageSmoothingEnabled：1:1 拷贝本来就用不到它，改它就是改像素 */
    SwxBlit(hdc->ctxId, hdcSrc->ctxId, (double)x, (double)y, (double)cx, (double)cy,
            sx, sy, sw, sh, -1);
    return TRUE;
}
extern "C" BOOL WINAPI StretchBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, int cx2, int cy2, DWORD rop)
{
    if (!hdc || !hdcSrc) return FALSE;
    if (cx <= 0 || cy <= 0 || cx2 <= 0 || cy2 <= 0) return TRUE;
    double sx, sy, sw, sh;
    srcRectDevice(hdcSrc, x1, y1, cx2, cy2, sx, sy, sw, sh);
    val &c = hdc->ctx;
    C2dRop(hdc, rop);
    C2dSmoothing(hdc, hdc->stretchBltMode != COLORONCOLOR);
    SwxBlit(hdc->ctxId, hdcSrc->ctxId, (double)x, (double)y, (double)cx, (double)cy,
            sx, sy, sw, sh, -1);
    return TRUE;
}
extern "C" BOOL WINAPI AlphaBlend(HDC hdcDest, int xo, int yo, int wD, int hD, HDC hdcSrc, int xo2, int yo2, int wS, int hS, BLENDFUNCTION ftn)
{
    if (!hdcDest || !hdcSrc) return FALSE;
    if (wD <= 0 || hD <= 0 || wS <= 0 || hS <= 0) return TRUE;
    double sx, sy, sw, sh;
    srcRectDevice(hdcSrc, xo2, yo2, wS, hS, sx, sy, sw, sh);
    val &c = hdcDest->ctx;
    double a = (ftn.SourceConstantAlpha) / 255.0;
    /* globalAlpha 也是持久状态：设值→画→设回 1，两次 set 都走缓存比较，
     * 比 save/restore 两次调用便宜（且不会作废整份状态缓存）。 */
    C2dAlpha(hdcDest, a);
    SwxBlit(hdcDest->ctxId, hdcSrc->ctxId, (double)xo, (double)yo, (double)wD, (double)hD,
            sx, sy, sw, sh, -1);
    C2dAlpha(hdcDest, 1.0);
    return TRUE;
}
extern "C" BOOL WINAPI TransparentBlt(HDC hdcDest, int xo, int yo, int wD, int hD, HDC hdcSrc, int xo2, int yo2, int wS, int hS, UINT)
{
    // 颜色键近似：直接 drawImage（忽略透明色键），复杂情形在文档中标注为已知近似
    if (!hdcDest || !hdcSrc) return FALSE;
    if (wD <= 0 || hD <= 0 || wS <= 0 || hS <= 0) return TRUE;
    double sx, sy, sw, sh;
    srcRectDevice(hdcSrc, xo2, yo2, wS, hS, sx, sy, sw, sh);
    SwxBlit(hdcDest->ctxId, hdcSrc->ctxId, (double)xo, (double)yo, (double)wD, (double)hD,
            sx, sy, sw, sh, -1);
    return TRUE;
}
extern "C" BOOL WINAPI PatBlt(HDC, int, int, int, int, DWORD) { return TRUE; }
extern "C" BOOL WINAPI StretchDIBits(HDC hdc, INT x_dst, INT y_dst, INT w_dst, INT h_dst,
                                    INT x_src, INT y_src, INT w_src, INT h_src, const void *bits,
                                    const BITMAPINFO *bmi, UINT coloruse, DWORD rop)
{
    if (!hdc || !bits) return FALSE;
    // 把 ARGB32 源写进一张临时 canvas 再 drawImage（DIB → canvas）
    int W = w_src > 0 ? w_src : 1, H = h_src > 0 ? h_src : 1;
    val tmp = SwinxCanvasCreate(W, H, 1);
    SwinxCanvasPutPixels(tmp, (int)(intptr_t)bits, W, H);
    val &c = hdc->ctx;
    c.call<void>("drawImage", tmp, (double)x_src, (double)y_src, (double)w_src, (double)h_src,
                 (double)x_dst, (double)y_dst, (double)w_dst, (double)h_dst);
    return TRUE;
}

/* 把源位图的一个子矩形按 EXPEND_MODE 画到目标矩形。
 *
 * 与 cairo 后端 DrawBitmapEx/AlphaBlendEx 语义对齐，三条模式行为不同：
 *   EXPEND_MODE_NONE    —— 原尺寸贴上去（不缩放），多用于九宫格的四个角；
 *   EXPEND_MODE_STRETCH —— 拉伸铺满目标矩形；
 *   EXPEND_MODE_TILE    —— 从目标左上开始平铺，超出目标矩形的部分按目标矩形剪裁
 *                          （不能像以前那样把最后一行/列压缩进去，那会把图拉变形）。
 * filterLevel 由 HIWORD(expendMode) 携带（kNone_FilterLevel == 0 ⇒ 关闭平滑）。
 * 注意 rcSrc 是"每状态子矩形"：多状态皮肤图把各状态按行/列排在一张图里，
 * 调用方通过 pRcSrc 选定其中一个状态，绝不能当成整图处理。 */
static void canvas_blit_rect(HDC__ *hdc, val &c, Canvas2DSurface *s, const RECT *rcDest, const RECT *rcSrc, UINT expendMode, double alpha)
{
    int sw = (int)(rcSrc->right - rcSrc->left);
    int sh = (int)(rcSrc->bottom - rcSrc->top);
    int dw = (int)(rcDest->right - rcDest->left);
    int dh = (int)(rcDest->bottom - rcDest->top);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    C2DStateCache st0 = hdc->c2d; /* save/restore 配对：结束后缓存整体取回 */
    Op(hdc, SWX_SAVE);
    if (alpha < 1.0)
    {
        Op(hdc, SWX_ALPHA, alpha);
        hdc->c2d.alpha = alpha;
    }
    /* 九宫格拼接时相邻块必须严丝合缝：打开平滑会在块边界产生半透明缝，
     * 与 cairo 后端 SetAntialiasMode(ANTIALIAS_NONE) 的做法对应。
     * SwxBlit 里按需设 imageSmoothingEnabled（值没变就不下发）。 */
    /* EXPEND_MODE_STRETCH 时平滑由调用方（DrawBitmapEx 的 filterLevel）决定，
     * 这里传 -1 表示"别动"；其余模式强制关平滑以保证拼接无缝。 */
    const int smooth = (expendMode == EXPEND_MODE_STRETCH) ? -1 : 0;

    Op(hdc, SWX_CLIP_RECT, (double)rcDest->left, (double)rcDest->top,
       (double)rcDest->right, (double)rcDest->bottom);

    /* 源 canvas 与坐标一一对应（1:1，无缩放） */
    double sx = (double)rcSrc->left, sy = (double)rcSrc->top;
    double psw = (double)sw, psh = (double)sh;

    if (expendMode == EXPEND_MODE_TILE)
    {
        for (int ty = (int)rcDest->top; ty < (int)rcDest->bottom; ty += sh)
            for (int tx = (int)rcDest->left; tx < (int)rcDest->right; tx += sw)
                SwxBlit(hdc->ctxId, s->ctxId, (double)tx, (double)ty, (double)sw, (double)sh,
                        sx, sy, psw, psh, smooth);
    }
    else if (expendMode == EXPEND_MODE_NONE)
    {
        SwxBlit(hdc->ctxId, s->ctxId, (double)rcDest->left, (double)rcDest->top, (double)sw, (double)sh,
                sx, sy, psw, psh, smooth);
    }
    else
    {
        SwxBlit(hdc->ctxId, s->ctxId, (double)rcDest->left, (double)rcDest->top, (double)dw, (double)dh,
                sx, sy, psw, psh, smooth);
    }
    Op(hdc, SWX_RESTORE);
    hdc->c2d = st0;
}

extern "C" BOOL WINAPI DrawBitmapEx(HDC hdc, LPCRECT pRcDest, HBITMAP bmp, LPCRECT pRcSrc, UINT expendMode, BYTE byAlpha)
{
    if (!hdc || !bmp || !pRcDest) return FALSE;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(bmp);
    if (!s) return FALSE;
    int filterLevel = HIWORD(expendMode);
    expendMode = LOWORD(expendMode);
    RECT rcSrc;
    if (pRcSrc)
        rcSrc = *pRcSrc;
    else
    {
        rcSrc.left = 0;
        rcSrc.top = 0;
        rcSrc.right = s->width;
        rcSrc.bottom = s->height;
    }
    val &c = hdc->ctx;
    C2DStateCache st0 = hdc->c2d;
    Op(hdc, SWX_SAVE);
    Op(hdc, SWX_SMOOTH, filterLevel != 0 ? 1 : 0);
    hdc->c2d.smoothing = (filterLevel != 0) ? 1 : 0;
    canvas_blit_rect(hdc, c, s, pRcDest, &rcSrc, expendMode, (double)byAlpha / 255.0);
    Op(hdc, SWX_RESTORE);
    hdc->c2d = st0;
    return TRUE;
}

/* 九宫格拉伸绘制。逐行照搬 swinx cairo 版 DrawBitmap9Patch：
 *  1) 源/目标各切成 4 条边界（xSrc/xDest/ySrc/yDest）；
 *  2) 当左右（或上下）边距之和大于目标尺寸时，按"只画左段 / 只画右段 / 左段+补右段"
 *     三种情况收缩切分点 —— 少了这段，dr-dl 会变成负数，中间与边全部画不出来；
 *  3) 3x3 逐格按各自模式绘制：四角 EXPEND_MODE_NONE，四边与中心用调用方给的 expendMode。
 * 这里必须使用 pRcSrc 而不是整图尺寸：皮肤图常把多个状态横向/纵向排在一张图里。 */
extern "C" BOOL WINAPI DrawBitmap9Patch(HDC hdc, LPCRECT pRcDest, HBITMAP hBmp, LPCRECT pRcSrc,
                                       LPCRECT pRcSourMargin, UINT expendMode, BYTE byAlpha)
{
    if (!hdc || !hBmp || !pRcDest || !pRcSrc || !pRcSourMargin) return FALSE;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(hBmp);
    if (!s) return FALSE;

    int xDest[4] = { pRcDest->left, pRcDest->left + pRcSourMargin->left, pRcDest->right - pRcSourMargin->right, pRcDest->right };
    int xSrc[4] = { pRcSrc->left, pRcSrc->left + pRcSourMargin->left, pRcSrc->right - pRcSourMargin->right, pRcSrc->right };
    int yDest[4] = { pRcDest->top, pRcDest->top + pRcSourMargin->top, pRcDest->bottom - pRcSourMargin->bottom, pRcDest->bottom };
    int ySrc[4] = { pRcSrc->top, pRcSrc->top + pRcSourMargin->top, pRcSrc->bottom - pRcSourMargin->bottom, pRcSrc->bottom };

    // 首先保证九宫分割正常
    if (!(xSrc[0] <= xSrc[1] && xSrc[1] <= xSrc[2] && xSrc[2] <= xSrc[3])) return FALSE;
    if (!(ySrc[0] <= ySrc[1] && ySrc[1] <= ySrc[2] && ySrc[2] <= ySrc[3])) return FALSE;

    // 调整目标位置
    int nDestWid = pRcDest->right - pRcDest->left;
    int nDestHei = pRcDest->bottom - pRcDest->top;

    if ((pRcSourMargin->left + pRcSourMargin->right) > nDestWid)
    { // 边缘宽度大于目标宽度的处理
        if (pRcSourMargin->left >= nDestWid)
        { // 只绘制左边部分
            xSrc[1] = xSrc[2] = xSrc[3] = xSrc[0] + nDestWid;
            xDest[1] = xDest[2] = xDest[3] = xDest[0] + nDestWid;
        }
        else if (pRcSourMargin->right >= nDestWid)
        { // 只绘制右边部分
            xSrc[0] = xSrc[1] = xSrc[2] = xSrc[3] - nDestWid;
            xDest[0] = xDest[1] = xDest[2] = xDest[3] - nDestWid;
        }
        else
        { // 先绘制左边部分，剩余的用右边填充
            int nRemain = xDest[3] - xDest[1];
            xSrc[2] = xSrc[3] - nRemain;
            xDest[2] = xDest[3] - nRemain;
        }
    }

    if (pRcSourMargin->top + pRcSourMargin->bottom > nDestHei)
    {
        if (pRcSourMargin->top >= nDestHei)
        { // 只绘制上边部分
            ySrc[1] = ySrc[2] = ySrc[3] = ySrc[0] + nDestHei;
            yDest[1] = yDest[2] = yDest[3] = yDest[0] + nDestHei;
        }
        else if (pRcSourMargin->bottom >= nDestHei)
        { // 只绘制下边部分
            ySrc[0] = ySrc[1] = ySrc[2] = ySrc[3] - nDestHei;
            yDest[0] = yDest[1] = yDest[2] = yDest[3] - nDestHei;
        }
        else
        { // 先绘制上边部分，剩余的用下边填充
            int nRemain = yDest[3] - yDest[1];
            ySrc[2] = ySrc[3] - nRemain;
            yDest[2] = yDest[3] - nRemain;
        }
    }

    // 定义绘制模式
    UINT mode[3][3] = { { EXPEND_MODE_NONE, expendMode, EXPEND_MODE_NONE },
                        { expendMode, expendMode, expendMode },
                        { EXPEND_MODE_NONE, expendMode, EXPEND_MODE_NONE } };

    val &c = hdc->ctx;
    double alpha = (double)byAlpha / 255.0;
    /* 对齐 cairo 版的 SetAntialiasMode(ANTIALIAS_NONE)：九宫格 9 块是拼接出来的，
     * 只要有一块被抗锯齿/双线性过滤吃到邻居像素，块边界就会出现半透明缝。 */
    C2DStateCache st0 = hdc->c2d;
    Op(hdc, SWX_SAVE);
    Op(hdc, SWX_SMOOTH, 0);
    hdc->c2d.smoothing = 0;
    for (int y = 0; y < 3; y++)
    {
        if (ySrc[y] == ySrc[y + 1]) continue;
        for (int x = 0; x < 3; x++)
        {
            if (xSrc[x] == xSrc[x + 1]) continue;
            RECT rcSrc = { xSrc[x], ySrc[y], xSrc[x + 1], ySrc[y + 1] };
            RECT rcDest = { xDest[x], yDest[y], xDest[x + 1], yDest[y + 1] };
            if (rcSrc.left >= rcSrc.right || rcSrc.top >= rcSrc.bottom) continue;
            if (rcDest.left >= rcDest.right || rcDest.top >= rcDest.bottom) continue;
            canvas_blit_rect(hdc, c, s, &rcDest, &rcSrc, mode[y][x], alpha);
        }
    }
    Op(hdc, SWX_RESTORE);
    hdc->c2d = st0;
    return TRUE;
}

/* ---------- 渐变填充 ---------- */
extern "C" BOOL WINAPI GradientFill(HDC hdc, TRIVERTEX *pVertices, ULONG nVertices, void *pMesh, ULONG nMesh, DWORD dwMode)
{
    if (!hdc || !pVertices || nVertices < 2) return FALSE;
    val &c = hdc->ctx;
    // 仅支持 GRADIENT_FILL_RECT_H / _V（最常用）：两顶点决定矩形对角与起止色
    auto color16 = [](COLOR16 v) -> double { return (double)v / 65535.0; };
    auto toCss = [&](TRIVERTEX &v) -> std::string {
        uint8_t r = (uint8_t)(color16(v.Red) * 255 + 0.5);
        uint8_t g = (uint8_t)(color16(v.Green) * 255 + 0.5);
        uint8_t b = (uint8_t)(color16(v.Blue) * 255 + 0.5);
        char buf[16]; snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
        return std::string(buf);
    };
    double x0 = pVertices[0].x, y0 = pVertices[0].y, x1 = pVertices[1].x, y1 = pVertices[1].y;
    val g = c.call<val>("createLinearGradient", x0, y0, x1, y1);
    g.call<void>("addColorStop", 0.0, toCss(pVertices[0]));
    g.call<void>("addColorStop", 1.0, toCss(pVertices[1]));
    int rx = (int)std::min(x0, x1), ry = (int)std::min(y0, y1);
    int rw = (int)std::abs(x1 - x0), rh = (int)std::abs(y1 - y0);
    C2dFillValue(hdc, g);
    c.call<void>("fillRect", (double)rx, (double)ry, (double)rw, (double)rh);
    return TRUE;
}

/* ---------- 剪裁 ---------- */
extern "C" int WINAPI SaveDC(HDC hdc) { if (!hdc) return 0; return ((HDC__*)hdc)->SaveState(); }
extern "C" BOOL WINAPI RestoreDC(HDC hdc, int nSavedDC) { if (!hdc) return FALSE; return ((HDC__*)hdc)->RestoreState(nSavedDC); }
extern "C" int WINAPI SelectClipRgn(HDC hdc, HRGN hrgn)
{
    if (!hdc) return 0;
    val &c = hdc->ctx;
    if (!hrgn) { return 0; } // 不重置（重置由 SaveDC/RestoreDC 处理）

    /* 只拿包围盒当裁剪区是不够的：SWaveProgress 用 CombineEllipse 建了个椭圆区域，
     * 退化成矩形后蓝色的水就溢出到圆形外面。区域内部本身是一组互不重叠的扫描带
     * 矩形，逐条 rect() 再 clip() 就能得到精确形状，不必去猜"它是不是椭圆"。
     *
     * 区域的坐标是**逻辑坐标**（和 IntersectClipRect / ExcludeClipRect 一致），
     * 所以要保留当前变换，让 rect() 自己把逻辑坐标映射到设备：分层窗口的 RT 带
     * OffsetViewportOrg(-rcWnd.left,-rcWnd.top)，绝对坐标 (552,300) 才落回 (0,0)。
     * 以前这里先把变换清成单位矩阵、直接拿逻辑值当设备值，192x52 的小 RT 上就
     * 裁到了 (552,300)-(744,352) 这个完全在图外的矩形 —— 内容被裁得一点不剩。 */
    DWORD need = GetRegionData(hrgn, 0, NULL);
    if (need > sizeof(RGNDATAHEADER))
    {
        std::vector<uint8_t> buf(need);
        RGNDATA *rd = (RGNDATA *)&buf[0];
        if (GetRegionData(hrgn, need, rd) == need && rd->rdh.nCount > 0 && rd->rdh.nCount <= 4096)
        {
            RECT *prc = (RECT *)&rd->Buffer;
            Op(hdc, SWX_BEGIN_PATH);
            for (DWORD i = 0; i < rd->rdh.nCount; i++)
                c.call<void>("rect", (double)prc[i].left, (double)prc[i].top,
                             (double)(prc[i].right - prc[i].left),
                             (double)(prc[i].bottom - prc[i].top));
            /* 扫描带互不重叠，nonzero 与 evenodd 结果一致 */
            c.call<void>("clip", std::string("nonzero"));
            /* 记账：region 的包围盒（逻辑）换算成设备坐标，与现有裁剪区求交。
             * GetClipBox 靠它回答，SOUI 用它推脏矩形（见 HDC__::rcClip 的注释）。 */
            RECT rcBox;
            if (GetRgnBox(hrgn, &rcBox))
            {
                RECT rcDev;
                logicRectToDevice(hdc, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, rcDev);
                clipBoxIntersect(hdc, rcDev);
            }
            /* 记账精确形状：后续 ExtSelectClipRgn(RGN_DIFF) 等模式要拿「现有裁剪区」
             * 参与布尔运算，只有包围盒没法做差（SGridCtrl 固定行头/列头就是
             * PushClipRect(rcFixed, RGN_DIFF) 挖掉左上角实现的）。存设备坐标。 */
            clipRgnStore(hdc, hrgn);
            /* 裁剪区是任意形状（扫描带矩形），矩形缓存不再可信 */
            hdc->c2d.clipValid = FALSE;
            return 0;
        }
    }

    RECT rc;
    if (GetRgnBox(hrgn, &rc))
    {
        clipRectDevice(hdc, rc.left, rc.top, rc.right, rc.bottom);
        clipRgnStore(hdc, hrgn);
    }
    else
    {   /* 空区域：Win32 语义是把裁剪区清空。canvas 撤不掉已有的 clip，
         * 只能把记账置空，让 GetClipBox 如实回答"没有可画区域"。 */
        hdc->rcClip.left = hdc->rcClip.top = hdc->rcClip.right = hdc->rcClip.bottom = 0;
        if (hdc->hClipRgn)
        {
            DeleteObject(hdc->hClipRgn);
            hdc->hClipRgn = NULL;
        }
    }
    return 0;
}
/* 与 cairo 后端同构的区域代数裁剪（cairo/gdi.cpp ExtSelectClipRgn）：
 * 取现有裁剪区（GetClipRgn 口径）与 hrgn 按 mode 做布尔运算后重新应用。
 * SGridCtrl::DrawCells 用 PushClipRect(rcFixed, RGN_DIFF) 只挖掉左上角固定区、
 * 再在剩下的 L 形区域里画固定行头/列头 —— 以前 mode 被忽略、一律按 RGN_AND，
 * 裁剪区塌成 rcFixed 这一小块，行头/列头全被裁没（表头只剩"模块"角格、
 * 首列空白、固定区露白底）。 */
extern "C" int WINAPI ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode)
{
    if (!hdc || !hrgn) return 0;
    if (mode == RGN_COPY)
        return SelectClipRgn(hdc, hrgn);
    /* 未设过裁剪时以「当前逻辑空间下的整个 surface」为基准 —— 与「无界」语义在
     * AND 下等价（hrgn 落在 surface 外的部分本来也画不出来）。基准必须先逆映射
     * 回**当前逻辑空间**再与 hrgn（当前逻辑）做布尔运算：以前直接拿 (0,0,surfW,
     * surfH) **设备坐标**与逻辑坐标求交，RT 带 OffsetViewportOrg(-rcWnd.left,
     * -rcWnd.top) 时两者差一个视口原点，交集恒为空 —— 分层窗口 SWindow::OnPaint
     * 里 PushClipRect(rcClient) 之后所有绘制全被裁没，画面只剩 ClearRect 的透明
     * 底（属性动画 prop_box 动画期间整块消失的根因）。cairo 版走「无界=AND 直接
     * 得 hrgn」，两条路在这里等价；DIFF/OR 因基准同样是逻辑坐标而保持 Win32 语义
     * （整块 surface 挖洞 / 并集）。 */
    HRGN rgnNow;
    if (hdc->hClipRgn)
        rgnNow = clipRgnAsLogical(hdc);
    else
    {
        double m[6], im[6];
        clipTransformOf(hdc, m);
        clipTransformInv(m, im);
        RECT rcSurf = {0, 0, hdc->surfW, hdc->surfH}, rcL;
        mapRectByMatrix(rcSurf, im, rcL);
        rgnNow = CreateRectRgn(rcL.left, rcL.top, rcL.right, rcL.bottom);
    }
    int ret = CombineRgn(rgnNow, rgnNow, hrgn, mode);
    SelectClipRgn(hdc, rgnNow);
    DeleteObject(rgnNow); /* SelectClipRgn 内部已复制，这份临时可以丢 */
    return ret;
}
extern "C" int WINAPI IntersectClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    if (!hdc) return 0;
    /* 视觉上只交上新矩形这一项：canvas 的 clip 是累积求交的，
     * clip(R1) 之后再 clip(r) 就是 clip(R1∩r)，不需要把整条累积区域按当前
     * 变换重放一遍 —— 重放会在「两次裁剪之间视图/窗口原点变过」的场合把旧
     * term 平移到错误位置（Scintilla 逐行带 SetClip 的绘制在 150% 档整行
     * 被裁没就是它）。记账仍然走区域代数，保证 GetClipBox/DIFF 精确。 */
    clipRectDevice(hdc, left, top, right, bottom);
    /* 入参是当前逻辑坐标，记账是设备坐标：先映射再求交（恒等映射下精确不变） */
    RECT rcIn = { left, top, right, bottom };
    double m[6];
    clipTransformOf(hdc, m);
    RECT rcDevIn;
    mapRectByMatrix(rcIn, m, rcDevIn);
    HRGN hrgn = CreateRectRgn(rcDevIn.left, rcDevIn.top, rcDevIn.right, rcDevIn.bottom);
    if (hdc->hClipRgn)
    {
        CombineRgn(hdc->hClipRgn, hdc->hClipRgn, hrgn, RGN_AND);
        RECT rcBox;
        if (GetRgnBox(hdc->hClipRgn, &rcBox))
        {
            /* 记账区域已是设备坐标，包围盒直接收缩（rcClip 本就是交集语义） */
            if (rcBox.left > hdc->rcClip.left) hdc->rcClip.left = rcBox.left;
            if (rcBox.top > hdc->rcClip.top) hdc->rcClip.top = rcBox.top;
            if (rcBox.right < hdc->rcClip.right) hdc->rcClip.right = rcBox.right;
            if (rcBox.bottom < hdc->rcClip.bottom) hdc->rcClip.bottom = rcBox.bottom;
        }
    }
    else
    {
        hdc->hClipRgn = hrgn;
        hrgn = NULL;
    }
    if (hrgn) DeleteObject(hrgn);
    return 0;
}
extern "C" int WINAPI ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom)
{
    if (!hdc) return 0;
    /* 走区域代数（cairo 后端同款）：现有裁剪区减去矩形。区域差集的记账是精确的
     * （rcClip 包围盒随之缩小），不像旧 evenodd 近似只能处理"整块被排除"退化情形。 */
    HRGN hrgn = CreateRectRgn(left, top, right, bottom);
    int ret = ExtSelectClipRgn(hdc, hrgn, RGN_DIFF);
    DeleteObject(hrgn);
    return ret;
}
extern "C" int WINAPI GetClipBox(HDC hdc, LPRECT lprect)
{
    if (!hdc || !lprect) return 0;
    /* 如实返回当前裁剪区的包围盒。返回整块画布会让 SHostWnd::OnPrint 把脏矩形
     * 膨胀到全屏（shostwnd.cpp:746），进而把整幅 memRT 贴到窗口画布 —— 局部重绘
     * 于是变成"其余区域被刷成背景"。canvas 查不到 clip，账在 HDC__::rcClip 里。
     * 记账是设备坐标，Win32 GetClipBox 返回**当前逻辑坐标**：SOUI 的 item 绘制
     * （SOsrPanel::Draw）取 GetClipBox 后直接拿 item 相对坐标与之求交，必须逆
     * 映射回当前逻辑空间（恒等映射下精确不变）。 */
    double m[6], im[6];
    clipTransformOf(hdc, m);
    clipTransformInv(m, im);
    mapRectByMatrix(hdc->rcClip, im, *lprect);
    if (lprect->left >= lprect->right || lprect->top >= lprect->bottom)
    {
        /* 空裁剪：矩形置空、仍回 SIMPLEREGION（调用方普遍只读矩形） */
        lprect->right = lprect->left;
        lprect->bottom = lprect->top;
    }
    return 1;
}
extern "C" int WINAPI GetClipRgn(HDC hdc, HRGN hrgn)
{
    /* GDI 语义：0 = 该 DC 没有裁剪区（hrgn 内容不保证），1 = 有且已拷入 hrgn。
     * cairo 后端的 ExtSelectClipRgn 靠它导出当前裁剪区参与布尔运算。
     * 记账是设备坐标；导出时逆映射回**当前逻辑空间**（真 GDI 的 GetClipRgn
     * 返回当前映射下的逻辑区域），SOsrPanel::Draw 在 OffsetViewportOrg 之后
     * 取裁剪区、拿 item 相对坐标用，坐标系必须跟着视图原点走。 */
    if (!hdc || !hrgn) return -1;
    if (!hdc->hClipRgn) return 0;
    HRGN rgnLog = clipRgnAsLogical(hdc);
    CombineRgn(hrgn, rgnLog, NULL, RGN_COPY);
    if (rgnLog)
        DeleteObject(rgnLog);
    return 1;
}
extern "C" BOOL WINAPI FillRgn(HDC hdc, HRGN hrgn, HBRUSH hbr)
{
    if (!hdc || !hrgn) return FALSE;
    RECT rc; if (!GetRgnBox(hrgn, &rc)) return FALSE;
    HBRUSH old = (HBRUSH)SelectObject(hdc, hbr ? hbr : (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, old);
    return TRUE;
}
extern "C" BOOL WINAPI PaintRgn(HDC hdc, HRGN hrgn) { return FillRgn(hdc, hrgn, hdc->brush); }
extern "C" BOOL WINAPI FrameRgn(HDC hdc, HRGN hrgn, HBRUSH hbr, int, int) { return FillRgn(hdc, hrgn, hbr); }
extern "C" BOOL WINAPI InvertRgn(HDC hdc, HRGN hrgn)
{
    if (!hdc || !hrgn) return FALSE;
    RECT rc; if (!GetRgnBox(hrgn, &rc)) return FALSE;
    return InvertRect(hdc, &rc);
}
extern "C" BOOL WINAPI DrawFocusRect(HDC hDC, CONST RECT *lprc)
{
    if (!hDC || !lprc) return FALSE;
    val &c = hDC->ctx;
    c.set(std::string("strokeStyle"),std::string( "#000000"));
    c.set(std::string("lineWidth"), 1.0);
    c.call<void>("setLineDash", val::array());
    c.call<void>("strokeRect", (double)lprc->left + 0.5, (double)lprc->top + 0.5,
                 (double)(lprc->right - lprc->left) - 1, (double)(lprc->bottom - lprc->top) - 1);
    return TRUE;
}

/* ---------- 文本 ---------- */
static void drawTextLines(HDC hdc, const wchar_t *text, int cch, LPRECT lprc, UINT format, bool measureOnly)
{
    val &c = hdc->ctx;
    applyFont(hdc, c);
    std::wstring ws(text, cch < 0 ? wcslen(text) : (size_t)cch);
    // 按 \n 分行
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t ch : ws)
    {
        if (ch == L'\n') { lines.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
    }
    lines.push_back(cur);
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    /* 行距与 cairo 版 drawtext.cpp 一致：lineSpan = ascent + descent（kLineInterval=0）。 */
    double lineH = fontLineH(f);
    double ascent = fontAscent(f);
    double totalH = lineH * lines.size();
    double maxW = 0;
    for (auto &ln : lines)
    {
        std::string s = toUtf8(ln.c_str());
        double w = measureWidth(hdc, s);
        if (w > maxW) maxW = w;
    }
    if (measureOnly)
    {
        lprc->right = lprc->left + (LONG)ceil(maxW);
        lprc->bottom = lprc->top + (LONG)ceil(totalH);
        return;
    }
    // OPAQUE 背景先填
    if (hdc->bkMode == OPAQUE)
    {
        C2dFill(hdc, hdc->crBk);
        Op(hdc, SWX_FILL_RECT, (double)lprc->left, (double)lprc->top, (double)maxW, (double)totalH);
    }
    int single = (format & DT_SINGLELINE) ? 1 : 0;
    double y = (double)lprc->top;
    if (single)
    {
        if (format & DT_VCENTER) y = lprc->top + ((lprc->bottom - lprc->top) - lineH) / 2;
        else if (format & DT_BOTTOM) y = lprc->bottom - lineH;
    }
    else
    {
        if (format & DT_VCENTER) y = lprc->top + ((lprc->bottom - lprc->top) - totalH) / 2;
    }
    /* 画字前必须把 fillStyle 设回 crText：上面的 OPAQUE 背景填充把它留在了
     * crBk 上 —— richedit 的选中行（OPAQUE 背景 + sel 前景）会因此变成
     * "背景色画背景色的字"（白底白字）。 */
    C2dFill(hdc, hdc->crText);
    for (auto &ln : lines)
    {
        std::string s = toUtf8(ln.c_str());
        double w = measureWidth(hdc, s);
        double x = (double)lprc->left;
        if (format & DT_CENTER) x = lprc->left + ((lprc->right - lprc->left) - w) / 2;
        else if (format & DT_RIGHT) x = lprc->right - w;
        /* y 是「字符格顶」（GDI 语义），基线要再往下加 ascent。 */
        double baseline = y + ascent;
        OpText(hdc, SWX_FILL_TEXT, s, x, baseline);
        if (f && f->underline) { Op(hdc, SWX_FILL_RECT, x, baseline + 1, w, 1); }
        if (f && f->strike) { Op(hdc, SWX_FILL_RECT, x, baseline - ascent * 0.3, w, 1); }
        y += lineH;
        if (single) break;
    }
}

extern "C" int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
    if (!hdc || !lpchText || !lprc) return 0;
    if (format & DT_CALCRECT)
    {
        RECT rc = *lprc;
        drawTextLines(hdc, lpchText, cchText, &rc, format | DT_SINGLELINE, true);
        *lprc = rc;
        return rc.bottom - rc.top;
    }
    drawTextLines(hdc, lpchText, cchText, lprc, format, false);
    return lprc->bottom - lprc->top;
}
extern "C" int WINAPI DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format)
{
    // A 版转 W
    std::wstring w = utf8ToW(lpchText, cchText);
    return DrawTextW(hdc, w.c_str(), (int)w.size(), lprc, format);
}
extern "C" BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
    if (!hdc || !lpString) return FALSE;
    val &cc = hdc->ctx;
    applyFont(hdc, cc);
    std::wstring ws(lpString, c < 0 ? wcslen(lpString) : (size_t)c);
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    /* TA_TOP（默认）时 y 是字符格顶，canvas 用 alphabetic 基线 ⇒ 加 ascent；
     * TA_BASELINE 时 y 本身就是基线。 */
    double baseY = (hdc->textAlign & TA_BASELINE) ? (double)y : (double)y + fontAscent(f);
    if (hdc->bkMode == OPAQUE)
    {
        double w = measureWidth(hdc, toUtf8(ws.c_str()));
        C2dFill(hdc, hdc->crBk);
        Op(hdc, SWX_FILL_RECT, (double)x, (double)y, w, fontLineH(f));
        /* 背景填充把 fillStyle 留在了 crBk，画字前设回 crText（同 ExtTextOutW 注释） */
        C2dFill(hdc, hdc->crText);
    }
    OpText(hdc, SWX_FILL_TEXT, toUtf8(ws.c_str()), (double)x, baseY);
    return TRUE;
}
extern "C" BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c)
{
    std::wstring w = utf8ToW(lpString, c);
    return TextOutW(hdc, x, y, w.c_str(), (int)w.size());
}
extern "C" BOOL WINAPI ExtTextOutW(HDC hdc, int X, int Y, UINT fuOptions, CONST RECT *lprc,
                                   LPCWSTR lpString, UINT cbCount, CONST INT *lpDx)
{
    if (!hdc || !lpString) return FALSE;
    val &cc = hdc->ctx;
    applyFont(hdc, cc);
    int n = (int)(cbCount < 0 ? wcslen(lpString) : cbCount);
    RECT rcClipSaved = hdc->rcClip;
    C2DStateCache c2dSt0; /* ETO_CLIPPED 的 save/restore 快照，供裁剪分支还原缓存 */
    if (fuOptions & ETO_CLIPPED && lprc)
    {
        Op(hdc, SWX_SAVE);
        c2dSt0 = hdc->c2d;
        /* lprc 是设备坐标（GDI 的 ETO_CLIPPED 语义），走设备空间裁剪，别被世界变换缩放 */
        clipRectDevice(hdc, lprc->left, lprc->top, lprc->right, lprc->bottom);
    }
    if (fuOptions & ETO_OPAQUE && lprc)
    {
        /* Win32：ETO_OPAQUE 要求先用当前 BkColor 把 lprc 整块填成不透明背景再画字。
         * Scintilla 的 SurfaceGDI::FillRect 就是用 ExtTextOut(..., ETO_OPAQUE, &rc,
         * "", 0, NULL) 空串填充实现的（PlatWin.cxx "no dithering"），DrawTextNoClip
         * 的行背景也走这里 —— 不实现的话它的行背景/选中高亮/空白区背景全部丢失，
         * 选中时也看不到高亮。 */
        C2dFill(hdc, hdc->crBk);
        Op(hdc, SWX_FILL_RECT, (double)lprc->left, (double)lprc->top,
                      (double)(lprc->right - lprc->left), (double)(lprc->bottom - lprc->top));
    }
    if (hdc->bkMode == OPAQUE && n > 0)
    {
        double w = measureWidth(hdc, toUtf8(std::wstring(lpString, n).c_str()));
        GdiFont *fw = (GdiFont *)GetGdiObjPtr(hdc->hfont);
        C2dFill(hdc, hdc->crBk);
        Op(hdc, SWX_FILL_RECT, (double)X, (double)Y, w, fontLineH(fw));
    }
    /* 无论走没走上面的背景填充，画字时 fillStyle 必须是 crText —— ETO_OPAQUE
     * 的 lprc 填充同样会把它留在 crBk 上（Scintilla 行号因此变成灰底灰字）。 */
    C2dFill(hdc, hdc->crText);
    /* TA_TOP（默认）的 Y 是字符格顶 ⇒ 基线 = Y + ascent（见 TextOutW 注释）。 */
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    double baseY = (hdc->textAlign & TA_BASELINE) ? (double)Y : (double)Y + fontAscent(f);
    if (lpDx)
    {
        // 逐字符按 lpDx 前进
        double x = (double)X;
        for (int i = 0; i < n; ++i)
        {
            std::wstring chs(lpString + i, 1);
            OpText(hdc, SWX_FILL_TEXT, toUtf8(chs.c_str()), x, baseY);
            x += lpDx[i];
        }
    }
    else
    {
        OpText(hdc, SWX_FILL_TEXT, toUtf8(std::wstring(lpString, n).c_str()), (double)X, baseY);
    }
    if (fuOptions & ETO_CLIPPED && lprc)
    {
        Op(hdc, SWX_RESTORE);
        hdc->c2d = c2dSt0;
    }
    /* 复原裁剪记账：canvas 的 clip 已被 restore 撤销，HDC 侧的包围盒也要跟着回去，
     * 否则每一步 ExtTextOut 都把 rcClip 与"本行矩形"求交并且永久生效，几十行之后
     * GetClipBox 只剩最后一行那么高 —— SOUI 据此认定"其余区域已经画过"，编辑器页
     * 切过去就只剩一小块文字（Scintilla 每行一次 ETO_CLIPPED 最容易踩到）。 */
    hdc->rcClip = rcClipSaved;
    return TRUE;
}
extern "C" BOOL WINAPI ExtTextOutA(HDC hdc, int X, int Y, UINT fuOptions, CONST RECT *lprc,
                                   LPCSTR lpString, UINT cbCount, CONST INT *lpDx)
{
    std::wstring w = utf8ToW(lpString, (int)cbCount);
    return ExtTextOutW(hdc, X, Y, fuOptions, lprc, w.c_str(), (UINT)w.size(), lpDx);
}
extern "C" BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
    if (!hdc || !psizl) return FALSE;
    val &cc = hdc->ctx; applyFont(hdc, cc);
    int n = (c < 0) ? (int)wcslen(lpString) : c;
    std::wstring ws(lpString, n);
    double w = measureWidth(hdc, toUtf8(ws.c_str()));
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    psizl->cx = (LONG)ceil(w);
    /* GDI 的 cy == tmHeight == tmAscent + tmDescent（实测字体度量）。SOUI 里
     * wrapContent 的行高全靠这个值，估算会让整页每行偏矮。 */
    psizl->cy = (LONG)ceil(fontLineH(f));
    return TRUE;
}
extern "C" BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl)
{
    std::wstring w = utf8ToW(lpString, c);
    return GetTextExtentPoint32W(hdc, w.c_str(), (int)w.size(), psizl);
}
/* lpnDx 必须填「到第 i 个字符为止的累计宽度」，lpnFit = nMaxExtent 内放得下的
 * 字符数。Scintilla 的 SurfaceGDI::MeasureWidths 全靠这个数组构建 LineLayout 的
 * positions —— 只回总宽度不填 lpnDx 的话，positions 读到的是未初始化内存，
 * 布局宽度随机塌成 0，DrawForeground 里 rcSegment 宽 0 → Intersects 判 false，
 * 文本段被整体跳过（编辑器页只剩两行有字）。与 cairo 后端同口径：单字符宽度
 * 累计（无 kerning），lpSize 仍用整串 measureText 的总宽。 */
extern "C" BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR lpsz, int cch, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
{
    if (!hdc || !lpsz) return FALSE;
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32W(hdc, lpsz, cch, lpSize);
    int wchars = cch < 0 ? (int)wcslen(lpsz) : cch;
    if (wchars > (int)wcslen(lpsz)) wchars = (int)wcslen(lpsz);
    val &cc = hdc->ctx;
    applyFont(hdc, cc);
    int acc = 0;
    int fit = 0;
    for (int w = 0; w < wchars; w++)
    {
        double adv = measureWidth(hdc, toUtf8(std::wstring(lpsz + w, 1).c_str()));
        acc += (int)floor(adv + 0.5);
        if (lpnDx) lpnDx[w] = acc;
        if (acc <= nMaxExtent) fit = w + 1;
    }
    if (lpnFit) *lpnFit = fit;
    if (lpSize) GetTextExtentPoint32W(hdc, lpsz, wchars, lpSize);
    return TRUE;
}
extern "C" BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR lpsz, int cch, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
{
    if (!hdc || !lpsz) return FALSE;
    if (!lpnFit && !lpnDx)
        return GetTextExtentPoint32A(hdc, lpsz, cch, lpSize);
    int n = cch < 0 ? (int)strlen(lpsz) : cch;
    val &cc = hdc->ctx;
    applyFont(hdc, cc);
    int acc = 0;
    int fit = 0;
    for (int i = 0; i < n; i++)
    {
        /* ANSI 单字节字符：逐字节测量累计（GDI 的 lpDx 也按字节索引） */
        std::wstring w = utf8ToW(lpsz + i, 1);
        double adv = measureWidth(hdc, toUtf8(w.c_str()));
        acc += (int)floor(adv + 0.5);
        if (lpnDx) lpnDx[i] = acc;
        if (acc <= nMaxExtent) fit = i + 1;
    }
    if (lpnFit) *lpnFit = fit;
    if (lpSize) GetTextExtentPoint32A(hdc, lpsz, n, lpSize);
    return TRUE;
}
extern "C" BOOL WINAPI GetTextMetricsW(HDC hdc, TEXTMETRICW *txtMetric)
{
    if (!txtMetric) return FALSE;
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    double sz = f ? f->sizePx : 12;
    memset(txtMetric, 0, sizeof(*txtMetric));
    /* 与 GetTextExtentPoint32 同口径：真实字体度量，不是 sizePx*1.2 估算。 */
    txtMetric->tmAscent = (LONG)ceil(fontAscent(f));
    txtMetric->tmDescent = (LONG)ceil(fontDescent(f));
    txtMetric->tmHeight = txtMetric->tmAscent + txtMetric->tmDescent;
    txtMetric->tmInternalLeading = 0;
    txtMetric->tmExternalLeading = 0;
    txtMetric->tmAveCharWidth = (LONG)(sz * 0.5);
    txtMetric->tmMaxCharWidth = (LONG)(sz);
    txtMetric->tmWeight = f ? f->lf.lfWeight : FW_NORMAL;
    return TRUE;
}
extern "C" BOOL WINAPI GetTextMetricsA(HDC hdc, TEXTMETRICA *txtMetric)
{
    if (!txtMetric) return FALSE;
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    double sz = f ? f->sizePx : 12;
    memset(txtMetric, 0, sizeof(*txtMetric));
    txtMetric->tmAscent = (LONG)ceil(fontAscent(f));
    txtMetric->tmDescent = (LONG)ceil(fontDescent(f));
    txtMetric->tmHeight = txtMetric->tmAscent + txtMetric->tmDescent;
    txtMetric->tmAveCharWidth = (LONG)(sz * 0.5);
    txtMetric->tmMaxCharWidth = (LONG)(sz);
    return TRUE;
}
extern "C" BOOL WINAPI GetCharWidthW(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
    if (!hdc || !lpBuffer) return FALSE;
    val &cc = hdc->ctx; applyFont(hdc, cc);
    GdiFont *f = (GdiFont *)GetGdiObjPtr(hdc->hfont);
    double sz = f ? f->sizePx : 12;
    for (UINT c = iFirst; c <= iLast; ++c)
    {
        /* 不能写 toUtf8(&ch)wcslen：ch 是单个栈变量，wcslen 会越过它读后面的
         * 栈内存 —— 大多数字符旁边恰好是 0（宽度正常），"："这类调用踩到垃圾
         * 就返回 269px 的宽度，RE41 把它缓存进 CCcs 后整行排版多出一段大空隙。 */
        wchar_t buf[2] = {(wchar_t)c, 0};
        std::string s = toUtf8(buf);
        lpBuffer[c - iFirst] = (int)ceil(measureWidth(hdc, s));
    }
    return TRUE;
}
extern "C" BOOL WINAPI GetCharWidthA(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
    if (!hdc || !lpBuffer) return FALSE;
    val &cc = hdc->ctx; applyFont(hdc, cc);
    for (UINT c = iFirst; c <= iLast; ++c)
    {
        char ch = (char)c;
        lpBuffer[c - iFirst] = (int)ceil(measureWidth(hdc, std::string(1, ch)));
    }
    return TRUE;
}
extern "C" BOOL WINAPI GetCharWidth32W(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer) { return GetCharWidthW(hdc, iFirst, iLast, lpBuffer); }
extern "C" BOOL WINAPI GetCharWidth32A(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer) { return GetCharWidthA(hdc, iFirst, iLast, lpBuffer); }
extern "C" int WINAPI GetTextFaceW(HDC hdc, int nCount, LPWSTR lpFaceName)
{
    GdiFont *f = hdc ? (GdiFont *)GetGdiObjPtr(hdc->hfont) : nullptr;
    if (lpFaceName && nCount > 0 && f) wcsncpy(lpFaceName, f->lf.lfFaceName, nCount - 1);
    return f ? (int)wcslen(f->lf.lfFaceName) : 0;
}
extern "C" int WINAPI GetTextFaceA(HDC hdc, int nCount, LPSTR lpFaceName)
{
    GdiFont *f = hdc ? (GdiFont *)GetGdiObjPtr(hdc->hfont) : nullptr;
    if (lpFaceName && nCount > 0 && f)
    {
        std::string s = toUtf8(f->lf.lfFaceName);
        strncpy(lpFaceName, s.c_str(), nCount - 1);
        lpFaceName[s.size()] = 0;
    }
    return f ? (int)wcslen(f->lf.lfFaceName) : 0;
}
extern "C" DWORD WINAPI GetTabbedTextExtentW(HDC, LPCWSTR, int, int, CONST LPINT) { return 0; }
extern "C" LONG WINAPI TabbedTextOutW(HDC, int, int, LPCWSTR, int, int, CONST LPINT, int) { return 0; }
extern "C" DWORD WINAPI GetTabbedTextExtentA(HDC, LPCSTR, int, int, CONST LPINT) { return 0; }
extern "C" LONG WINAPI TabbedTextOutA(HDC, int, int, LPCSTR, int, int, CONST LPINT, int) { return 0; }
extern "C" UINT WINAPI SetTextAlign(HDC hdc, UINT align) { if (!hdc) return 0; UINT old = hdc->textAlign; hdc->textAlign = align; return old; }
extern "C" UINT WINAPI GetTextAlign(HDC hdc) { return hdc ? hdc->textAlign : 0; }
extern "C" COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color) { if (!hdc) return 0; COLORREF o = hdc->crText; hdc->crText = color; return o; }
extern "C" COLORREF WINAPI GetTextColor(HDC hdc) { return hdc ? hdc->crText : 0; }
extern "C" int WINAPI SetBkMode(HDC hdc, int mode) { if (!hdc) return 0; int o = hdc->bkMode; hdc->bkMode = mode; return o; }
extern "C" int WINAPI GetBkMode(HDC hdc) { return hdc ? hdc->bkMode : 0; }
extern "C" COLORREF WINAPI SetBkColor(HDC hdc, COLORREF cr) { if (!hdc) return 0; COLORREF o = hdc->crBk; hdc->crBk = cr; return o; }
extern "C" COLORREF WINAPI GetBkColor(HDC hdc) { return hdc ? hdc->crBk : 0; }
extern "C" int WINAPI SetROP2(HDC hdc, int rop2) { if (!hdc) return 0; int o = hdc->rop2; hdc->rop2 = rop2; return o; }
extern "C" int WINAPI SetPolyFillMode(HDC hdc, int mode) { if (!hdc) return 0; int o = hdc->polyFillMode; hdc->polyFillMode = mode; return o; }
extern "C" int WINAPI SetStretchBltMode(HDC hdc, int mode) { if (!hdc) return 0; hdc->stretchBltMode = mode; return mode; }
extern "C" Antialias WINAPI SetAntialiasMode(HDC hdc, Antialias) { return ANTIALIAS_DEFAULT; }
extern "C" Antialias WINAPI GetAntialiasMode(HDC) { return ANTIALIAS_DEFAULT; }
extern "C" BOOL WINAPI DrawEdge(HDC hdc, LPRECT rc, UINT, UINT) { return Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom); }

/* ---------- DIB / 位图创建 ---------- */
/* 把 WxH 的 ARGB32 位图整幅写进 surface 的 canvas（1:1，putImageData）。
 * 语义是「整幅替换」：先 clearRect 再写（QR 等调用方写完就整体重传）。 */
static void surfacePutBits(Canvas2DSurface *s, const void *bits, int w, int h)
{
    if (!s || !bits || w <= 0 || h <= 0) return;
    SwinxCanvasPutPixels(s->canvas, (int)(intptr_t)bits, w, h);
}

/* 反向：把 surface 的 canvas 读回 WxH 的 ARGB32（1:1）。
 * LockPixelBits/GetDIBits 的调用方按 surface 尺寸寻址。 */
static void surfaceGetBits(Canvas2DSurface *s, uint32_t *dst, int stride)
{
    if (!s) return;
    SwinxCanvasReadPixels(s->canvas, 0, 0, s->width, s->height, dst, stride);
}

extern "C" HBITMAP WINAPI CreateBitmap(int nWidth, int nHeight, UINT, UINT, CONST VOID *lpvBits)
{
    HBITMAP b = SwinxCanvasCreateBitmap(nWidth, nHeight, 1);
    if (lpvBits) UpdateDIBPixmap(b, nWidth, nHeight, 32, nWidth * 4, lpvBits);
    return b;
}
extern "C" HBITMAP WINAPI CreateCompatibleBitmap(HDC, int cx, int cy)
{
    return SwinxCanvasCreateBitmap(cx, cy, 1);
}
/* CreateDIBitmap: 从调用方给的 DIB 像素建位图。resprovider-zip 解包主题包里的
 * 位图走的就是这条路（SResProviderZip.cpp）。canvas2d 后端只认 24/32bpp 真彩色，
 * 调色板格式（1/4/8bpp）直接失败——与 UpdateDIBPixmap 的口径一致，不静默转换。
 * biHeight > 0 是 bottom-up（末行在前），要逐行倒序；< 0 是 top-down，原样贴。 */
extern "C" HBITMAP WINAPI CreateDIBitmap(HDC, CONST BITMAPINFOHEADER *pbmih, DWORD flInit,
                                         CONST VOID *pjBits, CONST BITMAPINFO *pbmi, UINT iUsage)
{
    (void)pbmih;
    if (iUsage != DIB_RGB_COLORS || !pbmi)
        return nullptr;

    const int width = (int)pbmi->bmiHeader.biWidth;
    const int height = (int)pbmi->bmiHeader.biHeight;
    const int absHeight = height < 0 ? -height : height;
    const int bits = (int)pbmi->bmiHeader.biBitCount;
    if (width <= 0 || absHeight <= 0 || bits <= 0)
        return nullptr;

    HBITMAP bmp = SwinxCanvasCreateBitmap(width, absHeight, 1);
    if (!bmp)
        return nullptr;
    if ((flInit & CBM_INIT) == 0 || pjBits == nullptr)
        return bmp; /* 只建空位图 */

    /* DIB 行按 4 字节对齐，与 UpdateDIBPixmap 的 stride 同口径。 */
    const int stride = ((width * bits + 31) / 32) * 4;
    if (height < 0)
    {
        if (!UpdateDIBPixmap(bmp, width, absHeight, bits, stride, pjBits))
        {
            DeleteObject(bmp);
            return nullptr;
        }
        return bmp;
    }

    uint8_t *flip = new uint8_t[(size_t)stride * absHeight];
    for (int y = 0; y < absHeight; ++y)
        memcpy(flip + (size_t)y * stride,
               (const uint8_t *)pjBits + (size_t)(absHeight - 1 - y) * stride, (size_t)stride);
    BOOL ok = UpdateDIBPixmap(bmp, width, absHeight, bits, stride, flip);
    delete[] flip;
    if (!ok)
    {
        DeleteObject(bmp);
        return nullptr;
    }
    return bmp;
}
extern "C" HBITMAP WINAPI CreateDIBSection(HDC, CONST BITMAPINFO *lpbmi, UINT, VOID **ppvBits, HANDLE, DWORD)
{
    int w = lpbmi ? (int)lpbmi->bmiHeader.biWidth : 1;
    int h = lpbmi ? (int)abs(lpbmi->bmiHeader.biHeight) : 1;
    HBITMAP b = SwinxCanvasCreateBitmap(w, h, 1);
    if (ppvBits)
    {
        Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(b);
        s->bits = new uint8_t[(size_t)w * h * 4];
        *ppvBits = s->bits;
    }
    return b;
}
extern "C" HBITMAP WINAPI CreateDIBSectionEx(int bitsPixel, int wid, int hei, int stride, VOID *pvBits)
{
    HBITMAP b = SwinxCanvasCreateBitmap(wid, hei, 1);
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(b);
    s->bitsPixel = bitsPixel;
    s->stride = stride;
    if (pvBits)
    {
        s->bits = new uint8_t[(size_t)stride * hei];
        memcpy(s->bits, pvBits, (size_t)stride * hei);
        surfacePutBits(s, s->bits, wid, hei);
    }
    return b;
}
extern "C" BOOL WINAPI UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits)
{
    if (!bmp) return FALSE;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(bmp);
    if (!s) return FALSE;
    /* pjBits 为空表示"只建位图、不填内容"：SBitmap_GDI::Init(w, h, NULL) 就是这条路，
     * 而它把返回值当作"位图尺寸有效"的依据。cairo 参考实现同样在 pjBits 为空时返回
     * TRUE。第一版在这里返回 FALSE，导致所有 Init(w,h,NULL) 的位图尺寸停在 0x0 ——
     * 二维码控件因此把图当成空图，画出来是一个白方块。 */
    if (!pjBits)
        return TRUE;
    if (bitsPixel == 32 && stride == wid * 4)
        surfacePutBits(s, pjBits, wid, hei);
    else
    {
        /* 任意行距 / 非 32 位：整幅转成 32bpp BGRA 缓冲再一次贴上（HiDPI 下
         * putImageData 不能缩放，必须走 surfacePutBits 的临时画布路径）。 */
        int bpp = bitsPixel / 8;
        if (bpp < 3)
            return FALSE; // 调色板/1 位等格式 SOUI 不用，不做静默转换
        uint8_t *full = new uint8_t[(size_t)wid * hei * 4];
        for (int y = 0; y < hei; ++y)
        {
            const uint8_t *src = (const uint8_t *)pjBits + (size_t)y * stride;
            uint8_t *row = full + (size_t)y * wid * 4;
            for (int x = 0; x < wid; ++x)
            {
                /* 保持 BGRA 字节序（SwinxCanvasPutPixels 负责换成 ImageData 的 RGBA） */
                row[x * 4 + 0] = src[x * bpp + 0]; // B
                row[x * 4 + 1] = src[x * bpp + 1]; // G
                row[x * 4 + 2] = src[x * bpp + 2]; // R
                row[x * 4 + 3] = (bpp == 4) ? src[x * bpp + 3] : 0xFF;
            }
        }
        surfacePutBits(s, full, wid, hei);
        delete[] full;
    }
    return TRUE;
}
/* LockPixelBits/UnlockPixelBits 这条路径是「直接改 CPU 缓冲」的：SBitmap_GDI 拿到
 * bmBits 改完像素后调 UnlockPixelBits → MarkPixmapDirty。空实现等于什么都不发生，
 * canvas 里永远是旧内容 —— SQrCtrl 逐模块写二维码、写完再读回来合成，于是整块空白。 */
extern "C" void WINAPI MarkPixmapDirty(HBITMAP bmp)
{
    if (!bmp) return;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(bmp);
    if (!s || !s->bits) return;
    surfacePutBits(s, s->bits, s->width, s->height);
}

/* 反向同步：位图的权威内容在 canvas 里（绘制都落在 canvas 上），而 LockPixelBits
 * 返回的是 CPU 缓冲。谁要拿这个指针，就得先把 canvas 读回 bits，否则读到的是上一次
 * 上传时的旧像素。 */
static void syncSurfaceBits(Canvas2DSurface *s)
{
    if (!s || !s->bits) return;
    surfaceGetBits(s, (uint32_t *)(intptr_t)s->bits, s->width);
}
extern "C" int WINAPI GetDIBits(HDC, HBITMAP hbm, UINT, UINT, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage)
{
    if (!hbm) return 0;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(hbm);
    if (!s) return 0;
    if (lpbmi)
    {
        memset(&lpbmi->bmiHeader, 0, sizeof(lpbmi->bmiHeader));
        lpbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        lpbmi->bmiHeader.biWidth = s->width;
        lpbmi->bmiHeader.biHeight = s->height;
        lpbmi->bmiHeader.biPlanes = 1;
        lpbmi->bmiHeader.biBitCount = 32;
        lpbmi->bmiHeader.biCompression = BI_RGB;
    }
    /* lpvBits 为空表示"只问格式/尺寸"，GDI 允许这么调（很多代码先问一遍再分配） */
    if (!lpvBits) return (int)s->height;
    // 读回像素（getImageData → ARGB32），逻辑尺寸（HiDPI 下先经临时画布缩回）
    int stride = s->width * 4;
    uint32_t *dst = (uint32_t *)lpvBits;
    surfaceGetBits(s, (uint32_t *)(intptr_t)dst, s->width);
    return (int)s->height;
}

/* ---------- 坐标 / 设备能力 / 杂项 ---------- */
extern "C" int WINAPI SetMapMode(HDC hdc, int iMode) { return MM_TEXT; }
extern "C" int WINAPI GetMapMode(HDC) { return MM_TEXT; }

/* 把 HDC 的完整变换落到 canvas 上：逻辑坐标 → 世界变换 → +视图原点（-窗口原点）。
 *
 * 为什么用绝对 setTransform 而不是相对 translate：setTransform 每次都能把变换
 * 拉回唯一正确值，不会因为中途 canvas 尺寸变化（改 width/height 会把上下文状态
 * 重置成单位矩阵）或某次 save/restore 不配对而累积漂移。
 *
 * 与 save/restore 的一致性：ctx.save()/restore() 会连带保存/恢复变换，而
 * HDC__::SaveState/RestoreState 也同步存取 ptViewport/ptWinOrg/xfWorld。
 *
 * 背景：以前这些函数只把值写进 hdc->ptOrigin（那是 MoveToEx/LineTo 的当前点），
 * 变换从未被应用，于是 STreeCtrl::DrawItem / SOsrPanel::Draw 里的
 * OffsetViewportOrg 全是空操作 —— 树控件的每一行文字都画在窗口左上角同一处。
 * 世界变换同理：render-gdi 的 DrawSVG 靠 SetWorldTransform 把 SVG 原始坐标缩放到
 * 目标矩形，空实现时 src="svg:..." 的皮肤全画在原始坐标上（Lua 游戏页整页空白）。 */
static void applyCtxTransform(HDC__ *hdc)
{
    const XFORM &w = hdc->xfWorld;
    /* GDI 顺序是「先世界变换，再减窗口原点、加视图原点」：
     *   device = W * (logical - winOrg) + viewport
     * 展开成 canvas 的 a/b/c/d/e/f：e/f 里含 -W*winOrg。
     * 坐标 1:1（DPI 缩放由 SOUI 布局层完成，见文件头的说明）。 */
    double ox = (double)hdc->ptViewport.x - ((double)hdc->ptWinOrg.x * w.eM11 + (double)hdc->ptWinOrg.y * w.eM21);
    double oy = (double)hdc->ptViewport.y - ((double)hdc->ptWinOrg.x * w.eM12 + (double)hdc->ptWinOrg.y * w.eM22);
    double m[6] = {(double)w.eM11, (double)w.eM12, (double)w.eM21, (double)w.eM22,
                   (double)w.eDx + ox, (double)w.eDy + oy};
    /* 矩阵没变就不下发：STreeCtrl/SOsrPanel 每画一行都 OffsetViewportOrg 定位内容，
     * 画完再减回去，一来一回两次 setTransform；同值重复调是纯开销。 */
    if (hdc->c2d.xfValid)
    {
        bool same = true;
        for (int i = 0; i < 6; ++i)
        {
            if (hdc->c2d.xf[i] != m[i])
            {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }
    SwxXform(hdc->ctxId, m[0], m[1], m[2], m[3], m[4], m[5]);
    for (int i = 0; i < 6; ++i)
        hdc->c2d.xf[i] = m[i];
    hdc->c2d.xfValid = TRUE;
}

/* 逻辑矩形 → 设备坐标包围盒（裁剪记账用；与 srcRectDevice 同一套换算）。 */
static void logicRectToDevice(HDC__ *hdc, int l, int t, int r, int b, RECT &out)
{
    double sx, sy, sw, sh;
    srcRectDevice((HDC)hdc, l, t, r - l, b - t, sx, sy, sw, sh);
    out.left = (LONG)floor(sx);
    out.top = (LONG)floor(sy);
    out.right = (LONG)ceil(sx + sw);
    out.bottom = (LONG)ceil(sy + sh);
}

/* 把一块坐标矩形并入 HDC 的裁剪记账（GetClipBox 的返回值）。
 * 坐标 1:1，直接求交。canvas 的 clip 自己会与已有 clip 求交，这里只是把同一件
 * 事在 HDC 侧记一遍。 */
static void clipBoxIntersect(HDC__ *hdc, const RECT &rcDev)
{
    RECT rc = hdc->rcClip;
    if (rcDev.left > rc.left)
        rc.left = rcDev.left;
    if (rcDev.top > rc.top)
        rc.top = rcDev.top;
    if (rcDev.right < rc.right)
        rc.right = rcDev.right;
    if (rcDev.bottom < rc.bottom)
        rc.bottom = rcDev.bottom;
    if (rc.left >= rc.right || rc.top >= rc.bottom)
        rc.left = rc.top = rc.right = rc.bottom = 0;
    hdc->rcClip = rc;
}

/* 矩形裁剪：参数是**逻辑坐标**（Win32 的 IntersectClipRect/ExcludeClipRect 都是
 * 逻辑单位，经过映射模式换算），所以直接在当前变换下建路径再 clip 即可 ——
 * canvas 的 clip 一旦建立就固化在设备空间，之后改变换也不会影响它，正好等价于
 * GDI「裁剪是设备状态、建的时候按当时的映射模式换算」。
 *
 * 以前这里先把变换清成单位矩阵、把逻辑值当设备值用，且靠它来"保证"外层控件的
 * 裁剪框不被 SVG 的世界变换缩放。但分层窗口的 RT 带 OffsetViewportOrg(-rcWnd.left,
 * -rcWnd.top)，逻辑坐标是绝对坐标：992x52 的 RT 上裁 (552,300)-(744,352) 完全在
 * 图外，内容被裁得一点不剩（属性动画里 prop_box 只剩 ClearRect 的黑底）。 */
static void clipRectDevice(HDC__ *hdc, int l, int t, int r, int b)
{
    /* 与上一次应用到 canvas 的裁剪矩形比较：**完全相同**就跳过。SOUI 的绘制
     * （SWindow::OnPaint → PushClipRect → 绘制 → PopClip）会让同一块区域在一帧内
     * 被反复 Push：父子窗口区域相同、控件内部再交一次同一个框都是常态。裁剪是
     * 累积求交的，重复交同一个矩形结果不变，跳过不影响语义。
     * 只比"完全相同"而不做包含判断：canvas 的当前裁剪可能由更早的多个 clip 累积
     * 而成，单看最后一个矩形判定"包含"会把实际更小的裁剪区当成没变化。 */
    if (hdc->c2d.clipValid)
    {
        const RECT &cur = hdc->c2d.clipRect;
        if (cur.left == l && cur.top == t && cur.right == r && cur.bottom == b)
            return;
    }
    /* beginPath+rect+clip 合成一次跨语言调用（一帧 170 次，见派发器注释） */
    Op(hdc, SWX_CLIP_RECT, (double)l, (double)t, (double)r, (double)b);
    RECT rcDev;
    logicRectToDevice(hdc, l, t, r, b, rcDev);
    clipBoxIntersect(hdc, rcDev);
    hdc->c2d.clipValid = TRUE;
    hdc->c2d.clipRect.left = l;
    hdc->c2d.clipRect.top = t;
    hdc->c2d.clipRect.right = r;
    hdc->c2d.clipRect.bottom = b;
}

extern "C" BOOL WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    if (!hdc) return FALSE;
    if (lppt) *lppt = hdc->ptViewport;
    hdc->ptViewport.x = x;
    hdc->ptViewport.y = y;
    applyCtxTransform(hdc);
    return TRUE;
}
extern "C" BOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT lpPoint)
{
    if (!hdc || !lpPoint) return FALSE;
    *lpPoint = hdc->ptViewport;
    return TRUE;
}
extern "C" BOOL WINAPI OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt)
{
    if (!hdc) return FALSE;
    if (lppt) *lppt = hdc->ptViewport;
    hdc->ptViewport.x += x;
    hdc->ptViewport.y += y;
    applyCtxTransform(hdc);
    return TRUE;
}
/* SetWindowOrgEx 在 GDI 里是「逻辑坐标 - 窗口原点」再映射到设备坐标，与视图原点
 * 方向相反（设备 = 逻辑 - 窗原点 + 视图原点）。SOUI 主渲染路径不用它，只有少数
 * 位图搬运路径会清零，这里按同一套公式落到变换上，避免又成为死状态。 */
extern "C" BOOL WINAPI SetWindowOrgEx(HDC hdc, int X, int Y, LPPOINT lpPoint)
{
    if (!hdc) return FALSE;
    if (lpPoint) { lpPoint->x = hdc->ptWinOrg.x; lpPoint->y = hdc->ptWinOrg.y; }
    hdc->ptWinOrg.x = X;
    hdc->ptWinOrg.y = Y;
    applyCtxTransform(hdc);
    return TRUE;
}
extern "C" BOOL WINAPI SetWindowExtEx(HDC hdc, int, int, LPSIZE) { return TRUE; }
extern "C" BOOL WINAPI DPtoLP(HDC, LPPOINT, int) { return TRUE; }
extern "C" BOOL WINAPI LPtoDP(HDC, LPPOINT, int) { return TRUE; }
extern "C" int WINAPI GetDeviceCaps(HDC hdc, int cap)
{
    if (!hdc) return 0;
    switch (cap)
    {
    case HORZRES: return hdc->surfW;
    case VERTRES: return hdc->surfH;
    case DESKTOPHORZRES: return hdc->surfW;
    case DESKTOPVERTRES: return hdc->surfH;
    case BITSPIXEL: return 32;
    case LOGPIXELSX: return 96;
    case LOGPIXELSY: return 96;
    case PLANES: return 1;
    default: return 0;
    }
}
extern "C" int WINAPI SetGraphicsMode(HDC hdc, int iMode) { if (!hdc) return 0; int o = hdc->graphicsMode; hdc->graphicsMode = iMode; return o; }
extern "C" int WINAPI GetGraphicsMode(HDC hdc) { return hdc ? hdc->graphicsMode : 0; }
extern "C" BOOL WINAPI SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt) { if (!hdc) return FALSE; if (lppt) *lppt = hdc->brushOrg; hdc->brushOrg.x = x; hdc->brushOrg.y = y; return TRUE; }
extern "C" BOOL WINAPI GetBrushOrgEx(HDC hdc, LPPOINT lppt) { if (!hdc || !lppt) return FALSE; *lppt = hdc->brushOrg; return TRUE; }
extern "C" BOOL WINAPI GdiFlush(void) { return TRUE; }
extern "C" int WINAPI ExtEscape(HDC, int nEscape, int, LPCSTR, int, LPSTR) { return (nEscape == QUERYESCSUPPORT) ? 1 : 0; }
extern "C" COLORREF WINAPI GetPixel(HDC hdc, int x, int y)
{
    if (!hdc) return 0;
    // 读单个像素（getImageData 1x1）；坐标与 canvas 像素一一对应（1:1）
    val img = hdc->ctx.call<val>("getImageData", (double)x, (double)y, 1.0, 1.0);
    val d = img["data"];
    uint8_t r = d[0].as<int>(), g = d[1].as<int>(), b = d[2].as<int>();
    return (COLORREF)((b << 16) | (g << 8) | r);
}
extern "C" COLORREF WINAPI SetPixel(HDC hdc, int x, int y, COLORREF color)
{
    if (!hdc) return 0;
    val &c = hdc->ctx;
    C2dFill(hdc, color);
    c.call<void>("fillRect", (double)x, (double)y, 1.0, 1.0);
    return color;
}
extern "C" COLORREF WINAPI GetNearestColor(HDC, COLORREF cr) { return cr; }
extern "C" HDC WINAPI CreateICW(LPCWSTR, LPCWSTR, LPCWSTR, CONST void *) { return (HDC) new HDC__(NULL); }
extern "C" HDC WINAPI CreateICA(LPCSTR, LPCSTR, LPCSTR, CONST void *) { return (HDC) new HDC__(NULL); }
extern "C" HPALETTE WINAPI SelectPalette(HDC, HPALETTE, BOOL) { return 0; }
extern "C" UINT WINAPI RealizePalette(HDC) { return 0; }
/* ---------- 世界变换 ---------- */
/* XFORM 用行向量约定：(x,y,1) * [eM11 eM12 0; eM21 eM22 0; eDx eDy 1]。
 * 两个变换组合「先 a 后 b」= a * b： */
static XFORM xfMul(const XFORM &a, const XFORM &b)
{
    XFORM r;
    r.eM11 = a.eM11 * b.eM11 + a.eM12 * b.eM21;
    r.eM12 = a.eM11 * b.eM12 + a.eM12 * b.eM22;
    r.eM21 = a.eM21 * b.eM11 + a.eM22 * b.eM21;
    r.eM22 = a.eM21 * b.eM12 + a.eM22 * b.eM22;
    r.eDx = a.eDx * b.eM11 + a.eDy * b.eM21 + b.eDx;
    r.eDy = a.eDx * b.eM12 + a.eDy * b.eM22 + b.eDy;
    return r;
}
static void xfIdentity(XFORM &m)
{
    m.eM11 = m.eM22 = 1.0f;
    m.eM12 = m.eM21 = 0.0f;
    m.eDx = m.eDy = 0.0f;
}
extern "C" BOOL WINAPI SetWorldTransform(HDC hdc, CONST XFORM *lpxf)
{
    if (!hdc || !lpxf) return FALSE;
    hdc->xfWorld = *lpxf;
    applyCtxTransform(hdc);
    return TRUE;
}
extern "C" BOOL WINAPI GetWorldTransform(HDC hdc, LPXFORM lpxf)
{
    if (!hdc || !lpxf) return FALSE;
    *lpxf = hdc->xfWorld;
    return TRUE;
}
/* MWT_LEFTMULTIPLY：新变换先生效（= 新 * 旧）；MWT_RIGHTMULTIPLY：旧先生效（= 旧 * 新）；
 * MWT_IDENTITY：清成单位矩阵。render-gdi 只用 Set/Get，这里按文档补全以免又成空实现。 */
extern "C" BOOL WINAPI ModifyWorldTransform(HDC hdc, CONST XFORM *lpxf, DWORD mode)
{
    if (!hdc) return FALSE;
    if (mode == MWT_IDENTITY || !lpxf)
        xfIdentity(hdc->xfWorld);
    else if (mode == MWT_LEFTMULTIPLY)
        hdc->xfWorld = xfMul(*lpxf, hdc->xfWorld);
    else if (mode == MWT_RIGHTMULTIPLY)
        hdc->xfWorld = xfMul(hdc->xfWorld, *lpxf);
    else
        return FALSE;
    applyCtxTransform(hdc);
    return TRUE;
}

extern "C" BOOL WINAPI SetMiterLimit(HDC hdc, FLOAT e, PFLOAT) { if (hdc) hdc->miterLimit = e; return TRUE; }
extern "C" BOOL WINAPI GetMiterLimit(HDC hdc, PFLOAT pe) { if (hdc && pe) *pe = hdc->miterLimit; return TRUE; }
/* GetPath：把当前记录的路径按 GDI 形式（POINT + PT_* 类型）导出。
 * 语义：count==0 或 pointers 为空时只返回点数；否则返回实际拷贝的点数（不够就拷贝
 * count 个）；失败返回 -1。
 *
 * 谁在用：components/render-gdi 的 SVG 渲染。渐变填充的 shape 要先 GetPathBounds
 * 量出包围盒才能算渐变起止点/圆心，而它第一步就是 GetPath(hdc,NULL,NULL,0)——
 * 这个返回 0 的话 GetPathBounds 直接 FALSE，渐变那条 fill 分支被 `continue` 跳过。
 * 症状很有辨识度：**只有用渐变色填充的图形整块消失**，同一条 SVG 里纯色填的
 * 高光/圆点/描边都正常（Lua 游戏页的棋子只剩几个小碎片、七段数字只剩底框）。 */
extern "C" int WINAPI GetPath(HDC hdc, LPPOINT pPoints, LPBYTE pTypes, int nCount)
{
    if (!hdc) return -1;
    /* 先折算成 GDI 的点序列：一个 PT_BEZIERTO 记录展开成 GDI 的 3 个控制/终点。 */
    std::vector<POINT> pts;
    std::vector<BYTE> types;
    pts.reserve(hdc->recordedPath.size());
    types.reserve(hdc->recordedPath.size());
    POINT start = {0, 0}, cur = {0, 0};
    for (auto &op : hdc->recordedPath)
    {
        switch (op.type)
        {
        case PT_MOVETO:
            pts.push_back({(LONG)op.x, (LONG)op.y});
            types.push_back(PT_MOVETO);
            start = cur = {(LONG)op.x, (LONG)op.y};
            break;
        case PT_LINETO:
            pts.push_back({(LONG)op.x, (LONG)op.y});
            types.push_back(PT_LINETO);
            cur = {(LONG)op.x, (LONG)op.y};
            break;
        case PT_BEZIERTO:
            pts.push_back({(LONG)op.cx1, (LONG)op.cy1});
            types.push_back(PT_BEZIERTO);
            pts.push_back({(LONG)op.cx2, (LONG)op.cy2});
            types.push_back(PT_BEZIERTO);
            pts.push_back({(LONG)op.x, (LONG)op.y});
            types.push_back(PT_BEZIERTO);
            cur = {(LONG)op.x, (LONG)op.y};
            break;
        case PT_ARC_OP:
        { /* 圆弧：GDI 的 Arc 在路径里会被摊成折线，这里按 3 段折线近似（够算包围盒）。 */
            double rx = op.cx1, ry = op.cy1, a0 = op.cx2, a1 = op.cy2;
            for (int k = 1; k <= 3; k++)
            {
                double a = a0 + (a1 - a0) * k / 3.0;
                pts.push_back({(LONG)(op.x + rx * cos(a)), (LONG)(op.y + ry * sin(a))});
                types.push_back(k == 1 && pts.size() == 1 ? PT_MOVETO : PT_LINETO);
            }
            cur = pts.back();
            break;
        }
        case PT_CLOSEFIGURE:
            pts.push_back(start);
            types.push_back(PT_CLOSEFIGURE);
            cur = start;
            break;
        }
    }
    int n = (int)pts.size();
    if (n == 0)
        return 0;
    if (!pPoints || !pTypes || nCount <= 0)
        return n;
    int c = (nCount < n) ? nCount : n;
    for (int i = 0; i < c; i++)
    {
        pPoints[i] = pts[i];
        pTypes[i] = types[i];
    }
    return c;
}

/* ---------- 路径 ---------- */
extern "C" BOOL WINAPI BeginPath(HDC hdc)
{
    if (!hdc) return FALSE;
    hdc->pathRecording = TRUE;
    hdc->recordedPath.clear();
    hdc->pathHasPos = FALSE;
    hdc->ptOrigin.x = hdc->ptOrigin.y = 0;
    return TRUE;
}
extern "C" BOOL WINAPI EndPath(HDC hdc) { if (!hdc) return FALSE; hdc->pathRecording = FALSE; return TRUE; }
extern "C" BOOL WINAPI AbortPath(HDC hdc) { if (!hdc) return FALSE; hdc->pathRecording = FALSE; hdc->recordedPath.clear(); hdc->pathHasPos = FALSE; return TRUE; }
extern "C" BOOL WINAPI CloseFigure(HDC hdc) { if (!hdc) return FALSE; hdc->recordedPath.push_back({PT_CLOSEFIGURE, 0, 0, 0, 0, 0, 0}); return TRUE; }

/* 把记录的路径放进 canvas 的当前路径。canvas 没有「查询当前路径」的接口，只能
 * 自己记账再重放；FillPath/StrokePath/SelectClipPath 都走这里。 */
static void buildRecordedPath(HDC hdc)
{
    val &c = hdc->ctx;
    Op(hdc, SWX_BEGIN_PATH);
    bool hasPos = false;
    double curx = 0, cury = 0;
    for (auto &op : hdc->recordedPath)
    {
        switch (op.type)
        {
        case PT_MOVETO:
            Op(hdc, SWX_MOVE_TO, op.x, op.y);
            curx = op.x; cury = op.y; hasPos = true;
            break;
        case PT_LINETO:
            /* 没有前导 moveTo 时 canvas 会从上一个子路径连过来，必须补一个 moveTo */
            if (!hasPos) { Op(hdc, SWX_MOVE_TO, op.x, op.y); hasPos = true; }
            else Op(hdc, SWX_LINE_TO, op.x, op.y);
            curx = op.x; cury = op.y;
            break;
        case PT_BEZIERTO:
            if (!hasPos) { Op(hdc, SWX_MOVE_TO, curx, cury); hasPos = true; }
            Op(hdc, SWX_BEZIER, op.cx1, op.cy1, op.cx2, op.cy2, op.x, op.y);
            curx = op.x; cury = op.y;
            break;
        case PT_ARC_OP:
            emitArc(c, op.x, op.y, op.cx1, op.cy1, op.cx2, op.cy2);
            hasPos = true;
            break;
        case PT_CLOSEFIGURE:
            Op(hdc, SWX_CLOSE_PATH);
            break;
        }
    }
}

/* 记录路径的包围盒：渐变画刷要一个矩形来建立渐变坐标系（第一版把 w/h 传 0，
 * 渐变退化成一个点，弧形进度条的渐变就全成了单色）。 */
static void recordedPathBounds(HDC hdc, double *minx, double *miny, double *maxx, double *maxy)
{
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (auto &op : hdc->recordedPath)
    {
        switch (op.type)
        {
        case PT_BEZIERTO:
            x0 = std::min(x0, std::min(op.cx1, std::min(op.cx2, op.x)));
            y0 = std::min(y0, std::min(op.cy1, std::min(op.cy2, op.y)));
            x1 = std::max(x1, std::max(op.cx1, std::max(op.cx2, op.x)));
            y1 = std::max(y1, std::max(op.cy1, std::max(op.cy2, op.y)));
            break;
        case PT_ARC_OP:
            x0 = std::min(x0, op.x - fabs(op.cx1));
            y0 = std::min(y0, op.y - fabs(op.cy1));
            x1 = std::max(x1, op.x + fabs(op.cx1));
            y1 = std::max(y1, op.y + fabs(op.cy1));
            break;
        case PT_CLOSEFIGURE:
            break;
        default:
            x0 = std::min(x0, op.x); y0 = std::min(y0, op.y);
            x1 = std::max(x1, op.x); y1 = std::max(y1, op.y);
            break;
        }
    }
    if (x0 > x1) { x0 = y0 = 0; x1 = y1 = 0; }
    *minx = x0; *miny = y0; *maxx = x1; *maxy = y1;
}

/* 填充规则跟 GDI 对齐：默认 ALTERNATE = even-odd（cairo 参考实现也是
 * polyFillMode == ALTERNATE 就用 CAIRO_FILL_RULE_EVEN_ODD）。 */
static void replayPath(HDC hdc, bool stroke, bool fill)
{
    val &c = hdc->ctx;
    buildRecordedPath(hdc);
    if (hdc->recordedPath.empty())
    {
        if (stroke && applyPen(hdc, c)) c.call<void>("stroke");
        return;
    }
    double x0, y0, x1, y1;
    recordedPathBounds(hdc, &x0, &y0, &x1, &y1);
    if (fill)
    {
        if (applyBrush(hdc, c, x0, y0, x1 - x0, y1 - y0))
            c.call<void>("fill", std::string(hdc->polyFillMode == ALTERNATE ? "evenodd" : "nonzero"));
    }
    /* 渐变画笔要知道包围盒才能在正确位置生成扫掠渐变（仪表盘的彩色圆弧） */
    if (stroke && applyPen(hdc, c, x0, y0, x1 - x0, y1 - y0))
        c.call<void>("stroke");
}
extern "C" BOOL WINAPI StrokePath(HDC hdc) { if (!hdc) return FALSE; replayPath(hdc, true, false); return TRUE; }
extern "C" BOOL WINAPI FillPath(HDC hdc) { if (!hdc) return FALSE; replayPath(hdc, false, true); return TRUE; }
extern "C" BOOL WINAPI StrokeAndFillPath(HDC hdc) { if (!hdc) return FALSE; replayPath(hdc, true, true); return TRUE; }
extern "C" HRGN WINAPI PathToRegion(HDC hdc)
{
    if (!hdc) return NULL;
    // 近似：用记录路径的包围盒建矩形区域
    if (hdc->recordedPath.empty()) return CreateRectRgn(0, 0, 0, 0);
    /* 只需包围盒：圆弧的半径也得算进去，否则只取弧端点会得到一条零面积的细条 */
    double minx, miny, maxx, maxy;
    recordedPathBounds(hdc, &minx, &miny, &maxx, &maxy);
    return CreateRectRgn((int)minx, (int)miny, (int)ceil(maxx), (int)ceil(maxy));
}
extern "C" BOOL WINAPI SelectClipPath(HDC hdc, int)
{
    if (!hdc) return FALSE;
    val &c = hdc->ctx;
    buildRecordedPath(hdc);
    c.call<void>("clip", std::string(hdc->polyFillMode == ALTERNATE ? "evenodd" : "nonzero"));
    return TRUE;
}
extern "C" BOOL WINAPI WidenPath(HDC) { return FALSE; }

/* ---------- 图标 / 位图加载（近似：图标走位图，字体资源忽略）---------- */
extern "C" HANDLE WINAPI LoadImageW(HINSTANCE, LPCWSTR, UINT type, INT, INT, UINT) { return NULL; }
extern "C" HANDLE WINAPI LoadImageA(HINSTANCE, LPCSTR, UINT type, INT, INT, UINT) { return NULL; }
extern "C" HANDLE WINAPI LoadImageBuf(const void *, UINT, UINT, INT, INT, UINT) { return NULL; }
extern "C" HBITMAP WINAPI LoadBitmapW(HINSTANCE, LPCWSTR) { return NULL; }
extern "C" HBITMAP WINAPI LoadBitmapA(HINSTANCE, LPCSTR) { return NULL; }
extern "C" HICON WINAPI CreateIconIndirect(PICONINFO) { return (HICON)1; }
extern "C" BOOL WINAPI GetIconInfo(HICON, PICONINFO) { return FALSE; }
extern "C" BOOL WINAPI DrawIcon(HDC hdc, INT x, INT y, HICON) { return TRUE; }
extern "C" BOOL WINAPI DrawIconEx(HDC hdc, INT x, INT y, HICON, INT w, INT h, UINT, HBRUSH, UINT) { return TRUE; }
extern "C" BOOL WINAPI DestroyIcon(HICON) { return TRUE; }
extern "C" HICON WINAPI CreateIconFromResource(LPBYTE, UINT, BOOL, DWORD) { return (HICON)1; }
extern "C" HICON WINAPI CreateIconFromResourceEx(LPBYTE, UINT, BOOL, DWORD, INT, INT, UINT) { return (HICON)1; }
extern "C" BOOL WINAPI DestroyIconIndirect(HICON) { return TRUE; }
extern "C" int WINAPI LookupIconIdFromDirectoryEx(LPBYTE, BOOL, INT, INT, UINT) { return 0; }
extern "C" BOOL WINAPI GetIconInfoEx(HICON, void *) { return FALSE; }

/* 图标/光标访问器：cursormgr.cpp 依赖 SetCursorID/GetCursorID，wnd.cpp 依赖
 * GetIconHotSpot。全部 C++ 链接（与 cairo/apple 后端的定义一致，均无 extern "C"）。
 * 句柄判空，避免对 CreateIconIndirect 之类的假句柄解引用。 */
void SetCursorID(HICON hIcon, WORD cursorId) { if (hIcon) hIcon->cursorId = cursorId; }
WORD GetCursorID(HICON hIcon) { return hIcon ? hIcon->cursorId : 0; }
POINT GetIconHotSpot(HICON hIcon)
{
    POINT ret = { 0, 0 };
    if (hIcon) { ret.x = (LONG)hIcon->xHotspot; ret.y = (LONG)hIcon->yHotspot; }
    return ret;
}

/* ---------- WebKit / EMF 兼容接口（SOUI 不使用，给合理桩）---------- */
extern "C" int WINAPI EnumFontFamiliesExW(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD) { return 0; }
extern "C" int WINAPI EnumFontFamiliesExA(HDC, LPLOGFONTA, FONTENUMPROCA, LPARAM, DWORD) { return 0; }
extern "C" DWORD WINAPI GetGlyphIndicesW(HDC, LPCWSTR, int, LPWORD, DWORD) { return 0; }
extern "C" DWORD WINAPI GetGlyphIndicesA(HDC, LPCSTR, int, LPWORD, DWORD) { return 0; }
extern "C" DWORD WINAPI GetGlyphOutlineW(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, CONST MAT2 *) { return GDI_ERROR; }
extern "C" DWORD WINAPI GetGlyphOutlineA(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID, CONST MAT2 *) { return GDI_ERROR; }
extern "C" UINT WINAPI GetOutlineTextMetricsW(HDC, UINT, LPOUTLINETEXTMETRICW) { return 0; }
extern "C" UINT WINAPI GetOutlineTextMetricsA(HDC, UINT, LPOUTLINETEXTMETRICA) { return 0; }
extern "C" DWORD WINAPI GetFontData(HDC, DWORD, DWORD, LPVOID, DWORD) { return GDI_ERROR; }
extern "C" DWORD WINAPI GetFontUnicodeRanges(HDC, LPGLYPHSET) { return 0; }
extern "C" BOOL WINAPI GetCharWidthI(HDC, UINT, UINT, LPWORD, LPINT) { return FALSE; }
extern "C" DWORD WINAPI GetCharacterPlacementW(HDC, LPCWSTR, int, int, LPGCP_RESULTSW, DWORD) { return 0; }
extern "C" DWORD WINAPI GetCharacterPlacementA(HDC, LPCSTR, int, int, LPGCP_RESULTSA, DWORD) { return 0; }
extern "C" HDC WINAPI CreateEnhMetaFileW(HDC, LPCWSTR, CONST RECT *, LPCWSTR) { return (HDC)1; }
extern "C" HDC WINAPI CreateEnhMetaFileA(HDC, LPCSTR, CONST RECT *, LPCSTR) { return (HDC)1; }
extern "C" HENHMETAFILE WINAPI CloseEnhMetaFile(HDC) { return (HENHMETAFILE)1; }
extern "C" BOOL WINAPI DeleteEnhMetaFile(HENHMETAFILE) { return TRUE; }
extern "C" BOOL WINAPI EnumEnhMetaFile(HDC, HENHMETAFILE, ENHMFENUMPROC, LPVOID, CONST RECT *) { return FALSE; }
extern "C" int WINAPI AddFontResourceW(LPCWSTR) { return 1; }
extern "C" int WINAPI AddFontResourceA(LPCSTR) { return 1; }
extern "C" int WINAPI AddFontResourceExW(LPCWSTR, DWORD, PVOID) { return 1; }
extern "C" int WINAPI AddFontResourceExA(LPCSTR, DWORD, PVOID) { return 1; }
extern "C" HANDLE WINAPI AddFontMemResourceEx(PVOID, DWORD, PVOID, DWORD *) { return (HANDLE)1; }
extern "C" BOOL WINAPI RemoveFontMemResourceEx(HANDLE) { return TRUE; }

/* ---------- 其他未用接口（返回安全默认）---------- */
extern "C" BOOL WINAPI UpdateLayeredWindow(HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD) { return FALSE; }
extern "C" BOOL WINAPI DrawFrameControl(HDC, LPRECT, UINT, UINT) { return FALSE; }
extern "C" int WINAPI GetLayout(HDC) { return 0; }
extern "C" BOOL WINAPI SetLayout(HDC, int) { return TRUE; }
