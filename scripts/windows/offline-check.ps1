param(
    [switch]$SkipHardwareCapture
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo = Resolve-Path (Join-Path $PSScriptRoot "..\\..")
Push-Location $repo
try {
    if (!(Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw "cmake was not found in PATH. Install Visual Studio C++/CMake tools or CMake before running the offline build."
    }

    Write-Host "[RTXMac] Configuring portable C++ core..."
    cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    Write-Host "[RTXMac] Building..."
    cmake --build build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

    Write-Host "[RTXMac] Running offline tests..."
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Offline tests failed ($LASTEXITCODE)" }

    if (!$SkipHardwareCapture) {
        & "$PSScriptRoot\\collect-hardware.ps1"
        & "$PSScriptRoot\\prepare-target.ps1"
    }

    Write-Host ""
    Write-Host "RTXMac Windows preflight passed. No macOS driver was installed and no GPU MMIO was touched."
} finally {
    Pop-Location
}
