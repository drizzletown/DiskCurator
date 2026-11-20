$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$required = @(
    'CMakeLists.txt',
    'RELEASE-ACCEPTANCE.md',
    'README.md',
    'REQUIREMENTS.md',
    'START-HERE.txt',
    'SECURITY.md',
    'QUALITY_GATES.md',
    'src/core.hpp',
    'src/core.cpp',
    'src/hash_index.cpp',
    'src/scan_progress.hpp',
    'src/hash_actions.cpp',
    'src/scoped_analysis.cpp',
    'src/observations.hpp',
    'src/observations.cpp',
    'src/cli.cpp',
    'src/win_main.cpp',
    'src/photo_windows.cpp',
    'src/photo_portable.cpp',
    'src/app.manifest',
    'src/app.rc',
    'scripts/mark_windows_validation.ps1',
    'scripts/build_windows.bat',
    'scripts/package_release.ps1',
    'scripts/check_source_layout.ps1',
    'scripts/check_manifest_windows.ps1',
    'scripts/smoke_windows.ps1',
    'scripts/smoke_policy_windows.ps1',
    'scripts/smoke_gui_windows.ps1',
    '.github/workflows/windows-build.yml',
    'TESTING.md',
    'docs/design-v2.6/06-acceptance.md',
    'tests/enable_assertions.hpp',
    'tests/test_core.cpp',
    'tests/test_recovery.cpp',
    'tests/test_stress.cpp',
    'tests/test_merge.cpp',
    'tests/test_recommend.cpp',
    'tests/test_workflow.cpp',
    'tests/test_structure.cpp',
    'tests/test_foundation.cpp',
    'tests/test_hash_policy.cpp',
    'tests/test_observations.cpp',
    'tests/test_scan_scope.cpp',
    'tests/test_assert_enabled.cpp',
    'tests/test_scan_progress.cpp',
    'tests/test_hash_actions.cpp',
    'tests/test_scan_cancellation.cpp',
    'tests/test_scoped_analysis.cpp',
    'tools/perf_benchmark.cpp',
    'docs/design-v2.6/README.md'
)
foreach ($relative in $required) {
    $full = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw "Source distribution is incomplete; missing: $relative"
    }
}
# Windows PowerShell 5.1 reads .ps1 without BOM using the active ANSI code page
# (often GBK/CP936), so a UTF-8-only script containing Chinese fixture names
# can be corrupted at parse time. Reject such source distributions early.
$psScripts = Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -Filter '*.ps1' -File
foreach ($script in $psScripts) {
    $bytes = [IO.File]::ReadAllBytes($script.FullName)
    $hasNonAscii = $false
    foreach ($b in $bytes) { if ($b -ge 128) { $hasNonAscii = $true; break } }
    if (-not $hasNonAscii) { continue }
    $hasUtf8Bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    if ($bytes.Length -ge 6 -and $bytes[3] -eq 0xEF -and $bytes[4] -eq 0xBB -and $bytes[5] -eq 0xBF) {
        throw "Duplicate UTF-8 BOM in $($script.Name)"
    }
    if (-not $hasUtf8Bom) {
        throw "Windows PowerShell 5.1 requires UTF-8 BOM for non-ASCII script: $($script.Name)"
    }
    $strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
    try { [void]$strictUtf8.GetString($bytes, 3, $bytes.Length - 3) }
    catch { throw "Invalid UTF-8 in $($script.Name): $_" }
}
# Preflight the ACTUAL PowerShell 5.1 grammar before spending time building C++.
# This catches accidental `$variable:` string interpolation and malformed
# quotes/parentheses in smoke scripts before the release test gate runs.
foreach ($script in $psScripts) {
    $parseTokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $script.FullName, [ref]$parseTokens, [ref]$parseErrors
    )
    if ($null -ne $parseErrors -and $parseErrors.Count -gt 0) {
        $details = @($parseErrors | ForEach-Object {
            "line $($_.Extent.StartLineNumber), column $($_.Extent.StartColumnNumber): $($_.Message)"
        }) -join ' | '
        throw "PowerShell syntax error in $($script.Name): $details"
    }
}
Write-Host "PowerShell parser check passed: $($psScripts.Count) scripts."
Write-Host "PowerShell encoding check passed: non-ASCII scripts have valid UTF-8 BOM."
Write-Host "File Dedup Organizer source distribution complete: $($required.Count) required files."
