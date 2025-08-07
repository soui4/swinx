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
class OsState {
  protected:
  SwinXApplication *m_nsApp;
  public:
  OsState();
  ~OsState();

  void setListener(SConnBase *pListener) {
    m_nsApp->m_pOsListener = pListener;
  }
};

OsState::OsState() : m_nsApp(nullptr) {
  @autoreleasepool {
    SwinXApplication *nsApp = [SwinXApplication sharedApplication];
    [nsApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [nsApp activateIgnoringOtherApps:YES];
    AppDelegate *appDelegate = [[AppDelegate alloc] init];
    [nsApp setDelegate:appDelegate];
    [nsApp finishLaunching];
    m_nsApp = nsApp;
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
