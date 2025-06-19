

#import <Cocoa/Cocoa.h>
#import <Foundation/NSDebug.h>
#include "os_state.h"
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

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
  return YES;
}

-(void) applicationDidResignActive:(NSNotification *)notification {
   SLOG_STMI()<<"hjx applicationDidResignActive";
}

-(void) applicationDidBecomeActive:(NSNotification *)notification {
   SLOG_STMI()<<"hjx applicationDidBecomeActive";
}
@end


namespace swinx {
struct OsState {
  void *application;
  SConnBase *m_pOsListener;
  OsState(SConnBase *pListener);
  ~OsState();
};

OsState::OsState(SConnBase *pListener) : m_pOsListener(pListener) {
  @autoreleasepool {
      NSDebugEnabled=        YES;
      NSZombieEnabled=    YES;
    SwinXApplication *nsApp = [SwinXApplication sharedApplication];
    [nsApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    AppDelegate *appDelegate = [[AppDelegate alloc] init];
    [nsApp setDelegate:appDelegate];
    [nsApp finishLaunching];
    nsApp->m_pOsListener = pListener;
    application = (__bridge void *)nsApp;
  }
}
OsState::~OsState() {
  SwinXApplication *nsApp = (__bridge SwinXApplication *)application;
  [nsApp terminate:nil];
}

static OsState *s_OsState = nil;

bool init(SConnBase *pListener) {
  if (s_OsState != nil)
    return false;
  s_OsState = new OsState(pListener);
  return true;
}
void shutdown() {
  if (s_OsState == nil)
    return;
  delete s_OsState;
  s_OsState = nil;
}

} // namespace swinx
