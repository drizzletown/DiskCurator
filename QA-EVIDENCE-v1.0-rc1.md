# DiskCurator Native v1.0-rc1 — 可复现质量证据

执行日期：2026-11-17，环境：Linux 容器（不含 Windows SDK、Visual Studio 和原生 GUI 会话）。

## 在当前执行环境确实运行过的检查

1. C++17 GCC Release：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure`。结果 **7/7 PASS**，含压力测试（约 4,040 文件）。原始结果 `qa/release-ctest.txt`。
2. C++17 GCC Debug 严格警告：`-Wall -Wextra -Wpedantic -Werror` 编译无警告错误，`ctest -E directory_stress` **6/6 PASS**；未运行 Debug 压力用例。结果 `qa/debug-strict-ctest.txt`。
3. C++17 Clang Debug ASan/UBSan：`-fsanitize=address,undefined`（`-O0 -g0`）编译，`ASAN_OPTIONS=detect_leaks=1 ctest -E directory_stress` **6/6 PASS**；未运行消毒器压力用例。结果 `qa/clang-asan-ubsan-ctest.txt`。
4. 独立 CLI 临时目录验证：`dc_cli structure ROOT 整理结果 --apply --yes` → `dc_cli retire ROOT old`（`eligible=1, blocked=1`）→ `dc_cli retire ROOT old --apply --yes` → `dc_cli undo ROOT`。结果 **PASS**，旧目录中副本不足文件原样保留；明确确认前不会修改。

## 特别关注的安全断言

- 目录建议和文件级映射只读；不能把已有异内容文件当作目标、不能越界或指向 Windows 保留设备名。
- 修改逐文件映射必须重新计算操作预览；手工精确目标不被自动内容去重悄悄覆盖。
- 完整迁移保留源目录，复制后逐字节核验完整旧→新映射。
- 待清退文件必须具备**被选择旧目录之外的两份**字节一致、物理文件身份不相同的活动副本。仅有一个外部副本时拒绝。
- 若旧文件内容在预览后改变，即使大小与 mtime 恢复原值，执行阶段仍拒绝操作。
- 中断恢复日志、重名冲突与事务撤销均由回归测试覆盖。

## 本执行环境没有验证的项目

**NOT RUN：** Windows MSVC 编译、原生 GUI 交互、WIC/EXIF、NTFS/ACL、USB 断连、断电一致性和 Windows 自动化脚本。必须在 Windows 机器或 GitHub Actions windows-2022 运行 `scripts/build_windows.bat` 才能获取 Windows 自动构建证据。自动测试通过也不能替代真实移动硬盘故障验收。

结论：源码候选版，**不能标为正式稳定 v1.0**。无永久删除实现；隔离不释放磁盘容量。
