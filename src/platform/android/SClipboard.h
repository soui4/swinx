#ifndef _ANDROID_SCLIPBOARD_H_
#define _ANDROID_SCLIPBOARD_H_

#include <windows.h>
#include <map>
#include <mutex>
#include <string>
#include <objidl.h>

class SAndroidClipboardDataObject;

class SClipboard {
public:
    SClipboard();
    ~SClipboard();

    BOOL emptyClipboard();
    BOOL hasFormat(UINT format);
    BOOL openClipboard(HWND hWndNewOwner);
    BOOL closeClipboard();
    HWND getClipboardOwner();
    HANDLE getClipboardData(UINT uFormat);
    HANDLE setClipboardData(UINT uFormat, HANDLE hMem);

    static UINT RegisterClipboardFormatA(LPCSTR pszName);

    IDataObject *getDataObject();
    BOOL isCurrentClipboard(IDataObject *pDo);
    BOOL setDataObject(IDataObject *pDo);
    void flushClipboard();

private:
    std::recursive_mutex m_mutex;
    HWND m_hOwner;
    bool m_bOpen;
    SAndroidClipboardDataObject *m_clipDataObject;
    IDataObject *m_externalDataObject;
};

#endif // _ANDROID_SCLIPBOARD_H_