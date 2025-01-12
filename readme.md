projext swinx
target: help bridging applications that developed for windows to linux and mocos.

build for debian/ubunbu:
sudo apt  install cmake git build-essential gdb libcairo2-dev libxcb1-dev libxcb-util0-dev
mkdir build & cd build
cmake ..
make

for openKylin:
sudo apt install libgl1-mesa-dev
sudo apt install freeglut3-dev

author: setoutsoft
contributor: 008(359501300), 维生素C(1394020320), 足迹™(253765263)