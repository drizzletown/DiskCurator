# DiskCurator Native v1.0-rc6：三层 UI 质量检查与验证边界

| 项目 | 证据 | 本轮状态 |
|---|---|---|
| 核心代码和 API | 直接沿用 rc5，无修改 | 源码对比确认 |
| Linux Clang Release CTest | `ctest --test-dir build --output-on-failure` | 7/7 PASS |
| GUI 三层导航 | `src/win_main.cpp`，3 个模式按钮 + 6 阶段 | 已编码，Windows 未实测 |
| 基础工具可用性 | 原 13 个视图 + CSV/撤销保留 | 已编码，待 GUI 验收 |
| Win32 GUI 自动化 | `scripts/smoke_gui_windows.ps1`（调整为新导航） | 已更新，待真实 Windows 执行 |
| 依赖和源包 | 仅 Win32/Common Controls，Release build 不新增依赖 | 静态确认 |
| PowerShell 5.1 中文兼容 | 两个烟雾脚本保持 UTF-8 BOM | 静态检查 |
| Win32 MSVC 19.44 构建 | 用户此前提供 rc4 编译和 7/7 测试 | **本轮新版尚未执行** |
| WIC/EXIF 与真实 NTFS | 必须使用真实 Windows/磁盘 | 未执行 |
| 断电/USB 故障 | 没有安全结论 | 未执行 |

## Windows 运行命令

```powershell
.\scripts\build_windows.bat
# 构建后测试时可以执行：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1
```

### 测试新交互而不是旧 UI

1. 启动后有“智能整理 / 主题工具 / 基础工具”三种模式。
2. 默认智能模式在无扫描时给出“开始扫描”；扫描之后给出不超过一屏的概要。
3. 主题层可以从“历史备份融合”进入候选；切回智能层，主按钮依次进入候选与目标结构。
4. 预览之前不创建 `整理结果` 和 `.DiskCurator-System`；自动化只验证非破坏性工作流。
5. 目标树/旧→新映射可被审查；执行仍需独立用户确认。

### 已知限制

- 当前采用 Win32 原生 GDI 自绘而非完整设计系统，尚需 Windows 实机下字体、绘制、DPI 和控件兼容验证。
- 目录树及 ListView 仍沿用 Win32 Common Controls，本版不提供跨平台 GUI。
- UI 改进不等于存储引擎具有物理介质断电一致性保证。
