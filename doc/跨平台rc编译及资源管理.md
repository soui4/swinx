# 跨平台 RC 编译及资源管理 (swinx)

本模块使用 MinGW 的 `windres` 工具在 Linux/Unix 平台上提供 Windows 资源管理 API 兼容性，用于编译 `.rc` 资源文件。

## 概述

资源系统允许您：
- 在 Linux 上使用 MinGW 的 `windres` 编译 Windows `.rc` 资源文件
- 将资源链接到 ELF/Mach-O 二进制文件中
- 在运行时使用标准 Windows 资源 API 访问资源

## 支持的 API

### 资源定位
- `FindResource` / `FindResourceA` / `FindResourceW`
- `FindResourceEx` / `FindResourceExA` / `FindResourceExW`

### 资源访问
- `LoadResource`
- `LockResource`
- `SizeofResource`
- `FreeResource`

### 资源枚举
- `EnumResourceNames` / `EnumResourceNamesA` / `EnumResourceNamesW`
- `EnumResourceTypes` / `EnumResourceTypesA` / `EnumResourceTypesW`
- `EnumResourceLanguages` / `EnumResourceLanguagesA` / `EnumResourceLanguagesW`

### 字符串资源
- `LoadString` / `LoadStringA` / `LoadStringW`

### 资源修改（桩实现 - 只读实现）
- `BeginUpdateResource` / `BeginUpdateResourceA` / `BeginUpdateResourceW`
- `UpdateResource` / `UpdateResourceA` / `UpdateResourceW`
- `EndUpdateResource` / `EndUpdateResourceA` / `EndUpdateResourceW`

## 前置条件

安装 MinGW 资源编译器：

```bash
# Ubuntu/Debian
sudo apt-get install mingw-w64

# macOS
brew install mingw-w64
```

## 使用方法

### 1. 包含 CMake 模块

在主 `CMakeLists.txt` 中：

```cmake
include(__cmake/windres.cmake)
```

### 2. 为构建目标添加资源

#### 方法 1：使用 target_compile_resources 函数（推荐）

```cmake
add_executable(myapp main.cpp)
target_compile_resources(myapp myapp.rc)
```

#### 方法 2：使用宏

```cmake
add_executable(myapp main.cpp)
add_target_resources(myapp myapp.rc)
```

### 3. 在代码中访问资源

```cpp
#include <windows.h>
#include <winres.h>

// 查找资源
HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(101), RT_BITMAP);
if (hRes) {
    // 获取资源大小
    DWORD size = SizeofResource(NULL, hRes);
    
    // 加载并锁定资源
    HGLOBAL hGlobal = LoadResource(NULL, hRes);
    LPVOID pData = LockResource(hGlobal);
    
    // 使用数据...
    
    // 释放资源（可选，资源不是动态分配的）
    FreeResource(hGlobal);
}

// 加载字符串
WCHAR buffer[256];
int len = LoadStringW(GetModuleHandle(NULL), 1, buffer, 256);
```

## 资源类型

支持标准 Windows 资源类型：

- `RT_CURSOR` (1) - 光标资源
- `RT_BITMAP` (2) - 位图资源
- `RT_ICON` (3) - 图标资源
- `RT_MENU` (4) - 菜单资源
- `RT_DIALOG` (5) - 对话框
- `RT_STRING` (6) - 字符串表项
- `RT_FONTDIR` (7) - 字体目录资源
- `RT_FONT` (8) - 字体资源
- `RT_ACCELERATOR` (9) - 加速键表
- `RT_RCDATA` (10) - 应用程序定义的资源
- `RT_MESSAGETABLE` (11) - 消息表项
- `RT_GROUP_CURSOR` (12) - 光标组
- `RT_GROUP_ICON` (14) - 图标组
- `RT_VERSION` (16) - 版本资源
- `RT_ANICURSOR` (21) - 动画光标
- `RT_ANIICON` (22) - 动画图标
- `RT_HTML` (23) - HTML 文档
- `RT_MANIFEST` (24) - 并行程序集清单

## Linux 和 macOS 上的实现技术

### 核心技术架构

`target_compile_resources` 函数实现了跨平台资源编译的完整流程，其核心技术包括：

#### 1. 工具链检测与准备

在 CMake 配置阶段，自动检测所需的工具链：

**Windows 平台：**
- 直接启用资源编译（`ENABLE_RESOURCES_BUILD = ON`）
- 使用 MSVC 或 MinGW 的原生资源编译器

**Linux 平台：**
- 检测 `x86_64-w64-mingw32-windres`：编译 .rc 为 COFF 格式
- 检测 `ld`：链接 COFF 到 ELF 格式
- 检测 `objcopy`：重定义符号名称
- 检测 `nm`：提取和分析符号

**macOS 平台：**
- 检测 `x86_64-w64-mingw32-windres`：编译 .rc 为 COFF 格式
- 检测 `as`：汇编器，将 COFF 嵌入到 Mach-O 对象

#### 2. 多阶段编译流程

##### 第一阶段：RC → COFF（所有非 Windows 平台）

使用 `windres` 将 `.rc` 文件编译为 Windows COFF 格式的目标文件：

```bash
x86_64-w64-mingw32-windres --target=pe-x86-64 \
  -I${PROJECT_BINARY_DIR}/config \
  -I${PROJECT_SOURCE_DIR}/soui-sys-resource \
  -i app.rc -o app_res.coff.o -O coff
```

这一步生成包含 Windows 资源格式的标准 COFF 目标文件。

##### 第二阶段：COFF → 平台原生格式

**Linux 平台（COFF → ELF）：**

采用四步转换流程：

1. **COFF → ELF 初步转换**
   ```bash
   ld -r -b binary app_res.coff.o -o app_res.elf.o
   ```
   使用 `ld` 的二进制输入模式，将 COFF 文件作为原始二进制数据嵌入到 ELF 目标文件中。

2. **符号提取与分析**
   ```bash
   nm -g app_res.elf.o > symbols.txt
   ```
   提取全局符号，生成符号列表。

3. **符号重命名脚本生成**
   通过 `process_symbols.cmake` 脚本处理符号列表，将所有符号重命名为以 `soui_` 为前缀，避免与其他库冲突。

4. **应用符号重命名**
   ```bash
   objcopy --redefine-syms=rename.txt app_res.elf.o app_res.final.o
   ```
   使用 `objcopy` 重定义所有符号名称，确保符号唯一性。

**macOS 平台（COFF → Mach-O）：**

采用嵌入式汇编方案：

1. **生成汇编文件**
   创建包含 COFF 二进制数据的汇编文件：
   ```assembly
   .section __DATA,__data
   .globl __binary_soui_coff_o_start
   .globl __binary_soui_coff_o_end
   
   __binary_soui_coff_o_start:
       .incbin "app_res.coff.o"
   __binary_soui_coff_o_end:
       .byte 0
   ```

2. **汇编为 Mach-O**
   ```bash
   as -o app_res.o app.s
   ```
   使用系统汇编器将汇编文件编译为 Mach-O 目标文件。

#### 3. 链接集成

将生成的平台原生目标文件添加到构建目标：

```cmake
target_sources(${target} PRIVATE ${resource_objects})
```

**Linux 特殊处理：**
- 设置 `-rdynamic` 导出所有全局符号
- 使用版本控制脚本 `export_res_sym.lst` 控制符号导出
- 添加 `--no-gc-sections` 防止链接器优化删除资源段

#### 4. 运行时资源访问

编译后的资源被嵌入到最终的可执行文件或共享库中，在运行时通过 swinx 提供的 Windows API 兼容层访问：

- ELF/Mach-O 文件中的 `.rsrc` 段（或等效数据段）包含所有资源
- 首次访问时自动解析并缓存资源目录结构
- 提供与 Windows 完全兼容的 API 接口

### CMake 函数详解

#### `target_compile_resources(target rc_file1 [rc_file2 ...])`

**功能：** 为指定构建目标编译并链接资源文件

**参数：**
- `target`：构建目标名称（必须是已存在的 target）
- `rc_file1, rc_file2, ...`：要编译的 .rc 文件列表

**内部流程：**
1. 检查 `ENABLE_RESOURCES_BUILD` 标志
2. 遍历所有 .rc 文件
3. 对每个文件调用 `windres_compile_rc()`
4. 将生成的目标文件添加到 target
5. 在 Linux 上设置特殊的链接选项
6. 添加 `ENABLE_BUILD_RESOURCE` 编译定义

**示例：**
```cmake
add_executable(demo main.cpp)
target_compile_resources(demo demo.rc uires.rc)
```

#### `windres_compile_rc(output_var rc_file [INCLUDE_DIRS dir1 dir2 ...])`

**功能：** 将单个 .rc 文件编译为平台原生的目标文件

**参数：**
- `output_var`：输出变量名，接收生成的目标文件路径
- `rc_file`：输入的 .rc 文件路径
- `INCLUDE_DIRS`：（可选）额外的头文件包含路径

**返回值：**
- Linux: `${CMAKE_CURRENT_BINARY_DIR}/${rc_name}_res.final.o`
- macOS: `${CMAKE_CURRENT_BINARY_DIR}/${rc_name}_res.o`

**特性：**
- 自动添加项目配置目录和资源目录到包含路径
- 根据目标平台选择适当的转换流程
- 生成中间文件便于调试

### 实际使用示例

参考 `demos/demo/CMakeLists.txt`：

```cmake
# 收集所有资源文件
file(GLOB_RECURSE CURRENT_RC *.rc *.rc2)

# 创建可执行文件
add_soui_exe(demo ${CURRENT_HEADERS} ${CURRENT_SRCS} 
                  ${CURRENT_SOUIS} ${CURRENT_RC})

# 编译资源文件（仅在非 Windows 平台生效）
target_compile_resources(demo ${CURRENT_RC})

# 在非 Windows 平台需要额外包含 swinx 头文件
if (NOT CMAKE_SYSTEM_NAME MATCHES Windows)
    include_directories(${PROJECT_SOURCE_DIR}/swinx/include)
endif()
```

### 平台差异处理

| 特性 | Windows | Linux | macOS |
|------|---------|-------|-------|
| 资源编译器 | MSVC rc.exe 或 windres | x86_64-w64-mingw32-windres | x86_64-w64-mingw32-windres |
| 目标格式 | PE-COFF | ELF | Mach-O |
| 转换工具 | 无需转换 | ld + objcopy + nm | as |
| 符号处理 | 自动 | 重命名为 soui_* | 使用 __binary_* 符号 |
| 链接选项 | 默认 | -rdynamic + version-script | 默认 |
| 资源段名称 | .rsrc | .rsrc | .rsrc |

## 工作原理

1. **编译**：`windres` 工具将 `.rc` 文件编译为包含 Windows 资源格式的 COFF 目标文件。

2. **转换**：根据平台不同，使用不同策略将 COFF 转换为原生格式：
   - **Linux**：通过 `ld` 链接为 ELF，再用 `objcopy` 重命名符号
   - **macOS**：通过内联汇编将 COFF 嵌入到 Mach-O 数据段

3. **链接**：资源目标文件被链接到最终二进制文件中。

4. **运行时**：swinx 资源系统在程序启动时解析 ELF/Mach-O 文件的资源段，提供 Windows 资源 API 来访问嵌入的资源。

## 限制

1. **只读**：资源修改API（`BeginUpdateResource`、`UpdateResource`、`EndUpdateResource`）是桩实现，返回 `ERROR_CALL_NOT_IMPLEMENTED`。

2. **语言支持**：支持特定语言的资源。

3. **资源更新**：资源在编译时嵌入，无法在运行时修改。

4. **性能**：资源查找涉及解析 ELF/Mach-O 文件，该过程是延迟执行并缓存的。

5. **工具依赖**：需要安装 MinGW 工具链和 binutils 工具（ld、objcopy、nm）。

## 示例 .rc 文件

```c
#include <winres.h>

// 图标
IDI_APP_ICON ICON "app.ico"

// 字符串表
STRINGTABLE
BEGIN
    1 "Hello, World!"
    2 "Application Name"
END

// 二进制数据
MYDATA RCDATA
BEGIN
    0x01, 0x02, 0x03, 0x04
END

// 版本信息
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

## 故障排除

### 常见问题

**问题 1：windres 找不到**
```
windres not found. Resource files (.rc) will not be compiled.
```
**解决：** 安装 mingw-w64 包

**问题 2：编译后符号冲突**
**解决：** 确保使用了符号重命名机制（Linux 上自动处理）

**问题 3：资源在运行时找不到**
**解决：** 检查是否正确调用了 `target_compile_resources` 并且资源段未被链接器优化

### 调试技巧

1. 查看生成的中间文件：
   - `${CMAKE_CURRENT_BINARY_DIR}/*_res.coff.o` - COFF 格式
   - `${CMAKE_CURRENT_BINARY_DIR}/*_res.final.o` - ELF 格式（Linux）
   - `${CMAKE_CURRENT_BINARY_DIR}/*_symbols.txt` - 符号列表

2. 使用 `nm` 检查最终符号：
   ```bash
   nm -g your_binary | grep soui
   ``

## 注意事项

- 资源系统在首次使用时自动初始化。
- 支持主程序和共享库中的资源。
- 实现是线程安全的。
- 在 Linux 上必须使用 `-rdynamic` 链接选项导出资源符号。
