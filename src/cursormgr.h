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

    /* 是否为 LoadCursor 缓存的标准（共享）光标。Windows 语义：LoadCursor 返回的
     * 系统光标归系统所有，DestroyIcon / DestroyCursor 对它必须失败（返回 FALSE），
     * 调用方（如 WebKit 的 SharedCursor）无条件销毁也不会破坏其他使用者。 */
    static BOOL isStdCursor(HCURSOR cursor);

  private:
    HCURSOR _LoadCursor(LPCSTR lpCursorName);

    BOOL _DestroyCursor(HCURSOR cursor);

    BOOL _IsStdCursor(HCURSOR cursor);

    std::map<WORD, HCURSOR> m_stdCursor;
    std::mutex  m_mutex;
};

#endif//_CURSOR_MGR_H_