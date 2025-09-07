#include "cursormgr.h"
#include "cursors.hpp"
#include <assert.h>
#include "cursorid.h"
static CursorMgr s_cursorMgr;

struct CData
{
    const unsigned char *buf;
    UINT length;
};

static bool getStdCursorCData(WORD wId, CData &data)
{
    bool ret = true;
    switch (wId)
    {
    case CIDC_MOVE:
        data.buf = drag_move_cur;
        data.length = sizeof(drag_move_cur);
        break;
    case CIDC_COPY:
        data.buf = drag_copy_cur;
        data.length = sizeof(drag_copy_cur);
        break;
    case CIDC_LINK:
        data.buf = drag_link_cur;
        data.length = sizeof(drag_link_cur);
        break;
    case CIDC_NODROP:
        data.buf = drag_nodrop_cur;
        data.length = sizeof(drag_nodrop_cur);
        break;
    case CIDC_ARROW:
        data.buf = ocr_normal_cur;
        data.length = sizeof(ocr_normal_cur);
        break;
        ;
    case CIDC_IBEAM:
        data.buf = ocr_ibeam_cur;
        data.length = sizeof(ocr_ibeam_cur);
        break;
        ;
    case CIDC_WAIT:
        data.buf = ocr_wait_cur;
        data.length = sizeof(ocr_wait_cur);
        break;
    case CIDC_CROSS:
        data.buf = ocr_cross_cur;
        data.length = sizeof(ocr_cross_cur);
        break;
    case CIDC_UPARROW:
        data.buf = ocr_up_cur;
        data.length = sizeof(ocr_up_cur);
        break;
    case CIDC_SIZE:
        data.buf = ocr_size_cur;
        data.length = sizeof(ocr_size_cur);
        break;
    case CIDC_SIZEALL:
        data.buf = ocr_sizeall_cur;
        data.length = sizeof(ocr_sizeall_cur);
        break;
    case CIDC_ICON:
        data.buf = ocr_icon_cur;
        data.length = sizeof(ocr_icon_cur);
        break;
    case CIDC_SIZENWSE:
        data.buf = ocr_sizenwse_cur;
        data.length = sizeof(ocr_sizenwse_cur);
        break;
    case CIDC_SIZENESW:
        data.buf = ocr_sizenesw_cur;
        data.length = sizeof(ocr_sizenesw_cur);
        break;
    case CIDC_SIZEWE:
        data.buf = ocr_sizewe_cur;
        data.length = sizeof(ocr_sizewe_cur);
        break;
    case CIDC_SIZENS:
        data.buf = ocr_sizens_cur;
        data.length = sizeof(ocr_sizens_cur);
        break;
    case CIDC_HAND:
        data.buf = ocr_hand_cur;
        data.length = sizeof(ocr_hand_cur);
        break;
    case CIDC_HELP:
        data.buf = ocr_help_cur;
        data.length = sizeof(ocr_help_cur);
        break;
    case CIDC_APPSTARTING:
        data.buf = ocr_appstarting_cur;
        data.length = sizeof(ocr_appstarting_cur);
        break;
    default:
        ret = false;
        break;
    }
    return ret;
}

CursorMgr::CursorMgr()
{
}

CursorMgr::~CursorMgr()
{
    for (auto it : m_stdCursor)
    {
        DestroyIcon((HICON)it.second);
    }
    m_stdCursor.clear();
}

HCURSOR CursorMgr::loadCursor(LPCSTR lpCursorName)
{
    return s_cursorMgr._LoadCursor(lpCursorName);
}

void SetCursorID(HICON hIcon, WORD cursorId);
HCURSOR CursorMgr::_LoadCursor(LPCSTR lpCursorName)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    HCURSOR ret = 0;
    if (IS_INTRESOURCE(lpCursorName))
    {
        WORD wId = (WORD)(ULONG_PTR)lpCursorName;
        if (m_stdCursor.find(wId) != m_stdCursor.end())
            return m_stdCursor[wId];
        CData data;
        if (!getStdCursorCData(wId, data))
        {
            getStdCursorCData(CIDC_ARROW, data);
        }
        ret = (HCURSOR)LoadImageBuf((PBYTE)data.buf, data.length, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
        assert(ret);
        SetCursorID((HICON)ret, wId);
        m_stdCursor.insert(std::make_pair(wId, ret));
    }
    else
    {
        ret = (HCURSOR)LoadImageA(0, lpCursorName, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
    }
    return ret;
}

BOOL CursorMgr::destroyCursor(HCURSOR cursor)
{
    return s_cursorMgr._DestroyCursor(cursor);
}

BOOL CursorMgr::_DestroyCursor(HCURSOR cursor)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // look for sys cursor
    for (auto it : m_stdCursor)
    {
        if (cursor == it.second)
            return FALSE;
    }
    DestroyIcon((HICON)cursor);
    return TRUE;
}