# Windows 构建与验收

每个通过结论绑定到一个构建及其执行记录。需求、通过条件和对应的 CTest/脚本见[需求矩阵](REQUIREMENTS.md)。本版结果见[发布验收记录](RELEASE-ACCEPTANCE.md)和 [Windows 自动验收日志](qa/v1.2.0-windows-validation.txt)。

## 构建并执行发布检查

在装有 Visual Studio 2022 C++ 桌面工作负载、Windows SDK、CMake 3.21+ 的 Windows x64 机器上，从工程根目录运行。GUI 检查需要可交互的 Windows 会话。

```powershell
.\scripts\build_windows.bat
```

脚本先检查源码清单和 PowerShell 5.1 解析，再构建 Release，检查嵌入清单并执行 CTest、中文路径 CLI 工作流、保留策略、原生 GUI 和不可写缓存场景。发布脚本验证测试凭据及二进制哈希后生成便携包。任何检查失败时，保留相应的错误输出和 CTest 的 `build-win/Testing/Temporary/LastTest.log`，修复后重新执行发布检查。

针对已构建的 Release，可以单独复跑范围与故障恢复的回归，或启动使用临时夹具的界面测试。

```powershell
ctest --test-dir build-win -C Release -R 'selected_directory_pair_scope|recovery_fault_injection' --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1 -CacheUnavailable
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1 -ScreenshotDir build-win\ui-captures
```

## 需要单独留存结果的场景

| 场景 | 操作 | 判定及记录 |
|---|---|---|
| 两盘候选、单根精确数和保留者 | 两个测试卷各置一份相同内容，在其中一卷另放一个同内容文件。勾选两盘扫描，切换该卷，运行精确核验，按 K=2 预览隔离其中一条。 | 总览候选数为 3，该根精确数为 2。隔离一条会使该根只剩一份，应阻止执行；另一卷不可补足 K。导出显示行，核对所属根、计数标签和导出范围。 |
| 过期结果及取消 | 在临时根预览并确认改名。查看最近操作，返回结果并导出；开始一次显式重扫后请求停止。 | 操作后的状态标明过期。过期结果能查看，不能再用于写入。重扫取消后不可恢复原计划。分别记录成功、部分执行及异常场景。 |
| 显示上限、搜索与布局 | 在测试根生成超过 5000 个大小有差异的文件，切到文件视角并搜索一个小文件。在最小窗口和不同 DPI 下打开策略、预览和导出。 | 无筛选时有截取说明，搜索能找到未直接展示的文件，全选/导出与当前显示集合一致，中文长路径、确认和取消控件可访问。 |
| 介质与故障恢复 | 使用可废弃的测试介质和已记录哈希的夹具，分别演练同盘符换卷、扫描断连、暂存空间不足和提交阶段断电。重新连接后读取日志并比对源、目标、暂存、隔离区。 | 卷变化拒绝复用旧选盘库存。MOVE 和 COPY 按各自语义恢复，保留冲突现场供人工处理。记录设备、文件系统、构建哈希、故障阶段、恢复状态和文件集合；每种实盘故障独立记录。 |
| 实际大盘性能 | 在固定的 HDD/SSD、数据集和缓存条件下运行随构建提供的诊断工具 `dc_benchmark.exe ROOT --scan`，同时记录机器和设备信息。 | 记录枚举、内容读取和观察耗时、访问量、吞吐及内存。GUI 阻塞等待与取消另记实际等待时间。比较两个构建时复用同一夹具和介质条件。 |

旧盘 GUI 回归使用同内容的多份文件、两套各有独有文件和冲突版本的旧目录，覆盖按组隔离、归档与收起原件、手工重映射、书签与写后刷新。

自动测试使用临时文件或合成库存。设备断电、换卷、多 DPI 与大容量吞吐的状态以实测记录为准。[v2.6 端到端验收场景 A-01～A-17](docs/design-v2.6/06-acceptance.md)
