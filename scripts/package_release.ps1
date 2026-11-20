param([string]$BuildDir = 'build-win', [string]$OutputDir = 'dist')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $project $BuildDir }
if (-not [IO.Path]::IsPathRooted($OutputDir)) { $OutputDir = Join-Path $project $OutputDir }
$version = [IO.File]::ReadAllText((Join-Path $BuildDir 'diskcurator-version.txt')).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version' }
$name = "DiskOrganizer-v$version"
$receipt = Join-Path $BuildDir 'diskcurator-windows-validation.txt'
$receiptLines = [IO.File]::ReadAllLines($receipt)
if ($receiptLines -notcontains "DiskOrganizer $version / automated Windows validation receipt") { throw 'Validation receipt version mismatch' }
function Get-Digest([string]$file) { (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() }
$log = Join-Path $BuildDir 'Testing\Temporary\LastTest.log'
if ($receiptLines -notcontains ('CTest LastTest.log SHA256: '+(Get-Digest $log))) { throw 'CTest evidence changed after validation' }

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDir).Path
# Each invocation owns its temporary directory and never empties a pre-existing user folder.
$temp = Join-Path $outputRoot ('.diskcurator-package-'+[Guid]::NewGuid().ToString('N'))
$stage = Join-Path $temp $name
$zip = Join-Path $outputRoot "$name-Win64.zip"
New-Item -ItemType Directory -Path $temp | Out-Null
try {
  New-Item -ItemType Directory -Path $stage | Out-Null
  $binaries = @('磁盘文件整理助手.exe','dc_cli.exe')
  $benchmark = 'dc_benchmark.exe'
  if (Test-Path -LiteralPath (Join-Path $BuildDir "Release\$benchmark") -PathType Leaf) { $binaries += $benchmark }
  foreach ($binary in $binaries) {
    $copy = Join-Path $stage $binary
    Copy-Item -LiteralPath (Join-Path $BuildDir "Release\$binary") -Destination $copy
    if ($receiptLines -notcontains ($binary+' SHA256: '+(Get-Digest $copy))) { throw "Untested binary: $binary" }
  }
  foreach ($doc in @('START-HERE.txt','SECURITY.md','RELEASE-ACCEPTANCE.md')) {
    Copy-Item -LiteralPath (Join-Path $project $doc) -Destination $stage
  }
  Copy-Item -LiteralPath (Join-Path $project "RELEASE-v$version.md") -Destination (Join-Path $stage 'RELEASE-NOTES.md')
  [IO.File]::WriteAllLines((Join-Path $stage 'WINDOWS-AUTOMATED-VALIDATION.txt'),$receiptLines)
  $hashes = @(Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object { (Get-Digest $_.FullName)+'  '+$_.Name })
  [IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'),[string[]]$hashes)
  $pendingZip = Join-Path $temp 'archive.zip'
  Compress-Archive -LiteralPath $stage -DestinationPath $pendingZip -CompressionLevel Optimal
  $hash = Get-Digest $pendingZip
  $pendingChecksum = Join-Path $temp 'checksum.txt'
  [IO.File]::WriteAllText($pendingChecksum,($hash+'  '+(Split-Path -Leaf $zip)+[Environment]::NewLine))
  foreach ($pair in @(@($pendingZip,$zip),@($pendingChecksum,"$zip.sha256"))) {
    if (Test-Path -LiteralPath $pair[1]) { [IO.File]::Replace($pair[0],$pair[1],($pair[0]+'.previous')) }
    else { [IO.File]::Move($pair[0],$pair[1]) }
  }
  Write-Host "Portable Windows archive: $zip"
  Write-Host "SHA256: $hash"
} finally {
  $resolved = (Resolve-Path -LiteralPath $temp).Path
  if ([IO.Path]::GetDirectoryName($resolved) -ne $outputRoot) { throw 'Unexpected staging location' }
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
