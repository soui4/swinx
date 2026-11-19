#include "drawtext.h"
#include "cairo_show_text2.h"
#include <math.h>

#define DT_ELLIPSIS (DT_PATH_ELLIPSIS | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS)
#define CH_ELLIPSIS "..."
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))

void cairo_draw_text(cairo_t *ctx, const char *text, int len, RECT *box, UINT uFormat)
{
    if (len < 0)
        len = strlen(text);
    TextLayoutEx layout;
    layout.init(ctx, text, len, *box, uFormat);

    *box = layout.draw();
}

//////////////////////////////////////////////////////////////////////////
void TextLayoutEx::init(cairo_t *ctx, const char *text, size_t length, const RECT &rc, UINT uFormat)
{
    // cairo/fontconfig 下 '\t' 没有字形，会被画成 .notdef 方块。
    // 先按 kTabWidth 制表位把 '\t' 展开成空格，再解析前缀符，
    // 保证 m_prefix 的索引与展开后的文本一致。
    std::vector<char> src;
    src.reserve(length);
    int nCol = 0;
    for (size_t i = 0; i < length;)
    {
        if (text[i] == '\t')
        {
            int nSpaces = kTabWidth - nCol % kTabWidth;
            src.insert(src.end(), nSpaces, ' ');
            nCol += nSpaces;
            i++;
        }
        else if (text[i] == '\r' || text[i] == '\n')
        {
            src.push_back(text[i]);
            nCol = 0;
            i++;
        }
        else
        {
            const uint8_t *p = (const uint8_t *)text + i;
            const uint8_t *next = _mbsinc(p);
            src.insert(src.end(), text + i, text + i + (next - p));
            nCol++;
            i = next - (const uint8_t *)text;
        }
    }

    if (uFormat & DT_NOPREFIX)
    {
        m_text = src;
    }
    else
    {
        m_prefix.clear();
        const char *p = src.data();
        for (int i = 0; i < (int)src.size();)
        {
            if (*p == '&' && i + 1 < (int)src.size())
            {
                if (src[1] == '&')
                {
                    src.erase(src.begin() + i);
                }
                else
                {
                    src.erase(src.begin() + i);
                    m_prefix.push_back(i);
                }
                i += 1;
                p += 1;
            }
            else
            {
                const char *next = (const char *)_mbsinc((const uint8_t *)p);
                i += next - p;
                p = next;
            }
        }
        m_text = src;
    }

    m_ctx = ctx;
    m_rcBound = rc;
    m_uFormat = uFormat;
    buildLines();
}

void TextLayoutEx::buildLines()
{
    m_lines.clear();

    if (m_uFormat & DT_SINGLELINE)
    {
        LineInfo info = { 0, (int)m_text.size() };
        m_lines.push_back(info);
    }
    else
    {
        const char *text = m_text.data();
        const char *stop = text + m_text.size();
        SkScalar maxWid = m_rcBound.right - m_rcBound.left;
        if (m_uFormat & DT_CALCRECT && maxWid < 1.0f)
            maxWid = 10000.0f;
        int lineHead = 0;
        while (lineHead < (int)m_text.size())
        {
            int endLen = 0;
            size_t line_len = cairo_break_text(m_ctx, text, stop - text, maxWid, endLen);
            line_len -= endLen;
            if (line_len + endLen == 0)
                break;
            LineInfo info = { lineHead, (int)line_len };
            m_lines.push_back(info);
            text += line_len + endLen;
            lineHead += line_len + endLen;
        };
    }
}

SkScalar TextLayoutEx::drawLine(cairo_t *ctx, SkScalar x1, SkScalar x2, SkScalar y, int iBegin, int iEnd, SkScalar nTextWidth)
{
    const char *text = m_text.data() + iBegin;

    if (nTextWidth < 0.0f)
        nTextWidth = measureText(ctx, text, (iEnd - iBegin));
    if (!(m_uFormat & DT_CALCRECT))
    {
        SkScalar x = x1;
        switch (m_uFormat & (DT_CENTER | DT_RIGHT))
        {
        case DT_CENTER:
            x += ((x2 - x1) - nTextWidth) / 2.0f;
            break;
        case DT_RIGHT:
            x += (x2 - x1) - nTextWidth;
            break;
        }
        drawText(ctx, text, (iEnd - iBegin), x, y);
        // draw underlines
        int i = 0;
        while (i < (int)m_prefix.size())
        {
            if (m_prefix[i] >= iBegin)
                break;
            i++;
        }

        while (i < (int)m_prefix.size() && m_prefix[i] < iEnd)
        {
            SkScalar x1 = measureText(ctx, text, (m_prefix[i] - iBegin));
            SkScalar x2 = measureText(ctx, text, (m_prefix[i] - iBegin + 1));
            cairo_draw_line(ctx, x + x1, y + 1, x + x2, y + 1); //绘制下划线
            i++;
        }
    }
    return nTextWidth;
}

SkScalar TextLayoutEx::drawLineEndWithEllipsis(cairo_t *ctx, SkScalar x1, SkScalar x2, SkScalar y, int iBegin, int iEnd, SkScalar maxWidth)
{
    SkScalar widReq = measureText(ctx, m_text.data() + iBegin, (iEnd - iBegin));
    if (widReq <= m_rcBound.right - m_rcBound.left)
    {
        /* 不溢出：宽度刚刚量过，直接交给 drawLine，省掉它的内部再量一次
         * （DT_END_ELLIPSIS 是常用格式，"文本放得下"更是常态，所以这条
         * 分支少一次整行 shaping 就等于这一档快一倍）。 */
        return drawLine(ctx, x1, x2, y, iBegin, iEnd, widReq);
    }
    else
    {
        SkScalar fWidEllipsis = measureText(ctx, CH_ELLIPSIS, 3);
        maxWidth -= fWidEllipsis;

        const char *text = m_text.data() + iBegin;
        int nLen = iEnd - iBegin;

        /* 逐字求"能塞下几个字"：原来这里是
         *     while (i < nLen) { fWord = measureText(ctx, text + i, 1); ... i++; }
         * 即每个字节一次 shaping（SplitTextRuns + text_to_glyphs +
         * glyph_extents + 两次 heap 分配）。一次调用的固定开销远大于单字
         * 度量本身，于是"一行溢出文本"要花几十次 shaping 的钱；如果某个
         * 后端的固定开销偏高，这里还会把偏差放大几十倍。
         *
         * cairo_text_extents2_ex 一次就给出逐字节累计宽度（多字节字符的
         * 每个字节带同一个累计值），扫一遍数组即可，且天然按字符对齐 ——
         * 顺带修掉了原来按字节调用 measureText(text+i, 1) 会把 UTF-8 字符
         * 削成半个字节序列去度量的老问题。 */
        SkScalar fWid = 0.0f;
        int i = 0;
        if (nLen > 0)
        {
            std::vector<int> charWid(nLen);
            cairo_text_extents_t extChar;
            cairo_text_extents2_ex(ctx, text, nLen, &extChar, &charWid[0]);
            while (i < nLen && (double)charWid[i] <= (double)maxWidth)
                i++;
            // 截断点必须落在字符边界上（charWid 是按字节索引的）
            while (i > 0 && i < nLen && (((unsigned char)text[i]) & 0xC0) == 0x80)
                i--;
            fWid = (i > 0) ? (SkScalar)charWid[i - 1] : 0.0f;
        }
        if (!(m_uFormat & DT_CALCRECT))
        {
            char *pbuf = new char[i + 3];
            memcpy(pbuf, text, i);
            memcpy(pbuf + i, CH_ELLIPSIS, 3);
            cairo_move_to(ctx, x1, y);
            cairo_show_text2(ctx, pbuf, i + 3);
            delete[] pbuf;
        }
        return fWid + fWidEllipsis;
    }
}

RECT TextLayoutEx::draw()
{
    cairo_font_extents_t font_ext;
    cairo_font_extents(m_ctx, &font_ext);
    float lineSpan = font_ext.ascent + font_ext.descent;

    RECT rcDraw = m_rcBound;

    float x1 = m_rcBound.left, x2 = m_rcBound.right;

    cairo_save(m_ctx);
    if (!(m_uFormat & DT_NOCLIP))
    {
        cairo_rectangle(m_ctx, m_rcBound.left, m_rcBound.top, m_rcBound.right - m_rcBound.left, m_rcBound.bottom - m_rcBound.top);
        cairo_clip(m_ctx);
    }
    cairo_set_line_width(m_ctx, 1);
    cairo_set_dash(m_ctx, nullptr, 0, 0.0);

    float height = m_rcBound.bottom - m_rcBound.top;
    float y = m_rcBound.top + font_ext.ascent;
    if (m_uFormat & DT_SINGLELINE)
    { //单行显示
        // 浮点测量（cairo 的 advance 是小数）输出 extent 时向上取整，
        // 保证返回矩形足以容纳文本；若向下截断，调用方按该宽度开窗
        // 绘制时行尾会超出约束被折行/裁剪
        rcDraw.bottom = rcDraw.top + (LONG)ceil(lineSpan);
        if (m_uFormat & DT_VCENTER)
        {
            y += (height - lineSpan) / 2.0f;
        }
        else if (m_uFormat & DT_BOTTOM)
        {
            y += (height - lineSpan);
        }
        if (m_uFormat & DT_ELLIPSIS)
        { //只支持在行尾增加省略号
            rcDraw.right = rcDraw.left + (LONG)ceil(drawLineEndWithEllipsis(m_ctx, x1, x2, y, 0, m_text.size(), m_rcBound.right - m_rcBound.left));
        }
        else
        {
            rcDraw.right = rcDraw.left + (LONG)ceil(drawLine(m_ctx, x1, x2, y, 0, m_text.size()));
        }
    }
    else
    { //多行显示
        SkScalar maxLineWid = 0;
        int iLine = 0;
        while (iLine < (int)m_lines.size())
        {
            if (y + lineSpan >= m_rcBound.bottom)
                break; // the last visible line
            int iBegin = m_lines[iLine].nOffset;
            int iEnd = iBegin + m_lines[iLine].nLen;
            SkScalar lineWid = drawLine(m_ctx, x1, x2, y, iBegin, iEnd);
            maxLineWid = MAX(maxLineWid, lineWid);
            y += lineSpan + kLineInterval;
            iLine++;
        }
        if (iLine < (int)m_lines.size())
        { // draw the last visible line
            int iBegin = m_lines[iLine].nOffset;
            int iEnd = iBegin + m_lines[iLine].nLen;
            SkScalar lineWid;
            if (m_uFormat & DT_ELLIPSIS)
            { //只支持在行尾增加省略号
                lineWid = drawLineEndWithEllipsis(m_ctx, x1, x2, y, iBegin, iEnd, m_rcBound.right - m_rcBound.left);
            }
            else
            {
                lineWid = drawLine(m_ctx, x1, x2, y, iBegin, iEnd);
            }
            maxLineWid = MAX(maxLineWid, lineWid);
            y += lineSpan;
        }
        rcDraw.right = rcDraw.left + (LONG)ceil(maxLineWid);
        rcDraw.bottom = rcDraw.top + (LONG)ceil(y - font_ext.ascent);
    }
    cairo_restore(m_ctx);
    return rcDraw;
}

void TextLayoutEx::drawText(cairo_t *ctx, const char *text, size_t length, SkScalar x, SkScalar y)
{
    cairo_move_to(ctx, x, y);
    cairo_show_text2(ctx, text, length);
}

SkScalar TextLayoutEx::measureText(cairo_t *ctx, const char *text, size_t length)
{
    cairo_text_extents_t ext;
    cairo_text_extents2(ctx, text, length, &ext);
    return ext.x_advance;
}
