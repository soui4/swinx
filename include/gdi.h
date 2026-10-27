#ifndef _LINUX_GDI_H_
#define _LINUX_GDI_H_
#include <ctypes.h>
#include <winuser.h>
#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

#define OBJ_PEN         1
#define OBJ_BRUSH       2
#define OBJ_DC          3
#define OBJ_FONT        6
#define OBJ_BITMAP      7
#define OBJ_REGION      8
#define OBJ_PAL         9
#define OBJ_METAFILE    10
#define OBJ_ENHMETAFILE 13

    HGDIOBJ InitGdiObj(int type, void *ptr);
    void *GetGdiObjPtr(HGDIOBJ hgdiobj);
    void SetGdiObjPtr(HGDIOBJ hgdiObj, void *ptr);
    HGDIOBJ RefGdiObj(HGDIOBJ hgdiObj);

    typedef struct _XFORM
    {
        FLOAT eM11;
        FLOAT eM12;
        FLOAT eM21;
        FLOAT eM22;
        FLOAT eDx;
        FLOAT eDy;
    } XFORM, *PXFORM, *LPXFORM;

    typedef struct _FIXED
    {
#ifndef _MAC
        WORD fract;
        short value;
#else
    short value;
    WORD fract;
#endif
    } FIXED;

#define GDI_ERROR (0xFFFFFFFFL)

#define SRCCOPY   (DWORD)0x00CC0020 /* dest = source                   */
#define SRCPAINT  (DWORD)0x00EE0086 /* dest = source OR dest           */
#define SRCAND    (DWORD)0x008800C6 /* dest = source AND dest          */
#define SRCINVERT (DWORD)0x00660046 /* dest = source XOR dest          */
#define DSTINVERT (DWORD)0x00550009 /* dest = (NOT dest)               */

#define PATCOPY   (DWORD)0x00F00021 /* dest = pattern                  */
#define PATPAINT  (DWORD)0x00FB0A09 /* dest = DPSnoo                   */
#define PATINVERT (DWORD)0x005A0049 /* dest = pattern XOR dest         */
#define DSTINVERT (DWORD)0x00550009 /* dest = (NOT dest)               */
#define BLACKNESS (DWORD)0x00000042 /* dest = BLACK                    */
#define WHITENESS (DWORD)0x00FF0062 /* dest = WHITE                    */

    typedef struct _MAT2
    {
        FIXED eM11;
        FIXED eM12;
        FIXED eM21;
        FIXED eM22;
    } MAT2, FAR *LPMAT2;

    typedef struct _GLYPHMETRICS
    {
        UINT gmBlackBoxX;
        UINT gmBlackBoxY;
        POINT gmptGlyphOrigin;
        short gmCellIncX;
        short gmCellIncY;
    } GLYPHMETRICS, FAR *LPGLYPHMETRICS;

    //  GetGlyphOutline constants

#define GGO_METRICS 0
#define GGO_BITMAP  1
#define GGO_NATIVE  2
#define GGO_BEZIER  3

#if (WINVER >= 0x0400)
#define GGO_GRAY2_BITMAP 4
#define GGO_GRAY4_BITMAP 5
#define GGO_GRAY8_BITMAP 6
#define GGO_GLYPH_INDEX  0x0080
#endif /* WINVER >= 0x0400 */

#if (_WIN32_WINNT >= _WIN32_WINNT_WIN2K)
#define GGO_UNHINTED 0x0100
#endif // (_WIN32_WINNT >= _WIN32_WINNT_WIN2K)

#define TT_POLYGON_TYPE 24

#define TT_PRIM_LINE    1
#define TT_PRIM_QSPLINE 2
#define TT_PRIM_CSPLINE 3

    /* GetGlyphOutline(GGO_NATIVE) 返回的 TrueType 轮廓记录。
     * POINTFX 的坐标是 16.16 定点（FIXED），与 Win32 一致。 */
    typedef struct tagPOINTFX
    {
        FIXED x;
        FIXED y;
    } POINTFX, *LPPOINTFX;

    typedef struct tagTTPOLYGONHEADER
    {
        DWORD cb;     /* 本记录（含其后的 TTPOLYCURVE）的总字节数 */
        DWORD dwType; /* TT_POLYGON_TYPE */
        POINTFX pfxStart;
    } TTPOLYGONHEADER, *LPTTPOLYGONHEADER;

    typedef struct tagTTPOLYCURVE
    {
        WORD wType; /* TT_PRIM_LINE / TT_PRIM_QSPLINE / TT_PRIM_CSPLINE */
        WORD cpfx;  /* apfx 中的点数 */
        POINTFX apfx[1];
    } TTPOLYCURVE, *LPTTPOLYCURVE;

/* tmPitchAndFamily flags */
#define TMPF_FIXED_PITCH 0x01
#define TMPF_VECTOR      0x02
#define TMPF_DEVICE      0x08
#define TMPF_TRUETYPE    0x04

#define ANSI_CHARSET        0
#define DEFAULT_CHARSET     1
#define SYMBOL_CHARSET      2
#define SHIFTJIS_CHARSET    128
#define HANGEUL_CHARSET     129
#define HANGUL_CHARSET      129
#define GB2312_CHARSET      134
#define CHINESEBIG5_CHARSET 136
#define OEM_CHARSET         255
#if (WINVER >= 0x0400)
#define JOHAB_CHARSET      130
#define HEBREW_CHARSET     177
#define ARABIC_CHARSET     178
#define GREEK_CHARSET      161
#define TURKISH_CHARSET    162
#define VIETNAMESE_CHARSET 163
#define THAI_CHARSET       222
#define EASTEUROPE_CHARSET 238
#define RUSSIAN_CHARSET    204
#endif // WINVER

/* Font Families */
#define FF_DONTCARE (0 << 4)   /* Don't care or don't know. */
#define FF_ROMAN    (1 << 4)   /* Variable stroke width, serifed. */
                               /* Times Roman, Century Schoolbook, etc. */
#define FF_SWISS (2 << 4)      /* Variable stroke width, sans-serifed. */
                               /* Helvetica, Swiss, etc. */
#define FF_MODERN (3 << 4)     /* Constant stroke width, serifed or sans-serifed. */
                               /* Pica, Elite, Courier, etc. */
#define FF_SCRIPT     (4 << 4) /* Cursive, etc. */
#define FF_DECORATIVE (5 << 4) /* Old English, etc. */

/* Font Weights */
#define FW_DONTCARE   0
#define FW_THIN       100
#define FW_EXTRALIGHT 200
#define FW_LIGHT      300
#define FW_NORMAL     400
#define FW_MEDIUM     500
#define FW_SEMIBOLD   600
#define FW_BOLD       700
#define FW_EXTRABOLD  800
#define FW_HEAVY      900

#define FW_ULTRALIGHT FW_EXTRALIGHT
#define FW_REGULAR    FW_NORMAL
#define FW_DEMIBOLD   FW_SEMIBOLD
#define FW_ULTRABOLD  FW_EXTRABOLD
#define FW_BLACK      FW_HEAVY

#define LF_FACESIZE 32
    typedef struct tagLOGFONTA
    {
        LONG lfHeight;
        LONG lfWidth;
        LONG lfEscapement;
        LONG lfOrientation;
        LONG lfWeight;
        BYTE lfItalic;
        BYTE lfUnderline;
        BYTE lfStrikeOut;
        BYTE lfCharSet;
        BYTE lfOutPrecision;
        BYTE lfClipPrecision;
        BYTE lfQuality;
        BYTE lfPitchAndFamily;
        char lfFaceName[LF_FACESIZE];
    } LOGFONTA, *PLOGFONTA, NEAR *NPLOGFONTA, FAR *LPLOGFONTA;

    typedef struct tagLOGFONTW
    {
        LONG lfHeight;
        LONG lfWidth;
        LONG lfEscapement;
        LONG lfOrientation;
        LONG lfWeight;
        BYTE lfItalic;
        BYTE lfUnderline;
        BYTE lfStrikeOut;
        BYTE lfCharSet;
        BYTE lfOutPrecision;
        BYTE lfClipPrecision;
        BYTE lfQuality;
        BYTE lfPitchAndFamily;
        wchar_t lfFaceName[LF_FACESIZE];
    } LOGFONTW, *PLOGFONTW, NEAR *NPLOGFONTW, FAR *LPLOGFONTW;

#define LF_FULLFACESIZE 64

    /* Structure passed to FONTENUMPROC */
    typedef struct tagENUMLOGFONTA
    {
        LOGFONTA elfLogFont;
        BYTE elfFullName[LF_FULLFACESIZE];
        BYTE elfStyle[LF_FACESIZE];
    } ENUMLOGFONTA, FAR *LPENUMLOGFONTA;
    /* Structure passed to FONTENUMPROC */
    typedef struct tagENUMLOGFONTW
    {
        LOGFONTW elfLogFont;
        WCHAR elfFullName[LF_FULLFACESIZE];
        WCHAR elfStyle[LF_FACESIZE];
    } ENUMLOGFONTW, FAR *LPENUMLOGFONTW;
#ifdef UNICODE
    typedef LOGFONTW LOGFONT;
    typedef ENUMLOGFONTW ENUMLOGFONT;
    typedef LPENUMLOGFONTW LPENUMLOGFONT;
#else
typedef LOGFONTA LOGFONT;
typedef ENUMLOGFONTA ENUMLOGFONT;
typedef LPENUMLOGFONTA LPENUMLOGFONT;
#endif // UNICODE

    /* Non-client area metrics (SystemParametersInfo SPI_GETNONCLIENTMETRICS).
     * These live in gdi.h rather than winuser.h because they embed LOGFONTA /
     * LOGFONTW, and windows.h includes winuser.h *before* gdi.h. */
    typedef struct tagNONCLIENTMETRICSA
    {
        UINT cbSize;
        int iBorderWidth;
        int iScrollWidth;
        int iScrollHeight;
        int iCaptionWidth;
        int iCaptionHeight;
        LOGFONTA lfCaptionFont;
        int iSmCaptionWidth;
        int iSmCaptionHeight;
        LOGFONTA lfSmCaptionFont;
        int iMenuWidth;
        int iMenuHeight;
        LOGFONTA lfMenuFont;
        LOGFONTA lfStatusFont;
        LOGFONTA lfMessageFont;
    } NONCLIENTMETRICSA, *PNONCLIENTMETRICSA, FAR *LPNONCLIENTMETRICSA;

    typedef struct tagNONCLIENTMETRICSW
    {
        UINT cbSize;
        int iBorderWidth;
        int iScrollWidth;
        int iScrollHeight;
        int iCaptionWidth;
        int iCaptionHeight;
        LOGFONTW lfCaptionFont;
        int iSmCaptionWidth;
        int iSmCaptionHeight;
        LOGFONTW lfSmCaptionFont;
        int iMenuWidth;
        int iMenuHeight;
        LOGFONTW lfMenuFont;
        LOGFONTW lfStatusFont;
        LOGFONTW lfMessageFont;
    } NONCLIENTMETRICSW, *PNONCLIENTMETRICSW, FAR *LPNONCLIENTMETRICSW;

#ifdef UNICODE
    typedef NONCLIENTMETRICSW NONCLIENTMETRICS;
    typedef PNONCLIENTMETRICSW PNONCLIENTMETRICS;
    typedef LPNONCLIENTMETRICSW LPNONCLIENTMETRICS;
#else
typedef NONCLIENTMETRICSA NONCLIENTMETRICS;
typedef PNONCLIENTMETRICSA PNONCLIENTMETRICS;
typedef LPNONCLIENTMETRICSA LPNONCLIENTMETRICS;
#endif // UNICODE

    /* Logical Brush (or Pattern) */
    typedef struct tagLOGBRUSH
    {
        UINT lbStyle;
        COLORREF lbColor;
        UINT_PTR lbHatch; // Sundown: lbHatch could hold a HANDLE
    } LOGBRUSH, *PLOGBRUSH, NEAR *NPLOGBRUSH, FAR *LPLOGBRUSH;

#define BS_SOLID         0
#define BS_NULL          1
#define BS_HOLLOW        BS_NULL
#define BS_HATCHED       2
#define BS_PATTERN       3
#define BS_INDEXED       4
#define BS_DIBPATTERN    5
#define BS_DIBPATTERNPT  6
#define BS_PATTERN8X8    7
#define BS_DIBPATTERN8X8 8
#define BS_MONOPATTERN   9
#define BS_BRUSH         10

    HPEN WINAPI ExtCreatePen(DWORD iPenStyle, DWORD cWidth, CONST LOGBRUSH *plbrush, DWORD cStyle, CONST DWORD *pstyle);

    int WINAPI GetObjectA(HGDIOBJ h, int c, LPVOID pv);
    int WINAPI GetObjectW(HGDIOBJ h, int c, LPVOID pv);
#ifdef UNICODE
#define GetObject GetObjectW
#else
#define GetObject GetObjectA
#endif // UNICODE

    int WINAPI GetObjectType(HGDIOBJ hgdiobj);
    /* Logical Pen */
    typedef struct tagLOGPEN
    {
        UINT lopnStyle;
        POINT lopnWidth;
        COLORREF lopnColor;
    } LOGPEN, *PLOGPEN, NEAR *NPLOGPEN, FAR *LPLOGPEN;

    HPEN WINAPI CreatePen(int iStyle, int cWidth, COLORREF color);
    HPEN WINAPI CreatePenIndirect(CONST LOGPEN *plpen);

    HFONT WINAPI CreateFontIndirectA(CONST LOGFONTA *lplf);
    HFONT WINAPI CreateFontIndirectW(CONST LOGFONTW *lplf);
    HFONT WINAPI CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
    HFONT WINAPI CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName);

#ifdef UNICODE
#define CreateFont         CreateFontW
#define CreateFontIndirect CreateFontIndirectW
#else
#define CreateFont         CreateFontA
#define CreateFontIndirect CreateFontIndirectA
#endif // UNICODE

#pragma pack(push, 1)
    /* Bitmap Header Definition */
    typedef struct tagBITMAP
    {
        LONG bmType;
        LONG bmWidth;
        LONG bmHeight;
        LONG bmWidthBytes;
        WORD bmPlanes;
        WORD bmBitsPixel;
        LPVOID bmBits;
    } BITMAP, *PBITMAP, NEAR *NPBITMAP, FAR *LPBITMAP;

    typedef struct tagBITMAPINFOHEADER
    {
        DWORD biSize;
        LONG biWidth;
        LONG biHeight;
        WORD biPlanes;
        WORD biBitCount;
        DWORD biCompression;
        DWORD biSizeImage;
        LONG biXPelsPerMeter;
        LONG biYPelsPerMeter;
        DWORD biClrUsed;
        DWORD biClrImportant;
    } BITMAPINFOHEADER, FAR *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

    typedef LONG FXPT16DOT16, *LPFXPT16DOT16;
    typedef LONG FXPT2DOT30, *LPFXPT2DOT30;
    typedef LONG LCSCSTYPE;
    typedef LONG LCSGAMUTMATCH;

    typedef struct tagCIEXYZ
    {
        FXPT2DOT30 ciexyzX;
        FXPT2DOT30 ciexyzY;
        FXPT2DOT30 ciexyzZ;
    } CIEXYZ, *LPCIEXYZ;

    typedef struct tagCIEXYZTRIPLE
    {
        CIEXYZ ciexyzRed;
        CIEXYZ ciexyzGreen;
        CIEXYZ ciexyzBlue;
    } CIEXYZTRIPLE, *LPCIEXYZTRIPLE;

    typedef struct
    {
        DWORD bV4Size;
        LONG bV4Width;
        LONG bV4Height;
        WORD bV4Planes;
        WORD bV4BitCount;
        DWORD bV4V4Compression;
        DWORD bV4SizeImage;
        LONG bV4XPelsPerMeter;
        LONG bV4YPelsPerMeter;
        DWORD bV4ClrUsed;
        DWORD bV4ClrImportant;
        DWORD bV4RedMask;
        DWORD bV4GreenMask;
        DWORD bV4BlueMask;
        DWORD bV4AlphaMask;
        DWORD bV4CSType;
        CIEXYZTRIPLE bV4Endpoints;
        DWORD bV4GammaRed;
        DWORD bV4GammaGreen;
        DWORD bV4GammaBlue;
    } BITMAPV4HEADER, *PBITMAPV4HEADER;

    typedef struct
    {
        DWORD bV5Size;
        LONG bV5Width;
        LONG bV5Height;
        WORD bV5Planes;
        WORD bV5BitCount;
        DWORD bV5Compression;
        DWORD bV5SizeImage;
        LONG bV5XPelsPerMeter;
        LONG bV5YPelsPerMeter;
        DWORD bV5ClrUsed;
        DWORD bV5ClrImportant;
        DWORD bV5RedMask;
        DWORD bV5GreenMask;
        DWORD bV5BlueMask;
        DWORD bV5AlphaMask;
        DWORD bV5CSType;
        CIEXYZTRIPLE bV5Endpoints;
        DWORD bV5GammaRed;
        DWORD bV5GammaGreen;
        DWORD bV5GammaBlue;
        DWORD bV5Intent;
        DWORD bV5ProfileData;
        DWORD bV5ProfileSize;
        DWORD bV5Reserved;
    } BITMAPV5HEADER, *LPBITMAPV5HEADER, *PBITMAPV5HEADER;

    typedef struct tagBITMAPFILEHEADER
    {
        WORD bfType;
        DWORD bfSize;
        WORD bfReserved1;
        WORD bfReserved2;
        DWORD bfOffBits;
    } BITMAPFILEHEADER, *PBITMAPFILEHEADER;

    typedef struct
    {
        DWORD bcSize;
        WORD bcWidth;
        WORD bcHeight;
        WORD bcPlanes;
        WORD bcBitCount;
    } BITMAPCOREHEADER, *PBITMAPCOREHEADER, *LPBITMAPCOREHEADER;

    typedef struct tagRGBTRIPLE
    {
        BYTE rgbtBlue;
        BYTE rgbtGreen;
        BYTE rgbtRed;
    } RGBTRIPLE;

    typedef struct
    {
        BITMAPCOREHEADER bmciHeader;
        RGBTRIPLE bmciColors[1];
    } BITMAPCOREINFO, *PBITMAPCOREINFO, *LPBITMAPCOREINFO;

#pragma pack(pop)

/* constants for the biCompression field */
#define BI_RGB       0L
#define BI_RLE8      1L
#define BI_RLE4      2L
#define BI_BITFIELDS 3L
#define BI_JPEG      4L
#define BI_PNG       5L

    typedef struct tagBITMAPINFO
    {
        BITMAPINFOHEADER bmiHeader;
        RGBQUAD bmiColors[1];
    } BITMAPINFO, FAR *LPBITMAPINFO, *PBITMAPINFO;

    typedef struct tagDIBSECTION
    {
        BITMAP dsBm;
        BITMAPINFOHEADER dsBmih;
        DWORD dsBitfields[3];
        HANDLE dshSection;
        DWORD dsOffset;
    } DIBSECTION, *PDIBSECTION, *LPDIBSECTION;

    typedef struct tagGRADIENTITEM
    {
        COLORREF cr;
        float pos;
    } GRADIENTITEM, FAR *LPGRADIENTITEM, *PGRADIENTITEM;

    typedef enum tagGRADIENTTYPE
    {
        grad_linear = 0, /*<linear gradient*/
        grad_radial,     /*<radial gradient*/
    } GRADIENTTYPE;

    typedef struct tagGRADIENTINFO
    {
        GRADIENTTYPE type;
        union {
            float angle; // for linear
            struct
            {
                float radius;  // radical
                float centerX; // 0.0 -> 1.0, 0.5 is center of x
                float centerY; // 0.0 -> 1.0, 0.5 is center of y
            } radial;
            struct
            {
                BOOL bFullArc; // true(default) indicator gradient is rendered for full circle.
                float centerX; // 0.0 -> 1.0, 0.5 is center of x
                float centerY; // 0.0 -> 1.0, 0.5 is center of y
            } sweep;           // sweep;
        };
    } GRADIENTINFO, FAR *LPGRADIENTINFO, *PGRADIENTINFO;

    typedef enum tagTILEMODE
    {
        /** replicate the edge color if the shader draws outside of its
         *  original bounds
         */
        kTileMode_Clamp = 0,

        /** repeat the shader's image horizontally and vertically */
        kTileMode_Repeat,

        /** repeat the shader's image horizontally and vertically, alternating
         *  mirror images so that adjacent images always seam
         */
        kTileMode_Mirror,
    } TILEMODE;

#define DIB_RGB_COLORS 0 /* color table in RGBs */
#define DIB_PAL_COLORS 1 /* color table in palette indices */

#define CBM_INIT 0x04L /* initialize bitmap */

    /* StretchBlt() modes */
#define BLACKONWHITE      1
#define WHITEONBLACK      2
#define COLORONCOLOR      3
#define HALFTONE          4
#define MAXSTRETCHBLTMODE 4

#define STRETCH_ANDSCANS    BLACKONWHITE
#define STRETCH_ORSCANS     WHITEONBLACK
#define STRETCH_DELETESCANS COLORONCOLOR
#define STRETCH_HALFTONE    HALFTONE

    HBITMAP WINAPI CreateBitmap(int nWidth,         // bitmap width, in pixels
                                int nHeight,        // bitmap height, in pixels
                                UINT cPlanes,       // number of color planes
                                UINT cBitsPerPel,   // number of bits to identify color
                                CONST VOID *lpvBits // color data array
    );
    HBITMAP WINAPI CreateDIBitmap(HDC hdc, CONST BITMAPINFOHEADER *pbmih, DWORD flInit, CONST VOID *pjBits, CONST BITMAPINFO *pbmi, UINT iUsage);
    HBRUSH WINAPI CreateDIBPatternBrush(HGLOBAL h, UINT iUsage);
    HBRUSH WINAPI CreateDIBPatternBrushPt(CONST VOID *lpPackedDIB, UINT iUsage);
    HBRUSH WINAPI CreateSolidBrush(COLORREF color);
    HBRUSH WINAPI CreatePatternBrush(HBITMAP bmp);
    HBRUSH WINAPI CreatePatternBrush2(HBITMAP bmp, TILEMODE tileMode);
    HBRUSH WINAPI CreateGradientBrush(const GRADIENTITEM *pGradients, int nCount, const GRADIENTINFO *info, BYTE byAlpha, TILEMODE tileMode);

    HBITMAP WINAPI CreateDIBSection(HDC hdc, CONST BITMAPINFO *lpbmi, UINT usage, VOID **ppvBits, HANDLE hSection, DWORD offset);
    HBITMAP WINAPI CreateDIBSectionEx(int bitsPixel, int wid, int hei, int stride, VOID *pvBits);
    BOOL WINAPI UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits);
    void WINAPI MarkPixmapDirty(HBITMAP bmp);

    HDC WINAPI CreateCompatibleDC(HDC hdc);
    BOOL WINAPI DeleteDC(HDC hdc);

/* Background Modes */
#define TRANSPARENT 1
#define OPAQUE      2
#define BKMODE_LAST 2

    /* Graphics Modes */

#define GM_COMPATIBLE 1
#define GM_ADVANCED   2
#define GM_LAST       2

    int WINAPI SetBkMode(HDC hdc, int mode);
    int WINAPI GetBkMode(HDC hdc);
    int WINAPI SetGraphicsMode(HDC hdc, int iMode);
    int WINAPI GetGraphicsMode(HDC hdc);

    COLORREF WINAPI GetBkColor(HDC hdc);
    COLORREF WINAPI SetBkColor(HDC hdc, COLORREF cr);

    HBITMAP WINAPI CreateCompatibleBitmap(HDC hdc, int cx, int cy);
    HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ h);
    BOOL WINAPI DeleteObject(HGDIOBJ ho);
    int WINAPI SaveDC(HDC hdc);
    int WINAPI SelectClipRgn(HDC hdc, HRGN hrgn);
    int WINAPI ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode);
    int WINAPI ExcludeClipRect(HDC hdc, int left, int top, int right, int bottom);
    int WINAPI IntersectClipRect(HDC hdc, int left, int top, int right, int bottom);
    BOOL WINAPI RestoreDC(HDC hdc, int nSavedDC);

    BOOL WINAPI InvertRgn(HDC hdc,  // handle to device context
                          HRGN hrgn // handle to region to be inverted
    );
    int WINAPI GetClipBox(HDC hdc, LPRECT lprect);
    int WINAPI GetClipRgn(HDC hdc, HRGN hrgn);

    BOOL WINAPI FillRgn(HDC hdc,   // handle to device context
                        HRGN hrgn, // handle to region to be filled
                        HBRUSH hbr // handle to brush used to fill the region
    );

    BOOL WINAPI PaintRgn(HDC hdc,  // handle to device context
                         HRGN hrgn // handle to region to be painted
    );

    BOOL WINAPI FrameRgn(HDC hdc,    // handle to device context
                         HRGN hrgn,  // handle to region to be framed
                         HBRUSH hbr, // handle to brush used to draw border
                         int nWidth, // width of region frame
                         int nHeight // height of region frame
    );

    BOOL WINAPI DrawFocusRect(HDC hDC,         // handle to device context
                              CONST RECT *lprc // logical coordinates
    );

    HGDIOBJ WINAPI GetCurrentObject(HDC hdc, UINT type);
    int WINAPI GetDIBits(HDC hdc, HBITMAP hbm, UINT start, UINT cLines, LPVOID lpvBits, LPBITMAPINFO lpbmi, UINT usage); // SAL actual size of lpbmi is computed from structure elements

    HICON WINAPI CreateIconIndirect(PICONINFO iconinfo);

    BOOL WINAPI BitBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, DWORD rop);

    BOOL WINAPI StretchBlt(HDC hdc, int x, int y, int cx, int cy, HDC hdcSrc, int x1, int y1, int cx2, int cy2, DWORD rop);

    BOOL WINAPI PatBlt(_In_ HDC hdc, _In_ int x, _In_ int y, _In_ int w, _In_ int h, _In_ DWORD rop);

    BOOL WINAPI TransparentBlt(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, UINT crTransparent);

    BOOL WINAPI DrawIcon(HDC hdc, INT x, INT y, HICON hIcon);

    BOOL WINAPI DrawIconEx(HDC hdc, INT x0, INT y0, HICON icon, INT width, INT height, UINT step, HBRUSH brush, UINT flags);

    INT WINAPI StretchDIBits(HDC hdc, INT x_dst, INT y_dst, INT width_dst, INT height_dst, INT x_src, INT y_src, INT width_src, INT height_src, const void *bits, const BITMAPINFO *bmi, UINT coloruse, DWORD rop);

    int WINAPI DrawTextA(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format);
    int WINAPI DrawTextW(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);

    BOOL WINAPI GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
    BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl);
#define GetTextExtentPointA GetTextExtentPoint32A
#define GetTextExtentPointW GetTextExtentPoint32W

    BOOL WINAPI GetTextExtentExPointA(HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize);
    BOOL WINAPI GetTextExtentExPointW(HDC hdc, LPCWSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize);

#ifdef UNICODE
#define GetTextExtentPoint   GetTextExtentPoint32W
#define GetTextExtentExPoint GetTextExtentExPointW
#define DrawText             DrawTextW
#define GetTextExtentPoint32 GetTextExtentPoint32W
#else
#define GetTextExtentPoint   GetTextExtentPoint32A
#define GetTextExtentExPoint GetTextExtentExPointA
#define DrawText             DrawTextA
#define GetTextExtentPoint32 GetTextExtentPoint32A
#endif // UNICODE

/* Stock Logical Objects */
#define WHITE_BRUSH         0
#define LTGRAY_BRUSH        1
#define GRAY_BRUSH          2
#define DKGRAY_BRUSH        3
#define BLACK_BRUSH         4
#define NULL_BRUSH          5
#define HOLLOW_BRUSH        NULL_BRUSH
#define WHITE_PEN           6
#define BLACK_PEN           7
#define NULL_PEN            8
#define OEM_FIXED_FONT      10
#define ANSI_FIXED_FONT     11
#define ANSI_VAR_FONT       12
#define SYSTEM_FONT         13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE     15
#define SYSTEM_FIXED_FONT   16
#define DEFAULT_GUI_FONT    17
#define NULL_BITMAP         20

    HGDIOBJ WINAPI GetStockObject(int i);

    COLORREF WINAPI GetSysColor(int i);
    HBRUSH WINAPI GetSysColorBrush(int i);
    HPEN WINAPI GetSysColorPen(int i);

    BOOL WINAPI Rectangle(HDC hdc, int left, int top, int right, int bottom);

    BOOL WINAPI RoundRect(HDC hdc, int left, int top, int right, int bottom, int width, int height);

    BOOL WINAPI Polyline(HDC hdc, CONST POINT *apt, int cpt);
    BOOL WINAPI PolyDraw(HDC hdc, LPPOINT lppt, LPBYTE lpbTypes, int cpt);

    BOOL WINAPI PolyBezier(HDC hdc, const POINT *apt, DWORD cpt);

    BOOL WINAPI PolyBezierTo(HDC hdc, const POINT *apt, DWORD cpt);

    int WINAPI SetPolyFillMode(HDC hdc, int mode);

    BOOL WINAPI AlphaBlend(HDC hdcDest, int xoriginDest, int yoriginDest, int wDest, int hDest, HDC hdcSrc, int xoriginSrc, int yoriginSrc, int wSrc, int hSrc, BLENDFUNCTION ftn);

#ifndef ULW_ALPHA
#define ULW_ALPHA 0x00000002
#endif
    // layered windows have no meaning on a virtual-window platform; the
    // freertos implementation always returns FALSE
    BOOL WINAPI UpdateLayeredWindow(HWND hWnd, HDC hdcDst, POINT *pptDst, SIZE *psize, HDC hdcSrc, POINT *pptSrc, COLORREF crKey, BLENDFUNCTION *pblend, DWORD dwFlags);

#define EXPEND_MODE_NONE    0
#define EXPEND_MODE_STRETCH 1
#define EXPEND_MODE_TILE    2

#define FILTER_NONE   0
#define FILTER_FAST   1
#define FILTER_MIDIUM 2
#define FILTER_BEST   3
    BOOL WINAPI DrawBitmapEx(HDC hdc, LPCRECT pRcDest, HBITMAP bmp, LPCRECT pRcSrc, UINT expendMode, BYTE byAlpha);

    BOOL WINAPI DrawBitmap9Patch(HDC hdc, LPCRECT pRcDest, HBITMAP hBmp, LPCRECT pRcSrc, LPCRECT pRcSourMargin, UINT expendMode, BYTE byAlpha);

    BOOL WINAPI TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c);
    BOOL WINAPI TextOutW(HDC hdc, int x, int y, LPCWSTR lpString, int c);

    LONG WINAPI TabbedTextOutA(HDC hDC,                         // handle to DC
                               int X,                           // x-coord of start
                               int Y,                           // y-coord of start
                               LPCSTR lpString,                 // character string
                               int nCount,                      // number of characters
                               int nTabPositions,               // number of tabs in array
                               CONST LPINT lpnTabStopPositions, // array of tab positions
                               int nTabOrigin                   // start of tab expansion
    );
    LONG WINAPI TabbedTextOutW(HDC hDC,                         // handle to DC
                               int X,                           // x-coord of start
                               int Y,                           // y-coord of start
                               LPCWSTR lpString,                // character string
                               int nCount,                      // number of characters
                               int nTabPositions,               // number of tabs in array
                               CONST LPINT lpnTabStopPositions, // array of tab positions
                               int nTabOrigin                   // start of tab expansion
    );

    DWORD WINAPI GetTabbedTextExtentA(HDC hDC,                        // handle to DC
                                      LPCSTR lpString,                // character string
                                      int nCount,                     // number of characters
                                      int nTabPositions,              // number of tab positions
                                      CONST LPINT lpnTabStopPositions // array of tab positions
    );
    DWORD WINAPI GetTabbedTextExtentW(HDC hDC,                        // handle to DC
                                      LPCWSTR lpString,               // character string
                                      int nCount,                     // number of characters
                                      int nTabPositions,              // number of tab positions
                                      CONST LPINT lpnTabStopPositions // array of tab positions
    );
#ifdef UNICODE
#define TextOut             TextOutW
#define TabbedTextOut       TabbedTextOutW
#define GetTabbedTextExtent GetTabbedTextExtentW
#else
#define TextOut             TextOutA
#define TabbedTextOut       TabbedTextOutA
#define GetTabbedTextExtent GetTabbedTextExtentA
#endif // UNICODE

    /* 映射模式：swinx 只支持 MM_TEXT（逻辑单位 = 设备像素）。 */
#define MM_TEXT 1
    int WINAPI SetMapMode(HDC hdc, int iMode);
    int WINAPI GetMapMode(HDC hdc);

    BOOL WINAPI SetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt);
    BOOL WINAPI GetViewportOrgEx(HDC hdc, LPPOINT lpPoint);
    BOOL WINAPI OffsetViewportOrgEx(HDC hdc, int x, int y, LPPOINT lppt);
    BOOL WINAPI SetWindowOrgEx(HDC hdc,        // handle to device context
                               int X,          // new x-coordinate of window origin
                               int Y,          // new y-coordinate of window origin
                               LPPOINT lpPoint // original window origin
    );
    BOOL WINAPI SetWindowExtEx(HDC hdc,      // handle to device context
                               int nXExtent, // new horizontal window extent
                               int nYExtent, // new vertical window extent
                               LPSIZE lpSize // original window extent
    );
    int WINAPI FillRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr);
    int WINAPI ClearRect(HDC hdc, const RECT *lprc, COLORREF cr);
    int WINAPI FrameRect(HDC hDC, CONST RECT *lprc, HBRUSH hbr);

    BOOL WINAPI InvertRect(HDC hDC, CONST RECT *lprc);

    BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT lpPoint);
    BOOL WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT lpPoint);
    BOOL WINAPI LineTo(HDC hdc,   // device context handle
                       int nXEnd, // x-coordinate of ending point
                       int nYEnd  // y-coordinate of ending point
    );

    BOOL WINAPI Ellipse(HDC hdc, int left, int top, int right, int bottom);

    BOOL WINAPI Pie(HDC hdc, int left, int top, int right, int bottom, int xr1, int yr1, int xr2, int yr2);

    BOOL WINAPI Arc(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);

    BOOL WINAPI Chord(HDC hdc, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);

    BOOL WINAPI GetWorldTransform(HDC hdc, LPXFORM lpxf);
    BOOL WINAPI SetWorldTransform(HDC hdc, CONST XFORM *lpxf);
/* xform stuff */
#define MWT_IDENTITY      1
#define MWT_LEFTMULTIPLY  2
#define MWT_RIGHTMULTIPLY 3

#define MWT_MIN MWT_IDENTITY
#define MWT_MAX MWT_RIGHTMULTIPLY

    BOOL WINAPI ModifyWorldTransform(HDC hdc,              // handle to device context
                                     CONST XFORM *lpXform, // transformation data
                                     DWORD iMode           // modification mode
    );

    enum
    {
        R2_BLACK = 1,
        R2_NOTMERGEPEN,
        R2_MASKNOTPEN,
        R2_NOTCOPYPEN,
        R2_MASKPENNOT,
        R2_NOT,
        R2_XORPEN,
        R2_NOTMASKPEN,
        R2_MASKPEN,
        R2_NOTXORPEN,
        R2_NOP,
        R2_MERGENOTPEN,
        R2_COPYPEN,
        R2_MERGEPENNOT,
        R2_MERGEPEN,
        R2_WHITE,

        R2_EXT_CLEAR,
        R2_EXT_SOURCE,
        R2_EXT_OVER,
        R2_EXT_IN,
        R2_EXT_OUT,
        R2_EXT_ATOP,
        R2_EXT_DEST,
        R2_EXT_DEST_OVER,
        R2_EXT_DEST_IN,
        R2_EXT_DEST_OUT,
        R2_EXT_DEST_ATOP,
        R2_EXT_XOR,
        R2_EXT_ADD,
        R2_EXT_SATURATE,

        R2_LAST
    };

    int WINAPI SetROP2(HDC hdc, int rop2);

    COLORREF WINAPI SetTextColor(HDC hdc, COLORREF color);
    COLORREF WINAPI GetTextColor(HDC hdc);

    BOOL WINAPI SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt);
    BOOL WINAPI GetBrushOrgEx(HDC hdc, LPPOINT lppt);

    typedef USHORT COLOR16;

    typedef struct _TRIVERTEX
    {
        LONG x;
        LONG y;
        COLOR16 Red;
        COLOR16 Green;
        COLOR16 Blue;
        COLOR16 Alpha;
    } TRIVERTEX, *PTRIVERTEX, *LPTRIVERTEX;

    typedef struct _GRADIENT_RECT
    {
        ULONG UpperLeft;
        ULONG LowerRight;
    } GRADIENT_RECT, *PGRADIENT_RECT;

#define GRADIENT_FILL_RECT_H   0x00000000
#define GRADIENT_FILL_RECT_V   0x00000001
#define GRADIENT_FILL_TRIANGLE 0x00000002
#define GRADIENT_FILL_OP_FLAG  0x000000ff

    BOOL WINAPI GradientFill(HDC hdc, TRIVERTEX *pVertices, ULONG nVertices, void *pMesh, ULONG nMeshElements, DWORD dwMode);

/* types of LoadImage */
#define IMAGE_BITMAP      0
#define IMAGE_ICON        1
#define IMAGE_CURSOR      2
#define IMAGE_ENHMETAFILE 3

/* loadflags to LoadImage */
#define LR_DEFAULTCOLOR     0x0000
#define LR_MONOCHROME       0x0001
#define LR_COLOR            0x0002
#define LR_COPYRETURNORG    0x0004
#define LR_COPYDELETEORG    0x0008
#define LR_LOADFROMFILE     0x0010
#define LR_LOADTRANSPARENT  0x0020
#define LR_DEFAULTSIZE      0x0040
#define LR_VGACOLOR         0x0080
#define LR_LOADMAP3DCOLORS  0x1000
#define LR_CREATEDIBSECTION 0x2000
#define LR_COPYFROMRESOURCE 0x4000
#define LR_SHARED           0x8000

/* Flags for DrawIconEx.  */
#define DI_MASK        0x0001
#define DI_IMAGE       0x0002
#define DI_NORMAL      (DI_MASK | DI_IMAGE)
#define DI_COMPAT      0x0004
#define DI_DEFAULTSIZE 0x0008
#define DI_NOMIRROR    0x0010

    /* Device parameters for GetDeviceCaps() */
#define DRIVERVERSION 0
#define TECHNOLOGY    2
#define HORZSIZE      4
#define VERTSIZE      6
#define HORZRES       8
#define VERTRES       10
#define BITSPIXEL     12
#define PLANES        14
#define NUMBRUSHES    16
#define NUMPENS       18
#define NUMMARKERS    20
#define NUMFONTS      22
#define NUMCOLORS     24
#define PDEVICESIZE   26
#define CURVECAPS     28
#define LINECAPS      30
#define POLYGONALCAPS 32
#define TEXTCAPS      34
#define CLIPCAPS      36
#define RASTERCAPS    38
#define ASPECTX       40
#define ASPECTY       42
#define ASPECTXY      44
#define LOGPIXELSX    88
#define LOGPIXELSY    90
#define CAPS1         94
#define SIZEPALETTE   104
#define NUMRESERVED   106
#define COLORRES      108

/* RASTERCAPS bits returned by GetDeviceCaps(hdc, RASTERCAPS). */
#define RC_BITBLT       0x0001
#define RC_BANDING      0x0002
#define RC_SCALING      0x0004
#define RC_BITMAP64     0x0008
#define RC_GLYPHINDEX   0x0010
#define RC_DIBTODEV     0x0080
#define RC_BIGFONT      0x0400
#define RC_STRETCHBLT   0x0800
#define RC_FLOODFILL    0x1000
#define RC_STRETCHDIB   0x2000
#define RC_OP_DX_OUTPUT 0x4000
#define RC_DEVBITS      0x8000

/* Device Technologies */
#define DT_PLOTTER    0 /* Vector plotter                   */
#define DT_RASDISPLAY 1 /* Raster display                   */
#define DT_RASPRINTER 2 /* Raster printer                   */
#define DT_RASCAMERA  3 /* Raster camera                    */
#define DT_CHARSTREAM 4 /* Character-stream, PLP            */
#define DT_METAFILE   5 /* Metafile, VDM                    */
#define DT_DISPFILE   6 /* Display-file                     */

#define PHYSICALWIDTH   110
#define PHYSICALHEIGHT  111
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113
#define SCALINGFACTORX  114
#define SCALINGFACTORY  115
#define VREFRESH        116
#define DESKTOPVERTRES  117
#define DESKTOPHORZRES  118
#define BLTALIGNMENT    119
#define SHADEBLENDCAPS  120
#define COLORMGMTCAPS   121

    int WINAPI GetDeviceCaps(HDC hdc, int cap);

    /* 冲刷 GDI 的延迟绘制队列。swinx 的绘制是立即的，直接返回成功。 */
    BOOL WINAPI GdiFlush(void);

    /* 打印机的转义接口。swinx 没有打印后端：QUERYESCSUPPORT 之外的请求
     * 一律按「不支持」处理（返回 <= 0），与没有该能力的显示 DC 一致。 */
#define QUERYESCSUPPORT 8
    int WINAPI ExtEscape(HDC hdc, int nEscape, int cbInput, LPCSTR lpszInData, int cbOutput, LPSTR lpszOutData);

    HANDLE WINAPI LoadImageW(HINSTANCE hinst, LPCWSTR name, UINT type, INT desiredx, INT desiredy, UINT loadflags);
    HANDLE WINAPI LoadImageA(HINSTANCE hinst, LPCSTR name, UINT type, INT desiredx, INT desiredy, UINT loadflags);

#ifdef _UNICODE
#define LoadImage LoadImageW
#else
#define LoadImage LoadImageA
#endif

    HANDLE WINAPI LoadImageBuf(const void *buf, UINT length, UINT type, INT desiredx, INT desiredy, UINT loadflags);

    HBITMAP WINAPI CreateBitmap(INT width, INT height, UINT planes, UINT bpp, const void *bits);

    int WINAPI SetStretchBltMode(HDC hdc, int mode);

    HICON WINAPI CreateIconIndirect(PICONINFO piconinfo);

    BOOL WINAPI GetIconInfo(HICON hIcon, PICONINFO piconinfo);

    BOOL WINAPI DrawIcon(HDC hDC, int X, int Y, HICON hIcon);

    BOOL WINAPI DrawIconEx(HDC hdc, int xLeft, int yTop, HICON hIcon, int cxWidth, int cyWidth, UINT istepIfAniCur, HBRUSH hbrFlickerFreeDraw, UINT diFlags);

    BOOL WINAPI DestroyIcon(HICON hIcon);

    HICON WINAPI CreateIconFromResourceEx(LPBYTE bits, UINT cbSize, BOOL bIcon, DWORD dwVersion, INT width, INT height, UINT cFlag);

    INT WINAPI LookupIconIdFromDirectoryEx(LPBYTE xdir, BOOL bIcon, INT width, INT height, UINT cFlag);

    BOOL WINAPI UpdateDIBPixmap(HBITMAP bmp, int wid, int hei, int bitsPixel, int stride, CONST VOID *pjBits);

    typedef struct tagTEXTMETRICA
    {
        LONG tmHeight;
        LONG tmAscent;
        LONG tmDescent;
        LONG tmInternalLeading;
        LONG tmExternalLeading;
        LONG tmAveCharWidth;
        LONG tmMaxCharWidth;
        LONG tmWeight;
        LONG tmOverhang;
        LONG tmDigitizedAspectX;
        LONG tmDigitizedAspectY;
        BYTE tmFirstChar;
        BYTE tmLastChar;
        BYTE tmDefaultChar;
        BYTE tmBreakChar;
        BYTE tmItalic;
        BYTE tmUnderlined;
        BYTE tmStruckOut;
        BYTE tmPitchAndFamily;
        BYTE tmCharSet;
    } TEXTMETRICA, *PTEXTMETRICA, NEAR *NPTEXTMETRICA, FAR *LPTEXTMETRICA;
    typedef struct tagTEXTMETRICW
    {
        LONG tmHeight;
        LONG tmAscent;
        LONG tmDescent;
        LONG tmInternalLeading;
        LONG tmExternalLeading;
        LONG tmAveCharWidth;
        LONG tmMaxCharWidth;
        LONG tmWeight;
        LONG tmOverhang;
        LONG tmDigitizedAspectX;
        LONG tmDigitizedAspectY;
        WCHAR tmFirstChar;
        WCHAR tmLastChar;
        WCHAR tmDefaultChar;
        WCHAR tmBreakChar;
        BYTE tmItalic;
        BYTE tmUnderlined;
        BYTE tmStruckOut;
        BYTE tmPitchAndFamily;
        BYTE tmCharSet;
    } TEXTMETRICW, *PTEXTMETRICW, NEAR *NPTEXTMETRICW, FAR *LPTEXTMETRICW;

    BOOL WINAPI GetTextMetricsW(HDC hdc, TEXTMETRICW *txtMetric);
    BOOL WINAPI GetTextMetricsA(HDC hdc, TEXTMETRICA *txtMetric);

    int WINAPI GetTextFaceA(HDC hdc,         // handle to DC
                            int nCount,      // length of typeface name buffer
                            LPSTR lpFaceName // typeface name buffer);
    );

    int WINAPI GetTextFaceW(HDC hdc,          // handle to DC
                            int nCount,       // length of typeface name buffer
                            LPWSTR lpFaceName // typeface name buffer);
    );

#ifdef UNICODE
#define TEXTMETRIC     TEXTMETRICW
#define GetTextMetrics GetTextMetricsW
#define GetTextFace    GetTextFaceW
#else
#define TEXTMETRIC     TEXTMETRICA
#define GetTextMetrics GetTextMetricsA
#define GetTextFace    GetTextFaceA
#endif // UNICODE

#define PAN_CULTURE_LATIN 0

    typedef struct tagPANOSE
    {
        BYTE bFamilyType;
        BYTE bSerifStyle;
        BYTE bWeight;
        BYTE bProportion;
        BYTE bContrast;
        BYTE bStrokeVariation;
        BYTE bArmStyle;
        BYTE bLetterform;
        BYTE bMidline;
        BYTE bXHeight;
    } PANOSE, *LPPANOSE;

    BOOL WINAPI Polygon_Priv(HDC hdc, const POINT *apt, int cpt);

#ifndef MacPolygon
#define Polygon Polygon_Priv
#endif // MacPolygon

#define TA_NOUPDATECP 0
#define TA_UPDATECP   1

#define TA_LEFT   0
#define TA_RIGHT  2
#define TA_CENTER 6

#define TA_TOP      0
#define TA_BOTTOM   8
#define TA_BASELINE 24
#if (WINVER >= 0x0400)
#define TA_RTLREADING 256
#define TA_MASK       (TA_BASELINE + TA_CENTER + TA_UPDATECP + TA_RTLREADING)
#else
#define TA_MASK (TA_BASELINE + TA_CENTER + TA_UPDATECP)
#endif

    UINT WINAPI SetTextAlign(HDC hdc, UINT align);
    UINT WINAPI GetTextAlign(HDC hdc);

    COLORREF WINAPI GetNearestColor(HDC hdc,         // handle to DC
                                    COLORREF crColor // color to be matched
    );

#define ETO_OPAQUE  0x0002
#define ETO_CLIPPED 0x0004
#if (WINVER >= 0x0400)
#define ETO_GLYPH_INDEX    0x0010
#define ETO_RTLREADING     0x0080
#define ETO_NUMERICSLOCAL  0x0400
#define ETO_NUMERICSLATIN  0x0800
#define ETO_IGNORELANGUAGE 0x1000
#endif /* WINVER >= 0x0400 */
#if (_WIN32_WINNT >= _WIN32_WINNT_WIN2K)
#define ETO_PDY 0x2000
#endif // (_WIN32_WINNT >= _WIN32_WINNT_WIN2K)
#if (_WIN32_WINNT >= _WIN32_WINNT_LONGHORN)
#define ETO_REVERSE_INDEX_MAP 0x10000
#endif

    BOOL WINAPI ExtTextOutA(HDC hdc,          // handle to DC
                            int X,            // x-coordinate of reference point
                            int Y,            // y-coordinate of reference point
                            UINT fuOptions,   // text-output options
                            CONST RECT *lprc, // optional dimensions
                            LPCSTR lpString,  // string
                            UINT cbCount,     // number of characters in string
                            CONST INT *lpDx   // array of spacing values
    );

    BOOL WINAPI ExtTextOutW(HDC hdc,          // handle to DC
                            int X,            // x-coordinate of reference point
                            int Y,            // y-coordinate of reference point
                            UINT fuOptions,   // text-output options
                            CONST RECT *lprc, // optional dimensions
                            LPCWSTR lpString, // string
                            UINT cbCount,     // number of characters in string
                            CONST INT *lpDx   // array of spacing values
    );

#ifdef UNICODE
#define ExtTextOut ExtTextOutW
#else
#define ExtTextOut ExtTextOutA
#endif // UNICODE

    typedef enum Antialias
    {
        ANTIALIAS_DEFAULT = 0,

        /* method */
        ANTIALIAS_NONE,
        ANTIALIAS_GRAY,
        ANTIALIAS_SUBPIXEL,

        /* hints */
        ANTIALIAS_FAST,
        ANTIALIAS_GOOD,
        ANTIALIAS_BEST
    } Antialias;

    Antialias WINAPI GetAntialiasMode(HDC hdc);
    Antialias WINAPI SetAntialiasMode(HDC hdc, Antialias mode);

    COLORREF WINAPI GetPixel(IN HDC hdc, IN int x, IN int y);
    COLORREF WINAPI SetPixel(IN HDC hdc, IN int x, IN int y, IN COLORREF color);

#define PC_RESERVED   0x01 /* palette index used for animation */
#define PC_EXPLICIT   0x02 /* palette index is explicit to device */
#define PC_NOCOLLAPSE 0x04 /* do not match color to system palette */

    typedef struct tagPALETTEENTRY
    {
        BYTE peRed;
        BYTE peGreen;
        BYTE peBlue;
        BYTE peFlags;
    } PALETTEENTRY;

    typedef struct tagLOGPALETTE
    {
        WORD palVersion;
        WORD palNumEntries;
        PALETTEENTRY palPalEntry[1];
    } LOGPALETTE;

    typedef struct tagKERNINGPAIR
    {
        WORD wFirst;
        WORD wSecond;
        int iKernAmount;
    } KERNINGPAIR, *LPKERNINGPAIR;

    typedef struct tagFONTSIGNATURE
    {
        DWORD fsUsb[4];
        DWORD fsCsb[2];
    } FONTSIGNATURE, *PFONTSIGNATURE, FAR *LPFONTSIGNATURE;

    typedef struct _ABC
    {
        int abcA;
        UINT abcB;
        int abcC;
    } ABC, *PABC, NEAR *NPABC, FAR *LPABC;

    typedef struct _ABCFLOAT
    {
        FLOAT abcfA;
        FLOAT abcfB;
        FLOAT abcfC;
    } ABCFLOAT, *PABCFLOAT, NEAR *NPABCFLOAT, FAR *LPABCFLOAT;

#define OUT_DEFAULT_PRECIS        0
#define OUT_STRING_PRECIS         1
#define OUT_CHARACTER_PRECIS      2
#define OUT_STROKE_PRECIS         3
#define OUT_TT_PRECIS             4
#define OUT_DEVICE_PRECIS         5
#define OUT_RASTER_PRECIS         6
#define OUT_TT_ONLY_PRECIS        7
#define OUT_OUTLINE_PRECIS        8
#define OUT_SCREEN_OUTLINE_PRECIS 9
#define OUT_PS_ONLY_PRECIS        10

#define CLIP_DEFAULT_PRECIS   0
#define CLIP_CHARACTER_PRECIS 1
#define CLIP_STROKE_PRECIS    2
#define CLIP_MASK             0xf

#define DEFAULT_QUALITY        0
#define DRAFT_QUALITY          1
#define PROOF_QUALITY          2
#define NONANTIALIASED_QUALITY 3
#define ANTIALIASED_QUALITY    4

#define DEFAULT_PITCH  0
#define FIXED_PITCH    1
#define VARIABLE_PITCH 2

    UINT WINAPI RealizePalette(_In_ HDC hdc);
    HPALETTE WINAPI SelectPalette(_In_ HDC hdc, _In_ HPALETTE hPal, _In_ BOOL bForceBkgd);

    BOOL WINAPI DPtoLP(HDC hdc,          // handle to device context
                       LPPOINT lpPoints, // array of points
                       int nCount        // count of points in array
    );

    BOOL WINAPI LPtoDP(HDC hdc,          // handle to device context
                       LPPOINT lpPoints, // array of points
                       int nCount        // count of points in array
    );

#define CLR_INVALID 0x00FFFFFF

    BOOL WINAPI DrawFrameControl(_In_ HDC, _Inout_ LPRECT, _In_ UINT, _In_ UINT);

    BOOL WINAPI GetCharWidthA(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer);
    BOOL WINAPI GetCharWidthW(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer);
#ifdef UNICODE
#define GetCharWidth GetCharWidthW
#else
#define GetCharWidth GetCharWidthA
#endif // !UNICODE

    /* 语义与 GetCharWidth 相同：按字符（而非 glyph index）取步进宽度。
     * Win32 里带 32 后缀的版本取的是「32 位字符集」的宽度，swinx 直接复用。 */
    BOOL WINAPI GetCharWidth32A(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer);
    BOOL WINAPI GetCharWidth32W(_In_ HDC hdc, _In_ UINT iFirst, _In_ UINT iLast, _Out_writes_(iLast + 1 - iFirst) LPINT lpBuffer);
#ifdef UNICODE
#define GetCharWidth32 GetCharWidth32W
#else
#define GetCharWidth32 GetCharWidth32A
#endif // !UNICODE

    HDC WINAPI CreateICW(LPCWSTR lpszDriver,   // driver name
                         LPCWSTR lpszDevice,   // device name
                         LPCWSTR lpszOutput,   // port or file name
                         CONST void *lpdvmInit // optional initialization data
    );

    HDC WINAPI CreateICA(LPCSTR lpszDriver,    // driver name
                         LPCSTR lpszDevice,    // device name
                         LPCSTR lpszOutput,    // port or file name
                         CONST void *lpdvmInit // optional initialization data
    );

    BOOL WINAPI DrawEdge(HDC hdc, LPRECT rc, UINT edge, UINT flags);

    int WINAPI AddFontResourceA(LPCSTR lpszFilename);
    int WINAPI AddFontResourceExA(LPCSTR lpszFilename, // font file name
                                  DWORD fl,            // font characteristics
                                  PVOID pdv            // reserved
    );

    int WINAPI AddFontResourceW(LPCWSTR lpszFilename);
    int WINAPI AddFontResourceExW(LPCWSTR lpszFilename, // font file name
                                  DWORD fl,             // font characteristics
                                  PVOID pdv             // reserved
    );

    /* Installs font data that is already resident in memory (the session font
     * table): *pNumFonts receives the number of fonts added, and the returned
     * handle goes back to RemoveFontMemResourceEx(). */
    HANDLE WINAPI AddFontMemResourceEx(PVOID pFileView, DWORD cjSize, PVOID pvReserved, DWORD *pNumFonts);
    BOOL WINAPI RemoveFontMemResourceEx(HANDLE hFontResource);

#ifdef UNICODE
#define AddFontResource   AddFontResourceW
#define AddFontResourceEx AddFontResourceExW
#else
#define AddFontResource   AddFontResourceA
#define AddFontResourceEx AddFontResourceExA
#endif // !UNICODE

// Path API constants
#define PT_CLOSEFIGURE 0x01
#define PT_LINETO      0x02
#define PT_BEZIERTO    0x04
#define PT_MOVETO      0x06

// Region combine modes for SelectClipPath
#define RGN_AND  1
#define RGN_OR   2
#define RGN_XOR  3
#define RGN_DIFF 4
#define RGN_COPY 5

    // Path API functions
    BOOL WINAPI BeginPath(HDC hdc);
    BOOL WINAPI EndPath(HDC hdc);
    BOOL WINAPI AbortPath(HDC hdc);
    BOOL WINAPI CloseFigure(HDC hdc);
    BOOL WINAPI StrokePath(HDC hdc);
    BOOL WINAPI FillPath(HDC hdc);
    BOOL WINAPI StrokeAndFillPath(HDC hdc);
    HRGN WINAPI PathToRegion(HDC hdc);
    BOOL WINAPI SelectClipPath(HDC hdc, int mode);
    /* 把当前路径按画笔宽度加粗。需要打印机 DC 的路径语义，swinx 不支持。 */
    BOOL WINAPI WidenPath(HDC hdc);
    int WINAPI GetPath(HDC hdc, LPPOINT lpPoints, LPBYTE lpTypes, int nSize);
    BOOL WINAPI SetMiterLimit(HDC hdc, FLOAT eNewLimit, PFLOAT peOldLimit);
    BOOL WINAPI GetMiterLimit(HDC hdc, PFLOAT peLimit);

    typedef UINT_PTR(CALLBACK *LPCFHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

// ChooseFont flags
#define CF_SCREENFONTS          0x00000001L
#define CF_PRINTERFONTS         0x00000002L
#define CF_BOTH                 (CF_SCREENFONTS | CF_PRINTERFONTS)
#define CF_SHOWHELP             0x00000004L
#define CF_ENABLEHOOK           0x00000008L
#define CF_ENABLETEMPLATE       0x00000010L
#define CF_ENABLETEMPLATEHANDLE 0x00000020L
#define CF_INITTOLOGFONTSTRUCT  0x00000040L
#define CF_USESTYLE             0x00000080L
#define CF_EFFECTS              0x00000100L
#define CF_APPLY                0x00000200L
#define CF_ANSIONLY             0x00000400L
#define CF_SCRIPTSONLY          CF_ANSIONLY
#define CF_NOVECTORFONTS        0x00000800L
#define CF_NOOEMFONTS           CF_NOVECTORFONTS
#define CF_NOSIMULATIONS        0x00001000L
#define CF_LIMITSIZE            0x00002000L
#define CF_FIXEDPITCHONLY       0x00004000L
#define CF_WYSIWYG              0x00008000L
#define CF_FORCEFONTEXIST       0x00010000L
#define CF_SCALABLEONLY         0x00020000L
#define CF_TTONLY               0x00040000L
#define CF_NOFACESEL            0x00080000L
#define CF_NOSTYLESEL           0x00100000L
#define CF_NOSIZESEL            0x00200000L
#define CF_SELECTSCRIPT         0x00400000L
#define CF_NOSCRIPTSEL          0x00800000L
#define CF_NOVERTFONTS          0x01000000L

    typedef struct tagCHOOSEFONTA
    {
        DWORD lStructSize;
        HWND hwndOwner;        // caller's window handle
        HDC hDC;               // printer DC/IC or NULL
        LPLOGFONTA lpLogFont;  // ptr. to a LOGFONT struct
        INT iPointSize;        // 10 * size in points of selected font
        DWORD Flags;           // enum. type flags
        COLORREF rgbColors;    // returned text color
        LPARAM lCustData;      // data passed to hook fn.
        LPCFHOOKPROC lpfnHook; // ptr. to hook function
        LPCSTR lpTemplateName; // custom template name
        HINSTANCE hInstance;   // instance handle of.EXE that
        //   contains cust. dlg. template
        LPSTR lpszStyle; // return the style field here
        // must be LF_FACESIZE or bigger
        WORD nFontType; // same value reported to the
        WORD ___MISSING_ALIGNMENT__;
        INT nSizeMin; // minimum pt size allowed &
        INT nSizeMax; // max pt size allowed if
    } CHOOSEFONTA, *PCHOOSEFONTA, *LPCHOOSEFONTA;
    typedef struct tagCHOOSEFONTW
    {
        DWORD lStructSize;
        HWND hwndOwner;         // caller's window handle
        HDC hDC;                // printer DC/IC or NULL
        LPLOGFONTW lpLogFont;   // ptr. to a LOGFONT struct
        INT iPointSize;         // 10 * size in points of selected font
        DWORD Flags;            // enum. type flags
        COLORREF rgbColors;     // returned text color
        LPARAM lCustData;       // data passed to hook fn.
        LPCFHOOKPROC lpfnHook;  // ptr. to hook function
        LPCWSTR lpTemplateName; // custom template name
        HINSTANCE hInstance;    // instance handle of.EXE that
        //   contains cust. dlg. template
        LPWSTR lpszStyle; // return the style field here
        // must be LF_FACESIZE or bigger
        WORD nFontType; // same value reported to the
        WORD ___MISSING_ALIGNMENT__;
        INT nSizeMin; // minimum pt size allowed &
        INT nSizeMax; // max pt size allowed if
    } CHOOSEFONTW, *PCHOOSEFONTW, *LPCHOOSEFONTW;

#ifdef UNICODE
#define CHOOSEFONT   CHOOSEFONTW
#define PCHOOSEFONT  PCHOOSEFONTW
#define LPCHOOSEFONT LPCHOOSEFONTW
#else
#define CHOOSEFONT   CHOOSEFONTA
#define PCHOOSEFONT  PCHOOSEFONTA
#define LPCHOOSEFONT LPCHOOSEFONTA
#endif // !UNICODE

    HICON WINAPI CreateIconFromResource(LPBYTE bits, UINT cbSize, BOOL bIcon, DWORD dwVersion);
    HBITMAP WINAPI LoadBitmapA(_In_opt_ HINSTANCE hInstance, _In_ LPCSTR lpBitmapName);
    HBITMAP WINAPI LoadBitmapW(_In_opt_ HINSTANCE hInstance, _In_ LPCWSTR lpBitmapName);

#ifdef UNICODE
#define LoadBitmap LoadBitmapW
#else
#define LoadBitmap LoadBitmapA
#endif // !UNICODE

    /*
     * Glyph / font enumeration.
     *
     * WebKit 的 win 字体代码（FontCacheWin / GlyphPageTreeNodeCairoWin /
     * SimpleFontData*Win）需要按 glyph index 取宽度、取字形度量、按字符集
     * 枚举字体族，以及取字体的 Unicode 覆盖区间。这里补齐这些入口与配套结构。
     */

#define GGI_MARK_NONEXISTING_GLYPHS 0x0001

    typedef struct tagWCRANGE
    {
        WCHAR wcLow;
        USHORT cGlyphs;
    } WCRANGE, *PWCRANGE, FAR *LPWCRANGE;

    typedef struct tagGLYPHSET
    {
        DWORD cbThis;  // 整个结构的字节数（含尾部的 ranges 数组）
        DWORD flAccel; // 加速标志（GS_8BIT_INDICES）
        DWORD cGlyphsSupported;
        DWORD cRanges;
        WCRANGE ranges[1];
    } GLYPHSET, *PGLYPHSET, FAR *LPGLYPHSET;

#define GS_8BIT_INDICES 0x00000001

    typedef struct tagENUMLOGFONTEXA
    {
        LOGFONTA elfLogFont;
        BYTE elfFullName[LF_FULLFACESIZE];
        BYTE elfStyle[LF_FACESIZE];
        BYTE elfScript[LF_FACESIZE];
    } ENUMLOGFONTEXA, FAR *LPENUMLOGFONTEXA;

    typedef struct tagENUMLOGFONTEXW
    {
        LOGFONTW elfLogFont;
        WCHAR elfFullName[LF_FULLFACESIZE];
        WCHAR elfStyle[LF_FACESIZE];
        WCHAR elfScript[LF_FACESIZE];
    } ENUMLOGFONTEXW, FAR *LPENUMLOGFONTEXW;

#ifdef UNICODE
    typedef ENUMLOGFONTEXW ENUMLOGFONTEX;
    typedef LPENUMLOGFONTEXW LPENUMLOGFONTEX;
#else
typedef ENUMLOGFONTEXA ENUMLOGFONTEX;
typedef LPENUMLOGFONTEXA LPENUMLOGFONTEX;
#endif // UNICODE

    typedef int(CALLBACK *FONTENUMPROCA)(CONST LOGFONTA *, CONST TEXTMETRICA *, DWORD, LPARAM);
    typedef int(CALLBACK *FONTENUMPROCW)(CONST LOGFONTW *, CONST TEXTMETRICW *, DWORD, LPARAM);

    typedef struct tagOUTLINETEXTMETRICA
    {
        UINT otmSize;
        TEXTMETRICA otmTextMetrics;
        BYTE otmFiller;
        PANOSE otmPanoseNumber;
        UINT otmfsSelection;
        UINT otmfsType;
        int otmsCharSlopeRise;
        int otmsCharSlopeRun;
        int otmItalicAngle;
        UINT otmEMSquare;
        int otmAscent;
        int otmDescent;
        UINT otmLineGap;
        UINT otmsCapEmHeight;
        UINT otmsXHeight;
        RECT otmrcFontBox;
        int otmMacAscent;
        int otmMacDescent;
        UINT otmMacLineGap;
        UINT otmusMinimumPPEM;
        POINT otmptSubscriptSize;
        POINT otmptSubscriptOffset;
        POINT otmptSuperscriptSize;
        POINT otmptSuperscriptOffset;
        UINT otmsStrikeoutSize;
        int otmsStrikeoutPosition;
        int otmsUnderscoreSize;
        int otmsUnderscorePosition;
        /* 以下四个字段是「相对本结构体基址」的字节偏移，不是指针。
         * 取值方式：(char*)potm + potm->otmpFaceName，与 Win32 一致。 */
        PSTR otmpFamilyName;
        PSTR otmpFaceName;
        PSTR otmpStyleName;
        PSTR otmpFullName;
    } OUTLINETEXTMETRICA, *POUTLINETEXTMETRICA, FAR *LPOUTLINETEXTMETRICA;

    typedef struct tagOUTLINETEXTMETRICW
    {
        UINT otmSize;
        TEXTMETRICW otmTextMetrics;
        BYTE otmFiller;
        PANOSE otmPanoseNumber;
        UINT otmfsSelection;
        UINT otmfsType;
        int otmsCharSlopeRise;
        int otmsCharSlopeRun;
        int otmItalicAngle;
        UINT otmEMSquare;
        int otmAscent;
        int otmDescent;
        UINT otmLineGap;
        UINT otmsCapEmHeight;
        UINT otmsXHeight;
        RECT otmrcFontBox;
        int otmMacAscent;
        int otmMacDescent;
        UINT otmMacLineGap;
        UINT otmusMinimumPPEM;
        POINT otmptSubscriptSize;
        POINT otmptSubscriptOffset;
        POINT otmptSuperscriptSize;
        POINT otmptSuperscriptOffset;
        UINT otmsStrikeoutSize;
        int otmsStrikeoutPosition;
        int otmsUnderscoreSize;
        int otmsUnderscorePosition;
        /* 同 OUTLINETEXTMETRICA：相对结构体基址的字节偏移 */
        PWSTR otmpFamilyName;
        PWSTR otmpFaceName;
        PWSTR otmpStyleName;
        PWSTR otmpFullName;
    } OUTLINETEXTMETRICW, *POUTLINETEXTMETRICW, FAR *LPOUTLINETEXTMETRICW;

    int WINAPI EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, FONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags);
    int WINAPI EnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, FONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags);

    DWORD WINAPI GetGlyphIndicesA(HDC hdc, LPCSTR lpstr, int c, LPWORD pgi, DWORD fl);
    DWORD WINAPI GetGlyphIndicesW(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl);

    DWORD WINAPI GetGlyphOutlineA(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, CONST MAT2 *lpmat2);
    DWORD WINAPI GetGlyphOutlineW(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, CONST MAT2 *lpmat2);

    /* cjCopy == 0 时只返回所需字节数（不写入 potm），与 Win32 一致。 */
    UINT WINAPI GetOutlineTextMetricsA(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA potm);
    UINT WINAPI GetOutlineTextMetricsW(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW potm);

    /* 无后缀别名，与 Win32 一致（cairo 的 win32 字体后端用的就是这个名字）。 */
#ifdef UNICODE
    typedef OUTLINETEXTMETRICW OUTLINETEXTMETRIC, *POUTLINETEXTMETRIC, FAR *LPOUTLINETEXTMETRIC;
#else
typedef OUTLINETEXTMETRICA OUTLINETEXTMETRIC, *POUTLINETEXTMETRIC, FAR *LPOUTLINETEXTMETRIC;
#endif // UNICODE

    /* 从字体文件里取 sfnt 表（dwTable 是大端序的 4 字节 tag，如 'eman'）。
     * pvBuffer 为 NULL 时返回该表的字节数；失败返回 GDI_ERROR。
     * swinx 不支持 dwTable == 0（整个字体文件）。 */
    DWORD WINAPI GetFontData(HDC hdc, DWORD dwTable, DWORD dwOffset, LPVOID pvBuffer, DWORD cbData);

    /*
     * GetCharacterPlacement：把字符串转成 glyph index 与步进数组。
     * swinx 只实现「逐字符直排」这一种最常用语义：
     *   - 忽略 lpOutString / lpOrder / lpCaretPos / lpClass；
     *   - nMaxExtent 只用于计算 nMaxFit（> 0 时按宽度截断）；
     *   - GCP_* 的重排/连字标志不产生实际效果（CoreText 不做这些处理）。
     */
#define GCP_DBCS            0x0001
#define GCP_REORDER         0x0002
#define GCP_USEKERNING      0x0008
#define GCP_GLYPHSHAPE      0x0010
#define GCP_LIGATE          0x0020
#define GCP_DIACRITIC       0x0100
#define GCP_KASHIDA         0x0400
#define GCP_ERROR           0x8000
#define FLI_MASK            (0x103B)
#define GCP_JUSTIFY         0x00010000L
#define FLI_GLYPHS          0x00040000L
#define GCP_CLASSIN         0x00080000L
#define GCP_MAXEXTENT       0x00100000L
#define GCP_JUSTIFYIN       0x00200000L
#define GCP_DISPLAYZWG      0x00400000L
#define GCP_SYMSWAPOFF      0x00800000L
#define GCP_NUMERICOVERRIDE 0x01000000L
#define GCP_NEUTRALOVERRIDE 0x02000000L
#define GCP_NUMERICSLATIN   0x04000000L
#define GCP_NUMERICSLOCAL   0x08000000L

    typedef struct tagGCP_RESULTSA
    {
        DWORD lStructSize;
        LPSTR lpOutString;
        UINT *lpOrder;
        int *lpDx;
        int *lpCaretPos;
        LPSTR lpClass;
        LPSTR lpGlyphs;
        UINT nGlyphs;
        int nMaxFit;
    } GCP_RESULTSA, *LPGCP_RESULTSA;

    typedef struct tagGCP_RESULTSW
    {
        DWORD lStructSize;
        LPWSTR lpOutString;
        UINT *lpOrder;
        int *lpDx;
        int *lpCaretPos;
        LPSTR lpClass;
        LPWSTR lpGlyphs;
        UINT nGlyphs;
        int nMaxFit;
    } GCP_RESULTSW, *LPGCP_RESULTSW;

#ifdef UNICODE
    typedef GCP_RESULTSW GCP_RESULTS, *LPGCP_RESULTS;
#else
typedef GCP_RESULTSA GCP_RESULTS, *LPGCP_RESULTS;
#endif // UNICODE

    DWORD WINAPI GetCharacterPlacementA(HDC hdc, LPCSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSA lpResults, DWORD dwFlags);
    DWORD WINAPI GetCharacterPlacementW(HDC hdc, LPCWSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSW lpResults, DWORD dwFlags);
#ifdef UNICODE
#define GetCharacterPlacement GetCharacterPlacementW
#else
#define GetCharacterPlacement GetCharacterPlacementA
#endif // UNICODE

    /* lpgs == NULL 时返回所需的 GLYPHSET 字节数。 */
    DWORD WINAPI GetFontUnicodeRanges(HDC hdc, LPGLYPHSET lpgs);

    /* 与 GetCharWidthW 不同：pg 是 glyph index 数组，pgi 为 NULL 时表示
     * 区间 [giFirst, giFirst+cgi] 内的连续 glyph index。 */
    BOOL WINAPI GetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT lpBuffer);

#ifdef UNICODE
#define EnumFontFamiliesEx    EnumFontFamiliesExW
#define GetGlyphIndices       GetGlyphIndicesW
#define GetGlyphOutline       GetGlyphOutlineW
#define GetOutlineTextMetrics GetOutlineTextMetricsW
#else
#define EnumFontFamiliesEx    EnumFontFamiliesExA
#define GetGlyphIndices       GetGlyphIndicesA
#define GetGlyphOutline       GetGlyphOutlineA
#define GetOutlineTextMetrics GetOutlineTextMetricsA
#endif // UNICODE

    /*
     * Enhanced metafile（EMF）
     *
     * swinx 只实现 WebKit 需要的「录制-回放」子集：CreateEnhMetaFile 返回的 DC 上
     * SelectObject(OBJ_FONT) 会记录一条 EMR_EXTCREATEFONTINDIRECTW，CloseEnhMetaFile
     * 之后可由 EnumEnhMetaFile 逐条回放。WebKit 用它反查 Uniscribe 实际选中的字体。
     */

#define ELF_VENDOR_SIZE 4

    typedef struct tagEXTLOGFONTA
    {
        LOGFONTA elfLogFont;
        BYTE elfFullName[LF_FULLFACESIZE];
        BYTE elfStyle[LF_FACESIZE];
        DWORD elfVersion;
        DWORD elfStyleSize;
        DWORD elfMatch;
        DWORD elfReserved;
        BYTE elfVendorId[ELF_VENDOR_SIZE];
        DWORD elfCulture;
        PANOSE elfPanose;
    } EXTLOGFONTA, *PEXTLOGFONTA, FAR *LPEXTLOGFONTA;

    typedef struct tagEXTLOGFONTW
    {
        LOGFONTW elfLogFont;
        WCHAR elfFullName[LF_FULLFACESIZE];
        WCHAR elfStyle[LF_FACESIZE];
        DWORD elfVersion;
        DWORD elfStyleSize;
        DWORD elfMatch;
        DWORD elfReserved;
        BYTE elfVendorId[ELF_VENDOR_SIZE];
        DWORD elfCulture;
        PANOSE elfPanose;
    } EXTLOGFONTW, *PEXTLOGFONTW, FAR *LPEXTLOGFONTW;

    typedef struct tagENHMETARECORD
    {
        DWORD iType;
        DWORD nSize;
        DWORD dParm[1];
    } ENHMETARECORD, *PENHMETARECORD, FAR *LPENHMETARECORD;

    typedef ENHMETARECORD EMR, *PEMR;

#define EMR_HEADER                 1
#define EMR_EXTCREATEFONTINDIRECTW 82

    typedef struct tagEMREXTCREATEFONTINDIRECTW
    {
        EMR emr;
        EXTLOGFONTW elfw;
    } EMREXTCREATEFONTINDIRECTW, *PEMREXTCREATEFONTINDIRECTW, FAR *LPEMREXTCREATEFONTINDIRECTW;

    typedef struct tagHANDLETABLE
    {
        HGDIOBJ objectHandle[1];
    } HANDLETABLE, *PHANDLETABLE, FAR *LPHANDLETABLE;

    typedef int(CALLBACK *ENHMFENUMPROC)(HDC, HANDLETABLE FAR *, CONST ENHMETARECORD FAR *, int, LPARAM);

    /* lpFilename / lpDescription 暂不支持（swinx 不落盘 EMF），需传 NULL。 */
    HDC WINAPI CreateEnhMetaFileA(HDC hdcRef, LPCSTR lpFilename, CONST RECT *lpRect, LPCSTR lpDescription);
    HDC WINAPI CreateEnhMetaFileW(HDC hdcRef, LPCWSTR lpFilename, CONST RECT *lpRect, LPCWSTR lpDescription);

    HENHMETAFILE WINAPI CloseEnhMetaFile(HDC hdc);
    BOOL WINAPI DeleteEnhMetaFile(HENHMETAFILE hmf);
    BOOL WINAPI EnumEnhMetaFile(HDC hdc, HENHMETAFILE hmf, ENHMFENUMPROC lpProc, LPVOID lpParam, CONST RECT *lpRect);

#ifdef UNICODE
#define CreateEnhMetaFile CreateEnhMetaFileW
#else
#define CreateEnhMetaFile CreateEnhMetaFileA
#endif // UNICODE

#ifdef __cplusplus
}
#endif //__cplusplus
#endif //_LINUX_GDI_H_