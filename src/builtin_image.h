#ifndef _BUILTIN_IMAGE_H_
#define _BUILTIN_IMAGE_H_

#include <windows.h>
#include <cairo.h>
class BuiltinImage {
public:
	enum {
		BI_SCROLLBAR=0,
		BI_MSGBOXICONS=1,
		BI_COUNT
	};

	enum SbPart {
		Sb_Line_Up=0,
		Sb_Line_Down,
		Sb_Thumb,
		Sb_Rail,
		Sb_Triangle=8,
	};
	enum ImgState{
		St_Normal=0,
		St_Hover,
		St_Push,
		St_Disable,
	};

	struct BiInfo {
		HBITMAP hBmp;
		int  states;
		UINT expendMode;
		RECT margin;
		BiInfo() :hBmp(nullptr) {}
		~BiInfo() {
			if(hBmp) DeleteObject(hBmp);
		}
	};

public:
	static BuiltinImage* instance();
	BOOL Init();

	BOOL drawBiState(HDC hdc, int imgId, ImgState st, const RECT *rcDst, BYTE byAlpha);
    BOOL drawBiIdx(HDC hdc, int imgId, int idx, const RECT *rcDst, BYTE byAlpha);
	BOOL drawScrollbarState(HDC hdc, int iPart, BOOL bVert, int st, const RECT* rcDst,BYTE byAlpha);
protected:
	BuiltinImage();
	~BuiltinImage();

	BiInfo* m_biInfo100;
	BiInfo* m_biInfo200;
};

#endif//_BUILTIN_IMAGE_H_
