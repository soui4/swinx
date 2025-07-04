
# 安装依赖
## ubuntu, debian
sudo apt install cmake git build-essential gdb libcairo2-dev libxcb1-dev libgl1-mesa-dev freeglut3-dev
## centos, fedora
sudo yum install cmake git gcc g++ gdb cairo-devel libxcb-devel mesa-libGL-devel libuuid-devel
## macos
brew install cmake ninja pkgconf cairo glfw3 glew
### build cairo for swinx
git clone https://gitee.com/setoutsoft/cairo.git
view cairo/build_mac.md for how to build cairo for swinx
this cairo version diverge from the official cairo 1.18.4 tag, and has some patches include using utf8 font family.
# 编译
mkdir build & cd build
cmake ..
make


setoutsoft@qq.com  2025/2/1

