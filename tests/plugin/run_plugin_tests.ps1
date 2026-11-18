# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the tests of the OpenScatter plugin in 3dsmaxbatch: 3ds Max 2027 without its interface.
#
#   powershell -ExecutionPolicy Bypass -File tests\plugin\run_plugin_tests.ps1 [-Only load,render_arnold,...]
#                                               [-Bundle <OpenScatter.bundle>] [-Work <folder>] [-TimeoutSeconds 900]
#
# Build the bundle first (build.ps1 -Plugin). Nothing is installed: the bundle is copied to a private
# folder under -Work, and only the test processes see it, through ADSK_APPLICATION_PLUGINS. Every test
# resets the scene of the process it runs in, which is a new one each time; a 3ds Max that is already
# open is never touched. A test that does not finish in time has its own process killed, nothing else.
#
# Tests: load, render_arnold, render_corona, save_reopen, determinism. Each writes results\<test>.txt
# next to this script, ending in "RESULT: PASS" or "RESULT: FAIL"; the exit code is the number of
# tests that did not pass.
param(
    [string[]]$Only = @(),
    [string]$Bundle = "",
    [string]$Work = (Join-Path ([System.IO.Path]::GetTempPath()) "osc_plugin_test"),
    [string]$MaxRoot = "C:\Program Files\Autodesk\3ds Max 2027",
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = "Stop"
# "-Only a,b" arrives as one string when the script is started with powershell -File.
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$here = $PSScriptRoot
$repo = Split-Path (Split-Path $here)
$results = Join-Path $here "results"
$batch = Join-Path $MaxRoot "3dsmaxbatch.exe"
if (-not (Test-Path $batch)) { throw "3dsmaxbatch.exe not found in $MaxRoot (pass -MaxRoot)" }

# The bundle build.ps1 -Plugin made: in the build folder, which build.local.ps1 may have moved.
if (-not $Bundle) {
    $BuildRoot = Join-Path $repo "build"
    $localSettings = Join-Path $repo "build.local.ps1"
    if (Test-Path $localSettings) { . $localSettings }
    $Bundle = Join-Path $BuildRoot "release\bundle\OpenScatter.bundle"
}
if (-not (Test-Path (Join-Path $Bundle "PackageContents.xml"))) { throw "No bundle at ${Bundle}: run build.ps1 -Plugin first" }

# A private copy, so that a build cannot change the plugin under a running test.
$plugins = Join-Path $Work "plugins"
$noPlugins = Join-Path $Work "no_plugins"
New-Item -ItemType Directory -Force $Work, $noPlugins, $results | Out-Null
if (Test-Path $plugins) { Remove-Item -Recurse -Force $plugins }
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item -Recurse $Bundle (Join-Path $plugins "OpenScatter.bundle")
$dlo = Get-Item (Join-Path $plugins "OpenScatter.bundle\Contents\plugins\2027\OpenScatter.dlo")
Write-Output "plugin under test: $($dlo.FullName) ($($dlo.Length) bytes, $($dlo.LastWriteTime), version $($dlo.VersionInfo.FileVersion))"

# Runs one script in its own 3dsmaxbatch and waits for it. $environment: extra variables for that process.
function Invoke-Batch([string]$script, [hashtable]$environment = @{}, [bool]$withPlugin = $true, [string]$tag = "") {
    $env:OSC_TEST_RESULTS = $results
    $env:OSC_TEST_WORK = $Work
    $env:ADSK_APPLICATION_PLUGINS = $(if ($withPlugin) { $plugins } else { $noPlugins })
    foreach ($key in $environment.Keys) { Set-Item -Path "Env:$key" -Value $environment[$key] }
    $name = [IO.Path]::GetFileNameWithoutExtension($script) + $tag
    $out = Join-Path $Work "$name.out"
    $started = Get-Date
    $p = Start-Process -FilePath $batch -ArgumentList "`"$(Join-Path $here $script)`"" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $out -RedirectStandardError (Join-Path $Work "$name.err")
    $null = $p.Handle   # without this a process started with redirection forgets its exit code
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $p.Id /T /F | Out-Null
        Write-Host "    TIMEOUT after $TimeoutSeconds s: $script was closed"
        return $false
    }
    Write-Host ("    {0}{1}: exit code {2}, {3:N0} s" -f $script, $tag, $p.ExitCode, ((Get-Date) - $started).TotalSeconds)
    # The verdict is in the result file. An exit code other than 0 is shown because it may mean that
    # 3ds Max did not shut down cleanly after the script had finished.
    if ($p.ExitCode -ne 0) { Write-Host "    WARNING: $script$tag left exit code $($p.ExitCode); its console output is in $out" }
    foreach ($key in $environment.Keys) { Remove-Item -Path "Env:$key" -ErrorAction SilentlyContinue }
    return $true
}

function Test-Passed([string]$file) {
    $path = Join-Path $results $file
    return (Test-Path $path) -and [bool](Select-String -Path $path -Pattern '^RESULT: PASS' -Quiet)
}

$tests = [ordered]@{
    "load" = {
        Remove-Item (Join-Path $results "load.txt") -ErrorAction SilentlyContinue
        [void](Invoke-Batch "test_load.ms")
        Test-Passed "load.txt"
    }
    "render_arnold" = {
        Remove-Item (Join-Path $results "render_arnold.txt") -ErrorAction SilentlyContinue
        [void](Invoke-Batch "test_render.ms" @{ OSC_TEST_RENDERER = "Arnold" } $true "_arnold")
        Test-Passed "render_arnold.txt"
    }
    "render_corona" = {
        Remove-Item (Join-Path $results "render_corona.txt") -ErrorAction SilentlyContinue
        [void](Invoke-Batch "test_render.ms" @{ OSC_TEST_RENDERER = "Corona" } $true "_corona")
        Test-Passed "render_corona.txt"
    }
    "save_reopen" = {
        $file = Join-Path $results "save_reopen.txt"
        Remove-Item $file -ErrorAction SilentlyContinue
        [void](Invoke-Batch "test_save_reopen.ms" @{ OSC_TEST_STEP = "save" } $true "_save")
        [void](Invoke-Batch "test_save_reopen.ms" @{ OSC_TEST_STEP = "reopen" } $true "_reopen")
        [void](Invoke-Batch "test_save_reopen.ms" @{ OSC_TEST_STEP = "missing" } $false "_missing")
        [void](Invoke-Batch "test_save_reopen.ms" @{ OSC_TEST_STEP = "restored" } $true "_restored")
        # Four steps, four verdicts.
        $steps = @(Select-String -Path $file -Pattern '^STEP (\w+): (PASS|FAIL)' -ErrorAction SilentlyContinue)
        $passed = @($steps | Where-Object { $_.Matches[0].Groups[2].Value -eq "PASS" }).Count
        $ok = ($steps.Count -eq 4) -and ($passed -eq 4)
        Add-Content $file ("RESULT: {0} ({1} of 4 steps passed)" -f $(if ($ok) { "PASS" } else { "FAIL" }), $passed)
        $ok
    }
    "determinism" = {
        $file = Join-Path $results "determinism.txt"
        Remove-Item $file, (Join-Path $results "determinism_run1.txt"), (Join-Path $results "determinism_run2.txt") -ErrorAction SilentlyContinue
        [void](Invoke-Batch "test_determinism.ms" @{ OSC_TEST_RUN = "1" } $true "_run1")
        [void](Invoke-Batch "test_determinism.ms" @{ OSC_TEST_RUN = "2" } $true "_run2")
        $runs = 1, 2 | ForEach-Object {
            $path = Join-Path $results "determinism_run$_.txt"
            if (Test-Path $path) { , @(Select-String -Path $path -Pattern '^HASH ' | ForEach-Object { $_.Line }) } else { , @() }
        }
        $same = ($runs[0].Count -eq 2) -and (($runs[0] -join "|") -eq ($runs[1] -join "|"))
        $ok = $same -and (Test-Passed "determinism_run1.txt") -and (Test-Passed "determinism_run2.txt")
        $lines = @("Two processes, the same scene:", "  run 1: $($runs[0] -join '; ')", "  run 2: $($runs[1] -join '; ')",
                   "  $(if ($same) { 'ok   ' } else { 'FAIL ' }) the instance buffers of the two runs have the same hash",
                   "RESULT: $(if ($ok) { 'PASS' } else { 'FAIL' })")
        Set-Content $file $lines
        $ok
    }
}

$failed = 0
foreach ($name in $tests.Keys) {
    if ($Only.Count -gt 0 -and $Only -notcontains $name) { continue }
    Write-Output "[$name]"
    $ok = & $tests[$name] | Select-Object -Last 1
    if ($ok) { Write-Output "    PASS" } else { Write-Output "    FAIL  (see $(Join-Path $results "$name.txt"))"; $failed++ }
}
Remove-Item Env:OSC_TEST_RESULTS, Env:OSC_TEST_WORK, Env:ADSK_APPLICATION_PLUGINS -ErrorAction SilentlyContinue
Write-Output "$failed test(s) failed. Results: $results"
exit $failed
