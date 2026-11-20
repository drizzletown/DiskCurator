param([string]$BuildDir = 'build-win')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$base = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $base $BuildDir }
$cli = Join-Path $BuildDir 'Release\dc_cli.exe'
if (-not (Test-Path -LiteralPath $cli)) { throw "Missing CLI executable: $cli" }
$root = Join-Path ([IO.Path]::GetTempPath()) ('DiskCurator-smoke-' + [guid]::NewGuid().ToString('N'))
$A = Join-Path $root '资料甲'
$B = Join-Path $root '资料乙'
function RunDC([string[]] $Arguments) {
  $output = & $cli @Arguments 2>&1 | Out-String
  if ($LASTEXITCODE -ne 0) { throw "dc_cli exit $LASTEXITCODE; command=$($Arguments -join ' '); output=$output" }
  return $output
}
try {
  New-Item -ItemType Directory -Path (Join-Path $A '空目录') -Force | Out-Null
  New-Item -ItemType Directory -Path (Join-Path $B '空目录') -Force | Out-Null
  [IO.File]::WriteAllText((Join-Path $A '相同.txt'), 'same contents', [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $B '相同.txt'), 'same contents', [Text.UTF8Encoding]::new($false))
  # Exclusive access proves inventory can count the file without opening its contents.
  $inventoryCache = Join-Path $root 'inventory-appdata'
  $savedLocalAppData = $env:LOCALAPPDATA
  $locked = [IO.File]::Open((Join-Path $A '相同.txt'), 'Open', 'Read', 'None')
  try {
    $env:LOCALAPPDATA = $inventoryCache
    $inventory = & $cli inventory $root | Out-String
    if ($LASTEXITCODE -ne 0 -or $inventory.Trim() -cne 'Status=complete Files=2 Folders=5 Bytes=26 ScanErrors=0 DirectoryErrors=0 ReparseSkipped=0 OtherErrors=0') {
      throw "Unexpected metadata inventory: $inventory"
    }
    # An NTFS junction must be counted as skipped rather than followed or named in the summary.
    if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($root)).DriveFormat -eq 'NTFS') {
      $aliasPath = Join-Path $root 'private-folder-alias'
      New-Item -ItemType Junction -Path $aliasPath -Target $A | Out-Null
      try {
        $partial = & $cli inventory $root | Out-String
        if ($LASTEXITCODE -ne 4 -or $partial.Trim() -cne 'Status=partial Files=2 Folders=5 Bytes=26 ScanErrors=1 DirectoryErrors=0 ReparseSkipped=1 OtherErrors=0') {
          throw "Unexpected partial metadata inventory: $partial"
        }
      } finally {
        [IO.Directory]::Delete($aliasPath)
      }
    }
    if ((Test-Path -LiteralPath $inventoryCache) -or (Test-Path -LiteralPath (Join-Path $root '.DiskCurator-System'))) {
      throw 'Metadata inventory wrote an index or transaction workspace'
    }
  } finally {
    $env:LOCALAPPDATA = $savedLocalAppData
    $locked.Dispose()
  }
  $duplicated = RunDC @('duplicates',$root)
  if ($duplicated -notmatch 'DuplicateGroups=1') { throw "Unexpected duplicates: $duplicated" }
  $folders = RunDC @('folders',$root)
  if ($folders -notmatch 'GROUP') { throw "Missing duplicate folders: $folders" }
  $pair = RunDC @('pair',$root,'资料甲','资料乙')
  if ($pair -notmatch 'exact_tree=1' -or $pair -notmatch 'shared=1') { throw "Manual pair report failed: $pair" }
  $compare = RunDC @('compare',$root,'资料甲','资料乙')
  if ($compare -notmatch 'A_subset_B=1 B_subset_A=1') { throw "Unexpected directory comparison: $compare" }
  $rename = RunDC @('rename',$root,'资料甲','P_','--apply','--yes')
  if (-not (Test-Path -LiteralPath (Join-Path $A 'P_相同.txt'))) { throw "Rename did not apply: $rename" }
  # The rc3 Windows build wrote CRLF in text mode, but verified in binary mode.
  # Test the actual file bytes so the bug cannot silently return.
  $ownerPath = Join-Path $root '.DiskCurator-System\owner.txt'
  $actualOwner = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ownerPath))
  $expectedOwner = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("DiskCurator Native workspace v1`n"))
  if ($actualOwner -ne $expectedOwner) { throw 'Workspace owner marker is not canonical LF bytes' }
  [void](RunDC @('undo',$root))
  if (-not (Test-Path -LiteralPath (Join-Path $A '相同.txt'))) { throw 'Rename undo failed after owner marker verification' }
  # Two identical copies cannot be quarantined, even when the user asks.
  $two = RunDC @('clean',$root,'--apply','--yes')
  if ($two -notmatch '0 moves') { throw "Two-copy safety rule failed: $two" }
  $C = Join-Path $root '第三次备份'
  New-Item -ItemType Directory -Path $C -Force | Out-Null
  [IO.File]::WriteAllText((Join-Path $C '相同.txt'), 'same contents', [Text.UTF8Encoding]::new($false))
  $manualPlan = RunDC @('quarantine-selected',$root,'第三次备份/相同.txt')
  if ($manualPlan -notmatch '1 moves') { throw "Manual selected quarantine plan failed: $manualPlan" }
  $manualApplied = RunDC @('quarantine-selected',$root,'第三次备份/相同.txt','--apply','--yes')
  if ($manualApplied -notmatch 'Applied 1 changes' ) { throw "Manual quarantine uncertain: $manualApplied" }
  $remaining = @(Get-ChildItem -LiteralPath $A,$B,$C -Filter '相同.txt' -File)
  if ($remaining.Count -ne 2) { throw "Two-copy policy violated; got $($remaining.Count)" }
  [void](RunDC @('undo',$root))
  if (-not (Test-Path -LiteralPath (Join-Path $C '相同.txt'))) { throw 'Quarantine undo failed' }
  # Merge source into target, retaining both versions of conflicting names.
  $old = Join-Path $root '旧照片'
  $new = Join-Path $root '新照片'
  New-Item -ItemType Directory -Path $old,$new -Force | Out-Null
  [IO.File]::WriteAllText((Join-Path $old '共同.txt'), 'SAME', [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $new '共同.txt'), 'SAME', [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $old '仅旧有.txt'), 'OLD-ONLY', [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $old '同名.txt'), 'OLD', [Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $new '同名.txt'), 'NEW', [Text.UTF8Encoding]::new($false))
  $m = RunDC @('merge',$root,'旧照片','新照片','--apply','--yes')
  if ($m -notmatch 'Merge complete') { throw "Merge did not complete: $m" }
  if (-not (Test-Path -LiteralPath (Join-Path $new '仅旧有.txt'))) { throw 'Merge omitted old-only file' }
  if (-not (Test-Path -LiteralPath (Join-Path $new '同名 (from 旧照片).txt'))) { throw 'Merge failed to preserve conflicting version' }
  if ([IO.File]::ReadAllText((Join-Path $new '同名.txt')) -ne 'NEW') { throw 'Merge overwrote destination' }
  $repeat = RunDC @('merge',$root,'旧照片','新照片')
  if ($repeat -notmatch 'Merge: 0 new copies') { throw "Merge is not idempotent: $repeat" }
  # Full workflow: recommend a new directory, verify its contents, then
  # quarantine only those old files with TWO independent keepers outside.
  $workflow = Join-Path $root '结构迁移验收'
  $oldwf = Join-Path $workflow '旧备份'
  $newwf = Join-Path $workflow '新备份'
  New-Item -ItemType Directory -Path $oldwf,$newwf -Force | Out-Null
  [IO.File]::WriteAllText((Join-Path $oldwf 'shared.bin'), 'SHARED-CONTENTS')
  [IO.File]::WriteAllText((Join-Path $newwf 'shared.bin'), 'SHARED-CONTENTS')
  [IO.File]::WriteAllText((Join-Path $oldwf 'unique.bin'), 'OLD-ONLY-CONTENTS')
  $structure = RunDC @('structure',$workflow,'整理结果','--apply','--yes')
  if ($structure -notmatch 'Structure built and verified') { throw "Structure migration failed: $structure" }
  $retirement = RunDC @('retire',$workflow,'旧备份')
  if ($retirement -notmatch 'eligible=1 blocked=1') { throw "Two-external-keeper cleanup preview incorrect: $retirement" }
  $retireApply = RunDC @('retire',$workflow,'旧备份','--apply','--yes')
  if (-not (Test-Path -LiteralPath (Join-Path $oldwf 'unique.bin'))) { throw "Retirement removed under-backed file: $retireApply" }
  if (Test-Path -LiteralPath (Join-Path $oldwf 'shared.bin')) { throw "Eligible old duplicate was not quarantined: $retireApply" }
  if (-not (Test-Path -LiteralPath (Join-Path $newwf 'shared.bin'))) { throw 'One keeper disappeared' }
  $outFiles = Get-ChildItem -LiteralPath (Join-Path $workflow '整理结果') -File -Recurse
  $newCopy = @($outFiles | Where-Object { [IO.File]::ReadAllText($_.FullName) -eq 'SHARED-CONTENTS' })
  if ($newCopy.Count -ne 1) { throw 'Missing independently copied new-structure keeper' }
  [void](RunDC @('undo',$workflow))
  if (-not (Test-Path -LiteralPath (Join-Path $oldwf 'shared.bin'))) { throw 'Scoped quarantine undo failed' }
  # Generate two minimal 24-bit BMP files. This invokes the actual Windows WIC
  # decoder/bitmap scaler (not the Linux portable test decoder).
  function WriteBmp([string] $p) {
    $stream = [IO.File]::Create($p)
    try {
      $b = [IO.BinaryWriter]::new($stream)
      $b.Write([Text.Encoding]::ASCII.GetBytes('BM'))
      $b.Write([int]278)                       # 54-byte header + 8*28 bytes pixels
      $b.Write([int16]0); $b.Write([int16]0)
      $b.Write([int]54)
      $b.Write([int]40); $b.Write([int]9); $b.Write([int]8)
      $b.Write([int16]1); $b.Write([int16]24)
      $b.Write([int]0); $b.Write([int]224)
      $b.Write([int]0); $b.Write([int]0); $b.Write([int]0); $b.Write([int]0)
      for ($y=0; $y -lt 8; ++$y) {
        for ($x=0; $x -lt 9; ++$x) {
          $gray = [byte]($x * 20)
          $b.Write($gray); $b.Write($gray); $b.Write($gray)
        }
        $b.Write([byte]0)                     # 27-byte row -> 28-byte alignment
      }
      $b.Flush()
    } finally { $stream.Dispose() }
  }
  WriteBmp (Join-Path $A '图片1.bmp')
  WriteBmp (Join-Path $B '图片2.bmp')
  $photos = RunDC @('similar',$root,'0')
  if ($photos -notmatch 'Decoded=2 SimilarPairs=1 ImageErrors=0') { throw "Windows WIC photo decoding failed: $photos" }
  $archive = RunDC @('archive',$root,'date','--apply','--yes')
  if ($archive -notmatch '2 moves') { throw "Photo date archive unexpectedly skipped images: $archive" }
  [void](RunDC @('undo',$root))
  if (-not (Test-Path -LiteralPath (Join-Path $A '图片1.bmp'))) { throw 'Photo archive undo failed' }
  Write-Host 'PASS Windows smoke: Chinese paths, exact files/folders, rename/undo, two-copy quarantine/undo, additive merge, structure build, scoped two-external-keeper retirement, WIC dHash, date archive/undo' 
}
finally { if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force } }
