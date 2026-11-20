# 02 — 扫描进度与性能设计

## 实际问题及根因范围

旧手动目录/进入单盘精细管理路径为完整SHA → 全部候选逐字节比较 → 目录分组 → 结构建议 → 融合建议。重复组有n份、每份S字节时，哈希读nS，逐字节阶段还读2(n−1)S；同时旧比较大文件时没有块内进度和取消。旧“选择磁盘”首次扫描原本没有这段额外比较，不能把所有慢扫描都归因于它。

旧文件SHA为通用标量C++实现；旧结构分类对每个顶层目录遍历全部文件。仅凭1TB容量和一晚耗时不能确定用户设备瓶颈，需记录实际文件字节、文件数、阶段、吞吐与错误。

## 目标与边界

- PERF-25-01：初次扫描对每个可读普通文件完整计算SHA256；不以抽样、大小或旧mtime缓存替代。
- PERF-25-02：默认扫描不自动追加候选逐字节核验、照片解码或全目录关系搜索。
- PERF-25-03：单文件内部按读取块检查取消并累积真实I/O；进度通知按时间限流，GUI消费最新快照。
- PERF-25-04：扫描枚举阶段不伪造百分比；哈希阶段按字节而非文件个数估计进度。
- PERF-25-05：进度出现错误时可以达到“工作已处理完”，但必须同时显示未读/异常项，不宣称内容100%成功。
- PERF-25-06：选定A/B任务仅读取这两个子树。全范围精确分析为独立、可取消的显式操作。

## 阶段与计数

`ScanProgress` 独立于文本状态：phase、root、current_file、files_discovered、files_completed、total_bytes、bytes_processed、bytes_read、errors、root_index、root_count、totals_known、elapsed_seconds。

`bytes_read`只累计实际内容读入字节。`bytes_processed`为已处理工作量，失败文件剩余字节会计为“已尝试处理”，所以必须与errors同时呈现；files_completed也包含失败尝试。枚举完成后才得到可读文件集合的字节总量。不会把盘符标称容量当作总量。

GUI在约250ms定时器上读取互斥保护的最新快照，不为每个读取块投递一个堆分配消息。哈希内核约100ms发布一次，阶段边界和首个读取块立即发布，以支持大文件中途取消。

近期速率用实际读取字节增量/时间增量并平滑；阶段或磁盘变化时重置。ETA = 当前阶段剩余字节 / 近期速率，在样本不足、总量未知、长时间没有进展时显示“估算中”。磁盘多选时显示当前第几盘，字节进度和ETA不冒充整个多盘任务的总剩余时间。

保存索引和观察统计有独立阶段。扫描的Complete回调是核心扫描阶段状态；GUI须等观察结果建完才标任务完成。取消不把半成品当完整可操作库存；底层同步ReadFile若堵塞，停止请求需等该次OS调用返回。

## I/O 与算法

Windows使用 BCrypt/CNG SHA256，共享算法provider、可重用hash object、1MiB缓冲区；CreateFileW顺序访问提示与Unicode长路径处理。CNG是否使用特定CPU加速由平台provider决定，不保证固定倍数。仍按一个根的顺序流读取，避免为单个机械移动硬盘启动大量竞争读取线程。

目录计数先累计直属文件，再从深目录向父目录合并。观察层通过文件/目录索引构建树签名，避免长期保存所有文件的编码字符串。结构分类一次聚合顶层目录分类统计，避免O(顶层目录数×文件数)。重组、相似与推荐任务检查取消。

缓存是外部可重建SHA索引，不是恢复点。本版没有可信增量命中、USN/MFT直读、分块续扫、每盘智能并发调度。不能把这些设计方向写成当前交付能力。

## 性能测量方法

1. `dc_benchmark ROOT --scan`测量真实完整SHA+观察流程；输出TSV阶段、耗时、进程逻辑读取量、峰值内存和哈希内容读取量。
2. 先用有代表性的样本目录测量，再决定是否对整盘测量，避免仅为诊断重复整盘读取。
3. 读盘、哈希CPU、目录枚举、小文件打开、观察统计分别看；热缓存内存测试不能替代移动硬盘实测。
4. `qa/observation-scaling.json`记录50万文件纯内存统计的旧新对比；性能数字必须附数据规模、平台、计时方法和限制。

理论读取时间按实际文件总字节/有效吞吐计算。若实际待读为1,000,000,000,000字节，在100MB/s、50MB/s、20MB/s下，仅内容读取分别约2小时47分、5小时33分、13小时53分，尚未加枚举和其他开销。这是算术示例，不是产品承诺或用户磁盘诊断。

## 主要技术来源

- Microsoft `BCryptOpenAlgorithmProvider`：https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptopenalgorithmprovider
- Microsoft `Creating a Hash with CNG`：https://learn.microsoft.com/en-us/windows/win32/seccng/creating-a-hash-with-cng
- Microsoft `CreateFileW`：https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
