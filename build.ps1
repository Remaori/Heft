<#
.SYNOPSIS
    Rebuilds, validates and installs Heft.vst3 with a single command.

.EXAMPLE
    .\build.ps1                  # build + pluginval (level 10) + install
    .\build.ps1 -Strictness 5    # faster validation
    .\build.ps1 -Measure         # also measure the sound (HeftMeasure) and run the unit tests (HeftTests)
    .\build.ps1 -SkipValidation  # build and install only
    .\build.ps1 -Clean           # delete build\ and build from scratch
    .\build.ps1 -Host            # only build JUCE's AudioPluginHost, a second host for testing
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 10)]
    [int]    $Strictness = 10,
    [switch] $SkipValidation,
    [switch] $Measure,
    [switch] $Clean,
    # -Host. PowerShell already uses the name $Host, so the switch is an alias.
    [Alias('Host')]
    [switch] $BuildHost,
    [string] $InstallDir = (Join-Path $env:USERPROFILE 'VST3')
)

$ErrorActionPreference = 'Stop'

$root         = $PSScriptRoot
$buildDir     = Join-Path $root 'build'
$pluginBuilt  = Join-Path $buildDir 'Heft_artefacts\Release\VST3\Heft.vst3'
$measureExe   = Join-Path $buildDir 'HeftMeasure_artefacts\Release\HeftMeasure.exe'
$testsExe     = Join-Path $buildDir 'HeftTests_artefacts\Release\HeftTests.exe'
$pluginvalDir = Join-Path $root 'tools\pluginval'
$pluginvalExe = Join-Path $pluginvalDir 'pluginval.exe'
$pluginvalUrl = 'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_Windows.zip'

function Step ([string] $text) { Write-Host ''; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail ([string] $text) { Write-Host ''; Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # CMake bundled with Visual Studio / Build Tools
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $installs = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($vs in $installs) {
            $candidate = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    Fail 'CMake not found. Install CMake, or the "C++ CMake tools for Windows" component in the Visual Studio Installer.'
}

function Get-Pluginval {
    if (Test-Path $pluginvalExe) { return }

    Step 'Downloading pluginval v1.0.4'
    New-Item -ItemType Directory -Force $pluginvalDir | Out-Null
    $zip = Join-Path $pluginvalDir 'pluginval_Windows.zip'
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $pluginvalUrl -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $pluginvalDir -Force
}

$timer = [Diagnostics.Stopwatch]::StartNew()
$cmake = Find-CMake

# --- Optional: JUCE's AudioPluginHost, to test Heft in a second host -------------
# Uses the JUCE source the normal build already downloaded (no second download) and
# its own folder, build\juce-host. Later runs only rebuild what changed.
if ($BuildHost) {
    $juceSource = Join-Path $buildDir '_deps\juce-src'
    $hostDir    = Join-Path $buildDir 'juce-host'
    $hostExe    = Join-Path $hostDir 'extras\AudioPluginHost\AudioPluginHost_artefacts\Release\AudioPluginHost.exe'

    if (-not (Test-Path (Join-Path $juceSource 'CMakeLists.txt'))) {
        Step 'Configuring the project first (downloads JUCE, takes a few minutes)'
        & $cmake -S $root -B $buildDir -A x64
        if ($LASTEXITCODE -ne 0) { Fail 'CMake configuration failed.' }
    }

    if (-not (Test-Path (Join-Path $hostDir 'CMakeCache.txt'))) {
        Step 'Configuring the JUCE extras'
        & $cmake -S $juceSource -B $hostDir -A x64 -DJUCE_BUILD_EXTRAS=ON -DJUCE_BUILD_EXAMPLES=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
        if ($LASTEXITCODE -ne 0) { Fail 'Configuring the AudioPluginHost failed.' }
    }

    Step 'Building the AudioPluginHost (the first time takes a few minutes)'
    & $cmake --build $hostDir --config Release --parallel --target AudioPluginHost
    if ($LASTEXITCODE -ne 0) { Fail 'Building the AudioPluginHost failed.' }
    if (-not (Test-Path $hostExe)) { Fail "AudioPluginHost.exe not found at $hostExe" }

    Write-Host ''
    Write-Host "AudioPluginHost ready in $([int]$timer.Elapsed.TotalSeconds) s:" -ForegroundColor Green
    Write-Host "  $hostExe"
    Write-Host 'To load Heft in it, see "Testing in a second host" in the README.'
    exit 0
}

# --- 1. Configure --------------------------------------------------------------
if ($Clean -and (Test-Path $buildDir)) {
    Step 'Deleting build\'
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Step 'Configuring the project (the first time it downloads JUCE, takes a few minutes)'
    & $cmake -S $root -B $buildDir -A x64
    if ($LASTEXITCODE -ne 0) { Fail 'CMake configuration failed.' }
}

# --- 2. Build ------------------------------------------------------------------
Step 'Building in Release'
& $cmake --build $buildDir --config Release --parallel --target Heft_VST3
if ($LASTEXITCODE -ne 0) { Fail 'Build failed. Check the errors above.' }
if (-not (Test-Path $pluginBuilt)) { Fail "Built plugin not found at $pluginBuilt" }

# --- 3. Validate ---------------------------------------------------------------
if (-not $SkipValidation) {
    Get-Pluginval
    Step "Validating with pluginval (strictness $Strictness)"
    $logDir = Join-Path $buildDir 'pluginval'
    $pvArgs = @('--strictness-level', "$Strictness", '--output-dir', "`"$logDir`"", '--validate', "`"$pluginBuilt`"")
    $pv = Start-Process -FilePath $pluginvalExe -ArgumentList $pvArgs -NoNewWindow -Wait -PassThru
    if ($pv.ExitCode -ne 0) { Fail "pluginval found errors (log in $logDir). The plugin was NOT installed." }
    Write-Host 'pluginval: all tests passed.' -ForegroundColor Green
}

if ($Measure) {
    Step 'Measuring the sound (HeftMeasure)'
    & $cmake --build $buildDir --config Release --parallel --target HeftMeasure
    if ($LASTEXITCODE -ne 0) { Fail 'Building HeftMeasure failed.' }
    & $measureExe $pluginBuilt
    if ($LASTEXITCODE -ne 0) { Fail 'Some measurements failed. The plugin was NOT installed.' }

    Step 'Running the unit tests (HeftTests)'
    & $cmake --build $buildDir --config Release --parallel --target HeftTests
    if ($LASTEXITCODE -ne 0) { Fail 'Building HeftTests failed.' }
    & $testsExe
    if ($LASTEXITCODE -ne 0) { Fail 'Some unit tests failed. The plugin was NOT installed.' }
}

# --- 4. Install ----------------------------------------------------------------
Step "Installing to $InstallDir"
New-Item -ItemType Directory -Force $InstallDir | Out-Null
$dest = Join-Path $InstallDir 'Heft.vst3'

if (Test-Path $dest) {
    # If REAPER has the plugin loaded, the DLL is locked: detect that before deleting anything.
    $dll = Join-Path $dest 'Contents\x86_64-win\Heft.vst3'
    if (Test-Path $dll) {
        try   { [IO.File]::Open($dll, 'Open', 'ReadWrite', 'None').Close() }
        catch { Fail 'Heft.vst3 is in use (is REAPER open with the plugin loaded?). Close REAPER and run build.bat again.' }
    }
    Remove-Item -Recurse -Force $dest
}

Copy-Item -Recurse $pluginBuilt $dest

Write-Host ''
Write-Host "Done in $([int]$timer.Elapsed.TotalSeconds) s. Plugin installed at:" -ForegroundColor Green
Write-Host "  $dest"
Write-Host 'If REAPER already knew it, just reopen the project. If it is new: Options > Preferences > Plug-ins > VST > Re-scan.'
