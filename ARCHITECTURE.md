# 架构与状态约束

磁盘文件整理助手采用 C++17。Windows GUI 使用 Win32 Common Controls，CLI、GUI 与诊断工具链接同一个核心库。以下路径相对工程根目录。

| 环节 | 实现 | 传给下一步的数据 |
|---|---|---|
| 输入、范围与任务 | [win_main.cpp](src/win_main.cpp)、[cli.cpp](src/cli.cpp) | GUI 先选择空间、查重或备份任务，再冻结所选卷或目录、对象路径和参数。CLI 校验命令及参数后进入相应服务。 |
| 枚举和完整读取 | [core.cpp](src/core.cpp)、[hash_index.cpp](src/hash_index.cpp)、[scan_progress.hpp](src/scan_progress.hpp) | 每根生成 Inventory，记录文件、目录、逻辑字节、完整 SHA-256 和读取错误。FullScan 保存所选根与跨根候选。Windows 使用 CNG 计算文件哈希。 |
| 观察和精确分析 | [observations.cpp](src/observations.cpp)、[scoped_analysis.cpp](src/scoped_analysis.cpp)、[photo_windows.cpp](src/photo_windows.cpp) | 从内存库存聚合文件、目录候选。显式全根精确核验生成当前根内容组。A/B 在临时子树库存中核验。照片解码和 dHash 使用 WIC。 |
| 预览与执行 | [hash_actions.cpp](src/hash_actions.cpp)、[core.hpp](src/core.hpp)、[core.cpp](src/core.cpp) | 候选隔离在私有库存中规划，保留原观察证据。Plan、MergePlan、StructurePlan 分别描述移动、目录融合和新结构构建；执行器复核对应快照、目标占用和冻结策略。 |
| 应用缓存与事务历史 | [hash_index.cpp](src/hash_index.cpp)、[core.cpp](src/core.cpp) | 用户缓存保存可重建的 SHA 索引。受管理根的 `.DiskCurator-System` 保存写事务的日志、暂存和隔离数据。 |

**快照有三个独立维度。** `Inventory.errors` 决定内容完整性。`index_persisted` 和 `index_error` 记录缓存落盘结果。GUI 的 `results_stale` 表明在快照生成后已执行可能改变文件的操作。缓存写入失败允许继续观察已读内容；内容不完整限制依赖完整证据的分析；过期结果供查询和导出，继续整理须显式重扫。

**事务以根为边界。** 计划保存根内相对路径。执行时检查路径组件、快照和目标占用，获取同根写锁，并检查历史待恢复事务。MOVE 遇到中断时依据日志和实际路径决定回滚或报告人工恢复。COPY 保留已经提交的目标，清理未提交暂存。移动 Undo 恢复路径位置。重新生成预览、编辑映射和再扫描均须使被取代的计划失效。

**扫描与分析有不同的读取范围。** 空间任务读取文件属性并按层建立索引。查重完整读取选定范围。备份对照分别读取两个所选目录，跨盘对照不创建写事务。用户发起 A/B 比较或融合时，只在两个子树内追加内容核验。独立 CLI 进程需要先扫描指定根以建立库存。GUI 工作线程发布进度快照，由 UI 线程读取并刷新控件。

输入、证据、策略和恢复的对外约定见 [需求矩阵](REQUIREMENTS.md)。更细的数据模型和计划状态见 [v2.6 数据与状态设计](docs/design-v2.6/04-data-and-state.md) 和 [操作计划与恢复设计](docs/design-v2.6/05-operation-plans.md)。
