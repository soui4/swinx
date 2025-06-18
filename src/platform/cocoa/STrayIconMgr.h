#ifndef _TRAYICON_MGR_H_
#define _TRAYICON_MGR_H_
#include <shellapi.h>
#include <list>
#include <mutex>



struct TrayIconData : NOTIFYICONDATAA
{
	void * hTrayProxy;
};

class STrayIconMgr {

	typedef std::list<TrayIconData*> TRAYLIST;
public:
    STrayIconMgr();
	~STrayIconMgr();

public:
	BOOL NotifyIcon(DWORD  dwMessage, PNOTIFYICONDATAA lpData) ;

private:
	BOOL AddIcon(PNOTIFYICONDATAA lpData);

	BOOL ModifyIcon(PNOTIFYICONDATAA lpData);

	BOOL DelIcon(PNOTIFYICONDATAA lpData);

	TRAYLIST::iterator findIcon(PNOTIFYICONDATAA src);
  private:
	std::recursive_mutex m_mutex;

	TRAYLIST m_lstTrays;
};

#endif//_TRAYICON_MGR_H_