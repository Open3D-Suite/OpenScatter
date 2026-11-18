# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the viewport test of the OpenScatter plugin in a second, fresh 3ds Max with its interface.
#
#   powershell -ExecutionPolicy Bypass -File tests\plugin\run_viewport_test.ps1 -Driver x11   # Nitrous Direct3D 11 (GPU)
#   powershell -ExecutionPolicy Bypass -File tests\plugin\run_viewport_test.ps1 -Driver xs    # Nitrous Software (WARP)
#
# That 3ds Max works on a private copy of the user's 3dsMax.ini (-i) with the display driver chosen
# here (-v), and loads the plugin from a private folder (ADSK_APPLICATION_PLUGINS): the user's settings
# file is neither used for the driver nor written (its hash is checked), and nothing is installed. If
# the test does not finish in time, only that process is killed: a 3ds Max that was already open is
# never touched. Result: results\viewport_<driver>.txt next to this script.
param(
    [ValidateSet("x11", "xs")][string]$Driver = "x11",
    [string]$Bundle = "",
    [string]$Work = (Join-Path ([System.IO.Path]::GetTempPath()) "osc_plugin_test"),
    [string]$MaxRoot = "C:\Program Files\Autodesk\3ds Max 2027",
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$repo = Split-Path (Split-Path $here)
$results = Join-Path $here "results"
$max = Join-Path $MaxRoot "3dsmax.exe"
if (-not (Test-Path $max)) { throw "3dsmax.exe not found in $MaxRoot (pass -MaxRoot)" }

if (-not $Bundle) {
    $BuildRoot = Join-Path $repo "build"
    $localSettings = Join-Path $repo "build.local.ps1"
    if (Test-Path $localSettings) { . $localSettings }
    $Bundle = Join-Path $BuildRoot "release\bundle\OpenScatter.bundle"
}
if (-not (Test-Path (Join-Path $Bundle "PackageContents.xml"))) { throw "No bundle at ${Bundle}: run build.ps1 -Plugin first" }

# Its own copy of the bundle, apart from the one the batch tests use: the two may run at the same time.
$plugins = Join-Path $Work "plugins_viewport_$Driver"
New-Item -ItemType Directory -Force $Work, $results | Out-Null
if (Test-Path $plugins) { Remove-Item -Recurse -Force $plugins }
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item -Recurse $Bundle (Join-Path $plugins "OpenScatter.bundle")

# \\localhost\C$ and not %LOCALAPPDATA%: a shell started by a packaged app may see a redirected AppData.
$userDir = "\\localhost\C$\Users\$env:USERNAME\AppData\Local\Autodesk\3dsMax\2027 - 64bit\ENU"
$userIni = Join-Path $userDir "3dsMax.ini"
if (-not (Test-Path $userIni)) { throw "The user's 3dsMax.ini was not found at $userIni" }
$altIni = Join-Path $Work "3dsMax_viewport_$Driver.ini"
Copy-Item $userIni $altIni -Force
$userIniHash = (Get-FileHash $userIni).Hash

$result = Join-Path $results "viewport_$Driver.txt"
Remove-Item $result -ErrorAction SilentlyContinue
$env:OSC_TEST_RESULTS = $results
$env:OSC_TEST_WORK = $Work
$env:OSC_TEST_DRIVER = $Driver
$env:ADSK_APPLICATION_PLUGINS = $plugins

$script = Join-Path $here "test_viewport.ms"
$p = Start-Process -FilePath $max -ArgumentList '-silent', "-v$Driver", '-i', "`"$altIni`"", '-U', 'MAXScript', "`"$script`"" -PassThru
Write-Output "started 3ds Max, pid $($p.Id), driver switch -v$Driver"
if ($p.WaitForExit($TimeoutSeconds * 1000)) {
    Write-Output "3ds Max finished, exit code $($p.ExitCode)"
} else {
    & taskkill.exe /PID $p.Id /T /F | Out-Null
    Write-Output "TIMEOUT after $TimeoutSeconds s: that 3ds Max was closed"
}
Remove-Item Env:OSC_TEST_RESULTS, Env:OSC_TEST_WORK, Env:OSC_TEST_DRIVER, Env:ADSK_APPLICATION_PLUGINS -ErrorAction SilentlyContinue

# Which driver that 3ds Max really created, from its own log.
$created = Get-ChildItem (Join-Path $userDir "Network") -Filter "Max.*" -ErrorAction SilentlyContinue |
    ForEach-Object { Select-String -Path $_.FullName -Pattern ("\[{0:D5}\].*(Graphics driver|OGS graphic)" -f $p.Id) } |
    ForEach-Object { ($_.Line -replace '^.*?\]\s*\[\d+\]\s*', '') } | Select-Object -Unique
if (Test-Path $result) { Add-Content -Path $result -Value "", "--- from 3ds Max's log ---", $created }
Write-Output "user's 3dsMax.ini unchanged: $((Get-FileHash $userIni).Hash -eq $userIniHash)"
if (Test-Path $result) { Get-Content $result } else { Write-Output "no result file: the test did not run" }
exit $(if ((Test-Path $result) -and (Select-String -Path $result -Pattern '^RESULT: PASS' -Quiet)) { 0 } else { 1 })
