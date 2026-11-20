# DiskCurator Native v1.0-rc7 — GUI 测试修复与真实验收状态

日期：2026-11-18。基础版本：rc6，文件管理核心保持不变。

## 用户实际 Windows/MSVC 证据（来自 rc6 构建日志）

| 项目 | 结果 |
|---|---|
| Visual Studio 2022 / MSVC 19.44 x64 构建 | PASS，DiskCurator.exe 生成 |
| RT_MANIFEST #1 longPathAware / dpiAware | PASS |
| CTest 七套核心、恢复、性能、融合、推荐、工作流、目录结构测试 | PASS，7/7，总 32.29 秒 |
| Windows CLI 烟雾测试 | PASS（含中文路径、WIC、相似度、归档、融合、双副本等） |
| GUI 原生启动 | PASS（主窗口，TreeView、ListView、TabControl 创建） |
| GUI 内容筛选 | BLOCKED（测试脚本使用跨进程 SetWindowText，实得 5 行而预期 1 行） |
| GUI 融合导航、结构树、预览 | NOT RUN（因上一项停止） |

## rc7 修改范围

1. `scripts/smoke_gui_windows.ps1`：改为跨进程 `SendMessageW(WM_SETTEXT)`；使用 `WM_GETTEXT` 字符串回读，避免假的输入成功；使用 `LVM_GETITEMCOUNT` 有界重试验证实际刷新。**没有伪造 `EN_CHANGE` 消息**，如果编辑事件路径出错，测试仍会失败。
2. `src/win_main.cpp`：更名局部 `confidence_label`，修复 MSVC C4458 警告，不改变算法或事件路径。
3. 版本同步：Windows 构建回显、Release 包和 GitHub Actions 工件更新为 rc7。

## 未验收项

- rc7 补丁在 Windows PowerShell 5.1 上的实际 GUI 过滤及后续导航测试：NOT RUN。
- Windows 高 DPI、键盘可访问性、文件操作真实移动硬盘故障注入：NOT RUN。
- 物理断电后数据/日志完整性：NOT RUN。

## 复测

覆盖最小补丁的 `scripts/smoke_gui_windows.ps1` 后无需重新编译：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1
```

若使用 rc7 完整源码，请运行 `scripts\build_windows.bat`。只有 GUI 烟雾测试真正通过才能将相关验收状态改为 PASS。
