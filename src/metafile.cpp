#include <windows.h>
#include <vector>
#include <string.h>
#include "handle.h"
#include "sdc.h"
#include "metafile.h"

/*
 * EMF（增强型元文件）最小实现。
 *
 * swinx 只需要 WebKit 用到的那一个场景：把 HDC 变成「录制 DC」，在它上面
 * 选中一个字体，然后回放记录反查实际被选中的 LOGFONT
 * ——见 platform/graphics/win/FontCacheWin.cpp 的 getFontDataForCharacters()，
 * 它用这个办法询问 Uniscribe「你最终会用哪个字体画这个字符」。
 *
 * 因此这里只记录 EMR_EXTCREATEFONTINDIRECTW 一种记录，不实现磁盘 EMF 文件，
 * 也不提供 GetEnhMetaFile* / SetEnhMetaFile* 系列（swinx 用不到）。
 *
 * 录制缓冲挂在 DC 上（_SDC::recording），CloseEnhMetaFile 把它转交给
 * HENHMETAFILE 句柄；若 DC 先被 DeleteDC，则由 ~_SDC 释放。
 */

struct EnhMetaFile
{
    std::vector<EMREXTCREATEFONTINDIRECTW> records;
};

static void emf_free(void *ptr)
{
    delete (EnhMetaFile *)ptr;
}

/* swinx 内部把字体统一存成 LOGFONTA（面名按 UTF-8 存，见 CreateFontIndirectW），
 * 而 EMF 记录按 Win32 的 EXTLOGFONTW 布局，需要逐字段转换。 */
static void logFontA2W(const LOGFONTA *src, LOGFONTW *dst)
{
    memset(dst, 0, sizeof(LOGFONTW));
    dst->lfHeight = src->lfHeight;
    dst->lfWidth = src->lfWidth;
    dst->lfEscapement = src->lfEscapement;
    dst->lfOrientation = src->lfOrientation;
    dst->lfWeight = src->lfWeight;
    dst->lfItalic = src->lfItalic;
    dst->lfUnderline = src->lfUnderline;
    dst->lfStrikeOut = src->lfStrikeOut;
    dst->lfCharSet = src->lfCharSet;
    dst->lfOutPrecision = src->lfOutPrecision;
    dst->lfClipPrecision = src->lfClipPrecision;
    dst->lfQuality = src->lfQuality;
    dst->lfPitchAndFamily = src->lfPitchAndFamily;
    MultiByteToWideChar(CP_UTF8, 0, src->lfFaceName, -1, dst->lfFaceName, LF_FACESIZE);
}

void SwinxMetaFileRecordFont(HDC hdc, HGDIOBJ hfont)
{
    if (!hdc || !hdc->recording || !hfont)
        return;
    if (GetObjectType(hfont) != OBJ_FONT)
        return;
    const LOGFONTA *lf = (const LOGFONTA *)GetGdiObjPtr(hfont);
    if (!lf)
        return;
    EnhMetaFile *emf = (EnhMetaFile *)GetGdiObjPtr(hdc->recording);
    if (!emf)
        return;

    EMREXTCREATEFONTINDIRECTW rec;
    memset(&rec, 0, sizeof(rec));
    rec.emr.iType = EMR_EXTCREATEFONTINDIRECTW;
    rec.emr.nSize = sizeof(rec);
    logFontA2W(lf, &rec.elfw.elfLogFont);
    // 面名同时写进 elfFullName（Win32 会填完整字体名，这里退化为面名）
    for (int i = 0; i < LF_FULLFACESIZE && rec.elfw.elfLogFont.lfFaceName[i]; i++)
        rec.elfw.elfFullName[i] = rec.elfw.elfLogFont.lfFaceName[i];
    emf->records.push_back(rec);
}

HDC CreateEnhMetaFileW(HDC hdcRef, LPCWSTR lpFilename, CONST RECT *lpRect, LPCWSTR lpDescription)
{
    // 不支持落盘 EMF：只提供内存录制
    if (lpFilename || lpDescription)
        return nullptr;
    (void)lpRect;
    HDC hdc = CreateCompatibleDC(hdcRef);
    if (!hdc)
        return nullptr;
    hdc->recording = (HENHMETAFILE)InitHandle(OBJ_ENHMETAFILE, new EnhMetaFile(), emf_free);
    if (!hdc->recording)
    {
        DeleteDC(hdc);
        return nullptr;
    }
    return hdc;
}

HDC CreateEnhMetaFileA(HDC hdcRef, LPCSTR lpFilename, CONST RECT *lpRect, LPCSTR lpDescription)
{
    if (lpFilename || lpDescription)
        return nullptr;
    return CreateEnhMetaFileW(hdcRef, nullptr, lpRect, nullptr);
}

HENHMETAFILE CloseEnhMetaFile(HDC hdc)
{
    if (!hdc || !hdc->recording)
        return nullptr;
    HENHMETAFILE hmf = hdc->recording;
    hdc->recording = nullptr; // 所有权转移给调用方
    DeleteDC(hdc);
    return hmf;
}

BOOL DeleteEnhMetaFile(HENHMETAFILE hmf)
{
    if (!hmf)
        return FALSE;
    return DeleteObject(hmf);
}

BOOL EnumEnhMetaFile(HDC hdc, HENHMETAFILE hmf, ENHMFENUMPROC lpProc, LPVOID lpParam, CONST RECT *lpRect)
{
    if (!hmf || !lpProc)
        return FALSE;
    (void)lpRect;
    EnhMetaFile *emf = (EnhMetaFile *)GetGdiObjPtr(hmf);
    if (!emf)
        return FALSE;
    HANDLETABLE table;
    memset(&table, 0, sizeof(table));
    for (size_t i = 0; i < emf->records.size(); i++)
    {
        // 记录首字段就是 EMR，可直接按 ENHMETARECORD 回放
        if (!lpProc(hdc, &table, (CONST ENHMETARECORD *)&emf->records[i], 1, (LPARAM)lpParam))
            return FALSE; // 回调返回 0：停止枚举（Win32 语义）
    }
    return TRUE;
}
