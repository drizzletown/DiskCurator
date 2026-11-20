# DiskCurator Native v1.0-rc11 源码候选

**基线**：[docs/design-v2.4/README.md](docs/design-v2.4/README.md)。

默认模式改为选择磁盘；没有默认勾选。扫描前必须选卷并在开始前后确认卷身份。目录模式可只分析一个文件夹。扫描结束后用观察视角浏览文件/目录容量及哈希候选份数，跨卷哈希候选只读，单根精确验证后才允许提交计划。

新增源文件：`src/observations.hpp`、`src/observations.cpp`、`tests/test_observations.cpp`。

可移植核心测试（Linux Debug -O0）：11/11 PASS。Windows MSVC/GUI/USB 实际测试未执行；不提供已经验收的 Windows EXE，也不开放跨卷删除。

在 Windows 通过 `scripts\build_windows.bat` 构建并进行端到端验收；测试脚本使用临时目录，不会实际扫描系统盘。
