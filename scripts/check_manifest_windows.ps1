param([string]$BuildDir = 'build-win')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$productName = '磁盘文件整理助手'
$exe = Join-Path $BuildDir "Release\$productName.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
  throw "Win32 GUI executable not found: $exe"
}

$fileInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
if ($fileInfo.ProductName -cne $productName -or $fileInfo.FileDescription -cne $productName -or
    $fileInfo.OriginalFilename -cne [IO.Path]::GetFileName($exe)) {
  throw "Unexpected Windows product name, file description or original filename in $exe."
}

# A manifest is explicitly compiled as RT_MANIFEST/#1 by src/app.rc.
# Find mt.exe from PATH or the installed Windows 10/11 SDK and extract the
# actual manifest from the built EXE (not just the source XML).
$mtCommand = Get-Command mt.exe -ErrorAction SilentlyContinue
$mtPath = if ($mtCommand) { $mtCommand.Source } else { $null }
if (-not $mtPath) {
  $kitsBase = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
  if (Test-Path -LiteralPath $kitsBase) {
    $candidates = @(Get-ChildItem -LiteralPath $kitsBase -Directory |
      Sort-Object Name -Descending |
      ForEach-Object { Join-Path $_.FullName 'x64\mt.exe' } |
      Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($candidates.Count -gt 0) { $mtPath = $candidates[0] }
  }
}
if (-not $mtPath) { throw 'Windows SDK mt.exe not found; cannot verify embedded GUI manifest.' }

$tempManifest = Join-Path ([IO.Path]::GetTempPath()) ("DiskCurator-manifest-$([guid]::NewGuid().ToString('N')).xml")
try {
  & $mtPath "-inputresource:$exe;#1" "-out:$tempManifest" -nologo
  if ($LASTEXITCODE -ne 0) { throw "Failed to extract RT_MANIFEST #1 from $exe (mt.exe exit $LASTEXITCODE)." }
  [xml]$xml = Get-Content -LiteralPath $tempManifest -Raw
  if ($null -eq $xml.SelectSingleNode("/*[local-name()='assembly']")) {
    throw 'Unexpected root element in the embedded application manifest.'
  }
  $version = [IO.File]::ReadAllText((Join-Path $BuildDir 'diskcurator-version.txt')).Trim()
  $identity = $xml.SelectSingleNode("/*[local-name()='assembly']/*[local-name()='assemblyIdentity']")
  if ($null -eq $identity -or $identity.GetAttribute('version') -ne ($version+'.0')) { throw 'Manifest version mismatch' }
  $description = $xml.SelectSingleNode("/*[local-name()='assembly']/*[local-name()='description']")
  if ($null -eq $description -or $description.InnerText -cne 'DiskCurator') { throw 'Manifest product description mismatch' }
  $controls = $xml.SelectSingleNode("//*[local-name()='dependentAssembly']/*[local-name()='assemblyIdentity' and @name='Microsoft.Windows.Common-Controls']")
  if ($null -eq $controls -or $controls.GetAttribute('version') -ne '6.0.0.0') { throw 'Native common controls v6 manifest dependency missing' }
  foreach ($setting in @('longPathAware', 'dpiAware')) {
    $node = $xml.SelectSingleNode("//*[local-name()='$setting']")
    if ($null -eq $node -or $node.InnerText.Trim().ToLowerInvariant() -ne 'true') {
      throw "Embedded manifest is missing required $setting=true setting."
    }
  }
  Write-Host "$productName.exe contains the correct product name, Common Controls v6, longPathAware=true and dpiAware=true."
}
finally {
  if (Test-Path -LiteralPath $tempManifest) { Remove-Item -LiteralPath $tempManifest -Force }
}
