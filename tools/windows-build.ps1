# Build the cells CLI on Windows (full binary with sync/WebRTC).
# Requires: Bazelisk (honors .bazelversion), VS 2022 C++ tools.
# Usage (from repo root):
#   .\tools\windows-build.ps1
#   .\tools\windows-build.ps1 -Release
#   .\tools\windows-build.ps1 -ConverterOnly

param(
    [switch]$Release,
    [switch]$ConverterOnly
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$bazel = Get-Command bazelisk -ErrorAction SilentlyContinue
if (-not $bazel) {
    $bazel = Get-Command bazel -ErrorAction SilentlyContinue
}
if (-not $bazel) {
    Write-Error "bazelisk (or bazel) not found on PATH. Install: winget install Bazel.Bazelisk"
}

# Prefer Git Bash over WSL if any rules shell out (not required for pure-Bazel libdc).
$gitBash = "C:\Program Files\Git\bin\bash.exe"
if (Test-Path $gitBash) {
    $env:BAZEL_SH = $gitBash
}

$target = if ($ConverterOnly) { "//apps/cli:cells-converter" } else { "//apps/cli:cells" }
$config = @()
if ($Release) { $config = @("-c", "opt") }

Write-Host "Building $target $($config -join ' ')..."
& $bazel.Source @($config + @("build", $target))
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exeName = if ($ConverterOnly) { "cells-converter.exe" } else { "cells.exe" }
$src = Join-Path $RepoRoot "bazel-bin\apps\cli\$exeName"
$destDir = Join-Path $RepoRoot "dist\cli"
New-Item -ItemType Directory -Force -Path $destDir | Out-Null
$dest = Join-Path $destDir $(if ($ConverterOnly) { "cells-converter.exe" } else { "cells.exe" })
Copy-Item -Force $src $dest
Write-Host "Built: $dest"
