# DiskCurator rc8 performance profiling instrumentation evidence

Date: 2026-11-18. This is a development addition to the rc8 source archive, **not** a new Windows-verified release.

Changed files: `CMakeLists.txt`, `README.md` (documentation only), `tools/perf_benchmark.cpp`, and `docs/design-v2.1/15-性能架构与验收基线.md`. The existing `src/core.cpp`, `src/win_main.cpp`, operation safety and all tests are byte-for-byte unchanged.

- Linux Release optional `dc_benchmark` build: PASS.
- Linux Debug `-Wall -Wextra -Wpedantic -Werror`: PASS.
- Linux existing CTest suite, independently rebuilt with optional benchmark target: 8/8 PASS.
- Linux synthetic fixture used for smoke (900 tiny files): metadata scan 8 ms; deduplicate 14 ms; directory equality 0 ms; overlap 0 ms; pair 2 ms. These figures only prove instrumentation works and are **not** representative of real disks.
- Windows MSVC / NTFS / GUI benchmark tool: NOT RUN.

Benchmark is read-only and does not create a `.DiskCurator-System` workspace. It reports OS logical process read counters, not physical drive I/O. For reliability, all speed results must be recorded with volume type, operating system, cold/warm cache condition and dataset manifest.

Future optimization claims require before/after benchmarks on the same hardware and byte-identical correctness outputs; the instrumentation change alone is not a performance improvement.
