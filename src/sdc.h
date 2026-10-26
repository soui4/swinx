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
struct HDC__
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
    HPEN pen;
    HBRUSH brush;
    HFONT hfont;
    UINT    uGetFlags;
    int bkMode;
    int nSave;
    UINT textAlign;
    int rop2;
    int polyFillMode;
    int graphicsMode;    // SetGraphicsMode/GetGraphicsMode: GM_COMPATIBLE / GM_ADVANCED
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

    // EMF 录制句柄：非 NULL 表示本 DC 由 CreateEnhMetaFile 创建，
    // SelectObject(OBJ_FONT) 会把一条 EMR_EXTCREATEFONTINDIRECTW 追加进去
    // （见 src/metafile.cpp）。CloseEnhMetaFile 取走该句柄后置回 NULL；
    // 若 DC 未关闭就被析构，则由 ~_SDC 负责释放。
    HENHMETAFILE recording;

    HDC__(HWND _hwnd);
    ~HDC__();
    
    int SaveState();
    BOOL RestoreState(int nState);
};

/* 既有实现代码里的名称，保持不变。 */
typedef HDC__ _SDC;

#endif //_SDC_H_
