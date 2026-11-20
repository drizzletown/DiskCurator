# 1.0.4

改进首次扫描、查找重复文件、预览与确认、单文件改名及表单纠错。[1.0.4 发布说明](RELEASE-v1.0.4.md)

# 1.0.3

事务恢复与无覆盖移动、GUI 预览失效、严格 CLI 参数、发布一致性。本版详情见 [1.0.3 发布说明](RELEASE-v1.0.3.md)。

## v1.0.1 — result workspace and visible scan progress (2026-11-20)

- Remove sidebar mode controls and directory trees; actions share scan results.
- Native progress, phase telemetry, per-volume ETA, coalesced UI updates and block-level cancellation.
- Windows CNG SHA256; no automatic full-root re-verification or smart analysis after scan.
- Policy-controlled hash-candidate quarantine and selected-directory-pair analysis.
- Reduce observation memory and structure classification complexity; expand regression and release preflight.
- See RELEASE-v1.0.1.md and docs/design-v2.5 for current behavior and validation limits.

## v1.0.0 — source release build and release-gate hardening (2026-11-19)

- Consolidate selected-drive/manual-folder scan and multi-view observations from rc11 into the v1.0.0 release build.
- Fix test integrity: CTest asserts stay active even with Release/NDEBUG; add a test that fails at compile time if disabled.
- Fix stale "always two keepers" statements in live UI and backup safety report; respect configurable retention policy.
- Align Win32 `VERSIONINFO`, CLI banner, Windows packaging and CI artifact names to 1.0.0.
- Only produce the portable binary ZIP after manifest check, all CTest and CLI/GUI smoke gates pass; attach a machine validation receipt, per-binary and ZIP SHA256.
- Add explicit acceptance matrix, honest hardware validation gaps and end-user quick start.

# v1.0-rc11 — 选择磁盘扫描与可操作的观察视角

- 用户明确勾选 1..N 个盘符，默认无勾选；不再自动扫描全部本地卷。卷身份扫描前后校验。
- 全量 SHA-256 扫描后提供磁盘、文件、目录、哈希候选组、异常视角；目录树候选包含空目录。
- 单卷精确复核后在结果表右键触发人工隔离、双目录比较、融合预览和映射编辑。
- 保留跨卷只读与单根事务的边界；更新 Win32 GUI 测试与需求设计 v2.4。
- Windows 实机新功能尚待编译和验收；Linux Debug C++17 CTest 11/11 通过。

## v1.0-rc10 (2026-11-19)
- Default all accessible local disks scan or user-selected directory scan, both with complete SHA-256 for readable files.
- Aggregated read-only multi-volume inventory and candidate hashes; select one volume for verified single-root actions.
- Application-owned cache is excluded when its containing volume is scanned.
- Added portable scope regression test and GUI default/manual scope smoke checks.
- This is a source candidate; Windows implementation has not been tested locally.

## v1.0-rc7 (2026-11-18) — Win32 GUI 自动化输入修复

- 用户真实 Windows/MSVC 构建与 7/7 CTest、CLI 烟雾测试通过；GUI 筛选测试阻断。
- PowerShell GUI 自动化改用跨进程 `WM_SETTEXT`/`WM_GETTEXT`，并等待真实过滤结果；修正 `SetWindowText` 跨进程误用。
- 修复 MSVC C4458 局部变量遮蔽警告；文件管理核心未修改。
- Windows 新版 GUI 自动化仍待复测。

## v1.0-rc5 — Windows PowerShell 5.1 smoke-script encoding

- Fix PowerShell parse errors on Chinese-path test fixtures: both Windows smoke scripts now include UTF-8 BOM.
- Add source distribution preflight that detects non-ASCII PowerShell scripts missing BOM or containing invalid UTF-8.
- Windows CI now invokes Windows PowerShell 5.1 explicitly for both smoke test suites.
- User's rc4 Windows build and all 7/7 CTest passed; rc5 CLI and GUI smoke remain awaiting actual Windows execution.

## v1.0-rc4 (2026-11-17) — MSVC runtime workspace marker compatibility

- 用户 Windows 11 / VS 2022/MSVC 19.44 构建验证：GUI EXE 链接成功，自定义 manifest 检查通过；CTest 3/7 通过，其余四项因 owner marker 被拒绝而失败。
- 修复二进制读取与 Windows 文本模式写入导致的 CRLF/LF 不一致；新的 owner.txt 使用 binary 输出固定 LF。
- 兼容由 rc3 在 Windows 生成的精确 CRLF 标记（无需删除已有 workspace / journal / quarantine）。
- 拒绝附加内容、非法标记；新增针对 CRLF 标记与实际输出字节的回归测试和 Windows CLI 烟雾检查。
- Windows rc4 CTest 和 GUI/NTFS/WIC 烟雾测试尚待实机复测，禁止宣称 Windows 全面通过。

## v1.0-rc2 (2026-11-17) — MSVC compilation fix

- Fix mixed-type `auto` declaration causing MSVC C3538 in `win_main.cpp`.
- Enable `/utf-8` on all MSVC targets, including tests, to prevent C4819/C2001 under CP936.
- Use explicit `fs::u8path()` for UTF-8 Chinese fixture directories in `test_structure.cpp`.
- Windows native revalidation remains pending; functionality unchanged.

## v0.8 — 自动检测历史备份与融合建议

- 扫描结束后自动生成跨层级历史备份融合候选，采用已逐字节校验的内容重合及相对路径一致性评分。
- 新增“融合建议”选项卡和按钮：展示推荐方向、重合文件、双方差异、冲突及等级，双击后仅生成计划，用户独立确认才执行复制。
- 对真正完全一致的目录、仅同名异内容、系统元数据重合不推荐融合；识别已经合并的同名异版本，重试不误报需复制。
- 建议计算和手动融合计划改由后台线程执行；窄屏按钮栏自动换行。
- 新增 `dc_cli suggest` 只读接口与独立推荐测试；Windows GUI 烟雾测试扩展到临时照片备份的自动推荐及不改动数据的检查。
- 仍保持单目录、两份已验证独立文件副本保留规则，且 Windows SDK/GUI 尚未在 Windows 真实环境验收。

## v0.7 — 增量融合与两份保留安全规则

- 新增历史备份融合，选择两个当前根目录下的互不包含子树：仅补充缺失文件与空目录、跳过完全相同内容、对同名异内容的文件生成保留双方版本的名称。
- 新增源/目标双目录快照核对和跳过文件再校验。复制全部先写到工作区 stage，逐字节确认并请求持久化后，不覆盖地移入目标目录；未完成时只清理 stage，不删除已成功复制的目标文件。
- 同一源目标重复融合具备幂等性；冲突版本已存在且字节相同时跳过。
- 重复清理策略从“保留一份”提高到“活动目录中保留两份”，并且硬链接不计为两份；执行前重新验证保留副本内容及独立文件身份。
- Win32 新增“融合目录”按钮和预览，CLI 新增 `merge`；新增单独的 merge/retention CTest 和中文路径的 Windows smoke 覆盖。
- 当前仅 Linux 核心测试通过；Windows MSVC、WIC、真实设备和断电验证仍为 NOT RUN。

# DiskCurator Native 更新记录

## v0.6 — 发布质量门槛、日志持久化请求与交付修复

- 修复上一版压缩包遗漏 `scripts/build_windows.bat` 的交付问题。
- 事务日志使用 Windows `FlushFileBuffers` / POSIX `fsync` 请求落盘，仍需实盘断电验收。
- 严格禁止伪造事务日志从内部工作区移动文件或将文件移动到内部工作区非合法隔离区域。
- 新增恶意工作区源/目标日志测试，建立可核查的 v1.0 发布质量门槛。
- 维持“Windows SDK/GUI 尚未实测”声明。

## v0.5 — 工作区信任边界、严格日志协议与 GUI 交互自动化

- 校验工作区标记、预留子目录以及日志是否为普通文件/目录，拒绝 junction/reparse points/符号链接，阻止越界恢复风险。
- 事务日志解析改为严格状态机，未知字段、缺失状态、重复记录和非法迁移立即阻断新操作。
- 增加非法 owner、伪造链接、未知记录、截断记录等负面测试。
- Win32 批量重命名和整理归档改用原生复选框，改善短屏幕键盘焦点滚动。
- Windows GUI 自动测试新增中文路径实际扫描、列表/树生成、查询过滤与复位（临时数据）。
- Linux 核心 Release、Debug ASan/UBSan、严格警告构建及三套测试通过。Windows SDK/GUI 构建、启动和交互仍待实测。


## v0.4 — 恢复安全、批量性能与原生 GUI 启动验收

- 事务恢复时严格验证 journal 的根内相对路径、事务 ID、暂存路径编号；损坏和伪造日志立即阻断操作。
- 恢复部分提交的事务时检测“暂存和目标同时存在”或“两者均不存在”冲突，拒绝静默覆盖。允许重建安全的原目录父级。
- 新增 6 类事务故障注入用例，覆盖 PENDING/STAGEDALL 中断、恢复冲突和危险路径。
- 归档同名编号改为递增游标，避免重复尝试已有编号；移动计划的嵌套源检测改为沿祖先路径查询而非 O(n²) 两两比较。
- 4,040 文件合成压力测试在同一 Linux 环境由约 16.8 秒缩短至约 0.6 秒；含 82 个一级目录和 100 组内容重复。
- Windows 保留设备名识别扩展至多重扩展名（例如 CON.backup.txt）。
- Win32 批量配置对话框适配较矮显示器，滚动字段并固定确认/取消。
- Windows 编译脚本和 GitHub Actions 流水线新增 Win32 GUI 启动、TreeView/ListView/TabControl 检查；只有这些在 Windows 真正执行通过，才可声称验收成功。

**状态：** Linux 核心与故障注入/压力测试已实测；Windows SDK/GUI 编译和启动验收仍未在当前环境执行。本版为源码发布候选，尚非 Windows 实机验收的正式版。

## v0.3 — Win32 发布准备与安全回归

- 保持单目录、C++17、Win32 SDK/WIC 原生技术路线，不使用 Qt/MFC/vcpkg。
- 批量重命名、整理归档、重复文件隔离的计划附带源文件大小和时间戳快照；目录重命名附带完整目录项及文件元数据快照，执行前重新核对，发现预览后的新增/修改/删除则拒绝执行。
- 对大量目录/文件的快照规划改为分组一次遍历，避免每个待处理文件再次遍历全部索引。
- 目录对比必须在内容识别完整后才能给出包含/一致判定。
- 批量重命名的目标已存在且不属于此次重命名的源时，在预览阶段拒绝，而不是等执行时才失败。
- 仅经过校验的重复文件可进入保留工作区的隔离路径，减少操作计划误用风险。
- 新增 Windows PowerShell CLI 烟雾测试：中文路径、精确重复文件和目录、改名/撤销、隔离/撤销、WIC BMP dHash、照片日期归档/撤销。
- 构建脚本、GitHub Actions CI 增加 Windows x64 Release ZIP 和 SHA256 输出；MSVC 静态运行库 (/MT)。
- Linux 上用 Debug / Release、GCC 严格警告及 AddressSanitizer + UndefinedBehaviorSanitizer 回归验证核心功能。

**状态：** Windows 平台编译、WIC 和 GUI 尚未由本交付环境实测。请在 Windows 10/11 上使用 `scripts/build_windows.bat` 执行编译与自动测试，并按照 `TESTING.md` 做图形界面验收。

**安全边界：** 文件系统快照属于预览一致性检查，不是对文件操作与外部进程并发修改的严格原子性保证。撤销依赖本机完整的事务日志/隔离文件，不等于独立备份。不提供永久删除操作。

## v0.9 (2026-11-17) - Proposed structure and controlled migration

- Read-only directory-structure suggestions based on verified content overlap and conservative extension categories.
- Editable source-to-target top-level directory mappings, output root label and native target-tree preview.
- Win32 actions: Suggest Structure, Edit Mapping, Preview Migration, Reject Suggestion; execution remains two-step confirmation.
- Original-to-target mapping table (including content-reuse and multiple retained conflict versions).
- Non-destructive staged, byte-verified additive copy transaction with full-root snapshot and changed-content detection.
- Idempotent rescan/replan; pre-existing unowned target folder is blocked.
- New cross-platform structure tests and Windows native UI smoke checks (Windows checks not yet executed).
- Original backups are **not** automatically removed; safe-retirement flow and Windows real-device QA remain outstanding.


## v1.0-rc1 (2026-11-17) — Integrated organizing workspace

- Default native Win32 task-oriented workspace: scan → verified merge suggestions → new structure proposal → editable mappings → preview/confirm → source-specific safe retirement.
- `StructureFileRule` allows an explicit original file → exact final path override. An edit invalidates and recomputes the whole migration plan; explicit target conflicts do not silently overwrite or rename.
- `RetirementProposal` identifies safe old-source files with TWO independent byte-verified active keepers OUTSIDE the specified source folder, listing all blocked files.
- Restricted cleanup only quarantines; it does not delete files permanently or reclaim space. Core transaction recovery and undo remain in force.
- Added integrated regression test and Windows CLI/GUI smoke scripts for the new workflow.
- Windows SDK/native GUI/NTFS/WIC remain NOT RUN in the authoring environment; this is a source release candidate, not a Windows production release.
# 补充：v1.0.1 文件操作后的刷新策略

隔离、重命名、融合、结构构建及撤销后不再自动全范围重算 SHA-256。旧快照明确过期，只读保留，新的分析或文件操作前需用户显式扫描。失败及异常也使结果过期，防止部分变更后继续使用旧计划。可信增量刷新尚未实现。

