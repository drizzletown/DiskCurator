# DiskCurator · 磁盘文件整理助手 1.0.9

在“隔离多余副本”的预览中可看到拟移动的文件数、大小，以及按策略原样保留的组数。默认每组至少保留两份，因此可以查到重复文件，同时无需移动任何文件。命令行 `dc_cli clean ROOT` 也会显示这项说明。

Windows 使用扩展长度路径读取旧事务日志时，现在能正确还原日志中的相对路径。已验证旧版创建的隔离记录可以撤销。

[下载 Windows 便携版](https://github.com/drizzletown/DiskCurator/releases/latest) · [使用指南](START-HERE.txt) · [验收记录](RELEASE-ACCEPTANCE.md)
