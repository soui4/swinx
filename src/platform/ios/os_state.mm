#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include "os_state.h"
#include <windows.h>
#undef interface
#include <log.h>
#define kLogTag "os_state"

// iOS 平台 os_state：
// 与 macOS 不同，iOS 的 UIApplication 必须由宿主 App 的 main() 通过
// UIApplicationMain 创建。swinx 无法在构造函数中创建 UIApplication。
// 因此 OsState 通过 NSNotificationCenter 观察 UIApplication 生命周期通知，
// 在 UIApplicationWillTerminateNotification 时回调 onTerminate。
// 要求：SConnMgr::instance() 首次调用发生在 UIApplication 创建之后
// （即在 AppDelegate 的 didFinishLaunching 之后）。

@interface SwinxOsObserver : NSObject
@property (nonatomic, assign) SConnBase *listener;
@end

@implementation SwinxOsObserver

- (void)onTerminate:(NSNotification *)note {
    if (self.listener) {
        self.listener->onTerminate();
    }
}

@end

namespace swinx {
class OsState {
  protected:
  SwinxOsObserver *m_observer;
  public:
  OsState();
  ~OsState();

  void setListener(SConnBase *pListener) {
    m_observer.listener = pListener;
  }
};

OsState::OsState() : m_observer(nil) {
  @autoreleasepool {
    SwinxOsObserver *observer = [[SwinxOsObserver alloc] init];
    observer.listener = nullptr;
    [[NSNotificationCenter defaultCenter] addObserver:observer
                                             selector:@selector(onTerminate:)
                                                 name:UIApplicationWillTerminateNotification
                                               object:nil];
    m_observer = observer;
  }
}

OsState::~OsState() {
  if (m_observer) {
    @autoreleasepool {
      [[NSNotificationCenter defaultCenter] removeObserver:m_observer];
    }
  }
}

static OsState *s_OsState = nullptr;

bool init(SConnBase *pListener) {
  if (s_OsState == nullptr)
    s_OsState = new OsState();
  s_OsState->setListener(pListener);
  return true;
}
void shutdown() {
  if (s_OsState == nullptr)
    return;
  s_OsState->setListener(nullptr);
}

} // namespace swinx
