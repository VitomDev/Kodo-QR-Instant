# Sets up the MSVC x64 toolchain with the pinned Windows SDK, CMake and Ninja
# from .deps/ in the current PowerShell session. Dot-source it: . .\scripts\dev-env.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root '.deps'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ x64 tools was not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] }
}

# Use the pinned Windows SDK from NuGet so builds do not depend on what is installed.
$sdkVersion = '10.0.28000.2526'
$sdk = Join-Path $deps "microsoft.windows.sdk.cpp.$sdkVersion\c"
$sdkLib = Join-Path $deps "microsoft.windows.sdk.cpp.x64.$sdkVersion\c"
$sdkTools = Join-Path $deps "microsoft.windows.sdk.buildtools.$sdkVersion\bin\10.0.28000.0\x64"
$sdkInclude = Join-Path $sdk 'Include\10.0.28000.0'
$sdkIncludes = @('ucrt', 'um', 'shared', 'winrt', 'cppwinrt') | ForEach-Object { Join-Path $sdkInclude $_ }
$env:INCLUDE = ($sdkIncludes -join ';') + ';' + $env:INCLUDE
$env:LIB = (Join-Path $sdkLib 'ucrt\x64') + ';' + (Join-Path $sdkLib 'um\x64') + ';' + $env:LIB
$env:PATH = "$sdkTools;$(Join-Path $deps 'cmake-3.31.9-windows-x86_64\bin');$(Join-Path $deps 'ninja-1.12.1');$env:PATH"
