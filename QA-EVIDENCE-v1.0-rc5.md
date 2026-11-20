# QA evidence — DiskCurator Native v1.0-rc5

- **Windows (user-provided rc4 log, 2026-11-17):** MSVC 19.44 successfully built `DiskCurator.exe`; manifest check passed (`longPathAware=true`, `dpiAware=true`); all seven CTest cases passed (35.06 seconds). CLI smoke was blocked by PowerShell 5.1 parse-time mojibake, so GUI smoke was not reached.
- **rc5 change under test:** UTF-8 BOM added to both smoke scripts, which contain Chinese characters. A strict UTF-8 BOM check now runs as part of source layout validation. This is a targeted fix for the observed parsing error, not evidence that Windows smoke tests have passed.
- **Pending:** re-run full Windows CLI and GUI smoke tests (including WIC and NTFS interactions); real device power-loss, removal and filesystem-fault testing. Do not declare v1.0 final before these pass.
