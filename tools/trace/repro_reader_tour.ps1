# Measure the four interactions the layout plan's section 4.7 asks for - open the contents tray,
# switch it to 搜索, switch it back to 目录, change the font size - on the real reader page, in
# Release, min of @Runs passes.
#
# The reader's `ZUI_READER_AUTOTOUR` harness performs those four state changes through the same
# lambdas the taps go through, prints `[reader] tour <step>` to stderr before each one, and quits at
# the end. `ZUI_PERF=1 ZUI_PERF_MS=1` makes every frame print the `[perf]` line, which carries
# `cascade=` (cascade self time) and `laid=` (`frame.laid`); splitting that stream at the step
# markers is what says which frames belong to which interaction.
#
# Usage:
#   pwsh tools/trace/repro_reader_tour.ps1
#   pwsh tools/trace/repro_reader_tour.ps1 -Runs 5 -Settle 45
#   pwsh tools/trace/repro_reader_tour.ps1 -Categories app,engine,layout.phase,layout.algo
#   pwsh tools/trace/repro_reader_tour.ps1 -NoTrace
#
# Output: build/trace/reader_tour_<n>.pftrace + .stderr.txt per pass, and a summary table.
#
# `-Categories` widens the recorded set: `layout.phase` carries the per-box `find`/`store`/`shape`
# zones, `engine` carries `relower`/`box.*`/`hit_test`. `-NoTrace` runs the same tour with the
# `[perf]` line only - that is the configuration a before/after comparison should use, because the
# record-time cost of the per-box zones (~7ms on the reader's contents frame) disappears with it.

param(
    [string]$Config = "windows-x64-release",
    [int]$Runs = 3,
    # Settle window in frames between two steps. A slow frame blocks the loop, so the count is only
    # a lower bound on the quiet frames each step gets.
    [int]$Settle = 30,
    [string]$OutDir = "D:\code\zui\build\trace",
    [string]$Categories = "app,engine",
    [switch]$NoTrace
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir | Out-Null
}

$exe = Join-Path $repoRoot (Join-Path "build" (Join-Path $Config "zui_reader.exe"))
if (-not (Test-Path $exe)) {
    throw "executable not found: $exe (build it first, or pass -Config)"
}

$steps = @("toc_open", "tab_find", "tab_toc", "font_large")
$passes = @()

for ($run = 1; $run -le $Runs; $run++) {
    $trace = Join-Path $OutDir "reader_tour_$run.pftrace"
    $log = Join-Path $OutDir "reader_tour_$run.stderr.txt"
    if (Test-Path $trace) { Remove-Item -LiteralPath $trace -Force }
    if (Test-Path $log) { Remove-Item -LiteralPath $log -Force }

    # The trace is written when the process exits, which the harness's EXIT flag makes it do.
    if (-not $NoTrace) {
        $env:ZUI_TRACE = $trace
        if (-not $env:ZUI_TRACE_BUFFER_KB) { $env:ZUI_TRACE_BUFFER_KB = "65536" }
        $env:ZUI_TRACE_CATEGORIES = $Categories
    }
    $env:ZUI_PERF = "1"
    $env:ZUI_READER_AUTOTOUR = "$Settle"
    $env:ZUI_READER_AUTOTOUR_EXIT = "1"
    $env:ZUI_PERF_MS = "1"

    if (-not $NoTrace) { Write-Host "pass $run -> $trace" }
    # Launch directly, not through run_app.ps1: the frame counters and the step markers are on the
    # child's stderr, and only a redirect captures that (`Start-Process` children inherit the
    # console, so PowerShell's own redirection never sees it).
    $proc = Start-Process -FilePath $exe -WorkingDirectory $repoRoot -PassThru -NoNewWindow `
        -RedirectStandardError $log -RedirectStandardOutput "$log.stdout.txt"
    $proc.WaitForExit() | Out-Null
    if (-not $proc.HasExited) { $proc.Kill() }

    Remove-Item Env:ZUI_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_TRACE_CATEGORIES -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_PERF -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_READER_AUTOTOUR -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_READER_AUTOTOUR_EXIT -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_PERF_MS -ErrorAction SilentlyContinue

    $lines = Get-Content -LiteralPath $log
    if (-not ($lines | Where-Object { $_ -match '\[reader\] tour done' })) {
        Write-Warning "pass $run never reached the end of the tour - its numbers are not usable"
    }
    # Bucket the frame lines by the step marker that precedes them. Frames before the first marker
    # (startup, shelf, book parse) land in `shelf`.
    $bucket = "shelf"
    $frames = @{}
    foreach ($line in $lines) {
        if ($line -match '\[reader\] tour (\w+)') {
            $bucket = $Matches[1]
            if (-not $frames.ContainsKey($bucket)) { $frames[$bucket] = @() }
            continue
        }
        if ($line -match '\[perf\] \S+ (static|resize) total=([0-9.]+)ms work=([0-9.]+)ms cascade=([0-9.]+) layout=([0-9.]+) paint=([0-9.]+) present=([0-9.]+).*?\blaid=(\d+)') {
            if (-not $frames.ContainsKey($bucket)) { $frames[$bucket] = @() }
            $frames[$bucket] += [pscustomobject]@{
                Total   = [double]$Matches[2]
                Work    = [double]$Matches[3]
                Cascade = [double]$Matches[4]
                Layout  = [double]$Matches[5]
                Paint   = [double]$Matches[6]
                Present = [double]$Matches[7]
                Laid    = [int]$Matches[8]
            }
        }
    }
    $passes += , @{ Run = $run; Frames = $frames }
}

# min-of-N per step. The plan asks for `cascade` self time and `frame.laid`; the peak *frame* is
# what a reader feels, so its total and its layout stage come along.
$summary = @()
foreach ($name in @("shelf") + $steps) {
    $peaks = @()
    foreach ($pass in $passes) {
        if (-not $pass.Frames.ContainsKey($name)) { continue }
        $rows = $pass.Frames[$name]
        if ($rows.Count -eq 0) { continue }
        $peak = $rows | Sort-Object Total -Descending | Select-Object -First 1
        $peaks += [pscustomobject]@{
            Run         = $pass.Run
            Frames      = $rows.Count
            PeakTotal   = $peak.Total
            PeakLayout  = $peak.Layout
            PeakCascade = $peak.Cascade
            PeakFrameLaid = $peak.Laid
            MaxLaid     = ($rows | Measure-Object -Property Laid -Maximum).Maximum
            MaxCascade  = ($rows | Measure-Object -Property Cascade -Maximum).Maximum
        }
    }
    if ($peaks.Count -eq 0) { continue }
    $summary += [pscustomobject]@{
        Step       = $name
        MinTotal   = ($peaks | Measure-Object -Property PeakTotal -Minimum).Minimum
        MinLayout  = ($peaks | Measure-Object -Property PeakLayout -Minimum).Minimum
        MinCascade = ($peaks | Measure-Object -Property PeakCascade -Minimum).Minimum
        MinMaxCascade = ($peaks | Measure-Object -Property MaxCascade -Minimum).Minimum
        MinMaxLaid = ($peaks | Measure-Object -Property MaxLaid -Minimum).Minimum
        Passes     = $peaks
    }
}

Write-Host ""
Write-Host "min of $Runs passes (peak frame per step):"
$summary | Format-Table Step, MinTotal, MinLayout, MinCascade, MinMaxCascade, MinMaxLaid -AutoSize
Write-Host ""
Write-Host "per pass:"
foreach ($row in $summary) {
    foreach ($p in $row.Passes) {
        Write-Host ("  {0,-12} run {1}  frames={2,-4} peak total={3,8:N2} layout={4,7:N2} cascade={5,6:N2} laid={6,-6} max cascade={7,6:N2} max laid={8}" -f
            $row.Step, $p.Run, $p.Frames, $p.PeakTotal, $p.PeakLayout, $p.PeakCascade, $p.PeakFrameLaid, $p.MaxCascade, $p.MaxLaid)
    }
}
