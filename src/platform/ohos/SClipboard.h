#ifndef _OHOS_SCLIPBOARD_H_
#define _OHOS_SCLIPBOARD_H_

#include <mutex>
#include <string>
#include <ctypes.h>
#include <objidl.h>

class SOhosClipboardDataObject;

class SClipboard {
  public:
    SClipboard();
    ~SClipboard();

    static UINT RegisterClipboardFormatA(LPCSTR pszName);

    bool hasFormat(UINT fmtAtom);
    HANDLE getClipboardData(UINT fmtAtom);
    HANDLE setClipboardData(UINT fmtAtom, HANDLE hMem);
    BOOL openClipboard(HWND hWndNewOwner);
    BOOL closeClipboard();
    HWND getClipboardOwner() const;
    BOOL emptyClipboard();
    IDataObject *getDataObject();
    BOOL isCurrentClipboard(IDataObject *pDo);
    BOOL setDataObject(IDataObject *pDo);
    void flushClipboard();

  private:
    mutable std::recursive_mutex m_mutex;
    SOhosClipboardDataObject *m_clipDataObject;
    IDataObject *m_externalDataObject;
    HWND m_owner;
    BOOL m_open;
};

#endif // _OHOS_SCLIPBOARD_H_
