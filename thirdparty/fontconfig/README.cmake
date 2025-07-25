# FontConfig CMake Build System

This document describes the CMake build system for FontConfig, which has been migrated from the original Meson build system.

## Prerequisites

- CMake 3.16 or later
- C compiler (GCC, Clang, or MSVC)
- FreeType2 development libraries
- Expat or libxml2 development libraries
- Python 3 (for build scripts)
- gperf (for hash table generation)

## Quick Start

```bash
# Create build directory
mkdir build-cmake
cd build-cmake

# Configure the build
cmake ..

# Build the project
make -j$(nproc)

# Install (optional)
sudo make install
```

## Build Options

The CMake build system supports the following options:

- `ENABLE_DOCS`: Build documentation (default: ON)
- `ENABLE_NLS`: Enable native language support (default: ON)
- `ENABLE_TESTS`: Enable unit tests (default: ON)
- `ENABLE_TOOLS`: Build command-line tools (default: ON)
- `ENABLE_CACHE_BUILD`: Run fc-cache on install (default: ON)
- `ENABLE_ICONV`: Enable iconv support (default: OFF)
- `ENABLE_FONTATIONS`: Use Fontations for indexing (default: OFF)

### XML Backend Selection

- `XML_BACKEND`: Choose XML backend (auto, expat, libxml2, default: auto)

### Configuration Options

- `DEFAULT_HINTING`: Default hinting (none, slight, medium, full, default: slight)
- `DEFAULT_SUB_PIXEL_RENDERING`: Sub-pixel rendering (none, bgr, rgb, vbgr, vrgb, default: none)
- `BITMAP_CONF`: Bitmap font configuration (yes, no, no-except-emoji, default: no-except-emoji)

### Path Configuration

- `FC_CACHEDIR`: Cache directory (default: /usr/local/var/cache/fontconfig)
- `FC_TEMPLATEDIR`: Template directory (default: /usr/local/share/fontconfig/conf.avail)
- `FC_BASECONFIGDIR`: Base config directory (default: /usr/local/etc/fonts)
- `FC_CONFIGDIR`: Config directory (default: /usr/local/etc/fonts/conf.d)
- `FC_XMLDIR`: XML directory (default: /usr/local/share/xml/fontconfig)