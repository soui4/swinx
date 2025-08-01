/*
 * Cursor and icon support
 *
 * Copyright 1995 Alexandre Julliard
 * Copyright 1996 Martin Von Loewis
 * Copyright 1997 Alex Korobka
 * Copyright 1998 Turchanov Sergey
 * Copyright 2007 Henri Verbeet
 * Copyright 2009 Vincent Povirk for CodeWeavers
 * Copyright 2016 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <png.h>
#include <unistd.h>
#include <stdlib.h>
#include "gdi.h"
#include "debug.h"
#define kLogTag "cursoricon"

#define smax(a, b) ((a) > (b) ? (a) : (b))
#define smin(a, b) ((a) < (b) ? (a) : (b))

#define RIFF_FOURCC(c0, c1, c2, c3) ((DWORD)(BYTE)(c0) | ((DWORD)(BYTE)(c1) << 8) | ((DWORD)(BYTE)(c2) << 16) | ((DWORD)(BYTE)(c3) << 24))
#define PNG_SIGN                    RIFF_FOURCC(0x89, 'P', 'N', 'G')

/* NtUserSetCursorIconData parameter, not compatible with Windows */
struct cursoricon_frame
{
    UINT width;    /* frame-specific width */
    UINT height;   /* frame-specific height */
    HBITMAP color; /* color bitmap */
    HBITMAP alpha; /* pre-multiplied alpha bitmap for 32-bpp icons */
    HBITMAP mask;  /* mask bitmap (followed by color for 1-bpp icons) */
    POINT hotspot;
};

struct cursoricon_desc
{
    UINT flags;
    UINT num_steps;
    UINT num_frames;
    UINT delay;
    struct cursoricon_frame *frames;
    DWORD *frame_seq;
    DWORD *frame_rates;
    HRSRC rsrc;
};

#pragma pack(push, 1)
typedef struct
{
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
} ICONRESDIR;

typedef struct
{
    WORD wWidth;
    WORD wHeight;
} CURSORDIR;

typedef struct
{
    union {
        ICONRESDIR icon;
        CURSORDIR cursor;
    } ResInfo;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    WORD wResId;
} CURSORICONDIRENTRY;

typedef struct
{
    WORD idReserved;
    WORD idType;
    WORD idCount;
    CURSORICONDIRENTRY idEntries[1];
} CURSORICONDIR;

typedef struct
{
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD xHotspot;
    WORD yHotspot;
    DWORD dwDIBSize;
    DWORD dwDIBOffset;
} CURSORICONFILEDIRENTRY;

typedef struct
{
    WORD idReserved;
    WORD idType;
    WORD idCount;
    CURSORICONFILEDIRENTRY idEntries[1];
} CURSORICONFILEDIR;
#pragma pack(pop)

/**********************************************************************
 * User objects management
 */

static HBITMAP create_color_bitmap(int width, int height)
{
    HDC hdc = GetDC(0);
    HBITMAP ret = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(0, hdc);
    return ret;
}

static int get_display_bpp(void)
{
    HDC hdc = GetDC(0);
    int ret = GetDeviceCaps(hdc, BITSPIXEL);
    ReleaseDC(0, hdc);
    return ret;
}

static void free_icon_frame(struct cursoricon_frame *frame)
{
    if (frame->color)
        DeleteObject(frame->color);
    if (frame->alpha)
        DeleteObject(frame->alpha);
    if (frame->mask)
        DeleteObject(frame->mask);
}

/***********************************************************************
 *             map_fileW
 *
 * Helper function to map a file to memory:
 *  name			-	file name
 *  [RETURN] ptr		-	pointer to mapped file
 *  [RETURN] filesize           -       pointer size of file to be stored if not NULL
 */

typedef struct MapFileInfo
{
    void *ptr;
    int fd;
    DWORD fileSize;
} MapFileInfo;

static int map_file(LPCSTR name, MapFileInfo *ret)
{
    int fd = open(name, O_RDONLY); // 以只读方式打开文件
    if (fd == -1)
    {
        perror("open");
        return 0;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    { // 获取文件状态，这里用以获取文件大小
        perror("fstat");
        close(fd);
        return 0;
    }

    void *mapped_file = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped_file == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 0;
    }
    ret->fd = fd;
    ret->fileSize = sb.st_size;
    ret->ptr = mapped_file;
    return 1;
}

static int map_fileW(LPCWSTR name, MapFileInfo *ret)
{
    char szNameU8[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, name, -1, szNameU8, MAX_PATH, NULL, NULL);
    return map_file(szNameU8, ret);
}

static void map_close(MapFileInfo *info)
{
    if (!info->ptr)
        return;
    munmap(info->ptr, info->fileSize);
    close(info->fd);
}

/***********************************************************************
 *          get_dib_image_size
 *
 * Return the size of a DIB bitmap in bytes.
 */
static int get_dib_image_size(int width, int height, int depth)
{
    return (((width * depth + 31) / 8) & ~3) * abs(height);
}

/***********************************************************************
 *           bitmap_info_size
 *
 * Return the size of the bitmap info structure including color table.
 */
int bitmap_info_size(const BITMAPINFO *info, WORD coloruse)
{
    unsigned int colors, size, masks = 0;

    if (info->bmiHeader.biSize == sizeof(BITMAPCOREHEADER))
    {
        const BITMAPCOREHEADER *core = (const BITMAPCOREHEADER *)info;
        colors = (core->bcBitCount <= 8) ? 1 << core->bcBitCount : 0;
        return sizeof(BITMAPCOREHEADER) + colors * ((coloruse == DIB_RGB_COLORS) ? sizeof(RGBTRIPLE) : sizeof(WORD));
    }
    else /* assume BITMAPINFOHEADER */
    {
        colors = info->bmiHeader.biClrUsed;
        if (colors > 256) /* buffer overflow otherwise */
            colors = 256;
        if (!colors && (info->bmiHeader.biBitCount <= 8))
            colors = 1 << info->bmiHeader.biBitCount;
        if (info->bmiHeader.biCompression == BI_BITFIELDS)
            masks = 3;
        size = smax(info->bmiHeader.biSize, sizeof(BITMAPINFOHEADER) + masks * sizeof(DWORD));
        return size + colors * ((coloruse == DIB_RGB_COLORS) ? sizeof(RGBQUAD) : sizeof(WORD));
    }
}

/***********************************************************************
 *          is_dib_monochrome
 *
 * Returns whether a DIB can be converted to a monochrome DDB.
 *
 * A DIB can be converted if its color table contains only black and
 * white. Black must be the first color in the color table.
 *
 * Note : If the first color in the color table is white followed by
 *        black, we can't convert it to a monochrome DDB with
 *        SetDIBits, because black and white would be inverted.
 */
static BOOL is_dib_monochrome(const BITMAPINFO *info)
{
    if (info->bmiHeader.biSize == sizeof(BITMAPCOREHEADER))
    {
        const RGBTRIPLE *rgb = ((const BITMAPCOREINFO *)info)->bmciColors;

        if (((const BITMAPCOREINFO *)info)->bmciHeader.bcBitCount != 1)
            return FALSE;

        /* Check if the first color is black */
        if ((rgb->rgbtRed == 0) && (rgb->rgbtGreen == 0) && (rgb->rgbtBlue == 0))
        {
            rgb++;

            /* Check if the second color is white */
            return ((rgb->rgbtRed == 0xff) && (rgb->rgbtGreen == 0xff) && (rgb->rgbtBlue == 0xff));
        }
        else
            return FALSE;
    }
    else /* assume BITMAPINFOHEADER */
    {
        const RGBQUAD *rgb = info->bmiColors;

        if (info->bmiHeader.biBitCount != 1)
            return FALSE;

        /* Check if the first color is black */
        if ((rgb->rgbRed == 0) && (rgb->rgbGreen == 0) && (rgb->rgbBlue == 0) && (rgb->rgbReserved == 0))
        {
            rgb++;

            /* Check if the second color is white */
            return ((rgb->rgbRed == 0xff) && (rgb->rgbGreen == 0xff) && (rgb->rgbBlue == 0xff) && (rgb->rgbReserved == 0));
        }
        else
            return FALSE;
    }
}

/***********************************************************************
 *           DIB_GetBitmapInfo
 *
 * Get the info from a bitmap header.
 * Return 1 for INFOHEADER, 0 for COREHEADER, -1 in case of failure.
 */
static int DIB_GetBitmapInfo(const BITMAPINFOHEADER *header, LONG *width, LONG *height, WORD *bpp, DWORD *compr)
{
    if (header->biSize == sizeof(BITMAPCOREHEADER))
    {
        const BITMAPCOREHEADER *core = (const BITMAPCOREHEADER *)header;
        *width = core->bcWidth;
        *height = core->bcHeight;
        *bpp = core->bcBitCount;
        *compr = 0;
        return 0;
    }
    else if (header->biSize == sizeof(BITMAPINFOHEADER) || header->biSize == sizeof(BITMAPV4HEADER) || header->biSize == sizeof(BITMAPV5HEADER))
    {
        *width = header->biWidth;
        *height = header->biHeight;
        *bpp = header->biBitCount;
        *compr = header->biCompression;
        return 1;
    }
    SLOG_FMTW("unknown/wrong size (%u) for header\n", header->biSize);
    return -1;
}

/**********************************************************************
 *              get_icon_size
 */
BOOL get_icon_size(HICON handle, SIZE *size)
{
    ICONINFO iconInfo;
    GetIconInfo(handle, &iconInfo);
    if (!iconInfo.hbmColor)
        return FALSE;
    BITMAP bm;
    GetObject(iconInfo.hbmColor, sizeof(bm), &bm);
    size->cx = bm.bmWidth;
    size->cy = bm.bmHeight;
    DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask)
        DeleteObject(iconInfo.hbmMask);
    return TRUE;
}

struct png_wrapper
{
    const char *buffer;
    size_t size, pos;
};

static void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct png_wrapper *png = (struct png_wrapper *)png_get_io_ptr(png_ptr);

    if (png->size - png->pos >= length)
    {
        memcpy(data, png->buffer + png->pos, length);
        png->pos += length;
    }
    else
    {
        png_error(png_ptr, "failed to read PNG data");
    }
}

static unsigned be_uint(unsigned val)
{
    union {
        unsigned val;
        unsigned char c[4];
    } u;

    u.val = val;
    return (u.c[0] << 24) | (u.c[1] << 16) | (u.c[2] << 8) | u.c[3];
}

static BOOL get_png_info(const void *png_data, DWORD size, int *width, int *height, int *bpp)
{
    static const unsigned char png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    static const unsigned char png_IHDR[8] = { 0, 0, 0, 0x0d, 'I', 'H', 'D', 'R' };
    struct PngData
    {
        char png_sig[8];
        char ihdr_sig[8];
        unsigned width, height;
        char bit_depth, color_type, compression, filter, interlace;
    };
    PngData *png = (PngData *)png_data;

    if (size < sizeof(*png))
        return FALSE;
    if (memcmp(png->png_sig, png_sig, sizeof(png_sig)) != 0)
        return FALSE;
    if (memcmp(png->ihdr_sig, png_IHDR, sizeof(png_IHDR)) != 0)
        return FALSE;

    *bpp = (png->color_type == PNG_COLOR_TYPE_RGB_ALPHA) ? 32 : 24;
    *width = be_uint(png->width);
    *height = be_uint(png->height);

    return TRUE;
}

static BITMAPINFO *load_png(const char *png_data, DWORD *size)
{
    struct png_wrapper png;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers = NULL;
    int color_type, bit_depth, bpp, width, height;
    int rowbytes, image_size, mask_size = 0, i;
    BITMAPINFO *info = NULL;
    unsigned char *image_data;

    if (!get_png_info(png_data, *size, &width, &height, &bpp))
        return NULL;

    png.buffer = png_data;
    png.size = *size;
    png.pos = 0;

    /* initialize libpng */
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        return NULL;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }

    /* set up setjmp/longjmp error handling */
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        free(row_pointers);
        png_destroy_info_struct(png_ptr, &info_ptr);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    png_set_crc_action(png_ptr, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);

    /* set up custom i/o handling */
    png_set_read_fn(png_ptr, &png, user_read_data);

    /* read the header */
    png_read_info(png_ptr, info_ptr);

    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    /* expand grayscale image data to rgb */
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    /* expand palette image data to rgb */
    if (color_type == PNG_COLOR_TYPE_PALETTE || bit_depth < 8)
        png_set_expand(png_ptr);

    /* update color type information */
    png_read_update_info(png_ptr, info_ptr);

    color_type = png_get_color_type(png_ptr, info_ptr);
    bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    bpp = 0;

    switch (color_type)
    {
    case PNG_COLOR_TYPE_RGB:
        if (bit_depth == 8)
            bpp = 24;
        break;

    case PNG_COLOR_TYPE_RGB_ALPHA:
        if (bit_depth == 8)
        {
            png_set_bgr(png_ptr);
            bpp = 32;
        }
        break;

    default:
        break;
    }

    if (!bpp)
    {
        // FIXME("unsupported PNG color format %d, %d bpp\n", color_type, bit_depth);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);

    rowbytes = (width * bpp + 7) / 8;
    image_size = height * rowbytes;
    if (bpp != 32) /* add a mask if there is no alpha */
        mask_size = (width + 7) / 8 * height;

    info = (BITMAPINFO *)malloc(sizeof(BITMAPINFOHEADER) + image_size + mask_size);
    if (!info)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    image_data = (unsigned char *)info + sizeof(BITMAPINFOHEADER);
    memset(image_data + image_size, 0, mask_size);

    row_pointers = (png_bytep *)malloc(height * sizeof(png_bytep));
    if (!row_pointers)
    {
        free(info);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    /* upside down */
    for (i = 0; i < height; i++)
        row_pointers[i] = image_data + (height - i - 1) * rowbytes;

    png_read_image(png_ptr, row_pointers);
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = width;
    info->bmiHeader.biHeight = height * 2;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = bpp;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = image_size;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrUsed = 0;
    info->bmiHeader.biClrImportant = 0;

    *size = sizeof(BITMAPINFOHEADER) + image_size + mask_size;
    return info;
}

/*
 *  The following macro functions account for the irregularities of
 *   accessing cursor and icon resources in files and resource entries.
 */
typedef BOOL (*fnGetCIEntry)(LPCVOID dir, DWORD size, int n, int *width, int *height, int *bits);

/**********************************************************************
 *	    CURSORICON_FindBestIcon
 *
 * Find the icon closest to the requested size and bit depth.
 */
static int CURSORICON_FindBestIcon(LPCVOID dir, DWORD size, fnGetCIEntry get_entry, int width, int height, int depth, UINT loadflags)
{
    int i, cx, cy, bits, bestEntry = -1;
    UINT iTotalDiff, iXDiff = 0, iYDiff = 0, iColorDiff;
    UINT iTempXDiff, iTempYDiff, iTempColorDiff;

    /* Find Best Fit */
    iTotalDiff = 0xFFFFFFFF;
    iColorDiff = 0xFFFFFFFF;

    if (loadflags & LR_DEFAULTSIZE)
    {
        if (!width)
            width = GetSystemMetrics(SM_CXICON);
        if (!height)
            height = GetSystemMetrics(SM_CYICON);
    }
    else if (!width && !height)
    {
        /* use the size of the first entry */
        if (!get_entry(dir, size, 0, &width, &height, &bits))
            return -1;
        iTotalDiff = 0;
    }

    for (i = 0; iTotalDiff && get_entry(dir, size, i, &cx, &cy, &bits); i++)
    {
        iTempXDiff = abs(width - cx);
        iTempYDiff = abs(height - cy);

        if (iTotalDiff > (iTempXDiff + iTempYDiff))
        {
            iXDiff = iTempXDiff;
            iYDiff = iTempYDiff;
            iTotalDiff = iXDiff + iYDiff;
        }
    }

    /* Find Best Colors for Best Fit */
    for (i = 0; get_entry(dir, size, i, &cx, &cy, &bits); i++)
    {
        // SLOG_FMTD("entry %d: %d x %d, %d bpp\n", i, cx, cy, bits);

        if (abs(width - cx) == iXDiff && abs(height - cy) == iYDiff)
        {
            iTempColorDiff = abs(depth - bits);
            if (iColorDiff > iTempColorDiff)
            {
                bestEntry = i;
                iColorDiff = iTempColorDiff;
            }
        }
    }

    return bestEntry;
}

static BOOL CURSORICON_GetResIconEntry(LPCVOID dir, DWORD size, int n, int *width, int *height, int *bits)
{
    const CURSORICONDIR *resdir = (const CURSORICONDIR *)dir;
    const ICONRESDIR *icon;

    if (resdir->idCount <= n)
        return FALSE;
    if ((const char *)&resdir->idEntries[n + 1] - (const char *)dir > size)
        return FALSE;
    icon = &resdir->idEntries[n].ResInfo.icon;
    *width = icon->bWidth;
    *height = icon->bHeight;
    *bits = resdir->idEntries[n].wBitCount;
    if (!*width && !*height)
        *width = *height = 256;
    return TRUE;
}

/**********************************************************************
 *	    CURSORICON_FindBestCursor
 *
 * Find the cursor closest to the requested size.
 *
 * FIXME: parameter 'color' ignored.
 */
static int CURSORICON_FindBestCursor(LPCVOID dir, DWORD size, fnGetCIEntry get_entry, int width, int height, int depth, UINT loadflags)
{
    int i, maxwidth, maxheight, maxbits, cx, cy, bits, bestEntry = -1;

    if (loadflags & LR_DEFAULTSIZE)
    {
        if (!width)
            width = GetSystemMetrics(SM_CXCURSOR);
        if (!height)
            height = GetSystemMetrics(SM_CYCURSOR);
    }
    else if (!width && !height)
    {
        /* use the first entry */
        if (!get_entry(dir, size, 0, &width, &height, &bits))
            return -1;
        return 0;
    }

    /* First find the largest one smaller than or equal to the requested size*/

    maxwidth = maxheight = maxbits = 0;
    for (i = 0; get_entry(dir, size, i, &cx, &cy, &bits); i++)
    {
        if (cx > width || cy > height)
            continue;
        if (cx < maxwidth || cy < maxheight)
            continue;
        if (cx == maxwidth && cy == maxheight)
        {
            if (loadflags & LR_MONOCHROME)
            {
                if (maxbits && bits >= maxbits)
                    continue;
            }
            else if (bits <= maxbits)
                continue;
        }
        bestEntry = i;
        maxwidth = cx;
        maxheight = cy;
        maxbits = bits;
    }
    if (bestEntry != -1)
        return bestEntry;

    /* Now find the smallest one larger than the requested size */

    maxwidth = maxheight = 255;
    for (i = 0; get_entry(dir, size, i, &cx, &cy, &bits); i++)
    {
        if (cx > maxwidth || cy > maxheight)
            continue;
        if (cx == maxwidth && cy == maxheight)
        {
            if (loadflags & LR_MONOCHROME)
            {
                if (maxbits && bits >= maxbits)
                    continue;
            }
            else if (bits <= maxbits)
                continue;
        }
        bestEntry = i;
        maxwidth = cx;
        maxheight = cy;
        maxbits = bits;
    }
    if (bestEntry == -1)
        bestEntry = 0;

    return bestEntry;
}

static BOOL CURSORICON_GetResCursorEntry(LPCVOID dir, DWORD size, int n, int *width, int *height, int *bits)
{
    const CURSORICONDIR *resdir = (const CURSORICONDIR *)dir;
    const CURSORDIR *cursor;

    if (resdir->idCount <= n)
        return FALSE;
    if ((const char *)&resdir->idEntries[n + 1] - (const char *)dir > size)
        return FALSE;
    cursor = &resdir->idEntries[n].ResInfo.cursor;
    *width = cursor->wWidth;
    *height = cursor->wHeight;
    *bits = resdir->idEntries[n].wBitCount;
    if (*height == *width * 2)
        *height /= 2;
    return TRUE;
}

static const CURSORICONDIRENTRY *CURSORICON_FindBestIconRes(const CURSORICONDIR *dir, DWORD size, int width, int height, int depth, UINT loadflags)
{
    int n;

    n = CURSORICON_FindBestIcon(dir, size, CURSORICON_GetResIconEntry, width, height, depth, loadflags);
    if (n < 0)
        return NULL;
    return &dir->idEntries[n];
}

static const CURSORICONDIRENTRY *CURSORICON_FindBestCursorRes(const CURSORICONDIR *dir, DWORD size, int width, int height, int depth, UINT loadflags)
{
    int n = CURSORICON_FindBestCursor(dir, size, CURSORICON_GetResCursorEntry, width, height, depth, loadflags);
    if (n < 0)
        return NULL;
    return &dir->idEntries[n];
}

static BOOL CURSORICON_GetFileEntry(LPCVOID dir, DWORD size, int n, int *width, int *height, int *bits)
{
    const CURSORICONFILEDIR *filedir = (const CURSORICONFILEDIR *)dir;
    const CURSORICONFILEDIRENTRY *entry;
    const BITMAPINFOHEADER *info;

    if (filedir->idCount <= n)
        return FALSE;
    if ((const char *)&filedir->idEntries[n + 1] - (const char *)dir > size)
        return FALSE;
    entry = &filedir->idEntries[n];
    if (entry->dwDIBOffset > size - sizeof(info->biSize))
        return FALSE;
    info = (const BITMAPINFOHEADER *)((const char *)dir + entry->dwDIBOffset);

    if (info->biSize == PNG_SIGN)
        return get_png_info(info, size, width, height, bits);
    if (info->biSize != sizeof(BITMAPCOREHEADER))
    {
        if ((const char *)(info + 1) - (const char *)dir > size)
            return FALSE;
        *bits = info->biBitCount;
    }
    else
    {
        const BITMAPCOREHEADER *coreinfo = (const BITMAPCOREHEADER *)((const char *)dir + entry->dwDIBOffset);
        if ((const char *)(coreinfo + 1) - (const char *)dir > size)
            return FALSE;
        *bits = coreinfo->bcBitCount;
    }
    *width = entry->bWidth;
    *height = entry->bHeight;
    return TRUE;
}

static const CURSORICONFILEDIRENTRY *CURSORICON_FindBestCursorFile(const CURSORICONFILEDIR *dir, DWORD size, int width, int height, int depth, UINT loadflags)
{
    int n = CURSORICON_FindBestCursor(dir, size, CURSORICON_GetFileEntry, width, height, depth, loadflags);
    if (n < 0)
        return NULL;
    return &dir->idEntries[n];
}

static const CURSORICONFILEDIRENTRY *CURSORICON_FindBestIconFile(const CURSORICONFILEDIR *dir, DWORD size, int width, int height, int depth, UINT loadflags)
{
    int n = CURSORICON_FindBestIcon(dir, size, CURSORICON_GetFileEntry, width, height, depth, loadflags);
    if (n < 0)
        return NULL;
    return &dir->idEntries[n];
}

/***********************************************************************
 *          bmi_has_alpha
 */
static BOOL bmi_has_alpha(const BITMAPINFO *info, const void *bits)
{
    int i;
    BOOL has_alpha = FALSE;
    const unsigned char *ptr = (const unsigned char *)bits;

    if (info->bmiHeader.biBitCount != 32)
        return FALSE;
    for (i = 0; i < info->bmiHeader.biWidth * abs(info->bmiHeader.biHeight); i++, ptr += 4)
        if ((has_alpha = (ptr[3] != 0)))
            break;
    return has_alpha;
}

/***********************************************************************
 *          create_alpha_bitmap
 *
 * Create the alpha bitmap for a 32-bpp icon that has an alpha channel.
 */
static HBITMAP create_alpha_bitmap(HBITMAP color, const BITMAPINFO *src_info, const void *color_bits)
{
    HBITMAP alpha = 0;
    BITMAPINFO *info = NULL;
    BITMAP bm;
    HDC hdc;
    void *bits;
    unsigned char *ptr;
    int i;

    if (!GetObject(color, sizeof(bm), &bm))
        return 0;
    if (bm.bmBitsPixel != 32)
        return 0;

    if (!(hdc = CreateCompatibleDC(0)))
        return 0;
    if (!(info = (BITMAPINFO *)malloc(FIELD_OFFSET(BITMAPINFO, bmiColors[256]))))
        goto done;
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = bm.bmWidth;
    info->bmiHeader.biHeight = -bm.bmHeight; // todo:hjx
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = 32;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = bm.bmWidth * bm.bmHeight * 4;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrUsed = 0;
    info->bmiHeader.biClrImportant = 0;
    if (!(alpha = CreateDIBSection(hdc, info, DIB_RGB_COLORS, &bits, 0, 0)))
        goto done;

    if (src_info)
    {
        SelectObject(hdc, alpha);
        StretchDIBits(hdc, 0, 0, bm.bmWidth, bm.bmHeight, 0, 0, src_info->bmiHeader.biWidth, -src_info->bmiHeader.biHeight, color_bits, src_info, DIB_RGB_COLORS, SRCCOPY);
    }
    else
    {
        GetDIBits(hdc, color, 0, bm.bmHeight, bits, info, DIB_RGB_COLORS);
        if (!bmi_has_alpha(info, bits))
        {
            DeleteObject(alpha);
            alpha = 0;
            goto done;
        }
    }

    /* pre-multiply by alpha */
    for (i = 0, ptr = (unsigned char *)bits; i < bm.bmWidth * bm.bmHeight; i++, ptr += 4)
    {
        unsigned int alpha = ptr[3];
        ptr[0] = (ptr[0] * alpha + 127) / 255;
        ptr[1] = (ptr[1] * alpha + 127) / 255;
        ptr[2] = (ptr[2] * alpha + 127) / 255;
    }

done:
    DeleteDC(hdc);
    free(info);
    return alpha;
}

static BOOL create_icon_frame(const BITMAPINFO *bmi, DWORD maxsize, POINT hotspot, BOOL is_icon, INT width, INT height, UINT flags, struct cursoricon_frame *frame)
{
    DWORD size, color_size, mask_size, compr;
    const void *color_bits, *mask_bits;
    void *alpha_mask_bits = NULL;
    LONG bmi_width, bmi_height;
    BITMAPINFO *bmi_copy;
    BOOL do_stretch;
    HDC hdc = 0;
    WORD bpp;
    BOOL ret = FALSE;

    memset(frame, 0, sizeof(*frame));

    /* Check bitmap header */

    if (bmi->bmiHeader.biSize == PNG_SIGN)
    {
        BITMAPINFO *bmi_png;
        if (!(bmi_png = load_png((const char *)bmi, &maxsize)))
            return FALSE;
        ret = create_icon_frame(bmi_png, maxsize, hotspot, is_icon, width, height, flags, frame);
        free(bmi_png);
        return ret;
    }

    if (maxsize < sizeof(BITMAPCOREHEADER))
    {
        SLOG_FMTW("invalid size %u\n", maxsize);
        return FALSE;
    }
    if (maxsize < bmi->bmiHeader.biSize)
    {
        SLOG_FMTW("invalid header size %u\n", bmi->bmiHeader.biSize);
        return FALSE;
    }
    if ((bmi->bmiHeader.biSize != sizeof(BITMAPCOREHEADER)) && (bmi->bmiHeader.biSize != sizeof(BITMAPINFOHEADER) || (bmi->bmiHeader.biCompression != BI_RGB && bmi->bmiHeader.biCompression != BI_BITFIELDS)))
    {
        SLOG_FMTW("invalid bitmap header %u\n", bmi->bmiHeader.biSize);
        return FALSE;
    }

    size = bitmap_info_size(bmi, DIB_RGB_COLORS);
    DIB_GetBitmapInfo(&bmi->bmiHeader, &bmi_width, &bmi_height, &bpp, &compr);
    color_size = get_dib_image_size(bmi_width, bmi_height / 2, bpp);
    mask_size = get_dib_image_size(bmi_width, bmi_height / 2, 1);
    if (size > maxsize || color_size > maxsize - size)
    {
        SLOG_FMTW("truncated file %u < %u+%u+%u\n", maxsize, size, color_size, mask_size);
        return 0;
    }
    if (mask_size > maxsize - size - color_size)
        mask_size = 0; /* no mask */

    if (flags & LR_DEFAULTSIZE)
    {
        if (!width)
            width = GetSystemMetrics(is_icon ? SM_CXICON : SM_CXCURSOR);
        if (!height)
            height = GetSystemMetrics(is_icon ? SM_CYICON : SM_CYCURSOR);
    }
    else
    {
        if (!width)
            width = bmi_width;
        if (!height)
            height = bmi_height / 2;
    }
    do_stretch = (bmi_height / 2 != height) || (bmi_width != width);

    /* Scale the hotspot */
    if (is_icon)
    {
        hotspot.x = width / 2;
        hotspot.y = height / 2;
    }
    else if (do_stretch)
    {
        hotspot.x = (hotspot.x * width) / bmi_width;
        hotspot.y = (hotspot.y * height) / (bmi_height / 2);
    }

    if (!(bmi_copy = (BITMAPINFO *)malloc(smax(size, FIELD_OFFSET(BITMAPINFO, bmiColors[2])))))
        return 0;
    if (!(hdc = CreateCompatibleDC(0)))
        goto done;

    memcpy(bmi_copy, bmi, size);
    if (bmi_copy->bmiHeader.biSize != sizeof(BITMAPCOREHEADER))
        bmi_copy->bmiHeader.biHeight /= 2;
    else
        ((BITMAPCOREINFO *)bmi_copy)->bmciHeader.bcHeight /= 2;
    bmi_height /= 2;

    color_bits = (const char *)bmi + size;
    mask_bits = (const char *)color_bits + color_size;

    if (is_dib_monochrome(bmi))
    {
        if (!(frame->mask = CreateBitmap(width, height * 2, 1, 1, NULL)))
            goto done;

        /* copy color data into second half of mask bitmap */
        SelectObject(hdc, frame->mask);
        StretchDIBits(hdc, 0, height, width, height, 0, 0, bmi_width, -bmi_height, color_bits, bmi_copy, DIB_RGB_COLORS, SRCCOPY);
    }
    else
    {
        if (!(frame->mask = CreateBitmap(width, height, 1, 1, NULL)))
            goto done;
        if (!(frame->color = create_color_bitmap(width, height)))
            goto done;
        SelectObject(hdc, frame->color);
        StretchDIBits(hdc, 0, 0, width, height, 0, 0, bmi_width, -bmi_height, color_bits, bmi_copy, DIB_RGB_COLORS, SRCCOPY);

        if (bmi_has_alpha(bmi_copy, color_bits))
        {
            frame->alpha = create_alpha_bitmap(frame->color, bmi_copy, color_bits);
            if (!mask_size) /* generate mask from alpha */
            {
                LONG x, y, dst_stride = ((bmi_width + 31) / 8) & ~3;

                if ((alpha_mask_bits = malloc(bmi_height * dst_stride)))
                {
                    static const unsigned char masks[] = { 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1 };
                    const DWORD *src = (const DWORD *)color_bits;
                    unsigned char *dst = (unsigned char *)alpha_mask_bits;

                    for (y = 0; y < bmi_height; y++, src += bmi_width, dst += dst_stride)
                        for (x = 0; x < bmi_width; x++)
                            if (src[x] >> 24 != 0xff)
                                dst[x >> 3] |= masks[x & 7];

                    mask_bits = alpha_mask_bits;
                    mask_size = bmi_height * dst_stride;
                }
            }
        }

        /* convert info to monochrome to copy the mask */
        if (bmi_copy->bmiHeader.biSize != sizeof(BITMAPCOREHEADER))
        {
            RGBQUAD *rgb = bmi_copy->bmiColors;

            bmi_copy->bmiHeader.biBitCount = 1;
            bmi_copy->bmiHeader.biClrUsed = bmi_copy->bmiHeader.biClrImportant = 2;
            rgb[0].rgbBlue = rgb[0].rgbGreen = rgb[0].rgbRed = 0x00;
            rgb[1].rgbBlue = rgb[1].rgbGreen = rgb[1].rgbRed = 0xff;
            rgb[0].rgbReserved = rgb[1].rgbReserved = 0;
        }
        else
        {
            RGBTRIPLE *rgb = (RGBTRIPLE *)(((BITMAPCOREHEADER *)bmi_copy) + 1);

            ((BITMAPCOREINFO *)bmi_copy)->bmciHeader.bcBitCount = 1;
            rgb[0].rgbtBlue = rgb[0].rgbtGreen = rgb[0].rgbtRed = 0x00;
            rgb[1].rgbtBlue = rgb[1].rgbtGreen = rgb[1].rgbtRed = 0xff;
        }
    }

    if (mask_size)
    {
        SelectObject(hdc, frame->mask);
        StretchDIBits(hdc, 0, 0, width, height, 0, 0, bmi_width, bmi_height, mask_bits, bmi_copy, DIB_RGB_COLORS, SRCCOPY);
    }

    frame->width = width;
    frame->height = height;
    frame->hotspot = hotspot;
    ret = TRUE;

done:
    if (!ret)
        free_icon_frame(frame);
    DeleteDC(hdc);
    free(bmi_copy);
    free(alpha_mask_bits);
    return ret;
}

static HICON create_cursoricon_object(struct cursoricon_desc *desc, BOOL is_icon, HINSTANCE module, const WCHAR *resname, HRSRC rsrc)
{
    if (desc->num_frames == 0)
        return NULL;
    ICONINFO info;
    info.fIcon = is_icon;
    info.hbmColor = desc->frames[0].color;
    info.hbmMask = desc->frames[0].mask;
    info.xHotspot = desc->frames[0].hotspot.x;
    info.yHotspot = desc->frames[0].hotspot.y;
    return CreateIconIndirect(&info);
}

/***********************************************************************
 *          create_icon_from_bmi
 *
 * Create an icon from its BITMAPINFO.
 */
static HICON create_icon_from_bmi(const BITMAPINFO *bmi, DWORD maxsize, HMODULE module, LPCWSTR resname, HRSRC rsrc, POINT hotspot, BOOL bIcon, INT width, INT height, UINT flags)
{
    struct cursoricon_frame frame;
    struct cursoricon_desc desc;
    {
        desc.flags = flags, desc.num_frames = 1, desc.frames = &frame;
    }

    if (!create_icon_frame(bmi, maxsize, hotspot, bIcon, width, height, flags, &frame))
        return 0;

    HICON ret = create_cursoricon_object(&desc, bIcon, module, resname, rsrc);
    free_icon_frame(&frame);
    return ret;
}

/**********************************************************************
 *          .ANI cursor support
 */
#define ANI_RIFF_ID RIFF_FOURCC('R', 'I', 'F', 'F')
#define ANI_LIST_ID RIFF_FOURCC('L', 'I', 'S', 'T')
#define ANI_ACON_ID RIFF_FOURCC('A', 'C', 'O', 'N')
#define ANI_anih_ID RIFF_FOURCC('a', 'n', 'i', 'h')
#define ANI_seq__ID RIFF_FOURCC('s', 'e', 'q', ' ')
#define ANI_fram_ID RIFF_FOURCC('f', 'r', 'a', 'm')
#define ANI_rate_ID RIFF_FOURCC('r', 'a', 't', 'e')

#define ANI_FLAG_ICON     0x1
#define ANI_FLAG_SEQUENCE 0x2

typedef struct
{
    DWORD header_size;
    DWORD num_frames;
    DWORD num_steps;
    DWORD width;
    DWORD height;
    DWORD bpp;
    DWORD num_planes;
    DWORD display_rate;
    DWORD flags;
} ani_header;

typedef struct
{
    DWORD data_size;
    const unsigned char *data;
} riff_chunk_t;

static void dump_ani_header(const ani_header *header)
{
    TRACE("     header size: %d\n", header->header_size);
    TRACE("          frames: %d\n", header->num_frames);
    TRACE("           steps: %d\n", header->num_steps);
    TRACE("           width: %d\n", header->width);
    TRACE("          height: %d\n", header->height);
    TRACE("             bpp: %d\n", header->bpp);
    TRACE("          planes: %d\n", header->num_planes);
    TRACE("    display rate: %d\n", header->display_rate);
    TRACE("           flags: 0x%08x\n", header->flags);
}

/*
 * RIFF:
 * DWORD "RIFF"
 * DWORD size
 * DWORD riff_id
 * BYTE[] data
 *
 * LIST:
 * DWORD "LIST"
 * DWORD size
 * DWORD list_id
 * BYTE[] data
 *
 * CHUNK:
 * DWORD chunk_id
 * DWORD size
 * BYTE[] data
 */
static void riff_find_chunk(DWORD chunk_id, DWORD chunk_type, const riff_chunk_t *parent_chunk, riff_chunk_t *chunk)
{
    const unsigned char *ptr = parent_chunk->data;
    const unsigned char *end = parent_chunk->data + (parent_chunk->data_size - (2 * sizeof(DWORD)));

    if (chunk_type == ANI_LIST_ID || chunk_type == ANI_RIFF_ID)
        end -= sizeof(DWORD);

    while (ptr < end)
    {
        if ((!chunk_type && *(const DWORD *)ptr == chunk_id) || (chunk_type && *(const DWORD *)ptr == chunk_type && *((const DWORD *)ptr + 2) == chunk_id))
        {
            ptr += sizeof(DWORD);
            chunk->data_size = (*(const DWORD *)ptr + 1) & ~1;
            ptr += sizeof(DWORD);
            if (chunk_type == ANI_LIST_ID || chunk_type == ANI_RIFF_ID)
                ptr += sizeof(DWORD);
            chunk->data = ptr;

            return;
        }

        ptr += sizeof(DWORD);
        if (ptr >= end)
            break;
        ptr += (*(const DWORD *)ptr + 1) & ~1;
        ptr += sizeof(DWORD);
    }
}

/*
 * .ANI layout:
 *
 * RIFF:'ACON'                  RIFF chunk
 *     |- CHUNK:'anih'          Header
 *     |- CHUNK:'seq '          Sequence information (optional)
 *     \- LIST:'fram'           Frame list
 *            |- CHUNK:icon     Cursor frames
 *            |- CHUNK:icon
 *            |- ...
 *            \- CHUNK:icon
 */
static HCURSOR CURSORICON_CreateIconFromANI(const BYTE *bits, DWORD bits_size, INT width, INT height, INT depth, BOOL is_icon, UINT loadflags)
{
    struct cursoricon_desc desc = { 0 };
    ani_header header;
    HCURSOR cursor;
    UINT i;
    BOOL error = FALSE;

    riff_chunk_t root_chunk = { bits_size, bits };
    riff_chunk_t ACON_chunk = { 0 };
    riff_chunk_t anih_chunk = { 0 };
    riff_chunk_t fram_chunk = { 0 };
    riff_chunk_t rate_chunk = { 0 };
    riff_chunk_t seq_chunk = { 0 };
    const unsigned char *icon_chunk;
    const unsigned char *icon_data;

    TRACE("bits %p, bits_size %d\n", bits, bits_size);

    riff_find_chunk(ANI_ACON_ID, ANI_RIFF_ID, &root_chunk, &ACON_chunk);
    if (!ACON_chunk.data)
    {
        ERR("Failed to get root chunk.\n");
        return 0;
    }

    riff_find_chunk(ANI_anih_ID, 0, &ACON_chunk, &anih_chunk);
    if (!anih_chunk.data)
    {
        ERR("Failed to get 'anih' chunk.\n");
        return 0;
    }
    memcpy(&header, anih_chunk.data, sizeof(header));
    dump_ani_header(&header);

    if (!(header.flags & ANI_FLAG_ICON))
    {
        FIXME("Raw animated icon/cursor data is not currently supported.\n");
        return 0;
    }

    if (header.flags & ANI_FLAG_SEQUENCE)
    {
        riff_find_chunk(ANI_seq__ID, 0, &ACON_chunk, &seq_chunk);
        if (seq_chunk.data)
        {
            desc.frame_seq = (DWORD *)seq_chunk.data;
        }
        else
        {
            FIXME("Sequence data expected but not found, assuming steps == frames.\n");
            header.num_steps = header.num_frames;
        }
    }

    riff_find_chunk(ANI_rate_ID, 0, &ACON_chunk, &rate_chunk);
    if (rate_chunk.data)
        desc.frame_rates = (DWORD *)rate_chunk.data;

    riff_find_chunk(ANI_fram_ID, ANI_LIST_ID, &ACON_chunk, &fram_chunk);
    if (!fram_chunk.data)
    {
        ERR("Failed to get icon list.\n");
        return 0;
    }

    /* The .ANI stores the display rate in jiffies (1/60s) */
    desc.delay = header.display_rate;
    desc.num_frames = header.num_frames;
    desc.num_steps = header.num_steps;
    if (!(desc.frames = (cursoricon_frame *)HeapAlloc(GetProcessHeap(), 0, sizeof(*desc.frames) * desc.num_frames)))
        return 0;

    icon_chunk = fram_chunk.data;
    icon_data = fram_chunk.data + (2 * sizeof(DWORD));
    for (i = 0; i < desc.num_frames; i++)
    {
        const DWORD chunk_size = *(const DWORD *)(icon_chunk + sizeof(DWORD));
        const CURSORICONFILEDIRENTRY *entry;
        INT frameWidth, frameHeight;
        const BITMAPINFO *bmi;
        POINT hotspot;

        entry = CURSORICON_FindBestIconFile((const CURSORICONFILEDIR *)icon_data, bits + bits_size - icon_data, width, height, depth, loadflags);

        hotspot.x = entry->xHotspot;
        hotspot.y = entry->yHotspot;
        if (!header.width || !header.height)
        {
            frameWidth = entry->bWidth;
            frameHeight = entry->bHeight;
        }
        else
        {
            frameWidth = header.width;
            frameHeight = header.height;
        }

        if (!(error = entry->dwDIBOffset >= bits + bits_size - icon_data))
        {
            bmi = (const BITMAPINFO *)(icon_data + entry->dwDIBOffset);
            /* Grab a frame from the animation */
            error = !create_icon_frame(bmi, bits + bits_size - (const BYTE *)bmi, hotspot, is_icon, frameWidth, frameHeight, loadflags, &desc.frames[i]);
        }

        if (error)
        {
            if (i == 0)
            {
                SLOG_FMTW("Completely failed to create animated cursor!\n");
                HeapFree(GetProcessHeap(), 0, desc.frames);
                return 0;
            }
            /* There was an error but we at least decoded the first frame, so just use that frame */
            SLOG_FMTW("Error creating animated cursor, only using first frame!\n");
            while (i > 1)
                free_icon_frame(&desc.frames[--i]);
            desc.num_frames = desc.num_steps = 1;
            desc.frame_seq = NULL;
            desc.delay = 0;
            break;
        }

        /* Advance to the next chunk */
        icon_chunk += chunk_size + (2 * sizeof(DWORD));
        icon_data = icon_chunk + (2 * sizeof(DWORD));
    }

    cursor = create_cursoricon_object(&desc, is_icon, 0, 0, 0);
    if (!cursor)
        for (i = 0; i < desc.num_frames; i++)
            free_icon_frame(&desc.frames[i]);
    HeapFree(GetProcessHeap(), 0, desc.frames);
    return cursor;
}

/**********************************************************************
 *		CreateIconFromResourceEx (USER32.@)
 *
 * FIXME: Convert to mono when cFlag is LR_MONOCHROME.
 */
HICON WINAPI CreateIconFromResourceEx(LPBYTE bits, UINT cbSize, BOOL bIcon, DWORD dwVersion, INT width, INT height, UINT cFlag)
{
    POINT hotspot;
    const BITMAPINFO *bmi;

    SLOG_FMTD("%p (%u bytes), ver %08x, %ix%i %s %s\n", bits, cbSize, dwVersion, width, height, bIcon ? "icon" : "cursor", (cFlag & LR_MONOCHROME) ? "mono" : "");

    if (!bits)
        return 0;

    if (dwVersion == 0x00020000)
    {
        SLOG_FMTW("\t2.xx resources are not supported\n");
        return 0;
    }

    /* Check if the resource is an animated icon/cursor */
    if (!memcmp(bits, "RIFF", 4))
        return CURSORICON_CreateIconFromANI(bits, cbSize, width, height, 0 /* default depth */, bIcon, cFlag);

    if (bIcon)
    {
        hotspot.x = width / 2;
        hotspot.y = height / 2;
        bmi = (BITMAPINFO *)bits;
    }
    else /* get the hotspot */
    {
        const SHORT *pt = (const SHORT *)bits;
        hotspot.x = pt[0];
        hotspot.y = pt[1];
        bmi = (const BITMAPINFO *)(pt + 2);
        cbSize -= 2 * sizeof(*pt);
    }

    return create_icon_from_bmi(bmi, cbSize, NULL, NULL, 0, hotspot, bIcon, width, height, cFlag);
}

/**********************************************************************
 *		CreateIconFromResource (USER32.@)
 */
HICON WINAPI CreateIconFromResource(LPBYTE bits, UINT cbSize, BOOL bIcon, DWORD dwVersion)
{
    return CreateIconFromResourceEx(bits, cbSize, bIcon, dwVersion, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
}

static HICON CURSORICON_LoadFromBuf(const char *bits, DWORD filesize, INT width, INT height, INT depth, BOOL fCursor, UINT loadflags)
{
    const CURSORICONFILEDIRENTRY *entry;
    CURSORICONFILEDIR *dir;
    HICON hIcon = 0;
    POINT hotspot;

    // TRACE("loading %s\n", debugstr_w( filename ));

    /* Check for .ani. */
    if (memcmp(bits, "RIFF", 4) == 0)
    {
        return CURSORICON_CreateIconFromANI((const PBYTE)bits, filesize, width, height, depth, !fCursor, loadflags);
    }

    dir = (CURSORICONFILEDIR *)bits;
    if (filesize < FIELD_OFFSET(CURSORICONFILEDIR, idEntries) + sizeof(CURSORICONFILEDIRENTRY) * dir->idCount)
        return 0;

    if (fCursor)
        entry = CURSORICON_FindBestCursorFile(dir, filesize, width, height, depth, loadflags);
    else
        entry = CURSORICON_FindBestIconFile(dir, filesize, width, height, depth, loadflags);

    if (!entry)
        return 0;

    /* check that we don't run off the end of the file */
    if (entry->dwDIBOffset > filesize)
        return 0;
    if (entry->dwDIBOffset + entry->dwDIBSize > filesize)
        return 0;

    hotspot.x = entry->xHotspot;
    hotspot.y = entry->yHotspot;
    return create_icon_from_bmi((const BITMAPINFO *)&bits[entry->dwDIBOffset], filesize - entry->dwDIBOffset, NULL, NULL, 0, hotspot, !fCursor, width, height, loadflags);
}

static HICON CURSORICON_LoadFromFile(LPCSTR filename, INT width, INT height, INT depth, BOOL fCursor, UINT loadflags)
{
    HICON hIcon = 0;
    MapFileInfo info;
    if (!map_file(filename, &info))
        return hIcon;
    hIcon = CURSORICON_LoadFromBuf((const char *)info.ptr, info.fileSize, width, height, depth, fCursor, loadflags);
    map_close(&info);
    return hIcon;
}

/**********************************************************************
 *          CURSORICON_Load
 *
 * Load a cursor or icon from resource or file.
 */

static HBITMAP create_masked_bitmap(int width, int height, const void *and_, const void *xor_)
{
    HBITMAP and_bitmap, xor_bitmap, bitmap;
    HDC src_dc, dst_dc;

    and_bitmap = CreateBitmap(width, height, 1, 1, and_);
    xor_bitmap = CreateBitmap(width, height, 1, 1, xor_);
    bitmap = CreateBitmap(width, height * 2, 1, 1, NULL);
    src_dc = CreateCompatibleDC(0);
    dst_dc = CreateCompatibleDC(0);

    SelectObject(dst_dc, bitmap);
    SelectObject(src_dc, and_bitmap);
    BitBlt(dst_dc, 0, 0, width, height, src_dc, 0, 0, SRCCOPY);
    SelectObject(src_dc, xor_bitmap);
    BitBlt(dst_dc, 0, height, width, height, src_dc, 0, 0, SRCCOPY);

    DeleteObject(and_bitmap);
    DeleteObject(xor_bitmap);
    DeleteDC(src_dc);
    DeleteDC(dst_dc);
    return bitmap;
}

/* copy an icon bitmap, even when it can't be selected into a DC */
/* helper for CreateIconIndirect */
static void stretch_blt_icon(HDC hdc_dst, int dst_x, int dst_y, int dst_width, int dst_height, HBITMAP src, int width, int height)
{
    HDC hdc = CreateCompatibleDC(0);

    if (!SelectObject(hdc, src)) /* do it the hard way */
    {
        BITMAPINFO *info;
        void *bits;

        if (!(info = (BITMAPINFO *)HeapAlloc(GetProcessHeap(), 0, FIELD_OFFSET(BITMAPINFO, bmiColors[256]))))
            return;
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = width;
        info->bmiHeader.biHeight = height;
        info->bmiHeader.biPlanes = GetDeviceCaps(hdc_dst, PLANES);
        info->bmiHeader.biBitCount = GetDeviceCaps(hdc_dst, BITSPIXEL);
        info->bmiHeader.biCompression = BI_RGB;
        info->bmiHeader.biSizeImage = get_dib_image_size(width, height, info->bmiHeader.biBitCount);
        info->bmiHeader.biXPelsPerMeter = 0;
        info->bmiHeader.biYPelsPerMeter = 0;
        info->bmiHeader.biClrUsed = 0;
        info->bmiHeader.biClrImportant = 0;
        bits = HeapAlloc(GetProcessHeap(), 0, info->bmiHeader.biSizeImage);
        if (bits && GetDIBits(hdc, src, 0, height, bits, info, DIB_RGB_COLORS))
            StretchDIBits(hdc_dst, dst_x, dst_y, dst_width, dst_height, 0, 0, width, height, bits, info, DIB_RGB_COLORS, SRCCOPY);

        HeapFree(GetProcessHeap(), 0, bits);
        HeapFree(GetProcessHeap(), 0, info);
    }
    else
        StretchBlt(hdc_dst, dst_x, dst_y, dst_width, dst_height, hdc, 0, 0, width, height, SRCCOPY);

    DeleteDC(hdc);
}

/***********************************************************************
 *           DIB_FixColorsToLoadflags
 *
 * Change color table entries when LR_LOADTRANSPARENT or LR_LOADMAP3DCOLORS
 * are in loadflags
 */
static void DIB_FixColorsToLoadflags(BITMAPINFO *bmi, UINT loadflags, BYTE pix)
{
    int colors;
    COLORREF c_W, c_S, c_F, c_L, c_C;
    int incr, i;
    RGBQUAD *ptr;
    int bitmap_type;
    LONG width;
    LONG height;
    WORD bpp;
    DWORD compr;

    if (((bitmap_type = DIB_GetBitmapInfo((BITMAPINFOHEADER *)bmi, &width, &height, &bpp, &compr)) == -1))
    {
        SLOG_FMTW("Invalid bitmap\n");
        return;
    }

    if (bpp > 8)
        return;

    if (bitmap_type == 0) /* BITMAPCOREHEADER */
    {
        incr = 3;
        colors = 1 << bpp;
    }
    else
    {
        incr = 4;
        colors = bmi->bmiHeader.biClrUsed;
        if (colors > 256)
            colors = 256;
        if (!colors && (bpp <= 8))
            colors = 1 << bpp;
    }

    c_W = GetSysColor(COLOR_WINDOW);
    c_S = GetSysColor(COLOR_3DSHADOW);
    c_F = GetSysColor(COLOR_3DFACE);
    c_L = GetSysColor(COLOR_3DLIGHT);

    if (loadflags & LR_LOADTRANSPARENT)
    {
        switch (bpp)
        {
        case 1:
            pix = pix >> 7;
            break;
        case 4:
            pix = pix >> 4;
            break;
        case 8:
            break;
        default:
            SLOG_FMTW("(%d): Unsupported depth\n", bpp);
            return;
        }
        if (pix >= colors)
        {
            SLOG_FMTW("pixel has color index greater than biClrUsed!\n");
            return;
        }
        if (loadflags & LR_LOADMAP3DCOLORS)
            c_W = c_F;
        ptr = (RGBQUAD *)((char *)bmi->bmiColors + pix * incr);
        ptr->rgbBlue = GetBValue(c_W);
        ptr->rgbGreen = GetGValue(c_W);
        ptr->rgbRed = GetRValue(c_W);
    }
    if (loadflags & LR_LOADMAP3DCOLORS)
        for (i = 0; i < colors; i++)
        {
            ptr = (RGBQUAD *)((char *)bmi->bmiColors + i * incr);
            c_C = RGB(ptr->rgbRed, ptr->rgbGreen, ptr->rgbBlue);
            if (c_C == RGB(128, 128, 128))
            {
                ptr->rgbRed = GetRValue(c_S);
                ptr->rgbGreen = GetGValue(c_S);
                ptr->rgbBlue = GetBValue(c_S);
            }
            else if (c_C == RGB(192, 192, 192))
            {
                ptr->rgbRed = GetRValue(c_F);
                ptr->rgbGreen = GetGValue(c_F);
                ptr->rgbBlue = GetBValue(c_F);
            }
            else if (c_C == RGB(223, 223, 223))
            {
                ptr->rgbRed = GetRValue(c_L);
                ptr->rgbGreen = GetGValue(c_L);
                ptr->rgbBlue = GetBValue(c_L);
            }
        }
}

/**********************************************************************
 *       BITMAP_Load
 */
static HBITMAP BITMAP_LoadBuf(const char *ptr, UINT length, INT desiredx, INT desiredy, UINT loadflags)
{
    HBITMAP hbitmap = 0, orig_bm;
    BITMAPINFO *info, *fix_info = NULL, *scaled_info = NULL;
    int size;
    BYTE pix;
    char *bits;
    LONG width, height, new_width, new_height;
    WORD bpp_dummy;
    DWORD compr_dummy, offbits = 0;
    INT bm_type;
    HDC screen_mem_dc = NULL;

    BITMAPFILEHEADER *bmfh;

    info = (BITMAPINFO *)(ptr + sizeof(BITMAPFILEHEADER));
    bmfh = (BITMAPFILEHEADER *)ptr;
    if (bmfh->bfType != 0x4d42 /* 'BM' */)
    {
        SLOG_FMTW("Invalid/unsupported bitmap format!\n");
        goto end;
    }
    if (bmfh->bfOffBits)
        offbits = bmfh->bfOffBits - sizeof(BITMAPFILEHEADER);

    bm_type = DIB_GetBitmapInfo(&info->bmiHeader, &width, &height, &bpp_dummy, &compr_dummy);
    if (bm_type == -1)
    {
        SLOG_FMTW("Invalid bitmap format!\n");
        goto end;
    }

    size = bitmap_info_size(info, DIB_RGB_COLORS);
    fix_info = (BITMAPINFO *)HeapAlloc(GetProcessHeap(), 0, size);
    scaled_info = (BITMAPINFO *)HeapAlloc(GetProcessHeap(), 0, size);

    if (!fix_info || !scaled_info)
        goto end;
    memcpy(fix_info, info, size);

    pix = *((LPBYTE)info + size);
    DIB_FixColorsToLoadflags(fix_info, loadflags, pix);

    memcpy(scaled_info, fix_info, size);

    if (desiredx != 0)
        new_width = desiredx;
    else
        new_width = width;

    if (desiredy != 0)
        new_height = height > 0 ? desiredy : -desiredy;
    else
        new_height = height;

    if (bm_type == 0)
    {
        BITMAPCOREHEADER *core = (BITMAPCOREHEADER *)&scaled_info->bmiHeader;
        core->bcWidth = (WORD)new_width;
        core->bcHeight = (WORD)new_height;
    }
    else
    {
        /* Some sanity checks for BITMAPINFO (not applicable to BITMAPCOREINFO) */
        if (info->bmiHeader.biHeight > 65535 || info->bmiHeader.biWidth > 65535)
        {
            SLOG_FMTW("Broken BitmapInfoHeader!\n");
            goto end;
        }

        scaled_info->bmiHeader.biWidth = new_width;
        scaled_info->bmiHeader.biHeight = new_height;
    }

    if (new_height < 0)
        new_height = -new_height;

    if (!(screen_mem_dc = CreateCompatibleDC(0)))
        goto end;

    bits = (char *)info + (offbits ? offbits : size);

    if (loadflags & LR_CREATEDIBSECTION)
    {
        scaled_info->bmiHeader.biCompression = 0; /* DIBSection can't be compressed */
        hbitmap = CreateDIBSection(0, scaled_info, DIB_RGB_COLORS, NULL, 0, 0);
    }
    else
    {
        if (is_dib_monochrome(fix_info))
            hbitmap = CreateBitmap(new_width, new_height, 1, 1, NULL);
        else
            hbitmap = create_color_bitmap(new_width, new_height);
    }

    orig_bm = (HBITMAP)SelectObject(screen_mem_dc, hbitmap);
    if (info->bmiHeader.biBitCount > 1)
        SetStretchBltMode(screen_mem_dc, HALFTONE);
    StretchDIBits(screen_mem_dc, 0, 0, new_width, new_height, 0, 0, width, height, bits, fix_info, DIB_RGB_COLORS, SRCCOPY);
    SelectObject(screen_mem_dc, orig_bm);

end:
    if (screen_mem_dc)
        DeleteDC(screen_mem_dc);
    HeapFree(GetProcessHeap(), 0, scaled_info);
    HeapFree(GetProcessHeap(), 0, fix_info);
    return hbitmap;
}

static HBITMAP BITMAP_Load(HINSTANCE instance, LPCSTR name, INT desiredx, INT desiredy, UINT loadflags)
{

    if (!(loadflags & LR_LOADFROMFILE))
    {
        // if (!instance)
        // {
        //     /* OEM bitmap: try to load the resource from user32.dll */
        //     instance = user32_module;
        // }

        // if (!(hRsrc = FindResourceW( instance, name, (LPWSTR)RT_BITMAP ))) return 0;
        // if (!(handle = LoadResource( instance, hRsrc ))) return 0;

        // if ((info = LockResource( handle )) == NULL)
        return 0;
    }
    else
    {
        MapFileInfo minfo;
        if (!(map_file(name, &minfo)))
            return 0;
        HBITMAP ret = BITMAP_LoadBuf((const char *)minfo.ptr, minfo.fileSize, desiredx, desiredy, loadflags);
        map_close(&minfo);
        return ret;
    }
}

HANDLE WINAPI LoadImageBuf(const void *buf, UINT length, UINT type, INT desiredx, INT desiredy, UINT loadflags)
{
    int depth;
    switch (type)
    {
    case IMAGE_BITMAP:
        return (HANDLE)BITMAP_LoadBuf((const char *)buf, length, desiredx, desiredy, loadflags);

    case IMAGE_ICON:
    case IMAGE_CURSOR:
        depth = 1;
        if (!(loadflags & LR_MONOCHROME))
            depth = get_display_bpp();
        return (HANDLE)CURSORICON_LoadFromBuf((const char *)buf, length, desiredx, desiredy, depth, (type == IMAGE_CURSOR), loadflags);
    }
    return 0;
}

/* StretchBlt from src to dest; helper for CopyImage(). */
static void stretch_bitmap(HBITMAP dst, HBITMAP src, int dst_width, int dst_height, int src_width, int src_height)
{
    HDC src_dc = CreateCompatibleDC(0), dst_dc = CreateCompatibleDC(0);

    SelectObject(src_dc, src);
    SelectObject(dst_dc, dst);
    StretchBlt(dst_dc, 0, 0, dst_width, dst_height, src_dc, 0, 0, src_width, src_height, SRCCOPY);

    DeleteDC(src_dc);
    DeleteDC(dst_dc);
}

#ifndef _WIN32

/***********************************************************************
 *		CreateCursor (USER32.@)
 */
HCURSOR WINAPI CreateCursor(HINSTANCE instance, int hotspot_x, int hotspot_y, int width, int height, const void *and_, const void *xor_)
{
    ICONINFO info;
    HCURSOR cursor;

    TRACE("hotspot (%d,%d), size %dx%d\n", hotspot_x, hotspot_y, width, height);

    info.fIcon = FALSE;
    info.xHotspot = hotspot_x;
    info.yHotspot = hotspot_y;
    info.hbmColor = NULL;
    info.hbmMask = create_masked_bitmap(width, height, and_, xor_);
    cursor = CreateIconIndirect(&info);
    DeleteObject(info.hbmMask);
    return cursor;
}

/***********************************************************************
 *		CreateIcon (USER32.@)
 *
 *  Creates an icon based on the specified bitmaps. The bitmaps must be
 *  provided in a device dependent format and will be resized to
 *  (SM_CXICON,SM_CYICON) and depth converted to match the screen's color
 *  depth. The provided bitmaps must be top-down bitmaps.
 *  Although Windows does not support 15bpp(*) this API must support it
 *  for Winelib applications.
 *
 *  (*) Windows does not support 15bpp but it supports the 555 RGB 16bpp
 *      format!
 *
 * RETURNS
 *  Success: handle to an icon
 *  Failure: NULL
 *
 * FIXME: Do we need to resize the bitmaps?
 */
HICON WINAPI CreateIcon(HINSTANCE instance, int width, int height, BYTE planes, BYTE depth, const void *and_, const void *xor_)
{
    ICONINFO info;
    HICON icon;

    SLOG_FMTD("%dx%d, planes %d, depth %d\n", width, height, planes, depth);

    info.fIcon = TRUE;
    info.xHotspot = width / 2;
    info.yHotspot = height / 2;
    if (depth == 1)
    {
        info.hbmColor = NULL;
        info.hbmMask = create_masked_bitmap(width, height, and_, xor_);
    }
    else
    {
        info.hbmColor = CreateBitmap(width, height, planes, depth, xor_);
        info.hbmMask = CreateBitmap(width, height, 1, 1, and_);
    }

    icon = CreateIconIndirect(&info);

    DeleteObject(info.hbmMask);
    DeleteObject(info.hbmColor);

    return icon;
}

/**********************************************************************
 *		LookupIconIdFromDirectoryEx (USER32.@)
 */
INT WINAPI LookupIconIdFromDirectoryEx(LPBYTE xdir, BOOL bIcon, INT width, INT height, UINT cFlag)
{
    const CURSORICONDIR *dir = (const CURSORICONDIR *)xdir;
    UINT retVal = 0;
    if (dir && !dir->idReserved && (dir->idType & 3))
    {
        const CURSORICONDIRENTRY *entry;
        int depth = (cFlag & LR_MONOCHROME) ? 1 : get_display_bpp();

        if (bIcon)
            entry = CURSORICON_FindBestIconRes(dir, ~0u, width, height, depth, LR_DEFAULTSIZE);
        else
            entry = CURSORICON_FindBestCursorRes(dir, ~0u, width, height, depth, LR_DEFAULTSIZE);

        if (entry)
            retVal = entry->wResId;
    }
    else
        SLOG_FMTW("invalid resource directory\n");
    return retVal;
}

/**********************************************************************
 *              LookupIconIdFromDirectory (USER32.@)
 */
INT WINAPI LookupIconIdFromDirectory(LPBYTE dir, BOOL bIcon)
{
    return LookupIconIdFromDirectoryEx(dir, bIcon, 0, 0, bIcon ? 0 : LR_MONOCHROME);
}

/***********************************************************************
 *		LoadIconW (USER32.@)
 */
HICON WINAPI LoadIconW(HINSTANCE hInstance, LPCWSTR name)
{

    return (HICON)LoadImageW(hInstance, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

/***********************************************************************
 *              LoadIconA (USER32.@)
 */
HICON WINAPI LoadIconA(HINSTANCE hInstance, LPCSTR name)
{

    return (HICON)LoadImageA(hInstance, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

/**********************************************************************
 *		LoadImageA (USER32.@)
 *
 * See LoadImageW.
 */
HANDLE WINAPI LoadImageW(HINSTANCE hinst, LPCWSTR name, UINT type, INT desiredx, INT desiredy, UINT loadflags)
{
    HANDLE res;
    LPSTR u_name;

    if (IS_INTRESOURCE(name))
        return LoadImageA(hinst, (LPCSTR)name, type, desiredx, desiredy, loadflags);
    DWORD len = WideCharToMultiByte(CP_UTF8, 0, name, -1, NULL, 0, NULL, NULL);
    u_name = (LPSTR)HeapAlloc(GetProcessHeap(), 0, len);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, u_name, len, NULL, NULL);
    res = LoadImageA(hinst, u_name, type, desiredx, desiredy, loadflags);
    HeapFree(GetProcessHeap(), 0, u_name);
    return res;
}

HANDLE WINAPI LoadImageA(HINSTANCE hinst, LPCSTR name, UINT type, INT desiredx, INT desiredy, UINT loadflags)
{
    int depth;
    if (loadflags & LR_LOADFROMFILE)
    {
        loadflags &= ~LR_SHARED;
        /* relative paths are not only relative to the current working directory */
        // if (SearchPathW(NULL, name, NULL, ARRAY_SIZE(path), path, NULL)) name = path;
    }
    switch (type)
    {
    case IMAGE_BITMAP:
        return (HANDLE)BITMAP_Load(hinst, name, desiredx, desiredy, loadflags);

    case IMAGE_ICON:
    case IMAGE_CURSOR:
        depth = 1;
        if (!(loadflags & LR_MONOCHROME))
            depth = get_display_bpp();
        return (HANDLE)CURSORICON_LoadFromFile(name, desiredx, desiredy, depth, (type == IMAGE_CURSOR), loadflags);
    }
    return 0;
}

/******************************************************************************
 *		LoadBitmapW (USER32.@) Loads bitmap from the executable file
 *
 * RETURNS
 *    Success: Handle to specified bitmap
 *    Failure: NULL
 */
HBITMAP WINAPI LoadBitmapW(HINSTANCE instance, /* [in] Handle to application instance */
                           LPCWSTR name)       /* [in] Address of bitmap resource name */
{
    return (HBITMAP)LoadImageW(instance, name, IMAGE_BITMAP, 0, 0, 0);
}

/**********************************************************************
 *		LoadBitmapA (USER32.@)
 *
 * See LoadBitmapW.
 */
HBITMAP WINAPI LoadBitmapA(HINSTANCE instance, LPCSTR name)
{
    return (HBITMAP)LoadImageA(instance, name, IMAGE_BITMAP, 0, 0, 0);
}

#endif