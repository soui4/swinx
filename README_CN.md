<!-- README_CN.md -->
# SwinX
## 简介
这是一个类似wine的帮助windows平台程序运行到Linux,macos平台上的胶水库。
## 功能
SwinX是为SOUI5(https://gitee.com/setoutsoft/soui4)支持跨平台而开发，当然其它项目也可以使用SwinX来支持跨平台。
SwinX通过在linux平台实现windows客户端必须的API，让windows客户端代码链接SwinX后即可像在Windows平台一下在Linux及macos平台运行。目前SOUI5所有功能都已经通过swinx实现跨平台。
## 限制
本项目在Linux平台实现的Windows平台必须的HWND，目前HWND的非客户区只支持滚动条，不支持其它如标题栏，菜单。此外也不支持MDI窗口类型。
## 加入我们 
本项目还在持续进化中，欢迎有兴趣的朋友加入一起开发，QQ群：229313785, 385438344
## 开源协议
本项目开源但不免费，参与项目并贡献有质量的提交将自动获得SwinX的终身免费授权(见Contributors.md)，重要贡献人员将按照一定比例（解释权归作者所有）获得项目收益。

# 版本：
## 1.1  2025.7.07
## 1.0  2025.3.11
## 0.1  2025.1.12