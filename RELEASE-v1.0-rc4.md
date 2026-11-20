# DiskCurator Native v1.0-rc4 — Windows workspace marker fix

**Target:** C++17 / Windows SDK / Win32 native UI; no Qt or MFC.

### Windows validation received

The user's v1.0-rc3 Windows MSVC release build produced `DiskCurator.exe`
and verified its RT_MANIFEST. CTest reported 3 passed and 4 failures, all
from `Invalid DiskCurator workspace marker`.

### Fix in rc4

- New workspaces: `owner.txt` is written with canonical LF bytes in binary mode.
- Existing rc3 Windows workspaces: strictly accept the old CRLF representation.
- Rejected: arbitrary/extra owner content, symlinks and unsafe workspace types.
- Extra portable regression tests and real Windows CLI smoke check are included.

### Rebuild

```powershell
Remove-Item -Recurse -Force .\build-win -ErrorAction SilentlyContinue
.\scripts\build_windows.bat
```

Only remove the **build-win build cache**, never the user's
`.DiskCurator-System` journal or quarantine directories.

### Acceptance

Current environment: portable core tests passed. Windows MSVC rc4 tests,
native GUI, NTFS/WIC and actual hardware fault injection still require
Windows execution. No permanent deletion is implemented.
