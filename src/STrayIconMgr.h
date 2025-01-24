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
  public:
    TrayWnd(NOTIFYICONDATAA *iconData)
        : m_iconData(iconData)
    {
    }
  protected:
    LRESULT NotifyOwner(UINT uMsg,WPARAM wp,LPARAM lp);
    void OnPaint(HDC hdc);

    BEGIN_MSG_MAP_EX(TrayWnd)
		MESSAGE_HANDLER_EX(WM_LBUTTONDOWN,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_RBUTTONDOWN,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_MOUSEMOVE,NotifyOwner)
		MESSAGE_HANDLER_EX(WM_LBUTTONDBLCLK,NotifyOwner)
        MSG_WM_PAINT(OnPaint)
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