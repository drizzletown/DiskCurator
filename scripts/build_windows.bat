@echo off
setlocal
cd /d "%~dp0.."
if exist build-win\diskcurator-windows-validation.txt del /q build-win\diskcurator-windows-validation.txt
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check_source_layout.ps1
if errorlevel 1 exit /b 9
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake is required. Install Visual Studio 2022 with C++ Desktop and CMake tools.
  exit /b 2
)
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DDC_BUILD_TESTS=ON -DDC_BUILD_PERF_TOOLS=ON
if errorlevel 1 exit /b 3
cmake --build build-win --config Release --parallel
if errorlevel 1 exit /b 4
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\mark_windows_validation.ps1
if errorlevel 1 exit /b 5
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package_release.ps1
if errorlevel 1 exit /b 8
echo.
echo Build complete. Start the GUI from the portable ZIP.
echo Release ZIP: see dist\ for the validated ZIP and SHA-256 checksum
endlocal
