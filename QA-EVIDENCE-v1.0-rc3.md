# DiskCurator Native v1.0-rc3 — Windows Manifest 重复资源修复与验收证据

## 来自真实 Windows 构建的缺陷报告

用户在 Windows 11、Visual Studio 2022 Community / MSVC 19.44、Windows SDK 10.0.26100.0 构建 v1.0-rc2 时报告：

```text
CVTRES : fatal error CVT1100: 资源重复。类型: MANIFEST，名称: 1，语言: 0x0409
LINK : fatal error LNK1123: 转换到 COFF 期间失败: 文件无效或损坏
```

同时报告 `dc_tests.exe` 等目标已生成，故障集中在 Win32 GUI 的资源链接阶段，而非 C++ 核心库。

## 根因（源码直接验证）

- `src/app.rc` 已有 `1 RT_MANIFEST "app.manifest"`。
- CMake 构建的 MSVC GUI 链接也可能自动生成 `RT_MANIFEST` 资源 #1。
- 两个同 ID 的 MANIFEST 资源冲突，导致 CVT1100 和连带的 LNK1123。

## 修复

- 对 **DiskCurator GUI 目标** 使用 MSVC `target_link_options(DiskCurator PRIVATE /MANIFEST:NO)`，保留 `app.rc` 中原有自定义 manifest；其他测试目标不改变。
- 更新 RC 文件和 manifest 的版本标记为 rc3。
- 新增 `scripts/check_manifest_windows.ps1`：利用 SDK `mt.exe` 从实际构建的 `DiskCurator.exe` 提取 `RT_MANIFEST #1`，验证 longPathAware 与 dpiAware 均为 true。Windows 一键构建与 GitHub Actions 均会执行此检查；检查不通过立即中止发布。
- 继续保留之前为中文路径及测试编码添加的 `/utf-8`。

## 验证状态

| 检查项 | 状态 |
| --- | --- |
| CMake 中 manifest 唯一所有权（静态检查） | 通过 |
| Linux C++17 Release CTest | **7/7 通过，2026-11-17 当前环境实测，0.50 秒** |
| Windows MSVC / 资源编译 / 链接 | **未执行：当前环境无 Windows SDK** |
| `mt.exe` 内嵌清单验证 | **未执行：需在 Windows 上构建后运行** |
| GUI / NTFS / WIC / 移动硬盘数据安全 | **未执行** |

本轮 Linux 原生 CMake 配置/编译/CTest 已执行：7/7 全部通过（测试记录：`qa/rc3-linux-release-ctest.txt`）。

此修复尚需用户在 Windows 上运行 `scripts\build_windows.bat` 实测。不得将源码级修复描述为 Windows 构建已通过或正式 v1.0 已发布。
