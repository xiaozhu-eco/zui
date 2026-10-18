# `布局成本结构性修复方案.md` section 1: how layout cost scales with the number of one-line boxes,
# and - the measurement that names the shape of the curve - how it changes when the same rows sit
# under one parent instead of eighty.
#
# The pages are generated into build/scratch (ignored); this script is the reproducible part. The
# stylesheet is plain - no content-visibility, no :has() - but note what it *is*: the item is
# `width: 100%` and the box is 400px tall, so the root overflows the 800x400 viewport and the frame
# takes `root.retry` (the `rounds=` / `bar=` columns below, 2 and 1 here). The second pass runs on
# an already-clean tree with a different percentage basis, so every box re-stores - which is what
# `布局成本结构性修复方案.md` section 1.4 is about. The reader's own TOC frame does *not* take that
# second pass, so this fixture's absolute numbers must not be read as reader numbers (section 1.5).
#
# Usage:
#   pwsh tools/trace/scale_layout.ps1                     # curve + shape, Release
#   pwsh tools/trace/scale_layout.ps1 -TraceDir build/trace   # also write pftrace for the queries
#   pwsh tools/trace/scale_layout.ps1 -Config windows-x64     # Debug (numbers are ~15x larger)

param(
    [string]$Config = "windows-x64-release",
    [string]$Scratch = "D:\code\zui\build\scratch\scale_layout",
    # Non-empty writes <name>.pftrace there, for 03_hotspots.sql and the store.* breakdown.
    [string]$TraceDir = "",
    [switch]$SkipCurve,
    [switch]$SkipShape
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dump = Join-Path $repoRoot (Join-Path "build" (Join-Path $Config "zui_dump.exe"))
if (-not (Test-Path $dump)) { throw "zui_dump not found: $dump" }
if (-not (Test-Path $Scratch)) { New-Item -ItemType Directory -Force -Path $Scratch | Out-Null }

$css = @"
#scroller { overflow: auto; height: 400px; }
.group { margin: 0; padding: 0; }
.ch {
    display: block;
    width: 100%;
    margin: 0;
    padding: 9px 10px 9px 12px;
    border: 0;
    font-family: serif;
    font-size: 14px;
    line-height: 1.45;
    text-align: left;
}
"@
Set-Content -Path (Join-Path $Scratch "scale.css") -Value $css -Encoding UTF8

function New-Page {
    param([string]$Path, [int]$Rows, [int]$Groups)
    $perGroup = [int]($Rows / $Groups)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="scale.css"></head><body><div id="scroller">')
    $i = 0
    for ($g = 0; $g -lt $Groups; $g++) {
        if ($Groups -gt 1) { [void]$sb.AppendLine('<div class="group">') }
        for ($k = 0; $k -lt $perGroup; $k++) {
            $i++
            [void]$sb.AppendLine("<button type=`"button`" id=`"ch$i`" class=`"ch`">Chapter $i</button>")
        }
        if ($Groups -gt 1) { [void]$sb.AppendLine('</div>') }
    }
    [void]$sb.AppendLine('</div></body></html>')
    Set-Content -Path $Path -Value $sb.ToString() -Encoding UTF8
}

function Measure-Page {
    param([string]$Name, [string]$Path)
    $traceArg = @()
    if ($TraceDir -ne "") {
        if (-not (Test-Path $TraceDir)) { New-Item -ItemType Directory -Force -Path $TraceDir | Out-Null }
        $traceArg = @("--trace", (Join-Path $TraceDir "$Name.pftrace"))
    }
    # The [perf] line goes to stderr; PowerShell turns redirected native stderr into ErrorRecords,
    # whose `.Line` is empty, so capture the whole stream as one string first.
    $text = (& $dump $Path --size 800x400 --brief @traceArg 2>&1 | Out-String)
    $line = ($text -split "`n" | Where-Object { $_ -match '\[perf\]' } | Select-Object -First 1)
    if ($line -match 'total=([\d.]+)ms\s+rounds=(\d+) bar=(\d+).*?\broot=([\d.]+).*?\blaid=(\d+) hits=(\d+) lookups=(\d+) shapes=(\d+).*?phase_settle=([\d.]+)') {
        [pscustomobject]@{
            Page    = $Name
            Total   = [double]$Matches[1]
            Rounds  = [int]$Matches[2]
            Bar     = [int]$Matches[3]
            Root    = [double]$Matches[4]
            Laid    = [int]$Matches[5]
            Hits    = [int]$Matches[6]
            Lookups = [int]$Matches[7]
            Shapes  = [int]$Matches[8]
            Settle  = [double]$Matches[9]
        }
    } else {
        Write-Warning "$Name : no [perf] line"
        $null
    }
}

$env:ZUI_PERF = "1"
$env:ZUI_PERF_MS = "1"
try {
    if (-not $SkipCurve) {
        Write-Host "== curve: one parent, N rows =="
        $rows = @()
        foreach ($n in 1000, 2000, 4000, 8000) {
            $page = Join-Path $Scratch "curve$n.html"
            if (-not (Test-Path $page)) { New-Page -Path $page -Rows $n -Groups 1 }
            $m = Measure-Page -Name "curve$n" -Path $page
            if ($m -ne $null) {
                $m | Add-Member -NotePropertyName PerRowUs -NotePropertyValue ([double]($m.Root / $n * 1000))
                # `phase_settle` divided by the stores of the same run: the P1 gate (flat vs nested)
                # is this number, not the trace zone.
                $m | Add-Member -NotePropertyName SettleNs -NotePropertyValue ([double]($m.Settle / [Math]::Max($m.Laid,1) * 1e6))
                $rows += $m
            }
        }
        $rows | Format-Table Page, Total, Root, PerRowUs, Laid, Lookups, Shapes, Rounds, Bar, SettleNs -AutoSize
    }

    if (-not $SkipShape) {
        Write-Host "== shape: 8000 rows, 1 parent vs 80 parents =="
        $flat = Join-Path $Scratch "flat8000.html"
        $nested = Join-Path $Scratch "nested80x100.html"
        if (-not (Test-Path $flat)) { New-Page -Path $flat -Rows 8000 -Groups 1 }
        if (-not (Test-Path $nested)) { New-Page -Path $nested -Rows 8000 -Groups 80 }
        $rows = @()
        foreach ($case in @(@("flat8000", $flat), @("nested80x100", $nested))) {
            $m = Measure-Page -Name $case[0] -Path $case[1]
            if ($m -ne $null) { $rows += $m }
        }
        $rows | Format-Table Page, Total, Root, Laid, Lookups, Shapes, Rounds, Bar -AutoSize
        if ($rows.Count -eq 2 -and $rows[0].Root -gt 0) {
            Write-Host ("flat / nested root ratio: {0:N2}x" -f ($rows[0].Root / $rows[1].Root))
        }
        if ($rows.Count -eq 2) {
            $flatNs = $rows[0].Settle / [Math]::Max($rows[0].Laid, 1) * 1e6
            $nestedNs = $rows[1].Settle / [Math]::Max($rows[1].Laid, 1) * 1e6
            Write-Host ("settle per store: flat {0:N0}ns / nested {1:N0}ns" -f $flatNs, $nestedNs)
            if ($nestedNs -gt 0) {
                Write-Host ("flat / nested settle ratio: {0:N2}x" -f ($flatNs / $nestedNs))
            }
        }
    }
} finally {
    Remove-Item Env:ZUI_PERF -ErrorAction SilentlyContinue
    Remove-Item Env:ZUI_PERF_MS -ErrorAction SilentlyContinue
}
