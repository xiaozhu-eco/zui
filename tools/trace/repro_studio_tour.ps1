# Measure zui_studio's data grid - the ~10k-cell table - the way a click on "Shuffle" hits it.
#
# The studio's own harness (`ZUI_STUDIO_AUTOTOUR=<frames>`, inert unless set) performs the four
# interactions that touch the grid - shuffle, scroll down, scroll up, switch theme, switch page -
# through the same state changes the handlers make, prints `[studio] tour <step>` to stderr before
# each one, and quits at the end so the trace is flushed. `ZUI_STUDIO_GRID=80x80` pins the grid size:
# without it every run randomizes 50x50..100x100 and two runs are not comparable.
#
# Usage:
#   pwsh tools/trace/repro_studio_tour.ps1
#   pwsh tools/trace/repro_studio_tour.ps1 -Runs 3 -Grid 80x80 -Settle 25
#   pwsh tools/trace/repro_studio_tour.ps1 -TraceDir build/trace        # write .pftrace per pass
#
# Output: a per-step peak-frame table (min over passes) plus the `[perf]` tail of each step.

param(
    [string]$Config = "windows-x64-release",
    [int]$Runs = 3,
    # Frames of quiet between two steps. A slow frame blocks the loop, so it is a lower bound.
    [int]$Settle = 20,
    # Fixed grid size, or "" for the page's own random 50x50..100x100.
    [string]$Grid = "80x80",
    [string]$OutDir = "D:\code\zui\build\trace\studio",
    [string]$TraceDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $repoRoot (Join-Path "build" (Join-Path $Config "zui_studio.exe"))
if (-not (Test-Path $exe)) { throw "executable not found: $exe (build it first, or pass -Config)" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

$steps = @("shuffle", "scroll_mid", "scroll_top", "theme_dawn", "theme_night", "page_log",
           "page_inspect")
$passes = @()

for ($run = 1; $run -le $Runs; $run++) {
    $log = Join-Path $OutDir "studio_tour_$run.stderr.txt"
    if (Test-Path $log) { Remove-Item -LiteralPath $log -Force }

    if ($TraceDir -ne "") {
        if (-not (Test-Path $TraceDir)) { New-Item -ItemType Directory -Path $TraceDir | Out-Null }
        $env:ZUI_TRACE = Join-Path $TraceDir "studio_tour_$run.pftrace"
        $env:ZUI_TRACE_CATEGORIES = "app,engine,layout.phase,layout.algo"
        $env:ZUI_TRACE_BUFFER_KB = "262144"
    }
    $env:ZUI_PERF = "1"
    $env:ZUI_PERF_MS = "1"
    $env:ZUI_STUDIO_AUTOTOUR = "$Settle"
    $env:ZUI_STUDIO_AUTOTOUR_EXIT = "1"
    if ($Grid -ne "") { $env:ZUI_STUDIO_GRID = $Grid }

    Write-Host "pass $run -> $log"
    $proc = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru `
        -NoNewWindow -RedirectStandardError $log -RedirectStandardOutput "$log.out.txt"
    $proc.WaitForExit() | Out-Null
    if (-not $proc.HasExited) { $proc.Kill() }

    Remove-Item Env:ZUI_PERF -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_PERF_MS -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_TRACE_CATEGORIES -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_TRACE_BUFFER_KB -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_STUDIO_AUTOTOUR -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_STUDIO_AUTOTOUR_EXIT -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_STUDIO_GRID -ErrorAction SilentlyContinue

    $lines = Get-Content -LiteralPath $log
    if (-not ($lines | Where-Object { $_ -match '\[studio\] tour done' })) {
        Write-Warning "pass $run never reached the end of the tour - its numbers are not usable"
    }
    $bucket = "startup"
    $frames = @{}
    foreach ($line in $lines) {
        if ($line -match '\[studio\] tour (\w+)') {
            $bucket = $Matches[1]
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

Write-Host ""
Write-Host "min of $Runs passes (peak frame per step):"
$summary = @()
foreach ($name in @("startup") + $steps) {
    $peaks = @()
    foreach ($pass in $passes) {
        if (-not $pass.Frames.ContainsKey($name)) { continue }
        $rows = $pass.Frames[$name]
        if ($rows.Count -eq 0) { continue }
        $peaks += ($rows | Sort-Object Total -Descending | Select-Object -First 1)
    }
    if ($peaks.Count -eq 0) { continue }
    $summary += [pscustomobject]@{
        Step     = $name
        MinTotal = ($peaks | Measure-Object -Property Total -Minimum).Minimum
        MinLayout = ($peaks | Measure-Object -Property Layout -Minimum).Minimum
        MinCascade = ($peaks | Measure-Object -Property Cascade -Minimum).Minimum
        MaxLaid  = ($peaks | Measure-Object -Property Laid -Maximum).Maximum
    }
}
$summary | Format-Table Step, MinTotal, MinLayout, MinCascade, MaxLaid -AutoSize
