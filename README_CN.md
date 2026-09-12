# SwinX

[中文](README_CN.md) | [English](README_EN.md)

SwinX 是一个面向 Linux / macOS 平台的 Windows 应用兼容层（兼容层设计思路与 Wine 类似）。它通过在非 Windows 平台上重新实现 Windows 客户端开发所依赖的核心系统 API，使基于 Win32 API 编写的客户端代码无需大规模改造，即可直接链接 SwinX 并运行于 Linux 与 macOS 平台。

## 项目背景

SwinX 最初为 [SOUI5](https://gitee.com/setoutsoft/soui4) 的跨平台支持而开发。目前 SOUI5 的全部功能均已基于 SwinX 实现跨平台运行。任何依赖 Win32 客户端 API 的项目，同样可以引入 SwinX 获得跨平台能力。

## 核心能力

- **Win32 API 兼容层**：在 Linux / macOS 上提供 `user32`、`gdi32`、`kernel32`、`ole32`、`shell32` 等模块中客户端开发所需 API 的实现，应用程序链接 SwinX 后即可像在 Windows 平台上一样编译、运行。
- **窗口系统实现**：提供完整的 HWND 窗口模型，包括窗口创建、消息循环、消息分发、输入法（IME）、剪贴板、拖放（OLE Drag & Drop）、多显示器管理等。
- **GDI 绘图支持**：实现常用 GDI 对象与绘制接口，配合平台后端完成渲染。
- **常用控件基础设施**：内置通用控件层（`cmnctl32`），覆盖 `richedit` 等客户端常用控件依赖。
- **COM / OLE 基础设施**：实现 COM 对象模型、自动化（Variant、SafeArray）、接口封送等基础能力。
- **多平台后端**：Linux 平台的窗口与输入基于 XCB / X11 实现，配合 cairo、freetype、fontconfig 等图形与文本栈；Apple 平台（macOS / iOS）则使用原生 NSView / UIView 与 Core Graphics，不依赖 X11 和 cairo。平台相关代码统一收敛于独立的平台抽象层。
- **可扩展的平台 API**：通过 `platform_api.h` 定义的平台抽象接口，外部可以为 SwinX 提供自己的平台 API 实现。SOUI 正是基于这套 API，实现了对 Android 与鸿蒙（HarmonyOS）的支持。

## 源码结构

```
swinx/
├── include/        # 对外暴露的 Windows 风格头文件（windows.h、winuser.h 等）
├── src/            # API 实现主体
│   ├── gdi/        # GDI 绘图实现
│   ├── cmnctl32/   # 通用控件实现
│   └── platform/   # 平台抽象层（Linux: XCB 后端；Apple: NSView / UIView 后端）
├── thirdparty/     # 内置第三方依赖（cairo、freetype、fontconfig、dbus、xkbcommon 等，供 Linux 后端使用）
├── linux.cmake / macos.cmake / ios.cmake   # 各平台构建配置
└── build.md        # 详细构建与调试指南
```

## 快速开始

### 环境要求

- CMake ≥ 3.x、Git
- Linux（Ubuntu / Debian）：

  ```bash
  sudo apt install build-essential libxcb1-dev libxcb-render0-dev \
      libgl1-mesa-dev freeglut3-dev uuid-dev pkg-config libasound2-dev
  ```

- Linux（CentOS / Fedora）：

  ```bash
  sudo yum install build-essential libxcb-devel xcb-util-renderutil-devel \
      mesa-libGL-devel libuuid-devel
  ```

- macOS：

  ```bash
  brew install ninja pkgconf glfw3 glew
  ```

### 编译

```bash
mkdir build && cd build
cmake ..
make
```

更多构建细节（含 UOS / deepin 适配、VS 远程调试、Valgrind 内存检测）请参阅 [build.md](build.md)。

## 已知限制

- 非客户区目前仅支持滚动条，暂不支持标题栏、菜单等系统绘制部分。
- 暂不支持 MDI 多文档窗口类型。

## 参与贡献

项目仍在持续演进中，欢迎感兴趣的开发者加入。

- 交流 QQ 群：229313785、385438344
- 贡献者名单见 [Contributors.md](Contributors.md)：参与项目并提交有质量的代码，将自动获得 SwinX 终身免费授权，重要贡献者还可按比例分享项目收益（解释权归作者所有）。

## 许可

本项目开源但**不免费**：源码开放供学习与评估，商业使用需获取授权。具体条款请参阅 [license_CN.txt](license_CN.txt)。

## 版本历史

| 版本 | 日期 |
| ---- | ---- |
| 1.1  | 2025-07-07 |
| 1.0  | 2025-03-11 |
| 0.1  | 2025-01-12 |
