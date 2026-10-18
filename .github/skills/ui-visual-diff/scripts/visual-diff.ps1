param(
    [string[]]$Page = @(),
    [switch]$All,
    [switch]$WebOnly,
    [switch]$AppOnly,
    [switch]$UpdateBaseline,
    [int]$Width = 1100,
    [int]$Height = 720,
    [int]$Tol = 36,
    [double]$FailDelta = 0.5,
    # Multi-scroll / full-page (opt-in). Default path is still a single 1100x720 viewport shot.
    [string]$ScrollId = '',
    [string]$ScrollTops = '',
    [switch]$ScrollAbsolute,
    [switch]$FullPage,
    [int]$ScrollWaitMs = 250,
    [int]$WaitMs = 0
)

# Same-scale visual self-check: render a page in our own app and in a real browser at the
# same client size, then diff the two images pixel by pixel.
#
#   powershell -File scripts/visual-diff.ps1 -Page hello checks
#   powershell -File scripts/visual-diff.ps1 -All
#   powershell -File scripts/visual-diff.ps1 -All -UpdateBaseline   # after an intended change
#
# Artifacts: <repo>/build/visual-diff/<page>.{app,web,diff}.png + baseline.json
# Optional: -ScrollId/-ScrollTops/-FullPage write <page>.{app,web}.sN.png or .full.png
# App *geometry* for scrolled frames uses zui_ui_probe (build/visual-diff/<page>.app.sN.boxes.txt).
# App *PNG* may still be window-capture s0 / FullPage PageDown (no public App scroll API).

$ErrorActionPreference = 'Stop'
# `powershell -File script.ps1 -Page a,b` arrives as one comma-joined string, so split it here.
$Page = @($Page | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class VisualDiff {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public const uint NOMOVE_NOZORDER = 0x0002 | 0x0004;

    // Force the *client* area to cx x cy by growing/shrinking the frame around it.
    public static void FitClient(IntPtr h, int cx, int cy) {
        RECT o; RECT c;
        GetWindowRect(h, out o);
        GetClientRect(h, out c);
        int dx = (o.Right - o.Left) - (c.Right - c.Left);
        int dy = (o.Bottom - o.Top) - (c.Bottom - c.Top);
        SetWindowPos(h, IntPtr.Zero, o.Left, o.Top, cx + dx, cy + dy, NOMOVE_NOZORDER);
    }

    public static string CaptureClient(IntPtr h, string path) {
        RECT c;
        GetClientRect(h, out c);
        int w = c.Right - c.Left, ht = c.Bottom - c.Top;
        POINT p = new POINT(); p.X = 0; p.Y = 0;
        ClientToScreen(h, ref p);
        using (var bmp = new Bitmap(w, ht))
        using (var g = Graphics.FromImage(bmp)) {
            g.CopyFromScreen(p.X, p.Y, 0, 0, bmp.Size);
            bmp.Save(path, ImageFormat.Png);
        }
        return w + "x" + ht;
    }

    public static string Compare(string pathA, string pathB, string pathOut, int tol) {
        using (var a = new Bitmap(pathA))
        using (var b = new Bitmap(pathB)) {
            int w = Math.Min(a.Width, b.Width), h = Math.Min(a.Height, b.Height);
            var rect = new Rectangle(0, 0, w, h);
            using (var da = a.Clone(rect, PixelFormat.Format32bppArgb))
            using (var db = b.Clone(rect, PixelFormat.Format32bppArgb)) {
                var la = da.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                var lb = db.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                int n = w * h * 4;
                byte[] pa = new byte[n], pb = new byte[n], po = new byte[n];
                Marshal.Copy(la.Scan0, pa, 0, n);
                Marshal.Copy(lb.Scan0, pb, 0, n);
                da.UnlockBits(la); db.UnlockBits(lb);
                int cell = 20, cw = (w + cell - 1) / cell, ch = (h + cell - 1) / cell;
                int[] counts = new int[cw * ch];
                long diff = 0;
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        int i = (y * w + x) * 4;
                        int d = Math.Abs(pa[i] - pb[i]) + Math.Abs(pa[i + 1] - pb[i + 1]) + Math.Abs(pa[i + 2] - pb[i + 2]);
                        if (d > tol) {
                            diff++;
                            counts[(y / cell) * cw + (x / cell)]++;
                            po[i] = 255; po[i + 1] = 0; po[i + 2] = 255; po[i + 3] = 255;
                        } else {
                            po[i] = (byte)(pa[i] / 3); po[i + 1] = (byte)(pa[i + 1] / 3); po[i + 2] = (byte)(pa[i + 2] / 3); po[i + 3] = 255;
                        }
                    }
                }
                using (var outBmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                    var lo = outBmp.LockBits(rect, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
                    Marshal.Copy(po, 0, lo.Scan0, n);
                    outBmp.UnlockBits(lo);
                    outBmp.Save(pathOut, ImageFormat.Png);
                }
                var sb = new System.Text.StringBuilder();
                double pct = 100.0 * diff / (w * h);
                sb.AppendFormat("diff={0:F2}%", pct);
                var idx = new int[cw * ch];
                for (int i = 0; i < idx.Length; i++) idx[i] = i;
                Array.Sort(idx, (p2, q) => counts[q].CompareTo(counts[p2]));
                int shown = 0;
                for (int k = 0; k < idx.Length && shown < 6; k++) {
                    int id = idx[k];
                    if (counts[id] < 30) break;
                    sb.AppendFormat(" [x{0}-{1} y{2}-{3} n={4}]", (id % cw) * cell, (id % cw) * cell + cell - 1,
                                    (id / cw) * cell, (id / cw) * cell + cell - 1, counts[id]);
                    shown++;
                }
                return sb.ToString();
            }
        }
    }

    public static void StitchVertical(string[] paths, string pathOut) {
        if (paths == null || paths.Length == 0) throw new ArgumentException("no strips");
        var bitmaps = new System.Collections.Generic.List<Bitmap>();
        try {
            int width = 0;
            foreach (var p in paths) {
                var bmp = new Bitmap(p);
                bitmaps.Add(bmp);
                if (width == 0) width = bmp.Width;
            }
            int overlap = Math.Max(0, bitmaps[0].Height / 10);
            int stitchedH = bitmaps[0].Height;
            for (int i = 1; i < bitmaps.Count; i++) stitchedH += Math.Max(1, bitmaps[i].Height - overlap);
            using (var outBmp = new Bitmap(width, stitchedH))
            using (var g = Graphics.FromImage(outBmp)) {
                g.Clear(Color.Black);
                int y = 0;
                for (int i = 0; i < bitmaps.Count; i++) {
                    g.DrawImage(bitmaps[i], 0, y, bitmaps[i].Width, bitmaps[i].Height);
                    if (i + 1 < bitmaps.Count) y += Math.Max(1, bitmaps[i].Height - overlap);
                }
                outBmp.Save(pathOut, ImageFormat.Png);
            }
        } finally {
            foreach (var b in bitmaps) b.Dispose();
        }
    }

    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public const uint WM_MOUSEWHEEL = 0x020A;
    public const uint WM_VSCROLL = 0x0115;
    public const int SB_PAGEDOWN = 3;
    public const int SB_TOP = 6;

    // Best-effort app scroll. zui has no scrollTop API; FullPage uses repeated page-downs.
    public static void WheelAtClient(IntPtr h, int clientX, int clientY, int wheelDelta) {
        POINT p = new POINT(); p.X = clientX; p.Y = clientY;
        ClientToScreen(h, ref p);
        IntPtr lParam = (IntPtr)((p.Y << 16) | (p.X & 0xFFFF));
        IntPtr wParam = (IntPtr)((wheelDelta << 16));
        PostMessage(h, WM_MOUSEWHEEL, wParam, lParam);
    }

    public static void ScrollTop(IntPtr h) { PostMessage(h, WM_VSCROLL, (IntPtr)SB_TOP, IntPtr.Zero); }
    public static void PageDown(IntPtr h) { PostMessage(h, WM_VSCROLL, (IntPtr)SB_PAGEDOWN, IntPtr.Zero); }
}
"@
try { [VisualDiff]::SetProcessDPIAware() | Out-Null } catch { }

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$exeDir = Join-Path $root 'build\windows-x64'
$outDir = Join-Path $root 'build\visual-diff'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$edge = @(
    'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
    'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
    'C:\Program Files\Google\Chrome\Application\chrome.exe'
) | Where-Object { Test-Path $_ } | Select-Object -First 1

# name = exe suffix, html under example/, app wait ms. studio needs a long first frame.
# Flags are the browser-side extras a page needs. The reader is a scripted app: without local file
# access its shelf never fills and the comparison would be against a booting page. The wait has to
# cover reading the book folder, which the app does on startup.
$pages = @(
    [pscustomobject]@{ Name='hello';   Exe='zui_hello';   Html='example/tests/hello/hello.html';       WaitMs=2500 },
    [pscustomobject]@{ Name='studio';  Exe='zui_studio';  Html='example/tests/studio/studio.html';     WaitMs=40000 },
    [pscustomobject]@{ Name='flow';    Exe='zui_flow';    Html='example/tests/flow/flow.html';         WaitMs=2500 },
    [pscustomobject]@{ Name='style';   Exe='zui_style';   Html='example/tests/style/style.html';       WaitMs=2500 },
    [pscustomobject]@{ Name='text';    Exe='zui_text';    Html='example/tests/text/text.html';         WaitMs=2500 },
    [pscustomobject]@{ Name='motion';  Exe='zui_motion';  Html='example/tests/transition/transition.html'; WaitMs=2500 },
    [pscustomobject]@{ Name='checks';  Exe='zui_checks';  Html='example/tests/checks/checks.html';     WaitMs=2500 },
    [pscustomobject]@{ Name='reader';  Exe='zui_reader';  Html='example/applications/reader/reader.html'; WaitMs=9000;
                       Url='http://localhost:8765/reader.html'; Reset='reader-progress.txt'; WaitSelector='.books .card' }
    # The calendar is a module-scripted app too: serve `example/applications/calendar` on 8769
    # (`python -m http.server 8769`) before running - a file:// module import is blocked.
    [pscustomobject]@{ Name='calendar'; Exe='calendar\zui_calendar'; Html='example/applications/calendar/index.html'; WaitMs=6000;
                       Url='http://localhost:8769/index.html'; WaitSelector='.cell' }
)
if ($All -or $Page.Count -eq 0) { $selected = $pages } else {
    $selected = $pages | Where-Object { $Page -contains $_.Name }
    if (-not $selected) { throw "unknown page: $($Page -join ', '). known: $(($pages | ForEach-Object Name) -join ', ')" }
}

$baselinePath = Join-Path $outDir 'baseline.json'
$baseline = @{}
if (Test-Path $baselinePath) {
    (Get-Content $baselinePath -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $baseline[$_.Name] = [double]$_.Value }
}
$rows = @()

function Parse-ScrollTops([string]$spec) {
    if ([string]::IsNullOrWhiteSpace($spec)) { return @() }
    return @($spec.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' } | ForEach-Object { [double]$_ })
}

function Get-ScrollCaptureAsset {
    return (Join-Path (Join-Path (Join-Path $PSScriptRoot '..') 'assets') 'scroll-capture.html')
}

function ConvertTo-FileUrl([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    return ('file:///' + ($full -replace '\\', '/'))
}

function Get-ScrollCapturePlan([string]$htmlPath) {
    $tops = @(Parse-ScrollTops $ScrollTops)
    $mode = if ($ScrollAbsolute) { 'absolute' } else { 'fraction' }
    if ($FullPage) {
        $probe = Get-ScrollCaptureAsset
        $url = "{0}?target={1}&w={2}&h={3}&scrollId={4}&top=0&mode=absolute&meta=1&t={5}" -f `
            (ConvertTo-FileUrl $probe), [uri]::EscapeDataString((ConvertTo-FileUrl $htmlPath)), `
            $Width, $Height, [uri]::EscapeDataString($ScrollId), [DateTime]::Now.Ticks
        $profileDir = Join-Path $env:TEMP 'zui-visual-diff-scroll-meta'
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $dom = (& $edge --headless=new --disable-gpu --allow-file-access-from-files --virtual-time-budget=8000 `
                "--user-data-dir=$profileDir" --dump-dom $url 2>$null | Out-String)
        } finally {
            $ErrorActionPreference = $previous
        }
        $maxScroll = 0.0
        if ($dom -match 'meta\|scroll\|([0-9.]+)\|([0-9.]+)\|([0-9.]+)') {
            $maxScroll = [double]$Matches[3]
        }
        $plan = @()
        $idx = 0
        $y = 0.0
        do {
            $plan += [pscustomobject]@{ Index = $idx; Top = $y; Mode = 'absolute' }
            $idx++
            $y += [double]$Height
        } while ($y -lt ($maxScroll - 1.0))
        if ($plan.Count -eq 0) {
            $plan = @([pscustomobject]@{ Index = 0; Top = 0.0; Mode = 'absolute' })
        }
        return ,$plan
    }
    if ($tops.Count -eq 0) {
        return ,[pscustomobject]@{ Index = 0; Top = 0.0; Mode = 'absolute' }
    }
    $plan = @()
    for ($i = 0; $i -lt $tops.Count; $i++) {
        $plan += [pscustomobject]@{ Index = $i; Top = $tops[$i]; Mode = $mode }
    }
    return ,$plan
}

function Capture-WebScroll([string]$htmlPath, [string]$outPng, $step) {
    $probe = Get-ScrollCaptureAsset
    $budget = [Math]::Max(5000, [int]$WaitMs + 3000)
    $url = "{0}?target={1}&w={2}&h={3}&scrollId={4}&top={5}&mode={6}&t={7}" -f `
        (ConvertTo-FileUrl $probe), [uri]::EscapeDataString((ConvertTo-FileUrl $htmlPath)), `
        $Width, $Height, [uri]::EscapeDataString($ScrollId), $step.Top, $step.Mode, [DateTime]::Now.Ticks
    $profileDir = Join-Path $env:TEMP 'zui-visual-diff-scroll'
    Remove-Item $outPng -ErrorAction SilentlyContinue
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $edge --headless=new --disable-gpu --force-device-scale-factor=1 --allow-file-access-from-files `
            --virtual-time-budget=$budget --user-data-dir="$profileDir" `
            --window-size="$Width,$Height" --screenshot="$outPng" $url 2>$null | Out-Null
    } finally {
        $ErrorActionPreference = $previous
    }
    Start-Sleep -Milliseconds $ScrollWaitMs
    return (Test-Path $outPng)
}


function Find-UiProbe {
    $probe = Join-Path $exeDir 'zui_ui_probe.exe'
    if (Test-Path $probe) { return $probe }
    return $null
}

# App-side scrolled geometry via internal probe (Node.scroll_* + sync_world). Pixel strips may still be s0.
function Write-AppProbeBoxes([string]$htmlPath, [string]$pageName, $plan, [bool]$multi) {
    $probe = Find-UiProbe
    if ($probe -eq $null) {
        Write-Host '   note: zui_ui_probe.exe missing; skip scrolled app box dumps (cmake --build build/windows-x64 --target zui_ui_probe)' -ForegroundColor Yellow
        return $false
    }
    if (-not $multi) {
        $out = Join-Path $outDir ('{0}.app.boxes.txt' -f $pageName)
        $args = @($htmlPath, '--brief', '--size', ("{0}x{1}" -f $Width, $Height), '--out', $out)
        & $probe @args 2>$null | Out-Null
        return (Test-Path $out)
    }
    $okAny = $false
    for ($si = 0; $si -lt $plan.Count; $si++) {
        $step = $plan[$si]
        $out = Join-Path $outDir ('{0}.app.s{1}.boxes.txt' -f $pageName, $si)
        $args = @($htmlPath, '--brief', '--size', ("{0}x{1}" -f $Width, $Height), '--out', $out)
        if ($ScrollId -ne '') { $args += @('--scroll-id', $ScrollId) }
        # Prefer exact scroll-tops frame emission when we have a list; else single top from plan.
        if ($step.Mode -eq 'fraction') {
            $args += @('--scroll-top', ([string]$step.Top), '--scroll-fraction')
        } else {
            $args += @('--scroll-top', ([string]$step.Top))
        }
        & $probe @args 2>$null | Out-Null
        if (Test-Path $out) { $okAny = $true }
    }
    # Also emit a combined multi-frame file when ScrollTops is set (scroll| headers + brief).
    if (-not [string]::IsNullOrWhiteSpace($ScrollTops) -and -not $FullPage) {
        $combined = Join-Path $outDir ('{0}.app.scroll-tops.boxes.txt' -f $pageName)
        $args = @($htmlPath, '--brief', '--size', ("{0}x{1}" -f $Width, $Height),
                  '--scroll-tops', $ScrollTops, '--out', $combined)
        if ($ScrollId -ne '') { $args += @('--scroll-id', $ScrollId) }
        if (-not $ScrollAbsolute) { $args += '--scroll-fraction' }
        & $probe @args 2>$null | Out-Null
    }
    return $okAny
}

# NB: the loop variable must not be `$page` - that is the same variable as the [string[]]$Page
# parameter (PowerShell is case-insensitive), so each item would be coerced to a string.
foreach ($entry in $selected) {
    $html = Join-Path $root ($entry.Html -replace '/', [IO.Path]::DirectorySeparatorChar.ToString())
    if (-not (Test-Path $html)) { $html = Join-Path $root $entry.Html }
    $multi = $FullPage -or -not [string]::IsNullOrWhiteSpace($ScrollTops)
    $plan = @(Get-ScrollCapturePlan $html)
    if (-not $multi) {
        $plan = @([pscustomobject]@{ Index = 0; Top = 0.0; Mode = 'absolute' })
    }

    $line = [pscustomobject]@{ page = $entry.Name; app = ''; web = ''; diff = $null; note = '' }
    $appStrips = New-Object System.Collections.Generic.List[string]
    $webStrips = New-Object System.Collections.Generic.List[string]

    # ---- browser ----
    if (-not $AppOnly) {
        if ($edge -eq $null) { throw 'no msedge.exe/chrome.exe found for the browser side' }
        foreach ($step in $plan) {
            $suffix = if ($multi) { '.s{0}' -f $step.Index } else { '' }
            $webPng = Join-Path $outDir ('{0}.web{1}.png' -f $entry.Name, $suffix)
            if ($multi -or $FullPage -or $ScrollId -ne '') {
                $ok = Capture-WebScroll $html $webPng $step
            } else {
                Remove-Item $webPng -ErrorAction SilentlyContinue
                $profileDir = Join-Path $env:TEMP 'zui-visual-diff-profile'
                # The page's own state (the reader keeps progress and the theme in localStorage)
                # has to start empty, like the app's, or the two sides describe different pages.
                Remove-Item -Recurse -Force $profileDir -ErrorAction SilentlyContinue
                $extra = @()
                if ($null -ne $entry.PSObject.Properties['Flags']) {
                    $extra = @($entry.Flags -split '\s+' | Where-Object { $_ -ne '' })
                }
                # A scripted app has to be served over http: from file:// its own code cannot read
                # the folder it needs, and the page sits in its booting state forever.
                $target = if ($null -ne $entry.PSObject.Properties['Url']) {
                    $entry.Url
                } else {
                    "file:///" + $html.Replace('\', '/')
                }
                $previous = $ErrorActionPreference
                $ErrorActionPreference = 'Continue'
                try {
                    if ($null -ne $entry.PSObject.Properties['WaitSelector']) {
                        # A scripted page has to be waited for, not photographed at its load event:
                        # the reader fills its shelf from `fetch` and Edge's `--screenshot` (and
                        # `--virtual-time-budget`) came back with an empty, unbooted page. The CDP
                        # driver the difftest already uses can wait for a selector instead.
                        & python (Join-Path $PSScriptRoot 'capture.py') --url $target `
                            --out $webPng --width $Width --height $Height `
                            --selector "$($entry.WaitSelector)" `
                            --wait-ms ([Math]::Max(5000, [int]$entry.WaitMs + 3000))
                    } else {
                        & $edge --headless=new --disable-gpu --force-device-scale-factor=1 --user-data-dir="$profileDir" `
                            --window-size="$Width,$Height" $extra --screenshot="$webPng" `
                            $target 2>$null | Out-Null
                    }
                } finally {
                    $ErrorActionPreference = $previous
                }
                $ok = Test-Path $webPng
            }
            if (-not $ok) {
                $line.note = 'browser screenshot failed'
                break
            }
            [void]$webStrips.Add($webPng)
            try {
                $wb = New-Object System.Drawing.Bitmap $webPng
                $line.web = '{0}x{1}' -f $wb.Width, $wb.Height
                $wb.Dispose()
            } catch { }
        }
        if ($FullPage -and $webStrips.Count -gt 1) {
            $fullWeb = Join-Path $outDir ('{0}.web.full.png' -f $entry.Name)
            [VisualDiff]::StitchVertical([string[]]$webStrips.ToArray(), $fullWeb)
            $line.note = ($line.note + ' web-full').Trim()
        }
        # Keep legacy name as alias of s0 / only frame.
        if ($webStrips.Count -gt 0) {
            $legacyWeb = Join-Path $outDir ('{0}.web.png' -f $entry.Name)
            if ((Resolve-Path $webStrips[0]).Path -ne (Join-Path $outDir (Split-Path $legacyWeb -Leaf))) {
                Copy-Item $webStrips[0] $legacyWeb -Force
            } elseif (-not (Test-Path $legacyWeb)) {
                Copy-Item $webStrips[0] $legacyWeb -Force
            }
            # When single-frame non-multi, webPng already IS legacy name.
            if (-not $multi) {
                # already written as .web.png
            } else {
                Copy-Item $webStrips[0] $legacyWeb -Force
            }
        }
    }

    # ---- app ----
    if (-not $WebOnly) {
        $exe = Join-Path $exeDir ($entry.Exe + '.exe')
        if (-not (Test-Path $exe)) {
            $line.note = ("missing {0}.exe (build first)" -f $entry.Exe)
            $rows += $line
            continue
        }
        # A page that keeps state on disk starts from whatever the last run left behind; the
        # reader sorts its shelf by a progress file next to the binary, and comparing that against
        # a fresh browser turned a 2% difference into a 30% one.
        if ($null -ne $entry.PSObject.Properties['Reset']) {
            foreach ($stale in @($entry.Reset -split ',')) {
                Remove-Item -Recurse -Force (Join-Path $exeDir $stale.Trim()) -ErrorAction SilentlyContinue
            }
        }
        $proc = Start-Process -FilePath $exe -ArgumentList $html -WorkingDirectory $exeDir -PassThru
        $wait = if ($WaitMs -gt 0) { $WaitMs } else { $entry.WaitMs }
        Start-Sleep -Milliseconds $wait
        $h = [IntPtr]::Zero
        for ($i = 0; $i -lt 20; $i++) {
            $proc.Refresh()
            if ($proc.HasExited) { break }
            $h = $proc.MainWindowHandle
            if ($h -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 250
        }
        if ($h -eq [IntPtr]::Zero) {
            if (-not $proc.HasExited) { $proc | Stop-Process -Force }
            $line.note = 'window did not appear'
            $rows += $line
            continue
        }
        [VisualDiff]::SetForegroundWindow($h) | Out-Null
        [VisualDiff]::FitClient($h, $Width, $Height)
        Start-Sleep -Milliseconds 800

        if ($multi) {
            # PNG limitation: no public App scrollTop. PageDown for -FullPage; -ScrollTops PNG stays s0.
            # Geometry alignment uses zui_ui_probe (Write-AppProbeBoxes) for every scroll step.
            [VisualDiff]::ScrollTop($h)
            Start-Sleep -Milliseconds $ScrollWaitMs
            for ($si = 0; $si -lt $plan.Count; $si++) {
                $appPng = Join-Path $outDir ('{0}.app.s{1}.png' -f $entry.Name, $si)
                $line.app = [VisualDiff]::CaptureClient($h, $appPng)
                [void]$appStrips.Add($appPng)
                if ($si + 1 -ge $plan.Count) { break }
                if ($FullPage) {
                    [VisualDiff]::PageDown($h)
                    Start-Sleep -Milliseconds $ScrollWaitMs
                } else {
                    # Cannot match arbitrary scroll tops; keep capturing the same frame once and stop.
                    $line.note = ($line.note + ' app-png=s0-only geometry=zui_ui_probe').Trim()
                    break
                }
            }
            if ($FullPage -and $appStrips.Count -gt 1) {
                $fullApp = Join-Path $outDir ('{0}.app.full.png' -f $entry.Name)
                [VisualDiff]::StitchVertical([string[]]$appStrips.ToArray(), $fullApp)
                $line.note = ($line.note + ' app-full').Trim()
            }
            Copy-Item $appStrips[0] (Join-Path $outDir ('{0}.app.png' -f $entry.Name)) -Force
        } else {
            $appPng = Join-Path $outDir ('{0}.app.png' -f $entry.Name)
            $line.app = [VisualDiff]::CaptureClient($h, $appPng)
            [void]$appStrips.Add($appPng)
        }
        if (-not $proc.HasExited) { $proc | Stop-Process -Force }
    }


    # ---- app geometry (zui_ui_probe; preferred for scrolled box-compare alignment) ----
    if (-not $WebOnly) {
        $wantProbe = $multi -or ($ScrollId -ne '') -or (-not [string]::IsNullOrWhiteSpace($ScrollTops)) -or $FullPage
        if ($wantProbe) {
            if (Write-AppProbeBoxes $html $entry.Name $plan $multi) {
                $line.note = ($line.note + ' app-boxes=zui_ui_probe').Trim()
            }
        }
    }

    # ---- compare ----
    if ($FullPage) {
        $a = Join-Path $outDir ('{0}.app.full.png' -f $entry.Name)
        $w = Join-Path $outDir ('{0}.web.full.png' -f $entry.Name)
        if (-not (Test-Path $a)) { $a = Join-Path $outDir ('{0}.app.png' -f $entry.Name) }
        if (-not (Test-Path $w)) { $w = Join-Path $outDir ('{0}.web.png' -f $entry.Name) }
        $d = Join-Path $outDir ('{0}.diff.full.png' -f $entry.Name)
        if ((Test-Path $a) -and (Test-Path $w)) {
            $line.diff = [VisualDiff]::Compare($a, $w, $d, $Tol)
        }
    } elseif ($multi) {
        $parts = @()
        $n = [Math]::Min($appStrips.Count, $webStrips.Count)
        for ($si = 0; $si -lt $n; $si++) {
            $d = Join-Path $outDir ('{0}.diff.s{1}.png' -f $entry.Name, $si)
            $parts += [VisualDiff]::Compare($appStrips[$si], $webStrips[$si], $d, $Tol)
        }
        if ($parts.Count -gt 0) { $line.diff = ($parts -join ' | ') }
        # Also write legacy single diff of s0.
        if ($n -gt 0) {
            $d0 = Join-Path $outDir ('{0}.diff.png' -f $entry.Name)
            [VisualDiff]::Compare($appStrips[0], $webStrips[0], $d0, $Tol) | Out-Null
        }
    } else {
        $appPng = Join-Path $outDir ('{0}.app.png' -f $entry.Name)
        $webPng = Join-Path $outDir ('{0}.web.png' -f $entry.Name)
        $diffPng = Join-Path $outDir ('{0}.diff.png' -f $entry.Name)
        if ((Test-Path $appPng) -and (Test-Path $webPng)) {
            $line.diff = [VisualDiff]::Compare($appPng, $webPng, $diffPng, $Tol)
        }
    }
    $rows += $line
}

Write-Output ''
Write-Output ('{0,-8} {1,-9} {2,-10} {3}' -f 'page', 'app', 'web', 'result')
foreach ($r in $rows) {
    Write-Output ('{0,-8} {1,-9} {2,-10} {3} {4}' -f $r.page, $r.app, $r.web, $r.diff, $r.note)
}

$current = @{}
foreach ($r in $rows) {
    if ($r.diff -ne $null -and $r.diff -match 'diff=([0-9.]+)%') { $current[$r.page] = [double]$Matches[1] }
}

if ($UpdateBaseline) {
    $current | ConvertTo-Json | Set-Content -Path $baselinePath -Encoding UTF8
    Write-Output "baseline updated: $baselinePath"
} elseif ($baseline.Count -gt 0 -and $current.Count -gt 0) {
    Write-Output ''
    $worse = @()
    foreach ($k in $current.Keys) {
        if (-not $baseline.ContainsKey($k)) { continue }
        $delta = $current[$k] - $baseline[$k]
        $flag = if ($delta -gt $FailDelta) { $worse += $k; 'WORSE' } elseif ($delta -lt -$FailDelta) { 'better' } else { 'ok' }
        Write-Output ('{0,-8} {1,6:F2}% -> {2,6:F2}%  ({3}{4:F2})' -f $k, $baseline[$k], $current[$k], $(if ($delta -ge 0) { '+' } else { '' }), $delta)
    }
    if ($worse.Count -gt 0) { Write-Output ("regression on: " + ($worse -join ', ')); exit 1 }
}
Write-Output ""
Write-Output "artifacts: $outDir"
