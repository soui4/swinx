#ifndef GTKFILECHOOSER_H
#define GTKFILECHOOSER_H
#include <windows.h>
#include <commdlg.h>

BOOL SChooseColor(HWND parent,const COLORREF initClr[16],COLORREF *out);

BOOL SGetOpenFileNameA(LPOPENFILENAMEA p, DlgMode mode);

#endif // GTKFILECHOOSER_H
