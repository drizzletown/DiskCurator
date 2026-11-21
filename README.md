# DiskCurator · 磁盘文件整理助手

从一块多年没打开的旧硬盘里，找回熟悉的资料。先看看有哪些内容，再核对不同年代的备份，把想留下的文件整理到新目录里。

**[下载 Windows 10 / 11 x64 便携版](https://github.com/drizzletown/DiskCurator/releases/latest)**。在发布页的 **Assets** 中选择以 `-Win64.zip` 结尾的文件。

## 从一块旧盘开始

打开“整理一块旧盘”，选择旧硬盘或其中一个资料目录。

1. **先认一认**　先看哪些目录占空间、里面主要是文档还是照片，以及文件大致在哪几年改过。点下一步后才读取文件内容，找出可能改过名字的备份目录。
2. **核对不同版本**　并排看两边各自独有的文件和同一位置的不同版本。选好要补到哪个目录，再看复制预览。相同内容可以复用，有冲突的版本另存。
3. **决定哪些留下**　每组重复文件由你选出多余的位置。暂时用不上的资料可以复制归档，常用资料可以按你起的新目录名重新归类。归档核实后，还可以单独预览收起原件。

每步都可以跳过。隔离的文件仍占磁盘空间，可按操作记录恢复。首页也可以直接看空间、找重复文件或核对两份备份。

![旧盘整理与三个快捷入口](docs/images/task-home.png)

[使用指南](START-HERE.txt) · [文件操作与恢复](SECURITY.md) · [微信赞赏](DONATE.md)

## 开发与构建

安装 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21 或更高版本，再运行 `scripts\build_windows.bat`。便携包输出到 `dist`。

[产品需求](REQUIREMENTS.md) · [总体架构](ARCHITECTURE.md) · [测试指南](TESTING.md) · [Windows 验收记录](RELEASE-ACCEPTANCE.md)
