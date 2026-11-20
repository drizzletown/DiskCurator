# DiskCurator Native v1.0-rc6 · 三层 Win32 交互重构候选

2026-11-18

本次交付不是正式稳定版，不改变文件操作算法。对 `v1.0-rc5` 的变化集中于 `src/win_main.cpp`、Win32 资源版本、GUI 烟雾测试、文档与发布构建版本：

- 默认“智能整理”，提供扫描、发现、建议、修改、执行预览、复核六步流程；扫描结果首先展示总结，而不是大批逐文件动作。
- “主题工具”把历史备份融合、精确重复、照片、命名、归档、旧备份清退和目录比较做成任务入口。
- “基础工具”保留 13 类原始数据视图，保留筛选、排序、CSV 导出、撤销与历史。
- 原先横向排满的十几个按钮及 13 个可见页签隐藏；底层动作与控件 ID 保留，便于软件复用和自动化核验。
- 深色导航区、浅色工作区、GDI 自绘单一主操作按钮和清晰的任务阶段；使用系统的 Win32 TreeView、虚拟 ListView、ComboBox。
- 后台任务执行期间禁用其他写入入口；不可取消的写任务执行时阻止关闭主窗口。
- GUI 烟雾测试由原来点击旧按钮调整为真实三层模式导航、智能阶段推进及只读结构预览。

### 验证记录

- 当前 Linux 环境的 Clang Release `ctest`：**7/7 PASS**。
- `core.hpp`、`core.cpp`、`photo_windows.cpp`、`photo_portable.cpp`、`cli.cpp` 内容与 rc5 **字节一致**。
- Windows/MSVC 的新版界面代码尚未实机编译/启动，原有用户提供的 Windows 7/7 CTest 证据属于之前的 rc4/rc5 核心，不可移作 rc6 GUI 完整验收结果。
- WIC、EXIF、NTFS、GUI 高 DPI 和交互仍需用户 Windows 环境验收。

请先在非重要数据的临时测试目录使用，不可在唯一本地备份上测试清退或迁移。

### Windows 验证

```powershell
.\scripts\build_windows.bat
# 单独核验新 Win32 流程（需要先构建）：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1
```
