# DiskCurator Native v1.0-rc3 — Windows manifest 链接热修版

## 本次改动

- 修复 v1.0-rc2 真实 Windows 构建报告的 `CVT1100: duplicate MANIFEST, name 1` 与 `LNK1123`。
- `src/app.rc` 继续提供 `RT_MANIFEST #1`；只对 Win32 GUI target 禁用 MSVC 自动 manifest (`/MANIFEST:NO`)。
- Windows 构建流水线在链接成功后，强制验证 `DiskCurator.exe` 实际嵌入的 manifest、长路径与 DPI 声明。
- 所有核心文件整理功能和两副本规则不作有意修改。

## Windows 构建

在 Windows PowerShell、已安装 VS2022 Desktop C++ 与 Windows 10/11 SDK 的环境里，从项目根目录执行：

```powershell
Remove-Item -Recurse -Force .\build-win -ErrorAction SilentlyContinue
.\scripts\build_windows.bat
```

`build_windows.bat` 将构建、检查 manifest、执行 CTest、Windows CLI/GUI smoke tests 和打包。若链接后清单验证失败，会明确停止，而非生成未经检查的发布包。

**状态：Windows 原生 SDK 实测尚待执行；请使用测试文件夹，不要用唯一重要备份测试。**
