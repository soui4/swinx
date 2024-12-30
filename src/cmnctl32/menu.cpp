﻿#include "../nativewnd.h"
#include <menu.h>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <list>
#include "../tostring.hpp"
#include "builtin_classname.h"

#define DEFAULT_ITEM_HEIGHT    30
#define DEFAULT_MIN_ITEM_WIDTH 100
#define TIMERID_POPSUBMENU     100
#define TIMESPAN_PUPUPSUBMENU  500

#define WIDTH_MENU_INIT 10000
#define WIDTH_MENU_MAX  2000
#define WIDTH_MENU_MIN  100.f

#define Y_MIDFLAG  L"-1000px"
#define Y_IMIDFLAG (-1000)

class CMenu;

class SMenuItem {

    friend class CMenu;

  public:
    SMenuItem(CMenu *pOwnerMenu);

    ~SMenuItem();

    CMenu *GetSubMenu();

    CMenu *GetOwnerMenu();

    WCHAR GetHotKey() const;

    void SetText(LPCSTR pszTitle);

    /*void EnableItem(BOOL bEnable)
    {
        (m_itemState & 0b11);
    }*/
    // 禁用有两种情况，变灰+不变灰
    bool IsEnable() const;

    void SetCheck(BOOL bCheck);

    void SetHotItem(bool bHot);

    UINT GetDrawState() const;

    SIZE GetItemSize(HDC hdc);

    bool IsOwnerDraw() const;

    bool IsSeparator() const;

    bool IsPopup() const;

    void SetPopup(bool bPopup);

  protected:
    void OnSubMenuHided(BOOL bUncheckItem);

    void SetMenuFlag(UINT uFlag);

    void ModifyMenuFlag(UINT uFlag);

  protected:
    bool m_bIsHotItem = false;

    UINT m_uMenuFlag;

    UINT m_id;

    SIZE m_size = { DEFAULT_MIN_ITEM_WIDTH, DEFAULT_ITEM_HEIGHT };

    std::shared_ptr<CMenu> m_pSubMenu;

    CMenu *m_pOwnerMenu = nullptr;

    WCHAR m_cHotKey;
    LPCSTR m_data = nullptr;
    std::string m_strTitle;
};

class CMenu : public CNativeWnd {
    friend class SMenuItem;
    friend class SMenuRunData;
    typedef CNativeWnd __baseCls;

    DWORD m_dwContextHelpId;

    std::vector<SMenuItem> m_lsMenuItem;

    int m_iSelItem = -1;
    int m_iHoverItem = -1;

  protected:
    int GetNextMenuItem(int iItem, BOOL bForword);

    void DrawItemLoop(HDC memdc, RECT clentRc);

    SIZE CalcMenuSize();

    void InvalidateItem(int iItem);

    BOOL GetItemRect(int iItem, RECT *prc) const;

    int Pos2Item(POINT pt) const;

    SMenuItem *GetMenuItem(int iItem);

    BOOL HoverItem(int iItem);

    BOOL SelectItem(int iItem);

  public:
    CMenu(SMenuItem *pParent = nullptr);

    virtual ~CMenu(void) noexcept override;

  public:
    BOOL CreateMenu();

    UINT GetMenuItemID(int nPos);

    int GetMenuItemCount();

    BOOL InsertMenu(UINT uPosition, UINT uFlags, UINT_PTR nIDNewItem, LPCSTR strText);

    BOOL AppendMenu(UINT uFlags, int uIDNewItem, LPCSTR lpNewItem);

    BOOL CheckMenuRadioItem(UINT idFirst, UINT idLast, UINT idCheck, UINT uFlags);

    BOOL CheckMenuItem(UINT uIdCheckItem, UINT uCheck);

    BOOL DeleteMenu(UINT uPosition, UINT uFlags);

    UINT TrackPopupMenu(UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS prcRect);

    void DestroyMenu();

    BOOL RemoveMenu(UINT uPosition, UINT uFlags);

    BOOL ModifyMenu(UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem);

    BOOL ModifyMenuString(UINT uPosition, UINT uFlags, LPCSTR lpItemString);

    DWORD GetContextHelpId() const;

    BOOL SetContextHelpId(DWORD dwId);

    BOOL InsertMenuItem(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi);

    BOOL SetMenuItemInfo(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi);

    BOOL GetMenuItemInfo(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi);

    BOOL EnableMenuItem(UINT uIDEnableItem, UINT uEnable);

    BOOL GetMenuInfo(LPMENUINFO lpMenuInfo);

    BOOL SetMenuInfo(LPCMENUINFO lpMenuInfo);

  public:
    static void EndMenu(int nCmdId = 0);

    SMenuItem *GetParentItem();

    CMenu *GetSubMenu(int nID, BOOL byCmdId);

    CMenu *GetSubMenu(int nPos);

    SMenuItem *GetMenuItem(int nID, BOOL byCmdId);

  protected:
    SMenuItem *FindItem(UINT id);

    SMenuItem *FindItem(UINT uPos, UINT uFlag);

    int OnMouseActivate(HWND wndTopLevel, UINT nHitTest, UINT message);

    void OnTimer(UINT_PTR timeID);

    void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

    void OnPaint(HDC hdc);

    void OnLButtonUp(UINT nFlags, POINT point);

    void OnMouseMove(UINT nFlags, POINT point);

    void OnMouseLeave();

    BEGIN_MSG_MAP_EX(CMenu)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_MOUSELEAVE(OnMouseLeave)
        MSG_WM_LBUTTONUP(OnLButtonUp)
        MSG_WM_TIMER(OnTimer)
        CHAIN_MSG_MAP(CNativeWnd)
    END_MSG_MAP()

  protected:
    void ShowMenu(UINT uFlag, int x, int y);

    void HideMenu(BOOL bUncheckParentItem);

    void HideSubMenu();

    void RunMenu(HWND hOwner);

    void PopupSubMenu(int iItem, BOOL bCheckFirstItem);

    void OnSubMenuHided(BOOL bUncheckItem);

    void SendInitPopupMenu2Owner(int idx);

    SMenuItem *m_pParent;
    BOOL m_bMenuInitialized;

    ULONG_PTR m_dwMenuData;
};


//--------------------------------------------------------------------------------
template <typename type>
inline bool _IsGoodcbSizePtr(type *var)
{
    return (var != nullptr) && (var->cbSize == sizeof(type));
}

//////////////////////////////////////////////////////////////////////////
class SMenuRunData {
    friend class CMenu;

  public:
    SMenuRunData(HWND hOwner)
        : m_hOwner(hOwner)
        , m_bExit(FALSE)
        , m_nCmdID(-1)
    {
    }

    BOOL IsMenuWnd(HWND hWnd)
    {
        for (auto ite : m_lstMenuEx)
        {
            if (ite->m_hWnd == hWnd)
                return TRUE;
        }
        return FALSE;
    }

    void PushMenuEx(CMenu *pMenu)
    {
        m_lstMenuEx.push_back(pMenu);
    }

    CMenu *GetMenuEx()
    {
        if (m_lstMenuEx.empty())
            return 0;
        return m_lstMenuEx.back();
    }

    CMenu *PopMenuEx()
    {
        CMenu *pMenuEx = m_lstMenuEx.back();
        m_lstMenuEx.pop_back();
        return pMenuEx;
    }

    CMenu *SMenuExFromHwnd(HWND hWnd)
    {
        for (auto ite : m_lstMenuEx)
        {
            if (ite->m_hWnd == hWnd)
                return ite;
        }
        return NULL;
    }

    BOOL IsMenuExited()
    {
        return m_bExit;
    }

    void ExitMenu(int nCmdID)
    {
        for (auto ite : m_lstMenuEx)
        {
            ::ShowWindow(ite->m_hWnd, SW_HIDE);
        }
        m_lstMenuEx.clear();
        m_bExit = TRUE;
        m_nCmdID = nCmdID;
    }

    int GetCmdID()
    {
        return m_nCmdID;
    }

    HWND GetOwner()
    {
        return m_hOwner;
    }

  protected:
    std::list<CMenu *> m_lstMenuEx;
    LPTPMPARAMS m_prcRect = nullptr;
    BOOL m_bExit;
    int m_nCmdID;
    HWND m_hOwner;
};

static SMenuRunData *s_MenuData = nullptr;

//////////////////////////////////////////////////////////////////////////
// SMenuItem

SMenuItem::~SMenuItem()
{
    /*if (m_pSubMenu)
    {
        delete m_pSubMenu;
        m_pSubMenu = nullptr;
    }*/
}

SMenuItem::SMenuItem(CMenu *pOwnerMenu)
    : m_pOwnerMenu(pOwnerMenu)
    /*, m_iIcon(-1)
    , m_bCheck(FALSE)
    , m_bRadio(FALSE)*/
    , m_cHotKey(0)
{
}

SIZE SMenuItem::GetItemSize(HDC hdc)
{
    if (IsOwnerDraw())
    {
        if (s_MenuData)
        {
            if (::IsWindow(s_MenuData->GetOwner()))
            {
                MEASUREITEMSTRUCT measureItemStruct{ ODT_MENU, m_id, m_id, 0, 0, (ULONG_PTR)m_data };

                ::SendMessage(s_MenuData->GetOwner(), WM_MEASUREITEM, m_id, (LPARAM)&measureItemStruct);
                m_size.cx = measureItemStruct.itemWidth;
                m_size.cy = measureItemStruct.itemHeight;
            }
        }
    }
    else if (IsSeparator()) // 不是自绘需要更新分割线尺寸
    {
        m_size.cx = DEFAULT_MIN_ITEM_WIDTH;
        m_size.cy = 1;
    }
    else
    {
        SIZE szSize;
        ::GetTextExtentPoint32(hdc, m_strTitle.c_str(), m_strTitle.size(), &szSize);
        m_size.cx = szSize.cx + 46;
    }
    return m_size;
}

bool SMenuItem::IsOwnerDraw() const
{
    return m_uMenuFlag & MF_OWNERDRAW;
}

bool SMenuItem::IsSeparator() const
{
    return m_uMenuFlag & MF_SEPARATOR;
}

bool SMenuItem::IsPopup() const
{
    return m_uMenuFlag & MF_POPUP;
}

void SMenuItem::SetPopup(bool bPopup)
{
    m_uMenuFlag = bPopup ? (m_uMenuFlag | MF_POPUP) : (m_uMenuFlag & (~MF_POPUP));
}

void SMenuItem::OnSubMenuHided(BOOL bUncheckItem)
{
    m_pOwnerMenu->OnSubMenuHided(bUncheckItem);
}

void SMenuItem::SetMenuFlag(UINT uFlag)
{
    m_uMenuFlag = uFlag & (~MF_BYPOSITION);
}

void SMenuItem::ModifyMenuFlag(UINT uFlag)
{
    m_uMenuFlag |= uFlag;
}

WCHAR SMenuItem::GetHotKey() const
{
    return m_cHotKey;
}

void SMenuItem::SetText(LPCSTR pszTitle)
{
    if (pszTitle != nullptr)
        m_strTitle = pszTitle;
}

/*void EnableItem(BOOL bEnable)
    {
        (m_itemState & 0b11);
    }*/
// 禁用有两种情况，变灰+不变灰

bool SMenuItem::IsEnable() const
{
    // 低两位
    return (m_uMenuFlag & 0b11) == MF_ENABLED;
}

void SMenuItem::SetCheck(BOOL bCheck)
{
    m_uMenuFlag = bCheck ? (m_uMenuFlag | MF_MOUSESELECT) : (m_uMenuFlag & (~MF_MOUSESELECT));
}

void SMenuItem::SetHotItem(bool bHot)
{
    m_uMenuFlag = bHot ? (m_uMenuFlag | MF_HILITE) : (m_uMenuFlag & (~MF_HILITE));
}

UINT SMenuItem::GetDrawState() const
{
    UINT state = 0;
    if (m_uMenuFlag & MF_HILITE)
        state |= ODS_HOTLIGHT;
    if (m_uMenuFlag & MF_CHECKED)
        state |= ODS_CHECKED;
    if (m_uMenuFlag & MF_GRAYED)
        state |= ODS_GRAYED;
    if (m_uMenuFlag & MF_MOUSESELECT)
        state |= ODS_SELECTED;
    return state;
}

CMenu *SMenuItem::GetOwnerMenu()
{
    return m_pOwnerMenu;
}

CMenu *SMenuItem::GetSubMenu()
{
    return m_pSubMenu.get();
}

//////////////////////////////////////////////////////////////////////////
CMenu::CMenu(SMenuItem *pParent)
    : m_pParent(pParent)
    , m_bMenuInitialized(FALSE)
{
}

CMenu::~CMenu(void)
{
    DestroyMenu();
}

int CMenu::GetNextMenuItem(int iItem, BOOL bForword)
{
    int change = bForword ? 1 : -1;
    int next = iItem + change;
    for (;;)
    {
        if (next < 0 || next >= m_lsMenuItem.size())
        {
            next = -1;
            break;
        }
        if (m_lsMenuItem[next].m_uMenuFlag & MF_DISABLED == 0)
            break;
    }
    return next;
}

#define RGB_MENU_SEL_BK      RGB(48, 110, 200)
#define RGB_MENU_BK          RGB(74, 137, 220)
#define RGB_MENU_HOTLIGHT_BK RGB(62, 130, 218)
#define RGB_MENU_LIGHT_TEXT  RGB(255, 255, 255)
#define RGB_MENU_GRAY_TEXT   RGB(188, 188, 188)
#define RGB_MENU_CHECKBOX    RGB(98, 210, 162)
#define RGB_MENU_CHECKBOX2   RGB(31, 171, 137)
#define RGB_MENU_POPUP       RGB(0, 0, 0)

void CMenu::DrawItemLoop(HDC memdc, RECT clentRc)
{
    HPEN Pen = CreatePen(PS_SOLID, 2, RGB_MENU_CHECKBOX);
    HBRUSH Brush = CreateSolidBrush(RGB_MENU_POPUP);

    SelectObject(memdc, Pen);
    SelectObject(memdc, Brush);

    HBRUSH GrayBrush = CreateSolidBrush(GetSysColor(RGB_MENU_GRAY_TEXT));

    int drawY = clentRc.top;

    for (const auto &ite : m_lsMenuItem)
    {
        const SIZE &size = ite.m_size;
        RECT rc{ clentRc.left, drawY, clentRc.right, drawY + size.cy };

        if (!ite.IsOwnerDraw())
        {
            const char *txt = ite.m_strTitle.c_str();
            if (ite.IsSeparator())
            {
                MoveToEx(memdc, rc.left, rc.top, nullptr);
                LineTo(memdc, rc.right, rc.top);
                drawY += size.cy;
                continue;
            }
            if (txt == nullptr)
            {
                drawY += size.cy;
                continue;
            }

            UINT state = ite.GetDrawState();

            if (ite.IsEnable() && state & (ODS_HOTLIGHT | ODS_SELECTED))
            {
                RECT rcBk{ clentRc.left, drawY, clentRc.right, drawY + size.cy };
                HBRUSH hbr = CreateSolidBrush(state & ODS_SELECTED ? RGB_MENU_SEL_BK : RGB_MENU_HOTLIGHT_BK);
                FillRect(memdc, &rcBk, hbr);
            }

            BOOL bChecked = state & ODS_CHECKED;
            BOOL bRadio = ite.m_uMenuFlag & MFT_RADIOCHECK;

            COLORREF colorText = state & ODS_GRAYED ? RGB_MENU_GRAY_TEXT : RGB_MENU_LIGHT_TEXT;
            SetTextColor(memdc, colorText);
            HGDIOBJ oldBrush = nullptr;
            if (state & ODS_GRAYED)
            {
                oldBrush = SelectObject(memdc, GrayBrush);
            }
            if (bChecked)
            {
                int itemHeigth = rc.bottom - rc.top;
                POINT ptCheckOk[3] = {
                    { rc.left + 7, rc.top + itemHeigth / 2 },
                    { rc.left + 10, rc.top + itemHeigth / 2 + 3 },
                    { rc.left + 16, rc.top + itemHeigth / 2 - 4 },
                };
                for (int i = 0; i < 2; i++)
                {
                    MoveToEx(memdc, ptCheckOk[i].x, ptCheckOk[i].y, nullptr);
                    LineTo(memdc, ptCheckOk[i + 1].x, ptCheckOk[i + 1].y);
                }
            }
            else if (bRadio)
            {
                // R1外圆半径R2 内圆半径
                int R1 = 6, R2 = 3;
                // 圆心
                POINT centerPoint = { rc.left + 6 + R1, rc.top + (rc.bottom - rc.top) / 2 };
                HGDIOBJ BackUpBrush = SelectObject(memdc, GetStockObject(NULL_BRUSH));
                Ellipse(memdc, centerPoint.x - R2, centerPoint.y - R2, centerPoint.x + R2, centerPoint.y + R2);

                HPEN Pen2 = CreatePen(PS_SOLID, 2, RGB_MENU_CHECKBOX2);

                HGDIOBJ BackUpPen = SelectObject(memdc, Pen2);

                Ellipse(memdc, centerPoint.x - R1, centerPoint.y - R1, centerPoint.x + R1, centerPoint.y + R1);
                if (BackUpBrush)
                {
                    SelectObject(memdc, BackUpBrush);
                }
                if (BackUpPen)
                {
                    SelectObject(memdc, BackUpPen);
                }
            }
            RECT rcText = { rc.left + 26, rc.top, rc.right - 10, rc.bottom };
            DrawText(memdc, txt, -1, &rcText, DT_SINGLELINE | DT_VCENTER);

            if (oldBrush)
            {
                SelectObject(memdc, oldBrush);
            }
        }
        else
        {
            DRAWITEMSTRUCT DrawItemStruct{ ODT_MENU, ite.m_id, ite.m_id, 0, ite.GetDrawState(), m_hWnd, memdc, rc, (ULONG_PTR)ite.m_data };
            ::SendMessage(s_MenuData->GetOwner(), WM_DRAWITEM, 0, (LPARAM)&DrawItemStruct);
        }
        if (ite.IsPopup())
        {
            POINT pt[3] = { 0 };
            pt[0].x = rc.right - 10;
            pt[0].y = drawY + DEFAULT_ITEM_HEIGHT / 2 - 3;
            pt[1].x = pt[0].x + 4;
            pt[1].y = pt[0].y + 3;
            pt[2].x = pt[0].x;
            pt[2].y = pt[1].y + 3;
            Polygon(memdc, pt, 3);
        }

        drawY += size.cy;
    }
    DeleteObject(Pen);
    DeleteObject(Brush);
    DeleteObject(GrayBrush);
}

BOOL CMenu::InsertMenu(UINT uPos, UINT uFlag, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    auto ite = m_lsMenuItem.begin();
    if (uFlag & MF_BYPOSITION)
    {
        if (uPos >= m_lsMenuItem.size())
            ite = m_lsMenuItem.end();
    }
    else
    {
        for (; ite < m_lsMenuItem.end(); ite++)
        {
            if (ite->m_id == uPos)
            {
                break;
            }
        }
    }

    SMenuItem item(this);
    item.m_id = uIDNewItem;

    if (MF_BITMAP & uFlag || MF_OWNERDRAW & uFlag)
    {
        if (lpNewItem == nullptr)
            return FALSE;
        item.m_data = lpNewItem;
    }
    else if (lpNewItem)
    {
        item.m_strTitle = lpNewItem;
    }
    item.SetMenuFlag(uFlag);
    if (uFlag & MF_POPUP)
    {
        if (!IsWindow(uIDNewItem))
            return FALSE;
        CMenu *pMenu = (CMenu *)GetWindowLongPtr(uIDNewItem, GWL_OPAQUE);
        if (pMenu)
        {
            item.m_pSubMenu.reset(pMenu);
        }
    }
    m_lsMenuItem.insert(ite, item);
    return TRUE;
}

BOOL CMenu::SetMenuItemInfo(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    SMenuItem *pItem = nullptr;
    if (!fByPosition)
    {
        pItem = FindItem(item);
    }
    else if (item < m_lsMenuItem.size())
        pItem = &m_lsMenuItem[item];
    if (pItem)
    {
        // MIIM_BITMAP        0x00000080 检索或设置 hbmpItem 成员。
        //  MIIM_CHECKMARKS 0x00000008 检索或设置 hbmpChecked 和 hbmpUnchecked 成员。
        //  MIIM_DATA 0x00000020 检索或设置 dwItemData 成员。
        //  MIIM_FTYPE 0x00000100 检索或设置 fType 成员。
        //  MIIM_ID 0x00000002 检索或设置 wID 成员。
        //  MIIM_STATE 0x00000001 检索或设置 fState 成员。
        //  MIIM_STRING 0x00000040 检索或设置 dwTypeData 成员。
        //  MIIM_SUBMENU 0x00000004 检索或设置 hSubMenu 成员。
        //  MIIM_TYPE 0x00000010 检索或设置 fType 和 dwTypeData 成员。
        if (lpmi->fMask & MIIM_SUBMENU)
        {
            pItem->SetPopup(true);
            pItem->m_pSubMenu.reset((CMenu *)GetWindowLongPtr(lpmi->hSubMenu, GWL_OPAQUE));
        }
    }
    return FALSE;
}

BOOL CMenu::GetMenuItemInfo(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    if (_IsGoodcbSizePtr(lpmi))
    {
        SMenuItem *pItem = nullptr;
        if (!fByPosition)
        {
            pItem = FindItem(item);
        }
        else if (item < m_lsMenuItem.size())
            pItem = &m_lsMenuItem[item];

        if (pItem)
        {
            if (lpmi->fMask & MIIM_FTYPE)
            {
                lpmi->fType = pItem->m_uMenuFlag;
            }
        }
    }
    return FALSE;
}

BOOL CMenu::EnableMenuItem(UINT uIDEnableItem, UINT uEnable)
{
    /*
    * uEnable
    值	含义
MF_BYCOMMAND
0x00000000L
指示 uIDEnableItem 提供菜单项的标识符。 如果 MF_BYCOMMAND 和 MF_BYPOSITION 标志均未指定， 则MF_BYCOMMAND 标志为默认标志。
MF_BYPOSITION
0x00000400L
指示 uIDEnableItem 提供菜单项的从零开始的相对位置。
MF_DISABLED
0x00000002L
指示菜单项已禁用，但不灰显，因此无法选择它。
MF_ENABLED
0x00000000L
指示菜单项已启用并从灰显状态还原，以便可以选择它。
MF_GRAYED
0x00000001L
指示菜单项已禁用且灰显，因此无法选中。
    */
    SMenuItem *item = FindItem(uIDEnableItem, uEnable);
    if (item)
    {
        UINT lastState = item->m_uMenuFlag;
        item->ModifyMenuFlag(uEnable & 0b11);
        return lastState;
    }
    return -1;
}

/*

值	含义
MIM_APPLYTOSUBMENUS
0x80000000
设置适用于菜单及其所有子菜单。 SetMenuInfo 使用此标志， GetMenuInfo 忽略此标志
MIM_BACKGROUND
0x00000002
检索或设置 hbrBack 成员。
MIM_HELPID
0x00000004
检索或设置 dwContextHelpID 成员。
MIM_MAXHEIGHT
0x00000001
检索或设置 cyMax 成员。
MIM_MENUDATA
0x00000008
检索或设置 dwMenuData 成员。
MIM_STYLE
0x00000010
检索或设置 dwStyle 成员。
*/

BOOL CMenu::GetMenuInfo(LPMENUINFO lpMenuInfo)
{
    if (_IsGoodcbSizePtr(lpMenuInfo))
    {
        if (lpMenuInfo->fMask & MIM_MENUDATA)
        {
            lpMenuInfo->dwMenuData = m_dwMenuData;
        }
    }
    return FALSE;
}

BOOL CMenu::SetMenuInfo(LPCMENUINFO lpMenuInfo)
{
    if (_IsGoodcbSizePtr(lpMenuInfo))
    {
        if (lpMenuInfo->fMask & MIM_MENUDATA)
        {
            m_dwMenuData = lpMenuInfo->dwMenuData;
        }
    }
    return FALSE;
}

SIZE CMenu::CalcMenuSize()
{
    SIZE size{ DEFAULT_MIN_ITEM_WIDTH, 0 };
    HDC hdc = GetDC(m_hWnd);
    for (int i = 0; i < m_lsMenuItem.size(); i++)
    {
        SIZE itemSize = m_lsMenuItem[i].GetItemSize(hdc);
        size.cx = std::max(itemSize.cx, size.cx);
        size.cy += itemSize.cy;
    }
    ReleaseDC(m_hWnd, hdc);
    return size;
}

void CMenu::InvalidateItem(int iItem)
{
    RECT rcItem;
    if (GetItemRect(iItem, &rcItem))
    {
        InvalidateRect(m_hWnd, &rcItem, TRUE);
    }
}

BOOL CMenu::GetItemRect(int iItem, RECT *prc) const
{
    if (iItem < 0 || iItem >= m_lsMenuItem.size())
        return FALSE;
    GetClientRect(m_hWnd, prc);

    for (int i = 0; i < iItem; i++)
    {
        prc->top += m_lsMenuItem[i].m_size.cy;
    }
    prc->bottom = prc->top + m_lsMenuItem[iItem].m_size.cy;
    return TRUE;
}

int CMenu::Pos2Item(POINT pt) const
{
    int cy = 0;
    for (int i = 0; i < m_lsMenuItem.size(); i++)
    {
        cy += m_lsMenuItem[i].m_size.cy;
        if (pt.y < cy)
            return i;
    }
    return -1;
}

SMenuItem *CMenu::GetMenuItem(int iItem)
{
    if (iItem < 0 || iItem >= m_lsMenuItem.size())
        return nullptr;
    return &m_lsMenuItem[iItem];
}

BOOL CMenu::CreateMenu()
{
    return __baseCls::CreateWindow(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WC_MENU, "", WS_POPUP, 0, 0, 0, 0, 0, 0, nullptr);
}

UINT CMenu::GetMenuItemID(int nPos)
{
    SMenuItem *pItem = GetMenuItem(nPos, FALSE);
    if (!pItem)
        return -1;
    return pItem->m_id;
}

int CMenu::GetMenuItemCount()
{
    return (int)m_lsMenuItem.size();
}

SMenuItem *CMenu::GetMenuItem(int nID, BOOL byCmdId)
{
    if (byCmdId)
    {
        return FindItem(nID);
    }
    else
    {
        return GetMenuItem(nID);
    }
    return nullptr;
}

CMenu *CMenu::GetSubMenu(int nID, BOOL byCmdId)
{
    SMenuItem *pItem = GetMenuItem(nID, byCmdId);
    if (!pItem)
        return nullptr;
    return pItem->GetSubMenu();
}

CMenu *CMenu::GetSubMenu(int nPos)
{
    return GetSubMenu(nPos, FALSE);
}

UINT CMenu::TrackPopupMenu(UINT flag, int x, int y, HWND hOwner, LPTPMPARAMS prcRect)
{
    if (!IsWindow(m_hWnd))
        return (UINT)-1;
    if (!s_MenuData)
        s_MenuData = new SMenuRunData(hOwner);

    s_MenuData->m_prcRect = prcRect;

    HWND hActive = hOwner;
    // 是否IsWindowEnabled的实现和WINDOWS形为不一致？，如果是ownerdarw，这里的窗口是不显示的。
    if (!hOwner || !::IsWindowEnabled(hOwner) || !::IsWindowVisible(hOwner))
        hActive = ::GetActiveWindow();

    HWND hRoot = hActive;
    while ((::GetWindowLong(hRoot, GWL_STYLE) & WS_CHILD) && ::GetParent(hRoot))
    {
        hRoot = ::GetParent(hRoot);
    }
    SetForegroundWindow(hRoot);
    ShowMenu(flag, x, y);
    RunMenu(hRoot);
    HideMenu(FALSE);

    if (hActive)
    {
        POINT pt;
        GetCursorPos(&pt);
        ::ScreenToClient(hActive, &pt);
        ::PostMessage(hActive, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
    }

    int nRet = s_MenuData->GetCmdID();

    delete s_MenuData;
    s_MenuData = nullptr;

    if (flag & TPM_RETURNCMD)
    {
        return nRet;
    }
    else
    {
        ::SendMessage(hOwner, WM_COMMAND, MAKEWPARAM(nRet, 0), 0);
        return TRUE;
    }
}

void CMenu::ShowMenu(UINT uFlag, int x, int y)
{
    SendInitPopupMenu2Owner(0);

    SIZE szMenu = CalcMenuSize();

    if (uFlag & TPM_CENTERALIGN)
    {
        x -= szMenu.cx / 2;
    }
    else if (uFlag & TPM_RIGHTALIGN)
    {
        x -= szMenu.cx;
    }

    if (uFlag & TPM_VCENTERALIGN)
    {
        y -= szMenu.cy / 2;
    }
    else if (uFlag & TPM_BOTTOMALIGN)
    {
        y -= szMenu.cy;
    }

    HMONITOR hMor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(MONITORINFO), 0 };
    GetMonitorInfo(hMor, &mi);

    RECT rcMenu{ x, y, x + szMenu.cx, y + szMenu.cy };
    RECT rcInter;
    RECT rcMointor = mi.rcMonitor;

    if (s_MenuData->m_prcRect && (s_MenuData->m_prcRect->cbSize == sizeof(TPMPARAMS)))
    {
        SubtractRect(&rcMointor, &(mi.rcMonitor), &(s_MenuData->m_prcRect->rcExclude));
    }

    IntersectRect(&rcInter, &rcMenu, &rcMointor);

    int subMenuOffset = 2;

    if (EqualRect(&rcInter, &rcMenu))
    {
        if (m_pParent)
        {
            CMenu *pParent = m_pParent->GetOwnerMenu();
            RECT rcParent;
            GetWindowRect(pParent->m_hWnd, &rcParent);
            if (rcMenu.right > rcMointor.right)
            {
                x = x - szMenu.cx - (rcParent.right - rcParent.left) - subMenuOffset;
                rcMenu.right = rcMenu.right - rcMenu.left + x;
                rcMenu.left = x;
            }
            else
            {
                x = x + subMenuOffset;
                rcMenu.right = rcMenu.right - rcMenu.left + x;
                rcMenu.left = x;
            }

            int xOffset = 0, yOffset = 0;
            if (rcMenu.left < rcMointor.left)
                xOffset = rcMointor.left - rcMenu.left;
            else if (rcMenu.right > rcMointor.right)
                xOffset = rcMointor.right - rcMenu.right;
            if (rcMenu.top < rcMointor.top)
                yOffset = rcMointor.top - rcMenu.top;
            else if (rcMenu.bottom > rcMointor.bottom)
                yOffset = rcMointor.bottom - rcMenu.bottom;

            ::OffsetRect(&rcMenu, xOffset, yOffset);
        }
        else
        {
            if (rcMenu.right > rcMointor.right)
            {
                x = x - szMenu.cx;
                rcMenu.right = rcMenu.right - rcMenu.left + x;
                rcMenu.left = x;
            }
            else
            {
                x = x;
                rcMenu.right = rcMenu.right - rcMenu.left + x;
                rcMenu.left = x;
            }

            if (rcMenu.top < rcMointor.top)
            {
                y = y + szMenu.cy;
                rcMenu.bottom = rcMenu.bottom - rcMenu.top + y;
                rcMenu.top = y;
            }
            if (rcMenu.bottom > rcMointor.bottom)
            {
                y = y - szMenu.cy;
                rcMenu.bottom = rcMenu.bottom - rcMenu.top + y;
                rcMenu.top = y;
            }
        }
    }
    else
    {
        if (m_pParent)
            x = x + subMenuOffset;
        rcMenu.right = rcMenu.right - rcMenu.left + x;
        rcMenu.left = x;
    }

    SetWindowPos(m_hWnd, HWND_TOPMOST, rcMenu.left, rcMenu.top, szMenu.cx, szMenu.cy, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    s_MenuData->PushMenuEx(this);
}

void CMenu::HideMenu(BOOL bUncheckParentItem)
{
    if (!IsWindowVisible(m_hWnd))
        return;
    HideSubMenu();
    ShowWindow(m_hWnd, SW_HIDE);
    if (m_iSelItem != -1)
    {
        SMenuItem *pItem = GetMenuItem(m_iSelItem);
        if (pItem)
            pItem->SetCheck(false);
        m_iSelItem = -1;
    }
    if (m_iHoverItem != -1)
    {
        SMenuItem *pItem = GetMenuItem(m_iHoverItem);
        if (pItem)
            pItem->SetHotItem(false);
        m_iHoverItem = -1;
    }
    s_MenuData->PopMenuEx();
    if (m_pParent)
    {
        m_pParent->OnSubMenuHided(bUncheckParentItem);
    }
}

void CMenu::HideSubMenu()
{
    printf("!!!hide sub menu %d\n", m_iSelItem);
    SMenuItem *pItem = GetMenuItem(m_iSelItem);
    if (pItem && pItem->GetSubMenu())
    {
        pItem->GetSubMenu()->HideMenu(TRUE);
    }
}

int CMenu::OnMouseActivate(HWND wndTopLevel, UINT nHitTest, UINT message)
{
    return MA_NOACTIVATE;
}

void CMenu::RunMenu(HWND hRoot)
{
    BOOL bMsgQuit(FALSE);
    HWND hCurMenu(0);

    for (;;)
    {

        if (s_MenuData->IsMenuExited())
        {
            break;
        }

        if (GetForegroundWindow() != hRoot)
        {
            break;
        }
        MSG msg = { 0 };

        for (;;)
        { // 获取菜单相关消息，抄自wine代码
            if (::PeekMessage(&msg, 0, 0, 0, FALSE))
            {
                if (!CallMsgFilter(&msg, MSGF_MENU))
                    break;
                ::PeekMessage(&msg, 0, msg.message, msg.message, TRUE);
            }
            else
            {
                ::WaitMessage();
            }
        }

        if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP)
        { // 拦截alt键
            if (msg.wParam == VK_MENU)
            { // handle alt key down, exit menu loop
                s_MenuData->ExitMenu(0);
                break;
            }
        }
        else if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN || msg.message == WM_LBUTTONDBLCLK)
        {
            // click on other window
            if (!s_MenuData->IsMenuWnd(msg.hwnd))
            {
                s_MenuData->ExitMenu(0);
                break;
            }
            // 同步为 windows自身菜单形为
            else
            {
                CMenu *pMenu = s_MenuData->SMenuExFromHwnd(msg.hwnd);

                SMenuItem *pItem = pMenu->GetMenuItem(pMenu->m_iSelItem);
                if (pItem)
                {
                    if (CMenu *pHideMenu = pItem->GetSubMenu())
                    {
                        SMenuItem *pItem = pHideMenu->GetMenuItem(pHideMenu->m_iSelItem);
                        if (pItem)
                        {
                            if (pHideMenu = pItem->GetSubMenu())
                                pHideMenu->HideMenu(TRUE);
                        }
                    }
                }
            }
        }
        else if (msg.message == WM_QUIT)
        {
            bMsgQuit = TRUE;
        }

        // 移除消息队列中当前的消息。

        ::PeekMessage(&msg, 0, msg.message, msg.message, TRUE);

        // 拦截非菜单窗口的MouseMove消息
        if (msg.message == WM_MOUSEMOVE)
        {
            if (msg.hwnd != hCurMenu)
            {
                if (hCurMenu)
                {
                    ::SendMessage(hCurMenu, WM_MOUSELEAVE, 0, 0);
                }
                hCurMenu = msg.hwnd;
            }

            CMenu *pMenu = s_MenuData->SMenuExFromHwnd(msg.hwnd);
            if (!pMenu)
            {
                hCurMenu = 0;
            }
        }

        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);

        if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_CHAR)
        { // 将键盘事件强制发送到最后一级菜单窗口，让菜单处理快速键
            HWND menuWnd = s_MenuData->GetMenuEx()->m_hWnd;
            ::SendMessage(menuWnd, msg.message, msg.wParam, msg.lParam);
        }

        if (bMsgQuit)
        {
            break;
        }
    }
}

void CMenu::OnTimer(UINT_PTR timeID)
{
    if (timeID != TIMERID_POPSUBMENU)
    {
        SetMsgHandled(FALSE);
        return;
    }
    KillTimer(m_hWnd, timeID);
    if (m_iSelItem != -1)
    {
        PopupSubMenu(m_iSelItem, TRUE);
    }
}

void CMenu::OnSubMenuHided(BOOL bUncheckItem)
{
    if (!bUncheckItem)
    {
        InvalidateItem(m_iSelItem);
        m_iSelItem = -1;
    }
}

void CMenu::PopupSubMenu(int iItem, BOOL bCheckFirstItem)
{
    SMenuItem *pItem = GetMenuItem(iItem);
    if (!pItem)
        return;
    CMenu *pSubMenu = pItem->GetSubMenu();
    if (!pSubMenu)
        return;
    if (!pSubMenu->m_bMenuInitialized)
    {
        pSubMenu->SendInitPopupMenu2Owner(0);
    }

    RECT rcWnd;
    GetWindowRect(m_hWnd, &rcWnd);
    RECT rcItem;
    GetItemRect(iItem, &rcItem);
    POINT showpt;
    showpt.x = rcWnd.right + 5, showpt.y = rcWnd.top + rcItem.top;
    m_iSelItem = iItem;
    printf("show sub menu %d\n", iItem);
    pSubMenu->ShowMenu(0, showpt.x, showpt.y);
}

BOOL CMenu::HoverItem(int iItem)
{
    if (iItem == m_iHoverItem)
        return TRUE;
    if (SMenuItem *pItem = GetMenuItem(m_iHoverItem))
    {
        pItem->SetHotItem(false);
        InvalidateItem(iItem);
    }
    m_iHoverItem = iItem;
    if (SMenuItem *pItem = GetMenuItem(iItem))
    {
        pItem->SetHotItem(true);
    }
    return TRUE;
}

BOOL CMenu::SelectItem(int iItem)
{
    if (iItem == m_iSelItem)
        return TRUE;
    if (SMenuItem *pItem = GetMenuItem(m_iSelItem))
    {
        pItem->SetCheck(FALSE);
        InvalidateItem(m_iSelItem);
        if (pItem->GetSubMenu())
        {
            pItem->GetSubMenu()->HideMenu(FALSE);
        }
    }
    m_iSelItem = iItem;
    if (SMenuItem *pItem = GetMenuItem(iItem))
    {
        pItem->SetCheck(TRUE);
        InvalidateItem(iItem);
        if (pItem->GetSubMenu())
        {
            KillTimer(m_hWnd, TIMERID_POPSUBMENU);
            SetTimer(m_hWnd, TIMERID_POPSUBMENU, TIMESPAN_PUPUPSUBMENU, nullptr);
        }
    }
    return TRUE;
}

void CMenu::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    // WM_MENUCHAR
    /*if (IsOwnerDraw())
    {
        if (s_MenuData)
        {
            if (::IsWindow(s_MenuData->GetOwner()))
            {
                LRESULT hr=        ::SendMessage(s_MenuData->GetOwner(), WM_MENUCHAR, MAKEWPARAM(nChar, nFlags), (LPARAM)m_hWnd);
                if (hr != 0)
                {
                    DWORD id = LOWORD(hr);
                    DWORD doit = HIWORD(hr);
                }
            }
        }
    }*/

    switch (nChar)
    {
    case VK_UP:
    case VK_DOWN:
    {
        int nextItem = GetNextMenuItem(m_iSelItem, nChar == VK_DOWN);
        if (nextItem != -1)
        {
            SelectItem(nextItem);
        }
    }
    break;
    case VK_ESCAPE:
    case VK_LEFT:
        if (m_pParent)
        {
            HideMenu(TRUE);
        }
        else
        {
            s_MenuData->ExitMenu(0);
        }
        break;
    case VK_RIGHT:
        PopupSubMenu(m_iSelItem, TRUE);
        break;
    case VK_RETURN:
        // if (m_pCheckItem)
        /*m_pCheckItem->FireCommand()*/;
        break;
    default:
        SetMsgHandled(FALSE);
        break;
    }
}

void CMenu::OnPaint(HDC hdc)
{
    RECT rcWnd;
    GetClientRect(m_hWnd, &rcWnd);
    PAINTSTRUCT ps;
    hdc = BeginPaint(m_hWnd, &ps);

    HDC memdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top);
    HGDIOBJ oldBmp = SelectObject(memdc, bmp);

    // 绘制默认背景
    int save = SaveDC(memdc);
    HBRUSH hbr = CreateSolidBrush(RGB_MENU_BK);
    FillRect(memdc, &rcWnd, hbr);
    // 绘制项
    DrawItemLoop(memdc, rcWnd);
    RestoreDC(memdc, save);
    BitBlt(hdc, rcWnd.left, rcWnd.top, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top, memdc, 0, 0, SRCCOPY);

    SelectObject(memdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memdc);

    EndPaint(m_hWnd, &ps);
}

void CMenu::OnLButtonUp(UINT nFlags, POINT point)
{
    int iItem = Pos2Item(point);

    if (iItem != -1)
    {
        CMenu *pChildMenu = m_lsMenuItem[iItem].GetSubMenu();

        if (m_lsMenuItem[iItem].IsEnable() && pChildMenu == nullptr)
            CMenu::EndMenu(m_lsMenuItem[iItem].m_id);
        else if (pChildMenu)
        {
            KillTimer(m_hWnd, TIMERID_POPSUBMENU);
            m_iSelItem = iItem;
            PopupSubMenu(m_iSelItem, TRUE);
        }
    }
}

void CMenu::OnMouseMove(UINT nFlags, POINT point)
{
    int newitem = Pos2Item(point);
    SMenuItem *pItem = GetMenuItem(newitem);
    if (pItem && pItem->IsEnable())
    {
        HoverItem(newitem);
        SelectItem(newitem);
    }
}

void CMenu::OnMouseLeave()
{
    HoverItem(-1);
}

void CMenu::EndMenu(int nCmdId /*=0*/)
{
    if (!s_MenuData)
        return;
    s_MenuData->ExitMenu(nCmdId);
    ::PostMessage(s_MenuData->GetOwner(), WM_NULL, 0, 0);
}

SMenuItem *CMenu::GetParentItem()
{
    return m_pParent;
}

EXTERN_C void EndMenuEx(int nCmdId)
{
    CMenu::EndMenu(nCmdId);
}

SMenuItem *CMenu::FindItem(UINT id)
{
    for (int idx = 0; idx < m_lsMenuItem.size(); idx++)
    {
        if (m_lsMenuItem[idx].m_id == id)
        {
            return &m_lsMenuItem[idx];
        }
    }
    // 如果没有找到，接着查找所以子菜单项
    for (int idx = 0; idx < m_lsMenuItem.size(); idx++)
    {
        if (CMenu *pChildMenu = m_lsMenuItem[idx].GetSubMenu())
        {
            SMenuItem *pRet = pChildMenu->FindItem(id);
            if (pRet)
                return pRet;
        }
    }
    return nullptr;
}

SMenuItem *CMenu::FindItem(UINT uPos, UINT uFlag)
{
    SMenuItem *pItemRef = nullptr;

    if (uFlag & MF_BYPOSITION)
    {
        if (uPos < m_lsMenuItem.size())
        {
            pItemRef = &(m_lsMenuItem[uPos]);
        }
    }
    else
    {
        pItemRef = FindItem(uPos);
    }
    return pItemRef;
}

BOOL CMenu::AppendMenu(UINT uFlags, int uIDNewItem, LPCSTR lpNewItem)
{
    return InsertMenu(-1, uFlags | MF_BYPOSITION, uIDNewItem, lpNewItem);
}

BOOL CMenu::DeleteMenu(UINT uPos, UINT uFlag)
{
    if (uFlag & MF_BYPOSITION)
    {
        if (uPos < m_lsMenuItem.size())
        {
            m_lsMenuItem.erase(m_lsMenuItem.begin() + uPos);
        }
    }
    else
    {
        for (auto ite = m_lsMenuItem.begin(); ite < m_lsMenuItem.end(); ite++)
        {
            if (ite->m_id == uPos)
            {
                m_lsMenuItem.erase(ite);
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL CMenu::CheckMenuItem(UINT uPos, UINT uFlag)
{
    SMenuItem *pItemRef = FindItem(uPos, uFlag);
    if (!pItemRef)
        return FALSE;
    pItemRef->SetCheck(uFlag & MF_CHECKED);
    return TRUE;
}

BOOL CMenu::CheckMenuRadioItem(UINT idFirst, UINT idLast, UINT idCheck, UINT uFlags)
{
    SMenuItem *pItemRefFirst = FindItem(idFirst, uFlags);
    SMenuItem *pItemRefLast = FindItem(idLast, uFlags);
    SMenuItem *pItemRefCheck = FindItem(idCheck, uFlags);

    if (!pItemRefFirst || !pItemRefLast || !pItemRefCheck)
        return FALSE;

    int idxFirst = -1;
    int idxLast = -1;
    int idxCheck = -1;

    /*SWindow *pChild = m_pMenuRoot->GetWindow(GSW_FIRSTCHILD);
    int i = 0;
    while (pChild)
    {
        if (pChild == pItemRefFirst)
            idxFirst = i;
        else if (pChild == pItemRefCheck)
            idxCheck = i;
        else if (pChild == pItemRefLast)
            idxLast = i;
        pChild = pChild->GetWindow(GSW_NEXTSIBLING);
        i++;
    }
    if (idxFirst == -1 || idxLast == -1 || idxCheck == -1)
        return FALSE;
    if (idxFirst < idxLast)
    {
        SWindow *t = pItemRefFirst;
        pItemRefFirst = pItemRefLast;
        pItemRefLast = t;
        int tIdx = idxFirst;
        idxFirst = idxLast;
        idxLast = tIdx;
    }

    if (idxFirst > idxCheck || idxLast < idxCheck)
        return FALSE;

    pChild = pItemRefFirst;
    for (;;)
    {
        pChild->SetAttribute(L"radio", L"1");
        if (pChild == pItemRefCheck)
        {
            pChild->SetAttribute(L"check", L"1");
        }
        else
        {
            pChild->SetAttribute(L"check", L"0");
        }
        if (pChild == pItemRefLast)
            break;
        else
            pChild = pChild->GetWindow(GSW_NEXTSIBLING);
    }*/
    return TRUE;
}

void CMenu::SendInitPopupMenu2Owner(int idx)
{
    if (m_bMenuInitialized)
        return;

    if (::IsWindow(s_MenuData->GetOwner()))
    {
        ::SendMessage(s_MenuData->GetOwner(), WM_INITMENUPOPUP, (WPARAM)this, (LPARAM)idx);
    }
    m_bMenuInitialized = TRUE;
}

DWORD CMenu::GetContextHelpId() const
{
    return m_dwContextHelpId;
}

BOOL CMenu::SetContextHelpId(DWORD dwId)
{
    m_dwContextHelpId = dwId;
    return TRUE;
}

BOOL CMenu::InsertMenuItem(UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    return FALSE;
}

void CMenu::DestroyMenu()
{
    if (::IsWindow(m_hWnd))
        ::DestroyWindow(m_hWnd);
}

BOOL CMenu::RemoveMenu(UINT uPosition, UINT uFlags)
{
    return FALSE;
}

BOOL CMenu::ModifyMenu(UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    return FALSE;
}

BOOL CMenu::ModifyMenuString(UINT uPosition, UINT uFlags, LPCSTR lpItemString)
{
    SMenuItem *pItemRef = FindItem(uPosition, uFlags);
    if (!pItemRef)
        return FALSE;
    pItemRef->SetText(lpItemString);
    return TRUE;
}

//-------------------------------------------------------------------------------
HMENU WINAPI CreatePopupMenu(VOID)
{
    CMenu *pMenu = new CMenu();
    if (!pMenu->CreateMenu())
    {
        delete pMenu;
        return 0;
    }
    return pMenu->m_hWnd;
}

BOOL WINAPI DestroyMenu(HMENU hMenu)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    pMenu->DestroyMenu();
    delete pMenu;
    return TRUE;
}

DWORD WINAPI CheckMenuItem(HMENU hMenu, UINT uIDCheckItem, UINT uCheck)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    return pMenu->CheckMenuItem(uIDCheckItem, uCheck);
}

BOOL WINAPI EnableMenuItem(HMENU hMenu, UINT uIDEnableItem, UINT uEnable)
{
    if (!IsMenu(hMenu))
        return -1;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    return pMenu->EnableMenuItem(uIDEnableItem, uEnable);
}

HMENU WINAPI GetSubMenu(HMENU hMenu, int nPos)
{
    if (!IsMenu(hMenu))
        return 0;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        CMenu *pSubMenu = pMenu->GetSubMenu(nPos);
        if (pSubMenu)
            return pSubMenu->m_hWnd;
    }
    return 0;
}

UINT WINAPI GetMenuItemID(HMENU hMenu, int nPos)
{
    if (!IsMenu(hMenu))
        return -1;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->GetMenuItemID(nPos);
    }
    return -1;
}

int WINAPI GetMenuItemCount(HMENU hMenu)
{
    if (!IsMenu(hMenu))
        return -1;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->GetMenuItemCount();
    }
    return -1;
}

BOOL WINAPI ModifyMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->ModifyMenu(uPosition, uFlags, uIDNewItem, lpNewItem);
    }
    return FALSE;
}

BOOL WINAPI ModifyMenuW(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem){
    std::string str;
    tostring(lpNewItem,-1,str);
    return ModifyMenuA(hMenu,uPosition,uFlags,uIDNewItem,str.c_str());
}

BOOL WINAPI RemoveMenu(HMENU hMenu, UINT uPosition, UINT uFlags)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->RemoveMenu(uPosition, uFlags);
    }
    return FALSE;
}

BOOL WINAPI DeleteMenu(HMENU hMenu, UINT uPosition, UINT uFlags)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->DeleteMenu(uPosition, uFlags);
    }
    return FALSE;
}

BOOL WINAPI SetMenuItemBitmaps(HMENU hMenu, UINT uPosition, UINT uFlags, HBITMAP hBitmapUnchecked, HBITMAP hBitmapChecked)
{
    return FALSE;
}

BOOL WINAPI TrackPopupMenu(HMENU hMenu, UINT uFlags, int x, int y, int nReserved, HWND hWnd, const RECT *prcRect)
{
    return TrackPopupMenuEx(hMenu, uFlags, x, y, hWnd, nullptr);
}

BOOL WINAPI TrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS lpTpmParams)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->TrackPopupMenu(uFlags, x, y, hWnd, lpTpmParams);
    }
    return 0;
}

BOOL WINAPI GetMenuInfo(HMENU hMenu, LPMENUINFO lpMenuInfo)
{
    if (lpMenuInfo && !IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->GetMenuInfo(lpMenuInfo);
    }
    return FALSE;
}

BOOL WINAPI SetMenuInfo(HMENU hMenu, LPCMENUINFO lpMenuInfo)
{
    if (lpMenuInfo && !IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->SetMenuInfo(lpMenuInfo);
    }
    return FALSE;
}

BOOL WINAPI SetMenuContextHelpId(HMENU hMenu, DWORD dwHelpID)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->SetContextHelpId(dwHelpID);
    }
    return FALSE;
}

DWORD WINAPI GetMenuContextHelpId(HMENU hMenu)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->GetContextHelpId();
    }
    return FALSE;
}

BOOL WINAPI IsMenu(HMENU hMenu)
{
    if (!IsWindow(hMenu))
        return FALSE;
    char szCls[MAX_ATOM_LEN + 1] = { 0 };
    GetClassNameA(hMenu, szCls, MAX_ATOM_LEN + 1);
    return strcmp(szCls, WC_MENU) == 0;
}

BOOL WINAPI CheckMenuRadioItem(HMENU hmenu, UINT first, UINT last, UINT check, UINT flags)
{
    return 0;
}

BOOL WINAPI InsertMenuA(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->InsertMenu(uPosition, uFlags, uIDNewItem, lpNewItem);
    }
    return FALSE;
}

BOOL WINAPI InsertMenuW(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem)
{
    std::string str;
    tostring(lpNewItem,-1,str);
    return InsertMenuA(hMenu,uPosition,uFlags,uIDNewItem,str.c_str());
}

BOOL WINAPI SetMenuItemInfo(HMENU hMenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (pMenu)
    {
        return pMenu->SetMenuItemInfo(item, fByPosition, lpmi);
    }
    return FALSE;
}

BOOL WINAPI GetMenuItemInfo(HMENU hMenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (lpmi && pMenu)
    {
        return pMenu->GetMenuItemInfo(item, fByPosition, lpmi);
    }
    return FALSE;
}

BOOL WINAPI InsertMenuItem(HMENU hMenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmi)
{
    if (!IsMenu(hMenu))
        return FALSE;
    CMenu *pMenu = (CMenu *)GetWindowLongPtr(hMenu, GWL_OPAQUE);
    if (lpmi && pMenu)
    {
        return pMenu->InsertMenuItem(item, fByPosition, lpmi);
    }
    return FALSE;
}


BOOL WINAPI AppendMenuA(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
{
    return InsertMenu(hMenu, -1, uFlags, uIDNewItem, lpNewItem);
}

BOOL WINAPI AppendMenuW(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCWSTR lpNewItem)
{
    std::string str;
    tostring(lpNewItem,-1,str);
    return AppendMenuA(hMenu, uFlags, uIDNewItem, str.c_str());
}