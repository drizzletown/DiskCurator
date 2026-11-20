> 以下保留 v0.8～1.0 候选版的历史运行记录。当前构建请查阅 [发布验收记录](RELEASE-ACCEPTANCE.md)，需求和复跑命令分别在 [需求矩阵](REQUIREMENTS.md) 与 [测试指南](TESTING.md)。

# DiskCurator Native v0.8 历史测试记录

日期：2026-11-16。当前环境：Linux（无 Windows SDK / MSVC / 桌面 GUI）。**源码候选版，不是可直接使用的重要数据正式版。**

## 实际执行并通过

| 检查 | 状态 | 范围 |
|---|---|---|
| Linux GCC C++17 Release | **PASS：5/5 CTest** | 核心、恢复、4,040 文件压力、融合双副本、自动建议 |
| Linux GCC Debug `-Wall -Wextra -Wpedantic -Werror` | **PASS：编译 + 4/4 CTest** | 核心、恢复、融合、自动建议；Debug 压力 **NOT RUN** |
| Linux Clang Debug ASan/UBSan | **PASS：4/4 CTest** | 核心、恢复、融合、自动建议；sanitizer 压力 **NOT RUN** |
| 自动推荐单独测试 | **PASS** | 中文历史照片目录、跨级目录、仅同名、系统元数据误匹配、完全相同无需融合、既有冲突版本、只读、取消与不完整扫描 |
| 源码 ZIP 清单及 SHA256 | **打包时校验** | 检查主工程、Windows 构建脚本、GUI、测试、文档完整性 |

曾尝试 GCC 的 `-O1` ASan/UBSan 构建，但大型 `core.cpp` 编译超时；**未将该配置记为通过**。后改用 Clang `-O0 -g0` ASan/UBSan，以上四项测试实际通过。

## Windows（NOT RUN，v1.0 发布阻断项）

- Visual Studio 2022 MSVC x64 C++17 + Win32 SDK 编译与 GUI 运行。
- 原生推荐列表显示、用户双击建议、后台融合计划预览与确认，GUI 取消/筛选/排序测试。
- NTFS/exFAT、WIC/EXIF、磁盘权限/空间不足、断电/掉线与文件操作恢复验收。
- `scripts/smoke_gui_windows.ps1` 中新增临时照片备份的推荐自动发现，**脚本已编写但尚未在 Windows 上执行**。

**发布结论：v0.8 候选源码版，Windows 原生验收仍未完成。请先用复制出来的测试数据操作。**


## v0.9 新增验证（2026-11-17）

已在本次 Linux 可移植核心的 C++17 Release 配置中执行全部 **6/6 CTest PASS**：`core`、`recovery_fault_injection`、`directory_stress`、`merge_and_two_copy_retention`、`read_only_merge_recommendations`、`suggested_directory_structure`。

新增结构测试实际覆盖：只读建议、手工修改顶层目录映射、完整来源到目标路径表、空目录保留、同名不同内容另存、重复内容复用、变更源目录拒绝旧计划、同长度/恢复时间戳内容变化检测、重复执行幂等性、已有用户目标目录阻断，以及较强相同备份证据建议共同目标。

Windows MSVC/Win32 GUI 与 NTFS 实机验收：**NOT RUN**。源码中新增的原生目标 TreeView、映射编辑及 GUI 烟雾测试不能替代实际测试，**不得将 v0.9 宣称为生产级 Windows 稳定版**。

进一步补充：v0.9 新增测试还包括 **结构迁移中断日志的保守恢复**（保留原文件和已提交目标文件，仅删除软件暂存区未完成的副本）。运行结构测试可复现：`ctest -R suggested_directory_structure --output-on-failure`。

本轮实际执行结果：Linux GCC Release **6/6**，GCC Debug 严格警告编译及非压力用例 **5/5**，Clang Debug ASan/UBSan 非压力用例 **5/5**；Debug/ASan 压力用例未执行。Windows 原生 GUI / MSVC / NTFS 仍未执行。


## v1.0-rc1 本轮实际验证（2026-11-17）

当前执行环境 Linux；没有 Windows SDK/MSVC/GUI 会话。**实际已执行**：

| 检查 | 结果 | 说明 |
|---|---|---|
| GCC C++17 Release CTest | PASS 7/7 | 含 `integrated_workspace_workflow` 与约 4,040 文件压力测试 |
| GCC Debug `-Wall -Wextra -Wpedantic -Werror` | PASS 6/6 | 严格编译通过，非压力 CTest 通过 |
| Clang Debug ASan/UBSan | PASS 6/6 | 非压力回归通过，压力用例未执行 |
| Linux 命令行 `structure → retire → undo` | PASS | 独立测试目录，迁移保留旧数据，外部两副本规则正确，撤销恢复 |
| Windows MSVC/Win32 GUI、原生 WIC/EXIF | NOT RUN | 当前环境不可验证 |
| Windows NTFS/exFAT、USB 掉线、断电与占用恢复 | NOT RUN | 发布阻断项 |

**判断**：v1.0-rc1 可作为源码候选版交付，不能据此证明 Windows 原生 EXE 已编译和通过验收。隔离并不回收磁盘空间。

## v1.0-rc2 — 用户反馈后修复（2026-11-17）

- 用户 Windows MSVC 首次实测中，核心库编译成功，但 GUI 的混合类型 auto 声明失败，结构测试的 UTF-8 编译失败。
- 已修复上述两处 C++/CMake 缺陷，统一 MSVC `/utf-8` 并规范中文 fixture 的 filesystem 路径。
- Linux Release 回归 7/7 通过。Windows v1.0-rc2 构建复验未执行，不可标记为已通过。

## v1.0-rc4 — Windows 实测反馈与工作区标记修复（2026-11-17）

- 用户已在 Windows / VS 2022 / MSVC 19.44 成功生成 `DiskCurator.exe`；manifest 校验通过。
- v1.0-rc3 Windows CTest 为 3/7 PASS；失败的四项均报 `Invalid DiskCurator workspace marker`。
- 已定位 Windows 写入 CRLF、二进制读取 LF 比较引起的错误；新增二进制写标记、精确兼容 CRLF、拒绝附加内容。
- 当前交付机仅能验证 Linux C++17 核心；v1.0-rc4 Windows CTest、GUI、WIC、NTFS 实盘仍待用户 Windows 重新执行。
- 保留根目录中的 `.DiskCurator-System`，绝不能以删除事务日志或隔离区作为临时修复手段。
