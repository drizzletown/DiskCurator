param([string]$ExePath = 'build-win\Release\磁盘文件整理助手.exe', [switch]$CacheUnavailable, [string]$ScreenshotDir)
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
    public static int HeightOutsideControl(IntPtr parent, IntPtr control) {
        Rect client, child;
        if (!GetClientRect(parent,out client) || !GetWindowRect(control,out child)) throw new InvalidOperationException("Cannot read result layout.");
        return (client.bottom-client.top)-(child.bottom-child.top);
    }
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr control);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    public static string DialogBody(IntPtr parent) {
        var result = new StringBuilder();
        EnumChildWindows(parent,delegate(IntPtr child,IntPtr unused) {
            var className = new StringBuilder(80); GetClassName(child,className,80);
            if (String.Equals(className.ToString(),"Static",StringComparison.OrdinalIgnoreCase)) {
                var buffer = new StringBuilder(8192);
                GetText(child,0x000D,new IntPtr(buffer.Capacity),buffer);
                result.AppendLine(buffer.ToString());
            }
            return true;
        },IntPtr.Zero);
        return result.ToString();
    }
    public static string DescribeChildren(IntPtr parent) {
        var result = new StringBuilder();
        EnumChildWindows(parent,delegate(IntPtr child,IntPtr unused) {
            var name = new StringBuilder(80); GetClassName(child,name,80);
            result.AppendFormat("{0}#{1} ",name,GetDlgCtrlID(child));
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
        return FindOwned(process,wantedClass,null);
    }
    public static IntPtr FindOwned(uint process, string wantedClass, string wantedTitle) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr unused) {
            uint owner; GetWindowThreadProcessId(h,out owner);
            if(owner!=process) return true;
            var name=new StringBuilder(256); GetClassName(h,name,256);
            if(!String.Equals(name.ToString(),wantedClass,StringComparison.OrdinalIgnoreCase)) return true;
            if(!String.IsNullOrEmpty(wantedTitle)) {
                var title=new StringBuilder(512); GetWindowText(h,title,title.Capacity);
                if(title.ToString()!=wantedTitle) return true;
            }
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
function Wait-Dialog([string]$class,[string]$phase,[string]$title,[int[]]$requiredControls,[string[]]$bodyParts=@()) {
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    $processId=[uint32]$script:process.Id
    $body=''
    do {
        if ($script:process.HasExited) { throw "GUI exited during ${phase}" }
        $window=[DiskCuratorGuiSmoke]::FindOwned($processId,$class,$title)
        if ($window -ne [IntPtr]::Zero -and [DiskCuratorGuiSmoke]::HasVisibleStyle($window) -and
            -not [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window)) {
            $ready=$true
            foreach ($id in $requiredControls) {
                $control=[DiskCuratorGuiSmoke]::GetDlgItem($window,$id)
                if ($control -eq [IntPtr]::Zero -or -not [DiskCuratorGuiSmoke]::IsWindowEnabled($control)) { $ready=$false; break }
            }
            if ($ready -and $bodyParts.Count) {
                # MessageBox creates the top-level window before all localized child text is available.
                $body=[DiskCuratorGuiSmoke]::DialogBody($window)
                foreach ($part in $bodyParts) {
                    if (-not $body.Contains($part)) { $ready=$false; break }
                }
            }
            if ($ready) { return $window }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { $window=[DiskCuratorGuiSmoke]::FindOwned($processId,$class) }
    if ($window -ne [IntPtr]::Zero) {
        throw "GUI ${phase}: expected '$title' with controls [$requiredControls] and text [$bodyParts]. Found '$(Get-ControlText $window)', children $([DiskCuratorGuiSmoke]::DescribeChildren($window)), confirmation body [$body]"
    }
    throw "GUI ${phase}: expected '$title' dialog missing."
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
function Assert-Hidden([int[]]$ids,[string]$phase) {
    foreach ($id in $ids) {
        $control=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id)
        if ($control -eq [IntPtr]::Zero -or [DiskCuratorGuiSmoke]::HasVisibleStyle($control)) { throw "GUI ${phase}: control $id should exist and be hidden" }
    }
}
function Click-DuplicateShortcut([string]$expectedLabel) {
    $button=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,257)
    if ((Get-ControlText $button) -cne $expectedLabel -or -not [DiskCuratorGuiSmoke]::IsWindowEnabled($button) -or
        -not [DiskCuratorGuiSmoke]::HasVisibleStyle($button)) { throw "Expected active duplicate shortcut '$expectedLabel', found '$(Get-ControlText $button)'" }
    [void][DiskCuratorGuiSmoke]::SendMessage($button,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
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
    if ((Get-ControlText $script:window) -cne '磁盘文件整理助手') { throw 'Main window did not display the product name' }
    foreach ($id in @(112,123,200,201,202,203,204,205,206)) {
        if ([DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id) -ne [IntPtr]::Zero) { throw "Removed sidebar/control still exists: $id" }
    }
    # Exercise the compact layout without showing the test window or moving mouse focus.
    if (-not [DiskCuratorGuiSmoke]::SetWindowPos($script:window,[IntPtr]::Zero,0,0,820,600,0x16)) { throw 'Cannot size the test window' }
    Assert-Layout @(253,254,255,256) 'welcome at 820x600'
    Assert-Hidden @(100,101,102,103,114,115,230,231,243,257,258) 'welcome without a selected range'
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
    $status=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,251)
    if ([DiskCuratorGuiSmoke]::SendMessage($scope,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 0) { throw 'Selected-drive mode must be default' }
    if ([DiskCuratorGuiSmoke]::IsWindowEnabled($rootEdit)) { throw 'Unselected drive scope must not enable root edit' }
    # Follow both visible entry points. Cancel both pickers; only the synthetic path below is submitted.
    $driveEntry=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,256)
    [void][DiskCuratorGuiSmoke]::PostMessage($driveEntry,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    $driveDialog=Wait-Dialog 'DCFormNative' 'drive selection' '选择本次需要扫描的磁盘（可以多选）' @(1,2,2000)
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
    $folderDialog=Wait-Dialog '#32770' 'folder selection' '选择要扫描的文件夹' @(2)
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
    # The first scan has one unique file and no duplicate groups. The empty
    # result must not insert a synthetic, selectable file into the list.
    Set-ControlText $rootEdit $folderA
    Assert-Layout @(100,101,102,230,258) 'selected folder before scanning'
    $quickButton=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,258)
    if ((Get-ControlText $quickButton) -ne '快速盘点') { throw 'A selected folder must offer quick inventory' }
    $file=Join-Path $folderA '甲.txt'
    $before=[IO.File]::GetLastWriteTimeUtc($file)
    $expectedCounts="1 个文件 · 1 个目录 · $((Get-Item -LiteralPath $file).Length) B"
    [void][DiskCuratorGuiSmoke]::SendMessage($quickButton,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Ready 'metadata-only inventory'
    $headline=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,253)
    $detail=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,254)
    if ((Get-ControlText $headline) -cne '快速盘点完成' -or (Get-ControlText $detail) -cne $expectedCounts) {
        throw "Unexpected quick inventory results: $(Get-ControlText $headline) / $(Get-ControlText $detail)"
    }
    if ((Get-ControlText $rootEdit) -cne $folderA -or (Get-ControlText $script:scanButton) -cne '开始扫描') { throw 'Inventory must preserve the selected scope for a full scan' }
    Assert-Hidden @(108,114,115,231,240,241,242,257) 'metadata-only result'
    if ((Test-Path -LiteralPath (Join-Path $folderA '.DiskCurator-System')) -or
        @(Get-ChildItem -LiteralPath $folderA -Force).Count -ne 1 -or
        [IO.File]::GetLastWriteTimeUtc($file) -ne $before -or [IO.File]::ReadAllText($file) -cne 'same-content') { throw 'Quick inventory modified its fixture' }
    if ((Test-Path -LiteralPath $cacheLocation -PathType Container) -and
        @(Get-ChildItem -LiteralPath $cacheLocation -Recurse -File -Force).Count -gt 0) { throw 'Quick inventory wrote a cache file' }
    Write-Host 'PASS quick inventory: count and size shown without modifying the fixture or writing an index; the original scope remains available for SHA-256 scanning.'
    Assert-Hidden @(103,243) 'idle before scanning'
    if (-not [DiskCuratorGuiSmoke]::IsWindowEnabled($script:scanButton)) { throw 'A selected folder must enable scanning' }
    [void][DiskCuratorGuiSmoke]::SendMessage($script:scanButton,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Lens 0 'one unique file'
    Wait-Rows 1 'one unique file'
    Click-DuplicateShortcut '重复文件'
    Wait-Lens 2 'no duplicate groups'
    Wait-Rows 0 'no fake rows for an empty group list'
    if ((Get-ControlText $status) -notlike '*未发现*') { throw 'The empty duplicate view must explain that no duplicate files were found' }
    Click-DuplicateShortcut '返回文件'
    Wait-Lens 0 'return to the unique file'
    Wait-Rows 1 'return to the unique file'
    # An unavailable file should leave the readable results open and identify
    # the affected path in the overview. No special account rights are needed.
    $lockedPath=Join-Path $folderA '正在使用.bin'
    $locked=[IO.File]::Open($lockedPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $locked.WriteByte(17)
        $locked.Flush()
        [void][DiskCuratorGuiSmoke]::SendMessage($script:scanButton,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
        Wait-Lens 0 'partial scan without a blocking dialog'
        if ((Get-ControlText $status) -notlike '*扫描不完整*1 项异常*扫描概览*') { throw 'Partial results must point to the issue count and details' }
        Set-Lens 12
        Set-ControlText $search '正在使用.bin'
        Wait-Rows 1 'unreadable file in scan overview'
        if ((Get-ControlText $status) -notlike '*扫描不完整*') { throw 'Filtered results lost the partial scan warning' }
        Write-Host 'PASS partial scan: accessible results stay open and the overview identifies the unreadable file.'
    } finally {
        $locked.Dispose()
        Remove-Item -LiteralPath $lockedPath -Force
    }
    Set-ControlText $rootEdit $fixture
    [void][DiskCuratorGuiSmoke]::SendMessage($script:scanButton,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Ready 'Unicode content scan'
    Wait-Rows 3 'file perspective after scan'
    if ([DiskCuratorGuiSmoke]::SendMessage($bar,0x408,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 1000) { throw 'Completed scan progress is not 100%' }
    if ([string]::IsNullOrWhiteSpace((Get-ControlText $script:metrics))) { throw 'Scan byte/time metrics are empty' }
    Assert-Layout @(100,101,102,114,115,230,231,240,241,242,245,247,251,252,257) 'results at 820x600'
    Assert-Hidden @(103,243) 'completed scan'
    if ([DiskCuratorGuiSmoke]::HeightOutsideControl($script:window,$script:list) -gt 360) { throw 'The completed scan did not reclaim space for the result list' }
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
    Click-DuplicateShortcut '重复文件'
    Wait-Lens 2 'duplicate files shortcut'
    Wait-Rows 2 'duplicate candidate perspective'
    Set-ControlText $search '不在夹具中的文件'
    Wait-Rows 0 'no matches in duplicate groups'
    if ((Get-ControlText $status) -notlike '*没有匹配*') { throw 'Filtered duplicate view must explain that the search matched no files' }
    Assert-Layout @(252) 'clear empty duplicate search'
    [void][DiskCuratorGuiSmoke]::SendMessage($clear,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-Rows 2 'restore duplicate groups after empty search'
    if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Observation created a transaction workspace' }
    if (-not $CacheUnavailable) {
        $previewNote=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,248)
        $apply=[DiskCuratorGuiSmoke]::GetDlgItem($script:window,108)
        foreach ($keep in @(2,1)) {
            Send-Action 107
            $retention=Wait-Dialog 'DCFormNative' 'automatic quarantine retention' '当前扫描范围的多余副本 · 保留策略' @(1,2,2000)
            $number=[DiskCuratorGuiSmoke]::GetDlgItem($retention,2000)
            if ($keep -eq 2 -and (Get-ControlText $number) -ne '2') { throw 'Fresh quarantine policy must preserve two copies' }
            if ($keep -eq 1) { Set-ControlText $number '1' }
            [void][DiskCuratorGuiSmoke]::PostMessage($retention,0x0111,[IntPtr]1,[IntPtr]::Zero)
            Wait-Lens 9 'automatic quarantine preview'
            $totals=Get-ControlText $script:metrics
            if ($keep -eq 2) {
                if ($totals -cne '同哈希候选 1 组 · 拟隔离 0 个文件，共 0 B' -or
                    (Get-ControlText $previewNote) -cne '1 组各有 2 份或更少，全部保留。') {
                    throw 'Zero-move preview must identify the protected pair and the current retention floor'
                }
                Assert-Hidden @(108) 'no quarantine for a protected pair'
                if ([DiskCuratorGuiSmoke]::IsWindowEnabled($apply)) { throw 'Empty quarantine plan allowed execution' }
            } else {
                $size=(Get-Item -LiteralPath $file).Length
                if ($totals -cne "同哈希候选 1 组 · 拟隔离 1 个文件，共 $size B") { throw 'Quarantine preview has incorrect planned file or byte counts' }
                Assert-Layout @(108) 'quarantine plan with one eligible file'
                Send-Action 108
                $confirm=Wait-Dialog '#32770' 'review automatic quarantine' '确认执行操作计划' @(6,7) @($totals,'执行前逐字节核对保留副本','隔离仍占用磁盘空间')
                $no=[DiskCuratorGuiSmoke]::GetDlgItem($confirm,7)
                [void][DiskCuratorGuiSmoke]::PostMessage($confirm,0x0111,[IntPtr]7,$no)
                Wait-Ready 'return to the declined quarantine preview'
            }
            Set-Lens 2
            Wait-Rows 2 'continue browsing the duplicate pair'
        }
        if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Quarantine preview created a transaction' }
        Write-Host 'PASS quarantine preview: protected pair, eligible file and bytes, explicit byte recheck, and cancellation.'
    }
    # Exact verification is an explicit action; its output replaces candidate
    # wording only after the comparison task has completed successfully.
    Click-DuplicateShortcut '核验副本'
    Wait-Lens 2 'explicit byte verification'
    Click-DuplicateShortcut '返回文件'
    Wait-Lens 0 'return from verified duplicate groups'
    Wait-Rows 3 'file perspective reuse'
    Set-ControlText $search '当前根精确副本 2'
    Wait-Rows 2 'explicit byte verification'
    Wait-Ready 'explicit byte verification'
    Set-ControlText $search ''
    Set-ControlText $search '甲.txt';Wait-Rows 1 'select file for direct rename'
    Send-Action 5107
    [void][DiskCuratorGuiSmoke]::PostMessage($script:list,0x0100,[IntPtr]0x71,[IntPtr]::Zero) # F2 down
    [void][DiskCuratorGuiSmoke]::PostMessage($script:list,0x0101,[IntPtr]0x71,[IntPtr]::Zero)
    $rename=Wait-Dialog 'DCFormNative' 'single file rename' '重命名文件' @(1,2,2000)
    $newName=[DiskCuratorGuiSmoke]::GetDlgItem($rename,2000)
    if ((Get-ControlText $newName) -cne '甲.txt' -or [DiskCuratorGuiSmoke]::GetDlgItem($rename,2001) -ne [IntPtr]::Zero) { throw 'Single-file rename should open one prefilled name field' }
    Set-ControlText $newName '已更名.txt'
    [void][DiskCuratorGuiSmoke]::PostMessage($rename,0x0111,[IntPtr]1,[IntPtr]::Zero)
    Wait-Lens 9 'single-file rename preview'
    if ((Get-ControlText $search) -ne '') { throw 'The new rename preview inherited the file selection filter' }
    Set-ControlText $search '已更名.txt';Wait-Rows 1 'one mapped rename destination'
    Send-Action 108
    $renameConfirm=Wait-Dialog '#32770' 'review single file rename' '确认执行操作计划' @(6,7) @($fixture,'甲.txt','已更名.txt','执行上述全部变更')
    $defaultButton=[DiskCuratorGuiSmoke]::SendMessage($renameConfirm,0x0400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -band 0xffff
    if ($defaultButton -ne 7) { throw 'The execution confirmation must default to No' }
    $no=[DiskCuratorGuiSmoke]::GetDlgItem($renameConfirm,7)
    [void][DiskCuratorGuiSmoke]::PostMessage($renameConfirm,0x0111,[IntPtr]7,$no)
    Wait-Ready 'decline single-file rename'
    Wait-Lens 9 'declining must preserve the preview'
    if ((Get-ControlText $search) -cne '已更名.txt' -or (Get-RowCount) -ne 1) { throw 'Declining execution lost the preview filter' }
    if ((Test-Path -LiteralPath (Join-Path $folderA '已更名.txt')) -or -not (Test-Path -LiteralPath (Join-Path $folderA '甲.txt'))) { throw 'Declining confirmation changed the source file' }
    Set-Lens 0;Wait-Rows 3 'back to files after declined preview'
    Send-Action 106
    $archiveDialog=Wait-Dialog 'DCFormNative' 'archive options' '归档当前目录的文件' @(1,2,2000)
    $mode=[DiskCuratorGuiSmoke]::GetDlgItem($archiveDialog,2000)
    if ([DiskCuratorGuiSmoke]::SendMessage($mode,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 2 -or
        [DiskCuratorGuiSmoke]::SendMessage($mode,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 0) { throw 'Archive options must provide two choices with type selected' }
    if ((Get-ControlText $mode) -notlike '*类型*') { throw 'Type archive choice lacks a readable label' }
    [void][DiskCuratorGuiSmoke]::SendMessage($mode,0x14E,[IntPtr]1,[IntPtr]::Zero)
    if ((Get-ControlText $mode) -notlike '*年月*') { throw 'Date archive choice lacks a readable label' }
    Close-TestDialog $archiveDialog
    Send-Action 5106
    $policyDialog=Wait-Dialog 'DCFormNative' 'retention settings' '副本保留与内容核验设置' @(1,2,2000)
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
    $unfilteredRows=Get-RowCount
    if ($unfilteredRows -lt 3) { throw 'Structure preview is missing affected paths' }
    Set-ControlText $search '不在预览内的文件'
    Wait-Rows 0 'filtered structure preview'
    Send-Action 121
    Wait-Rows $unfilteredRows 'new plan in the same view must show all changes'
    Wait-Lens 9 'regenerated preview'
    if ((Get-ControlText $search) -ne '' -or [DiskCuratorGuiSmoke]::SendMessage($script:list,0x1027,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 0) {
        throw 'Regenerating a plan must clear its filter and start the preview at row zero'
    }
    Save-TestWindow '03-preview.png'
    if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Read-only preview changed fixture files' }
    if (-not (Test-Path -LiteralPath (Join-Path $folderA '甲.txt')) -or -not (Test-Path -LiteralPath (Join-Path $folderB '乙.txt'))) { throw 'Fixture data changed during observation' }
    if ($CacheUnavailable) {
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
        Assert-Hidden @(249) 'already reviewing the plan'
        Assert-Layout @(108) 'plan can be confirmed from its preview'
        Set-Lens 0
        Wait-Lens 0 'browse files while a complete plan is ready'
        Assert-Hidden @(108) 'confirmation only belongs in preview'
        Assert-Layout @(249) 'return to pending preview'
        if ((Get-ControlText $script:stage) -notlike '*预览已就绪*') { throw 'File view mislabeled the pending preview' }
        Send-Action 249
        Wait-Lens 9 'reopen complete preview before confirming'
        Send-Action 108
        $dialog=Wait-Dialog '#32770' 'review fixture copy' '确认执行操作计划' @(6,7) @($fixture,'整理结果')
        if ([DiskCuratorGuiSmoke]::SendMessage($script:lens,0x147,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -ne 9) { throw 'Confirmation from another view must show the pending preview behind it' }
        if ((Get-ControlText $rootEdit) -cne $fixture) { throw 'Unexpected fixture root during apply test' }
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
        Click-DuplicateShortcut '重复文件'
        Wait-Lens 2 'read-only stale duplicate view'
        Click-DuplicateShortcut '返回文件'
        Wait-Lens 0 'return without verifying a stale snapshot'
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
