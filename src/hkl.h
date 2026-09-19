#ifndef _SWINX_HKL_H_
#define _SWINX_HKL_H_

/* swinx 内部约定：键盘布局句柄（HKL）的取值编码
 *
 * 句柄 = 布局索引 + SWINX_HKL_BASE，恒 >= SWINX_HKL_BASE。
 * 必须避开 Win32 魔法值 HKL_PREV(0) / HKL_NEXT(1)（见 include/winuser.h），
 * 否则把布局句柄传回 ActivateKeyboardLayout 会被误判成“上一个/下一个布局”；
 * 0 另有用途——各平台用它表示“尚未与真实布局同步”，GetKeyboardLayout 会惰性初始化。
 *
 * 本文件位于 src/，是**实现细节，不是公共 API**：HKL 的具体取值属各平台 SConnection 的
 * 内部状态，不随 include/ 安装出去——公共头只提供 Win32 兼容面（HKL 类型与两个魔法值）。
 * 实现在 src/platform/<平台>/SConnection.*，四个平台共用本约定以免编码再次分叉
 * （历史上 cocoa 用过 4、其余用裸索引，就是分叉导致的缺陷）。
 */

#include <ctypes.h>

#define SWINX_HKL_BASE ((DWORD)2)

#endif // _SWINX_HKL_H_
