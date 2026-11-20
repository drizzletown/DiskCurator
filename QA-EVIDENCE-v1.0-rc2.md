# DiskCurator Native v1.0-rc2 — MSVC 构建热修质量证据

## 用户 Windows 2022 MSVC 真实构建反馈

- Visual Studio 2022 Community，MSVC 19.44.35217，Windows SDK 10.0.26100.0。
- v1.0-rc1 首次 Windows 构建：`diskcurator_core.lib`、若干测试可执行文件和 CLI 已生成；`DiskCurator.exe` 与 `dc_structure_tests.exe` 编译失败。
- 两个证实的问题：`src/win_main.cpp:349` 的混合类型 `auto` 声明触发 C3538；`tests/test_structure.cpp` 的 UTF-8 源码未在该测试目标上开启 `/utf-8`，在代码页 936 下报 C4819/C2001。

## v1.0-rc2 实际修复

1. 拆分 GUI 启动工作线程前的六个不同类型的 `auto` 快照变量。
2. 在 CMake 的 `MSVC` 分支统一 `add_compile_options(/utf-8)`，覆盖 GUI、核心、CLI 与所有测试目标；保留 GUI 和核心 `/W4 /EHsc`。
3. 在中文目录 fixture 的测试中使用 `fs::u8path(u8"...")`，避免文件系统窄字符串依赖 Windows 当前 ANSI 代码页。
4. 更新构建/打包/CI 文件的发行版本号和发布清单。

## 当前可验证结果

- Linux CMake C++17 Release：7/7 CTest 通过（包括 4,040 文件的压力测试）。
- 全部源码、CMake 和 Windows 构建脚本纳入完整源码 ZIP，另有仅含修复文件的补丁包。
- **v1.0-rc2 Windows MSVC 编译：尚未复测；需要用户重新运行 `scripts\build_windows.bat`。**
- **Windows GUI、NTFS、WIC、断盘与故障恢复：尚未完成正式验收。**

此文档只记录真实已有证据，不能将 Linux 通过等同于 Windows 通过。禁止用于唯一重要备份。
