# Windows Resource API for Linux and macos (swinx)

This module provides Windows resource management API compatibility for Linux/Unix platforms using MinGW's `windres` tool to compile `.rc` resource files.

## Overview

The resource system allows you to:
- Compile Windows `.rc` resource files on Linux using MinGW's `windres`
- Link resources into ELF binaries
- Use standard Windows resource APIs to access resources at runtime

## Supported APIs

### Resource Location
- `FindResource` / `FindResourceA` / `FindResourceW`
- `FindResourceEx` / `FindResourceExA` / `FindResourceExW`

### Resource Access
- `LoadResource`
- `LockResource`
- `SizeofResource`
- `FreeResource`

### Resource Enumeration
- `EnumResourceNames` / `EnumResourceNamesA` / `EnumResourceNamesW`
- `EnumResourceTypes` / `EnumResourceTypesA` / `EnumResourceTypesW`
- `EnumResourceLanguages` / `EnumResourceLanguagesA` / `EnumResourceLanguagesW`

### String Resources
- `LoadString` / `LoadStringA` / `LoadStringW`

### Resource Modification (Stubs - Read-only implementation)
- `BeginUpdateResource` / `BeginUpdateResourceA` / `BeginUpdateResourceW`
- `UpdateResource` / `UpdateResourceA` / `UpdateResourceW`
- `EndUpdateResource` / `EndUpdateResourceA` / `EndUpdateResourceW`

## Prerequisites

Install MinGW resource compiler:

```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# macOS
brew install mingw-w64
```

## Usage

### 1. Include the CMake Module

In your main `CMakeLists.txt`:

```cmake
include(__cmake/windres.cmake)
```

### 2. Add Resources to Your Target

```cmake
# Method 1: Use the target_compile_resources function
add_executable(myapp main.cpp)
target_compile_resources(myapp myapp.rc)

# Method 2: Use the add_target_resources macro
add_executable(myapp main.cpp)
add_target_resources(myapp myapp.rc)
```

### 3. Access Resources in Code

```cpp
#include <windows.h>
#include <winres.h>

// Find a resource
HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(101), RT_BITMAP);
if (hRes) {
    // Get resource size
    DWORD size = SizeofResource(NULL, hRes);
    
    // Load and lock resource
    HGLOBAL hGlobal = LoadResource(NULL, hRes);
    LPVOID pData = LockResource(hGlobal);
    
    // Use the data...
    
    // Free resource (optional, resources are not dynamically allocated)
    FreeResource(hGlobal);
}

// Load a string
WCHAR buffer[256];
int len = LoadStringW(GetModuleHandle(NULL), 1, buffer, 256);
```

## Resource Types

Standard Windows resource types are supported:

- `RT_CURSOR` (1) - Cursor resource
- `RT_BITMAP` (2) - Bitmap resource
- `RT_ICON` (3) - Icon resource
- `RT_MENU` (4) - Menu resource
- `RT_DIALOG` (5) - Dialog box
- `RT_STRING` (6) - String table entry
- `RT_FONTDIR` (7) - Font directory resource
- `RT_FONT` (8) - Font resource
- `RT_ACCELERATOR` (9) - Accelerator table
- `RT_RCDATA` (10) - Application-defined resource
- `RT_MESSAGETABLE` (11) - Message table entry
- `RT_GROUP_CURSOR` (12) - Cursor group
- `RT_GROUP_ICON` (14) - Icon group
- `RT_VERSION` (16) - Version resource
- `RT_ANICURSOR` (21) - Animated cursor
- `RT_ANIICON` (22) - Animated icon
- `RT_HTML` (23) - HTML document
- `RT_MANIFEST` (24) - Side-by-side assembly manifest

## How It Works

1. **Compilation**: The `windres` tool compiles `.rc` files into COFF object files containing resources in the Windows resource format.

2. **Linking**: The resource object files are linked into the ELF binary. The resources are stored in a `.rsrc` section.

3. **Runtime**: The swinx resource system parses the ELF file's `.rsrc` section at runtime and provides the Windows resource API to access the embedded resources.

## Limitations

1. **Read-only**: Resource modification APIs (`BeginUpdateResource`, `UpdateResource`, `EndUpdateResource`) are stubs and return `ERROR_CALL_NOT_IMPLEMENTED`.

2. **Language Support**: Language-specific resources are supported but language filtering in `FindResourceEx` is not fully implemented.

3. **Resource Updates**: Resources are embedded at compile time and cannot be modified at runtime.

4. **Performance**: Resource lookup involves parsing the ELF file, which is done lazily and cached.

## Example .rc File

```c
#include <winres.h>

// Icon
IDI_APP_ICON ICON "app.ico"

// String table
STRINGTABLE
BEGIN
    1 "Hello, World!"
    2 "Application Name"
END

// Binary data
MYDATA RCDATA
BEGIN
    0x01, 0x02, 0x03, 0x04
END

// Version info
VS_VERSION_INFO VERSIONINFO
FILEVERSION 1,0,0,1
PRODUCTVERSION 1,0,0,1
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "My Application"
            VALUE "FileVersion", "1.0.0.1"
            VALUE "ProductName", "My Product"
        END
    END
END
```

## CMake Integration

The `windres.cmake` module provides the following functions:

### `windres_compile_rc(output_var rc_file [INCLUDE_DIRS dir1 dir2 ...])`

Compiles a single `.rc` file to an object file.

### `target_compile_resources(target rc_file1 [rc_file2 ...])`

Compiles and links resource files to a target.

### `add_target_resources(target [rc_files...])`

Convenience macro that automatically detects and compiles resources for a target.


## Notes

- The resource system is automatically initialized on first use.
- Resources from the main executable and shared libraries are supported.
- The implementation is thread-safe.
