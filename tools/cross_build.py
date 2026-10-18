#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""swinx 移动端交叉编译门禁。

只编 swinx 目标（compat 层本身 + 它自带的 thirdparty），不拉 SOUI 的其余部分：
这样一次完整校验只需几十秒到几分钟，适合放到 PR 门禁里跑。Windows 上 swinx
本来就不参与编译，移动端源码（src/platform/mobile、src/gdi/cairo、src/cmnctl32）
在开发机上等于零覆盖 —— 这个脚本就是那条"能编过"的底线。

用法：
  python tools/cross_build.py android [--sdk <NDK 根目录>] [--abi arm64-v8a]
                                     [--api 28] [--build <目录>] [--jobs 8]
  python tools/cross_build.py ohos    [--sdk <SDK native 目录>] [--abi arm64-v8a]
                                     [--build <目录>] [--jobs 8]
  python tools/cross_build.py fetch-ohos-sdk [--url <tar.gz>] --dest <目录>

  --sdk 省略时依次尝试环境变量：
      android: ANDROID_NDK_HOME, ANDROID_NDK_LATEST_HOME, ANDROID_NDK_ROOT,
               以及 $ANDROID_HOME/ndk/<最新版本>
      ohos:    OHOS_SDK_NATIVE_HOME, OHOS_SDK_HOME, DEVECO_SDK_HOME

关于 Android API 级别：必须 >= 28。swinx 用到的 iconv（API 28）与
ASharedMemory_*（API 26）在更低的 platform 上压根没有声明，编译会直接失败；
28 也正是下游 app（soui-android-lib / cnchess）的 minSdk。

fetch-ohos-sdk 是给 CI 用的：OpenHarmony 公开 SDK 是一个 3GB 的 tar.gz，里面
真正需要的只有 ohos-sdk/linux/native-linux-x64-*.zip（约 0.8~1GB）。这里用流式
解包，读到需要的成员就停，剩下的字节不再下载，也不用落盘整个 tar.gz。

注意解包时必须还原创作者写在 zip 里的两样东西（Windows 开发机都看不出来，
只有 Linux runner 会撞上）：
  * unix 权限位 —— zipfile.extractall() 不还原，llvm/bin/* 从 0755 变 0644，
    Linux 上直接 `clang: Permission denied`；
  * 符号链接 —— zip 把链接存成"内容为目标路径的普通条目"，extractall() 就落成
    一个存着 "clang-15" 的文本文件（chmod 也救不了）。native-linux zip 里有
    36 个这种链接（llvm/bin/clang、clang++、ld.lld、llvm/lib/*.so ...），
    不重建的话 clang 根本不能执行、共享库也链接不上。

退出码：0 = 配置+编译+产物校验全通过；非 0 = 失败（CI 直接用进程退出码）。
"""

import argparse
import fnmatch
import glob
import os
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SWINX_ROOT = os.path.dirname(HERE)

# ELF e_machine -> 可读名
ELF_MACHINE = {0x28: "arm", 0xB7: "aarch64", 0x3E: "x86_64", 0x03: "x86"}

ANDROID_MIN_API = 28

# 公开 OpenHarmony SDK（免注册）。与下游 DevEco 侧 SDK 大版本对齐：6.1-LTS。
DEFAULT_OHOS_SDK_URL = ("https://repo.huaweicloud.com/harmonyos/os/6.1-LTS/"
                        "ohos-sdk-windows_linux-public.tar.gz")
OHOS_NATIVE_MEMBER = "*/linux/native-linux-*.zip"


def log(msg):
    print("[cross_build] %s" % msg, flush=True)


def fail(msg):
    print("[cross_build] FAIL: %s" % msg, flush=True)
    sys.exit(1)


def first_existing(paths):
    for p in paths:
        if p and os.path.exists(p):
            return p
    return None


def latest_subdir(path):
    """取目录下版本号最大的子目录（NDK 常见布局）。"""
    if not os.path.isdir(path):
        return None
    subs = [os.path.join(path, d) for d in os.listdir(path)]
    subs = [d for d in subs if os.path.isdir(d)]
    if not subs:
        return None

    def key(p):
        parts = os.path.basename(p).replace("-", ".").split(".")
        return [int(s) if s.isdigit() else 0 for s in parts]

    subs.sort(key=key)
    return subs[-1]


def resolve_android_ndk(explicit):
    cands = [explicit]
    for env in ("ANDROID_NDK_HOME", "ANDROID_NDK_LATEST_HOME", "ANDROID_NDK_ROOT"):
        cands.append(os.environ.get(env))
    for env in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        sdk = os.environ.get(env)
        if sdk:
            cands.append(latest_subdir(os.path.join(sdk, "ndk")))
    ndk = first_existing(cands)
    if not ndk:
        fail("找不到 Android NDK；用 --sdk 指定，或设置 ANDROID_NDK_HOME")
    return ndk


def ohos_toolchain(sdk):
    """SDK 根、或 SDK/native 都能传进来；返回 (toolchain 文件, native 根)。"""
    for native in (sdk, os.path.join(sdk, "native")):
        tc = os.path.join(native, "build", "cmake", "ohos.toolchain.cmake")
        if os.path.exists(tc):
            return tc, native
    fail("在 %s 下找不到 build/cmake/ohos.toolchain.cmake" % sdk)


def resolve_ohos_sdk(explicit):
    cands = [explicit]
    for env in ("OHOS_SDK_NATIVE_HOME", "OHOS_SDK_HOME", "DEVECO_SDK_HOME"):
        cands.append(os.environ.get(env))
    sdk = first_existing(cands)
    if not sdk:
        fail("找不到 OHOS native SDK；用 --sdk 指定，或设置 OHOS_SDK_NATIVE_HOME")
    return sdk


def run(cmd):
    log(" ".join('"%s"' % c if " " in c else c for c in cmd))
    return subprocess.call(cmd)


def elf_machine(path):
    try:
        with open(path, "rb") as fp:
            head = fp.read(20)
    except Exception:
        return None
    if len(head) < 20 or head[:4] != b"\x7fELF":
        return None
    endian = "<" if head[5] == 1 else ">"
    (machine,) = struct.unpack(endian + "H", head[18:20])
    return ELF_MACHINE.get(machine, "0x%x" % machine)


def verify_artifacts(build_dir, expect_machine):
    """在 build 目录里找 libswinx 产物，校验存在、非空且架构正确。

    swinx 默认（SOUI_ENABLE_CORE_LIB=OFF）是 SHARED，产物名可能是
    libswinx.so / swinx.dll；静态库是 libswinx.a。这里按前缀匹配。
    """
    hits = []
    for root, _dirs, files in os.walk(build_dir):
        for f in files:
            if not (f.startswith("libswinx.") or f.startswith("swinx.")):
                continue
            if f.endswith((".so", ".dylib", ".dll", ".a")):
                hits.append(os.path.join(root, f))
    if not hits:
        fail("没有找到 libswinx 产物，构建可能悄悄失败了")
    for p in sorted(hits):
        size = os.path.getsize(p)
        if size < 1024:
            fail("产物过小（%d 字节）：%s" % (size, p))
        mach = elf_machine(p)
        if expect_machine and mach and mach != expect_machine:
            fail("产物架构不符：%s 是 %s，期望 %s" % (p, mach, expect_machine))
        log("产物 %s (%d 字节%s)" % (p, size, "，ELF %s" % mach if mach else ""))
    return len(hits)


def _chmod_x(path):
    try:
        os.chmod(path, 0o755)
        return True
    except OSError:
        return False


def ensure_executables(dir_list, recursive=False):
    """补齐丢失的可执行位，返回修复个数。

    三种来源都会丢 +x：zipfile.extractall（不解 external_attr 的权限位）、
    tarfile 在 Windows 上解包、CI 缓存里躺着旧版本解出来的树。表现是
    Linux 上 `clang: Permission denied`，而 Windows 开发机根本不看这个位，
    本地永远复现不出来 —— 所以只能显式修。

    判据刻意收窄：只认「无扩展名」或 `.sh` 的文件，且无扩展名的还要求是
    ELF。这样 llvm/bin/* 能补上，lib/*.so、cmake 脚本、头文件不会被误伤。
    Windows 上 os.access(X_OK) 恒为真（无此概念），因此这里是空操作。
    """
    fixed = 0
    for base in dir_list:
        if not base or not os.path.isdir(base):
            continue
        walker = os.walk(base) if recursive else [(base, [], os.listdir(base))]
        for root, _dirs, files in walker:
            for f in files:
                p = os.path.join(root, f)
                if not os.path.isfile(p) or os.access(p, os.X_OK):
                    continue
                if f.endswith(".sh"):
                    fixed += 1 if _chmod_x(p) else 0
                    continue
                if "." in f:
                    continue
                try:
                    with open(p, "rb") as fp:
                        if fp.read(4) != b"\x7fELF":
                            continue
                except OSError:
                    continue
                fixed += 1 if _chmod_x(p) else 0
    return fixed


def _extract_entry(z, zi, dest):
    """解单个 zip 项；符号链接项必须重建链接而不是落成文本文件。

    zip 里符号链接存的是"内容是目标路径的普通条目"，unix 模式记在
    external_attr 的 S_IFLNK。zipfile.extract() 不认这个，会把
    llvm/bin/clang 写成内容为 "clang-15" 的文本文件 —— 既不是可执行位
    的问题（chmod 也救不了），执行时报 Exec format error。
    """
    path = os.path.join(dest, zi.filename)
    fmode = (zi.external_attr >> 16) & 0o170000
    if fmode == 0o120000 and os.name != "nt":
        target = z.read(zi).decode("utf-8", "surrogateescape")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.lexists(path):
            os.remove(path)
        try:
            os.symlink(target, path)
            return
        except OSError:
            pass  # 退化成普通文件（与 zipfile 默认行为一致）
    z.extract(zi, dest)


def extract_zip(zip_path, dest):
    """解包 zip，还原符号链接与 unix 权限位。

    不能直接用 extractall()：它既不还原 external_attr 里的权限位，也不重建
    符号链接。前者让 llvm/bin/* 从 0755 变 0644（Linux 上 `clang: Permission
    denied`），后者让 clang 变成一个存着 "clang-15" 的文本文件。两者在
    Windows 上都看不出来（无 +x 概念、无 POSIX 链接），只有 CI 会撞上。
    """
    with zipfile.ZipFile(zip_path) as z:
        for zi in z.infolist():
            _extract_entry(z, zi, dest)
            if zi.is_dir():
                continue
            if ((zi.external_attr >> 16) & 0o170000) == 0o120000:
                continue  # 链接本身没有权限位，权限由目标文件决定
            mode = (zi.external_attr >> 16) & 0o7777
            if mode & 0o111:
                try:
                    os.chmod(os.path.join(dest, zi.filename), mode & 0o777)
                except OSError:
                    pass


def _describe_path(path):
    """给诊断用的文件画像：类型、权限、大小、符号链接目标、文件头。"""
    try:
        st = os.lstat(path)
    except OSError as e:
        return "stat 失败（%s）" % e
    if stat.S_ISLNK(st.st_mode):
        return "符号链接 -> %s" % os.readlink(path)
    kind = "目录" if stat.S_ISDIR(st.st_mode) else "文件"
    desc = "%s，mode=0o%o，%d 字节" % (kind, st.st_mode & 0o7777, st.st_size)
    if kind == "文件" and st.st_size:
        try:
            with open(path, "rb") as fp:
                head = fp.read(32)
            desc += "，ELF" if head[:4] == b"\x7fELF" else "，文件头=%r" % head
        except OSError:
            pass
    return desc


def ensure_ohos_toolchain(native):
    """配置前确认工具链可执行；缺失则就地修复并复核。

    CI 缓存可能带着上一版脚本解出来的 SDK（没有 +x），只在 fetch 时修是不够的，
    所以每次 configure 前都过一遍；修不动就明确报错，别让 CMake 给出
    "The C compiler is not able to compile a simple test program" 这种
    把人往错误方向带的结论。
    """
    bin_dir = os.path.join(native, "llvm", "bin")
    n = ensure_executables([bin_dir])
    if n:
        log("补齐 %d 个工具链文件的可执行位（解包或缓存丢失 +x）" % n)
    clang = os.path.join(bin_dir, "clang")
    if os.name != "nt" and os.path.exists(clang) and not os.access(clang, os.X_OK):
        fail("工具链编译器不可执行：%s（%s）—— 若是符号链接/文本文件说明解包"
             "没重建链接，删掉 SDK 目录后重跑 fetch-ohos-sdk"
             % (clang, _describe_path(clang)))


def cmd_fetch_ohos(argv):
    """流式取出 OHOS native SDK 的 zip 并解包到 dest（CI 用）。"""
    ap = argparse.ArgumentParser(prog="cross_build.py fetch-ohos-sdk")
    ap.add_argument("--url", default=DEFAULT_OHOS_SDK_URL)
    ap.add_argument("--dest", required=True, help="解包目标目录（最终含 native/）")
    ap.add_argument("--pattern", default=OHOS_NATIVE_MEMBER)
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)

    tc = os.path.join(a.dest, "native", "build", "cmake", "ohos.toolchain.cmake")
    if not a.force and os.path.exists(tc):
        log("已存在 OHOS native SDK：%s" % a.dest)
        ensure_ohos_toolchain(os.path.join(a.dest, "native"))
        return 0

    if a.url.startswith(("http://", "https://")):
        import urllib.request
        log("下载 %s" % a.url)
        stream = urllib.request.urlopen(a.url, timeout=120)
    else:
        log("读取本地归档 %s" % a.url)
        stream = open(a.url, "rb")

    tmp_zip = os.path.join(tempfile.gettempdir(), "swinx-ohos-native.zip")
    try:
        with tarfile.open(fileobj=stream, mode="r|gz") as tf:
            found = None
            for m in tf:
                if m.isfile() and fnmatch.fnmatch(m.name, a.pattern):
                    found = m
                    break
            if not found:
                fail("归档里没有匹配 %s 的成员" % a.pattern)
            log("提取 %s（%.1f MB），后续内容不再下载"
                % (found.name, found.size / 1048576.0))
            src = tf.extractfile(found)
            with open(tmp_zip, "wb") as out:
                shutil.copyfileobj(src, out, 1 << 20)
    finally:
        try:
            stream.close()
        except Exception:
            pass

    os.makedirs(a.dest, exist_ok=True)
    log("解包到 %s" % a.dest)
    extract_zip(tmp_zip, a.dest)
    try:
        os.remove(tmp_zip)
    except OSError:
        pass

    if not os.path.exists(tc):
        fail("解包后仍找不到 native/build/cmake/ohos.toolchain.cmake，"
             "归档布局可能变了（看 %s）" % a.dest)
    # 兜底：万一 zip 是用不写 unix 权限位的工具打的，external_attr 就靠不住，
    # 这里按 ELF 特征再补一遍。
    native = os.path.join(a.dest, "native")
    n = ensure_executables([native], recursive=True)
    log("校验工具链可执行位：补齐 %d 个%s" % (n, "" if n else "（无缺失）"))
    ensure_ohos_toolchain(native)
    log("OK：OHOS native SDK 就绪 -> %s" % native.replace("\\", "/"))
    return 0


def build(platform, args):
    if platform == "android":
        if args.api < ANDROID_MIN_API:
            fail("Android platform 必须 >= %d（iconv 需要 28，ASharedMemory 需要 26）"
                 % ANDROID_MIN_API)
        ndk = resolve_android_ndk(args.sdk)
        toolchain = os.path.join(ndk, "build", "cmake", "android.toolchain.cmake")
        if not os.path.exists(toolchain):
            fail("NDK 里没有 %s" % toolchain)
        log("NDK: %s" % ndk)
        # 同理防一手：NDK 若是从缓存/压缩包恢复的，bin 里也可能丢 +x。
        host_bins = glob.glob(os.path.join(ndk, "toolchains", "llvm",
                                           "prebuilt", "*", "bin"))
        n = ensure_executables(host_bins)
        if n:
            log("补齐 %d 个 NDK 工具文件的可执行位" % n)
        plat_args = ["-DANDROID_ABI=%s" % args.abi,
                     "-DANDROID_PLATFORM=android-%d" % args.api]
    else:
        toolchain, native = ohos_toolchain(resolve_ohos_sdk(args.sdk))
        log("OHOS native SDK: %s" % native)
        ensure_ohos_toolchain(native)
        plat_args = ["-DOHOS_ARCH=%s" % args.abi]

    expect = {"arm64-v8a": "aarch64", "armeabi-v7a": "arm",
              "x86_64": "x86_64"}.get(args.abi)
    build_dir = args.build or os.path.join(SWINX_ROOT, "build",
                                           "cross-%s-%s" % (platform, args.abi))
    gen = ["-G", args.generator] if args.generator else []

    log("platform=%s abi=%s build=%s" % (platform, args.abi, build_dir))
    rc = run(["cmake", "-S", SWINX_ROOT, "-B", build_dir] + gen + [
        "-DCMAKE_TOOLCHAIN_FILE=%s" % toolchain,
        "-DCMAKE_BUILD_TYPE=Release",
    ] + plat_args)
    if rc != 0:
        fail("configure 失败（rc=%d）" % rc)

    rc = run(["cmake", "--build", build_dir, "--target", args.target,
              "--parallel", str(args.jobs)])
    if rc != 0:
        fail("编译失败（rc=%d）" % rc)

    n = verify_artifacts(build_dir, expect)
    log("OK：%s/%s 交叉编译通过，产物 %d 个" % (platform, args.abi, n))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "fetch-ohos-sdk":
        return cmd_fetch_ohos(sys.argv[2:])

    ap = argparse.ArgumentParser(description="swinx 移动端交叉编译门禁")
    ap.add_argument("platform", choices=["android", "ohos"])
    ap.add_argument("--sdk", default=None, help="NDK / OHOS native SDK 路径")
    ap.add_argument("--abi", default="arm64-v8a")
    ap.add_argument("--api", type=int, default=ANDROID_MIN_API,
                    help="Android platform 级别（默认 28，不能低于 28）")
    ap.add_argument("--build", default=None,
                    help="构建目录（默认 <swinx>/build/cross-<platform>-<abi>）")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument("--target", default="swinx")
    ap.add_argument("--generator", default=None, help="默认不指定（用平台默认生成器）")
    args = ap.parse_args()
    return build(args.platform, args)


if __name__ == "__main__":
    sys.exit(main())
