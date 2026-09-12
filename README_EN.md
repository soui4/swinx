# SwinX

[中文](README_CN.md) | [English](README_EN.md)

SwinX is a Windows application compatibility layer for Linux and macOS, built with a design philosophy similar to Wine. By re-implementing the core system APIs that Windows client development depends on, SwinX allows applications written against the Win32 API to be linked against SwinX and run on Linux and macOS with minimal code changes.

## Background

SwinX was originally developed to bring cross-platform support to [SOUI5](https://github.com/soui4/soui). Today, all SOUI5 features run on Linux and macOS through SwinX. Any project that relies on Win32 client APIs can likewise adopt SwinX to gain cross-platform capability.

## Key Capabilities

- **Win32 API compatibility layer**: Implements the client-facing APIs of `user32`, `gdi32`, `kernel32`, `ole32`, `shell32`, and more. Once linked, applications compile and run on Linux/macOS just as they would on Windows.
- **Windowing system**: A complete HWND model, including window creation, message loops, message dispatching, IME support, clipboard, OLE drag & drop, and multi-monitor management.
- **GDI drawing support**: Implements common GDI objects and drawing interfaces, with rendering handled through platform backends.
- **Common controls infrastructure**: A built-in common controls layer (`cmnctl32`) covering dependencies such as `richedit`.
- **COM / OLE infrastructure**: Implements the COM object model, automation (Variant, SafeArray), interface marshaling, and related fundamentals.
- **Multi-platform backends**: On Linux, windowing and input are implemented on XCB/X11, combined with graphics and text stacks such as cairo, freetype, and fontconfig. On Apple platforms (macOS / iOS), SwinX uses the native NSView / UIView and Core Graphics instead, with no dependency on X11 or cairo. Platform-specific code is consolidated in a dedicated platform abstraction layer.
- **Extensible platform APIs**: The platform abstraction interfaces defined in `platform_api.h` allow external implementations to supply their own platform APIs for SwinX. SOUI is built on top of this API set to support Android and HarmonyOS.

## Repository Layout

```
swinx/
├── include/        # Exposed Windows-style headers (windows.h, winuser.h, etc.)
├── src/            # Core API implementations
│   ├── gdi/        # GDI implementation
│   ├── cmnctl32/   # Common controls
│   └── platform/   # Platform abstraction layer (Linux: XCB backend; Apple: NSView / UIView backend)
├── thirdparty/     # Bundled third-party dependencies (cairo, freetype, fontconfig, dbus, xkbcommon, etc., used by the Linux backend)
├── linux.cmake / macos.cmake / ios.cmake   # Per-platform build configs
└── build.md        # Detailed build & debugging guide (Chinese)
```

## Quick Start

### Prerequisites

- CMake ≥ 3.x, Git
- Linux (Ubuntu / Debian):

  ```bash
  sudo apt install build-essential libxcb1-dev libxcb-render0-dev \
      libgl1-mesa-dev freeglut3-dev uuid-dev pkg-config libasound2-dev
  ```

- Linux (CentOS / Fedora):

  ```bash
  sudo yum install build-essential libxcb-devel xcb-util-renderutil-devel \
      mesa-libGL-devel libuuid-devel
  ```

- macOS:

  ```bash
  brew install ninja pkgconf glfw3 glew
  ```

### Building

```bash
mkdir build && cd build
cmake ..
make
```

For additional build details (UOS / deepin integration, VS remote debugging, Valgrind memory analysis), see [build.md](build.md) (Chinese).

## Known Limitations

- The non-client area currently supports scroll bars only; system-drawn title bars and menus are not yet supported.
- MDI (multiple-document interface) windows are not yet supported.

## Contributing

The project is under active development, and contributions are welcome.

- QQ groups: 229313785, 385438344
- See [Contributors.md](Contributors.md): contributors with quality submissions automatically receive a lifetime free license for SwinX, and major contributors may share project revenue (at the author's discretion).

## License

This project is open source but **not free of charge**: the source is open for learning and evaluation, while commercial use requires a license. See [license.txt](license_EN.txt) for details.

## Version History

| Version | Date |
| ------- | ---- |
| 1.1     | 2025-07-07 |
| 1.0     | 2025-03-11 |
| 0.1     | 2025-01-12 |
