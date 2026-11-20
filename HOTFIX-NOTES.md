# DiskCurator Native 1.0.0 — GUI Smoke PowerShell Hotfix 1

This fixes a Windows PowerShell parser error at `scripts/smoke_gui_windows.ps1:70`:

- Before: `throw "GUI $phase: expected $expected ListView rows, got $actual"`
- After: `throw "GUI ${phase}: expected $expected ListView rows, got $actual"`

It also strengthens `scripts/check_source_layout.ps1` to parse all `.ps1` source
files with the PowerShell AST parser before the time-consuming C++ build.

**Scope:** PowerShell smoke-test and preflight scripts only. No C++ source,
file management logic, file formats, or application binaries were changed.
The application version stays **1.0.0**.

## Apply

Copy the two files from `scripts/` into the existing project `scripts/`
directory, preserving the file names. They contain UTF-8 BOM for Windows
PowerShell 5.1 Chinese text compatibility.

Run in the project directory, with the existing compiled EXE:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check_source_layout.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_gui_windows.ps1
```

When this succeeds, continue the release gate (it will re-run tests but does
not need to recompile C++):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\mark_windows_validation.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_release.ps1
```

**Verification:** Source inspection, UTF-8 BOM, ZIP integrity, and static check
for unbraced `$variable:` tokens passed in the packaging environment.
Windows PowerShell parsing and GUI interaction still require the user's
Windows machine. Successful smoke tests do not demonstrate safety against
USB disconnection, power loss, or faults on actual backup drives.
