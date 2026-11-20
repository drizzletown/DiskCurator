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
function Wait-Rows([long]$expected,[string]$phase) {
    for ($i=0; $i -lt 100; $i++) {
        if ((Get-RowCount) -eq $expected) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "GUI ${phase}: expected $expected rows, got $(Get-RowCount)"
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
function Control([int]$id) { return [DiskCuratorGuiSmoke]::GetDlgItem($script:window,$id) }
function Is-Visible([int]$id) {
    $control=Control $id
    return $control -ne [IntPtr]::Zero -and [DiskCuratorGuiSmoke]::HasVisibleStyle($control)
}
function Text-At([int]$id) { return Get-ControlText (Control $id) }
function Click([int]$id) {
    $control=Control $id
    if (-not (Is-Visible $id) -or -not [DiskCuratorGuiSmoke]::IsWindowEnabled($control)) {
        throw "Control $id is disabled or hidden: $(Text-At $id)"
    }
    [void][DiskCuratorGuiSmoke]::PostMessage($control,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
}
function Shown-Text {
    return (@(100,102,108,244,245,247,248,250,251,253,254,273,276,277,278) | ForEach-Object {
        if (Is-Visible $_) { Text-At $_ }
    }) -join ' | '
}
function Wait-Until([scriptblock]$condition,[string]$phase,[int]$seconds=35) {
    $deadline=[DateTime]::UtcNow.AddSeconds($seconds)
    do {
        if ($script:process.HasExited) { throw "GUI exited during $phase" }
        if (& $condition) { return }
        Start-Sleep -Milliseconds 70
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "GUI timeout ($phase). Visible text: $(Shown-Text). Rows: $(Get-RowCount)."
}
function Click-And-Home {
    Click 273
    Wait-Until { (Is-Visible 270) -or ([DiskCuratorGuiSmoke]::FindOwned([uint32]$script:process.Id,'#32770') -ne [IntPtr]::Zero) } 'return to home or discard pending preview'
    if (-not (Is-Visible 270)) {
        $dialog=Wait-Dialog '#32770' 'discard an unexecuted preview' '磁盘文件整理助手' @(6,7) @('放弃尚未执行的预览')
        Accept-Dialog $dialog 6
        Wait-Until { Is-Visible 270 } 'home after discarding unexecuted preview'
    }
}
function Start-FolderTask([int]$id,[string]$folder) {
    Click $id
    Wait-Until { (Is-Visible 100) -and (Is-Visible 102) } 'choose task folder'
    Set-ControlText (Control 100) $folder
    Click 102
}
function Check-Filter([string]$term,[int]$count) {
    Set-ControlText (Control 115) $term
    Wait-Rows $count 'filter result'
}
function Accept-Dialog([IntPtr]$dialog,[int]$id) {
    $button=[DiskCuratorGuiSmoke]::GetDlgItem($dialog,$id)
    [void][DiskCuratorGuiSmoke]::PostMessage($dialog,0x111,[IntPtr]$id,$button)
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
    $script:list=Control 114
    $script:scanButton=Control 102
    $script:stage=Control 244
    $script:metrics=Control 247
    if (-not [DiskCuratorGuiSmoke]::SetWindowPos($script:window,[IntPtr]::Zero,0,0,820,600,0x16)) { throw 'Cannot size test window' }
    Assert-Layout @(270,271,272,250) 'task home at 820x600'
    foreach ($id in @(100,102,108,230,231)) { if (Is-Visible $id) { throw "Unexpected home control $id" } }
    foreach ($pair in @(@(270,'看空间占用'),@(271,'找重复文件'),@(272,'核对两份备份'))) {
        if ((Text-At $pair[0]) -notlike ('*'+$pair[1]+'*')) { throw 'Task action has an ambiguous label' }
    }
    Save-TestWindow '01-welcome.png'
    $folderA=Join-Path $fixture '原资料';$folderB=Join-Path $fixture '备份'
    [void][IO.Directory]::CreateDirectory($folderA);[void][IO.Directory]::CreateDirectory($folderB)
    $file=Join-Path $folderA '甲.txt'
    $twin=Join-Path $folderB '乙.txt'
    [IO.File]::WriteAllText($file,'same-content',[Text.Encoding]::UTF8)
    [IO.File]::WriteAllText($twin,'same-content',[Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $fixture '单一.bin'),'other',[Text.Encoding]::UTF8)
    $before=[IO.File]::GetLastWriteTimeUtc($file)
    Start-FolderTask 270 $fixture
    Wait-Until { (Is-Visible 114) -and (Get-RowCount) -ge 3 } 'metadata-only folder results'
    if ($before -ne [IO.File]::GetLastWriteTimeUtc($file) -or (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System'))) { throw 'Space scan modified the fixture' }
    if ((Test-Path -LiteralPath $cacheLocation -PathType Container) -and @(Get-ChildItem -LiteralPath $cacheLocation -File -Recurse -Force).Count -gt 0) { throw 'Metadata-only space task wrote a content index' }
    Check-Filter '原资料' 1
    Send-Action 5107
    Wait-Until { (Get-SelectedCount) -eq 1 } 'select a child folder'
    [void][DiskCuratorGuiSmoke]::PostMessage($script:list,0x0100,[IntPtr]13,[IntPtr]::Zero)
    Wait-Until { (Is-Visible 276) -and (Text-At 276) -like '*上一级*' -and (Get-RowCount) -eq 1 } 'open a folder in space results'
    Click 277
    $archive=Wait-Dialog 'DCFormNative' 'archive inside the selected folder' '' @(1,2,2000,2001)
    $mode=[DiskCuratorGuiSmoke]::GetDlgItem($archive,2000)
    if ([DiskCuratorGuiSmoke]::SendMessage($mode,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne 2) { throw 'Folder archive must offer type or year/month' }
    Accept-Dialog $archive 1
    Wait-Until { (Is-Visible 108) -and ((Shown-Text) -match '复制') } 'scoped copy archive preview'
    if (Test-Path -LiteralPath (Join-Path $folderA 'Organized')) { throw 'Folder archive preview wrote its destination' }
    Click 276
    Wait-Until { (Is-Visible 276) -and (Text-At 276) -like '*上一级*' } 'return to the previously opened folder'
    Click 276
    Wait-Until { -not (Is-Visible 276) -and (Get-RowCount) -eq 3 } 'navigate one level back to root'
    Click-And-Home
    Start-FolderTask 271 $fixture
    Wait-Until { (Is-Visible 114) -and ((Shown-Text) -match '无需|全部保留') } 'protected duplicates explained on the results page'
    if (Is-Visible 108) { throw 'Default protected pair offered execution' }
    if (Is-Visible 231) { throw 'Task results exposed the legacy thirteen-view picker' }
    Save-TestWindow '02-results.png'
    Check-Filter '甲.txt' 1
    Send-Action 5107
    Wait-Until { (Get-SelectedCount) -eq 1 } 'select a search result'
    Check-Filter '乙.txt' 1
    if ((Get-SelectedCount) -ne 0) { throw 'Changing the filter transferred file selection to another path' }
    Click 252
    Wait-Until { (Text-At 115) -eq '' } 'clear search'
    Click 257
    Wait-Until { (Is-Visible 114) -and -not (Is-Visible 257) -and ((Shown-Text) -match '核验') } 'explicit byte verification'
    Check-Filter '甲.txt' 1
    Send-Action 5107
    Wait-Until { (Get-SelectedCount) -eq 1 } 'select verified path for rename'
    [void][DiskCuratorGuiSmoke]::PostMessage($script:list,0x0100,[IntPtr]0x71,[IntPtr]::Zero)
    $rename=Wait-Dialog 'DCFormNative' 'F2 opens single file rename' '重命名文件' @(1,2,2000)
    if ((Get-ControlText ([DiskCuratorGuiSmoke]::GetDlgItem($rename,2000))) -ne '甲.txt') { throw 'F2 did not identify the selected filename' }
    Close-TestDialog $rename
    Click 252
    Wait-Until { (Text-At 115) -eq '' } 'clear verified file filter'
    if ($CacheUnavailable) {
        if ((Shown-Text) -notlike '*索引*') { throw 'Unavailable index must remain visible with usable in-memory results' }
    } else {
        Click 278
        $policy=Wait-Dialog 'DCFormNative' 'retention settings' '副本保留与内容核验设置' @(1,2,2000)
        $number=[DiskCuratorGuiSmoke]::GetDlgItem($policy,2000)
        if ((Get-ControlText $number) -ne '2') { throw 'Expected fresh two-copy retention' }
        Set-ControlText $number '3abc'
        Accept-Dialog $policy 1
        Wait-Until { (Get-ControlText ([DiskCuratorGuiSmoke]::GetDlgItem($policy,1999))) -like '*整数*' } 'invalid count remains in the settings dialog'
        Set-ControlText $number '1'
        Accept-Dialog $policy 1
        Wait-Until { [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window) -and (Is-Visible 277) } 'eligible duplicate action'
        Click 277
        Wait-Until { Is-Visible 108 } 'preview with one eligible file'
        Save-TestWindow '03-preview.png'
        Click 108
        $confirm=Wait-Dialog '#32770' 'review isolation' '确认执行操作计划' @(6,7) @('隔离仍占用磁盘空间')
        $default=[DiskCuratorGuiSmoke]::SendMessage($confirm,0x0400,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -band 0xffff
        if ($default -ne 7) { throw 'The execution confirmation must default to No' }
        Accept-Dialog $confirm 7
        Wait-Until { [DiskCuratorGuiSmoke]::IsWindowEnabled($script:window) } 'cancelled isolation'
        if ((Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) -or -not (Test-Path -LiteralPath $file) -or -not (Test-Path -LiteralPath $twin)) { throw 'Declining an isolation modified the fixture' }
        # Back out of the pending plan and rescan a fresh range.
    }
    Click-And-Home
    Click 272
    Wait-Until { (Is-Visible 100) -and (Is-Visible 274) } 'two independently editable backup directories'
    # Cover exact matches, missing files on both sides, and differing content at the same relative path.
    foreach($dir in @($folderA,$folderB)) { [IO.File]::WriteAllText((Join-Path $dir '相同.txt'),'equal') }
    [IO.File]::WriteAllText((Join-Path $folderA '两个版本.txt'),'source version')
    [IO.File]::WriteAllText((Join-Path $folderB '两个版本.txt'),'backup version')
    Set-ControlText (Control 100) $folderA;Set-ControlText (Control 274) $folderB
    Click 102
    Wait-Until { (Is-Visible 114) -and (Is-Visible 277) -and (Get-RowCount) -ge 3 } 'backup differences and same-root preview action'
    Check-Filter '两个版本.txt' 1
    Click 252
    Wait-Until { (Text-At 115) -eq '' } 'clear backup filter'
    Click 277
    Wait-Until { Is-Visible 108 } 'same-root copy preview'
    if (Test-Path -LiteralPath (Join-Path $fixture '.DiskCurator-System')) { throw 'Read-only comparison or preview created a transaction' }
    if (-not $CacheUnavailable) {
        Click 108
        $confirm=Wait-Dialog '#32770' 'copy into the backup' '确认执行操作计划' @(6,7)
        Accept-Dialog $confirm 6
        $expected=Join-Path $folderB '甲.txt'
        Wait-Until { (Test-Path -LiteralPath $expected) -and (Get-RowCount) -ge 1 -and -not (Is-Visible 108) } 'copy finishes and opens the result record'
        if ((Get-FileHash -LiteralPath $expected).Hash -ne (Get-FileHash -LiteralPath $file).Hash -or [IO.File]::ReadAllText((Join-Path $folderB '两个版本.txt')) -ne 'backup version') { throw 'Copy lost source content or overwrote a conflicting backup version' }
    }
    Click-And-Home
    Start-FolderTask 250 $fixture
    Wait-Until { (Is-Visible 114) -and (Get-RowCount) -ge 1 -and ((Shown-Text) -match '记录') } 'open history directly from home'
    if (Is-Visible 277) { throw 'Copy-only or empty history offered to undo the copied files' }
    Click-And-Home
    $partial=Join-Path $fixture '读取异常'
    [void][IO.Directory]::CreateDirectory($partial)
    [IO.File]::WriteAllText((Join-Path $partial '可读.txt'),'complete')
    $locked=[IO.File]::Open((Join-Path $partial '读取中.bin'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $locked.WriteByte(73);$locked.Flush()
        Start-FolderTask 271 $partial
        Wait-Until { (Is-Visible 114) -and ((Shown-Text) -match '异常|不完整') } 'incomplete content scan remains labelled'
        if ((Is-Visible 277) -or (Is-Visible 108)) { throw 'Incomplete observations offered automatic isolation' }
    } finally { $locked.Dispose() }
    Write-Host 'PASS task home, metadata-only space browsing, duplicate retention, filtering, preview, backup comparison, safe copy and recent operations.'

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
