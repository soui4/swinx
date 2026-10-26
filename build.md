
# 安装依赖
## all platforms
install cmake git
## ubuntu, debian
sudo apt install build-essential libxcb1-dev libxcb-render0-dev libgl1-mesa-dev freeglut3-dev uuid-dev pkg-config libasound2-dev
## centos, fedora
sudo yum install build-essential libxcb-devel xcb-util-renderutil-devel mesa-libGL-devel libuuid-devel
## macos
brew install ninja pkgconf glfw3 glew

# 编译
mkdir build & cd build
cmake ..
make

# uos/deepin 系统
Failed to call method: The name org.freedesktop.portal.Desktop was not provided by any .service files
## 安装xdg-desktop-portal核心服务
在deepin系统中，首先安装基础portal服务：

bash
sudo apt install xdg-desktop-portal

## 为了让portal与deepin桌面环境完美集成，还需要安装deepin特有的后端实现：

bash
sudo apt install xdg-desktop-portal-dde

# debug
##  **使用vs远程调试linux** 
linux机器上安装
sudo apt install -y openssh-server build-essential gdb rsync make zip ninja-build
本机安装
CMake, Vcxsvr(https://github.com/marchaesen/vcxsrv/releases)
在VS菜单：
工具\选项\跨平台 页面的列表中添加linux主机IP，注意配置好登陆启用名，密码
然后就可以在VS里运行远程调试了。
具体参考：https://learn.microsoft.com/zh-cn/cpp/build/get-started-linux-cmake?view=msvc-170

##  **linux 内存泄漏检测** 
valgrind --leak-check=full ./your_program

跑 `demos/fun_test` 时用这条，结果才只有一份可归因的 heap summary（`test_flock.cpp` 的 fork 子进程会继承整片父进程堆后 `_exit()`，逐字节重复报出 fontconfig/线程对象的假 `definitely lost`）：

```
valgrind --leak-check=full --child-silent-after-fork=yes \
  --suppressions=../swinx/valgrind-linux.supp \
  ./fun_test --gtest_filter=-window.*

leaks --atExit -- ./fun_test --gtest_filter='-window.*'

```


判读基线与残留项说明见 `doc/swinx-总体评估报告-2026-10-18.md` §6.3 与 `demos/fun_test/readme.md`。

setoutsoft@qq.com  2025/7/25

