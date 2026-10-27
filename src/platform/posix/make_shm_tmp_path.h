#include <stdio.h>
#include <swinx_version.h>

// ===========================================================================
// swinx 版本号（定义在源码中，供运行时 / 文件名拼接使用）。
//
// 当 swinx 的共享内存布局或临时文件命名方式发生变化时，请同步提高本版本号；
// 不同版本的 swinx 会生成不同文件名的临时共享内存文件，避免同一系统中
// 多版本 swinx 共存时互相干扰（详见 src/platform/posix/sharedmem.cpp 与
// src/platform/posix/sysobjs.cpp 中 /data/local/tmp/soui_shm_* 的回退路径）。
//
// 如需与构建系统联动（例如 cmake 中按 release tag 注入），可用
//   add_definitions(-DSWINX_VERSION_MAJOR=... -DSWINX_VERSION_MINOR=... -DSWINX_VERSION_PATCH=...)
// 在编译期覆盖下列数值默认值。SWINX_VERSION_STRING 由这三个数值自动拼接，
// 无需（也不应）单独覆盖，修改任一数值宏后字符串自动同步，避免版本号与
// 字符串不一致。
// ===========================================================================

inline void swinx_make_shm_tmp_path(char *buf, size_t buflen,
                                    const char *name, int pid)
{
    snprintf(buf, buflen, "/data/local/tmp/soui_v%s_%s_%d",
             SWINX_VERSION_STRING, name ? name : "anon", pid);
}
