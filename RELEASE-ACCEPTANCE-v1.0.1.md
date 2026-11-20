# DiskCurator Native 1.0.1 — Actual validation record

Date: 2026-11-20. Baseline: `DiskCurator-Native-v1.0.0-source-hotfix1.zip`.
This record covers the result workspace, visible scan progress and performance changes in this source distribution. Earlier release logs are historical and do not substitute for the checks below.

## Executed in this revision

| Check | Actual result | Evidence / limit |
|---|---|---|
| Linux C++17 Release build and regression | **PASS, 16/16** | `qa/v1.0.1-linux-release-ctest.txt`; GCC 14.2; Release assertions active |
| ASan + UBSan for the five changed scan/observation/action/scope tests | **PASS, 5/5 with leak detection disabled** | `qa/v1.0.1-linux-asan-ubsan-no-lsan.txt`; Debug, `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0` |
| LeakSanitizer | **NOT COMPLETED — runtime unsupported here** | First attempt exits with “LeakSanitizer does not work under ptrace”; original failure retained in `qa/v1.0.1-linux-sanitizers.txt`. No leak-clean claim is made. |
| Windows x64 cross compilation and link | **PASS** | `qa/v1.0.1-windows-cross-compile.txt`; MinGW GCC 14.2; native GUI/resources, CLI, diagnostic tool and 16 test executables. This is not Windows execution. |
| Final GUI strict compile | **PASS** | MinGW `-Wall -Wextra -Wpedantic -Werror -fsyntax-only`; final source SHA-256 `890604fa1bd436c9fb29ace12156d27d5b9fd2d4ccf7f11bc7849d3858589d27`. Full cross link above follows the final change. |
| PowerShell source preflight | **PASS** | `qa/v1.0.1-powershell-preflight.txt`; PowerShell 7.4.19 on Linux; 7 scripts parsed, UTF-8 BOM checked, 50 required files present. Not Windows PowerShell 5.1 execution. |
| GUI smoke embedded C# interoperability code | **COMPILE PASS ONLY** | `qa/v1.0.1-gui-smoke-interop-compile.txt`; no user32 calls executed |
| Source ZIP / patch reconstruction and SHA-256 | See packaging record | `qa/v1.0.1-packaging.json` is generated during packaging; no compiled binaries are distributed in these source archives. |

New regression coverage includes known SHA-256 vectors, block boundaries, zero-byte files, multi-root counters, cancellation within a large file, enumeration errors and missing files, complete/incomplete directory observations, K=0/1/3 and fast-mode policy behavior, fake hash collisions, hard links, and scoped A/B comparison/merge. The scoped test's two 1 TiB entries are synthetic offline metadata, not a real 1 TiB disk test.

## Performance evidence and meaning

| Workload | Before median | After median | Interpretation |
|---|---:|---:|---|
| In-memory observations: 500,000 files / 10,001 directories | 0.738192 s | 0.474773 s | About 36% less time for this computation |
| Same workload, whole-process peak RSS | 271,384 KiB | 188,680 KiB | About 30% lower peak memory; includes the input inventory |
| Full portable SHA scan: 20,000 files × 257 bytes | 0.481405 s | 0.404526 s | About 16% less time in this warm-cache Linux microbenchmark |
| Full portable SHA scan: one 64 MiB file | 0.235850 s | 0.240510 s | About 2% difference; does not establish a large-file speedup |

Raw runs and fixture/compiler details: `qa/observation-scaling.json` and `qa/hash-scan-linux-microbenchmark.json`. Shared Linux host, no cache flush. These figures do not measure Windows CNG or the user's USB HDD and do not predict the completion time for a 1 TB drive. `dc_benchmark --scan` measures the actual full-hash path; process read bytes are logical I/O rather than physical disk traffic.

## Required Windows evidence still outstanding

| Gate | Status in this environment |
|---|---|
| Visual Studio 2022 / MSVC Release build and embedded-manifest check | **NOT RUN** |
| Windows Release CTest, including Windows CNG known-vector behavior | **NOT RUN** |
| Windows CLI and retention-policy smoke | **NOT RUN** |
| Windows PowerShell 5.1 and actual native GUI smoke | **NOT RUN** |
| Actual HDD/SSD throughput, NTFS/exFAT, device removal and occupied-file behavior | **NOT RUN** |
| Windows GUI manual acceptance: progress/ETA, scoped actions, stale-result guard and layouts | **NOT RUN** |
| Packaged Win64 binary smoke | **NOT RUN — no Win64 binary release supplied here** |

`scripts/build_windows.bat` retains the native Windows gates. Only after they pass does `package_release.ps1` package the matching tested GUI/CLI binaries. Source updates and cross-compiled objects are not represented as a tested Windows binary release. Manual hardware and fault tests must use expendable fixtures, not valuable original data.

## Current product limits

Every primary scan rereads all readable ordinary files and recomputes SHA-256. Cross-session incremental cache skipping, resumable scans and trusted incremental post-operation refresh are not implemented. File-changing tasks now invalidate the visible snapshot and require an explicit rescan before more analysis or writes; they do not implicitly re-read the whole drive. A blocked synchronous operating-system disk read can delay cancellation until that call returns.
