# Box-by-box geometry comparison, the second half of the ui-visual-diff skill.
#
#   powershell -File scripts\box-compare.ps1 -Page hello
#   powershell -File scripts\box-compare.ps1 -All -Tol 1.5
#
# Dumps every box our layout engine produced (`zui_ui_probe <page> --brief`, fallback zui_dump) and every
# element rect a real browser produced for the same page at the same client size
# (assets/box-probe.html in headless Edge), joins them by id / tag+class, and
# prints the per-box delta. Where visual-diff.ps1 tells you *that* a page differs,
# this tells you *which box* differs and by how much.
#
# Default app side is STATIC markup via zui_ui_probe (fallback zui_dump; no script / model bind).
# For dynamic pages use -AfterBind with -WaitMs (browser) and optionally
# -AppDumpPath / -AppDumpCommand (pre-generated brief dump after bind).
#
# Exit code 1 when a joined box moves more than -Tol, or when one side has a box
# the other side does not. Missing boxes are reported separately so you can tell
# "we never created it" from "we placed it wrong".

[CmdletBinding()]
param(
    [string[]]$Page = @(),
    [switch]$All,
    [string]$Html = '',
    [int]$Width = 1100,
    [int]$Height = 720,
    [double]$Tol = 1.0,
    [int]$Top = 25,
    [switch]$NoFail,
    # After-bind / dynamic pages (opt-in). Default zui_ui_probe/zui_dump is static markup only.
    [switch]$AfterBind,
    [int]$WaitMs = 0,
    [string]$AppDumpPath = '',
    [string]$AppDumpCommand = '',
    # Scroll geometry via zui_ui_probe (passthrough).
    [string]$ScrollId = '',
    [string]$ScrollTop = '',
    [switch]$ScrollFraction
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$exeDir = Join-Path $root 'build\windows-x64'
$outDir = Join-Path $root 'build\box-compare'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# html path per page, same set the pixel comparison knows about
$pages = [ordered]@{
    hello  = 'example/tests/hello/hello.html'
    checks = 'example/tests/checks/checks.html'
    style  = 'example/tests/style/style.html'
    text   = 'example/tests/text/text.html'
    flow   = 'example/tests/flow/flow.html'
    motion = 'example/tests/transition/transition.html'
    studio = 'example/tests/studio/studio.html'
    # Static markup only: the month grid and the agenda list are created by the app's C++, so
    # only the shell boxes (topbar / board / aside) are meaningful here.
    calendar = 'example/applications/calendar/index.html'
}

function Find-Browser {
    $candidates = @(
        'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
        'C:\Program Files\Google\Chrome\Application\chrome.exe',
        'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe'
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    throw 'No Edge or Chrome found; install one or edit $candidates in box-compare.ps1.'
}

function To-FileUrl([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    return 'file:///' + ($full -replace '\\', '/' -replace ' ', '%20')
}

function Read-AppRows([string]$html, [int]$w, [int]$h) {
    # After-bind extension point: prefer an explicit dump file or command when
    # the page's content is produced by script/model bind (probe/dump is static).
    $raw = $null
    if ($AppDumpPath -ne '' -and (Test-Path $AppDumpPath)) {
        Write-Host ("   AfterBind: reading app dump from {0}" -f $AppDumpPath)
        $raw = Get-Content -Path $AppDumpPath
    } elseif ($AppDumpCommand -ne '') {
        Write-Host ("   AfterBind: running AppDumpCommand")
        $raw = Invoke-Expression $AppDumpCommand 2>&1
    } else {
        if ($AfterBind) {
            Write-Host '   AfterBind: no -AppDumpPath/-AppDumpCommand; falling back to static probe/dump (bind not applied).' -ForegroundColor Yellow
        }
        $probe = Join-Path $exeDir 'zui_ui_probe.exe'
        $dump = Join-Path $exeDir 'zui_dump.exe'
        if (Test-Path $probe) {
            $exe = $probe
            $tool = 'zui_ui_probe'
        } elseif (Test-Path $dump) {
            $exe = $dump
            $tool = 'zui_dump'
        } else {
            throw "zui_ui_probe.exe / zui_dump.exe not built: cmake --build build/windows-x64 --target zui_ui_probe"
        }
        $full = Join-Path $root $html
        $dumpArgs = @($full, '--brief', '--size', ("{0}x{1}" -f $w, $h))
        if ($WaitMs -gt 0) { $dumpArgs += @('--wait-ms', "$WaitMs") }
        if ($tool -eq 'zui_ui_probe') {
            if ($ScrollId -ne '') { $dumpArgs += @('--scroll-id', $ScrollId) }
            if ($ScrollTop -ne '') {
                $dumpArgs += @('--scroll-top', $ScrollTop)
                if ($ScrollFraction) { $dumpArgs += '--scroll-fraction' }
            }
        } elseif ($ScrollId -ne '' -or $ScrollTop -ne '') {
            Write-Host '   ScrollId/ScrollTop ignored: zui_ui_probe.exe missing; using zui_dump.' -ForegroundColor Yellow
        }
        Write-Host ("   app geometry via {0}" -f $tool)
        $raw = & $exe @dumpArgs 2>&1
    }
    $rows = New-Object System.Collections.ArrayList
    foreach ($line in $raw) {
        $text = [string]$line
        if (-not $text.StartsWith('box|')) { continue }
        $p = $text.Split('|')
        if ($p.Count -lt 10) { continue }
        # anonymous boxes have no element behind them, so the browser has nothing
        # to join them with; they are still counted and reported as ONLY-ZUI
        [void]$rows.Add([pscustomobject]@{
                tag = $p[1]; id = $p[2]; cls = $p[3]
                x = [double]$p[4]; y = [double]$p[5]; w = [double]$p[6]; h = [double]$p[7]
                cw = [double]$p[8]; ch = [double]$p[9]
            })
    }
    return $rows
}

function Read-DomRows([string]$html, [int]$w, [int]$h) {
    $edge = Find-Browser
    $probe = To-FileUrl (Join-Path $PSScriptRoot '..\assets\box-probe.html')
    # ${...} braces are required: `$probe?target=` would be read as one variable name
    $frame = To-FileUrl (Join-Path $root $html)
    # the last parameter defeats the profile's cache of the probe page itself
    $url = "${probe}?target=${frame}&w=${w}&h=${h}&t=$([DateTime]::Now.Ticks)&wait=$WaitMs"
    $edgeArgs = @(
        '--headless=new', '--disable-gpu', '--allow-file-access-from-files',
        "--window-size=$w,$h", ("--virtual-time-budget={0}" -f [Math]::Max(5000, $WaitMs + 3000)),
        "--user-data-dir=$env:TEMP\zui-box-probe-profile", '--dump-dom', $url
    )
    # native stderr is not a PowerShell error; keep the preference out of the way
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $dom = (& $edge @edgeArgs 2>$null | Out-String)
    } finally {
        $ErrorActionPreference = $previous
    }
    # Read only the probe's own <pre>, so neither the page markup nor the probe's
    # script source can be mistaken for a row.
    $open = '<pre id="out">'
    $begin = $dom.IndexOf($open)
    if ($begin -lt 0) { throw 'the probe page produced no output (see the raw dump above)' }
    $begin += $open.Length
    $end = $dom.IndexOf('</pre>', $begin)
    if ($end -lt 0) { $end = $dom.Length }
    $data = $dom.Substring($begin, $end - $begin)

    $rows = New-Object System.Collections.ArrayList
    $meta = $null
    foreach ($line in ($data -split "`n")) {
        $row = $line.Trim()
        if ($row.StartsWith('ERR|')) { throw "probe failed: $row" }
        if ($row.StartsWith('meta|scroll|')) {
            $m = $row.Split('|')
            $meta = [pscustomobject]@{
                inner_w = [double]$m[2]; inner_h = [double]$m[3]
                scroll_h = [double]$m[4]; client_w = [double]$m[5]
            }
            continue
        }
        if ($row.StartsWith('meta|elements|')) {
            $counts = $row.Split('|')
            Write-Host ("   probe walked {0} elements, {1}..{2}" -f $counts[2], $counts[3], $counts[4])
            continue
        }
        if (-not $row.StartsWith('dom|')) { continue }
        $p = $row.Split('|')
        if ($p.Count -lt 8) { continue }
        [void]$rows.Add([pscustomobject]@{
                tag = $p[1]; id = $p[2]; cls = $p[3]
                x = [double]$p[4]; y = [double]$p[5]; w = [double]$p[6]; h = [double]$p[7]
            })
    }
    if ($rows.Count -eq 0) {
        $head = if ($dom.Length -gt 500) { $dom.Substring(0, 500) } else { $dom }
        Write-Host "   the browser dump held no rows; requested $url" -ForegroundColor Red
        Write-Host "   raw output follows:"
        Write-Host "   $head"
    }
    return [pscustomobject]@{ rows = $rows; meta = $meta }
}

function Get-JoinKey($row) {
    if ($row.id -ne '') { return "#$($row.id)" }
    if ($row.cls -ne '') { return "$($row.tag).$($row.cls -replace ' ', '.')" }
    return [string]$row.tag
}

# Duplicate keys (three `span.nav`, ten `td`) are numbered in document order, so
# the Nth one on our side is compared with the Nth one on the browser side.
function Group-ByKey($rows) {
    $map = @{}
    foreach ($r in $rows) {
        $base = Get-JoinKey $r
        if ($map.ContainsKey($base)) {
            $seen = $map[$base]
        } else {
            $seen = New-Object System.Collections.ArrayList
            $map[$base] = $seen
        }
        $index = $seen.Count
        [void]$seen.Add($r)
        $key = if ($index -eq 0) { $base } else { "$base~$index" }
        $r | Add-Member -NotePropertyName Key -NotePropertyValue $key -Force
    }
    return $map
}

function Format-Rect($r) {
    return ('{0},{1},{2},{3}' -f $r.x, $r.y, $r.w, $r.h)
}

function Format-Delta([double]$v) {
    $s = '{0:0.0}' -f $v
    if ($v -gt 0) { return "+$s" }
    return $s
}

function Compare-One([string]$name, [string]$html) {
    Write-Host ''
    Write-Host ("== {0}  {1}x{2}  tol={3}" -f $name, $Width, $Height, $Tol) -ForegroundColor Cyan

    $appRows = Read-AppRows $html $Width $Height
    $domResult = Read-DomRows $html $Width $Height
    $domRows = $domResult.rows

    $app = Group-ByKey $appRows
    $dom = Group-ByKey $domRows

    if ($domResult.meta -ne $null) {
        $bar = $domResult.meta.inner_w - $domResult.meta.client_w
        Write-Host ("   browser viewport {0}x{1}  client width {2}  document {3}px tall" -f `
                $domResult.meta.inner_w, $domResult.meta.inner_h, $domResult.meta.client_w, $domResult.meta.scroll_h)
        if ($bar -gt 0) {
            Write-Host ("   NOTE: the browser reserved {0}px for a page scrollbar (we reserve 11px)" -f $bar) -ForegroundColor Yellow
        }
    }

    $diffs = New-Object System.Collections.ArrayList
    $onlyDom = New-Object System.Collections.ArrayList
    $onlyApp = New-Object System.Collections.ArrayList
    $same = 0

    foreach ($key in $dom.Keys) {
        if (-not $app.ContainsKey($key)) {
            foreach ($r in $dom[$key]) { [void]$onlyDom.Add([pscustomobject]@{ Key = $key; Rect = (Format-Rect $r) }) }
            continue
        }
        $a = $app[$key]
        foreach ($d in $dom[$key]) {
            if ($a.Count -eq 0) {
                [void]$onlyDom.Add([pscustomobject]@{ Key = $key; Rect = (Format-Rect $d) })
                continue
            }
            $m = $a[0]
            $a.RemoveAt(0)
            $dx = $m.x - $d.x; $dy = $m.y - $d.y; $dw = $m.w - $d.w; $dh = $m.h - $d.h
            $worst = ($dx, $dy, $dw, $dh | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum
            if ($worst -le $Tol) {
                $same++
            } else {
                [void]$diffs.Add([pscustomobject]@{
                        Key = $key; Dom = (Format-Rect $d); Zui = (Format-Rect $m)
                        Dx = $dx; Dy = $dy; Dw = $dw; Dh = $dh; Worst = $worst
                    })
            }
        }
    }
    foreach ($key in $app.Keys) {
        foreach ($r in $app[$key]) {
            # anon / #text are our text containers; the browser has no element for
            # them (the pixel comparison covers the glyphs themselves)
            if ($r.tag -eq 'anon' -or $r.tag -eq '#text') { continue }
            [void]$onlyApp.Add([pscustomobject]@{ Key = $key; Rect = (Format-Rect $r) })
        }
    }

    $sorted = $diffs | Sort-Object -Property Worst -Descending
    if ($sorted.Count -gt 0) {
        Write-Host '   key                          browser (x,y,w,h)          zui (x,y,w,h)              dx     dy     dw     dh'
        $shown = 0
        foreach ($d in $sorted) {
            if ($shown -ge $Top) { break }
            $shown++
            Write-Host ('   {0,-28} {1,-26} {2,-26} {3,6} {4,6} {5,6} {6,6}' -f `
                    $d.Key, $d.Dom, $d.Zui, (Format-Delta $d.Dx), (Format-Delta $d.Dy), (Format-Delta $d.Dw), (Format-Delta $d.Dh))
        }
        if ($sorted.Count -gt $shown) {
            Write-Host ("   ... {0} more differing boxes in diff.txt" -f ($sorted.Count - $shown))
        }
    }

    if ($onlyDom.Count -gt 0) {
        Write-Host ("   ONLY-BROWSER ({0}) - the browser created a box we never made:" -f $onlyDom.Count) -ForegroundColor Yellow
        $shown = 0
        foreach ($o in $onlyDom) {
            if ($shown -ge 8) { Write-Host ("   ... {0} more" -f ($onlyDom.Count - $shown)); break }
            $shown++
            Write-Host ('   {0,-28} {1}' -f $o.Key, $o.Rect)
        }
    }
    if ($onlyApp.Count -gt 0) {
        Write-Host ("   ONLY-ZUI ({0}) - we made a box the browser has no element for:" -f $onlyApp.Count)
        $shown = 0
        foreach ($o in $onlyApp) {
            if ($shown -ge 8) { Write-Host ("   ... {0} more" -f ($onlyApp.Count - $shown)); break }
            $shown++
            Write-Host ('   {0,-28} {1}' -f $o.Key, $o.Rect)
        }
    }

    $report = @()
    $report += "== $name  ${Width}x${Height}  tol=$Tol"
    $report += "matched-within-tol: $same   differing: $($diffs.Count)   only-browser: $($onlyDom.Count)   only-zui: $($onlyApp.Count)"
    $report += ''
    $report += 'differing boxes, largest first:'
    $report += ($sorted | ForEach-Object {
            '{0,-28} browser={1,-26} zui={2,-26} dx={3} dy={4} dw={5} dh={6}' -f `
                $_.Key, $_.Dom, $_.Zui, (Format-Delta $_.Dx), (Format-Delta $_.Dy), (Format-Delta $_.Dw), (Format-Delta $_.Dh)
        })
    $report += ''
    $report += 'only-browser:'
    $report += ($onlyDom | Sort-Object Key | ForEach-Object { '{0,-28} {1}' -f $_.Key, $_.Rect })
    $report += ''
    $report += 'only-zui:'
    $report += ($onlyApp | Sort-Object Key | ForEach-Object { '{0,-28} {1}' -f $_.Key, $_.Rect })
    $report -join "`r`n" | Set-Content -Path (Join-Path $outDir "$name.diff.txt") -Encoding UTF8
    ($appRows | ForEach-Object { 'box|{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f $_.tag, $_.id, $_.cls, $_.x, $_.y, $_.w, $_.h }) |
        Set-Content -Path (Join-Path $outDir "$name.app.txt") -Encoding UTF8
    ($domRows | ForEach-Object { 'dom|{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f $_.tag, $_.id, $_.cls, $_.x, $_.y, $_.w, $_.h }) |
        Set-Content -Path (Join-Path $outDir "$name.dom.txt") -Encoding UTF8

    $fail = ($diffs.Count -gt 0) -or ($onlyDom.Count -gt 0)
    Write-Host ("   -> {0}   ({1} matched, {2} differing, {3} only-browser, {4} only-zui)" -f `
        $(if ($fail) { 'DIFF' } else { 'OK' }), $same, $diffs.Count, $onlyDom.Count, $onlyApp.Count) -ForegroundColor `
    $(if ($fail) { 'Red' } else { 'Green' })
    return $fail
}

$targets = [ordered]@{}
if ($Html -ne '') {
    $targets['custom'] = $Html
} else {
    if ($Page.Count -eq 0 -and -not $All) { $Page = @('hello') }
    if ($Page.Count -gt 0) {
        foreach ($name in ($Page -join ',').Split(',')) {
            $n = $name.Trim()
            if ($n -eq '') { continue }
            if ($pages.Contains($n)) {
                $targets[$n] = $pages[$n]
            } elseif (Test-Path (Join-Path $root $n)) {
                $targets[[System.IO.Path]::GetFileNameWithoutExtension($n)] = $n
            } else {
                Write-Host "unknown page '$n'; known: $($pages.Keys -join ', ')" -ForegroundColor Yellow
            }
        }
    }
    if ($All) {
        foreach ($k in $pages.Keys) { $targets[$k] = $pages[$k] }
    }
}

$anyFail = $false
foreach ($name in $targets.Keys) {
    if (Compare-One $name $targets[$name]) { $anyFail = $true }
}

Write-Host ''
if ($anyFail) {
    Write-Host "geometry differs from the browser (tol=$Tol). See build\box-compare\*.diff.txt" -ForegroundColor Red
    if (-not $NoFail) { exit 1 }
} else {
    Write-Host "geometry matches the browser within $Tol px" -ForegroundColor Green
}
