
# 安装依赖
## ubuntu, debian
sudo apt install cmake git build-essential gdb libcairo2-dev libxcb1-dev libgl1-mesa-dev freeglut3-dev
## centos, fedora
sudo yum install cmake git gcc g++ gdb cairo-devel libxcb-devel mesa-libGL-devel libuuid-devel
## macos
brew install cmake ninja pkgconf cairo glfw3 glew

# 编译
mkdir build & cd build
cmake ..
make


setoutsoft@qq.com  2025/2/1

