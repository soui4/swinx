#ifndef _SDC_H_
#define _SDC_H_
#include <windows.h>
#if defined(__APPLE__)
#undef interface
typedef struct CGContext *CGContextRef;
struct CGAffineTransform;
typedef const struct CGPath *CGPathRef;
#else
#include <cairo.h>
#endif

#include <region.h>
#include <vector>
#include <memory>

struct DCState;
typedef struct _SDC
{
    HWND hwnd;
#if defined(__APPLE__)
    // APPLE (macOS/iOS) 平台：使用 CoreGraphics 的 CGContext 实现 GDI 方法，替代 cairo。
    // 成员均为指针类型，配合前置声明，避免在头文件中包含 <CoreGraphics/CoreGraphics.h>
    CGContextRef cgCtx;        // 若指向窗口系统传入的 drawRect 上下文则为非拥有引用；
                                    // 若为内存 DC（SelectObject(hBmp)）创建的 CGBitmapContext 则为拥有引用，需 CGContextRelease。
    BOOL cgCtxOwned;                // TRUE 表示需要 CGContextRelease(cgCtx)
    CGAffineTransform *worldMtx;  // 使用指针避免需要完整类型定义
    CGPathRef recordedPath;   // BeginPath/EndPath/PathToRegion 等路径记录
    FLOAT miterLimit;               // SetMiterLimit/GetMiterLimit. CGContext doesn't persist across re-creation; keep in HDC.
#else
    cairo_t *cairo;
    cairo_matrix_t mtx;
#endif
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
    int polyFillMode;
    POINT brushOrg;      // SetBrushOrgEx/GetBrushOrgEx: pattern brush origin (device coords)
#ifdef __OHOS__
    BOOL hasDirty;
    RECT dirtyRect;
#endif

    // Path support
    BOOL pathRecording;     // TRUE if path recording is active
#if !defined(__APPLE__)
    cairo_path_t *currentPath;  // Current path being recorded (non-APPLE keeps cairo_path_t)
#endif

    // State stack for SaveDC/RestoreDC
    std::vector<std::shared_ptr<DCState>> stateStack;

    _SDC(HWND _hwnd);
    ~_SDC();
    
    int SaveState();
    BOOL RestoreState(int nState);
} * HDC;

#endif //_SDC_H_
