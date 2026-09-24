/*
 * SClipboard.h -- in-memory clipboard for the FreeRTOS port.  Bare metal has
 * no OS clipboard service, so formats live in a per-connection map; only the
 * swinx process can read/write it.  Interface mirrors SConnection's clipboard
 * methods (which forward here), same as the mobile layout.
 */
#ifndef _SWINX_FREERTOS_SCLIPBOARD_H_
#define _SWINX_FREERTOS_SCLIPBOARD_H_

#include <windows.h>
#include <map>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

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

private:
    std::recursive_mutex m_mutex;
    HWND m_hOwner;
    std::atomic<bool> m_bOpen;
    // registered (non-standard) format name table
    static std::recursive_mutex s_fmtMutex;
    static swinx_stl::map<swinx_stl::string, UINT> s_fmtNames;
    static UINT s_nextFmt;
};

#endif // _SWINX_FREERTOS_SCLIPBOARD_H_
