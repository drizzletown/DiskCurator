# 文件查重与整理助手 1.0.6 · Windows 验收记录

本版在 Windows 10.0.26200 x64 上使用 Visual Studio 2022 和 Windows SDK 10.0.26100 构建。文件操作均使用测试目录中的示例文件。

| 验收场景 | 实测结果 |
|---|---|
| 下载与启动 | 便携包内的主程序名为 `文件查重与整理助手.exe`。启动后的主窗口和资源管理器中的产品名称与之相符。命令行 `dc_cli --version` 返回 `1.0.6`。 |
| 旧版工作目录 | 用 1.0.5 给一个中文文件改名，再用 1.0.6 查看同一条历史并撤销。原文件名和内容均已还原。 |
| 界面回归 | 在 820×600 窗口选择范围、查重、筛选、核验、预览、确认复制和查看历史。模拟缓存不可写时，仍能浏览和核验。 |
| 核心和命令行 | 全部 16 项 CTest 通过。Windows CLI、保留策略、无效参数和诊断扫描通过。 |

[本机构建日志](qa/v1.0.6-windows-build.txt) · [Windows 回归输出](qa/v1.0.6-windows-validation.txt) · [实际查重窗口](docs/images/duplicate-files.png)

便携包附带 `SHA256SUMS.txt` 和与受测程序绑定的 `WINDOWS-AUTOMATED-VALIDATION.txt`。硬件验证范围见[测试指南](TESTING.md)。
