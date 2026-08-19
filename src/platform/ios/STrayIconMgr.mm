// iOS 平台系统托盘管理：iOS 无状态栏图标概念，此处为桩实现。
// 仅维护图标数据列表以保证 Add/Modify/Del 语义一致，不创建任何 UI 元素。

#include "STrayIconMgr.h"
#include <stdlib.h>
#include <string.h>
#define kLogTag "traywnd"

STrayIconMgr::STrayIconMgr()
{
}

STrayIconMgr::~STrayIconMgr()
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        free(*it);
    }
    m_lstTrays.clear();
}

BOOL STrayIconMgr::AddIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    if (findIcon(lpData) != m_lstTrays.end())
        return FALSE;
    TrayIconData *icon = (TrayIconData *)malloc(sizeof(TrayIconData));
    memcpy(icon, lpData, sizeof(NOTIFYICONDATAA));
    icon->hTrayProxy = nullptr;
    m_lstTrays.push_back(icon);
    return TRUE;
}

BOOL STrayIconMgr::ModifyIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;
    memcpy(*it, lpData, sizeof(NOTIFYICONDATAA));
    (*it)->hTrayProxy = nullptr;
    return TRUE;
}

BOOL STrayIconMgr::DelIcon(PNOTIFYICONDATAA lpData)
{
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = findIcon(lpData);
    if (it == m_lstTrays.end())
        return FALSE;
    free(*it);
    m_lstTrays.erase(it);
    return TRUE;
}

STrayIconMgr::TRAYLIST::iterator STrayIconMgr::findIcon(PNOTIFYICONDATAA src)
{
    for (auto it = m_lstTrays.begin(); it != m_lstTrays.end(); it++)
    {
        if ((*it)->hWnd == src->hWnd && (*it)->uID == src->uID)
            return it;
    }
    return m_lstTrays.end();
}

BOOL STrayIconMgr::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData)
{
    switch (dwMessage)
    {
    case NIM_ADD:
        return AddIcon(lpData);
    case NIM_DELETE:
        return DelIcon(lpData);
    case NIM_MODIFY:
        return ModifyIcon(lpData);
    }
    return FALSE;
}
