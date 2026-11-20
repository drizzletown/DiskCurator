# DiskCurator Native v1.0-rc8 — 基础工具候选版

2026-11-18。基线：docs/design-v2.1。平台：Windows 10/11 x64、C++17、原生 Win32 SDK。

## 新增（相对 rc7）

- 可人工选择精确重复文件隔离：plan_quarantine_selected，支持一个或多个组、全计划的两份独立副本校验、保留原始快照与执行时字节验证。
- `evaluate_directory_pair`：用户指定目录 A/B 的精确事实报告，覆盖率、Jaccard、同路径相同/冲突、两侧独有、空目录；不依赖推荐阈值。
- `find_overlapping_dirs`：任意互不包含的目录中按已验证的文件内容寻找相似候选，包含嵌套目录，明确总候选数和截断；大规模不安全分析时报错而不是沉默漏报。
- Win32 基础工具增加人工隔离、选中重复目录清退、相似目录检索/选择、指定两个目录评估及可选融合预览。上下文动作替代增加并列按钮。
- CLI 增加 pair、overlaps、quarantine-selected；Windows smoke 增加双目录和手动隔离检查。
- 新增 `tests/test_foundation.cpp`，项目自带需求/设计基线 v2.1。

## 测试与风险边界

- 当前容器 Linux Release：CTest 8/8 PASS。
- Clang 严格编译（-Wall -Wextra -Wpedantic -Werror）和选定 6 项 Debug 测试：PASS。
- Clang AddressSanitizer/UBSan 选定 6 项 Debug 测试：PASS。
- **Windows rc8 MSVC/GUI/NTFS：NOT RUN，构成正式发布阻断项。** 历史 rc6 Windows 7/7 及 CLI PASS 不能转移认定为 rc8 通过。
- 未开发永久删除；隔离不释放磁盘空间；双副本定义为已验证、独立的活动文件对象，不等于多物理介质备份。
- 纯只读评估不创建工作区，默认计划预览不直接执行。
