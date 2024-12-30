#include "../nativewnd.h"
#include "../tostring.hpp"
#include "cmnctl32.h"
#include <unordered_map>
#include <algorithm>
#include <src/builtin_image.h>

class CMessageBox : public CNativeWnd {

    RECT CalcTextRect(LPCSTR text, int maxWid)
    {
        if (text == nullptr)
            return RECT();
        HDC hDc = GetDC(m_hWnd);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT hOldFont = (HFONT)SelectObject(hDc, hFont);
        //此处SOUI实现和windows API DrawText实现不一致
        RECT rc = { 0, 0, 0, 65535 };
        rc.right = maxWid;        
        DrawText(hDc, text, -1, &rc, DT_CALCRECT);

        SelectObject(hDc, hOldFont);
        ReleaseDC(m_hWnd, hDc);
        return rc;
    }

  public:
    void InitMsgBox(LPCSTR lpText, UINT uType)
    {
        m_retCode = IDCANCEL;

        std::unordered_map<int, std::string> _ButtonInfo;

        switch (uType & 0x0f)
        {
        case MB_ABORTRETRYIGNORE:
            _ButtonInfo.insert({ IDABORT, u8"中止" });
            _ButtonInfo.insert({ IDRETRY, u8"重试" });
            _ButtonInfo.insert({ IDIGNORE, u8"忽略" });
            break;
        case MB_CANCELTRYCONTINUE:
            _ButtonInfo.insert({ IDCANCEL, u8"取消" });
            _ButtonInfo.insert({ IDTRYAGAIN, u8"重试" });
            _ButtonInfo.insert({ IDCONTINUE, u8"继续" });
            break;
        case MB_OK:
            _ButtonInfo.insert({ IDOK, u8"确定" });
            break;
        case MB_OKCANCEL:
            _ButtonInfo.insert({ IDOK, u8"确定" });
            _ButtonInfo.insert({ IDCANCEL, u8"取消" });
            break;
        case MB_RETRYCANCEL:
            _ButtonInfo.insert({ IDTRYAGAIN, u8"重试" });
            _ButtonInfo.insert({ IDCANCEL, u8"取消" });
            break;
        case MB_YESNO:
            _ButtonInfo.insert({ IDYES, u8"是" });
            _ButtonInfo.insert({ IDNO, u8"否" });
            break;
        case MB_YESNOCANCEL:
            _ButtonInfo.insert({ IDYES, u8"是" });
            _ButtonInfo.insert({ IDNO, u8"否" });
            _ButtonInfo.insert({ IDCANCEL, u8"取消" });
            break;
        default:
            _ButtonInfo.insert({ IDOK, u8"确定" });
            break;
        }
        switch (uType & 0xf0)
        {
        case MB_ICONHAND:
            m_iconIdx = 0;
            break;
        case MB_ICONQUESTION:
            m_iconIdx = 4;
            break;
        case MB_ICONEXCLAMATION:
            m_iconIdx = 2;
            break;
        case MB_ICONASTERISK:
            m_iconIdx = 1;
            break;
        default:
            m_iconIdx = -1;
        }

        RECT rcWnd;
        GetClientRect(m_hWnd, &rcWnd);
        // GetSystemMetrics 的SM_CXFULLSCREEN未实现
        MONITORINFO info{ sizeof(MONITORINFO) };
        GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &info);

        int nScreenWid = info.rcWork.right - info.rcWork.left;
        int nScreenHei = info.rcWork.bottom - info.rcWork.top;
        RECT rcText = CalcTextRect(lpText, nScreenWid);
        int nContentWid = rcText.right - rcText.left; // 文字宽度
        nContentWid += ((m_iconIdx != -1) ? 49 : 0) + 6 + 20; // 图标宽度

        int wndWid = rcWnd.right - rcWnd.left;
        wndWid = std::max(wndWid, nContentWid);

        int buttonWid = _ButtonInfo.size() * (120 + 6) - 6;

        wndWid = std::max(wndWid, buttonWid + 20);

        rcWnd.right = rcWnd.left + wndWid;

        int nContentHei = std::max((m_iconIdx != -1) ? 49 : 0, rcText.bottom - rcText.top); // 文字图标最小高度
        nContentHei += 30 + 6 + 36;                                                         // 加上边距和间距和按钮高度
        int wndHei = std::max(rcWnd.bottom - rcWnd.top, nContentHei);

        rcWnd.bottom = rcWnd.top + wndHei;

        HWND hParentWnd = GetParent(m_hWnd);

        RECT rcParent = { 0, 0, nScreenWid, nScreenHei };
        if (IsWindow(hParentWnd))
        {
            GetWindowRect(hParentWnd, &rcParent);
        }
        int nParentWidth = rcParent.right - rcParent.left;
        int nParentHeight = rcParent.bottom - rcParent.top;
        // 居中父窗口
        int offsetX = rcParent.left + ((nParentWidth - wndWid) != 0 ? (nParentWidth - wndWid) / 2 : 0);
        int offsetY = rcParent.top + ((nParentHeight - wndHei) != 0 ? (nParentHeight - wndHei) / 2 : 0);

        /* rcWnd.left += offsetX;
         rcWnd.right += offsetX;
         rcWnd.bottom += offsetY;
         rcWnd.top += offsetY;*/

        OffsetRect(&rcWnd, offsetX, offsetY);
        // SetWindowPos(m_hWnd, hParentWnd,rcWnd.left, rcWnd.top, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top,0);
        MoveWindow(m_hWnd, rcWnd.left, rcWnd.top, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top, FALSE);

        CreateWindowEx(0, WC_STATIC, lpText, WS_CHILD | WS_VISIBLE, 10 + 6 + ((m_iconIdx != -1) ? 49 : 0), 20, rcText.right - rcText.left, rcText.bottom - rcText.top, m_hWnd, 0, 0, 0);

        POINT startPos = { (wndWid - buttonWid) / 2, rcWnd.bottom - rcWnd.top - 10 - 36 };

        for (const auto &item : _ButtonInfo)
        {
            CreateWindowEx(0, WC_BUTTON, item.second.c_str(), WS_CHILD | WS_VISIBLE, startPos.x, startPos.y, 120, 36, m_hWnd, item.first, 0, 0);
            startPos.x += 6 + 120;
        }
    }

    BOOL OnEraseBkgnd(HDC hdc)
    {
        RECT rc;
        GetClientRect(m_hWnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return TRUE;
    }

    RECT GetIconRect()
    {
        return { 10, 10, 59, 59 };
    }

    void OnPaint(HDC hdc)
    {
        PAINTSTRUCT ps;
        BeginPaint(m_hWnd, &ps);
        if (m_iconIdx != -1)
        {
            auto builtinImage = BuiltinImage::instance();
            RECT iconRect = GetIconRect();
            builtinImage->drawBiIdx(ps.hdc, BuiltinImage::BI_MSGBOXICONS, m_iconIdx, &iconRect, 0xFF);
        }
        EndPaint(m_hWnd, &ps);
    }

    void OnCommand(UINT uNotifyCode, int nID, HWND wndCtl)
    {
        m_retCode = nID;
        PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }

    BEGIN_MSG_MAP_EX(CMessageBox)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_COMMAND(OnCommand)
    END_MSG_MAP()

    int m_retCode = IDOK;
    int m_iconIdx = 0;
};

int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    CMessageBox msgBoxWnd;

    DWORD dwStyle = WS_POPUP | WS_CAPTION;
    dwStyle &= ~WS_MINIMIZEBOX; // 设置窗体取消最小化按钮
    dwStyle &= ~WS_MAXIMIZEBOX; // 设置窗体取消最大化按钮

    msgBoxWnd.CreateWindowA(WS_EX_TOPMOST, CLS_WINDOW, lpCaption, dwStyle, 0, 0, 10, 10, 0, 0, 0);
    // ShowWindow(msgBoxWnd.m_hWnd,SW_HIDE);
    SetParent(msgBoxWnd.m_hWnd, hWnd);
    msgBoxWnd.InitMsgBox(lpText, uType);

    EnableWindow(hWnd, FALSE);
    ShowWindow(msgBoxWnd.m_hWnd, SW_SHOWNORMAL);
    UpdateWindow(msgBoxWnd.m_hWnd);
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0))
    {
        if (msg.hwnd == hWnd)
        {
            if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)
                continue;
            if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
                continue;
            else if (msg.message == WM_CLOSE)
                break;
        }
        if (msg.message == WM_CLOSE && msg.hwnd == msgBoxWnd.m_hWnd)
            break;
        if (msg.message == WM_QUIT)
        {
            PostQuitMessage(msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    DestroyWindow(msgBoxWnd.m_hWnd);
    EnableWindow(hWnd, TRUE);
    return msgBoxWnd.m_retCode;
}

int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
    std::string strText, strCaption;
    tostring(lpText, -1, strText);
    tostring(lpCaption, -1, strCaption);
    return MessageBoxA(hWnd, strText.c_str(), strCaption.c_str(), uType);
}