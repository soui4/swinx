#ifndef _TRAYICON_MGR_H_
#define _TRAYICON_MGR_H_
#include <shellapi.h>
#include <list>
#include <mutex>
#include <xcb/xproto.h>


class SConnection;
class STrayIconMgr {

	typedef std::list<PNOTIFYICONDATAA> TRAYLIST;
	SConnection* m_pConn;
public:
	STrayIconMgr(SConnection* pConn) :m_pConn(pConn) {}
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
private:
	std::recursive_mutex m_mutex;

	TRAYLIST m_lstTrays;
};

#endif//_TRAYICON_MGR_H_