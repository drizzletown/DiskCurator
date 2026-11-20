param([string]$BuildDir = 'build-win')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $project $BuildDir }
$receipt = Join-Path $BuildDir 'diskcurator-windows-validation.txt'
# A failed revalidation must never leave an older acceptance receipt usable.
if (Test-Path -LiteralPath $receipt) { Remove-Item -LiteralPath $receipt -Force }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check_source_layout.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Source/parser preflight failed' }
$exe = Join-Path $BuildDir 'Release\文件查重与整理助手.exe'
$cli = Join-Path $BuildDir 'Release\dc_cli.exe'
$testLog = Join-Path $BuildDir 'Testing\Temporary\LastTest.log'
foreach ($f in @($exe,$cli)) { if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { throw "Cannot seal validation receipt; missing $f" } }
$version = [IO.File]::ReadAllText((Join-Path $BuildDir 'diskcurator-version.txt')).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid build version' }
if ((Get-Item -LiteralPath $exe).VersionInfo.FileVersion -ne $version) { throw 'GUI version differs from configured version' }
if ((& $cli --version) -ne $version -or $LASTEXITCODE -ne 0) { throw 'CLI version differs from configured version' }
# Bind the receipt to the artifacts present before the first gate.
$inputHashes = [ordered]@{}
$benchmark = Join-Path $BuildDir 'Release\dc_benchmark.exe'
$deliverables = @($exe,$cli)
if (Test-Path -LiteralPath $benchmark -PathType Leaf) { $deliverables += $benchmark }
foreach ($file in $deliverables) { $inputHashes[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
# Independently execute every release gate before writing a receipt. This script
# may be run directly; it must never mark an untested build as accepted.
$checkManifest = Join-Path $PSScriptRoot 'check_manifest_windows.ps1'
$smokes = @('smoke_windows.ps1','smoke_policy_windows.ps1','smoke_gui_windows.ps1')
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $checkManifest -BuildDir $BuildDir
if ($LASTEXITCODE -ne 0) { throw "Embedded manifest verification failed ($LASTEXITCODE)" }
& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Release CTest failed ($LASTEXITCODE)" }
if (-not (Test-Path -LiteralPath $testLog -PathType Leaf)) { throw "CTest did not produce its LastTest.log" }
foreach ($script in $smokes) {
  if ($script -eq 'smoke_gui_windows.ps1') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $script) -ExePath $exe
  } else {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $script) -BuildDir $BuildDir
  }
  if ($LASTEXITCODE -ne 0) { throw "Windows smoke gate $script failed ($LASTEXITCODE)" }
}
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'smoke_gui_windows.ps1') -ExePath $exe -CacheUnavailable
if ($LASTEXITCODE -ne 0) { throw 'Cache warning GUI regression failed' }
if ($inputHashes.Contains($benchmark)) {
  & $benchmark (Join-Path $project 'tests') --scan
  if ($LASTEXITCODE -ne 0) { throw 'Diagnostic scan failed' }
}
foreach ($file in $deliverables) {
  if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $inputHashes[$file]) {
    throw "Release binary changed during validation: $file"
  }
}
$logHash=(Get-FileHash -LiteralPath $testLog -Algorithm SHA256).Hash.ToLowerInvariant()
$receipt=Join-Path $BuildDir 'diskcurator-windows-validation.txt'
$body = @(
  "File Dedup Organizer $version / automated Windows validation receipt",
  ('UTC: '+[DateTime]::UtcNow.ToString('o')),
  ('OS: '+[Environment]::OSVersion.VersionString),
  'Build: Visual Studio 2022 Win64 Release',
  'Gates: version metadata, manifest, full CTest, CLI, retention/invalid arguments, Win32 GUI, cache-failure GUI',
  'Each gate was executed by this validation script before producing this receipt.',
  ('CTest LastTest.log SHA256: '+$logHash),
  'This receipt is not a digital signature and not proof of real USB/power-loss safety.'
)
foreach ($file in $deliverables) { $body += ((Split-Path -Leaf $file)+' SHA256: '+$inputHashes[$file]) }
[IO.File]::WriteAllText($receipt,($body -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
Write-Host "Recorded Windows auto-validation receipt: $receipt"
