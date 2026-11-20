# 06 — 需求追溯与验收基线

> 1.0.2 交付补充：G-01/G-02/G-04 已补齐；缓存状态新增独立字段，CSV 带范围和状态。本文关于 1.0.1 的现状审计保留作对照，最新实现与验证以 [交付记录](../../RELEASE-ACCEPTANCE.md) 为准。

## 1. 证据等级

本轮是设计文档修订，C++、资源、脚本和测试代码没有变化；不以“文档已更新”新增任何运行测试通过声明。对应源码的既有实测记录为 [RELEASE-ACCEPTANCE.md](../../RELEASE-ACCEPTANCE.md)，本章把它映射到当前需求。

| 证据类别 | 能支持的结论 | 不能替代的结论 |
|---|---|---|
| 源码审查 | 存在某实现路径、状态守卫或算法结构 | 目标 Windows GUI 真正运行正确 |
| Linux Release 回归 | 可移植核心在给定用例上运行通过 | Windows CNG、WIC、卷身份、NTFS/exFAT 和 UI |
| ASan／UBSan 指定用例 | 这组运行中的地址和未定义行为检查 | 全部路径或内存泄漏已经验证 |
| Windows 交叉编译与链接 | Windows 目标源码、资源和符号能够构建 | MSVC 构建及实际 Windows 执行 |
| PowerShell 7 解析／C# 编译 | 脚本语法、编码或互操作代码编译通过 | Windows PowerShell 5.1 与 user32 调用运行正确 |
| 原生 GUI / 实盘运行 | 对记录的环境、数据集和操作序列提供证据 | 未执行设备、故障和任意容量的普遍保证 |
| 微基准 | 指定算法、数据集和环境的对照 | 1TB 用户移动硬盘的实际吞吐和完成时间 |

## 2. 已有证据清单

| 编号 | 状态 | 位置与范围 |
|---|---|---|
| E-01 | PASS，16/16 | `qa/v1.0.1-linux-release-ctest.txt`；Release 断言保持有效 |
| E-02 | PASS，5/5，关闭泄漏检测 | `qa/v1.0.1-linux-asan-ubsan-no-lsan.txt`；扫描进度、观察、哈希操作、取消和 A/B 范围五项 |
| E-03 | 未完成泄漏检查 | `qa/v1.0.1-linux-sanitizers.txt`；LeakSanitizer 在当前 ptrace 环境失败，保留原始失败记录 |
| E-04 | 交叉编译与链接 PASS | `qa/v1.0.1-windows-cross-compile.txt`；GUI、资源、CLI、诊断程序、测试目标 |
| E-05 | 解析/编码/布局检查 PASS | `qa/v1.0.1-powershell-preflight.txt`；PowerShell 7、7 个脚本、当时 50 项源码要求 |
| E-06 | 互操作代码编译 PASS | `qa/v1.0.1-gui-smoke-interop-compile.txt`；未执行 Win32 调用 |
| E-07 | Linux 纯内存对照 | `qa/observation-scaling.json`；50 万文件观察统计，非存储读取 |
| E-08 | Linux 热缓存微基准 | `qa/hash-scan-linux-microbenchmark.json`；2 万小文件和一个 64 MiB 文件 |
| E-09 | NOT RUN | Windows MSVC、PowerShell 5.1、GUI、CNG 实机、真实 USB HDD/SSD 与设备故障 |

测试中的离线 TiB 条目是用于检查“不读取范围外对象”的合成元数据，并未分配或扫描真实 TiB 数据。历史版本的 Windows 成功日志不替代 1.0.1 改动后的 Windows 验证。

## 3. 需求—实现—测试追溯

表中的源码符号用于定位；已有回归表示相应测试在 E-01 中运行过，不代表每项 UI 或硬件条件均已验收。P/OBS 在需求章定义，UI 在交互章定义，SCAN/PERF 在性能章定义，ACT 在计划章定义。

### 产品与观察

| ID | 验收对象 | 主要实现 | 现有证据及限制 |
|---|---|---|---|
| P-01 | 结果中心与统一变更计划 | `App / run_action / plan_*` | 界面源码审查；Windows 工作流待验收 |
| P-02 | 明确选择范围，零预选 | `choose_drives / scan_roots_hashed` | full_disk_and_manual_scope；GUI 待实机 |
| P-03 | 完整 SHA 覆盖普通文件 | `scan_hashed` | complete_sha256_and_retention_policy；scan_progress_and_streaming_hash |
| P-04 | 观察、建议和预览不改资料 | `observe_scan / plan_*` | 观察离线回归、建议/结构回归；GUI smoke 待运行 |
| P-05 | 基础能力可独立发起 | `hash_actions / scoped_analysis / run_action` | 策略、手动基础、范围分析回归；任意唯一文件隔离仍有缺口 |
| P-06 | 范围与证据等级可辨 | `Observations / exact_ready / rows` | 候选正确性回归；范围标签仍需补齐 |
| P-07 | 计划冻结与前置一致性 | `Plan / MergePlan / StructurePlan` | 快照、冲突和预览后变化回归；GUI 策略显示待验收 |
| P-08 | 长任务可观察 | `ScanProgress / render_progress` | 进度与取消核心回归；真实 GUI 刷新待验收 |
| P-09 | 写后过期，不隐式重扫 | `complete / results_stale` | 源码状态审查；成功/异常/切换视角待实机 |
| P-10 | 证据等级分开记录 | `RELEASE-ACCEPTANCE / qa` | 既有日志核对；文档检查不新增运行结论 |
| OBS-01 | 逻辑字节与目录汇总 | `scan / Folder` | scan_rollup_and_verification_cancel；目录压力回归 |
| OBS-02 | 同哈希候选计数含自身 | `observe_scan` | scan_observation_perspectives |
| OBS-03 | 树候选、空目录与完整性 | `observe_scan` | 目录候选、缺父目录及错误根回归 |
| OBS-04 | 精确数属于实际核验范围 | `deduplicate / duplicate_dirs` | core / manual_foundation_and_directory_pair；计数标签待改 |
| OBS-05 | 解释 A/B 多重集与冲突 | `evaluate_directory_pair / scoped_analysis` | manual_foundation_and_directory_pair / selected_directory_pair_scope |
| OBS-06 | 错误和未知状态不伪装完成 | `Inventory.errors / observe_scan` | 缺文件、变动、取消回归；缓存告警持久展示有缺口 |
| OBS-07 | 冗余估计不等于回收空间 | `候选计数 / quarantine` | 规则已明确；所有界面用语需人工核对 |
| OBS-08 | 观察范围与结果时效 | `focus_volume / results_stale / export` | 源码审查；跨盘候选范围标签未完整落实 |

### 界面

| ID | 验收对象 | 主要实现 | 现有证据及限制 |
|---|---|---|---|
| UI-01 | 全宽结果工作区 | `init_controls / layout` | 旧控件不存在等 GUI smoke 已编写；未实机运行 |
| UI-02 | 范围输入与单根操作 | `switch_scan_scope / ensure_result_scope` | 范围核心回归及 GUI 静态审查 |
| UI-03 | 视角与证据表达 | `refresh / Observations` | 候选算法已测；UI-G01 计数口径仍需完善 |
| UI-04 | 菜单分类与明确作用范围 | `action_menu / run_action` | 源码审查；全根与所选操作提示需实机 |
| UI-05 | 选择身份、筛选与显示导出 | `selected_paths / restore_paths / export` | 脚本含选择不转移场景；未实机运行；非全量导出 |
| UI-06 | A/B 选择与范围核验 | `do_pair_assess / do_merge` | selected_directory_pair_scope；GUI 参数链待实机 |
| UI-07 | 有效预览与执行 | `invalidate_pending / preview_*` | 核心计划回归；旧计划失效和策略冻结显示待实机 |
| UI-08 | 后台生命周期与取消 | `start / complete / worker` | 读取取消已测；阻塞 I/O 和关闭行为待实机 |
| UI-09 | 不完整与过期分离 | `results_stale / complete` | 源码审查；写后状态与多盘切换待实机 |
| UI-10 | 原生 GUI 独立验收 | `smoke_gui_windows.ps1` | PowerShell 7 解析和 C# 编译通过；Windows 运行未执行 |

### 扫描与性能

| ID | 验收对象 | 主要实现 | 现有证据及限制 |
|---|---|---|---|
| SCAN-01 | 输入根规范化和明确选择 | `scan_roots_hashed / DriveChoice` | 范围测试；卷替换真实设备待验收 |
| SCAN-02 | 稳定文件完整 SHA | `scan_hashed` | 已知向量、块边界、空文件、mtime 恢复后重读 |
| SCAN-03 | 分阶段完成定义 | `ScanProgress / start / complete` | 事件回归和源码审查；GUI 阶段显示待实机 |
| SCAN-04 | 读取错误与结果完整性 | `scan / scan_hashed / observe_scan` | 消失文件、异常根、目录未知回归 |
| SCAN-05 | 实际读量、处理量和 ETA | `ScanProgress / render_progress` | 计数单调性/多根测试；ETA 实机精度未测 |
| SCAN-06 | 按任务能力协作取消 | `cancel 参数与任务集合` | 大文件块内取消回归；非所有预览可立即取消 |
| SCAN-07 | 索引保存与独立告警 | `save_index` | 保存阶段取消回归；GUI 持久失败状态未完备 |
| SCAN-08 | 首扫候选，分析按需 | `scan_roots_hashed → observe_scan` | GUI 调用路径审查；候选/策略/范围核心回归 |
| PERF-01 | 无默认第二轮内容分析 | `Task::Scan / Task::ScanAll` | 读取量及路径审查；不代表所有 CLI 命令 |
| PERF-02 | 进度最新快照与节流 | `record_progress / timer` | 源码审查；GUI 负载下刷新待实机测量 |
| PERF-03 | 顺序读取和资源复用 | `CNG / read buffer / multi-root loop` | MinGW 编译；Windows CNG 吞吐未实测 |
| PERF-04 | 汇总/观察/分类的规模开销 | `scan / observe_scan / suggest_structure` | 目录回归、640 目录分类、50 万记录内存对照 |
| PERF-05 | A/B 限读与切换复用 | `scoped_analysis / focus_volume` | 无关离线对象不读测试；UI 切换只读审查 |
| PERF-06 | 小操作不触发整根重扫 | `results_stale` | 源码审查；无增量刷新能力 |
| PERF-07 | 分阶段实测与证据范围 | `dc_benchmark --scan / qa` | Linux 微基准与日志；真实 Windows HDD 未完成 |

### 操作与恢复

| ID | 验收对象 | 主要实现 | 现有证据及限制 |
|---|---|---|---|
| ACT-01 | 单根、路径和卷身份 | `need_scan / validate / same_drive` | 范围/路径回归；手动模式身份统一尚未完成 |
| ACT-02 | 候选与已验证分开 | `hash_actions 临时库存` | 伪哈希碰撞、原库存不变回归 |
| ACT-03 | 用户可配置保留策略 | `RetentionPolicy / execute_plan` | K=0/1/3、独立性、快速模式回归；GUI 同意链待验收 |
| ACT-04 | 选择隔离和目录外保留 | `plan_quarantine_* / plan_retire_*` | 手动基础、策略、工作流回归；唯一文件入口有限 |
| ACT-05 | A/B 子树融合 | `scoped_analysis / execute_merge_plan` | 范围、冲突、幂等、变化拒绝回归 |
| ACT-06 | 可编辑结构映射 | `StructureSuggestion / plan_structure` | suggested_directory_structure / integrated_workspace_workflow |
| ACT-07 | 冻结计划及外部变化 | `pending / verify_snapshot` | 源目标变化与冲突回归；设置改变不重写既有计划 |
| ACT-08 | MOVE/COPY 分开恢复 | `execute_plan / recover_*` | recovery_fault_injection / merge / structure；实盘故障待验收 |
| ACT-09 | 有边界的 Undo | `undo_latest` | 移动撤销与复制拒绝回归；没有内容版本回滚 |
| ACT-10 | 写任务不可取消及结果过期 | `Task / results_stale / complete` | 读取取消回归；写后 GUI 状态需实机验收 |


## 4. 必须独立验收的端到端场景

| 场景 | 输入或步骤 | 通过条件 |
|---|---|---|
| A-01 初次启动 | 多个磁盘连接，尚未选择 | 零预选；不自动扫描资料；旧模式和目录树不存在 |
| A-02 完整内容扫描 | 唯一、重复、空文件和大文件混合 | 每个稳定可读文件有完整 SHA；已知向量正确；首扫不自动做全部精确比较 |
| A-03 长时间读取 | 大文件或小文件密集目录 | 阶段、字节、文件数、当前路径和时间可见；ETA 范围明确；不以 100% 掩盖错误 |
| A-04 取消与关闭 | 正常读取、大文件中途、等待设备时请求停止 | 界面表述与 worker 生命周期一致；未完成结果不可执行；同步 I/O 阻塞不伪报已停止 |
| A-05 筛选与选择 | 甲/乙轮流占同一行，排序和后台刷新 | 选择始终对应真实路径；错误继承选择为失败 |
| A-06 副本数口径 | 两盘同哈希候选，进入单盘再精确核验 | 每处标签能识别计数范围；本根保留者数量不能借用他盘候选数 |
| A-07 指定 A/B | A/B 很小，第三目录很大或离线 | 比较/融合预览不读取第三目录内容；结构、冲突、空目录信息正确 |
| A-08 用户策略 | K=1、K=3、K=0、硬链接及快速模式 | 执行冻结策略；K=0 单独确认；不强制回到两份；不虚报字节复核 |
| A-09 预览变化 | 生成预览后修改映射、生成失败或改变全局策略 | 新预览失败不残留旧执行入口；全局设置不能暗改已冻结策略 |
| A-10 文件变更 | 预览后改变源、目标、时间或目录内容 | 按该操作的实际保证拒绝过期计划；快速模式的已知限制明确 |
| A-11 写后过期 | 移动/隔离/复制成功、部分失败、外层异常 | 结果过期、历史可看；无自动全盘重扫；视角切换不解除限制 |
| A-12 过期后新扫描取消 | 显式开始扫描后停止 | 不恢复旧库存为可执行，不恢复旧计划 |
| A-13 事务与撤销 | MOVE 成功及占用冲突；COPY 部分提交 | 区分回滚、部分保留与人工恢复；COPY 不被普通 Undo 自动删除 |
| A-14 规模与导出 | 超过显示上限的库存和搜索结果 | 搜索完整库存、截取有提示；全选和导出范围一致；不声称全量导出 |
| A-15 缓存写入失败 | 模拟不可写应用缓存 | 内容结果与保存失败分开；完成后仍能查见持久化失败状态；当前 GUI 尚有缺口 |
| A-16 设备身份变化 | 扫描前后换卷或断开 | 不把旧卷库存用于新卷；明确本次失败/未发布语义；不假称所有断盘都保留部分结果 |
| A-17 原生布局 | 最小窗口、多 DPI、中文长路径 | 进度、范围、选择与执行信息可读，菜单/对话框可达，无旧入口文案误导 |

GUI smoke 已自动化覆盖其中一部分受控路径，但还没有原生 Windows 执行结果；A-06、A-09～A-17 的完整交互和硬件条件不能仅靠现有脚本宣布通过。

## 5. 文档与发布质量门槛

本基线发布时检查：九份当前文档齐全；53 个需求编号均有定义和追溯；文档内链接有效；当前入口不再要求读者拼接旧版本；实现差距不被标成完成；没有修改对应代码和脚本。该文档检查与软件运行测试分别记录。

Windows 分发仍由现有 `build_windows.bat` 和验收记录控制。更新设计文档不签署 Windows 发布，也不改变已有二进制验证记录。软件源分发修改文档后，应重新生成来源文件哈希与归档校验，避免把旧校验清单套到新归档。
