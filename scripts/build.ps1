# Configures and builds LAB X-19 (VST3, Standalone and the LabX19Render test harness).
param(
    [string]$Config = "Release",
    [string]$JucePath = "F:/_DEV/tools/JUCE"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = "F:\_DEV\tools\cmake\cmake-4.4.4-windows-x86_64\bin\cmake.exe" }

& $cmake -S $root -B "$root\build" -G "Visual Studio 17 2022" -A x64 "-DLABX19_JUCE_PATH=$JucePath"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build "$root\build" --config $Config --target LabX19_VST3 LabX19_Standalone LabX19Render --parallel
exit $LASTEXITCODE
