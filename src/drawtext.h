#ifndef _DRAWTEXT_H_
#define _DRAWTEXT_H_

#include <windows.h>
#include <vector>
#include <cairo.h>

#define SkScalar float
class TextLayoutEx {
    enum
    {
        kLineInterval = 0,
    };

  public:
    // not support for DT_PREFIXONLY
    void init(cairo_t *ctx, const char *text, size_t length, const RECT &rc, UINT uFormat);

    RECT draw();

  private:
    SkScalar drawLineEndWithEllipsis(cairo_t *ctx, SkScalar x1, SkScalar x2, SkScalar y, int iBegin, int iEnd, SkScalar maxWidth);

    SkScalar drawLine(cairo_t *ctx, SkScalar x1, SkScalar x2, SkScalar y, int iBegin, int iEnd);

    void buildLines();

    void drawText(cairo_t *ctx, const char *text, size_t length, SkScalar x, SkScalar y);
    SkScalar measureText(cairo_t *ctx, const char *text, size_t length);

  private:
    std::vector<char> m_text;  //文本内容
    std::vector<int> m_prefix; //前缀符索引
    struct LineInfo
    {
        int nOffset;
        int nLen;
    };
    std::vector<LineInfo> m_lines; //分行索引
    UINT m_uFormat;                //显示标志
    RECT m_rcBound;                //限制矩形
    cairo_t *m_ctx;
};

void cairo_draw_text(cairo_t *ctx, const char *text, int len, RECT *box, UINT uFormat);

#endif //_DRAWTEXT_H_
