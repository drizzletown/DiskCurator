# QA-EVIDENCE v1.0-rc4 — 2026-11-17

## Verified by Windows user (v1.0-rc3; not rc4)

- Visual Studio 2022 Community, MSVC 19.44.35217.0, Windows SDK 10.0.26100.0, x64 Release:
  `DiskCurator.exe` linked successfully.
- Embedded RT_MANIFEST #1 checked: `longPathAware=true`, `dpiAware=true`.
- CTest (v1.0-rc3): 3/7 PASSED. `core`, `merge_and_two_copy_retention`,
  `integrated_workspace_workflow`, and `suggested_directory_structure` FAILED
  with `Invalid DiskCurator workspace marker`. The other three passed.

## Root cause and change

- Windows CRT translated `\n` to `\r\n` when owner.txt was created by
  text-mode `std::ofstream`. Validation opened owner.txt in binary mode and
  compared the first line without removing `\r`.
- rc4 creates canonical LF marker bytes with binary-mode output. Existing
  exact CRLF legacy markers are accepted, without touching the user workspace.
- Validation rejects oversized, appended, or otherwise modified marker files.
- Windows CLI smoke tests the exact new marker bytes.

## Test evidence on current environment

- The new CRLF compatibility regression was confirmed FAILED on rc3 code
  before fixing the code, then PASSED after the fix.
- Linux Release CTest: **7/7 PASS** with GCC `-Wall -Wextra -Wpedantic -Werror`; see `qa/rc4-linux-release-ctest.txt`.
- Clang ASan/UBSan CTest (excluding directory_stress): **6/6 PASS**;
  see `qa/rc4-clang-asan-ubsan-ctest.txt`.
- GCC Debug strict-warning build timed out in the current container and is
  NOT counted as passed.
- The C++17 structured-binding lambda capture warning discovered with Clang
  was removed from tests/test_workflow.cpp.

## Not yet verified

- v1.0-rc4 Windows MSVC CTest and native GUI / NTFS / WIC smoke tests.
- Actual unexpected-power-loss and removable-drive disconnection behavior.
- This remains a release candidate, NOT an accepted stable v1.0.
