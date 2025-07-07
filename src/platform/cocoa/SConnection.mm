#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/NSObjCRuntime.h>
#include <mutex>
#include <MacTypes.h>
#include <memory>
#include <map>
#include <cairo-quartz.h>
#include "SNsWindow.h"
#include "SConnection.h"
#include <synhandle.h>
#include <sdc.h>
#include <wndobj.h>
#include <log.h>
#include "os_state.h"
#include "tostring.hpp"
#include "STrayIconMgr.h"
#include "keyboard.h"

using namespace swinx;

#define kLogTag "connection"

// 1. 定义自定义事件类型（确保唯一性）
static const NSEventType kFDReadyEventType = NSEventTypeApplicationDefined;
static const NSEventType kDummyEventType = NSEventTypeApplicationDefined;
static const NSInteger kDummyEventSubtype = 200; // 自定义子类型
static const NSInteger kFDReadyEventSubtype = 100; // 自定义子类型

NSWindow *getNsWindow(HWND hWnd);

cairo_t * getNsWindowCanvas(HWND hWnd){
  WndObj wndObj = WndMgr::fromHwnd(hWnd);
  assert(wndObj);
  assert(wndObj->hdc);
  return wndObj->hdc->cairo;
}

static void ConvertNSRect(NSScreen *screen, bool fullscreen, NSRect *r)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    r->origin.y = screen_height - r->origin.y - r->size.height;
}

static void ConvertNSPoint(NSScreen *screen, NSSize wndSize, NSPoint *pt)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    pt->y = screen_height - pt->y - wndSize.height;
}

static void RevertNSRect(NSScreen *screen, bool fullscreen, NSRect *r)
{
    size_t screen_height = screen?screen.frame.size.height:CGDisplayPixelsHigh(kCGDirectMainDisplay);
    r->origin.y = screen_height - r->origin.y - r->size.height;
}


static RECT NSRectToRECT(NSRect rect)
{
    RECT r;
    r.left = rect.origin.x;
    r.top = rect.origin.y;
    r.right = rect.origin.x + rect.size.width;
    r.bottom = rect.origin.y + rect.size.height;
    return r; 
}
int SConnection::GetDisplayBounds(int displayIndex, RECT *rect)
{
    assert(rect);
    @autoreleasepool{
        NSArray<NSScreen*> *screens = [NSScreen screens];
        if(displayIndex >= [screens count])
            return -1;
        NSScreen *screen = [screens objectAtIndex:displayIndex];
        NSRect nsrc = [screen frame];
        *rect = NSRectToRECT(nsrc);
        return 0;
    }
}


int SConnection::GetRectDisplayIndex(int x, int y, int w, int h)
{
    RECT rcSrc={x,y,x+w,y+h};
    RECT rcInterMax={0};
    int  rcMaxArea= 0;
    int closest = -1;

    @autoreleasepool{
        NSArray<NSScreen*> *screens = [NSScreen screens];
        for (int i = 0; i < [screens count]; ++i) {
            NSScreen *screen = [screens objectAtIndex:i];
            NSRect rect = [screen frame];
            RECT rcDisplay = NSRectToRECT(rect);
            RECT rcInter;
            IntersectRect(&rcInter, &rcSrc, &rcDisplay);
            int area = (rcInter.right - rcInter.left) * (rcInter.bottom - rcInter.top);
            if(area > rcMaxArea){
                rcMaxArea = area;
                closest = i;
            } 
        }
    }
    return closest;
}


SConnection::SConnection(int screenNum)
:m_bQuit(false)
,m_bBlockTimer(false)
{
  swinx::init(this);
  m_tid = GetCurrentThreadId();
  m_clipboard = new SClipboard();
  m_trayIconMgr = new STrayIconMgr();
    m_deskDC = new _SDC(0);
    m_deskBmp = CreateCompatibleBitmap(m_deskDC, 1, 1);
    SelectObject(m_deskDC, m_deskBmp);
    memset(&m_caretInfo, 0, sizeof(m_caretInfo));
}

SConnection::~SConnection() {
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
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
  swinx::shutdown();
}

static bool isKeyPressed(uint16_t keyCode) {
    //todo:hjx
    UINT vKey = convertKeyCodeToVK(keyCode);
    return Keyboard::instance().getKeyState(vKey) & 0x8000;
}


SHORT SConnection::GetKeyState(int vk) {
    @autoreleasepool{
        switch(vk){
            case VK_LBUTTON:
                return [NSEvent pressedMouseButtons] & (1<<0) ? 0x8000 : 0;
            case VK_RBUTTON:
                return [NSEvent pressedMouseButtons] & (1<<1) ? 0x8000 : 0;
            case VK_MBUTTON:
                return [NSEvent pressedMouseButtons] & (1<<2) ? 0x8000 : 0;
            case VK_SHIFT:
                return [NSEvent modifierFlags] & NSShiftKeyMask ? 0x8000 : 0;
            case VK_CONTROL:
                return [NSEvent modifierFlags] & NSControlKeyMask ? 0x8000 : 0;
            case VK_MENU:
                return [NSEvent modifierFlags] & NSAlternateKeyMask ? 0x8000 : 0;
            case VK_CAPITAL:
                return [NSEvent modifierFlags] & NSAlphaShiftKeyMask ? 0x8000 : 0;
            default:
                return isKeyPressed(convertVKToKeyCode(vk)) ? 0x8000 : 0;
        }
    }
    return 0;
}

bool SConnection::GetKeyboardState(PBYTE lpKeyState) {
    Keyboard::instance().getKeyboardState(lpKeyState);
    lpKeyState[VK_LBUTTON]=GetKeyState(VK_LBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_RBUTTON]=GetKeyState(VK_RBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_MBUTTON]=GetKeyState(VK_MBUTTON) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_SHIFT]=GetKeyState(VK_SHIFT) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_CONTROL]=GetKeyState(VK_CONTROL) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_MENU]=GetKeyState(VK_MENU) & 0x8000 ? 0x80 : 0;
    lpKeyState[VK_CAPITAL]=GetKeyState(VK_CAPITAL) & 0x8000 ? 0x80 : 0;
    return TRUE;
}

SHORT SConnection::GetAsyncKeyState(int vk) {
    return GetKeyState(vk);
}

UINT SConnection::MapVirtualKey(UINT uCode, UINT type) const {
    UINT ret;
    switch (type)
    {
    case MAPVK_VK_TO_VSC_EX:
    case MAPVK_VK_TO_VSC:
    {
        ret = convertVKToKeyCode(uCode);
        break;
    }
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
    {
        ret = convertKeyCodeToVK(uCode);
        break;
    }
    case MAPVK_VK_TO_CHAR:
    {
        UINT scanCode = convertVKToKeyCode(uCode);
        ret = scanCodeToASCII(scanCode,0);
        break;
    }
    default:
        // FIXME_(keyboard)("unknown type %d\n", type);
        return 0;
    }
    return ret;
}

UINT SConnection::GetDoubleClickTime() const {
    @autoreleasepool{
        return [NSEvent doubleClickInterval]*1000;
    }
}

LONG SConnection::GetMsgTime() const {
  if(m_msgPeek){
    return m_msgPeek->time;
  }else{
    return 0;
  }
}

DWORD SConnection::GetMsgPos() const {
  if(m_msgPeek){
    return MAKELONG(m_msgPeek->pt.x, m_msgPeek->pt.y);
  }else{
    return 0;
  }
}

DWORD SConnection::GetQueueStatus(UINT flags) {
    // Empty implementation
    return 0;
}


bool SConnection::waitMsg() {
    UINT timeOut = INFINITE;
    if (!m_bBlockTimer)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        for (auto &it : m_lstTimer)
        {
            timeOut = std::min(timeOut, it.fireRemain);
        }
    }
    updateMsgQueue(timeOut);
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    return !m_msgQueue.empty();
}

static void setupFDMonitoring(NSMutableArray *fdSources,int fds[], int fdCount) {
    for (int i = 0; i < fdCount; i++) {
        int fd = fds[i];
        dispatch_source_t source = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_READ,
            fd,
            0,
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)
        );
        dispatch_source_set_event_handler(source, ^{
            NSEvent *event = [NSEvent otherEventWithType:kFDReadyEventType
                                              location:NSZeroPoint
                                         modifierFlags:0
                                             timestamp:0
                                          windowNumber:0
                                               context:nil
                                               subtype:kFDReadyEventSubtype
                                                 data1:i // 传递 fd
                                                 data2:0];
            
            [NSApp postEvent:event atStart:NO];
        });
        dispatch_resume(source);
        [fdSources addObject:source];
    }
}


/**
 * convert ns point to client point 
 * @param nswindow ns window
 * @param pt ns point
 * @return true if the point is in the window, false otherwise
 */
static bool wndpos2nsclient(NSWindow *nswindow, NSPoint &pt) {
    NSRect rect = [nswindow contentRectForFrameRect:(NSRect)nswindow.frame];
    rect.origin.x -= nswindow.frame.origin.x;
    rect.origin.y -= nswindow.frame.origin.y;
    if(!NSPointInRect(pt, rect)){
        return false;
    }
    ConvertNSRect([nswindow screen], FALSE, &rect);
    ConvertNSPoint([nswindow screen], rect.size, &pt);
    pt.x -= rect.origin.x;
    pt.y -= rect.origin.y;
    
    return true;
}

void SConnection::postMsg(Msg *pMsg){
  std::unique_lock<std::recursive_mutex> lock(m_mutex);
  m_msgQueue.push_back(pMsg);
  //SLOG_STMI() << "postMsg, msg=" << pMsg->message;
  stopEventWaiting();
}

void SConnection::stopEventWaiting(){
  @autoreleasepool{
      NSEvent *event = [NSEvent otherEventWithType:kDummyEventType
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:kDummyEventSubtype
                                           data1:0
                                           data2:0];
      [NSApp postEvent:event atStart:NO]; // atStart:NO 表示插入队列尾部
  }
}

static NSEvent *nextEvent(NSDate *timeoutDate, SConnection *connection) {
    NSEvent *nsEvent = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:timeoutDate
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
    if (nsEvent == nil)
      return nil;
    NSInteger eventType = nsEvent.type;
    if (eventType == NSEventTypeMouseMoved && connection->GetCapture() == NULL) {
      NSPoint mouseLocation = [nsEvent locationInWindow];
      if (nsEvent.window != nil) {
        mouseLocation = [nsEvent.window convertPointToScreen:mouseLocation];
      }
      static __weak NSWindow *lastHitWeak = nil;
      // find the window by its mouse location
      @autoreleasepool{
      NSWindow *lastHit = lastHitWeak;
      NSWindow *wndHit = nil;
      auto windows = [NSApp windows];
      for (int i = [windows count] - 1; i >= 0; i--) {
        auto window = [windows objectAtIndex:i];
        if(![window isVisible])
          continue;
        NSRect frame = [window frame];
        if (NSPointInRect(mouseLocation, frame)) {
          wndHit = window;
          break;
        }
      }
      if(wndHit != lastHit){
        if(lastHit != nil){
          [lastHit mouseExited:nsEvent];
        }
        if(wndHit != nil){
          [wndHit mouseEntered:nsEvent];
        }
        lastHitWeak = wndHit;
      }
      if (wndHit == nil)
        return nil;
      NSInteger windowNumber = [wndHit windowNumber];
      if (windowNumber != nsEvent.windowNumber) {
        NSPoint ptWnd = [wndHit convertPointFromScreen:(NSPoint)mouseLocation];
        nsEvent = [NSEvent mouseEventWithType:NSMouseMoved
                                     location:ptWnd
                                modifierFlags:0
                                    timestamp:0.0
                                 windowNumber:windowNumber
                                      context:nil
                                  eventNumber:0
                                   clickCount:0
                                     pressure:0.0];
      }
      }
    }
    return nsEvent;
}

int SConnection::waitMutliObjectAndMsg(const HANDLE *handles, int nCount, DWORD timeout, bool fWaitAll, DWORD dwWaitMask) {
    HANDLE tmpHandles[MAXIMUM_WAIT_OBJECTS] = { 0 };
    int fds[MAXIMUM_WAIT_OBJECTS] = { 0 };
    bool states[MAXIMUM_WAIT_OBJECTS] = { false };
    int nSignals = 0;
    for (int i = 0; i < nCount; i++)
    {
        tmpHandles[i] = AddHandleRef(handles[i]);
        fds[i] = GetSynHandle(tmpHandles[i])->getReadFd();
    }
    @autoreleasepool 
    {
        NSMutableArray *fdSources = [NSMutableArray array];
        setupFDMonitoring(fdSources, fds, nCount);
        NSDate *timeoutDate = nil;
        if(timeout != INFINITE)
            timeoutDate = [NSDate dateWithTimeIntervalSinceNow:timeout/1000.0];
        do
        {
            DWORD ts1=GetTickCount();
            NSEvent * nsEvent = nextEvent(timeoutDate, this);
            if(nsEvent == nil)
                break;
            DWORD ts2=GetTickCount();
            DWORD elapse = ts2 - ts1;
            if(timeout!=INFINITE){
              if(timeout>elapse)
                timeout -= elapse;
              else
                timeout = 0;
              timeoutDate = [NSDate dateWithTimeIntervalSinceNow:timeout/1000.0];
            }

            if(nsEvent.type == kFDReadyEventType && nsEvent.subtype == kFDReadyEventSubtype){
                int ifd = nsEvent.data1;
                states[ifd]=true;
                nSignals++;
                if(nSignals == nCount || !fWaitAll)
                  break;
            }else{
                [NSApp sendEvent:nsEvent];
                [NSApp updateWindows];
                break;
            }
        } while (true);
        //clear fdSources
        for (int i = 0; i < fdSources.count; i++) {
            dispatch_source_t source = [fdSources objectAtIndex:i];
            dispatch_source_cancel(source);
        }
    }

    int ret = WAIT_TIMEOUT;

    for (int i = 0; nSignals && i < nCount; i++)
    {
        if (GetSynHandle(tmpHandles[i])->onWaitDone())
        {
            if(ret == WAIT_TIMEOUT)
                ret = WAIT_OBJECT_0 + i;
            nSignals--;
        }
    }
    for(int i = 0; i < nCount; i++){
        CloseHandle(tmpHandles[i]);
    }
    return ret;
}

bool SConnection::TranslateMessage(const MSG *pMsg) {
    if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
    {
        uint32_t modifiers = 0;
        if(GetKeyState(VK_SHIFT) & 0x8000)
            modifiers |= shiftKey;
        if(GetKeyState(VK_CONTROL) & 0x8000)
            modifiers |= controlKey;
        if(GetKeyState(VK_MENU) & 0x8000)
            modifiers |= optionKey;
        uint8_t c = scanCodeToASCII(HIWORD(pMsg->lParam),modifiers);
        if (c != 0 && !GetKeyState(VK_CONTROL) && !GetKeyState(VK_MENU))
        {
            std::unique_lock<std::recursive_mutex> lock(m_mutex);
            Msg *msg = new Msg;
            msg->message = pMsg->message == WM_KEYDOWN ? WM_CHAR : WM_SYSCHAR;
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

extern "C" BOOL EndMenu();

void SConnection::updateMsgQueue(DWORD dwTimeout) {
    uint64_t ts = GetTickCount64();
    UINT elapse = m_tsLastMsg == -1 ? 0 : (ts - m_tsLastMsg);
    m_tsLastMsg = ts;
    if (!m_bBlockTimer)
    {
        std::unique_lock<std::recursive_mutex> lock(m_mutex);
        static const size_t kMaxDalayMsg = 5; // max delay ms for a timer.
        int elapse2 = elapse + std::min(m_msgQueue.size(), kMaxDalayMsg);
        POINT pt;
        GetCursorPos(&pt);
        for (auto &it : m_lstTimer)
        {
            if (it.fireRemain <= elapse2)
            {
                // fire timer event
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
            }
            else
            {
                it.fireRemain -= elapse;
            }
        }
    }
    @autoreleasepool
    {
        NSDate *timeoutDate = nil;
        if(dwTimeout == 0)
            timeoutDate = [NSDate distantPast];
        else if(dwTimeout != INFINITE)
            timeoutDate = [NSDate dateWithTimeIntervalSinceNow:dwTimeout/1000.0];
        do
        {
            NSEvent * nsEvent = nextEvent(timeoutDate, this);
            if(nsEvent == nil)
                break;
            if(nsEvent.type == kFDReadyEventType && nsEvent.subtype == kFDReadyEventSubtype){
                continue;
            }
            if(nsEvent.type == NSEventTypeAppKitDefined && nsEvent.subtype == NSEventSubtypeApplicationDeactivated){
                //quit menu.
                EndMenu();
                postMsg(0,WM_CANCELMODE,0,0);
            }
            [NSApp sendEvent:nsEvent];
            [NSApp updateWindows];
            timeoutDate = [NSDate distantPast];
        } while (true);
    }
}

bool SConnection::peekMsg(LPMSG pMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    updateMsgQueue(0);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    { // test for callback task
        auto it = m_lstCallbackTask.begin();
        while (it != m_lstCallbackTask.end())
        {
            auto cur = it++;
            if ((*cur)->TestEvent())
            {
                (*cur)->Release();
                m_lstCallbackTask.erase(cur);
            }
        }
    }
    auto it = m_msgQueue.begin();
    for (; it != m_msgQueue.end(); it++)
    {
        bool bMatch = TRUE;
        Msg *msg = (*it);
        do
        {
            if (msg->message == WM_QUIT)
                break;
            if (msg->message == WM_TIMER && msg->lParam == 0)
                break;
            if (msg->hwnd != hWnd && hWnd != 0)
            {
                bMatch = FALSE;
                break;
            }
            if (wMsgFilterMin == 0 && wMsgFilterMax == 0)
                break;
            if (wMsgFilterMin <= msg->message && wMsgFilterMax >= msg->message)
                break;
            bMatch = FALSE;
        } while (false);
        if (bMatch)
            break;
    }
   if (it != m_msgQueue.end())
    {
        Msg *msg = (*it);
        if (m_msgPeek && m_bMsgNeedFree)
        {
            delete m_msgPeek;
            m_msgPeek = nullptr;
            m_bMsgNeedFree = false;
        }
        if (msg->message == WM_TIMER && msg->lParam)
        {
            // SetTimer with callback, call it now.
            TIMERPROC proc = (TIMERPROC)msg->lParam;
            m_msgQueue.erase(it);
            proc(msg->hwnd, WM_TIMER, msg->wParam, msg->time);
            delete msg;
            return PeekMessage(pMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
        }
        m_msgPeek = msg;
        if (wRemoveMsg == PM_NOREMOVE)
        { // the peeked message should not be dispatch.
            m_bMsgNeedFree = false;
        }
        else
        {
            m_msgQueue.erase(it);
            m_bMsgNeedFree = true;
        }
        memcpy(pMsg, (MSG *)m_msgPeek, sizeof(MSG));
        return TRUE;
    }

    return FALSE;
}

bool SConnection::getMsg(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    bool bRet = FALSE;
    for (; !bRet && !m_bQuit;)
    {
        bRet = peekMsg(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, PM_REMOVE);
        if (!bRet)
        {
            waitMsg();
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
   if (!bWideChar)
    {
        Msg *pMsg = new Msg(reply);
        pMsg->hwnd = hWnd;
        pMsg->message = message;
        pMsg->wParam = wp;
        pMsg->lParam = lp;
        GetCursorPos(&pMsg->pt);
        postMsg(pMsg);
    }
    else
    {
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
    if (hWnd)
    {
        // find exist timer.
        for (auto &it : m_lstTimer)
        {
            if (it.hWnd != hWnd)
                continue;
            if (it.id == id)
            {
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
    }
    else
    {
        UINT_PTR newId = 0;
        for (auto &it : m_lstTimer)
        {
            if (it.hWnd)
                continue;
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

    if(GetCurrentThreadId() != m_tid){
      dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        stopEventWaiting();
      });
    }else{
        stopEventWaiting();
    }
    return ret;
}

bool SConnection::KillTimer(HWND hWnd, UINT_PTR id) {
    bool bRet = FALSE;
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    for (auto it = m_lstTimer.begin(); it != m_lstTimer.end(); it++)
    {
        if (it->hWnd == hWnd && it->id == id)
        {
            m_lstTimer.erase(it);
            bRet = TRUE;
            break;
        }
    }

    if (bRet)
    {
        // remove timer from message queue.
        auto it = m_msgQueue.begin();
        while (it != m_msgQueue.end())
        {
            auto it2 = it++;
            Msg *msg = *it2;
            if (msg->hwnd == hWnd && msg->message == WM_TIMER && msg->wParam == id)
            {
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
    if(!setNsWindowCapture(hCapture))
        return hRet;
    m_hWndCapture = hCapture;
    return hRet;
}

bool SConnection::ReleaseCapture() {
    if(!m_hWndCapture)
      return FALSE;
    if(!releaseNsWindowCapture(m_hWndCapture))
        return FALSE;
    m_hWndCapture = NULL;
    return TRUE;
}

HWND SConnection::GetCapture() const {
    return m_hWndCapture;
}

HCURSOR SConnection::SetCursor(HWND hWnd,HCURSOR cursor) {
    if(!hWnd){
        if(m_msgPeek){
            hWnd = m_msgPeek->hwnd;
        }else{
            hWnd = GetActiveWnd();
        }
    }
    if (!hWnd)
        return cursor;
    auto it = m_wndCursor.find(hWnd);
    HCURSOR ret = 0;
    if (it != m_wndCursor.end())
    {
        ret = it->second;
    }
    setNsWindowCursor(hWnd, cursor);
    m_wndCursor[hWnd] = cursor; // update window cursor
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
    for (auto &it : m_wndCursor)
    {
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
  return setNsActiveWindow(hWnd);
}

HWND SConnection::WindowFromPoint(POINT pt, HWND hWnd) const {
    return hwndFromPoint(hWnd, pt);
}

bool SConnection::IsWindow(HWND hWnd) const {
    return IsNsWindow(hWnd);
}

void SConnection::SetWindowPos(HWND hWnd, int x, int y) const {
    if (!IsWindow(hWnd))
      return;
    setNsWindowPos(hWnd, x, y);
}

void SConnection::SetWindowSize(HWND hWnd, int cx, int cy) const {
    if (!IsWindow(hWnd))
      return;
    setNsWindowSize(hWnd, cx, cy);
}

bool SConnection::MoveWindow(HWND hWnd, int x, int y, int cx, int cy) const {
    if (!IsWindow(hWnd))
      return FALSE;
    SetWindowPos(hWnd, x, y);
    SetWindowSize(hWnd, cx, cy);
    return TRUE;
}

bool SConnection::GetCursorPos(LPPOINT ppt) const {
    return getNsCursorPos(ppt);
}

int SConnection::GetDpi(bool bx) const {
    return getNsDpi(bx);
}

void SConnection::KillWindowTimer(HWND hWnd) {
   std::unique_lock<std::recursive_mutex> lock(m_mutex);
    auto it = m_lstTimer.begin();
    while (it != m_lstTimer.end())
    {
        auto cur = it++;
        if (cur->hWnd == hWnd)
        {
            m_lstTimer.erase(cur);
        }
    }
}

HWND SConnection::GetForegroundWindow() {
    HWND hret = m_hForeground;
    if(!IsNsWindow(hret)){
        hret = getNsForegroundWindow();
    }
    return hret;
}

bool SConnection::SetForegroundWindow(HWND hWnd) {
    bool ret =setNsForegroundWindow(hWnd);
    if(ret){
        m_hForeground = hWnd;
    }
    return ret;
}

bool SConnection::SetWindowOpacity(HWND hWnd, BYTE byAlpha) {
    return setNsWindowAlpha(hWnd,byAlpha);
}

bool SConnection::SetWindowRgn(HWND hWnd, HRGN hRgn) {
  RGNDATA rgnData;
  DWORD len = GetRegionData(hRgn, 0, nullptr);
  if (!len)
    return FALSE;
  GetRegionData(hRgn, len, &rgnData);
  const LPRECT prc=(LPRECT)rgnData.Buffer;
  return setNsWindowRgn(hWnd, prc, rgnData.rdh.nCount);
}

HKL SConnection::ActivateKeyboardLayout(HKL hKl) {
    // Empty implementation
    return NULL;
}

HBITMAP SConnection::GetDesktopBitmap() {
    return m_deskBmp;
}

HWND SConnection::GetFocus() const {
    return m_hFocus;
}

bool SConnection::SetFocus(HWND hWnd) {
    if(hWnd==m_hFocus)
        return TRUE;
    if(hWnd){
        setNsFocusWindow(hWnd);
    }
    HWND hOldFocus = m_hFocus;  
    if(hOldFocus){
        SendMessage(hOldFocus,WM_KILLFOCUS,(WPARAM)hWnd,0);
    }
    if(hWnd){
        SendMessage(hWnd,WM_SETFOCUS,(WPARAM)hOldFocus,0);
    }
    m_hFocus = hWnd;
    SLOG_STMI()  << "setFocus hWnd=" << hWnd<<" hOldFocus=" << hOldFocus;
    return TRUE;
}

bool SConnection::IsDropTarget(HWND hWnd) {
    return isNsDropTarget(hWnd);
}

bool SConnection::FlashWindowEx(PFLASHWINFO info) {
    return flashNsWindow(info->hwnd, info->dwFlags,info->uCount,info->dwTimeout);
}

int SConnection::OnGetClassName(HWND hWnd, LPSTR lpClassName, int nMaxCount) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
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
    if (wndObj)
    {
        return wndObj->title.length();
    }
    return 0;
}

int SConnection::OnGetWindowTextLengthW(HWND hWnd) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), nullptr, 0);
    }
    return 0;
}

int SConnection::OnGetWindowTextA(HWND hWnd, char *buf, int bufLen) {
    WndObj wndObj = WndMgr::fromHwnd(hWnd);
    if (wndObj)
    {
        if (bufLen < wndObj->title.length())
        {
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
    if (wndObj)
    {
        return MultiByteToWideChar(CP_UTF8, 0, wndObj->title.c_str(), wndObj->title.length(), buf, bufLen);
    }
    return 0;
}

HWND SConnection::OnFindWindowEx(HWND hParent, HWND hChildAfter, LPCSTR lpClassName, LPCSTR lpWindowName) {
    // Empty implementation
    return NULL;
}

extern HWND getHwndFromView(NSView *view);
bool SConnection::OnEnumWindows(HWND hParent, HWND hChildAfter, WNDENUMPROC lpEnumFunc, LPARAM lParam) {
    @autoreleasepool {
        if(!hParent){
            //enum all top level windows
            auto windows = [NSApp windows];
            for (NSWindow *window in windows) {
                HWND hWnd = getHwndFromView(window.contentView);
                if(!hWnd)
                    continue;
                if(!lpEnumFunc(hWnd, lParam))
                    break;
            }
            return TRUE;
        }else{
            NSView * nsParent = (__bridge NSView *)(void*)hParent;
            auto windows = [nsParent subviews];
            for (NSView *view in windows) {
                HWND hWnd = getHwndFromView(view);
                if(!hWnd)
                    continue;
                if(!lpEnumFunc(hWnd, lParam))
                    break;
            }
            return TRUE;
        }
    }
}

HWND SConnection::_GetRoot(HWND hWnd){
    HWND hRoot = hWnd;
    HWND hParent = _GetParent(hWnd);
    while(hParent){
        hRoot  = hParent;
        hParent = _GetParent(hParent);
    }
    return hRoot;
}

HWND SConnection::_GetParent(HWND hWnd){
  WndObj wndObj = WndMgr::fromHwnd(hWnd);
  if(!wndObj)
    return NULL;
  if(wndObj->dwStyle & WS_CHILD)
    return wndObj->parent;
  return NULL;
}

HWND SConnection::OnGetAncestor(HWND hwnd, UINT gaFlags) {
    switch (gaFlags)
    {
    case GA_PARENT:
        return _GetParent(hwnd);
    case GA_ROOT:
        _GetRoot(hwnd);
    case GA_ROOTOWNER:
    {
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
        NSScreen *screen = [NSScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

HMONITOR SConnection::MonitorFromPoint(POINT pt, DWORD dwFlags) {
@autoreleasepool {
        NSScreen *screen = [NSScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

HMONITOR SConnection::MonitorFromRect(LPCRECT lprc, DWORD dwFlags) {
    @autoreleasepool {
        NSScreen *screen = [NSScreen mainScreen];
        return (__bridge HMONITOR)screen;
    }
}

int SConnection::GetScreenWidth(HMONITOR hMonitor) const {
    @autoreleasepool {
        NSScreen *screen = (__bridge NSScreen *)hMonitor;
        NSRect rect = [screen frame];
        return rect.size.width * [screen backingScaleFactor];
    }
}

int SConnection::GetScreenHeight(HMONITOR hMonitor) const {
    @autoreleasepool {
        NSScreen *screen = (__bridge NSScreen *)hMonitor;
        NSRect rect = [screen frame];
        return rect.size.height * [screen backingScaleFactor];
    }
}

HWND SConnection::GetScreenWindow() const {
    return NULL;
}

void SConnection::UpdateWindowIcon(HWND hWnd, _Window *wndObj) {
   if(wndObj->iconSmall){
    setNsWindowIcon(hWnd,wndObj->iconSmall,FALSE);
   }
   if(wndObj->iconBig){
    setNsWindowIcon(hWnd,wndObj->iconBig,TRUE);
   }
}

uint32_t SConnection::GetVisualID(bool bScreen) const {
    // Empty implementation
    return 0;
}

uint32_t SConnection::GetCmap() const {
    // Empty implementation
    return 0;
}

void SConnection::SetZOrder(HWND hWnd, _Window *wndObj, HWND hWndInsertAfter) {
    setNsWindowZorder(hWnd, hWndInsertAfter);
}

void SConnection::SendClientMessage(HWND hWnd, uint32_t type, uint32_t *data, int len) {
    // Empty implementation
}

uint32_t SConnection::GetIpcAtom() const {
    // Empty implementation
    return 0;
}

void SConnection::postCallbackTask(CbTask *pTask) {
    std::unique_lock<std::recursive_mutex> lock(m_mutex);
    m_lstCallbackTask.push_back(pTask);
    pTask->AddRef();
}

cairo_surface_t *SConnection::CreateWindowSurface(HWND hWnd, uint32_t visualId, int width, int height) {
    return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
}

cairo_surface_t * SConnection::ResizeSurface(cairo_surface_t *surface,HWND hWnd, uint32_t visualId, int width, int height) {
    cairo_surface_destroy(surface);
    return CreateWindowSurface(hWnd, visualId, width, height);
}

DWORD SConnection::GetWndProcessId(HWND hWnd) {
    CFArrayRef windowInfos = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, hWnd);
    if (windowInfos && CFArrayGetCount(windowInfos) > 0) {
        CFDictionaryRef windowInfo = (CFDictionaryRef)CFArrayGetValueAtIndex(windowInfos, 0);
        CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(windowInfo, kCGWindowOwnerPID);
        
        pid_t pid;
        if (CFNumberGetValue(pidRef, kCFNumberIntType, &pid)) {
            CFRelease(windowInfos);
            return pid;
        }
    }
    
    if (windowInfos) {
        CFRelease(windowInfos);
    }
    
    return -1;
}

HWND SConnection::WindowFromPoint(POINT pt) {
    return hwndFromPoint(NULL,pt);
}

bool SConnection::GetClientRect(HWND hWnd, RECT *pRc) {
    return getNsWindowRect(hWnd, pRc);
}

void SConnection::SendSysCommand(HWND hWnd, int nCmd) {
    sendNsSysCommand(hWnd, nCmd);
}

bool SConnection::IsWindowVisible(HWND hWnd) {
    return isNsWindowVisible(hWnd);
}

HWND SConnection::GetWindow(HWND hWnd, _Window *wndObj, UINT uCmd) {
    if (uCmd == GW_OWNER)
    {
        if (!wndObj)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
        return wndObj->owner;
    }

  return getNsWindow(hWnd,uCmd);
}

UINT SConnection::RegisterMessage(LPCSTR lpString) {
    static UINT message = WM_USER+100000;
    return message++;
}

bool SConnection::NotifyIcon(DWORD dwMessage, PNOTIFYICONDATAA lpData) {
    return GetTrayIconMgr()->NotifyIcon(dwMessage, lpData);
}

HMONITOR SConnection::GetScreen(DWORD dwFlags) const {
    @autoreleasepool {
    NSScreen *screen = [NSScreen mainScreen];
    return (__bridge HMONITOR)screen;
    }
}

bool SConnection::CreateCaret(HWND hWnd, HBITMAP hBitmap, int nWidth, int nHeight)
{
    DestroyCaret();
    m_caretInfo.hOwner = hWnd;
    if (hBitmap == (HBITMAP)1) // windows api support hBitmap set to 1 to indicator a gray caret, ignore it.
        hBitmap = nullptr;
    m_caretInfo.hBmp = RefGdiObj(hBitmap);
    m_caretInfo.nWidth = nWidth;
    m_caretInfo.nHeight = nHeight;
    m_caretInfo.nVisible = 0;
    return TRUE;
}

bool SConnection::DestroyCaret()
{
    if (m_caretInfo.nVisible > 0)
    {
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

bool SConnection::ShowCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible++;
    if (m_caretInfo.nVisible == 1)
    {
        SetTimer(hWnd, TM_CARET, m_caretBlinkTime, NULL);
    }
    return TRUE;
}

bool SConnection::HideCaret(HWND hWnd)
{
    if (hWnd && hWnd != m_caretInfo.hOwner)
        return FALSE;
    m_caretInfo.nVisible--;

    if (m_caretInfo.nVisible == 0)
    {
        KillTimer(hWnd, TM_CARET);
    }
    return TRUE;
}

bool SConnection::SetCaretPos(int X, int Y)
{
    m_caretInfo.x = X;
    m_caretInfo.y = Y;
    return TRUE;
}

bool SConnection::GetCaretPos(LPPOINT lpPoint)
{
    if (!lpPoint)
        return FALSE;
    lpPoint->x = m_caretInfo.x;
    lpPoint->y = m_caretInfo.y;
    return TRUE;
}

const SConnection::CaretInfo* SConnection::GetCaretInfo() const {
    return &m_caretInfo;
}

void SConnection::SetCaretBlinkTime(UINT blinkTime) {
    m_caretBlinkTime = blinkTime;
}

UINT SConnection::GetCaretBlinkTime() const {
    return m_caretBlinkTime;
}

void SConnection::GetWorkArea(HMONITOR hMonitor, RECT* prc) {
    @autoreleasepool {
        NSScreen *screen = (__bridge NSScreen *)hMonitor;
        NSRect rect = [screen visibleFrame];
        float scale = [screen backingScaleFactor];
        prc->left = rect.origin.x * scale;
        prc->top = rect.origin.y* scale;
        prc->right = (rect.origin.x + rect.size.width)* scale;
        prc->bottom = (rect.origin.y + rect.size.height)* scale;
    }
}

SClipboard* SConnection::getClipboard() {
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

bool SConnection::hasXFixes() const {
    // Empty implementation
    return false;
}

STrayIconMgr* SConnection::GetTrayIconMgr() {
    return m_trayIconMgr;
}

void SConnection::EnableDragDrop(HWND hWnd, bool enable) {
    setNsDropTarget(hWnd, enable);
}

HRESULT SConnection::DoDragDrop(IDataObject *pDataObject,
                          IDropSource *pDropSource,
                          DWORD dwOKEffect,     
                          DWORD *pdwEffect){
    return doNsDragDrop(pDataObject, pDropSource, dwOKEffect, pdwEffect);
                          }

HWND SConnection::OnWindowCreate(_Window *wnd, CREATESTRUCT *cs, int depth) {
    return createNsWindow(cs->hwndParent, cs->style, cs->dwExStyle, cs->lpszName, cs->x, cs->y, cs->cx, cs->cy,this);
}

void SConnection::OnWindowDestroy(HWND hWnd, _Window *wnd) {
    closeNsWindow(hWnd);
    if(hWnd == m_hWndCapture)
        m_hWndCapture = 0;
    if(m_hFocus == hWnd)
        m_hFocus = 0;
    if(m_hForeground == hWnd)
        m_hForeground = 0;
    if(m_hActive == hWnd){
        m_hActive = 0;
        HWND hkeyWindow = findNsKeyWindow();
        if(hkeyWindow){
            setNsActiveWindow(hkeyWindow);
        }
    }
}

void SConnection::SetWindowVisible(HWND hWnd, _Window *wndObj, bool bVisible, int nCmdShow) { 
    assert(wndObj);
    if (bVisible)
    {
        if (0 == (wndObj->dwStyle & WS_CHILD))
        { // show a popup window, auto release capture.
            ReleaseCapture();
        }
        showNsWindow(hWnd,SW_SHOW);
        wndObj->dwStyle |= WS_VISIBLE;
        InvalidateRect(hWnd, nullptr, TRUE);
        if (nCmdShow != SW_SHOWNOACTIVATE && nCmdShow != SW_SHOWNA && !(wndObj->dwStyle & WS_CHILD) && wndObj->mConnection->GetActiveWnd() == 0)
            SetActiveWindow(hWnd);
    }
    else
    {
        showNsWindow(hWnd,SW_HIDE);
        wndObj->dwStyle &= ~WS_VISIBLE;
    }
}

void SConnection::SetParent(HWND hWnd, _Window *wnd, HWND parent) {
    setNsParent(hWnd,parent);
}

void SConnection::SendExposeEvent(HWND hWnd, LPCRECT rc) {
    invalidateNsWindow(hWnd, rc);
}

void SConnection::SetWindowMsgTransparent(HWND hWnd, _Window *wndObj, bool bTransparent) {
    setNsMsgTransparent(hWnd,bTransparent);
}

void SConnection::AssociateHIMC(HWND hWnd, _Window *wndObj, HIMC hIMC) {
    // Empty implementation
}

void SConnection::flush() {
}

void SConnection::sync() {
}

bool SConnection::IsScreenComposited() const {
    return TRUE;
}

//---------------------------------------------------------
SConnMgr *SConnMgr::instance()
{
    static SConnMgr inst;
    return &inst;
}

SConnection *SConnMgr::getConnection(tid_t tid_, int screenNum)
{
    if (tid_ != 0 && tid_ != m_tid)
    {
        return NULL;
    }
    return m_conn;
}

// 初始化 FontConfig 并添加自定义路径
static bool init_fontconfig_with_custom_dirs(const char** custom_dirs, int dir_count) {
    
    if (!FcInit())
    {
        SLOG_STMW() << "Failed to initialize Fontconfig";
        return FALSE;
    }
    FcConfig *config = FcConfigGetCurrent();
    if (!config)
    {
        SLOG_STMW() << "Failed to get current Fontconfig configuration";
        return FALSE;
    }
    // 添加自定义路径
    for (int i = 0; i < dir_count; i++) {
        if (!FcConfigAppFontAddDir(config, (const FcChar8*)custom_dirs[i])) {
            SLOG_STMW()<<"Warning: Failed to add font directory: "<<custom_dirs[i];
        }
    }
    
    //add  cur dir
    char szCurDir[1024];
    getcwd(szCurDir, sizeof(szCurDir));
    strcat(szCurDir,"/fonts");
    if (!FcConfigAppFontAddDir(config, (const FcChar8*)szCurDir)) {
        SLOG_STMW()<<"Warning: Failed to add font directory: "<<szCurDir;
    }
    return TRUE;
}


SConnMgr::SConnMgr()
{
    const char* mac_font_dirs[] = {
        "/System/Library/Fonts",
        "~/Library/Fonts",
    };
    
    // 初始化 FontConfig
    bool ret = init_fontconfig_with_custom_dirs(mac_font_dirs, ARRAYSIZE(mac_font_dirs));
    if (!ret) {
        SLOG_STMW() << "Failed to initialize FontConfig";
    }
    //setenv("CG_CONTEXT_SHOW_BACKTRACE", "1", 1);
    m_tid = GetCurrentThreadId();
    m_hHeap = HeapCreate(0, 0, 0);
    m_conn = new SConnection(0);
}

SConnMgr::~SConnMgr()
{
    delete m_conn;
    m_conn = NULL;
    CloseHandle(m_hHeap);
}

void SConnection::onTerminate() {
    PostQuitMessage(0);
}

void SConnection::OnNsEvent(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Msg *pMsg = new Msg;
    pMsg->hwnd = hWnd;
    pMsg->message = message;
    pMsg->wParam = wParam;
    pMsg->lParam = lParam;
    postMsg(pMsg);
}

void SConnection::OnNsActive(HWND hWnd, BOOL bActive) {
    if(!bActive){
        if(m_hFocus){
            if(hWnd == m_hFocus || IsChild(hWnd,m_hFocus))
            {
                SetFocus(0);
            }
        }
    }
    if(bActive)//record last active window
        m_hActive = hWnd;
    SendMessageA(hWnd, WM_ACTIVATE, bActive?1:0, 0);
}

void SConnection::OnDrawRect(HWND hWnd, const RECT &rc, cairo_t *ctx){
  WndObj wndObj = WndMgr::fromHwnd(hWnd);
  if(!wndObj)
    return;
  assert(wndObj->hdc);
  cairo_t * oldCtx = wndObj->hdc->cairo;
  wndObj->hdc->cairo = ctx;
  HRGN oldRgn = wndObj->invalid.hRgn;
  wndObj->invalid.hRgn = CreateRectRgnIndirect(&rc);
  SendMessage(hWnd, WM_ERASEBKGND, (LPARAM)wndObj->hdc,0);
  SendMessage(hWnd, WM_PAINT, 0,(LPARAM)wndObj->invalid.hRgn);
  wndObj->hdc->cairo = oldCtx;
  DeleteObject(wndObj->invalid.hRgn);
  wndObj->invalid.hRgn = oldRgn;
}

void SConnection::updateWindow(HWND hWnd, const RECT &rc){
    updateNsWindow(hWnd,rc);
}

void SConnection::commitCanvas(HWND hWnd, const RECT &rc){
    updateNsWindow(hWnd,rc);
}


void SConnection::BeforeProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (m_msgPeek && m_bMsgNeedFree && m_msgPeek->hwnd == hWnd && m_msgPeek->message == msg && m_msgPeek->wParam == wp && m_msgPeek->lParam == lp)
    {
        m_msgStack.push_back(m_msgPeek);
        m_msgPeek = nullptr;
        m_bMsgNeedFree = false;
    }
}

void SConnection::AfterProcMsg(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT res)
{
    if (m_msgStack.empty() || m_bMsgNeedFree)
        return;
    Msg *lastMsg = m_msgStack.back();

    if (lastMsg->hwnd == hWnd && lastMsg->message == msg && lastMsg->wParam == wp && lastMsg->lParam == lp)
    {
        lastMsg->SetResult(res);
        m_msgStack.pop_back();
        delete lastMsg;
    }
}
