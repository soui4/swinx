
# 安装依赖
## all platforms
install cmake git
## ubuntu, debian
sudo apt install build-essential libxcb1-dev libxcb-render0-dev libgl1-mesa-dev freeglut3-dev uuid-dev pkg-config
## centos, fedora
sudo yum install build-essential libxcb-devel mesa-libGL-devel libuuid-devel
## macos
brew install ninja pkgconf glfw3 glew

# 编译
mkdir build & cd build
cmake ..
make

# debug
##  **使用vs远程调试linux** 
linux机器上安装
sudo apt install -y openssh-server build-essential gdb rsync make zip ninja-build
本机安装
CMake, Vcxsvr(https://sourceforge.net/projects/vcxsrv/files/vcxsrv/21.1.10/vcxsrv-64.21.1.10.0.installer.exe/download)
在VS菜单：
工具\选项\跨平台 页面的列表中添加linux主机IP，注意配置好登陆启用名，密码
然后就可以在VS里运行远程调试了。
具体参考：https://learn.microsoft.com/zh-cn/cpp/build/get-started-linux-cmake?view=msvc-170

##  **linux 内存泄漏检测** 
valgrind --leak-check=full ./your_program

setoutsoft@qq.com  2025/7/25

