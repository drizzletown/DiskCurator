# DiskCurator Native 1.0.3 Windows 验收记录

2026-11-20 在 Windows x64 10.0.26200 上使用 Visual Studio 2022、MSVC 19.44、Windows SDK 10.0.26100 和 Windows PowerShell 5.1 验证。设计基线为 DC-DESIGN-2.6，当前实施范围在 REQUIREMENTS.md。

| 检查 | 本机结果 |
|---|---|
| 版本和构建 | x64 Release；GUI 文件版本资源、应用清单的程序集版本、CLI 版本输出与 CMake 版本一致。DPI 和长路径标志通过。 |
| CTest | 16/16 通过，耗时 33.59 秒。Release 断言检查通过。覆盖完整 SHA-256、扫描取消、目录压力、策略、范围核验、事务和恢复。 |
| 恢复故障夹具 | 覆盖源与暂存均缺失、恢复期间再次中断、循环重命名、撤销末尾记录丢失、普通文件名含 __TX__ 等情形。合并与历史计数通过。 |
| Windows CLI | 中文路径、精确比较、改名/撤销、保留份数与硬链接策略、隔离/撤销、融合、结构构建、WIC 和日期归档通过。9 组非法命令在扫描前被拒绝。 |
| 原生界面 | 通过默认范围、中文目录、筛选与选择身份、进度、精确核验、换根预览失效。夹具上确认复制后核对源文件和目标内容、历史及持续过期状态。 |
| 缓存故障 | 阻塞缓存路径，观察、精确核验与预览仍可用，持久化告警在视角切换后保留。 |
| 诊断工具 | 用本次随包的 dc_benchmark.exe 读取工程 tests 目录，17 个文件、103550 字节，内容读取错误 0。用于验证程序可运行。 |

构建日志与完整的测试输出分别为 [MSVC 日志](qa/v1.0.3-windows-build.txt) 和 [Windows 验收日志](qa/v1.0.3-windows-validation.txt)。发布包内的 WINDOWS-AUTOMATED-VALIDATION.txt 绑定三个 EXE 与 CTest 日志的 SHA-256。SHA256SUMS.txt 列出包内程序和文档的摘要。

真实 USB HDD/SSD 的换卷、拔盘与断电、NTFS/exFAT 故障恢复、多 DPI 人工检查及大盘长时性能仍待实机验收。增量刷新、断点续扫、全库存分页/导出等规划见需求矩阵。1.0.2 的记录已保存在 RELEASE-ACCEPTANCE-v1.0.2.md。
