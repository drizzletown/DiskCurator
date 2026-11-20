param([string]$ExePath = 'build-win\Release\DiskCurator.exe', [switch]$CacheUnavailable, [string]$ScreenshotDir)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not [IO.Path]::IsPathRooted($ExePath)) { $ExePath = Join-Path $project $ExePath }
if (-not (Test-Path -LiteralPath $ExePath)) { throw "GUI binary missing: $ExePath" }

# This harness exercises native control messages across processes. WM_SETTEXT
# is system-marshalled; pointers to private ListView structs are never sent.
Add-Type -ReferencedAssemblies 'System.Drawing.dll' -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class DiskCuratorGuiSmoke {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SetText(IntPtr window, uint message, IntPtr wparam, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr GetText(IntPtr window, uint message, IntPtr wparam, [Out] StringBuilder text);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    // The harness launches a hidden top-level window. Check the control's
    // own WS_VISIBLE flag so the owner's test launch mode does not mask it.
    [DllImport("user32.dll",EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr window, int index);
    public static bool HasVisibleStyle(IntPtr window) { return (GetWindowLong(window,-16) & 0x10000000) != 0; }
    [StructLayout(LayoutKind.Sequential)]
    struct Rect { public int left,top,right,bottom; }
    [StructLayout(LayoutKind.Sequential)]
    struct Point { public int x,y; }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    public static bool FitsClient(IntPtr parent, IntPtr control) {
        Rect client, child;
        Point offset = new Point();
        if (!GetClientRect(parent,out client) || !ClientToScreen(parent,ref offset) || !GetWindowRect(control,out child)) return false;
        return child.right>child.left && child.bottom>child.top && child.left>=offset.x && child.top>=offset.y &&
               child.right<=offset.x+client.right && child.bottom<=offset.y+client.bottom;
    }
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    public static string DialogBody(IntPtr parent) {
        var result = new StringBuilder();
        EnumChildWindows(parent,delegate(IntPtr child,IntPtr unused) {
            var className = new StringBuilder(80); GetClassName(child,className,80);
            if (className.ToString() == "Static") {
                var buffer = new StringBuilder(8192);
                GetText(child,0x000D,new IntPtr(buffer.Capacity),buffer);
                result.AppendLine(buffer.ToString());
            }
            return true;
        },IntPtr.Zero);
        return result.ToString();
    }
    [DllImport("user32.dll", SetLastError=true)] static extern bool PrintWindow(IntPtr window, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr window, IntPtr region, IntPtr clip, uint flags);
    [DllImport("dwmapi.dll")] static extern int DwmFlush();
    public static void Capture(IntPtr window, string destination) {
        Rect bounds;
        if (!GetWindowRect(window, out bounds)) throw new InvalidOperationException("Cannot read window dimensions.");
        // Composition ignores hidden windows. Briefly render just this app
        // without taking focus, then capture its own pixels and hide it again.
        ShowWindow(window,4); // SW_SHOWNOACTIVATE
        try {
            RedrawWindow(window,IntPtr.Zero,IntPtr.Zero,0x585); // invalidate, erase, children, update now, frame
            // Let native controls finish their reveal and progress animations before capturing.
            System.Threading.Thread.Sleep(800);
            DwmFlush();
            using (var bitmap = new System.Drawing.Bitmap(bounds.right-bounds.left,bounds.bottom-bounds.top))
            using (var graphics = System.Drawing.Graphics.FromImage(bitmap)) {
                var dc = graphics.GetHdc();
                try {
                    if (!PrintWindow(window,dc,2)) throw new InvalidOperationException("PrintWindow failed.");
                } finally { graphics.ReleaseHdc(dc); }
                // Ignore the native frame and detect black or unpainted clients.
                var colors = new HashSet<int>();
                for (int y=60;y<bitmap.Height-12;y+=3)
                    for (int x=12;x<bitmap.Width-12;x+=3) colors.Add(bitmap.GetPixel(x,y).ToArgb());
                if (colors.Count<16) throw new InvalidOperationException("The captured client is blank.");
                bitmap.Save(destination,System.Drawing.Imaging.ImageFormat.Png);
            }
        } finally { ShowWindow(window,0); }
    }
    public static IntPtr FindOwned(uint process, string wantedClass) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr unused) {
            uint owner; GetWindowThreadProcessId(h,out owner);
            if(owner!=process) return true;
            var name=new StringBuilder(256); GetClassName(h,name,256);
            if(name.ToString()!=wantedClass) return true;
            found=h; return false;
        },IntPtr.Zero);
        return found;
    }
}
'@
function Get-ControlText([IntPtr]$control) {
    $length = [DiskCuratorGuiSmoke]::SendMessage($control, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $buffer = [Text.StringBuilder]::new($length + 1)
    [void][DiskCuratorGuiSmoke]::GetText($control, 0x000D, [IntPtr]($length + 1), $buffer)
    return $buffer.ToString()
}
function Set-ControlText([IntPtr]$control, [string]$text) {
    if ([DiskCuratorGuiSmoke]::SetText($control, 0x000C, [IntPtr]::Zero, $text) -eq [IntPtr]::Zero) { throw 'WM_SETTEXT failed' }
    if ((Get-ControlText $control) -cne $text) { throw 'Edit text readback mismatch' }
}
function Get-RowCount { return [DiskCuratorGuiSmoke]::SendMessage($script:list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() }
function Get-SelectedCount { return [DiskCuratorGuiSmoke]::SendMessage($script:list,0x1032,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() }
function Send-Action([int]$command) {
    # Post commands that may open a modal dialog; never block this harness in
    # a synchronous message while the target is waiting for user confirmation.
    [void][DiskCuratorGuiSmoke]::PostMessage($script:window,0x111,[IntPtr]$command,[IntPtr]::Zero)
}
function Set-Lens([int]$index) {
    [void][DiskCuratorGuiSmoke]::SendMessage($script:lens,0x14E,[IntPtr]$index,[IntPtr]::Zero)
    [void][DiskCuratorGuiSmoke]::SendMessage($script:window,0x111,[IntPtr]65767,$script:lens)
}
function Wait-Rows([long]$expected,[string]$phase) {
    for ($i=0; $i -lt 100; $i++) {
        if ((Get-RowCount) -eq $expected) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "GUI ${phase}: expected $expected rows, got $(Get-RowCount)"
}
function Wait-Ready([string]$phase,[int]$seconds=40) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    $readySamples = 0
    do {
        if ($script:process.HasExited) { throw "GUI exited during ${phase}" }
        if ([DiskCuratorGuiSmoke]::IsWindowEnabled($script:scanButton) -and [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window)) { $readySamples++ } else { $readySamples=0 }
        if ($readySamples -ge 3) { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "GUI ${phase} timed out: $(Get-ControlText $script:stage) / $(Get-ControlText $script:metrics)"
}
function Wait-Lens([int]$expected,[string]$phase) {
    $deadline=[DateTime]::UtcNow.AddSeconds(40)
    $readySamples=0
    do {
        if ($script:process.HasExited) { throw "GUI exited during ${phase}" }
        $notice=[DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,'#32770')
        if ($notice -ne [IntPtr]::Zero) { throw "Unexpected modal dialog during ${phase}: $(Get-ControlText $notice)" }
        $actual=[DiskCuratorGuiSmoke]::SendMessage($script:lens,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
        if ($actual -eq $expected -and [DiskCuratorGuiSmoke]::IsWindowEnabled($script:scanButton) -and [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window)) { $readySamples++ } else { $readySamples=0 }
        if ($readySamples -ge 3) { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "GUI ${phase}: expected perspective $expected, got $actual"
}
function Wait-Dialog([string]$class,[string]$phase) {
    for ($i=0;$i -lt 100;$i++) {
        if ($script:process.HasExited) { throw "GUI exited during ${phase}" }
        $window=[DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,$class)
        if ($window -ne [IntPtr]::Zero -and -not [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window)) { return $window }
        Start-Sleep -Milliseconds 50
    }
    throw "Expected dialog missing: $phase"
}
function Close-TestDialog([IntPtr]$dialog) {
    [void][DiskCuratorGuiSmoke]::PostMessage($dialog,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
    for ($i=0;$i -lt 100;$i++) {
        if ([DiskCuratorGuiSmoke]::IsWindowEnabled($script:window)) { return }
        Start-Sleep -Milliseconds 50
    }
    throw 'Main window was not enabled after cancel'
}
function Assert-Layout([int[]]$ids,[string]$phase) {
    foreach ($id in $ids) {
        $control=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id)
        if ($control -eq [IntPtr]::Zero -or -not [DiskCuratorGuiSmoke]::HasVisibleStyle($control) -or
            -not [DiskCuratorGuiSmoke]::FitsClient($script:window,$control)) { throw "GUI ${phase}: control $id missing, clipped or hidden" }
    }
}

function Save-TestWindow([string]$filename) {
    if ([string]::IsNullOrWhiteSpace($ScreenshotDir)) { return }
    if (-not (Test-Path -LiteralPath $ScreenshotDir -PathType Container)) { New-Item -ItemType Directory -Path $ScreenshotDir -Force | Out-Null }
    [DiskCuratorGuiSmoke]::Capture($script:window,(Join-Path (Resolve-Path -LiteralPath $ScreenshotDir).Path $filename))
}

$script:process=$null
$script:window=[IntPtr]::Zero
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('DiskCurator-结果回归-'+[Guid]::NewGuid().ToString('N'))
$cacheLocation=$fixture+'-appdata'
$previousLocalAppData=$env:LOCALAPPDATA
# SetWindowPos, rect assertions and PrintWindow must all use physical pixels.
$previousDpiContext=[DiskCuratorGuiSmoke]::SetThreadDpiAwarenessContext([IntPtr](-3))
try {
    if ($CacheUnavailable) { [IO.File]::WriteAllText($cacheLocation,'blocked') }
    else { New-Item -ItemType Directory -Path $cacheLocation | Out-Null }
    $env:LOCALAPPDATA=$cacheLocation
    $script:process=Start-Process -FilePath $ExePath -PassThru -WindowStyle Hidden -WorkingDirectory $project
    $env:LOCALAPPDATA=$previousLocalAppData
    if (-not $script:process.WaitForInputIdle(15000)) { throw 'GUI startup did not become idle' }
    for ($i=0; $i -lt 100; $i++) {
        $script:window=[DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,'DiskCuratorNativeWindow')
        if ($script:window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    }
    if ($script:window -eq [IntPtr]::Zero) { throw 'Main GUI window missing' }
    foreach ($id in @(112,123,200,201,202,203,204,205,206)) {
        if ([DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id) -ne [IntPtr]::Zero) { throw "Removed sidebar/control still exists: $id" }
    }
    # Exercise the compact layout without showing the test window or moving mouse focus.
    if (-not [DiskCuratorGuiSmoke]::SetWindowPos($script:window,[IntPtr]::Zero,0,0,820,600,0x16)) { throw 'Cannot size the test window' }
    Assert-Layout @(253,254,255,256) 'welcome at 820x600'
    Save-TestWindow '01-welcome.png'
    $scope=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,230)
    $rootEdit=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,100)
    $script:scanButton=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,102)
    $script:list=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,114)
    $search=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,115)
    $script:lens=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,231)
    $bar=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,243)
    $script:stage=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,244)
    $script:metrics=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,247)
    if ([DiskCuratorGuiSmoke]::SendMessage($scope,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 0) { throw 'Selected-drive mode must be default' }
    if ([DiskCuratorGuiSmoke]::IsWindowEnabled($rootEdit)) { throw 'Unselected drive scope must not enable root edit' }
    # Follow both visible entry points. Cancel both pickers; only the synthetic path below is submitted.
    $driveEntry=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,256)
    [void][DiskCuratorGuiSmoke]::PostMessage($driveEntry,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    $driveDialog=Wait-Dialog 'DCFormNative' 'drive selection'
    $driveCount=0
    while ($true) {
        $box=[DiskCuratorGuiSmoke]::GetDlgItem($driveDialog,2000+$driveCount)
        if ($box -eq [IntPtr]::Zero) { break }
        if ([DiskCuratorGuiSmoke]::SendMessage($box,0x00F0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0) { throw 'Drive picker preselected a real drive' }
        $driveCount++
    }
    if ($driveCount -eq 0) { throw 'Drive picker did not expose any selectable drives' }
    Close-TestDialog $driveDialog
    $folderEntry=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,255)
    [void][DiskCuratorGuiSmoke]::PostMessage($folderEntry,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    $folderDialog=Wait-Dialog '#32770' 'folder selection'
    Close-TestDialog $folderDialog
    if ([DiskCuratorGuiSmoke]::SendMessage($scope,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 1) {
        [void][DiskCuratorGuiSmoke]::SendMessage($scope,0x14E,[IntPtr]1,[IntPtr]::Zero)
        [void][DiskCuratorGuiSmoke]::SendMessage($script:window,0x111,[IntPtr]65766,$scope)
    }
    $folderA=Join-Path $fixture '资料';$folderB=Join-Path $fixture '备份'
    New-Item -ItemType Directory -Path $folderA,$folderB -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $folderA '甲.txt'),'same-content',[Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $folderB '乙.txt'),'same-content',[Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $fixture 'unique.bin'),'different',[Text.Encoding]::UTF8)
    Set-ControlText $rootEdit $fixture
    [void][DiskCuratorGuiSmoke]::SendMessage($script:scanButton,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Ready 'Unicode content scan'
    Wait-Rows 3 'file perspective after scan'
    if ([DiskCuratorGuiSmoke]::SendMessage($bar,0x408,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 1000) { throw 'Completed scan progress is not 100%' }
    if ([string]::IsNullOrWhiteSpace((Get-ControlText $script:metrics))) { throw 'Scan byte/time metrics are empty' }
    Assert-Layout @(100,101,102,114,115,230,231,240,241,242,245,252,257) 'results at 820x600'
    Save-TestWindow '02-results.png'
    $clear=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,252)
    if ([DiskCuratorGuiSmoke]::IsWindowEnabled($clear)) { throw 'Empty filter enabled its clear button' }
    Set-ControlText $search '本次扫描同哈希候选 2'
    Wait-Rows 2 'SHA candidate copy filter'
    if (-not [DiskCuratorGuiSmoke]::IsWindowEnabled($clear)) { throw 'Nonempty filter disabled its clear button' }
    [void][DiskCuratorGuiSmoke]::SendMessage($clear,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Rows 3 'one-click filter reset'
    if ((Get-ControlText $search) -ne '') { throw 'Clear filter did not clear search text' }
    Set-ControlText $search '甲.txt';Wait-Rows 1 'keyboard clear fixture'
    [void][DiskCuratorGuiSmoke]::PostMessage($search,0x0100,[IntPtr]27,[IntPtr]::Zero)
    Wait-Rows 3 'Escape restores unfiltered results'
    if ((Get-ControlText $search) -ne '') { throw 'Escape did not clear the filter' }
    # Row position is not an identity: changing a filter must not silently
    # transfer a pending selection from one file to another.
    Set-ControlText $search '甲.txt';Wait-Rows 1 'first file filter'
    Send-Action 5107
    for ($i=0;$i -lt 40 -and (Get-SelectedCount) -ne 1;$i++) { Start-Sleep -Milliseconds 25 }
    if ((Get-SelectedCount) -ne 1) { throw 'Select visible results failed' }
    Set-ControlText $search '乙.txt';Wait-Rows 1 'second file filter'
    if ((Get-SelectedCount) -ne 0) { throw 'Filter transferred selection to a different file' }
    Set-ControlText $search '';Send-Action 5108
    Set-Lens 1;Wait-Rows 3 'directory perspective'
    $duplicates=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,257)
    [void][DiskCuratorGuiSmoke]::SendMessage($duplicates,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Lens 2 'duplicate files shortcut'
    Wait-Rows 2 'duplicate candidate perspective' 
    Set-Lens 0;Wait-Rows 3 'file perspective reuse'
    if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Observation created a transaction workspace' }
    # Exact verification is an explicit action; its output replaces candidate
    # wording only after the comparison task has completed successfully.
    Send-Action 5103
    Set-ControlText $search '当前根精确副本 2'
    Wait-Rows 2 'explicit byte verification'
    Wait-Ready 'explicit byte verification'
    Set-ControlText $search ''
    Set-ControlText $search '甲.txt';Wait-Rows 1 'select file for direct rename'
    Send-Action 5107
    Send-Action 105
    $rename=Wait-Dialog 'DCFormNative' 'single file rename'
    $newName=[DiskCuratorGuiSmoke]::GetDlgItem($rename,2000)
    if ((Get-ControlText $newName) -cne '甲.txt' -or [DiskCuratorGuiSmoke]::GetDlgItem($rename,2001) -ne [IntPtr]::Zero) { throw 'Single-file rename should open one prefilled name field' }
    Set-ControlText $newName '已更名.txt'
    [void][DiskCuratorGuiSmoke]::PostMessage($rename,0x0111,[IntPtr]1,[IntPtr]::Zero)
    Wait-Lens 9 'single-file rename preview'
    Set-ControlText $search '已更名.txt';Wait-Rows 1 'one mapped rename destination'
    Send-Action 108
    $renameConfirm=Wait-Dialog '#32770' 'review single file rename'
    $body=[DiskCuratorGuiSmoke]::DialogBody($renameConfirm)
    if (-not ($body.Contains($fixture) -and $body.Contains('甲.txt') -and $body.Contains('已更名.txt'))) { throw "Rename confirmation missing expected root or filenames. Fixture=$fixture; confirmation=$body" }
    $defaultButton=[DiskCuratorGuiSmoke]::SendMessage($renameConfirm,0x0400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -band 0xffff
    if ($defaultButton -ne 7) { throw 'The execution confirmation must default to No' }
    $no=[DiskCuratorGuiSmoke]::GetDlgItem($renameConfirm,7)
    [void][DiskCuratorGuiSmoke]::PostMessage($renameConfirm,0x0111,[IntPtr]7,$no)
    Wait-Ready 'decline single-file rename'
    if ((Test-Path -LiteralPath (Join-Path $folderA '已更名.txt')) -or -not (Test-Path -LiteralPath (Join-Path $folderA '甲.txt'))) { throw 'Declining confirmation changed the source file' }
    Set-Lens 0;Wait-Rows 3 'back to files after declined preview'
    Send-Action 106
    $archiveDialog=Wait-Dialog 'DCFormNative' 'archive options'
    $mode=[DiskCuratorGuiSmoke]::GetDlgItem($archiveDialog,2000)
    if ([DiskCuratorGuiSmoke]::SendMessage($mode,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 2 -or
        [DiskCuratorGuiSmoke]::SendMessage($mode,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0) { throw 'Archive options must provide two choices with type selected' }
    if ((Get-ControlText $mode) -notlike '*类型*') { throw 'Type archive choice lacks a readable label' }
    [void][DiskCuratorGuiSmoke]::SendMessage($mode,0x14E,[IntPtr]1,[IntPtr]::Zero)
    if ((Get-ControlText $mode) -notlike '*年月*') { throw 'Date archive choice lacks a readable label' }
    Close-TestDialog $archiveDialog
    Send-Action 5106
    $policyDialog=Wait-Dialog 'DCFormNative' 'retention settings'
    $number=[DiskCuratorGuiSmoke]::GetDlgItem($policyDialog,2000)
    Set-ControlText $number '3abc'
    [void][DiskCuratorGuiSmoke]::SendMessage($policyDialog,0x111,[IntPtr]1,[IntPtr]::Zero)
    $feedback=[DiskCuratorGuiSmoke]::GetDlgItem($policyDialog,1999)
    if ($feedback -eq [IntPtr]::Zero -or -not [DiskCuratorGuiSmoke]::HasVisibleStyle($feedback) -or
        (Get-ControlText $feedback) -notlike '*整数*' -or (Get-ControlText $number) -cne '3abc') {
        throw 'Invalid count must retain its input and explain the error in the same form'
    }
    Close-TestDialog $policyDialog
    Write-Host 'PASS guided forms: archive mode has readable choices and retention errors keep the original input.'
    # Request smart analysis explicitly from the same result workspace.
    Send-Action 119
    Wait-Lens 11 'explicit directory mapping analysis'
    if ((Get-RowCount) -lt 3) { throw 'Structure analysis did not expose editable mappings' }
    Send-Action 121
    Wait-Lens 9 'directory mapping preview'
    if ((Get-RowCount) -lt 1) { throw 'Structure preview is empty' }
    Save-TestWindow '03-preview.png'
    if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Read-only preview changed fixture files' }
    if (-not (Test-Path -LiteralPath (Join-Path $folderA '甲.txt')) -or -not (Test-Path -LiteralPath (Join-Path $folderB '乙.txt'))) { throw 'Fixture data changed during observation' }
    if ($CacheUnavailable) {
        $status=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,251)
        if ((Get-ControlText $status) -notlike '*索引未持久化*内存结果可用*') { throw 'Cache warning lost after analysis/preview' }
        Set-Lens 0
        Wait-Rows 3 'usable memory results after cache failure'
        if ((Get-ControlText $status) -notlike '*索引未持久化*') { throw 'Cache warning lost when switching perspective' }
        Write-Host 'PASS cache failure: content remains usable; warning persists through exact verification, preview and perspective changes.'
    }
    # Editing the manual scope invalidates even a previously frozen plan.
    $apply=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,108)
    if (-not [DiskCuratorGuiSmoke]::IsWindowEnabled($apply)) { throw 'Preview did not enable apply' }
    Set-ControlText $rootEdit $folderA
    Set-ControlText $rootEdit $fixture
    if ([DiskCuratorGuiSmoke]::IsWindowEnabled($apply)) { throw 'Changing scope left the old plan executable' }
    if (-not $CacheUnavailable) {
        Send-Action 121
        Wait-Lens 9 'rebuilt preview after scope change'
        Send-Action 108
        $dialog=[IntPtr]::Zero
        $deadline=[DateTime]::UtcNow.AddSeconds(10)
        while ([DateTime]::UtcNow -lt $deadline) {
            $dialog=[DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,'#32770')
            if ($dialog -ne [IntPtr]::Zero -and (Get-ControlText $dialog) -eq '确认执行操作计划') { break }
            Start-Sleep -Milliseconds 50
        }
        if ($dialog -eq [IntPtr]::Zero -or (Get-ControlText $dialog) -ne '确认执行操作计划' -or
            (Get-ControlText $rootEdit) -cne $fixture) { throw 'Unexpected operation or fixture root during apply test' }
        $body=[DiskCuratorGuiSmoke]::DialogBody($dialog)
        if (-not ($body.Contains($fixture) -and $body.Contains('整理结果'))) { throw 'Copy confirmation did not identify the operation root and destination' }
        $yes=[DiskCuratorGuiSmoke]::GetDlgItem($dialog,6)
        if ($yes -eq [IntPtr]::Zero) { throw 'Expected confirmation button missing' }
        [void][DiskCuratorGuiSmoke]::PostMessage($dialog,0x0111,[IntPtr]6,$yes)
        # Allow the confirmed dialog to finish dismissing before asserting that
        # the copy and history transition open no additional success notice.
        for ($i=0;$i -lt 100;$i++) {
            if ([DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,'#32770') -ne $dialog) { break }
            Start-Sleep -Milliseconds 25
        }
        Wait-Lens 7 'history after fixture copy' 
        $output=Join-Path $fixture '整理结果'
        if (-not (Test-Path -LiteralPath $output -PathType Container)) { throw 'Confirmed plan did not create output folder' }
        $copyHashes=@(Get-ChildItem -LiteralPath $output -File -Recurse | Get-FileHash | ForEach-Object { $_.Hash })
        foreach ($source in @((Join-Path $folderA '甲.txt'),(Join-Path $folderB '乙.txt'),(Join-Path $fixture 'unique.bin'))) {
            if ((Get-FileHash -LiteralPath $source).Hash -notin $copyHashes) { throw "Missing verified output content: $source" }
        }
        Set-Lens 0
        Wait-Rows 3 'stale snapshot after fixture copy'
        # Ordinary EN_SETFOCUS shall not dispatch an analysis command.
        [void][DiskCuratorGuiSmoke]::PostMessage($script:window,0x111,[IntPtr]16777331,$search)
        Wait-Ready 'browsing stale snapshot' 3
        $selectionContext=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,245)
        if ((Get-ControlText $selectionContext) -notlike '*过期*') { throw 'Stale marker lost on perspective switch' }
        foreach ($id in @(108,241,242)) {
            if ([DiskCuratorGuiSmoke]::IsWindowEnabled([DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id))) {
                throw "Stale snapshot enabled mutation/analysis control $id"
            }
        }
        Write-Host 'PASS fixture transaction: confirmed copy preserves sources, opens history, keeps snapshot stale and disables further operations.'
    }
    Write-Host 'PASS Windows GUI: scope, result lenses, filter identity, scan progress, exact analysis, read-only preview and invalidation on changed root.' 
}
finally {
    if ($previousDpiContext -ne [IntPtr]::Zero) { [void][DiskCuratorGuiSmoke]::SetThreadDpiAwarenessContext($previousDpiContext) }
    $env:LOCALAPPDATA=$previousLocalAppData
    if ($script:window -ne [IntPtr]::Zero) { [void][DiskCuratorGuiSmoke]::PostMessage($script:window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) }
    if ($null -ne $script:process -and -not $script:process.HasExited) {
        if (-not $script:process.WaitForExit(7000)) { Stop-Process -Id $script:process.Id -Force }
    }
    $tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    foreach ($created in @($cacheLocation,$fixture)) {
        $target=[IO.Path]::GetFullPath($created)
        if (-not $target.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($target) -notmatch '^DiskCurator-结果回归-[a-fA-F0-9]{32}(-appdata)?$') {
            throw "Unexpected fixture cleanup path: $target"
        }
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    }
}
