# DiskCurator Native v1.0-rc9 — 源码候选版

核心调整：全面 SHA-256 扫描与用户自行配置的保留策略。使用 C++17、Win32 SDK；基于 v1.0-rc8 + 性能诊断工具源码。设计基线升级至 v2.2。

- 新 `scan_hashed` 对根目录所有可读取普通文件计算 SHA-256；索引保存到根目录外的用户缓存；哈希按内容分桶，精确重复仍经字节验证。
- 清退策略保留数量 0～32、是否要求不同文件对象、执行前字节复核 / 全扫描哈希快速模式。风险选项须明确授权，操作计划冻结用户选择。
- GUI 人工隔离、自动隔离、旧目录定向清退共用同一设置界面；常规设置持久化，零份风险授权不得持久化。
- CLI `hashes`、`--keep N`、`--count-hardlinks`、`--trust-scan-hash`、`--allow-zero`。
- 现有事务执行器不支持永久删除；隔离区不释放磁盘空间。复制融合不删除原资料。
- Linux Release CTest 9/9 PASS，Clang strict 9/9 PASS，专项 ASan/UBSan PASS。
- **Windows rc9 MSVC、GUI、CLI/NTFS 和大目录性能仍待实机验证。不得称为正式稳定版。**
