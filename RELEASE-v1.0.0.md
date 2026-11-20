# DiskCurator Native 1.0.0 — Release notes

**Code version:** 1.0.0 (C++17, native Win32). **Design baseline:** v2.4 for selected-drive scanning and observation; earlier retained-policy / merge design remains applicable.

## Shipped functionality

- On startup choose a set of local logical drives (none preselected), or a single folder; scanning begins only after explicit action.
- Full SHA-256 scan of every readable ordinary file; excludes the application cache and reports unreadable/reparse entries. Index is persisted in the local user cache; incremental cache reuse is NOT implemented.
- Read-only multi-drive observations: drive inventory, file hash candidates, directory cumulative size and Merkle-like tree candidates, shared-hash groups, scan issues. Cross-drive matches are not byte-proven duplicates.
- Single-root verified operations: exact duplicate files and directories, directory difference/overlap/pair evaluation, manual quarantine with a user-specified retention policy, non-overwrite merge, suggested new structure and edit mappings, batch rename, date/type archive, WIC/dHash similar-photo hints, history and undo for eligible moves.
- Operations use a preview/confirmation workflow and have a separate transaction workspace.

## Safety limits

- No permanent deletion; quarantine does not reclaim space. No cross-volume direct deletion or fusion.
- Merge/structure creation is additive and does not automatically remove the originals. Undo of created copies is intentionally restricted.
- A SHA-256 match does not prove byte equality; exact duplicate operations use byte verification by default.
- Configurable retention: 0–32 active keeper paths, independence on/off, and byte-verification on/off. Default is 2 distinct byte-verified files. Choosing 0 or disabling byte checks reduces protection.
- Current application cannot prove backup independence across physical disks, and does not guarantee crash/USB-disconnect/power-loss atomicity.
- Large USB drives require full reads for each scan. Cache reuse, incremental indexing, and real space reclamation remain out of scope for this release.

## Packaging / build

Run `scripts\build_windows.bat` from a source checkout with VS 2022 C++ Desktop tools and Windows SDK. It compiles, checks the embedded application manifest, executes ALL CTest suites, runs CLI/policy/GUI smoke tests in temporary directories, then produces `dist\DiskCurator-Native-v1.0.0-Win64.zip` and a SHA256 file **only after the tests pass**.

The Win64 ZIP contains `DiskCurator.exe`, `dc_cli.exe`, release notes, usage and safety instructions and per-binary checksums. The binaries are not Authenticode-signed. There is no installer or auto-updater.

## Verified vs unverified

The 1.0.0 **source build** has passed Linux C++17 Release CTest including an assertion-enabled test: 12/12. Clang strict warnings and tests: 12/12. Clang ASan/UBSan: 11/11 non-stress tests passed; sanitized stress did not finish within this environment's limit. Previous Windows logs from the user confirm rc6 Windows MSVC and 7/7 CTest/CLI smoke success, not 1.0.0 itself.

**Not yet verified in this delivery environment:** Windows/MSVC 1.0.0 build, the selected-drive GUI, Win32 screenshots/usability, NTFS/exFAT, USB unplug/replug, WIC photo workflows on physical devices, and fault injection involving real hardware. Therefore it is not accurate to claim a Windows production-stable release based solely on this ZIP. The final sign-off checklist is in `RELEASE-ACCEPTANCE.md` in the source distribution.
