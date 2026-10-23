# Builds Kodo QR Instant: Kodo.exe (with ZXing-C++ linked in), the tests and
# the KodoSetup.exe installer.
#
#   .\build.ps1                 Build everything and run the tests.
#   .\build.ps1 -SkipTests      Build without running the tests.
#   .\build.ps1 -SkipInstaller  Build the binaries only.
#
# Requirements: Visual Studio 2022 or newer with the C++ x64 tools and Inno
# Setup 6. The Windows SDK, CMake, Ninja and ZXing-C++ are downloaded into
# .deps by scripts/fetch-deps.ps1.

param(
    [switch]$SkipTests,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$deps = Join-Path $root '.deps'
$out = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

& (Join-Path $root 'scripts\fetch-deps.ps1')
. (Join-Path $root 'scripts\dev-env.ps1')

function Invoke-Tool([string]$tool) {
    # Tools print warnings on stderr; only the exit code decides failure.
    $ErrorActionPreference = 'Continue'
    & $tool @args 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$tool failed with exit code $LASTEXITCODE" }
}

# --- Kodo.exe and the tests -----------------------------------------------------

Write-Host 'Building Kodo'
$appBuild = Join-Path $out 'app'
Invoke-Tool cmake -S $root -B $appBuild -G Ninja -DCMAKE_BUILD_TYPE=Release --log-level=WARNING -Wno-dev `
    "-DKODO_ZXING_SOURCE=$(Join-Path $deps 'zxing-cpp-3.1.1')"
Invoke-Tool cmake --build $appBuild

if (-not $SkipTests) {
    Write-Host 'Running tests'
    Invoke-Tool (Join-Path $appBuild 'KodoTests.exe') $appBuild
}

# --- Installer -------------------------------------------------------------------

if (-not $SkipInstaller) {
    $iscc = @("${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw 'Inno Setup 6 was not found.' }
    New-Item -ItemType Directory -Force $dist | Out-Null
    Invoke-Tool $iscc /Q "/DBuildDir=$appBuild" "/O$dist" (Join-Path $root 'installer\Kodo.iss')
    Write-Host "Installer: $dist\KodoSetup.exe"
}
