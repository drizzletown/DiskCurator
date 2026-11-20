param([string]$BuildDir = 'build-win')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$homeDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $homeDir $BuildDir }
$exe = Join-Path $BuildDir 'Release\dc_cli.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Missing CLI exe: $exe" }
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('dc_policy_smoke_' + [Guid]::NewGuid().ToString('N'))
try {
  New-Item -ItemType Directory -Path (Join-Path $fixture 'copies') -Force | Out-Null
  foreach ($n in @('a.bin', 'b.bin', 'c.bin', 'd.bin')) {
    [IO.File]::WriteAllText((Join-Path $fixture ("copies\" + $n)), 'hashfixture', [Text.Encoding]::ASCII)
  }
  $out = & $exe hashes $fixture 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { throw "Full hash scan failed: $out" }
  if ((($out -split "`n") | Where-Object { $_ -match '^[0-9a-f]{64}\s' }).Count -ne 4) {
    throw "Full SHA256 index should contain four hashes: $out"
  }
  $p3 = & $exe clean $fixture --keep 3 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0 -or $p3 -notmatch '1 moves') { throw "Keep 3 failed: $p3" }
  $p1 = & $exe clean $fixture --keep 1 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0 -or $p1 -notmatch '3 moves') { throw "Keep 1 failed: $p1" }
  $risky = & $exe quarantine-selected $fixture copies/a.bin copies/b.bin copies/c.bin copies/d.bin --keep 0 --allow-zero 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0 -or $risky -notmatch '4 moves') { throw "Explicit keep 0 failed: $risky" }
  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $denied = & $exe quarantine-selected $fixture copies/a.bin copies/b.bin copies/c.bin copies/d.bin --keep 0 2>&1 | Out-String
    $deniedExit = $LASTEXITCODE
  }
  finally { $ErrorActionPreference = $previousPreference }
  if ($deniedExit -eq 0) { throw 'Keep 0 without explicit consent must be blocked' }
  # Invalid commands must be rejected before the scanner or an executor runs.
  $invalid = @(
    @{ args=@('archive',$fixture,'typo','--apply','--yes') },
    @{ args=@('clean',$fixture,'--keep','1oops','--apply','--yes') },
    @{ args=@('quarantine-selected',$fixture,'copies/a.bin','--kep','2','--apply','--yes') },
    @{ args=@('clean',$fixture,'--keep','2','--keep','0','--allow-zero') },
    @{ args=@('scan',$fixture,'--apply','--yes') },
    @{ args=@('clean',$fixture,'--apply') },
    @{ args=@('rename',$fixture,'copies','--apply','--yes') },
    @{ args=@('similar',$fixture,'65') },
    @{ args=@('unknown',$fixture) }
  )
  $ErrorActionPreference = 'Continue'
  try {
    foreach ($case in $invalid) {
      $argv = $case.args
      $failure = & $exe @argv 2>&1 | Out-String
      if ($LASTEXITCODE -eq 0 -or $failure -match 'Files=') { throw "Invalid invocation reached scan or execution: $argv" }
    }
  } finally { $ErrorActionPreference = $previousPreference }
  if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Invalid options created a transaction' }
  $fast = & $exe clean $fixture --keep 2 --trust-scan-hash 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0 -or $fast -notmatch '2 moves') { throw "Fast mode dry run failed: $fast" }
  if (-not (Test-Path -LiteralPath (Join-Path $fixture 'copies\a.bin'))) { throw 'A dry run wrote user data' }
  Write-Host 'PASS Windows policy smoke: SHA256 all, keep 1/3/0-consent, fast-mode dry run'
}
finally {
  if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
