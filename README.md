# DiskCurator · 磁盘文件整理助手

在 Windows 上找重复文件、核对备份、整理照片和文档。扫描只读取所选位置的文件，整理时先看预览，再确认执行。

**[下载 Windows 10 / 11 x64 便携版](https://github.com/drizzletown/DiskCurator/releases/latest)**。在发布页的 **Assets** 中选择以 `-Win64.zip` 结尾的文件。

## 打开后，选一件事

| 想做什么 | 怎么用 |
|---|---|
| **看空间占用** | 选择磁盘或文件夹，按大小查看占用空间的目录，打开目录找大文件。 |
| **找重复文件** | 按内容找到改过名字的副本，核对位置与保留份数。默认每组至少留两份。 |
| **核对两份备份** | 选择原资料和备份，查看漏拷的文件，以及同一位置下内容不同的版本。 |

运行解压目录里的 `磁盘文件整理助手.exe` 即可开始。隔离后的文件可恢复，仍占用磁盘空间。

![DiskCurator Windows 首页的三个任务入口](docs/images/task-home.png)

[使用指南](START-HERE.txt) · [文件操作与恢复](SECURITY.md) · [微信赞赏](DONATE.md)

## 开发与构建

安装 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21 或更高版本，再运行 `scripts\build_windows.bat`。便携包输出到 `dist`。

[产品需求](REQUIREMENTS.md) · [总体架构](ARCHITECTURE.md) · [测试指南](TESTING.md) · [Windows 验收记录](RELEASE-ACCEPTANCE.md)
