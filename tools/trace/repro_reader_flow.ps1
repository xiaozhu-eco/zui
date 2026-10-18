# Drive zui_reader through the real wheel path and capture a flow dump.
#
# The reader's ghost text only shows up while the reading pane is scrolled the way a wheel scrolls
# it: App::set("scroll_top") marks geometry dirty but never re-maps the world, so a harness built on
# it would photograph a frozen frame. This script posts WM_MOUSEWHEEL to the SDL window instead,
# which is the same path a user's wheel takes.
#
# Usage:
#   pwsh tools/trace/repro_reader_flow.ps1                 # 60 notches down, then close
#   pwsh tools/trace/repro_reader_flow.ps1 -Notches 200
#   pwsh tools/trace/repro_reader_flow.ps1 -Items 6 -KeepWindow
#   pwsh tools/trace/repro_reader_flow.ps1 -ApiScroll 900  # data path instead of the wheel

param(
    [string]$Exe = "D:\code\zui\build\windows-x64-release\zui_reader.exe",
    [string]$Dump = "D:\code\zui\build\trace\flow_wheel.txt",
    [int]$Notches = 60,
    [int]$Items = 6,
    [int]$SettleMs = 7000,
    [int]$StepMs = 40,
    # Notches posted before a breath: the dump only records a scroll that moved more than 200px in
    # one frame, which is how a human flicks the wheel, not how one notch arrives.
    [int]$Burst = 8,
    [switch]$KeepWindow,
    [switch]$DebugInput,
    # >0 walks the pane through `App::set(..., "scroll_top", ...)` instead of the wheel: the data
    # path, which is the one a script uses. The reader stops itself once the value stops growing.
    [float]$ApiScroll = 0,
    # Screenshot of the client area taken after the scroll, so the dump and the pixels can be read
    # together. Pass "" to skip it.
    [string]$Shot = "D:\code\zui\build\trace\reader_shot.png"
)

$ErrorActionPreference = "Stop"

Add-Type -Namespace ZuiRepro -Name Native -MemberDefinition @'
[DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string win);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, System.IntPtr p);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(System.IntPtr h, System.Text.StringBuilder s, int n);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool ClientToScreen(System.IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr wp, System.IntPtr lp);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr h, int cmd);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, System.IntPtr extra);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public delegate bool EnumProc(System.IntPtr h, System.IntPtr p);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
'@

function Get-WindowFor([int]$targetPid) {
    $script:found = [System.IntPtr]::Zero
    $cb = [ZuiRepro.Native+EnumProc]{
        param($h, $p)
        $owner = 0
        [void][ZuiRepro.Native]::GetWindowThreadProcessId($h, [ref]$owner)
        if ($owner -ne $targetPid) { return $true }
        $cls = New-Object System.Text.StringBuilder 128
        [void][ZuiRepro.Native]::GetClassNameW($h, $cls, 128)
        if ($cls.ToString() -like "SDL*") { $script:found = $h; return $false }
        return $true
    }
    [void][ZuiRepro.Native]::EnumWindows($cb, [System.IntPtr]::Zero)
    return $script:found
}

function Get-ClientScreenPoint([System.IntPtr]$hwnd, [int]$x, [int]$y) {
    $screen = New-Object ZuiRepro.Native+POINT
    $screen.X = $x; $screen.Y = $y
    [void][ZuiRepro.Native]::ClientToScreen($hwnd, [ref]$screen)
    return $screen
}

function Send-Wheel([System.IntPtr]$hwnd, [int]$x, [int]$y, [int]$delta) {
    # WM_MOUSEMOVE takes client coordinates, WM_MOUSEWHEEL screen coordinates. SDL3 ignores posted
    # wheel messages (it only acts on ones the OS delivers), so the real input queue is used: the
    # cursor is moved over the pane, which is why the caller restores it afterwards.
    $screen = Get-ClientScreenPoint $hwnd $x $y
    $move = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][ZuiRepro.Native]::PostMessageW($hwnd, 0x0200, [IntPtr]::Zero, $move)
    $lp = [IntPtr]((($screen.Y -band 0xFFFF) -shl 16) -bor ($screen.X -band 0xFFFF))
    $wp = [IntPtr]((($delta -band 0xFFFF) -shl 16))
    [void][ZuiRepro.Native]::PostMessageW($hwnd, 0x020A, $wp, $lp)
    [void][ZuiRepro.Native]::SetCursorPos($screen.X, $screen.Y)
    [ZuiRepro.Native]::mouse_event(0x0800, 0, 0, $delta, [System.IntPtr]::Zero)
}

function Show-Dump([string]$Path, [string]$ErrPath) {
    Write-Host "dump: $Path"
    if (Test-Path $ErrPath) {
        Write-Host "---- stderr (last 12) ----"
        Get-Content $ErrPath -Tail 12
    }
    if (Test-Path $Path) {
        Get-Item $Path | Select-Object Length, LastWriteTime
        Select-String -Path $Path -Pattern 'title\[|T@\(|===== dump' | Select-Object -Last 30 |
            ForEach-Object { $_.Line }
    }
}

if (Test-Path $Dump) { Remove-Item -LiteralPath $Dump -Force }

$env:ZUI_DEBUG_FLOW = $Dump
if ($Items -gt 0) { $env:ZUI_DEBUG_FLOW_ITEMS = "$Items" }
# Open the saved book for us: the wheel below can only reach the reading pane once it is up.
$env:ZUI_READER_AUTOOPEN = "1"
if ($ApiScroll -gt 0) {
    $env:ZUI_READER_AUTOPLAY = "$ApiScroll"
    $env:ZUI_READER_AUTOPLAY_EXIT = "1"
} else {
    $env:ZUI_READER_AUTOPLAY = $null
    $env:ZUI_READER_AUTOPLAY_EXIT = $null
}
if ($DebugInput) { $env:ZUI_DEBUG_INPUT = "1" } else { $env:ZUI_DEBUG_INPUT = $null }

Write-Host "launching $Exe"
# Physical pixels everywhere: the reader's 880x576 client area is 1100x720 on a 125% display, and a
# DPI-unaware reader would compute the wheel target and the screenshot rect in virtual pixels.
[void][ZuiRepro.Native]::SetProcessDPIAware()
$errLog = "$Dump.stderr.txt"
$proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
    -RedirectStandardError $errLog -RedirectStandardOutput "$Dump.stdout.txt"
Start-Sleep -Milliseconds $SettleMs

if ($ApiScroll -gt 0) {
    # The reader walks the pane itself and quits when the value stops growing: there is no window to
    # raise, no cursor to move and no wheel to send, only a dump to read.
    Write-Host "api scroll: waiting for the reader to walk the pane"
    $proc.WaitForExit(120000) | Out-Null
    if (-not $proc.HasExited) {
        try { Stop-Process -Id $proc.Id -Force -ErrorAction Stop } catch { }
        Start-Sleep -Milliseconds 500
        if (-not $proc.HasExited) { Write-Warning "reader $($proc.Id) is still running" }
    }
    Show-Dump $Dump $errLog
    exit 0
}

$hwnd = [System.IntPtr]::Zero
for ($i = 0; $i -lt 40 -and $hwnd -eq [System.IntPtr]::Zero; $i++) {
    $hwnd = Get-WindowFor $proc.Id
    if ($hwnd -eq [System.IntPtr]::Zero) { Start-Sleep -Milliseconds 250 }
}
if ($hwnd -eq [System.IntPtr]::Zero) { throw "no SDL window for pid $($proc.Id)" }

$rect = New-Object ZuiRepro.Native+RECT
[void][ZuiRepro.Native]::GetClientRect($hwnd, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
Write-Host "window client ${w}x${h}"
[void][ZuiRepro.Native]::ShowWindow($hwnd, 5)
# SDL routes a wheel only to the window that holds the mouse, and the mouse can only be over this
# window if it is the top one: raise it for the run and drop the flag again on the way out.
[void][ZuiRepro.Native]::SetWindowPos($hwnd, [System.IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0040)
[void][ZuiRepro.Native]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 300

$savedCursor = New-Object ZuiRepro.Native+POINT
[void][ZuiRepro.Native]::GetCursorPos([ref]$savedCursor)

# The reading pane sits under the middle of the window; the wheel has to land on it, not on the
# shelf behind it.
$px = [int]($w / 2)
$py = [int]($h * 0.55)

# A background process cannot always raise a window, and SDL only takes wheel input for the active
# window: click the pane once, which both focuses it and is what a reader does before scrolling.
$clickPoint = Get-ClientScreenPoint $hwnd $px $py
[void][ZuiRepro.Native]::SetCursorPos($clickPoint.X, $clickPoint.Y)
[ZuiRepro.Native]::mouse_event(0x0002, 0, 0, 0, [System.IntPtr]::Zero)
[ZuiRepro.Native]::mouse_event(0x0004, 0, 0, 0, [System.IntPtr]::Zero)
Start-Sleep -Milliseconds 300
$fg = [ZuiRepro.Native]::GetForegroundWindow()
Write-Host ("foreground match: {0}" -f ($fg -eq $hwnd))

for ($i = 0; $i -lt $Notches; $i++) {
    Send-Wheel $hwnd $px $py -120
    if ((($i + 1) % $Burst) -eq 0) { Start-Sleep -Milliseconds $StepMs }
}
Start-Sleep -Milliseconds 800

if ($Shot -ne "") {
    # Capture from the screen, not PrintWindow: the SDL window is GL-backed and GDI's PrintWindow
    # returns black for those.
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    $origin = Get-ClientScreenPoint $hwnd 0 0
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "shot: $Shot"
}

[void][ZuiRepro.Native]::SetCursorPos($savedCursor.X, $savedCursor.Y)
[void][ZuiRepro.Native]::SetWindowPos($hwnd, [System.IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002)

if (-not $KeepWindow) {
    [void][ZuiRepro.Native]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $proc.WaitForExit(5000) | Out-Null
    # A leftover reader keeps zui.dll mapped and the next link fails with LNK1104, so make sure it
    # is really gone.
    if (-not $proc.HasExited) {
        try { Stop-Process -Id $proc.Id -Force -ErrorAction Stop } catch { }
        Start-Sleep -Milliseconds 500
        if (-not $proc.HasExited) { Write-Warning "reader $($proc.Id) is still running" }
    }
}

Show-Dump $Dump $errLog
