# DiskCurator Native v1.0-rc5 — Windows PowerShell 5.1 smoke script fix

## Source change

- Added a UTF-8 BOM to `scripts/smoke_windows.ps1` and `scripts/smoke_gui_windows.ps1`. Windows PowerShell 5.1 uses the active Windows ANSI code page for BOM-less scripts; Chinese fixture paths previously parsed incorrectly.
- `scripts/check_source_layout.ps1` now rejects non-ASCII PowerShell scripts lacking a valid UTF-8 BOM, before any build/test runs.
- Windows GitHub Actions smoke steps explicitly invoke Windows PowerShell (`powershell.exe`) rather than just PowerShell 7 (`pwsh`), testing the interpreter used by `build_windows.bat`.
- Source and release package version labels synchronized to rc5.

## Windows evidence

The user's rc4 log confirms MSVC x64 GUI link success, correct manifest, and 7/7 CTest passes (2026-11-17). The CLI smoke script then failed at PowerShell parse time. Those CLI/GUI smoke tests remain **not verified on Windows for rc5** until the patched scripts are run on Windows.

## Recommended steps

For a small update, overlay the hotfix ZIP onto your rc4 source directory preserving paths; then run `scripts\build_windows.bat`. You may keep `build-win`: no C++ files or CMake settings changed. For a clean rc5 source distribution, unpack the full ZIP. Testing scripts use temporary fixture directories; no real user backup directory is modified by the smoke tests.

## Release caution

This is still a release candidate, **not** certified safe for irreplaceable data. Windows NTFS/WIC GUI end-to-end smoke and physical-disk failure testing are pending. User data under `.DiskCurator-System` must never be removed to resolve build problems.
