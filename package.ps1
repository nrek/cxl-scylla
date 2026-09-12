[CmdletBinding()]
param(
    [string]$Version = "1.0.0",
    [ValidateSet("Release", "Debug", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$StrataSource = "",
    [string]$StrataPayload = "",
    [switch]$SkipApplicationBuild,
    [switch]$PreflightOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not $StrataSource) {
    $StrataSource = Join-Path (Split-Path -Parent $PSScriptRoot) "cxl-strata"
}

function Find-CommandPath([string]$Name, [string[]]$Candidates) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command -and $command.Source) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Required tool '$Name' was not found. Install it and retry."
}

function Add-ToolDirectory([string]$Executable) {
    $directory = Split-Path -Parent $Executable
    if (($env:PATH -split ';') -notcontains $directory) { $env:PATH = "$directory;$env:PATH" }
}

$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
$localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
$roamingAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData)
$dotnet = Find-CommandPath "dotnet" @("C:\Program Files\dotnet\dotnet.exe")
$uv = Find-CommandPath "uv" @(
    (Join-Path $userProfile ".local\bin\uv.exe"),
    (Join-Path $localAppData "Microsoft\WinGet\Links\uv.exe")
)
$managedPython = Get-ChildItem (Join-Path $roamingAppData "uv\python") -Filter python.exe -Recurse -File `
    -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
$python = Find-CommandPath "python" @(
    $managedPython,
    (Join-Path $localAppData "Programs\Python\Python311\python.exe")
)

$cmakeCandidates = @(
    "C:\Program Files\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
$programFilesX86 = ${env:ProgramFiles(x86)}
if (-not $programFilesX86) { $programFilesX86 = "C:\Program Files (x86)" }
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsRoot) { $cmakeCandidates += Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }
}
$cmake = Find-CommandPath "cmake" $cmakeCandidates
Add-ToolDirectory $dotnet
Add-ToolDirectory $cmake

$strataRoot = (Resolve-Path -LiteralPath $StrataSource).Path
$strataBuild = Join-Path $strataRoot "scripts\build-windows-standalone.ps1"
if (-not (Test-Path -LiteralPath $strataBuild -PathType Leaf)) { throw "STRATA builder not found: $strataBuild" }
if ($PreflightOnly) {
    Write-Host "Packaging preflight passed."
    Write-Host "dotnet: $dotnet"
    Write-Host "cmake:  $cmake"
    Write-Host "python: $python"
    Write-Host "uv:     $uv"
    return
}
if (-not $StrataPayload) {
    Write-Host "Building standalone STRATA..."
    $StrataPayload = & $strataBuild -Python $python -Uv $uv
}

$installerBuild = Join-Path $PSScriptRoot "installer\build-installer.ps1"
Write-Host "Building Scylla MSI version $Version..."
& $installerBuild -Version $Version -Configuration $Configuration -StrataSource $strataRoot `
    -StrataPayload $StrataPayload -SkipApplicationBuild:$SkipApplicationBuild

$msi = Join-Path $PSScriptRoot "installer\artifacts\ScyllaSetup.msi"
if (-not (Test-Path -LiteralPath $msi -PathType Leaf)) { throw "Packaging completed without producing $msi" }
Write-Host "Package ready: $msi"
