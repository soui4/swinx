# 编译(ubuntu)
## 安装依赖
sudo apt install cmake git build-essential gdb libcairo2-dev libxcb1-dev libxcb-util0-dev
mkdir build & cd build
cmake ..
make

## 缺少opengl库
sudo apt install libgl1-mesa-dev freeglut3-dev

