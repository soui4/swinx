/*
 * builtin_image.cpp -- route C（canvas2d）内置图片实现（BuiltinImage 接口）。
 *
 * 作用：wnd.cpp 的原生滚动条、msgbox.cpp 的消息框图标会调用 BuiltinImage 来画
 * 「内置皮肤位图」（滚动条上下箭头/滑块/滑轨、消息框 信息/警告/错误 图标）。
 *
 * cairo 后端（src/gdi/cairo/builtin_image.cpp）用 cairo 解 PNG；route C 不链接
 * cairo，也没有任何 PNG 解码库（imgdecoder-stb 属 SOUI 组件，swinx 不依赖），
 * 故这里自带一个**精简 PNG 解码器**（inflate + unfilter），把 src/image.hpp 的
 * 内嵌 PNG 解成 ARGB32 后经 SwinxCanvasPutPixels 上传到 canvas 后备位图：
 *   - 解码器只支持本项目资源实际用到的口径：8bit、colorType 6(RGBA)/2(RGB)、
 *     无隔行（四张内嵌 PNG 均为此格式，见本文件顶部解析注释）；其余口径返回
 *     失败，draw* 退化为「没有可画的位图」（与旧空实现行为一致）。
 *   - draw* 三方法的源图切分/状态选择/九宫格逻辑逐行对照 cairo 版，保证两个
 *     后端渲染一致。
 */
#include "../builtin_image.h"
#include "js_glue.h"
#include "image.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

/* ============================ zlib inflate ============================ */
/* puff 风格的精简实现：只解 RFC1950(zlib 流) → RFC1951(deflate)。
 * 本文件内嵌资源都很小（几 KB~十几 KB），无需性能优化，正确优先。 */

struct BitReader
{
    const uint8_t *data;
    size_t len;
    size_t pos;    // 下一个未消费字节
    uint32_t acc;  // 位累积器（低位先进）
    int nbits;     // acc 里的有效位数

    BitReader(const uint8_t *d, size_t n) : data(d), len(n), pos(0), acc(0), nbits(0) {}

    /* 取 need 位（need<=25）；不够时返回 -1 */
    int take(int need)
    {
        while (nbits < need)
        {
            if (pos >= len)
                return -1;
            acc |= (uint32_t)data[pos++] << nbits;
            nbits += 8;
        }
        int v = (int)(acc & ((1u << need) - 1));
        acc >>= need;
        nbits -= need;
        return v;
    }

    /* 字节对齐（stored 块用），丢弃 acc 中不满一个字节的零头 */
    void alignByte()
    {
        acc = 0;
        nbits = 0;
    }

    bool byteAligned() const { return nbits == 0; }
};

/* 规范 Huffman 解码表（长度 → 符号），puff 的 construct/decode 思路 */
struct Huff
{
    int count[16];      // 每个码长的符号数
    std::vector<int> symbol;

    bool build(const uint8_t *lengths, int n)
    {
        memset(count, 0, sizeof(count));
        for (int i = 0; i < n; i++)
            count[lengths[i]]++;
        if (count[0] == n) /* 全零：没有符号（空表合法，如无压缩数据） */
        {
            symbol.clear();
            return true;
        }
        /* over-subscribed / incomplete 检查：left = 1; len; left <<= 1; left -= count[len] */
        int left = 1;
        for (int l = 1; l < 16; l++)
        {
            left <<= 1;
            left -= count[l];
            if (left < 0)
                return false;
        }
        /* 按码长升序、同长按符号升序排符号表 */
        std::vector<int> offs(16, 0);
        for (int l = 1; l < 15; l++)
            offs[l + 1] = offs[l] + count[l];
        symbol.assign(n, 0);
        for (int i = 0; i < n; i++)
            if (lengths[i])
                symbol[offs[lengths[i]]++] = i;
        return true;
    }

    int decode(BitReader &br) const
    {
        int len = 1, code = 0, first = 0, index = 0;
        while (len < 16)
        {
            int b = br.take(1);
            if (b < 0)
                return -1;
            code |= b;
            int c = count[len];
            if (code - c < first)
                return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
            len++;
        }
        return -1;
    }
};

/* lit/len 与 dist 的基值/额外位表（RFC1951 3.2.5） */
const uint16_t kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                 8193, 12289, 16385, 24577 };
const uint8_t kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
const uint8_t kClcOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* 解一个 deflate 块；返回 0 成功，-1 失败 */
int inflateBlock(BitReader &br, std::vector<uint8_t> &out)
{
    Huff lit, dist;
    int last = 0;
    do
    {
        last = br.take(1);   // BFINAL
        if (last < 0)
            return -1;
        int type = br.take(2); // BTYPE
        if (type < 0)
            return -1;

        if (type == 0)
        { // stored：对齐字节后 LEN/NLEN + 原文
            br.alignByte();
            if (br.pos + 4 > br.len)
                return -1;
            int l = br.data[br.pos] | (br.data[br.pos + 1] << 8);
            int nl = br.data[br.pos + 2] | (br.data[br.pos + 3] << 8);
            br.pos += 4;
            if ((l ^ 0xFFFF) != nl || br.pos + (size_t)l > br.len)
                return -1;
            out.insert(out.end(), br.data + br.pos, br.data + br.pos + l);
            br.pos += l;
        }
        else if (type == 1 || type == 2)
        { // fixed / dynamic Huffman
            if (type == 1)
            {
                uint8_t lengths[288];
                for (int i = 0; i < 144; i++) lengths[i] = 8;
                for (int i = 144; i < 256; i++) lengths[i] = 9;
                for (int i = 256; i < 280; i++) lengths[i] = 7;
                for (int i = 280; i < 288; i++) lengths[i] = 8;
                if (!lit.build(lengths, 288))
                    return -1;
                uint8_t dlengths[30];
                memset(dlengths, 5, sizeof(dlengths));
                if (!dist.build(dlengths, 30))
                    return -1;
            }
            else
            {
                int hlit = br.take(5);
                int hdist = br.take(5);
                int hclen = br.take(4);
                if (hlit < 0 || hdist < 0 || hclen < 0)
                    return -1;
                int nlit = hlit + 257, ndist = hdist + 1, nclc = hclen + 4;
                uint8_t clcLengths[19];
                memset(clcLengths, 0, sizeof(clcLengths));
                for (int i = 0; i < nclc; i++)
                {
                    int v = br.take(3);
                    if (v < 0)
                        return -1;
                    clcLengths[kClcOrder[i]] = (uint8_t)v;
                }
                Huff clc;
                if (!clc.build(clcLengths, 19))
                    return -1;
                /* 解 lit+dist 的码长串（16/17/18 是重复指令） */
                std::vector<uint8_t> lengths((size_t)nlit + (size_t)ndist, 0);
                int i = 0;
                while (i < nlit + ndist)
                {
                    int sym = clc.decode(br);
                    if (sym < 0)
                        return -1;
                    if (sym < 16)
                    {
                        lengths[i++] = (uint8_t)sym;
                    }
                    else if (sym == 16)
                    { /* 重复前一个 3..6 次 */
                        if (i == 0)
                            return -1;
                        uint8_t prev = lengths[i - 1];
                        int rep = br.take(2);
                        if (rep < 0)
                            return -1;
                        rep += 3;
                        while (rep-- && i < nlit + ndist)
                            lengths[i++] = prev;
                    }
                    else if (sym == 17)
                    { /* 0 x 3..10 */
                        int rep = br.take(3);
                        if (rep < 0)
                            return -1;
                        rep += 3;
                        while (rep-- && i < nlit + ndist)
                            lengths[i++] = 0;
                    }
                    else
                    { /* 18：0 x 11..138 */
                        int rep = br.take(7);
                        if (rep < 0)
                            return -1;
                        rep += 11;
                        while (rep-- && i < nlit + ndist)
                            lengths[i++] = 0;
                    }
                }
                if (lengths[256] == 0) /* 没有块结束码 */
                    return -1;
                if (!lit.build(lengths.data(), nlit))
                    return -1;
                if (!dist.build(lengths.data() + nlit, ndist))
                    return -1;
            }

            /* 解码符号流 */
            for (;;)
            {
                int sym = lit.decode(br);
                if (sym < 0)
                    return -1;
                if (sym < 256)
                {
                    out.push_back((uint8_t)sym);
                }
                else if (sym == 256)
                {
                    break; /* 块结束 */
                }
                else
                {
                    sym -= 257;
                    if (sym >= 29)
                        return -1;
                    int lenExtra = br.take(kLenExtra[sym]);
                    if (lenExtra < 0)
                        return -1;
                    size_t length = kLenBase[sym] + (size_t)lenExtra;
                    int dsym = dist.decode(br);
                    if (dsym < 0 || dsym >= 30)
                        return -1;
                    int distExtra = br.take(kDistExtra[dsym]);
                    if (distExtra < 0)
                        return -1;
                    size_t d = kDistBase[dsym] + (size_t)distExtra;
                    if (d > out.size())
                        return -1; /* 引用越界 */
                    size_t from = out.size() - d;
                    for (size_t k = 0; k < length; k++)
                        out.push_back(out[from + k]); /* 逐字节：src/dst 可重叠 */
                }
            }
        }
        else
        {
            return -1; /* BTYPE=3 保留 */
        }
    } while (!last);
    return 0;
}

/* zlib 包装：2 字节头 + deflate 流 + 4 字节 adler32（忽略校验） */
bool inflateZlib(const uint8_t *src, size_t len, std::vector<uint8_t> &out)
{
    if (len < 6)
        return false;
    int cmf = src[0], flg = src[1];
    if ((cmf & 0x0F) != 8 /* deflate */ || ((cmf << 8) | flg) % 31 != 0)
        return false;
    size_t off = 2;
    if (flg & 0x20) /* FDICT */
    {
        if (len < 6)
            return false;
        off += 4;
    }
    BitReader br(src + off, len - off - 4 /* adler32 */);
    return inflateBlock(br, out) == 0;
}

/* ============================ PNG 解码 ============================ */
/* 只支持内置资源用到的口径：8bit、colorType 6(RGBA)/2(RGB)、无隔行。
 * 输出统一 RGBA8。失败返回 false（调用方按「无位图」降级，不致崩溃）。 */

static const uint8_t kPngSig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

uint32_t readBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Paeth 预测器（RFC2083） */
int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

bool decodePng(const uint8_t *png, size_t len, std::vector<uint8_t> &rgba, int &outW, int &outH)
{
    if (len < 8 + 12 || memcmp(png, kPngSig, 8) != 0)
        return false;

    int w = 0, h = 0, colorType = -1, channels = 0;
    std::vector<uint8_t> idat;
    size_t pos = 8;
    bool haveIHDR = false, haveIEND = false;

    while (pos + 12 <= len)
    {
        uint32_t clen = readBE32(png + pos);
        const uint8_t *type = png + pos + 4;
        const uint8_t *body = png + pos + 8;
        if (pos + 12 + (size_t)clen > len)
            return false;
        if (memcmp(type, "IHDR", 4) == 0)
        {
            if (clen < 13)
                return false;
            w = (int)readBE32(body);
            h = (int)readBE32(body + 4);
            int depth = body[8];
            colorType = body[9];
            int compression = body[10], filter = body[11], interlace = body[12];
            if (w <= 0 || h <= 0 || depth != 8 || compression != 0 || filter != 0 || interlace != 0)
                return false;
            if (colorType == 6)
                channels = 4;
            else if (colorType == 2)
                channels = 3;
            else
                return false; /* 调色板/灰度：内置资源用不到，按失败降级 */
            haveIHDR = true;
        }
        else if (memcmp(type, "IDAT", 4) == 0)
        {
            idat.insert(idat.end(), body, body + clen);
        }
        else if (memcmp(type, "IEND", 4) == 0)
        {
            haveIEND = true;
            break;
        }
        pos += 12 + clen;
    }
    if (!haveIHDR || !haveIEND || idat.empty())
        return false;

    /* inflate 原始扫描线（每行 1 filter 字节 + w*channels 字节） */
    std::vector<uint8_t> raw;
    if (!inflateZlib(idat.data(), idat.size(), raw))
        return false;
    size_t stride = (size_t)w * channels;
    if (raw.size() < (size_t)h * (stride + 1))
        return false;

    rgba.assign((size_t)w * (size_t)h * 4, 0);
    std::vector<uint8_t> prev(stride, 0);
    for (int y = 0; y < h; y++)
    {
        const uint8_t ft = raw[(size_t)y * (stride + 1)];
        uint8_t *line = &raw[(size_t)y * (stride + 1) + 1];
        for (size_t i = 0; i < stride; i++)
        {
            int a = (i >= (size_t)channels) ? line[i - channels] : 0;
            int b = prev[i];
            int c = (i >= (size_t)channels) ? prev[i - channels] : 0;
            int v = line[i];
            switch (ft)
            {
            case 0: break;                             /* None */
            case 1: v += a; break;                     /* Sub */
            case 2: v += b; break;                     /* Up */
            case 3: v += (a + b) / 2; break;           /* Average */
            case 4: v += paeth(a, b, c); break;        /* Paeth */
            default: return false;
            }
            line[i] = (uint8_t)v;
        }
        memcpy(prev.data(), line, stride);
        uint8_t *dst = &rgba[(size_t)y * (size_t)w * 4];
        if (channels == 4)
        {
            memcpy(dst, line, stride);
        }
        else
        { /* RGB → RGBA（A=255） */
            for (int x = 0; x < w; x++)
            {
                dst[x * 4 + 0] = line[x * 3 + 0];
                dst[x * 4 + 1] = line[x * 3 + 1];
                dst[x * 4 + 2] = line[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
    }
    outW = w;
    outH = h;
    return true;
}

/* 解码内嵌 PNG 并包成 canvas 后备 HBITMAP（像素经 ARGB32 上传）。失败返回 null。 */
HBITMAP loadPngBitmap(const uint8_t *png, size_t len)
{
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!decodePng(png, len, rgba, w, h))
        return nullptr;

    /* ImageData 要 RGBA 字节序，SwinxCanvasPutPixels 收的是 ARGB32（内存里
     * B,G,R,A）并负责转序 —— 这里把 RGBA 重排成 BGRA 再上传。 */
    for (size_t i = 0; i < rgba.size(); i += 4)
    {
        uint8_t r = rgba[i], g = rgba[i + 1], b = rgba[i + 2], a = rgba[i + 3];
        rgba[i + 0] = b;
        rgba[i + 1] = g;
        rgba[i + 2] = r;
        rgba[i + 3] = a;
    }
    HBITMAP hb = SwinxCanvasCreateBitmap(w, h, TRUE);
    if (!hb)
        return nullptr;
    Canvas2DSurface *s = (Canvas2DSurface *)GetGdiObjPtr(hb);
    if (!s)
    {
        DeleteObject(hb);
        return nullptr;
    }
    SwinxCanvasPutPixels(s->canvas, (int)(uintptr_t)rgba.data(), w, h);
    return hb;
}

} // namespace

BuiltinImage *BuiltinImage::instance()
{
    static BuiltinImage inst;
    return &inst;
}

BuiltinImage::BuiltinImage()
    : m_biInfo100(nullptr)
    , m_biInfo200(nullptr)
{
    Init();
}

BuiltinImage::~BuiltinImage()
{
    delete[] m_biInfo100;
    delete[] m_biInfo200;
}

BOOL BuiltinImage::Init()
{
    if (m_biInfo100) /* 幂等：静态实例构造已调过一次 */
        return TRUE;

    struct Cfg
    {
        const uint8_t *buf;
        size_t bufLen;
        int states;
        UINT expandMode;
        RECT margin;
    };
    static const Cfg cfg100[BI_COUNT] = {
        { sys_scrollbar_png, sizeof(sys_scrollbar_png), 1, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 4, 4, 4, 4 } },
        { sys_msgbox_icons_png, sizeof(sys_msgbox_icons_png), 5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0, 0, 0, 0 } },
    };
    static const Cfg cfg200[BI_COUNT] = {
        { sys_scrollbar_200_png, sizeof(sys_scrollbar_200_png), 1, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 8, 8, 8, 8 } },
        { sys_msgbox_icons200_png, sizeof(sys_msgbox_icons200_png), 5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0, 0, 0, 0 } },
    };

    m_biInfo100 = new BiInfo[BI_COUNT];
    m_biInfo200 = new BiInfo[BI_COUNT];
    for (int set = 0; set < 2; set++)
    {
        BiInfo *infos = (set == 0) ? m_biInfo100 : m_biInfo200;
        const Cfg *cfg = (set == 0) ? cfg100 : cfg200;
        for (int i = 0; i < BI_COUNT; i++)
        {
            infos[i].states = cfg[i].states;
            infos[i].expendMode = cfg[i].expandMode;
            infos[i].margin = cfg[i].margin;
            /* 解码失败时 hBmp 保持 null，draw* 返回 FALSE（= 无位图可画） */
            infos[i].hBmp = loadPngBitmap(cfg[i].buf, cfg[i].bufLen);
        }
    }
    return TRUE;
}

BOOL BuiltinImage::drawBiState(HDC hdc, int imgId, ImgState st, const RECT *rcDst, BYTE byAlpha)
{
    if (imgId < 0 || imgId >= BI_COUNT || !m_biInfo100 || !m_biInfo200)
        return FALSE;
    if (st < St_Normal || st > St_Disable)
        return FALSE;
    int scale = GetSystemScale();

    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + imgId;
    if (!biInfo->hBmp)
        return FALSE;
    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / 4;
    int hei = bm.bmHeight;
    RECT rcSrc = { 0, 0, wid, hei };
    OffsetRect(&rcSrc, st * wid, 0);
    DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    return TRUE;
}

BOOL BuiltinImage::drawBiIdx(HDC hdc, int imgId, int idx, const RECT *rcDst, BYTE byAlpha)
{
    if (imgId < 0 || imgId >= BI_COUNT || !m_biInfo100 || !m_biInfo200)
        return FALSE;
    if (idx < 0)
        return FALSE;
    int scale = GetSystemScale();

    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + imgId;
    if (!biInfo->hBmp || biInfo->states <= 0)
        return FALSE;
    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / biInfo->states;
    int hei = bm.bmHeight;
    if (idx >= biInfo->states)
        return FALSE;
    RECT rcSrc = { 0, 0, wid, hei };
    OffsetRect(&rcSrc, idx * wid, 0);
    if (rcDst->right - rcDst->left < wid || rcDst->bottom - rcDst->top < hei)
    {
        DrawBitmapEx(hdc, rcDst, biInfo->hBmp, &rcSrc, EXPEND_MODE_STRETCH, byAlpha);
    }
    else
    {
        DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    }
    return TRUE;
}

BOOL BuiltinImage::drawScrollbarState(HDC hdc, int iPart, BOOL bVert, int st, const RECT *rcDst, BYTE byAlpha)
{
    /* src format, see resource/image/sys_scrollbar.png：
     * 9 列 x 4 行。列 0..3 是纵向部件（LineUp/LineDown/Thumb/Rail），
     * 列 4..7 是横向部件；行 = 状态（Normal/Hover/Push/Disable）。
     * 列 8 是三角形与滑块 grip：行 0=上下三角、行 1/2=纵/横 grip。 */
    if (!m_biInfo100 || !m_biInfo200)
        return FALSE;
    if (st < St_Normal || st > St_Disable)
        return FALSE;
    /* 部件号合法域：0..3（Sb_Line_Up..Sb_Rail）与 8..11（Sb_Triangle 起的
     * 三角形/grip 行，对应 4 行状态图）。越界直接拒绝，别拿去乘坐标。 */
    if (iPart < Sb_Line_Up || (iPart > Sb_Rail && (iPart < Sb_Triangle || iPart > Sb_Triangle + 3)))
        return FALSE;

    int scale = GetSystemScale();
    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + BI_SCROLLBAR;
    if (!biInfo->hBmp)
        return FALSE;
    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / 9;
    int hei = bm.bmHeight / 4;
    RECT rcSrc = { 0, 0, wid, hei };
    if (iPart < Sb_Triangle)
    {
        int iCol = iPart;
        if (!bVert)
            iCol += 4;
        OffsetRect(&rcSrc, wid * iCol, hei * st);
    }
    else
    {
        OffsetRect(&rcSrc, wid * 8, hei * (iPart - Sb_Triangle));
    }
    DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    if (iPart == Sb_Thumb)
    { /* 滑块中段的 grip 点阵（列 8 行 1/2），按滑块长度内缩后原尺寸贴中 */
        RECT rcSrc = { 0, 0, wid, hei };
        RECT rcTmp = *rcDst;
        if (bVert)
        {
            OffsetRect(&rcSrc, wid * 8, hei * 1);
            InflateRect(&rcTmp, 0, -(rcTmp.bottom - rcTmp.top - hei) / 2);
        }
        else
        {
            OffsetRect(&rcSrc, wid * 8, hei * 2);
            InflateRect(&rcTmp, -(rcTmp.right - rcTmp.left - wid) / 2, 0);
        }
        if (rcTmp.right - rcTmp.left <= rcDst->right - rcDst->left && rcTmp.bottom - rcTmp.top <= rcDst->bottom - rcDst->top)
            DrawBitmapEx(hdc, &rcTmp, biInfo->hBmp, &rcSrc, EXPEND_MODE_NONE, byAlpha);
    }
    return TRUE;
}
