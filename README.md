# DiskCurator Native 1.0.4

DiskCurator 是 Windows 10/11 x64 本地磁盘整理工具。选定磁盘或目录后可查看文件、重复副本和目录关系，并预览重命名、归档、隔离、目录融合和目录重组操作。

## 下载和使用

1. 打开[最新版本](https://github.com/drizzletown/DiskCurator/releases/latest)，在 **Assets** 中下载文件名以 `-Win64.zip` 结尾的便携包。
2. 将压缩包解压到本地文件夹，双击 `DiskCurator.exe`。
3. 首次使用可先选一个熟悉的文件夹，点“开始扫描”，再点“重复文件”查看结果。

命令行用户可运行 `dc_cli --help` 查看命令。

## 项目文档

- [产品需求与验收矩阵](REQUIREMENTS.md)
- [总体架构](ARCHITECTURE.md)
- [操作与恢复约定](SECURITY.md)
- [版本改动](RELEASE-v1.0.4.md)
- [Windows 验收记录](RELEASE-ACCEPTANCE.md)

## 支持项目

如果 DiskCurator 帮你理顺了文件，可以自愿赞助项目维护。[赞助 DiskCurator](DONATE.md)

## 从源码构建

在安装了 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21 或更新版本的环境，运行 `scripts\build_windows.bat`。构建使用静态 MSVC 运行库。自动验证通过后，便携包及其 SHA-256 文件位于 `dist`。单独运行各项检查见[测试指南](TESTING.md)。
