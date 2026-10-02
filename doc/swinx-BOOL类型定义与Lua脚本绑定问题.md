## swinx BOOL 类型定义与 Lua 脚本绑定问题记录

> 本文记录一次由"平台 BOOL 类型宽度不一致"引发的 macOS 上层脚本 BUG 的完整排查与定论，供以后在 macOS/Apple 平台处理 BOOL 语义、lua_tinker 绑定或跨编译单元结构布局问题时参考。结论先行：**macOS 上不要试图把 swinx 暴露的 BOOL 统一为 int**，根因类问题应在绑定层（lua_tinker）修复。

### 1. 现象

SOUI4 demo 的消消乐游戏（Lua 脚本实现）在 macOS 上出现：交换两个棋子后，两个棋子同时消失，日志输出：

```
XXL settle FAIL: cell hidden id=30036 swnd=1217.0
```

同一份代码在 Windows（x64/x86）、Ubuntu（x64）上行为正常；同机的"跑马机"游戏（同为 Lua）正常。

### 2. 根因

#### 2.1 BOOL 在 Apple 平台不是 int

| 平台 | BOOL 定义 | 宽度 |
| --- | --- | --- |
| Windows（windef.h） | `typedef int BOOL;` | 4 字节 |
| Linux（swinx ctypes.h） | `typedef int BOOL;` | 4 字节 |
| macOS/iOS（系统 objc/objc.h） | `typedef signed char BOOL;`（macOS）/ `typedef bool BOOL;`（iOS 64 位） | 1 字节 |

macOS 下 Lua 脚本里 `SetVisible(true, true)` 这类调用，第二个 `true` 经 lua_tinker 解析：

- lua_tinker 的通用 `read<T>` 主模板走 `lua_tonumber`，`true` 被转成 **1**，再按目标类型截断。
- 没有 `read<signed char>` 特化时，`signed char` 走主模板 → `lua_tonumber(true) = 1` → 本应得到 `true`，实际得到 **1**（正确）；真正的坑是反过来：当 Lua 侧传的是 `0` 之外的布尔语义时，枚举/开关类参数被 `lua2enum<signed char>` 之类的转换把布尔值误解为数值。

具体链路：`SetVisible(true, true)` 的第二个参数在脚本里是布尔开关，被当成数值 0 处理 → 隐藏棋子 → 交换后棋子不显示，报 `cell hidden`。

#### 2.2 修复（已提交，位于 ScriptModule-LUA）

在 `lua_tinker` 中新增显式特化，让 `signed char`（以及 `bool` 语义）按 Lua 布尔语义解析：

- `components/ScriptModule-LUA/lua_tinker/lua_tinker.h:345` — 声明 `template<> signed char read(lua_State*, int);`
- `components/ScriptModule-LUA/lua_tinker/lua_tinker.cpp:363-367` — `lua_tinker::read<signed char>` 实现，用 `lua_toboolean`/`lua_tonumber` 语义返回正确的值。

### 3. 曾尝试：把 swinx 的 BOOL 统一为 int —— 代价过高，不可行

为了"从源头杜绝同类问题"，曾尝试在 swinx 的 `include/ctypes.h` 中把 Apple 平台的 BOOL 全部定义为 `int`。失败原因有两层，均不可绕过：

#### 3.1 系统 objc/objc.h 无法被覆盖

系统头 `objc/objc.h` 在它的 include guard **内部**、接近文件末尾处才定义 `OBJC_BOOL_DEFINED`：

```objc
// objc/objc.h（节选）
#ifndef OBJC_BOOL_DEFINED
typedef bool BOOL;           // 或 signed char BOOL，取决于平台
#define OBJC_BOOL_DEFINED 1
#endif
```

`#undef BOOL` 对 typedef 无效（typedef 不是宏）。任何已经通过

```
CoreFoundation/CoreGraphics → dispatch → fcntl → os/object.h → objc/NSObject.h → objc.h
```

链式包含过 `objc.h` 的编译单元，再想 `typedef int BOOL` 都会触发 **typedef redefinition** 编译错误。而 ObjC/ObjC++ 编译单元必然命中该链（`OS_OBJECT_USE_OBJC` 在 ObjC 模式下为 1），所以 ObjC++ 侧的 BOOL 宽度是系统强制的，改不动。

#### 3.2 跨编译单元的结构布局约束（真正的"代价"）

swinx 内部结构（`src/wndobj.h` 的 `_Window`、`ScrollBar`）含多个 BOOL 成员：

```cpp
class _Window : public CountMutex {
    ...
    BOOL bAutoDblClick;   // 4B 或 1B
    ...
    BOOL bDestroyed;
    BOOL bCaretVisible;
    struct { HRGN hRgn; BOOL bErase; } invalid;
    ...
    WNDPROC winproc;      // 关键字段，紧随多个 BOOL 成员之后
    ...
};
```

这些结构由**纯 C++ 编译单元**（`src/wnd.cpp` 等，BOOL 走 ctypes.h 分支）与 **ObjC++ 编译单元**（`src/platform/cocoa/SConnection.mm`、`SNsWindow.mm`，BOOL 走系统 objc.h 分支）跨文件共享同一份内存布局。若纯 C++ 侧 BOOL 为 4 字节、ObjC++ 侧为 1 字节，则 `sizeof(_Window)` 与 `offsetof(_Window, winproc)` 在两类编译单元中不一致——ObjC++ 侧写好的 `winproc` 指针，纯 C++ 侧按错误偏移读取，得到垃圾值。

**后果（实测回归）**：demo 启动即崩溃：

```
Exception Type: EXC_BAD_ACCESS (SIGBUS)
Exception Codes: KERN_PROTECTION_FAILURE at 0x000000011251de80  ← 只读 __LINKEDIT 区域
Thread 0: ShowWindow → SendMessageA → _SendMessageTimeout → CallWindowProcPriv (wnd.cpp:1281)
          → 调用损坏的 proc 指针
```

崩溃地址落在只读的 mapped file 区域，即执行了数据区里的垃圾指针，与"winproc 偏移错位"完全吻合。

**验证数据**（真实编译标志探针，修复后）：

| 探针编译模式 | sizeof(_Window) | offsetof(_Window, winproc) | sizeof(ScrollBar) | offsetof(ScrollBar, bDraging) |
| --- | --- | --- | --- | --- |
| 纯 C++（同 wnd.cpp） | 472 | 360 | 44 | 37 |
| ObjC++（同 SConnection.mm） | 472 | 360 | 44 | 37 |

修复前纯 C++ 侧为 `int BOOL` 时，上述偏移必然与 ObjC++ 侧不同。

### 4. 定论与后续参考

1. **macOS 上 BOOL 保持 1 字节（signed char），不要统一为 int。** `include/ctypes.h` 的 Apple 分支因此写成"全编译单元镜像系统 BOOL"：
   - ObjC/ObjC++（`__OBJC__`）或已含 objc.h（`OBJC_BOOL_DEFINED`）→ 镜像系统定义（macOS `signed char`，iOS 64 位 `bool`）；
   - 纯 C/C++ 编译单元 → 同样 `signed char`，**不是 int**——这是为了与 ObjC++ 侧保持同一结构布局。
   - 其余平台（Windows/Linux）维持 `int BOOL`。
2. **同类 Lua/JS 绑定问题一律在绑定层修**：lua_tinker 对 `signed char`/`bool` 等窄类型加显式 `read`/`push` 特化，按 Lua 布尔语义转换，不要反向改底层类型。
3. **凡是在 Apple 平台跨编译单元共享的结构体**（wndobj.h 的 `_Window`/`ScrollBar`、SImContext.h 的 `IMContext` 等），成员类型宽度必须在纯 C++ 与 ObjC++ 两种编译模式下完全一致；改动任何成员类型前先核对 `sizeof`/`offsetof` 探针。
4. **排查清单**：遇到"macOS 行为与 Windows/Linux 不一致、且与布尔/开关参数相关"的脚本层问题，优先怀疑 BOOL 宽度与绑定层转换，按第 2 条修复。

### 5. 涉及文件

- `components/ScriptModule-LUA/lua_tinker/lua_tinker.h` / `lua_tinker.cpp` — `read<signed char>` 特化（正解，已提交）。
- `swinx/include/ctypes.h` — BOOL 定义与 Apple 分支说明注释。
- `swinx/src/wndobj.h` — `_Window`/`ScrollBar`，跨 TU 布局敏感结构。
- `swinx/src/wnd.cpp`、`swinx/src/platform/cocoa/SConnection.mm`、`SNsWindow.mm` — 纯 C++ 与 ObjC++ 两类编译单元的代表。
