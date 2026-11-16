# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the OpenScatter core library, its tools and its tests with the Visual Studio Build Tools
# (MSVC, plus the CMake and Ninja that come with them), then runs the tests. With -Plugin it also
# builds the 3ds Max plugin and puts the bundle 3ds Max loads together.
#
#   powershell -ExecutionPolicy Bypass -File build.ps1 [-Config Release|Debug] [-BuildDir <dir>]
#                                                      [-NoTests] [-Bench] [-Clean]
#                                                      [-Plugin] [-MaxSdk <maxsdk folder>]
#
# The build goes to <repo>\build\<config> unless -BuildDir says otherwise. To build somewhere else every
# time (a local disk when the repo is on a network share, for instance) create build.local.ps1 next to
# this file with one line, e.g.  $BuildRoot = "D:\builds\openscatter"  - that file stays out of git.
#
# -Plugin needs the 3ds Max 2027 SDK and a Release build. The SDK's "maxsdk" folder comes from -MaxSdk,
# else from $MaxSdk in build.local.ps1, else from the ADSK_3DSMAX_SDK_2027 environment variable the
# SDK's installer sets. The result is <build>\bundle\OpenScatter.bundle, ready to be copied into an
# ApplicationPlugins folder; nothing is installed.
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [string]$BuildDir = "",
    [switch]$NoTests,
    [switch]$Bench,
    [switch]$Clean,
    [switch]$Plugin,
    [string]$MaxSdk = ""
)
$ErrorActionPreference = "Stop"
$src = $PSScriptRoot

$BuildRoot = Join-Path $src "build"
$localSettings = Join-Path $src "build.local.ps1"
$MaxSdkArgument = $MaxSdk
if (Test-Path $localSettings) { . $localSettings }
if ($MaxSdkArgument) { $MaxSdk = $MaxSdkArgument }
if (-not $MaxSdk) { $MaxSdk = $env:ADSK_3DSMAX_SDK_2027 }
if (-not $BuildDir) { $BuildDir = Join-Path $BuildRoot $Config.ToLower() }

if ($Plugin) {
    if ($Config -ne "Release") { throw "-Plugin needs -Config Release: 3ds Max only loads plugins built against the release C runtime." }
    if (-not $MaxSdk -or -not (Test-Path (Join-Path $MaxSdk "include\max.h"))) {
        throw "3ds Max SDK not found (looked in '$MaxSdk'). Pass -MaxSdk <path to the SDK's maxsdk folder>, or set `$MaxSdk in build.local.ps1."
    }
}

# Visual Studio (any edition with the C++ tools): ask vswhere, else the usual place of the 2022 Build Tools.
$vs = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
}
if (-not $vs) { $vs = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools" }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "Visual Studio C++ tools not found (looked for $vcvars)" }
# vcvars64.bat calls vswhere.exe by name; with its folder on PATH it does not complain about it.
$installer = Split-Path $vswhere
if (Test-Path $installer) { $env:PATH = "$env:PATH;$installer" }

# cmake and ninja are called by their full path: putting them on PATH with "set PATH=...;%PATH%" in the
# same cmd line as vcvars64.bat would be expanded before vcvars runs and wipe the compiler's PATH.
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source }
if (-not (Test-Path $ninja)) { $ninja = (Get-Command ninja.exe -ErrorAction Stop).Source }

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
New-Item -ItemType Directory -Force $BuildDir | Out-Null

# The plugin is switched on or off explicitly, so that a build folder does not remember the last choice.
$pluginArgs = "-DOSC_BUILD_PLUGIN=OFF"
if ($Plugin) { $pluginArgs = "-DOSC_BUILD_PLUGIN=ON `"-DOSC_MAXSDK=$($MaxSdk -replace '\\', '/')`"" }

$cmd = "call `"$vcvars`" >nul && " +
       "`"$cmake`" -S `"$src`" -B `"$BuildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$Config `"-DCMAKE_MAKE_PROGRAM=$ninja`" $pluginArgs && " +
       "`"$cmake`" --build `"$BuildDir`""
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
Write-Output "Built ($Config): $BuildDir\bin"

if (-not $NoTests) {
    & (Join-Path $BuildDir "bin\osc_core_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE" }
}

if ($Bench) {
    & (Join-Path $BuildDir "bin\osc_bench.exe")
    if ($LASTEXITCODE -ne 0) { throw "The benchmark missed its targets (exit code $LASTEXITCODE)" }
}

if ($Plugin) {
    # The bundle: the package description from the repo plus the plugin just built. The version is
    # written in three places (CMakeLists.txt, which also stamps the .dlo, and twice in the package
    # description): refuse to put a bundle together when they disagree.
    $dlo = Join-Path $BuildDir "plugin\OpenScatter.dlo"
    if (-not (Test-Path $dlo)) { throw "The plugin was not built: $dlo is missing" }
    $package = Join-Path $src "bundle\OpenScatter.bundle\PackageContents.xml"
    $version = [regex]::Match((Get-Content (Join-Path $src "CMakeLists.txt") -Raw), 'project\(OpenScatter VERSION ([0-9.]+)').Groups[1].Value
    $xml = [xml](Get-Content $package -Raw)
    $entry = $xml.ApplicationPackage.Components.ComponentEntry
    if ($xml.ApplicationPackage.AppVersion -ne $version -or $entry.Version -ne $version) {
        throw "Version mismatch: CMakeLists.txt says $version, PackageContents.xml says $($xml.ApplicationPackage.AppVersion) and $($entry.Version)"
    }
    $bundle = Join-Path $BuildDir "bundle\OpenScatter.bundle"
    if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }
    $dloDir = Join-Path $bundle "Contents\plugins\2027"
    New-Item -ItemType Directory -Force $dloDir | Out-Null
    Copy-Item $package (Join-Path $bundle "PackageContents.xml")
    Copy-Item $dlo (Join-Path $dloDir "OpenScatter.dlo")
    Write-Output "Bundle ($version): $bundle"
}
