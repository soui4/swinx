#include "drawtext.h"
#include "cairo_show_text2.h"

#define DT_ELLIPSIS (DT_PATH_ELLIPSIS | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS)
#define CH_ELLIPSIS "..."
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))

static size_t breakTextEx(cairo_t *ctx, const char *textD, size_t length, SkScalar maxWidth, int &endLen)
{
    size_t nLineLen = cairo_break_text(ctx, textD, length, maxWidth);
    if (nLineLen == 0)
        return 0;
    endLen = 0;
    int nRet = nLineLen;
    const char *p = textD;
    const char *p2 = textD + nLineLen;
    while (p < p2)
    {
        if (*p == '\r' || *p == '\n')
        {
            nRet = p - textD;
            break;
        }
        p = (const char *)_mbsinc((const uint8_t *)p);
    }
    if (nRet < nLineLen)
    {
        if (p + 1 < p2 && *p == '\r' && *(p + 1) == '\n')
            endLen = 2;
        else if (*p == '\r' || *p == '\n')
            endLen = 1;
    }
    return nRet;
}

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
    if (uFormat & DT_NOPREFIX)
    {
        m_text.resize(length);
        memcpy(m_text.data(), text, length);
    }
    else
    {
        m_prefix.clear();
        std::vector<char> tmp;
        tmp.resize(length);
        memcpy(tmp.data(), text, length);
        const char *p = tmp.data();
        for (int i = 0; i < tmp.size();)
        {
            if (*p == '&' && i + 1 < tmp.size())
            {
                if (tmp[1] == '&')
                {
                    tmp.erase(tmp.begin() + i);
                }
                else
                {
                    tmp.erase(tmp.begin() + i);
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
        m_text = tmp;
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
        while (lineHead < m_text.size())
        {
            int endLen = 0;
            size_t line_len = breakTextEx(m_ctx, text, stop - text, maxWid, endLen);
            if (line_len + endLen == 0)
                break;
            LineInfo info = { lineHead, (int)line_len };
            m_lines.push_back(info);
            text += line_len + endLen;
            lineHead += line_len + endLen;
        };
    }
}

SkScalar TextLayoutEx::drawLine(cairo_t *ctx, SkScalar x1, SkScalar x2, SkScalar y, int iBegin, int iEnd)
{
    const char *text = m_text.data() + iBegin;

    SkScalar nTextWidth = measureText(ctx, text, (iEnd - iBegin));
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
        while (i < m_prefix.size())
        {
            if (m_prefix[i] >= iBegin)
                break;
            i++;
        }

        while (i < m_prefix.size() && m_prefix[i] < iEnd)
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
        return drawLine(ctx, x1, x2, y, iBegin, iEnd);
    }
    else
    {
        SkScalar fWidEllipsis = measureText(ctx, CH_ELLIPSIS, 3);
        maxWidth -= fWidEllipsis;

        int i = 0;
        const char *text = m_text.data() + iBegin;
        SkScalar fWid = 0.0f;
        while (i < (iEnd - iBegin))
        {
            SkScalar fWord = measureText(ctx, text + i, 1);
            if (fWid + fWord > maxWidth)
                break;
            fWid += fWord;
            i++;
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
    cairo_rectangle(m_ctx, m_rcBound.left, m_rcBound.top, m_rcBound.right - m_rcBound.left, m_rcBound.bottom - m_rcBound.top);
    cairo_clip(m_ctx);
    cairo_set_line_width(m_ctx, 1);
    cairo_set_dash(m_ctx, nullptr, 0, 0.0);

    float height = m_rcBound.bottom - m_rcBound.top;
    float y = m_rcBound.top + font_ext.ascent;
    if (m_uFormat & DT_SINGLELINE)
    { //单行显示
        rcDraw.bottom = rcDraw.top + lineSpan;
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
            rcDraw.right = rcDraw.left + drawLineEndWithEllipsis(m_ctx, x1, x2, y, 0, m_text.size(), m_rcBound.right - m_rcBound.left);
        }
        else
        {
            rcDraw.right = rcDraw.left + drawLine(m_ctx, x1, x2, y, 0, m_text.size());
        }
    }
    else
    { //多行显示
        SkScalar maxLineWid = 0;
        int iLine = 0;
        while (iLine < m_lines.size())
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
        if (iLine < m_lines.size())
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
        rcDraw.right = rcDraw.left + maxLineWid;
        rcDraw.bottom = y - font_ext.ascent;
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
