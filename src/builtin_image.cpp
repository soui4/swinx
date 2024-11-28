#include "builtin_image.h"
#include <assert.h>
#include "image.hpp"

BuiltinImage* BuiltinImage::instance()
{
	static BuiltinImage inst;
	return &inst;
}

struct BiConfig {
	const BYTE* buf;
	int bufLen;
	int states;
	UINT expandMode;
	RECT margin;
};

struct MemBlock{
	const BYTE * buf;
	int bufLen;
	int pos;
};

static cairo_status_t read_png_from_memory(void *closure, unsigned char *data, unsigned int length) {
	MemBlock *memBlock = (MemBlock*)closure;
    if (length > memBlock->bufLen-memBlock->pos) {
		return CAIRO_STATUS_READ_ERROR;
    }
    memcpy(data, memBlock->buf + memBlock->pos, length);
	memBlock->pos += length;
    return CAIRO_STATUS_SUCCESS;
}

BOOL BuiltinImage::Init()
{
	m_biInfo100 = new BiInfo[BI_COUNT];
	m_biInfo200 = new BiInfo[BI_COUNT];
	BiConfig cfg100[] = {
		{sys_scrollbar_png,sizeof(sys_scrollbar_png),1,MAKELONG(EXPEND_MODE_TILE,FILTER_NONE),{4,4,4,4}},
        { sys_msgbox_icons_png, sizeof(sys_msgbox_icons_png), 5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0,0,0,0 } },
	};
	for (int i = 0; i < BI_COUNT; i++) {
		MemBlock memBlock={cfg100[i].buf,cfg100[i].bufLen,0};
		cairo_surface_t* surf = cairo_image_surface_create_from_png_stream(read_png_from_memory,&memBlock);
		assert(surf);
		m_biInfo100[i].hBmp = InitGdiObj(OBJ_BITMAP,surf);
		m_biInfo100[i].states = cfg100[i].states;
		m_biInfo100[i].expendMode=cfg100[i].expandMode;
		m_biInfo100[i].margin = cfg100[i].margin;
	}

	BiConfig cfg200[] = {
		{sys_scrollbar_200_png,sizeof(sys_scrollbar_200_png),1,MAKELONG(EXPEND_MODE_TILE,FILTER_NONE),{8,8,8,8}},
        { sys_msgbox_icons200_png, sizeof(sys_msgbox_icons200_png), 5, MAKELONG(EXPEND_MODE_TILE, FILTER_NONE), { 0, 0, 0, 0 } },
	};
	for (int i = 0; i < BI_COUNT; i++) {
		MemBlock memBlock={cfg200[i].buf,cfg200[i].bufLen,0};
		cairo_surface_t* surf = cairo_image_surface_create_from_png_stream(read_png_from_memory,&memBlock);
		assert(surf);
		m_biInfo200[i].hBmp = InitGdiObj(OBJ_BITMAP,surf);
		m_biInfo200[i].states = cfg200[i].states;
		m_biInfo200[i].expendMode=cfg200[i].expandMode;
		m_biInfo200[i].margin = cfg200[i].margin;
	}
	return 0;
}

BOOL BuiltinImage::drawBiState(HDC hdc, int imgId, ImgState st, const RECT* rcDst,BYTE byAlpha)
{
	if(imgId>=BI_COUNT)
		return FALSE;
	int scale = GetSystemScale();

	const BiInfo * biInfo = (scale<=100? m_biInfo100: m_biInfo200) +imgId;
	BITMAP bm;
	GetObject(biInfo->hBmp,sizeof(bm),&bm);
	int wid = bm.bmWidth/4;
	int hei = bm.bmHeight;
	RECT rcSrc={0,0,wid,hei};
	OffsetRect(&rcSrc,st*wid,0);
	DrawBitmap9Patch(hdc,rcDst,biInfo->hBmp,&rcSrc,&biInfo->margin,biInfo->expendMode,byAlpha);
	return TRUE;
}

BOOL BuiltinImage::drawBiIdx(HDC hdc, int imgId, int idx, const RECT *rcDst, BYTE byAlpha)
{
    if (imgId >= BI_COUNT)
        return FALSE;
    int scale = GetSystemScale();

    const BiInfo *biInfo = (scale <= 100 ? m_biInfo100 : m_biInfo200) + imgId;
    BITMAP bm;
    GetObject(biInfo->hBmp, sizeof(bm), &bm);
    int wid = bm.bmWidth / biInfo->states;
    int hei = bm.bmHeight;
    RECT rcSrc = { 0, 0, wid, hei };
    OffsetRect(&rcSrc, idx * wid, 0);
    DrawBitmap9Patch(hdc, rcDst, biInfo->hBmp, &rcSrc, &biInfo->margin, biInfo->expendMode, byAlpha);
    return TRUE;
}

BOOL BuiltinImage::drawScrollbarState(HDC hdc, int iPart, BOOL bVert, int st, const RECT *rcDst,BYTE byAlpha)
{
	//src format, see resource/image/sys_scrollbar.png
	BITMAP bm;
	int scale = GetSystemScale();
	const BiInfo * biInfo = (scale<=100? m_biInfo100: m_biInfo200) +BI_SCROLLBAR;
	GetObject(biInfo->hBmp,sizeof(bm),&bm);
	int wid = bm.bmWidth/9;
	int hei = bm.bmHeight/4;
	RECT rcSrc={0,0,wid,hei};
	if(iPart<Sb_Triangle){
		int iCol = iPart;
		if(!bVert) iCol += 4;
		OffsetRect(&rcSrc,wid*iCol,hei*st);
	}else{
		OffsetRect(&rcSrc,wid*8,hei*(iPart-Sb_Triangle));
	}
	DrawBitmap9Patch(hdc,rcDst,biInfo->hBmp,&rcSrc,&biInfo->margin,biInfo->expendMode,byAlpha);
	if(iPart == Sb_Thumb){
		RECT rcSrc={0,0,wid,hei};
		RECT rcTmp = *rcDst;
		if(bVert){
			OffsetRect(&rcSrc,wid*8,hei*1);
			InflateRect(&rcTmp,0,-(rcTmp.bottom-rcTmp.top - hei)/2);
		}else{
			OffsetRect(&rcSrc,wid*8,hei*2);
			InflateRect(&rcTmp,-(rcTmp.right-rcTmp.left - wid)/2,0);
		}
		if(rcTmp.right-rcTmp.left <= rcDst->right- rcDst->left &&
			rcTmp.bottom - rcTmp.top <= rcDst->bottom - rcDst->top)
			DrawBitmapEx(hdc,&rcTmp,biInfo->hBmp,&rcSrc,EXPEND_MODE_NONE,byAlpha);
	}
    return TRUE;
}

BuiltinImage::BuiltinImage():m_biInfo100(nullptr),m_biInfo200(nullptr)
{
	Init();
}

BuiltinImage::~BuiltinImage()
{
	if (m_biInfo100) delete[]m_biInfo100;
	if(m_biInfo200) delete []m_biInfo200;
}
