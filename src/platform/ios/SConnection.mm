// iOS 平台 SConnection 实现：
// 消息循环基于 CFRunLoop（替代 macOS 的 NSApplication 事件泵）。
// UIKit 事件通过 SUIView 的触摸/键盘回调直接调用 OnNsEvent → postMsg，
// 因此 updateMsgQueue 只需运行 CFRunLoop 让 UIKit 分发事件即可。
// 唤醒机制使用 CFRunLoopSource，postMsg 通过 stopEventWaiting 唤醒阻塞的 runloop。

#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <unistd.h>
#include <mutex>
#include <memory>
#include <map>
#include <atomic>
#include <fontconfig/fontconfig.h>
#include <CoreText/CoreText.h>
#include "SUIWindow.h"
#include "SConnection.h"
#undef interface   // Prevent basetyps.h #define interface struct from conflicting with ObjC
#include <synhandle.h>
#include <sdc.h>
#include <wndobj.h>
#include <log.h>
#include <stdio.h>
#include "os_state.h"
#include "tostring.hpp"
#include "STrayIconMgr.h"
#include "keyboard.h"
#include "atoms.h"
#include "handle.h"

using namespace swinx;

#define kLogTag "connection"

#define float2int(x) (int)floor((x)+0.5f)

static RECT CGRectToRect(CGRect r, CGFloat scale)
{
    RECT ret;
    ret.left = float2int(r.origin.x * scale);
    ret.top = float2int(r.origin.y * scale);
    ret.right = float2int((r.origin.x + r.size.width) * scale);
    ret.bottom = float2int((r.origin.y + r.size.height) * scale);
    return ret;
}

#if defined(CAIRO_HAS_QUARTZ_FONT) && CAIRO_HAS_QUARTZ_FONT

BOOL ios_register_font(const char *utf8Path) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:utf8Path];
        if (!path) {
            SLOG_STMI() << "Invalid UTF-8 path=" << utf8Path;
            return NO;
        }
        NSURL *fontURL = [NSURL fileURLWithPath:path];
        if (!fontURL) {
            SLOG_STMI() << "create fileURL failed, path=" << utf8Path;
            return NO;
        }
        CFErrorRef error = NULL;
        BOOL success = CTFontManagerRegisterFontsForURL((__bridge CFURLRef)fontURL,
                                                     kCTFontManagerScopeProcess,
                                                     &error);
        BOOL ret = success;
        if (!success) {
            CFIndex errorCode = CFErrorGetCode(error);
            if (errorCode != kCTFontManagerErrorAlreadyRegistered) {
                CFStringRef errorDescription = CFErrorCopyDescription(error);
                if (errorDescription) {
                    SLOG_STMW() << "Failed to register font: " << utf8Path << " error="
                                << CFStringGetCStringPtr(errorDescription, kCFStringEncodingUTF8);
                    CFRelease(errorDescription);
                } else {
                    SLOG_STMW() << "Failed to register font: " << utf8Path;
                }
            } else {
                ret = YES;
            }
            CFRelease(error);
        }
        return ret;
    }
}
#endif

extern "C" BOOL WINAPI GetAppleBundlePath(char *path, int maxLen) {
    @autoreleasepool {
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath.length >= maxLen)
            return FALSE;
        [bundlePath getCString:path maxLength:maxLen encoding:NSUTF8StringEncoding];
        return TRUE;
    }
}

int SConnection::GetDisplayBounds(int displayIndex, RECT *rect)
{
    assert(rect);
    @autoreleasepool {
        NSArray<UIScreen *> *screens = [UIScreen screens];
        if (displayIndex >= [screens count])
            return -1;
        UIScreen *screen = [screens objectAtIndex:displayIndex];
        CGRect bounds = [screen bounds];
        CGFloat scale = [screen scale];
        *rect = CGRectToRect(bounds, scale);
        return 0;
    }
}

int SConnection::GetRectDisplayIndex(int x, int y, int w, int h)
{
    RECT rcSrc = {x, y, x + w, y + h};
    int rcMaxArea = 0;
    int closest = -1;

    @autoreleasepool {
        NSArray<UIScreen *> *screens = [UIScreen screens];
        for (int i = 0; i < [screens count]; ++i) {
            UIScreen *screen = [screens objectAtIndex:i];
            CGFloat scale = [screen scale];
            RECT rcDisplay = CGRectToRect([screen bounds], scale);
            RECT rcInter;
            IntersectRect(&rcInter, &rcSrc, &rcDisplay);
            int area = (rcInter.right - rcInter.left) * (rcInter.bottom - rcInter.top);
            if (area > rcMaxArea) {
                rcMaxArea = area;
                closest = i;
            }
        }
    }
    return closest;
}

SConnection::SConnection(int screenNum)
: m_bQuit(false)
, m_bBlockTimer(false)
{
    swinx::init(this);
    m_tid = GetCurrentThreadId();
    m_clipboard = new SClipboard();
    m_trayIconMgr = new STrayIconMgr();
    m_deskDC = new _SDC(0);
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));

    // 初始化 CFRunLoopSource 唤醒机制
    m_wakeSource = nullptr;
    m_wakeRunLoop = nullptr;
    @autoreleasepool {
        CFRunLoopSourceContext ctx = {0};
        ctx.perform = [](void *) {};
        CFRunLoopSourceRef source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
        CFRunLoopRef rl = CFRunLoopGetMain();
        CFRunLoopAddSource(rl, source, kCFRunLoopDefaultMode);
        m_wakeSource = (void *)source;
        m_wakeRunLoop = (void *)rl;
    }
}

SConnection::~SConnection() {
    std::unique_lock<CountMutex> lock(m_mutex);
    delete m_clipboard;
    delete m_trayIconMgr;
    for (auto it = m_msgStack.rbegin(); it != m_msgStack.rend(); it++) {
        delete *it;
    }
    m_msgStack.clear();
    if (m_msgPeek && m_bMsgNeedFree) {
        delete m_msgPeek;
        m_msgPeek = nullptr;
    }
    for (auto &msg : m_msgQueue) {
        delete msg;
    }
    m_msgQueue.clear();
    DeleteDC(m_deskDC);
    DeleteObject(m_deskBmp);
    if (m_wakeSource) {
        CFRunLoopRemoveSource((CFRunLoopRef)m_wakeRunLoop,
                              (CFRunLoopSourceRef)m_wakeSource, kCFRunLoopDefaultMode);
        CFRelease((CFRunLoopSourceRef)m_wakeSource);
        m_wakeSource = nullptr;
    }
    swinx::shutdown();
}

static bool isKeyPressed(uint16_t keyCode) {
    UINT vKey = convertKeyCodeToVK(keyCode);
    return Keyboard::instance().getKeyState(vKey) & 0x8000;
}

SHORT SConnection::GetKeyState(int vk) {
    switch (vk) {
        case VK_LBUTTON:
            return Keyboard::instance().getKeyState(VK_LBUTTON) & 0x8000;
        case VK_RBUTTON:
            return Keyboard::instance().getKeyState(VK_RBUTTON) & 0x8000;
        case VK_MBUTTON:
            return Keyboard::instance().getKeyState(VK_MBUTTON) & 0x8000;
        case VK_SHIFT:
            return (Keyboard::instance().getKeyState(VK_LSHIFT) |
                    Keyboard::instance().getKeyState(VK_RSHIFT)) & 0x8000;
        case VK_CONTROL:
            return (Keyboard::instance().getKeyState(VK_LCONTROL) |
                    Keyboard::instance().getKeyState(VK_RCONTROL)) & 0x8000;
        case VK_MENU:
            return (Keyboard::instance().getKeyState(VK_LMENU) |
                    Keyboard::instance().getKeyState(VK_RMENU)) & 0x8000;
        case VK_CAPITAL:
        case VK_LWIN:
        case VK_RWIN:
        default:
            return isKeyPressed(convertVKToKeyCode(vk)) ? 0x8000 : 0;
    }
    return 0;
}

bool SConnection::GetKeyboardState(PBYTE lpKeyState) {
    Keyboard::instance().getKeyboardState(lpKeyState);
    lpKeyState[VK_LBUTTON] = GetKeyState(VK_LBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_RBUTTON] = GetKeyState(VK_RBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_MBUTTON] = GetKeyState(VK_MBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_SHIFT] = GetKeyState(VK_SHIFT) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_CONTROL] = GetKeyState(VK_CONTROL) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_MENU] = GetKeyState(VK_MENU) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_CAPITAL] = GetKeyState(VK_CAPITAL) & 0x8000 ? 0x80 : 0;
    return TRUE;
}

SHORT SConnection::GetAsyncKeyState(int vk) {
    return GetKeyState(vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT type) const {
    UINT ret;
    switch (type) {
    case MAPVK_VK_TO_VSC_EX:
    case MAPVK_VK_TO_VSC:
        ret = convertVKToKeyCode(uCode);
        break;
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        ret = convertKeyCodeToVK(uCode);
        break;
    case MAPVK_VK_TO_CHAR: {
        UINT scanCode = convertVKToKeyCode(uCode);
        ret = scanCodeToChar(scanCode, 0);
        break;
    }
    default:
        return 0;
    }
    return ret;
}

UINT SConnection::GetDoubleClickTime() const {
    return 300;
}

LONG SConnection::GetMsgTime() const {
    if (m_msgPeek) {
        return m_msgPeek->time;
    }
    return 0;
}

DWORD SConnection::GetMsgPos() const {
    if (m_msgPeek) {
        return MAKELONG(m_msgPeek->pt.x, m_msgPeek->pt.y);
    }
    return 0;
}

DWORD SConnection::GetQueueStatus(UINT flags) {
    std::unique_lock<CountMutex> lock(m_mutex);
    if ((flags & QS_ALLINPUT) == QS_ALLINPUT) {
        return MAKELONG(m_msgQueue.size(), 0);
    }
    DWORD ret = 0;
    for (auto it : m_msgQueue) {
        switch (it->message) {
        case WM_PAINT:
            if (flags & QS_PAINT) ret = MAKELONG(0, it->message);
            break;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if (flags & QS_KEY) ret = MAKELONG(0, it->message);
            break;
        case WM_MOUSEMOVE:
        case WM_MOUSEHOVER:
        case WM_MOUSELEAVE:
            if (flags & QS_MOUSEMOVE) ret = MAKELONG(0, it->message);
            break;
        case WM_TIMER:
            if (flags & QS_TIMER) ret = MAKELONG(0, it->message);
            break;
        default:
            if (it->message >= WM_LBUTTONDOWN && it->message <= WM_XBUTTONDBLCLK && (flags & QS_MOUSEBUTTON))
                ret = MAKELONG(0, it->message);
            if (it->msgReply) {
                if (it->msgReply->GetType() == MT_POST && flags & QS_POSTMESSAGE)
                    ret = MAKELONG(0, it->message);
                else if (it->msgReply->GetType() == MT_SEND && flags & QS_SENDMESSAGE)
                    ret = MAKELONG(0, it->message);
            }
            if (flags & QS_ALLPOSTMESSAGE)
                ret = MAKELONG(0, it->message);
            break;
        }
        if (ret != 0)
            break;
    }
    return ret;
}

bool SConnection::waitMsg(UINT timeOut) {
    //SLOG_STMI()<<"waitMsg enter, timeOut="<<timeOut;
    if (!m_bBlockTimer) {
        std::unique_lock<CountMutex> lock(m_mutex);
        for (auto &it : m_lstTimer) {
            timeOut = std::min(timeOut, it.fireRemain);
        }
    }
    updateMsgQueue(timeOut);
    std::unique_lock<CountMutex> lock(m_mutex);
    return !m_msgQueue.empty();
}

void SConnection::postMsg(Msg *pMsg) {
    std::unique_lock<CountMutex> lock(m_mutex);
    m_msgQueue.push_back(pMsg);
    stopEventWaiting();
}

void SConnection::stopEventWaiting() {
    if (m_wakeSource && m_wakeRunLoop) {
        CFRunLoopSourceSignal((CFRunLoopSourceRef)m_wakeSource);
        CFRunLoopWakeUp((CFRunLoopRef)m_wakeRunLoop);
    }
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD to, bool fWaitAll, DWORD dwWaitMask) {
    if (nCount == 0 && (dwWaitMask & QS_ALLINPUT) == QS_ALLINPUT) {
        return waitMsg(to) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    HANDLE tmpHandles[MAXIMUM_WAIT_OBJECTS] = {0};
    int fds[MAXIMUM_WAIT_OBJECTS] = {0};
    bool states[MAXIMUM_WAIT_OBJECTS] = {false};
    int nSignals = 0;
    for (int i = 0; i < nCount; i++) {
        tmpHandles[i] = AddHandleRef(handles[i]);
        fds[i] = GetSynHandle(tmpHandles[i])->getReadFd();
    }
    DWORD timeout = to;
    if (!m_bBlockTimer) {
        std::unique_lock<CountMutex> lock(m_mutex);
        for (auto &it : m_lstTimer) {
            timeout = std::min(timeout, it.fireRemain);
        }
    }
    BOOL bWakeByMsg = FALSE;
    @autoreleasepool {
        NSMutableArray *fdSources = [NSMutableArray array];
        __block int signaledFd = -1;
        for (int i = 0; i < nCount; i++) {
            int fdIdx = i;
            // 使用主队列：事件回调与下方 do-while 循环同在主线程，
            // 避免使用 std::atomic（C++11 下无法在 block 中按值捕获）。
            dispatch_source_t source = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_READ, fds[i], 0,
                dispatch_get_main_queue());
            dispatch_source_set_event_handler(source, ^{
                signaledFd = fdIdx;
                stopEventWaiting();
            });
            dispatch_resume(source);
            [fdSources addObject:source];
        }
        DWORD startTick = GetTickCount();
        do {
            CFTimeInterval tf;
            if (timeout == 0)
                tf = 0.0;
            else if (timeout == INFINITE)
                tf = 1e10;
            else
                tf = timeout / 1000.0;
        SInt32 rlResult = CFRunLoopRunInMode(kCFRunLoopDefaultMode, tf, true);
 
            int ifd = signaledFd;
            signaledFd = -1;
            if (ifd >= 0) {
                states[ifd] = true;
                nSignals++;
                if (nSignals == nCount || !fWaitAll)
                    break;
            }
            if (dwWaitMask != 0) {
                std::unique_lock<CountMutex> lock(m_mutex);
                if (!m_msgQueue.empty()) {
                    bWakeByMsg = TRUE;
                    break;
                }
            }
            if (timeout != INFINITE) {
                DWORD elapsed = GetTickCount() - startTick;
                if (elapsed >= timeout)
                    break;
            }
        } while (true);
        for (int i = 0; i < fdSources.count; i++) {
            dispatch_source_t source = [fdSources objectAtIndex:i];
            dispatch_source_cancel(source);
        }
    }

    int ret = bWakeByMsg ? (WAIT_OBJECT_0 + nCount) : WAIT_TIMEOUT;

    for (int i = 0; nSignals && i < nCount; i++) {
        if (GetSynHandle(tmpHandles[i])->onWaitDone()) {
            if (ret == WAIT_TIMEOUT)
                ret = WAIT_OBJECT_0 + i;
            nSignals--;
        }
    }
    for (int i = 0; i < nCount; i++) {
        CloseHandle(tmpHandles[i]);
    }
    if (m_bQuit)
        return WAIT_FAILED;
    if (ret == WAIT_TIMEOUT || ret == WAIT_OBJECT_0 + nCount) {
        updateMsgQueue(0);
        if (dwWaitMask != 0) {
            if (GetQueueStatus(dwWaitMask))
                ret = WAIT_OBJECT_0 + nCount;
        }
    }
    return ret;
}

bool SConnection::TranslateMessage(const MSG *pMsg) {
    if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN) {
        // iOS 上 scanCodeToChar 始终返回 0，TranslateMessage 不产生 WM_CHAR。
        // 字符输入由两条路径产生：
        //   1) 硬件键盘：SUIView.pressesBegan 利用 key.characters 直接发 WM_CHAR/WM_IME_CHAR
        //   2) 软键盘：SUIView.insertText / deleteBackward 直接发 WM_CHAR/WM_IME_CHAR
        // 此处保留与 cocoa 相同的框架代码但不产生消息，避免重复投递。
        uint32_t modifiers = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= 0x01;
        if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= 0x02;
        if (GetKeyState(VK_MENU) & 0x8000) modifiers |= 0x04;
        wchar_t c = scanCodeToChar(HIWORD(pMsg->lParam), modifiers);
        if (c != 0 && !GetKeyState(VK_CONTROL) && !GetKeyState(VK_MENU) && !GetKeyState(VK_LWIN)) {
            std::unique_lock<CountMutex> lock(m_mutex);
            Msg *msg = new Msg;
            if (c < 127)
                msg->message = pMsg->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
            else
                msg->message = WM_IME_CHAR;
            msg->hwnd = pMsg->hwnd;
            msg->wParam = c;
            msg->lParam = pMsg->lParam;
            GetCursorPos(&msg->pt);
            postMsg(msg);
            return TRUE;
        }
    }
    return FALSE;
}

void SConnection::updateMsgQueue(DWORD dwTimeout) {
    uint64_t ts = GetTickCount64();
    UINT elapse = m_tsLastMsg == -1 ? 0 : (ts - m_tsLastMsg);
    m_tsLastMsg = ts;
    if (!m_bBlockTimer) {
        std::unique_lock<CountMutex> lock(m_mutex);
        static const size_t kMaxDalayMsg = 5;
        int elapse2 = elapse + std::min(m_msgQueue.size(), kMaxDalayMsg);
        POINT pt;
        GetCursorPos(&pt);
        for (auto &it : m_lstTimer) {
            if (it.fireRemain <= elapse2) {
                Msg *pMsg = new Msg;
                pMsg->hwnd = it.hWnd;
                pMsg->message = WM_TIMER;
                pMsg->wParam = it.id;
                pMsg->lParam = (LPARAM)it.proc;
                pMsg->time = ts;
                pMsg->pt = pt;
                m_msgQueue.push_back(pMsg);
                it.fireRemain = it.elapse;
                dwTimeout = 0;
            } else {
                it.fireRemain -= elapse;
            }
        }
    }
    @autoreleasepool {
        CFTimeInterval tf;
        if (dwTimeout == 0)
            tf = 0.0;
        else if (dwTimeout == INFINITE)
            tf = 1e10;
        else
            tf = dwTimeout / 1000.0;
        SInt32 rlResult = CFRunLoopRunInMode(kCFRunLoopDefaultMode, tf, true);
        //SLOG_STMI()<<"updateMsgQueue CFRunLoopRunInMode, dwTimeout="<<dwTimeout<<" result="<rlResult;
    }
}

bool SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    updateMsgQueue(0);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    {
        auto it = m_lstCallbackTask.begin();
        while (it != m_lstCallbackTask.end()) {
            auto cur = it++;
            if ((*cur)->TestEvent()) {
                (*cur)->Release();
                m_lstCallbackTask.erase(cur);
            }
        }
    }
    auto it = m_msgQueue.begin();
    while (it != m_msgQueue.end()) {
        bool bMatch = TRUE;
        Msg *msg = (*it);
        do {
            if (msg->message == WM_QUIT) break;
            if (msg->message == WM_TIMER && msg->lParam == 0) break;
            if (msg->hwnd != hWnd && hWnd != 0) { bMatch = FALSE; break; }
            if (wMsgFilterMin == 0 && wMsgFilterMax == 0) break;
            if (wMsgFilterMin <= msg->message && wMsgFilterMax >= msg->message) break;
            bMatch = FALSE;
        } while (false);
        if (msg->hwnd) {
            WndObj wndObj = WndMgr::fromHwnd(msg->hwnd);
            if (!wndObj || wndObj->bDestroyed) {
                it = m_msgQueue.erase(it);
                delete msg;
                continue;
            }
        }
        if (bMatch) break;
        it++;
    }
    if (it != m_msgQueue.end()) {
        Msg *msg = (*it);
        if (m_msgPeek && m_bMsgNeedFree) {
            delete m_msgPeek;
            m_msgPeek = nullptr;
            m_bMsgNeedFree = false;
        }
        if (msg->message == WM_TIMER && msg->lParam) {
            TIMERPROC proc = (TIMERPROC)msg->lParam;
            m_msgQueue.erase(it);
            LONG lockCount = m_mutex.FreeLock();
            proc(msg->hwnd, WM_TIMER, msg->wParam, msg->time);
            m_mutex.RestoreLock(lockCount);
            delete msg;
            return peekMsg(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
        }
        m_msgPeek = msg;
        if (wRemoveMsg == PM_NOREMOVE) {
            m_bMsgNeedFree = false;
        } else {
            m_msgQueue.erase(it);
            m_bMsgNeedFree = true;
        }
        memcpy(pMsg, (MSG *)m_msgPeek, sizeof(MSG));
        return TRUE;
    }
    return FALSE;
}

bool SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    SLOG_STMI()<<"getMsg enter, hWnd="<<hWnd<<" min="<<wMsgFilterMin<<" max="<<wMsgFilterMax;
    bool bRet = FALSE;
    for (; !bRet && !m_bQuit;) {
        bRet = peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE);
        if (!bRet) {
            waitMsg();
        } else if (lpMsg->message == WM_QUIT) {
            bRet = FALSE;
            break;
        }
    }
    return bRet;
}

void SConnection::postMsg(HWND hWnd, UINT message, WPARAM wp, LPARAM lp) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    Msg *pMsg = new Msg;
    pMsg->hwnd = hWnd;
    pMsg->message = message;
    pMsg->wParam = wp;
    pMsg->lParam = lp;
    m_msgQueue.push_back(pMsg);
    stopEventWaiting();
}

void SConnection::postMsg2(bool bWideChar, HWND hWnd, UINT message, WPARAM wp, LPARAM lp, MsgReply *reply) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!bWideChar) {
        Msg *pMsg = new Msg(reply);
        pMsg->hwnd = hWnd;
        pMsg->message = message;
        pMsg->wParam = wp;
        pMsg->lParam = lp;
        GetCursorPos(&pMsg->pt);
        postMsg(pMsg);
    } else {
        MsgW2A *pMsg = new MsgW2A(reply);
        pMsg->orgMsg.message = message;
        pMsg->orgMsg.wParam = wp;
        pMsg->orgMsg.lParam = lp;
        pMsg->hwnd = hWnd;
        GetCursorPos(&pMsg->pt);
        postMsg(pMsg);
    }
}

UINT_PTR SConnection::SetTimer(HWND hWnd, UINT_PTR id, UINT uElapse, TIMERPROC proc) {
    UINT ret = 0;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (hWnd) {
        for (auto &it : m_lstTimer) {
            if (it.hWnd != hWnd) continue;
            if (it.id == id) {
                it.fireRemain = uElapse;
                it.proc = proc;
                return id;
            }
        }
        TimerInfo timer;
        timer.id = id;
        timer.fireRemain = uElapse;
        timer.hWnd = hWnd;
        timer.proc = proc;
        timer.elapse = uElapse;
        m_lstTimer.push_back(timer);
        ret = id;
    } else {
        UINT_PTR newId = 0;
        for (auto &it : m_lstTimer) {
            if (it.hWnd) continue;
            newId = std::max(it.id, newId);
        }
        TimerInfo timer;
        timer.id = newId + 1;
        timer.fireRemain = uElapse;
        timer.hWnd = 0;
        timer.proc = proc;
        timer.elapse = uElapse;
        m_lstTimer.push_back(timer);
        ret = timer.id;
    }
    if (GetCurrentThreadId() != m_tid) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            stopEventWaiting();
        });
    } else {
        stopEventWaiting();
    }
    return ret;
}

bool SConnection::KillTimer(HWND hWnd, UINT_PTR id) {
    bool bRet = FALSE;
    std::unique_lock<CountMutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); it++) {
        if (it->hWnd == hWnd && it->id == id) {
            m_lstTimer.erase(it);
            bRet = TRUE;
            break;
        }
    }
    if (bRet) {
        auto it = m_msgQueue.begin();
        while (it != m_msgQueue.end()) {
            auto it2 = it++;
            Msg *msg = *it2;
            if (msg->hwnd == hWnd && msg->message == WM_TIMER && msg->wParam == id) {
                m_msgQueue.erase(it2);
                delete msg;
            }
        }
    }
    return bRet;
}

HDC SConnection::GetDC() {
    return m_deskDC;
}

bool SConnection::ReleaseDC(HDC hdc) {
    return TRUE;
}

HWND SConnection::SetCapture(HWND hCapture) {
    HWND hRet = m_hWndCapture;
    if (!setUiWindowCapture(hCapture))
        return hRet;
    m_hWndCapture = hCapture;
    return hRet;
}

bool SConnection::ReleaseCapture() {
    if (!m_hWndCapture)
        return FALSE;
    if (!releaseUiWindowCapture(m_hWndCapture))
        return FALSE;
    m_hWndCapture = NULL;
    return TRUE;
}

HWND SConnection::GetCapture() const {
    return m_hWndCapture;
}

HCURSOR SConnection::SetCursor(HWND hWnd, HCURSOR cursor) {
    if (!hWnd) {
        if (!m_msgStack.empty()) {
            hWnd = m_msgStack.back()->hwnd;
        } else {
            hWnd = m_hActive;
        }
    }
    if (!hWnd)
        return cursor;
    auto it = m_wndCursor.find(hWnd);
    HCURSOR ret = 0;
    if (it != m_wndCursor.end()) {
        ret = it->second;
    }
    setUiWindowCursor(hWnd, cursor);
    m_wndCursor[hWnd] = cursor;
    return ret;
}

HCURSOR SConnection::GetCursor() {
    HWND hWnd = GetActiveWnd();
    auto it = m_wndCursor.find(hWnd);
    if (it == m_wndCursor.end())
        return 0;
    return it->second;
}

bool SConnection::DestroyCursor(HCURSOR cursor) {
    for (auto &it : m_wndCursor) {
        if (it.second == cursor)
            return FALSE;
    }
    return TRUE;
}

void SConnection::SetTimerBlock(bool bBlock) {
    m_bBlockTimer = bBlock;
}

HWND SConnection::GetActiveWnd() const {
    return m_hActive;
}

bool SConnection::SetActiveWindow(HWND hWnd) {
    return setUiActiveWindow(hWnd);
}

HWND SConnection::WindowFromPoint(POINT pt, HWND hWnd) const {
    return hwndFromPoint(hWnd, pt);
}

bool SConnection::IsWindow(HWND hWnd) const {
    return IsUiWindow(hWnd);
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const {
    if (!IsWindow(hWnd))
        return;
    setUiWindowPos(hWnd, x, y);
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const {
    if (!IsWindow(hWnd))
        return;
    setUiWindowSize(hWnd, cx, cy);
}

bool SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const {
    if (!IsWindow(hWnd))
        return FALSE;
    SetWindowPos(hWnd, x, y);
    SetWindowSize(hWnd, cx, cy);
    return TRUE;
}

bool SConnection::GetCursorPos(LPPOINT ppt) const {
    if(!ppt)
        return false;
    *ppt = m_cursorPos;
    return true;
}

int SConnection::GetDpi(bool bx) const {
    return getUiDpi(bx);
}

void SConnection::KillWindowTimer(HWND hWnd) {
    std::unique_lock<CountMutex> lock(m_mutex);
    auto it = m_lstTimer.begin();
    while (it != m_lstTimer.end()) {
        auto cur = it++;
        if (cur->hWnd == hWnd) {
            m_lstTimer.erase(cur);
        }
    }
}

HWND SConnection::GetForegroundWindow() {
    HWND hret = m_hForeground;
    if (!IsUiWindow(hret)) {
        hret = getUiForegroundWindow();
    }
    return hret;
}

bool SConnection::SetForegroundWindow(HWND hWnd) {
    bool ret = setUiForegroundWindow(hWnd);
    if (ret) {
        m_hForeground = hWnd;
    }
    return ret;
}

bool SConnection::BringWindowToTop(HWND hWnd) {
    return setUiWindowToTop(hWnd);
}

bool SConnection::SetWindowOpacity(HWND hWnd, BYTE byAlpha) {
    return setUiWindowAlpha(hWnd, byAlpha);
}

bool SConnection::SetWindowRgn(HWND hWnd, HRGN hRgn) {
    if (hRgn) {
        DWORD len = GetRegionData(hRgn, 0, nullptr);
        if (!len)
            return FALSE;
        RGNDATA *pData = (RGNDATA *)malloc(len);
        GetRegionData(hRgn, len, pData);
        const LPRECT prc = (LPRECT)pData->Buffer;
        bool ret = setUiWindowRgn(hWnd, prc, pData->rdh.nCount);
        free(pData);
        return ret;
    } else {
        return setUiWindowRgn(hWnd, nullptr, 0);
    }
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl) {
    return NULL;
}

HBITMAP SConnection::GetDesktopBitmap() {
    return m_deskBmp;
}

HWND SConnection::GetFocus() const {
    return m_hFocus;
}

bool SConnection::SetFocus(HWND hWnd) {
    if (hWnd == m_hFocus)
        return TRUE;
    if (hWnd) {
        setUiFocusWindow(hWnd);
    }
    HWND hOldFocus = m_hFocus;
    if (hOldFocus) {
        SendMessage(hOldFocus, WM_KILLFOCUS, (WPARAM)hWnd, 0);
    }
    if (hWnd) {
        SendMessage(hWnd, WM_SETFOCUS, (WPARAM)hOldFocus, 0);
    }
    m_hFocus = hWnd;
    SLOG_STMI() << "setFocus hWnd=" << hWnd << " hOldFocus=" << hOldFocus;
    return TRUE;
}

bool SConnection::IsDropTarget(HWND hWnd) {
    return isUiDropTarget(hWnd);
}

bool SConnection::FlashWindowEx(PFLASHWINFO info) {
    return flashUiWindow(info->hwnd, info->dwFlags, info->uCount, info->dwTimeout);
}

int SConnection::OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj) {
        return GetAtomNameA(wndObj->clsAtom, lpClassName, nMaxCount);
    }
    return 0;
}

bool SConnection::OnSetWindowText(HWND hWnd, _Window *wndObj, LPCSTR lpszString) {
    wndObj->title = lpszString ? lpszString : "";
    return TRUE;
}

int SConnection::OnGetWindowTextLengthA(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj) {
        return wndObj->title.length();
    }
    return 0;
}

int SConnection::OnGetWindowTextLengthW(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj) {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
    }
    return 0;
}

int SConnection::OnGetWindowTextA(HWND hWnd, char *buf, int bufLen) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj) {
        if (bufLen < (int)wndObj->title.length()) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        strcpy(buf, wndObj->title.c_str());
        return wndObj->title.length();
    }
    return 0;
}

int SConnection::OnGetWindowTextW(HWND hWnd, wchar_t *buf, int bufLen) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj) {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), buf, bufLen);
    }
    return 0;
}

HWND SConnection::OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName) {
    return NULL;
}

bool SConnection::OnEnumWindows(HWND hParent, HWND hChildAfter, WNDENUMPROC lpEnumFunc, LPARAM lParam) {
    @autoreleasepool {
        if (!hParent) {
            for (UIWindow *window in [UIApplication sharedApplication].windows) {
                for (UIView *v in window.subviews) {
                    HWND hWnd = getHwndFromUiView((__bridge void *)v);
                    if (!hWnd) continue;
                    if (!lpEnumFunc(hWnd, lParam))
                        break;
                }
            }
            return TRUE;
        } else {
            // HWND 即 SUIView 的桥接指针，直接还原为 UIView 枚举子视图
            UIView *parent = (__bridge UIView *)(void *)hParent;
            if (![parent isKindOfClass:[UIView class]]) return FALSE;
            for (UIView *v in parent.subviews) {
                HWND hWnd = getHwndFromUiView((__bridge void *)v);
                if (!hWnd) continue;
                if (!lpEnumFunc(hWnd, lParam))
                    break;
            }
            return TRUE;
        }
    }
}

HWND SConnection::_GetRoot(HWND hWnd) {
    HWND hRoot = hWnd;
    HWND hParent = _GetParent(hWnd);
    while (hParent) {
        hRoot = hParent;
        hParent = _GetParent(hParent);
    }
    return hRoot;
}

HWND SConnection::_GetParent(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return NULL;
    if (wndObj->dwStyle & WS_CHILD)
        return wndObj->parent;
    return NULL;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags) {
    switch (gaFlags) {
    case GA_PARENT:
        return _GetParent(hwnd);
    case GA_ROOT:
        return _GetRoot(hwnd);
    case GA_ROOTOWNER: {
        HWND ret = _GetRoot(hwnd);
        HWND hOwner = GetParent(ret);
        if (hOwner)
            ret = hOwner;
        return ret;
    }
    default:
        return 0;
    }
}

HMONITOR SConnection::MonitorFromWindow(HWND hWnd, DWORD dwFlags) {
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

HMONITOR SConnection::MonitorFromPoint(POINT pt, DWORD dwFlags) {
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

HMONITOR SConnection::MonitorFromRect(LPCRECT lprc, DWORD dwFlags) {
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

int SConnection::GetScreenWidth(HMONITOR hMonitor) const {
    @autoreleasepool {
        UIScreen *screen = (__bridge UIScreen *)hMonitor;
        if (!screen) screen = [UIScreen mainScreen];
        return [screen bounds].size.width * [screen scale];
    }
}

int SConnection::GetScreenHeight(HMONITOR hMonitor) const {
    @autoreleasepool {
        UIScreen *screen = (__bridge UIScreen *)hMonitor;
        if (!screen) screen = [UIScreen mainScreen];
        return [screen bounds].size.height * [screen scale];
    }
}

HWND SConnection::GetScreenWindow() const {
    return NULL;
}

void SConnection::UpdateWindowIcon(HWND hWnd, _Window *wndObj) {
    if (wndObj->iconSmall) {
        setUiWindowIcon(hWnd, wndObj->iconSmall, FALSE);
    }
    if (wndObj->iconBig) {
        setUiWindowIcon(hWnd, wndObj->iconBig, TRUE);
    }
}

uint32_t SConnection::GetVisualID(bool bScreen) const {
    return 0;
}

uint32_t SConnection::GetCmap() const {
    return 0;
}

void SConnection::SetZOrder(HWND hWnd, _Window *wndObj, HWND hWndInsertAfter) {
    setUiWindowZorder(hWnd, hWndInsertAfter);
}

void SConnection::OnStyleChanged(HWND hWnd, _Window *wndObj, DWORD oldStyle, DWORD newStyle) {
    if ((oldStyle & WS_DISABLED) != (newStyle & WS_DISABLED)) {
        enableUiWindow(hWnd, (newStyle & WS_DISABLED) == 0);
    }
}

void SConnection::OnExStyleChanged(HWND hWnd, _Window *wndObj, DWORD oldStyle, DWORD newStyle) {
    if (GetParent(hWnd))
        return;
    if (newStyle & WS_EX_TOPMOST) {
        SetZOrder(hWnd, wndObj, HWND_TOPMOST);
    } else {
        SetZOrder(hWnd, wndObj, HWND_NOTOPMOST);
    }
    if ((oldStyle & WS_EX_TOOLWINDOW) != (newStyle & WS_EX_TOOLWINDOW)) {
        setUiWindowToolWindow(hWnd, newStyle & WS_EX_TOOLWINDOW);
    }
}

void SConnection::SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len) {
}

uint32_t SConnection::GetIpcAtom() const {
    return 0;
}

void SConnection::postCallbackTask(CbTask *pTask) {
    std::unique_lock<CountMutex> lock(m_mutex);
    m_lstCallbackTask.push_back(pTask);
    pTask->AddRef();
}

DWORD SConnection::GetWndProcessId(HWND hWnd) {
    return getpid();
}

HWND SConnection::WindowFromPoint(POINT pt) {
    return hwndFromPoint(NULL, pt);
}

bool SConnection::GetClientRect(HWND hWnd, RECT *pRc) {
    return getUiWindowRect(hWnd, pRc);
}

void SConnection::SendSysCommand(HWND hWnd, int nCmd) {
    sendUiSysCommand(hWnd, nCmd);
}

bool SConnection::IsWindowVisible(HWND hWnd) {
    return isUiWindowVisible(hWnd);
}

HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd) {
    if (uCmd == GW_OWNER) {
        if (!wndObj) {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        return wndObj->owner;
    }
    return getUiWindow(hWnd, uCmd);
}

UINT SConnection::RegisterMessage(LPCSTR lpString) {
    return SAtoms::registerAtom(lpString) + WM_USER + 100000;
}

bool SConnection::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData) {
    return GetTrayIconMgr()->NotifyIcon(dwMessage, lpData);
}

HMONITOR SConnection::GetScreen(DWORD dwFlags) const {
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

bool SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight) {
    DestroyCaret();
    m_caretInfo.hOwner = hWnd;
    if (hBitmap == (HBITMAP)1)
        hBitmap = nullptr;
    m_caretInfo.hBmp = RefGdiObj(hBitmap);
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

bool SConnection::DestroyCaret() {
    if (m_caretInfo.nVisible > 0) {
        KillTimer(m_caretInfo.hOwner, TM_CARET);
    }
    DeleteObject(m_caretInfo.hBmp);
    m_caretInfo.hBmp = NULL;
    m_caretInfo.nHeight = 0;
    m_caretInfo.nWidth = 0;
    m_caretInfo.hOwner = 0;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

bool SConnection::ShowCaret(HWND hWnd) {
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible++;
    if (m_caretInfo.nVisible == 1) {
        SetTimer(hWnd, TM_CARET, m_caretBlinkTime, NULL);
    }
    return TRUE;
}

bool SConnection::HideCaret(HWND hWnd) {
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible--;
    if (m_caretInfo.nVisible == 0) {
        KillTimer(hWnd, TM_CARET);
    }
    return TRUE;
}

bool SConnection::SetCaretPos(int X, int Y) {
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    return TRUE;
}

bool SConnection::GetCaretPos(LPPOINT lpPoint) {
    if (!lpPoint)
        return FALSE;
    lpPoint->x = m_caretInfo.x;
    lpPoint->y = m_caretInfo.y;
    return TRUE;
}

const SConnection::CaretInfo *SConnection::GetCaretInfo() const {
    return &m_caretInfo;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime) {
    m_caretBlinkTime = blinkTime;
}

UINT SConnection::GetCaretBlinkTime() const {
    return m_caretBlinkTime;
}

void SConnection::GetWorkArea(HMONITOR hMonitor, RECT *prc) {
    @autoreleasepool {
        UIScreen *screen = (__bridge UIScreen *)hMonitor;
        if (!screen) screen = [UIScreen mainScreen];
        CGRect bounds = [screen bounds];
        CGFloat scale = [screen scale];
        // safeAreaInsets 是 UIView 的属性，UIScreen 没有；从 keyWindow 获取。
        UIEdgeInsets insets = UIEdgeInsetsZero;
        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if (keyWindow) {
            insets = keyWindow.safeAreaInsets;
        }
        prc->left = (bounds.origin.x + insets.left) * scale;
        prc->top = (bounds.origin.y + insets.top) * scale;
        prc->right = (bounds.origin.x + bounds.size.width - insets.right) * scale;
        prc->bottom = (bounds.origin.y + bounds.size.height - insets.bottom) * scale;
    }
}

SClipboard *SConnection::getClipboard() {
    return m_clipboard;
}

bool SConnection::EmptyClipboard() {
    return m_clipboard->emptyClipboard();
}

bool SConnection::IsClipboardFormatAvailable(UINT format) {
    return m_clipboard->hasFormat(format);
}

bool SConnection::OpenClipboard(HWND hWndNewOwner) {
    return m_clipboard->openClipboard(hWndNewOwner);
}

bool SConnection::CloseClipboard() {
    return m_clipboard->closeClipboard();
}

HWND SConnection::GetClipboardOwner() {
    return m_clipboard->getClipboardOwner();
}

HANDLE SConnection::GetClipboardData(UINT uFormat) {
    return m_clipboard->getClipboardData(uFormat);
}

HANDLE SConnection::SetClipboardData(UINT uFormat, HANDLE hMem) {
    return m_clipboard->setClipboardData(uFormat, hMem);
}

UINT SConnection::RegisterClipboardFormatA(LPCSTR pszName) {
    return SClipboard::RegisterClipboardFormatA(pszName);
}

STrayIconMgr *SConnection::GetTrayIconMgr() {
    return m_trayIconMgr;
}

void SConnection::EnableDragDrop(HWND hWnd, bool enable) {
    setUiDropTarget(hWnd, enable);
}

HRESULT SConnection::DoDragDrop(IDataObject *pDataObject,
                                IDropSource *pDropSource,
                                DWORD dwOKEffect,
                                DWORD *pdwEffect) {
    return doUiDragDrop(pDataObject, pDropSource, dwOKEffect, pdwEffect);
}

HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth) {
    return createUiWindow(cs->hwndParent, cs->style, cs->dwExStyle, wnd->bAutoDblClick, cs->lpszName, cs->x, cs->y, cs->cx, cs->cy, this);
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *wnd) {
    closeUiWindow(hWnd);
    if (hWnd == m_hWndCapture)
        m_hWndCapture = 0;
    if (m_hFocus == hWnd)
        m_hFocus = 0;
    if (m_hForeground == hWnd)
        m_hForeground = 0;
    if (m_hActive == hWnd) {
        m_hActive = 0;
        HWND hkeyWindow = findUiKeyWindow();
        if (hkeyWindow) {
            setUiActiveWindow(hkeyWindow);
        }
    }
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, bool bVisible, int nCmdShow) {
    assert(wndObj);
    if (bVisible) {
        if (0 == (wndObj->dwStyle & WS_CHILD)) {
            ReleaseCapture();
        }
        showUiWindow(hWnd, SW_SHOW);
        wndObj->dwStyle |= WS_VISIBLE;
        InvalidateRect(hWnd, nullptr, TRUE);
        if (nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA && !(wndObj->dwStyle & WS_CHILD) && wndObj->mConnection->GetActiveWnd() == 0)
            SetActiveWindow(hWnd);
    } else {
        showUiWindow(hWnd, SW_HIDE);
        wndObj->dwStyle &= ~WS_VISIBLE;
    }
}

void SConnection::SetParent(HWND hWnd, _Window *wnd, HWND parent) {
    setUiParent(hWnd, parent);
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc) {
    invalidateUiWindow(hWnd, rc);
}

void SConnection::SetWindowMsgTransparent(HWND hWnd, _Window *wndObj, bool bTransparent) {
    setUiMsgTransparent(hWnd, bTransparent);
}

void SConnection::AssociateHIMC(HWND hWnd, _Window *wndObj, HIMC hIMC) {
    enableUiWindowIme(hWnd, hIMC != 0);
}

void SConnection::flush() {
}

void SConnection::sync() {
}

bool SConnection::IsScreenComposited() const {
    return TRUE;
}

//---------------------------------------------------------
SConnMgr *SConnMgr::instance() {
    static SConnMgr inst;
    return &inst;
}

SConnection *SConnMgr::getConnection(tid_t tid_, int screenNum) {
    if (tid_ != 0 && tid_ != m_tid) {
        return NULL;
    }
    return m_conn;
}

static bool init_fontconfig_with_custom_dirs(const char **custom_dirs, int dir_count) {
    if (!FcInit()) {
        SLOG_STMW() << "Failed to initialize Fontconfig";
        return FALSE;
    }
    FcConfig *config = FcConfigGetCurrent();
    if (!config) {
        SLOG_STMW() << "Failed to get current Fontconfig configuration";
        return FALSE;
    }
    for (int i = 0; i < dir_count; i++) {
        if (!FcConfigAppFontAddDir(config, (const FcChar8 *)custom_dirs[i])) {
            SLOG_STMW() << "Warning: Failed to add font directory: " << custom_dirs[i];
        }
    }
    char szCurDir[1024];
    getcwd(szCurDir, sizeof(szCurDir));
    strcat(szCurDir, "/fonts");
    if (!FcConfigAppFontAddDir(config, (const FcChar8 *)szCurDir)) {
        SLOG_STMW() << "Warning: Failed to add font directory: " << szCurDir;
    }
    return TRUE;
}

class InitFontConfig {
public:
    InitFontConfig() {
        const char *ios_font_dirs[] = {
            "/System/Library/Fonts",
            "/System/Library/Fonts/Cache",
            "/System/Library/Fonts/Supplemental",
            "/var/mobile/Library/Fonts",
        };
        bool ret = init_fontconfig_with_custom_dirs(ios_font_dirs, ARRAYSIZE(ios_font_dirs));
        if (!ret) {
            SLOG_STMW() << "Failed to initialize FontConfig";
        }
    }
};
static InitFontConfig g_initFontConfig;

SConnMgr::SConnMgr() {
    m_tid = GetCurrentThreadId();
    m_hHeap = HeapCreate(0, 0, 0);
    m_conn = new SConnection(0);
}

SConnMgr::~SConnMgr() {
    delete m_conn;
    m_conn = NULL;
    CloseHandle(m_hHeap);
}

void SConnection::onTerminate() {
    PostQuitMessage(0);
}

void SConnection::OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    //SLOG_STMI()<<"OnNsEvent enter, hWnd="<<hWnd<<" message=0x"<<std::hex<<message<<" wParam="<<wParam<<" lParam="<<lParam<<std::dec;
    // 追踪鼠标按键状态（iOS 无 NSEvent pressedMouseButtons）
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        Keyboard::instance().setKeyState(VK_LBUTTON, 0x80);
        break;
    case WM_LBUTTONUP:
        Keyboard::instance().setKeyState(VK_LBUTTON, 0);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        Keyboard::instance().setKeyState(VK_RBUTTON, 0x80);
        break;
    case WM_RBUTTONUP:
        Keyboard::instance().setKeyState(VK_RBUTTON, 0);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        Keyboard::instance().setKeyState(VK_MBUTTON, 0x80);
        break;
    case WM_MBUTTONUP:
        Keyboard::instance().setKeyState(VK_MBUTTON, 0);
        break;
    }
    //todo:hjx considering replace postMsg with sendMsg
    Msg *pMsg = new Msg;
    pMsg->hwnd = hWnd;
    pMsg->message = message;
    pMsg->wParam = wParam;
    pMsg->lParam = lParam;
    postMsg(pMsg);
}

void SConnection::OnNsActive(HWND hWnd, BOOL bActive) {
    SLOG_STMI()<<"OnNsActive enter, hWnd="<<hWnd<<" bActive="<<bActive;
    if (!bActive) {
        if (m_hFocus) {
            if (hWnd == m_hFocus || IsChild(hWnd, m_hFocus)) {
                SetFocus(0);
            }
        }
    }
    if (bActive)
        m_hActive = hWnd;
    SendMessageA(hWnd, WM_ACTIVATE, bActive ? 1 : 0, 0);
}

void SConnection::OnDrawRect(HWND hWnd, const RECT &rc, CGContextRef ctx) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (!wndObj)
        return;
    assert(wndObj->hdc);
    CGContextRef oldCtx = wndObj->hdc->cgCtx;
    BOOL oldOwned = wndObj->hdc->cgCtxOwned;
    wndObj->hdc->cgCtx = ctx;
    wndObj->hdc->cgCtxOwned = NO; // window system 传入的 ctx 非拥有引用，不要 Release
    SetRectRgn(wndObj->invalid.hRgn, rc.left, rc.top, rc.right, rc.bottom);
    // 先擦除背景（与 macOS 适配一致），确保 Core Graphics 绘制上下文有正确的背景基础
    SendMessageA(hWnd, WM_ERASEBKGND, (WPARAM)wndObj->hdc, 0);
    SendMessageA(hWnd, WM_PAINT, 0, (LPARAM)wndObj->invalid.hRgn);
    wndObj->hdc->cgCtx = oldCtx;
    wndObj->hdc->cgCtxOwned = oldOwned;
    SetRectRgn(wndObj->invalid.hRgn, 0, 0, 0, 0);
}

void SConnection::updateWindow(HWND hWnd, const RECT &rc) {
    updateUiWindow(hWnd, rc);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc) {
    updateUiWindow(hWnd, rc);
}

void SConnection::BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (m_msgPeek && m_bMsgNeedFree && m_msgPeek->hwnd == hWnd && m_msgPeek->message == msg && m_msgPeek->wParam == wp && m_msgPeek->lParam == lp) {
        m_msgStack.push_back(m_msgPeek);
        m_msgPeek = nullptr;
        m_bMsgNeedFree = false;
    }
}

void SConnection::AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res) {
    if (m_msgStack.empty() || m_bMsgNeedFree)
        return;
    Msg *lastMsg = m_msgStack.back();
    if (lastMsg->hwnd == hWnd && lastMsg->message == msg && lastMsg->wParam == wp && lastMsg->lParam == lp) {
        lastMsg->SetResult(res);
        m_msgStack.pop_back();
        delete lastMsg;
    }
}

void SConnection::EnableWindow(HWND hWnd, int bEnable) {
    enableUiWindow(hWnd, bEnable);
}

bool SConnection::IsIconic(HWND hWnd) const {
    return isUiWindowMinimized(hWnd);
}

bool SConnection::IsZoomed(HWND hWnd) const {
    return isUiWindowMaximized(hWnd);
}

int SConnection::ShowCursor(bool bShow) {
    // iOS 无鼠标光标，仅维护计数以兼容 API
    if (bShow) {
        m_cursorCount++;
    } else {
        m_cursorCount--;
        if (m_cursorCount <= 0)
            m_cursorCount = 0;
    }
    return m_cursorCount;
}

struct RawInputDeviceEntry {
    std::string device_path;
    DWORD device_type;
};

static std::map<int, RawInputDeviceEntry> s_rawInputDevices;
static std::recursive_mutex s_rawInputMutex;
static int s_nextDeviceId = 1;

UINT SConnection::GetRawInputDeviceList(
    _Out_writes_opt_(*puiNumDevices) PRAWINPUTDEVICELIST pRawInputDeviceList,
    _Inout_ PUINT puiNumDevices,
    _In_ UINT cbSize) {
    return 0;
}

UINT SConnection::GetRawInputDeviceInfoA(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) {
    if (!pcbSize)
        return (UINT)-1;
    UINT requiredSize = 0;
    int deviceId = (int)(intptr_t)hDevice;
    std::string device_path;
    DWORD device_type = RIM_TYPEMOUSE;
    {
        std::lock_guard<std::recursive_mutex> lock(s_rawInputMutex);
        auto it = s_rawInputDevices.find(deviceId);
        if (it == s_rawInputDevices.end()) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return (UINT)-1;
        }
        device_path = it->second.device_path;
        device_type = it->second.device_type;
    }
    switch (uiCommand) {
        case RIDI_DEVICENAME: {
            requiredSize = (UINT)device_path.length() + 1;
            if (pData && *pcbSize >= requiredSize)
                strcpy((char *)pData, device_path.c_str());
            *pcbSize = requiredSize;
            break;
        }
        case RIDI_DEVICEINFO: {
            requiredSize = sizeof(RID_DEVICE_INFO);
            if (pData && *pcbSize >= requiredSize) {
                RID_DEVICE_INFO *pInfo = (RID_DEVICE_INFO *)pData;
                pInfo->cbSize = sizeof(RID_DEVICE_INFO);
                pInfo->dwType = device_type;
                if (pInfo->dwType == RIM_TYPEMOUSE) {
                    pInfo->mouse.dwId = 0;
                    pInfo->mouse.dwNumberOfButtons = 3;
                    pInfo->mouse.dwSampleRate = 125;
                    pInfo->mouse.fHasHorizontalWheel = FALSE;
                } else if (pInfo->dwType == RIM_TYPEKEYBOARD) {
                    pInfo->keyboard.dwType = 1;
                    pInfo->keyboard.dwSubType = 0;
                    pInfo->keyboard.dwKeyboardMode = 0;
                    pInfo->keyboard.dwNumberOfFunctionKeys = 12;
                    pInfo->keyboard.dwNumberOfIndicators = 3;
                    pInfo->keyboard.dwNumberOfKeysTotal = 104;
                } else if (pInfo->dwType == RIM_TYPEHID) {
                    pInfo->hid.dwVendorId = 0;
                    pInfo->hid.dwProductId = 0;
                    pInfo->hid.dwVersionNumber = 0;
                    pInfo->hid.usUsagePage = 0;
                    pInfo->hid.usUsage = 0;
                }
            }
            *pcbSize = requiredSize;
            break;
        }
        case RIDI_PREPARSEDDATA: {
            requiredSize = 0;
            *pcbSize = requiredSize;
            break;
        }
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return (UINT)-1;
    }
    return requiredSize;
}

UINT SConnection::GetRawInputDeviceInfoW(HRAWINPUT hDevice, UINT uiCommand, LPVOID pData, PUINT pcbSize) {
    if (uiCommand != RIDI_DEVICENAME) {
        return GetRawInputDeviceInfoA(hDevice, uiCommand, pData, pcbSize);
    }
    if (!pcbSize)
        return (UINT)-1;
    UINT requiredSize = 0;
    int deviceId = (int)(intptr_t)hDevice;
    std::string device_path;
    {
        std::lock_guard<std::recursive_mutex> lock(s_rawInputMutex);
        auto it = s_rawInputDevices.find(deviceId);
        if (it == s_rawInputDevices.end()) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return (UINT)-1;
        }
        device_path = it->second.device_path;
    }
    int wLen = MultiByteToWideChar(CP_UTF8, 0, device_path.c_str(), -1, NULL, 0);
    requiredSize = (UINT)(wLen * sizeof(wchar_t));
    if (pData && *pcbSize >= requiredSize)
        MultiByteToWideChar(CP_UTF8, 0, device_path.c_str(), -1, (wchar_t *)pData, wLen);
    *pcbSize = requiredSize;
    return requiredSize;
}

BOOL SConnection::ShowSoftKeyboard(HWND hWnd, BOOL bShow){
    // 对齐 Android SConnection::ShowSoftKeyboard：委托给平台层 showUiSoftKeyboard，
    // 后者内部走 [SUIView becomeFirstResponder/resignFirstResponder] 控制软键盘。
    return showUiSoftKeyboard(hWnd, bShow);
}