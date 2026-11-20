# DiskCurator Native v1.0-rc2 — Windows MSVC 构建热修版本

此版本修复 v1.0-rc1 首次 MSVC 构建时暴露的 C3538、C4819、C2001 错误，功能与数据安全策略无有意变更。

请替换**整个解压目录的源码**，或者使用提供的三文件补丁，随后执行：

```powershell
Remove-Item -Recurse -Force .\build-win -ErrorAction SilentlyContinue
.\scripts\build_windows.bat
```

如果仍然失败，请保存**从第一个 error 开始**的完整输出；不要只粘贴最后一行。

在通过 Windows SDK 编译、GUI/WIC/NTFS 自动验收及实测之前，**不能将本源码候选版作为正式可靠的 Windows 可执行程序**。
