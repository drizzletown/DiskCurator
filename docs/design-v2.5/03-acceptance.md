# 03 — 需求与验收追溯

| 需求 | 主要实现 | 验证 |
|---|---|---|
| 去掉左侧导航/目录树 | win_main.cpp controls/layout | Windows GUI smoke检查旧ID不再存在 |
| 结果上基础/主题/智能操作 | action_menu/run_action | GUI菜单、选择、预览路径 |
| 候选副本数和完整性 | observations.cpp | test_observations：离线、错误、缺父目录、4096文件 |
| 完整SHA和可见字节进度 | hash_index.cpp / scan_progress.hpp | test_scan_progress：已知向量、跨块、空文件、错误、中途取消、多根 |
| 块内精确核验进度/取消 | core.cpp equal_bytes/deduplicate | test_scan_cancellation |
| 从候选预览并遵循用户策略 | hash_actions.cpp | test_hash_actions：K1/3/0、伪hash碰撞、硬链接、metadata变化 |
| 两目录范围内核验/融合 | scoped_analysis.cpp | test_scoped_analysis：无关文件不读、冲突、取消、执行 |
| 一次结构分类聚合 | suggest_structure | test_structure：640目录分类边界、取消 |
| 结果筛选不转移选择 | selection identity/filter | Windows GUI smoke：甲→乙同一行号不继承选择 |
| 最新预览与执行状态一致 | pending invalidation | GUI源代码审查及Windows手工预览失败验收 |
| 写操作后不隐式全盘重扫 | results_stale / complete | GUI源代码审查；Windows手工执行、部分失败、切换多盘视角与重扫验收 |
| 发布脚本先解析再构建 | check_source_layout / mark_windows_validation | 当前PowerShell解析；Windows5.1自动门槛 |

## 必须在 Windows 上观察的结果

- 进度控件持续可见，大文件读取中百分比前进，速度/时间不被后续文本状态覆盖；枚举/汇总阶段显示活动条。
- 停止后不将未完成扫描标为可操作；真正阻塞的设备读取应显示等待，不保证立即取消OS调用。
- 未勾选磁盘不参与扫描；同名盘符换卷拒绝旧库存操作。
- 所选文件的路径与预览完全一致，筛选/排序/后台任务后不能换目标。
- 双目录操作不触发第三个大目录读取；全范围分析的范围在菜单中明确。
- 最新一次预览失败后，不能继续执行先前的旧计划。
- 隔离、改名、融合、结构构建和撤销后不自动扫描；成功、部分成功和失败均使结果过期。历史和只读查看保留，旧计划失效，切换视角不能绕过过期状态。显式扫描取消后，旧结果不能变回可执行状态。

本源码包的实际已执行记录统一见根目录 `RELEASE-ACCEPTANCE.md`。源码静态检查、Linux测试、交叉编译、PowerShell7解析与Windows实机运行分别记录，任何一项不得代替另一项。
