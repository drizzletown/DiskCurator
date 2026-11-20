# DiskCurator Native v1.0-rc1 — 交付说明

## 已交付

- C++17 核心引擎 + Win32 SDK 原生 GUI 源码；未使用 Qt、MFC、vcpkg。
- 扫描后默认打开新的“整理工作台”，串联融合候选 → 目录结构建议 → 目录映射编辑 → 文件映射编辑 → 预览/二次确认 → 非破坏性重建 → 旧目录定向安全隔离。
- `StructureSuggestion.file_rules`：逐文件手工精确目标覆盖；手工路径优先于内容去重，拒绝覆盖/目录冲突/越界；修改后重新规划。
- `plan_retire_directory`：仅隔离拥有两份**被选择旧目录以外**独立且字节一致活动副本的文件，其他文件保留并列出阻断理由。
- CLI `retire ROOT OLD_FOLDER [--apply --yes]` 和 Windows 自动化烟雾测试补充。

## 安全边界

- 当前无永久删除，不立即释放存储空间；**隔离 ≠ 删除 ≠ 物理备份**。
- 两份普通文件可以在同一物理盘上。该安全规则不等于异地备份或防止磁盘彻底损坏。
- 迁移保留所有原始来源。失败时保守恢复、完整目标保留，文件级撤销受后续外部变更限制。
- 任何建议都不能自动写入，实际执行必须查看计划并确认。

## 执行记录（仅本地可移植核心）

- Release: 7/7 CTest PASS。
- GCC Debug 严格警告: 6/6 非压力 CTest PASS。
- Clang ASan/UBSan: 6/6 非压力 CTest PASS。
- Windows MSVC 编译、Win32 GUI 操作、WIC/EXIF、NTFS/USB 故障注入 **NOT RUN**。

## Windows 构建

```bat
scripts\build_windows.bat
```

源码 ZIP 包含 CMake 工程、构建脚本、GitHub Actions、回归测试、质量门槛和说明文档。只有在真实 Windows 环境完成验证后才可以称为稳定版本。处理重要数据前必须在复制出的测试目录演练。
