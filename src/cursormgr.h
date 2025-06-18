#ifndef _CURSOR_MGR_H_
#define _CURSOR_MGR_H_
#include <windows.h>
#include <map>
#include <mutex>

class CursorMgr{
public:
    CursorMgr();
    ~CursorMgr();

    static HCURSOR loadCursor(LPCSTR lpCursorName);

    static BOOL destroyCursor(HCURSOR cursor);

  private:
    HCURSOR _LoadCursor(LPCSTR lpCursorName);

    BOOL _DestroyCursor(HCURSOR cursor);

    std::map<WORD, HCURSOR> m_stdCursor;
    std::mutex  m_mutex;
};

#endif//_CURSOR_MGR_H_