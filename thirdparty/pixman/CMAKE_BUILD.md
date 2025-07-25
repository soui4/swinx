# Pixman CMake Build System

This document describes how to build Pixman using the new CMake build system.

## Prerequisites

- CMake 3.16 or later
- A C compiler (GCC, Clang, or MSVC)
- Ninja build system (recommended) or Make

### Optional Dependencies

- **GTK+ 3.0** and **GLib 2.0**: Required for building demo programs
- **libpng**: Required for PNG support in tests and demos
- **OpenMP**: For parallel processing in tests (if available)

## Building

### Basic Build

```bash
mkdir build
cd build
cmake ..
ninja  # or make if using Make generator
```

### Build Options

The following CMake options are available:

#### General Options
- `PIXMAN_BUILD_TESTS` (ON): Build test programs
- `PIXMAN_BUILD_DEMOS` (ON): Build demo programs
- `PIXMAN_ENABLE_TIMERS` (OFF): Enable TIMER_* macros
- `PIXMAN_ENABLE_GNUPLOT` (OFF): Enable gnuplot output filters
- `PIXMAN_ENABLE_GTK` (ON): Enable GTK demos
- `PIXMAN_ENABLE_LIBPNG` (ON): Use libpng in tests
- `PIXMAN_ENABLE_OPENMP` (ON): Enable OpenMP for tests

#### SIMD Optimization Options
- `PIXMAN_ENABLE_LOONGSON_MMI` (AUTO): Loongson MMI optimizations
- `PIXMAN_ENABLE_MMX` (AUTO): x86 MMX optimizations
- `PIXMAN_ENABLE_SSE2` (AUTO): x86 SSE2 optimizations
- `PIXMAN_ENABLE_SSSE3` (AUTO): x86 SSSE3 optimizations
- `PIXMAN_ENABLE_VMX` (AUTO): PowerPC VMX/Altivec optimizations
- `PIXMAN_ENABLE_ARM_SIMD` (AUTO): ARMv6 SIMD optimizations
- `PIXMAN_ENABLE_NEON` (AUTO): ARM NEON optimizations
- `PIXMAN_ENABLE_A64_NEON` (AUTO): ARM A64 NEON optimizations
- `PIXMAN_ENABLE_MIPS_DSPR2` (AUTO): MIPS DSPr2 optimizations
- `PIXMAN_ENABLE_RVV` (AUTO): RISC-V Vector optimizations
- `PIXMAN_ENABLE_GNU_INLINE_ASM` (AUTO): GNU inline assembly
- `PIXMAN_ENABLE_TLS` (AUTO): Thread-local storage support

#### Android Support
- `PIXMAN_CPU_FEATURES_PATH`: Path to cpu-features.[ch] for Android builds

### Example Builds

#### Release Build
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```

#### Debug Build with Tests Disabled
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DPIXMAN_BUILD_TESTS=OFF ..
ninja
```

#### Build with Specific SIMD Optimizations
```bash
cmake -DPIXMAN_ENABLE_SSE2=ON -DPIXMAN_ENABLE_SSSE3=OFF ..
ninja
```

## Testing

Run the test suite:

```bash
ctest --output-on-failure
```

Or run tests in parallel:

```bash
ctest --output-on-failure -j4
```

## Installation

Install to the default prefix (/usr/local):

```bash
ninja install
```

Install to a custom prefix:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/opt/pixman ..
ninja install
```

Install to a staging directory:

```bash
DESTDIR=/tmp/staging ninja install
```

## Generated Files

The build system generates the following files:

- `pixman-config.h`: Configuration header with detected features
- `pixman-version.h`: Version information header
- `pixman-1.pc`: pkg-config file for library discovery
- `libpixman-1.a`: Static library (or shared library if BUILD_SHARED_LIBS=ON)

## Cross-Compilation

For cross-compilation, set the appropriate toolchain file:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake ..
```

## Comparison with Meson

This CMake build system provides equivalent functionality to the original Meson build:

- All SIMD optimizations are detected and enabled automatically
- All tests and demos are built and work correctly
- pkg-config support is maintained
- Installation layout matches the Meson build
- All configuration options are preserved

## Troubleshooting

### Common Issues

1. **Missing dependencies**: Install GTK+3, GLib, and libpng development packages
2. **SIMD detection failures**: Check compiler support and flags
3. **Test timeouts**: Some tests may take longer on slower systems

### Build Logs

For detailed build information, use:

```bash
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON
ninja -v
```
