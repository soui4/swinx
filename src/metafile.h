#ifndef _SWINX_METAFILE_H_
#define _SWINX_METAFILE_H_
#include <windows.h>

/*
 * EMF（增强型元文件）录制钩子。
 *
 * CreateEnhMetaFile 返回的 DC 上「选中字体」这个动作需要被记录下来，
 * 供 CloseEnhMetaFile / EnumEnhMetaFile 回放。录制逻辑集中在
 * src/metafile.cpp，两个 GDI 后端只需在选中字体的分支里
 * 调用 SwinxMetaFileRecordFont —— 非录制 DC 上它是空操作。
 */
void SwinxMetaFileRecordFont(HDC hdc, HGDIOBJ hfont);

#endif //_SWINX_METAFILE_H_
