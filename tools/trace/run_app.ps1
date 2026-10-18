# Launches one of the zui example / dump binaries with Perfetto tracing on, and writes
# `build/trace/<name>.pftrace` when the process exits (the SDK flushes at exit).
#
# The binary must link the SDK (`zui` or `zui_dump`). Layout unit-test exes do not.
#
# Usage:
#   pwsh tools/trace/run_app.ps1 studio
#   pwsh tools/trace/run_app.ps1 zui_studio -Categories app,engine
#   pwsh tools/trace/run_app.ps1 dump build/perf/grid60.html --size 1100x720 --repeat 6
#   pwsh tools/trace/run_app.ps1 -List
#
# Extra arguments after the app name are forwarded to the executable. Closing a windowed
# app is what triggers the write; dump exits on its own.

param(
    [Parameter(Position = 0)]
    [string]$App,

    # CMake build directory under build/, default windows-x64-release.
    [string]$Config = "windows-x64-release",

    # ZUI_TRACE_CATEGORIES. Default keeps per-box categories off so clicking around stays cheap.
    # Pass "" or use -AllCategories for every category.
    [string]$Categories = "app,engine",

    [switch]$AllCategories,

    # Also set ZUI_PERF=1 so stderr gets the one-line frame counters.
    [switch]$Perf,

    # Override the .pftrace path. Default: build/trace/<app>.pftrace
    [string]$Out,

    [switch]$List,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
)

$ErrorActionPreference = "Stop"

# Apps that link the Perfetto-enabled `zui` library (or zui_dump, which links the SDK itself).
$known = @(
    "zui_studio",
    "zui_hello",
    "zui_reader",
    "zui_motion",
    "zui_text",
    "zui_checks",
    "zui_flow",
    "zui_style",
    "zui_table",
    "zui_dump"
)

function Resolve-AppName([string]$name) {
    if ([string]::IsNullOrWhiteSpace($name)) {
        return $null
    }
    $n = $name.Trim()
    if ($n -like "*.exe") {
        $n = [System.IO.Path]::GetFileNameWithoutExtension($n)
    }
    if ($known -contains $n) {
        return $n
    }
    $with = "zui_" + $n
    if ($known -contains $with) {
        return $with
    }
    # Allow any zui_*.exe that happens to be built and linked; warn if not in the known list.
    if ($n.StartsWith("zui_")) {
        return $n
    }
    return $with
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binDir = Join-Path $repoRoot (Join-Path "build" $Config)
$traceDir = Join-Path $repoRoot "build/trace"

if ($List) {
    Write-Host "Traceable apps (link Perfetto via zui / zui_dump):"
    foreach ($name in $known) {
        $exe = Join-Path $binDir ($name + ".exe")
        $mark = if (Test-Path $exe) { "ok" } else { "missing" }
        Write-Host ("  {0,-14} [{1}]" -f $name, $mark)
    }
    Write-Host "Build dir: $binDir"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($App)) {
    Write-Host @"
Usage: pwsh tools/trace/run_app.ps1 <app> [args...]

  app          studio | hello | reader | motion | text | checks | flow | style | table | dump
               (zui_ prefix optional)
  -Config      build preset directory name (default windows-x64-release)
  -Categories  ZUI_TRACE_CATEGORIES (default app,engine)
  -AllCategories
  -Perf        also set ZUI_PERF=1
  -Out         override output path
  -List        show apps and whether the exe exists

Output: build/trace/<app>.pftrace  (written when the process exits)
"@
    exit 1
}

$resolved = Resolve-AppName $App
$exe = Join-Path $binDir ($resolved + ".exe")
if (-not (Test-Path $exe)) {
    throw "executable not found: $exe`nBuild it first, or pass -Config. Use -List to see what is present."
}

if (-not (Test-Path $traceDir)) {
    New-Item -ItemType Directory -Path $traceDir | Out-Null
}

if ([string]::IsNullOrWhiteSpace($Out)) {
    $Out = Join-Path $traceDir ($resolved + ".pftrace")
} else {
    $outParent = Split-Path -Parent $Out
    if ($outParent -and -not (Test-Path $outParent)) {
        New-Item -ItemType Directory -Path $outParent | Out-Null
    }
}

$Out = [System.IO.Path]::GetFullPath($Out)

$env:ZUI_TRACE = $Out
if (-not $env:ZUI_TRACE_BUFFER_KB) {
    # Match source/trace/trace.cpp default: long studio sessions overflow 4 MB quickly.
    $env:ZUI_TRACE_BUFFER_KB = "65536"
}
if ($AllCategories) {
    Remove-Item Env:ZUI_TRACE_CATEGORIES -ErrorAction SilentlyContinue
} elseif ($null -eq $Categories -or $Categories -eq "") {
    Remove-Item Env:ZUI_TRACE_CATEGORIES -ErrorAction SilentlyContinue
} else {
    $env:ZUI_TRACE_CATEGORIES = $Categories
}

if ($Perf) {
    $env:ZUI_PERF = "1"
}

Write-Host "exe:  $exe"
Write-Host "out:  $Out"
if ($env:ZUI_TRACE_CATEGORIES) {
    Write-Host "cats: $($env:ZUI_TRACE_CATEGORIES)"
} else {
    Write-Host "cats: * (all)"
}
if ($AppArgs -and $AppArgs.Count -gt 0) {
    Write-Host ("args: " + ($AppArgs -join " "))
}
Write-Host "Close the window (or let dump finish) to flush the trace."

# dump also accepts --trace; env alone is enough and keeps one path for every app.
# Run from the repo root so relative paths the user typed (HTML files, --trace, ...) still resolve.
# Start-Process rejects -ArgumentList @() / $null on some PowerShell builds, so omit it when
# the user passed no extra args.
$start = @{
    FilePath         = $exe
    WorkingDirectory = $repoRoot
    PassThru         = $true
    Wait             = $true
    NoNewWindow      = $true
}
if ($AppArgs -and $AppArgs.Count -gt 0) {
    $start.ArgumentList = $AppArgs
}
$p = Start-Process @start
$code = $p.ExitCode

if (-not (Test-Path $Out)) {
    Write-Host "warning: process exited $code but $Out was not written (binary built without Perfetto?)"
    exit $(if ($code -ne 0) { $code } else { 2 })
}

$size = (Get-Item $Out).Length
Write-Host ("wrote {0} ({1:N2} MB), exit {2}" -f $Out, ($size / 1MB), $code)
exit $code
