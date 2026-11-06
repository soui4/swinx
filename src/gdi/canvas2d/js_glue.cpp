/*
 * js_glue.cpp -- route C（canvas2d）GDI 后端的 JS 胶水。
 *
 * 用 emscripten::val 直接操作浏览器对象（document.createElement / 2D context），
 * C++ 侧只持有 emscripten::val 引用。所有函数仅在 __EMSCRIPTEN__ 下编译。
 *
 * 为什么全部用 val、不用 EM_JS：
 *   emscripten 的 create_em_js 只把签名里的「参数名」抽出来拼成
 *   `function name(a, b) { body }`，它既不认识 emscripten::val 参数（wasm 侧传进来
 *   的是 val 句柄整数，不是 JS 对象），也不包装 val 返回值（body 直接 return 一个 JS
 *   对象，wasm 侧却按句柄解读，于是拿到 undefined）。实测症状：
 *     Uncaught TypeError: Cannot read properties of undefined (reading 'getContext')
 *   所以凡是「返回 canvas / 接收 canvas」的胶水一律用 val 的 C++ API 实现；批量像素
 *   搬运用 typed_memory_view 把 wasm 堆当成 JS 类型化数组的底层 buffer，一次
 *   set()/构造 ImageData 拷完，避免逐像素跨语言访问。
 *
 * 唯一的例外是文件末尾的「热路径派发器」（swx_op / swx_op_str / swx_blit /
 * swx_xform）：它们的参数全是 int/double/指针、返回 void，正好是 EM_JS 能处理的
 * 形态。为什么这几条必须绕开 embind，见那边的注释 —— 一句话：embind 每次调用
 * 3~10µs，一帧上千次就是十几毫秒（实测整窗重绘 1800 次调用），换成普通 wasm
 * import 之后是几百纳秒。
 */
#include "js_glue.h"
#include <emscripten.h>   /* EM_ASM / EM_JS（热路径派发器） */
#include <emscripten/val.h>
#include <cstdint>
#include <string>
#include <vector>

using emscripten::typed_memory_view;
using emscripten::val;

extern "C" {

/* 创建一个离屏 HTMLCanvasElement 并返回它。alpha≠0 时该 canvas 支持透明像素
 *（用于位图内存 DC）；窗口后备画布用 alpha=0（与可见 canvas 一致，getImageData
 * 返回不透明像素）。 */
emscripten::val SwinxCanvasCreate(int w, int h, int alpha)
{
    val document = val::global("document");
    val c = document.call<val>("createElement", std::string("canvas"));
    c.set(std::string("width"), (w > 0) ? w : 1);
    c.set(std::string("height"), (h > 0) ? h : 1);
    val opts = val::object();
    opts.set(std::string("alpha"), alpha != 0);
    /* 不设 willReadFrequently：它会把这块 canvas 钉在 CPU（Skia 软件）后端。
     * 以前上屏是 getImageData 全屏读回，那个标志确实省掉了"每次读回都强制同步"；
     * 但现在 canvas2d 的上屏改成 SwxPresentSurface 把离屏画布直接 drawImage 到
     * 显示画布（见 SConnection::presentPending），窗口画布全程只写不读 ——
     * 留着它等于让所有绘制都走软件光栅，还要额外上传一次显存。
     *
     * 实测（有头 Edge，真 GPU，坦克大战一帧 ~1430 次调用）：
     *   不设：spanMed 4.7~4.9ms   设：spanMed 5.3~5.6ms
     * （无头 Edge 走 SwiftShader 软件光栅，A/B 结论相反 —— 别在无头下判这个。）
     * 前提是没有任何代码对窗口画布做 getImageData/GetDIBits：目前只有位图
     * （alpha!=0，全部位图操作都走它们自己的画布）会被读回。 */
    c.call<val>("getContext", std::string("2d"), opts);
    return c;
}

/* 改变 canvas 尺寸（会清空内容，与 cairo surface resize 语义一致）。ctx 对象在
 * 重新设置 width/height 后仍然有效，无需重新取。 */
void SwinxCanvasResize(emscripten::val canvas, int w, int h)
{
    canvas.set(std::string("width"), (w > 0) ? w : 1);
    canvas.set(std::string("height"), (h > 0) ? h : 1);
}

/* 把 canvas 的 (sx,sy,sw,sh) 矩形按浏览器原生 RGBA 字节序原样写进 dst（字节寻址，
 * dstStrideBytes 是每行字节数）。整行宽（sw*4 == dstStrideBytes）时一次 set 拷完，
 * 否则逐行 set —— 无论哪种都**不做逐像素换序**，这是上屏路径的主体开销。 */
void SwinxCanvasReadPixelsRaw(emscripten::val canvas, int sx, int sy, int sw, int sh,
                              uint8_t *dst, int dstStrideBytes)
{
    if (!dst || sw <= 0 || sh <= 0 || !dstStrideBytes)
        return;

    val ctx = canvas.call<val>("getContext", std::string("2d"));
    val img = ctx.call<val>("getImageData", sx, sy, sw, sh);
    val data = img["data"]; /* Uint8ClampedArray：RGBA 交错，长度 sw*sh*4 */
    const size_t rowBytes = (size_t)sw * 4;
    if ((size_t)dstStrideBytes == rowBytes)
    {
        /* 整块连续（脏矩形覆盖整行宽度，最常见）：一次 typed view + set */
        val view{ typed_memory_view(rowBytes * (size_t)sh, dst) };
        view.call<void>("set", data);
        return;
    }
    /* 目标有行间空隙：逐行 set（行数一般只有几十到几百，比逐像素便宜两个数量级） */
    for (int y = 0; y < sh; ++y)
    {
        val rowView{ typed_memory_view(rowBytes, dst + (size_t)y * (size_t)dstStrideBytes) };
        val rowSrc = data.call<val>("subarray", (int)((size_t)y * rowBytes), (int)((size_t)(y + 1) * rowBytes));
        rowView.call<void>("set", rowSrc);
    }
}

/* 把 canvas 的 (sx,sy,sw,sh) 矩形读回为 ARGB32（0xAARRGGBB，小端）写入 WASM 堆
 * dst（dstStride 以 uint32/像素为单位）。getImageData 给的是 RGBA 字节序：
 *   - 先用 typed_memory_view 把整段 RGBA 字节一次 set() 进 wasm 侧临时缓冲；
 *   - 再在 C++ 里做 RGBA→ARGB32 重排写进 dst（每行按 dstStride 跨步）。 */
void SwinxCanvasReadPixels(emscripten::val canvas, int sx, int sy, int sw, int sh,
                           uint32_t *dst, int dstStride)
{
    if (!dst || sw <= 0 || sh <= 0)
        return;

    static std::vector<uint8_t> s_tmp;
    size_t need = (size_t)sw * (size_t)sh * 4;
    if (s_tmp.size() < need)
        s_tmp.resize(need);

    val ctx = canvas.call<val>("getContext", std::string("2d"));
    val img = ctx.call<val>("getImageData", sx, sy, sw, sh);
    val data = img["data"]; /* Uint8ClampedArray：RGBA 交错，长度 sw*sh*4 */
    val view{ typed_memory_view(need, s_tmp.data()) }; /* 直接初始化：val(memory_view) 是 explicit */
    view.call<void>("set", data);

    for (int y = 0; y < sh; ++y)
    {
        const uint8_t *src = s_tmp.data() + (size_t)y * (size_t)sw * 4;
        uint32_t *row = dst + (size_t)y * (size_t)dstStride;
        for (int x = 0; x < sw; ++x)
        {
            const uint8_t *p = src + (size_t)x * 4;
            row[x] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                     ((uint32_t)p[1] << 8) | (uint32_t)p[2];
        }
    }
}

/* 把 WASM 堆里的位图（srcPtr，w×h 像素，RGBA 字节序）putImageData 到 canvas（DIB 回写）。
 * 用 typed_memory_view 拿到 wasm 堆的 ArrayBuffer，在其上切一个 Uint8ClampedArray
 * 直接构造 ImageData，避免额外拷贝。 */
void SwinxCanvasPutPixelsAt(emscripten::val canvas, int srcPtr, int w, int h, int x, int y)
{
    if (srcPtr <= 0 || w <= 0 || h <= 0)
        return;
    size_t len = (size_t)w * (size_t)h * 4;
    /* GDI / DIB 的 32 位像素在内存里是 BGRA（ARGB32 的小端序），ImageData 要的是
     * RGBA 交错。直接把这串字节塞进去等于把 R 和 B 对调 —— 系统皮肤里
     * (24,144,255) 的蓝色进度条会被画成 (255,144,24) 的橙色，滑块、勾选框、
     * 任何彩色位图都跟着一起错。这里逐像素换回 RGBA。 */
    static std::vector<uint8_t> s_buf;
    if (s_buf.size() < len)
        s_buf.resize(len);
    const uint8_t *src = (const uint8_t *)(uintptr_t)srcPtr;
    for (size_t i = 0; i < len; i += 4)
    {
        s_buf[i + 0] = src[i + 2]; /* R <- B 位 */
        s_buf[i + 1] = src[i + 1];
        s_buf[i + 2] = src[i + 0]; /* B <- R 位 */
        s_buf[i + 3] = src[i + 3];
    }
    val u8{ typed_memory_view(len, s_buf.data()) };
    val buf = u8["buffer"]; /* 整个 wasm 堆的 ArrayBuffer */
    val clamped = val::global("Uint8ClampedArray").new_(buf, (int)(uintptr_t)s_buf.data(), len);
    val img = val::global("ImageData").new_(clamped, w, h);
    val ctx = canvas.call<val>("getContext", std::string("2d"));
    ctx.call<void>("putImageData", img, (double)x, (double)y);
}

void SwinxCanvasPutPixels(emscripten::val canvas, int srcPtr, int w, int h)
{
    SwinxCanvasPutPixelsAt(canvas, srcPtr, w, h, 0, 0);
}

double SwinxCanvasDpr(void)
{
    val win = val::global("window");
    if (win.isUndefined() || win.isNull())
        return 1.0;
    val dpr = win["devicePixelRatio"];
    if (dpr.isUndefined() || dpr.isNull())
        return 1.0;
    return dpr.as<double>();
}

/* UTF-8 字节 → JS 字符串。std::string 经 embind 直接传会按 Latin-1 解码（中文变
 * 乱码），所以统一走 TextDecoder。解码器只建一次（TextDecoder 构造成本不低）。 */
emscripten::val SwinxJsText(const char *utf8, int len)
{
    static val s_decoder = val::global("TextDecoder").new_(std::string("utf-8"));
    if (!utf8 || len <= 0)
        return s_decoder.call<val>("decode", val::global("Uint8Array").new_(0));
    val u8{ typed_memory_view((size_t)len, (const uint8_t *)utf8) };
    return s_decoder.call<val>("decode", u8);
}

/* 浏览器实测字体度量 → [ascent, descent]（CSS px）。
 *
 * 为什么需要：GDI 的 tmAscent/tmDescent/tmHeight 以及 DrawText 的 DT_VCENTER 全部
 * 按真实字体度量的 ascent+descent 算；SOUI 里大量 wrapContent 行高（顶栏、页签条、
 * 文本行）又是由 MeasureText 返回的 cy 决定的。后端若用「sizePx*1.2」估算，每一行
 * 都会比真实度量矮 2~3px，于是整页内容逐行上移、页签文字压到上方内容上。
 * cairo 版取的是 FreeType 的 ascender/descender，这里取浏览器同名字段，口径一致。
 *
 * fontBoundingBox* 在 Chrome/Edge 与 node 的 canvas 实现里都有；老环境拿不到时返回
 * 0，调用方（makeFont）自行回退到估算值。 */
emscripten::val SwinxFontMetrics(const char *cssFontUtf8, int len)
{
    static val s_ctx = val::global("document")
                           .call<val>("createElement", std::string("canvas"))
                           .call<val>("getContext", std::string("2d"));
    val out = val::array();
    if (len > 0 && cssFontUtf8)
        s_ctx.set(std::string("font"), SwinxJsText(cssFontUtf8, len));
    s_ctx.set(std::string("textBaseline"), std::string("alphabetic"));
    val m = s_ctx.call<val>("measureText", std::string("Hg"));
    val ja = m["fontBoundingBoxAscent"];
    val jd = m["fontBoundingBoxDescent"];
    double a = (ja.isUndefined() || ja.isNull()) ? 0.0 : ja.as<double>();
    double d = (jd.isUndefined() || jd.isNull()) ? 0.0 : jd.as<double>();
    out.call<void>("push", a);
    out.call<void>("push", d);
    return out;
}


/* =====================================================================
 * 热路径派发器
 * =====================================================================
 * 为什么不用 embind（实测数据见 tools/diag/measure_span.cjs）：整窗重绘一帧
 * 约 2300 次 Canvas 2D 调用。embind 的 val::call/val::set 每次要跨 wasm→JS 边界、
 * 把方法名当 std::string 转成 JS 字符串、把参数逐个装箱，单次 3~10µs；累计 7~20ms，
 * 一帧就把 16.7ms 的预算吃光。EM_JS 展开成普通 wasm import，V8 对「整数/浮点参数」
 * 的直接调用只要一两百纳秒，同一个 switch 里再调 ctx 就没别的开销了。
 *
 * 约定：
 *   - ctx 用整数下标（Module/globalThis 的 __swxCtx 数组），创建 canvas 时登记一次；
 *   - 所有函数**立即执行**、不缓冲，绘制顺序与改造前逐字节一致；
 *   - 颜色串（'#rrggbb' / 'rgba(...)'）在 JS 侧按 COLORREF 缓存，跨调用复用。
 * ===================================================================== */

/* 一次性初始化：注册表、颜色串缓存、UTF-8 解码器。
 * 用 EM_ASM 写进 globalThis，供下面的 EM_JS 函数体使用（EM_JS 体在模块作用域里
 * 运行，严格模式下直接读未声明的全局会抛 ReferenceError，所以统一写成
 * globalThis.xxx 的形式）。 */
static void swxEnsureJs(void)
{
    EM_ASM({
        var g = globalThis;
        if (g.__swxCtx)
            return;
        g.__swxCtx = [];
        g.__swxColorTab = new Map();
        g.__swxEmptyDash = [];
        g.__swxDec = new TextDecoder('utf-8');
        g.__swxSetCtx = function (id, c) { g.__swxCtx[id] = c; };
        /* COLORREF → CSS 颜色串。字节序与 C++ 的 GetRValue/GetGValue/GetBValue/
         * GetAValue 一致：低字节是 R。alpha 为 0 视作不透明（GDI 的 COLORREF 没有
         * alpha 位，RGB()/GetSysColor 给的高字节永远是 0）。 */
        g.__swxColor = function (v) {
            v = v >>> 0;
            var s = g.__swxColorTab.get(v);
            if (s !== undefined)
                return s;
            /* 注意：EM_ASM 的参数是 C 预处理器实参，**花括号不参与分组**，
             * 顶层出现逗号会把后面的部分当成可变参数切走（症状是编译器开始把
             * JS 当 C++ 解析）。所以这里一句一行，不写 var a=1, b=2; */
            var r = v & 255;
            var gg = (v >>> 8) & 255;
            var b = (v >>> 16) & 255;
            var a = (v >>> 24) & 255;
            if (a === 0 || a === 255)
                s = '#' + ((r | 256).toString(16).slice(1)) + ((gg | 256).toString(16).slice(1)) + ((b | 256).toString(16).slice(1));
            else
                s = 'rgba(' + r + ',' + gg + ',' + b + ',' + (Math.round(a / 255 * 10000) / 10000) + ')';
            g.__swxColorTab.set(v, s);
            return s;
        };
    });
}

/* ctx 下标的回收表（不在 JS 侧维护：注册/注销都在 C++ 里同步完成，省掉跨语言
 * 调用；空闲表为空时按 s_next 递增分配）。 */
static std::vector<int> s_freeCtxIds;

int SwinxCtxRegister(emscripten::val ctx)
{
    static int s_next = 1;
    swxEnsureJs();
    int id;
    if (!s_freeCtxIds.empty())
    {
        id = s_freeCtxIds.back();
        s_freeCtxIds.pop_back();
    }
    else
        id = s_next++;
    /* 直接写进 JS 侧的注册表数组。不用 call<void>()：embind 的 call 模板在这里
     * 选不到重载（参数是 val 而不是可隐式转换的类型），set 更省事。 */
    val table = val::global("__swxCtx");
    table.set(val(id), ctx);
    return id;
}

void SwinxCtxUnregister(int ctxId)
{
    if (ctxId <= 0)
        return;
    val table = val::global("__swxCtx");
    if (table.isUndefined() || table.isNull())
        return;
    /* 置 undefined 而不是 delete：数组保持"快元素"，热路径 __swxCtx[id] 仍是
     * 一次下标访问；下标本身进空闲表复用，数组长度只跟「同时存活的画布数」有关，
     * 不再随游戏时长单调增长。 */
    table.set(val(ctxId), val::undefined());
    s_freeCtxIds.push_back(ctxId);
}

EM_JS(void, swx_op, (int id, int op, double a, double b, double c, double d, double e, double f),
{
    var g = globalThis;
    var ctx = g.__swxCtx[id];
    if (ctx === undefined)
        return;
    switch (op)
    {
    case 1: ctx.save(); break;
    case 2: ctx.restore(); break;
    /* SWX_CLIP_RECT：beginPath+rect+clip 三连是 SOUI 每控件裁一次的热路径
     * （一帧 170 次），合成一次调用省掉 2/3 的跨语言往返。 */
    case 3: ctx.beginPath(); ctx.rect(a, b, c - a, d - b); ctx.clip(); break;
    case 4: ctx.beginPath(); ctx.rect(a, b, c, d); break;
    case 5: ctx.beginPath(); break;
    case 6: ctx.moveTo(a, b); break;
    case 7: ctx.lineTo(a, b); break;
    case 8: ctx.bezierCurveTo(a, b, c, d, e, f); break;
    case 9: ctx.closePath(); break;
    case 10: ctx.fill(); break;
    case 11: ctx.stroke(); break;
    case 12: ctx.fillRect(a, b, c, d); break;
    case 13: ctx.clearRect(a, b, c, d); break;
    case 14: ctx.strokeRect(a, b, c, d); break;
    case 15: ctx.fillStyle = g.__swxColor(a); break;
    case 16: ctx.strokeStyle = g.__swxColor(a); break;
    /* 0=left 1=center 2=right（映射在 C++ 侧按 GDI 的 TA_* 位算好） */
    case 17: ctx.textAlign = a === 1 ? 'center' : (a === 2 ? 'right' : 'left'); break;
    case 18: ctx.textBaseline = 'alphabetic'; break;
    case 19: ctx.setTransform(a, b, c, d, e, f); break;
    case 20: ctx.globalAlpha = a; break;
    case 21: ctx.imageSmoothingEnabled = a !== 0; break;
    case 22: ctx.globalCompositeOperation = a === 1 ? 'source-in' : (a === 2 ? 'xor' : 'source-over'); break;
    case 23: ctx.lineWidth = a; break;
    case 24: ctx.setLineDash(g.__swxEmptyDash); break;
    case 25: ctx.beginPath(); ctx.roundRect(a, b, c, d, e, e); break;
    }
});

EM_JS(void, swx_op_str, (int id, int op, const char *p, int n, double a, double b, double c, double d),
{
    var g = globalThis;
    var ctx = g.__swxCtx[id];
    if (ctx === undefined)
        return;
    var s = (n > 0) ? g.__swxDec.decode(HEAPU8.subarray(p, p + n)) : '';
    switch (op)
    {
    case 26: ctx.fillText(s, a, b); break;
    case 27: ctx.strokeText(s, a, b); break;
    case 28: ctx.font = s; break;
    }
});

EM_JS(void, swx_blit, (int id, int srcId, double dx, double dy, double dw, double dh,
                       double sx, double sy, double sw, double sh, int smooth),
{
    var g = globalThis;
    var ctx = g.__swxCtx[id];
    if (ctx === undefined)
        return;
    /* 平滑开关是持久的 ctx 状态：smooth<0 表示"调用方自己管，别动它"
     * （BitBlt/AlphaBlend/TransparentBlt 都走这条），0/1 才强制下发。
     * 九宫格拼接必须关掉平滑，否则块边界会出现半透明缝。 */
    if (smooth >= 0 && ctx.imageSmoothingEnabled !== (smooth !== 0))
        ctx.imageSmoothingEnabled = smooth !== 0;
    var src = g.__swxCtx[srcId];
    if (src === undefined)
        return;
    ctx.drawImage(src.canvas, sx, sy, sw, sh, dx, dy, dw, dh);
});

EM_JS(void, swx_xform, (int id, double a, double b, double c, double d, double e, double f),
{
    var ctx = globalThis.__swxCtx[id];
    if (ctx !== undefined)
        ctx.setTransform(a, b, c, d, e, f);
});

void SwxOp(int ctxId, int op, double a, double b, double c, double d, double e, double f)
{
    swx_op(ctxId, op, a, b, c, d, e, f);
}

void SwxOpStr(int ctxId, int op, const char *utf8, int len, double a, double b, double c, double d)
{
    swx_op_str(ctxId, op, utf8, len, a, b, c, d);
}

void SwxBlit(int ctxId, int srcId, double dx, double dy, double dw, double dh,
             double sx, double sy, double sw, double sh, int smooth)
{
    swx_blit(ctxId, srcId, dx, dy, dw, dh, sx, sy, sw, sh, smooth);
}

void SwxXform(int ctxId, double a, double b, double c, double d, double e, double f)
{
    swx_xform(ctxId, a, b, c, d, e, f);
}

/* 离屏窗口画布 → 显示画布 的直接合成。
 * __swxDisplay 由 soui.html 在创建显示画布 ctx 时挂到 window 上。
 *
 * 这里用默认的 source-over 就够，不需要像 BitBlt(SRCCOPY) 那样换成 copy：
 * 窗口后备画布是 SwinxCanvasCreateSurfaceRaw(cx, cy, alpha=0) 建的，也就是
 * getContext('2d', {alpha:false})，源像素的 alpha 恒为 255，合成与替换等价，
 * 透明源让上一帧透出来的那条路径在这里不存在（可能含透明的 memRT → 窗口画布
 * 那一跳由 gdi.cpp 的 blitRop 负责，见那里的说明与 §33/§34）。 */
EM_JS(void, swx_present_surface, (int srcId, double sx, double sy, double w, double h, double dx, double dy),
{
    var g = globalThis;
    var dst = g.__swxDisplay;
    var src = g.__swxCtx[srcId];
    if (dst === undefined || src === undefined)
        return;
    if (src === dst)
        return;
    dst.drawImage(src.canvas, sx, sy, w, h, dx, dy, w, h);
});

void SwxPresentSurface(int srcCtxId, double sx, double sy, double w, double h, double dx, double dy)
{
    swx_present_surface(srcCtxId, sx, sy, w, h, dx, dy);
}

} // extern "C"
