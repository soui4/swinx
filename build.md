
# 安装依赖
## all platforms
install cmake git gperf python3
## ubuntu, debian
sudo apt install build-essential libxcb1-dev libxcb-render0-dev libgl1-mesa-dev freeglut3-dev uuid-dev 
## centos, fedora
sudo yum install build-essential libxcb-devel mesa-libGL-devel libuuid-devel
## macos
brew install ninja pkgconf glfw3 glew

# 编译
mkdir build & cd build
cmake ..
make


setoutsoft@qq.com  2025/7/25

