#ifndef _TRAYICON_MGR_H_
#define _TRAYICON_MGR_H_
#include <shellapi.h>
#include <list>
#include <mutex>
#include <xcb/xproto.h>
#include "nativewnd.h"

class SConnection;

class TrayWnd : public CNativeWnd {
    NOTIFYICONDATAA *m_iconData;
    /* 图标位图的原始像素尺寸缓存：面板给的槽位不一定是正方形，绘制时要按原始
     * 宽高比缩放（见 TrayWnd::OnPaint）。GetIconInfo 每次都会拷贝一份位图，
     * 尺寸只在 NIM_MODIFY 换图标时才变，所以按 HICON 缓存一次即可。 */
    HICON m_hIconSized = nullptr;
    int m_cxIconSized = 0;
    int m_cyIconSized = 0;
  public:
    TrayWnd(NOTIFYICONDATAA *iconData)
        : m_iconData(iconData)
    {
    }
    /* NIM_MODIFY 换了图标后调用：HICON 句柄地址可能被复用，缓存必须作废 */
    void ResetIconSizeCache() { m_hIconSized = nullptr; }
  protected:
    LRESULT NotifyOwner(UINT uMsg,WPARAM wp,LPARAM lp);
    void OnPaint(HDC hdc);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam);
    /* 取 hIcon 的原始像素尺寸（失败时返回 false） */
    bool IconPixelSize(HICON hIcon, int &cx, int &cy);

    BEGIN_MSG_MAP_EX(TrayWnd)
		MESSAGE_HANDLER_EX(WM_LBUTTONDOWN,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_RBUTTONDOWN,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_MBUTTONDOWN,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_LBUTTONUP,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_RBUTTONUP,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_MBUTTONUP,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_MOUSEMOVE,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_LBUTTONDBLCLK,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_RBUTTONDBLCLK,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_MBUTTONDBLCLK,NotifyOwner)
        MSG_WM_PAINT(OnPaint)
        MESSAGE_HANDLER_EX(WM_SIZE, OnSize)
    END_MSG_MAP()
};

struct TrayIconData : NOTIFYICONDATAA
{
    TrayWnd * hTray;
};

class STrayIconMgr {

	typedef std::list<TrayIconData*> TRAYLIST;
	SConnection* m_pConn;
public:
    STrayIconMgr(SConnection *pConn);
	~STrayIconMgr();

public:
	BOOL NotifyIcon(DWORD  dwMessage, PNOTIFYICONDATAA lpData) {
		switch (dwMessage) {
		case NIM_ADD:return AddIcon(lpData);
		case NIM_DELETE:return DelIcon(lpData);
		case NIM_MODIFY:return ModifyIcon(lpData);
		}
		return FALSE;
	}

private:
	BOOL AddIcon(PNOTIFYICONDATAA lpData);

	BOOL ModifyIcon(PNOTIFYICONDATAA lpData);

	BOOL DelIcon(PNOTIFYICONDATAA lpData);

	TRAYLIST::iterator findIcon(PNOTIFYICONDATAA src);

	static xcb_window_t locateTrayWindow(xcb_connection_t* connection, xcb_atom_t selection);

	bool visualHasAlphaChannel();

  private:
	std::recursive_mutex m_mutex;

	TRAYLIST m_lstTrays;
    xcb_window_t m_trayMgr;
};

#endif//_TRAYICON_MGR_H_