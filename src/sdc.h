#ifndef _SDC_H_
#define _SDC_H_
#include <windows.h>
#include <region.h>
#include <cairo.h>

typedef struct _SDC
{
    HWND hwnd;
    cairo_t *cairo;
    cairo_matrix_t mtx;
    POINT ptOrigin;
    HBITMAP bmp;
    COLORREF crText;
    COLORREF crBk;
    HGDIOBJ pen;
    HGDIOBJ brush;
    HGDIOBJ hfont;
    UINT    uGetFlags;
    int bkMode;
    int nSave;
    UINT textAlign;
    int rop2;
    _SDC(HWND _hwnd);

    ~_SDC();
    int SaveState();
    BOOL RestoreState(int nState);
} * HDC;

#endif //_SDC_H_
