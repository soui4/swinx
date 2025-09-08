#import <Cocoa/Cocoa.h>
#include "os_state.h"
#include <windows.h>
#undef interface
#include <log.h>
#define kLogTag "os_state"

@interface SwinXApplication : NSApplication {
@public
  SConnBase *m_pOsListener;
}
- (void)terminate:(id)sender;

@end

@implementation SwinXApplication

// Override terminate to handle Quit and System Shutdown smoothly.
- (void)terminate:(id)sender {
  m_pOsListener->onTerminate();
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate

- (void)applicationWillFinishLaunching:(NSNotification *)notification {

}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    // 应用程序启动完成后的处理
    NSLog(@"Application finished launching");
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  return YES;
}

-(void) applicationDidResignActive:(NSNotification *)notification {
   SLOG_STMI()<<"hjx applicationDidResignActive";
}

-(void) applicationDidBecomeActive:(NSNotification *)notification {
   SLOG_STMI()<<"hjx applicationDidBecomeActive";
}

- (NSMenu *)applicationDockMenu:(NSApplication *)sender {
  return [self createDockMenu];
}

- (void)initDockMenu:(NSMenu *)dockMenu hmenu:(HMENU)hMenu {
  for (int i = 0; i < GetMenuItemCount(hMenu); i++) {
    MENUITEMINFOA menuItemInfo = {0};
    char szText[256] = {0};
    menuItemInfo.dwTypeData = szText;
    menuItemInfo.cch = sizeof(szText);
    menuItemInfo.cbSize = sizeof(MENUITEMINFO);
    menuItemInfo.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID | MIIM_STRING| MIIM_SUBMENU;
    BOOL result = GetMenuItemInfoA(hMenu, i, TRUE, &menuItemInfo);
    if (result) {
      if (menuItemInfo.fType == MFT_SEPARATOR) {
        [dockMenu addItem:[NSMenuItem separatorItem]];
      } else {
        [NSString stringWithUTF8String:menuItemInfo.dwTypeData];
        NSMenuItem *item = [[NSMenuItem alloc]
            initWithTitle:[NSString
                              stringWithUTF8String:menuItemInfo.dwTypeData]
                   action:@selector(handleDockMenuItemClick:) 
            keyEquivalent:@""];
        [item setTarget:self];
        [item setEnabled:menuItemInfo.fState & MF_ENABLED];
        if (menuItemInfo.hSubMenu) {
          NSMenu *subMenu = [[NSMenu alloc] initWithTitle:@"Dock Sub Menu"];
          [self initDockMenu:subMenu hmenu:menuItemInfo.hSubMenu];
          [item setSubmenu:subMenu];
        } else {
          [item setTag:menuItemInfo.wID];
        }
        [dockMenu addItem:item];
      }
    }
  }
}

- (NSMenu *)createDockMenu {
    HWND hWnd = GetActiveWindow();
    if (!hWnd) {
        return nil;
    }
    HMENU hMenu = GetSystemMenu(hWnd, FALSE);
    if (!hMenu) {
        return nil;
    }
    NSMenu *dockMenu = [[NSMenu alloc] initWithTitle:@"Dock Menu"];
    [self initDockMenu:dockMenu hmenu:hMenu];
    return dockMenu;
}

- (void)handleDockMenuItemClick:(NSMenuItem *)sender {
  HWND hWnd = GetActiveWindow();
  if (!hWnd) {
    return;
  }
  PostMessageA(hWnd, WM_SYSCOMMAND, sender.tag, 0);
}

@end


namespace swinx {
class OsState {
  protected:
  SwinXApplication *m_nsApp;
  AppDelegate *m_appDelegate;
  public:
  OsState();
  ~OsState();

  void setListener(SConnBase *pListener) {
    m_nsApp->m_pOsListener = pListener;
  }
  
};

OsState::OsState() : m_nsApp(nullptr), m_appDelegate(nullptr) {
  @autoreleasepool {
    SwinXApplication *nsApp = [SwinXApplication sharedApplication];
    [nsApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [nsApp activateIgnoringOtherApps:YES];
    AppDelegate *appDelegate = [[AppDelegate alloc] init];
    [nsApp setDelegate:appDelegate];
    [nsApp finishLaunching];
    m_nsApp = nsApp;
    m_appDelegate = appDelegate;
  }
}
OsState::~OsState() {
}

static OsState *s_OsState = nullptr;

__attribute__((constructor)) void OnSwinxInit(){
  s_OsState = new OsState();
}

__attribute__((destructor)) void OnSwinxUninit(){
  delete s_OsState;
}

bool init(SConnBase *pListener) {
  if (s_OsState == nullptr)
    return false;
  s_OsState->setListener(pListener);
  return true;
}
void shutdown() {
  if (s_OsState == nullptr)
    return;
  s_OsState->setListener(nullptr);
}

} // namespace swinx