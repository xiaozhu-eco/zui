# Downloads the Perfetto `trace_processor_shell` this repository's traces are meant to be read
# with, and verifies the pinned sha256 before the file is usable.
#
# Why a download and not a vendored copy: the binary is 14 MB, is not linked into anything, and is
# a diagnostic tool rather than a dependency. `cmake/deps.cmake` never sees it, so a normal build
# still needs no network. What is version-controlled is the pin below.
#
# Usage:
#   pwsh tools/trace/fetch_trace_processor.ps1            # fetch if missing
#   pwsh tools/trace/fetch_trace_processor.ps1 -Force     # refetch
#
# Prints the path of the executable on success.

param(
    [switch]$Force,
    # Override for a mirror. Both Google addresses below serve the same artifact; some networks
    # resolve one and not the other, which is why both are tried.
    [string]$Url
)

$ErrorActionPreference = 'Stop'

# Pinned to the Perfetto release the trace format notes in docs/TRACING.md were written against.
$version = 'v58.2'
$sha256 = 'adfa6bad3d72be3ba9b83fa2b17b69fa13b3ab1cad0f42e52b86188bd5f0f997'
$artifact = "perfetto-luci-artifacts/$version/windows-amd64/trace_processor_shell.exe"
$urls = if ($Url) {
    @($Url)
} else {
    @("https://storage.googleapis.com/$artifact",
      "https://commondatastorage.googleapis.com/$artifact")
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$targetDir = Join-Path $repoRoot 'build/tools'
$target = Join-Path $targetDir 'trace_processor_shell.exe'

if ((Test-Path $target) -and -not $Force) {
    $existing = (Get-FileHash -Algorithm SHA256 -Path $target).Hash.ToLower()
    if ($existing -eq $sha256) {
        Write-Output $target
        exit 0
    }
    Write-Host "cached binary does not match the pin, refetching"
}

New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
$tmp = "$target.download"

# curl.exe ships with Windows and fails fast where `Invoke-WebRequest` can hang on a blocked host.
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
$downloaded = $false
foreach ($candidate in $urls) {
    Write-Host "downloading Perfetto trace_processor $version from $candidate"
    if ($curl) {
        & $curl.Source -fSL --connect-timeout 20 --max-time 900 -o $tmp $candidate
        $downloaded = $LASTEXITCODE -eq 0
    } else {
        try {
            Invoke-WebRequest -Uri $candidate -OutFile $tmp -UseBasicParsing -TimeoutSec 900
            $downloaded = $true
        } catch {
            $downloaded = $false
        }
    }
    if ($downloaded) { break }
    Write-Warning "could not download from $candidate"
}
if (-not $downloaded) {
    throw "no source served $artifact; pass -Url to point at a mirror"
}

$actual = (Get-FileHash -Algorithm SHA256 -Path $tmp).Hash.ToLower()
if ($actual -ne $sha256) {
    Remove-Item -LiteralPath $tmp -Force
    throw "checksum mismatch`n  expected $sha256`n  actual   $actual"
}

Move-Item -LiteralPath $tmp -Destination $target -Force
Write-Output $target
