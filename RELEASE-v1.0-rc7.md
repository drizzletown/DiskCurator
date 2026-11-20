# DiskCurator Native v1.0-rc7 — GUI 自动化兼容修复

- 仍使用 C++17、Win32 SDK。底层文件管理核心与 rc6 字节一致。
- Windows MSVC 已由用户验证 rc6 编译、链接、7/7 CTest 和 CLI 烟雾测试成功；GUI 筛选测试失败。
- 修复 `smoke_gui_windows.ps1`：不再跨进程调用 `SetWindowText` 修改 Edit 控件；改为 `SendMessageW(WM_SETTEXT)`、`WM_GETTEXT` 回读、轮询真实结果列表。
- 修复 `win_main.cpp` 的 C4458 局部变量隐藏成员警告。
- 版本号/发布包/CI 文件名同步为 rc7。
- **Windows rc7 GUI 烟雾测试仍待实机复测**；没有假定其成功。重要数据整理仍需手工复核与更多真实设备验收。
