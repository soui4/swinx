#ifndef SWINX_VERSION_H_
#define SWINX_VERSION_H_

// ===========================================================================
// swinx 版本号（定义在源码中，供运行时 / 文件名拼接使用）。
//
// 版本历史见文件末尾 CHANGELOG。当前版本由 SWINX_VERSION_MAJOR / MINOR /
// PATCH 三个数值宏描述，SWINX_VERSION_STRING 由它们自动拼接
// （如 主=1 次=7 修=0 -> "1.7.0"），修改任一数值宏后字符串自动同步，
// 无需手动维护。
//
// 如需与构建系统联动（例如 cmake 中按 release tag 注入），可用
//   add_definitions(-DSWINX_VERSION_MAJOR=... -DSWINX_VERSION_MINOR=... -DSWINX_VERSION_PATCH=...)
// 在编译期覆盖下列数值默认值；SWINX_VERSION_STRING 由这三个数值自动拼接，
// 无需（也不应）单独覆盖，避免版本号与字符串不一致。
// ===========================================================================
#define SWINX_VERSION_MAJOR 1
#define SWINX_VERSION_MINOR 8
#define SWINX_VERSION_PATCH 0

// 版本字符串：由上述三个数值宏自动拼接（如 主=1 次=2 修=3 -> "1.2.3"），
// 修改任一数值宏后字符串自动同步，无需手动维护。
// 两层 STRINGIFY 是必需的：内层先把参数展开为数字 token，外层再 # 字符串化；
// 若只有一层，#x 会把 "SWINX_VERSION_MAJOR" 整个当成字面量而非其取值。
#define SWINX_VERSION_STRINGIFY_(x) #x
#define SWINX_VERSION_STRINGIFY(x)  SWINX_VERSION_STRINGIFY_(x)
#define SWINX_VERSION_STRING \
    SWINX_VERSION_STRINGIFY(SWINX_VERSION_MAJOR) "." \
    SWINX_VERSION_STRINGIFY(SWINX_VERSION_MINOR) "." \
    SWINX_VERSION_STRINGIFY(SWINX_VERSION_PATCH)

// ===========================================================================
// CHANGELOG（原 swinx/version.txt，已并入本文件）
//   0.1   2025/1/12  初步实现框架搭建。
//   1.0   2025/5/3   完全swinx的框架搭建，实现soui的跨平台。
//   1.5   2026/9/13  完成移动端支持，完成IAccessible接口跨平台，完成内存泄漏检测，线程安全检测。
//   1.6   2026/10/8  解决子窗口剪裁的BUG，解决在渲染Scintilla控件时发现一组GDI API BUG，实现输入法光标跟随，修复移动端文件锁支持等。
//   1.7   2026/10/18 增加opengl API，完善ACC支持。
//   1.8   2026/10/26 初步完成freeRTOS适配，补充完成WKE浏览器适配需要的API
// ===========================================================================

#endif // SWINX_VERSION_H_
