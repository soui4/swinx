## CountMutex 设计说明

> 设计意图由作者提供，使用点经代码走读核对（countmutex.h、wnd.cpp:872、platform/linux/SConnection.cpp:992）。

### A.1 问题背景：回调重入死锁

Win32 的窗口消息模型中，DispatchMessage/CallWindowProc 由系统侧调用应用注册的窗口过程。swinx 用锁保护窗口对象（`_Window : public CountMutex`）的内部状态，消息派发时自然持有该锁。但业务层的窗口过程内会回头调用 swinx 的 API——GetProp/SetWindowText/对（相关）窗口的 SendMessage 等，这些 API 又会对同一窗口对象加锁。若派发期间一直持锁，业务线程会在自己的回调里自我死锁。

真实 Windows 没有这个问题：USER32 的内部锁在进入窗口过程之前就已释放。CountMutex 的 FreeLock/RestoreLock 就是对这一语义的适配——**在回调窗口期完全释放锁，回调返回后恢复原持有深度**。

### A.2 机制：递归锁 + 深度计数

```cpp
class CountMutex : public std::recursive_mutex {
    LONG cLock;          // 当前持有线程的重入深度
public:
    virtual void lock()   { std::recursive_mutex::lock();   cLock++; }
    virtual void unlock() { cLock--; std::recursive_mutex::unlock(); }
    LONG  FreeLock();     // 完全释放：保存 cLock 并解锁至 0，返回保存值
    void  RestoreLock(LONG preLock);  // 重新加锁至 cLock == preLock
    LONG  getLockCount() const;       // 诊断用：查询当前深度
};
```

- `FreeLock()`：记录当前重入深度（返回值 `preLock`），循环 `unlock()` 直至锁完全释放（cLock=0），返回 `preLock`。调用前必须已持有锁（此时深度必 ≥1）。
- `RestoreLock(preLock)`：循环 `lock()` 直至深度恢复到 `preLock`。若期间其它线程获得了锁，此处会阻塞等待——这正是期望的互斥行为。

递归锁本身只能保证"同线程重复加锁不死锁"，**不能**实现"回调窗口期降到 0 再恢复到任意深度"——`lock_guard`/`unique_lock` 都没有"一次清空、按值恢复"的能力。这是保留自定义类而非直接使用标准设施的原因。

### A.3 使用点（当前两处）

1. **窗口消息派发**（wnd.cpp `CallWindowObjProc`）：

```cpp
LONG cLock = wndObj->FreeLock();
ret = proc(hWnd, msg, wp, lp);        // 业务窗口过程：可重入任意 swinx API
wndObj->RestoreLock(cLock);
```

2. **WM_TIMER 回调**（linux/SConnection.cpp `peekMsg`）：`SetTimer` 带 TIMERPROC 的定时器在消息泵内直接回调，同样先 `FreeLock` 再调 `proc` 再 `RestoreLock`，保证定时器回调内可重入。

### A.4 使用约束（不变式）

- `FreeLock`/`RestoreLock` 必须在**同一线程**、**配对**调用，且调用 FreeLock 时必须已持有该锁（recursive_mutex 所有权约束；cLock 与所有权同线程绑定）。
- `preLock` 必须原样传回 `RestoreLock`；两组 FreeLock/RestoreLock 不得交叉嵌套。
- **回调窗口期内对象处于无锁状态**，其它线程可在此期间修改对象状态。这与真实 Win32 语义一致（窗口过程执行时系统并不持有窗口内部锁），是特性而非缺陷；但新增"回调窗口期"调用点时必须显式意识到，不能假设回调期间对象状态不变。
- `getLockCount()` 仅在持锁状态下读取才有意义。