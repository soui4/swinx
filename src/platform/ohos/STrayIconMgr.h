#ifndef _OHOS_TRAYICON_MGR_H_
#define _OHOS_TRAYICON_MGR_H_

#include <shellapi.h>

class STrayIconMgr {
  public:
    STrayIconMgr() {}
    ~STrayIconMgr() {}

    BOOL NotifyIcon(DWORD, PNOTIFYICONDATAA)
    {
        return FALSE;
    }
};

#endif // _OHOS_TRAYICON_MGR_H_
