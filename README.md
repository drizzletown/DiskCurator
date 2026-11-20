# DiskCurator · 磁盘文件整理助手

在 Windows 上找重复文件、核对备份、整理照片和文档。扫描只读取所选位置的文件，整理时先看预览，再确认执行。

**[下载 Windows 10 / 11 x64 便携版](https://github.com/drizzletown/DiskCurator/releases/latest)**。在发布页的 **Assets** 中选择以 `-Win64.zip` 结尾的文件。

## 什么时候用得上？

- **换电脑了**，对比新旧两份备份，看看哪些文件还没拷过去。
- **文件存了好几份**，按内容找出副本，改过名字也能找到。
- **照片攒了好几年**，按拍摄年月分文件夹，以后按月份就能找。

## 第一次扫描

解压后双击 `磁盘文件整理助手.exe`，选择文件夹或磁盘。

- 想看有多少文件、总共多大，点 **快速盘点**。
- 想找重复文件，点 **开始扫描**。完成后点 **重复文件** 查看结果。

![查重结果：三份名称不同的采购清单被归为同一组](docs/images/duplicate-files.png)

[使用指南](START-HERE.txt) · [文件操作与恢复](SECURITY.md) · [微信赞赏](DONATE.md)

## 开发与构建

安装 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 和 CMake 3.21 或更高版本，再运行 `scripts\build_windows.bat`。便携包输出到 `dist`。

[产品需求](REQUIREMENTS.md) · [总体架构](ARCHITECTURE.md) · [测试指南](TESTING.md) · [Windows 验收记录](RELEASE-ACCEPTANCE.md)
