[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0",
    [string]$StrataSource = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "cxl-strata"),
    [string]$StrataPayload = "",
    [switch]$SkipApplicationBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$payload = Join-Path $PSScriptRoot "payload"
$scyllaPayload = Join-Path $payload "scylla"
$strataPayload = Join-Path $payload "strata"
$artifacts = Join-Path $PSScriptRoot "artifacts"

if (-not $StrataPayload) {
    $strataBuilder = Join-Path (Resolve-Path -LiteralPath $StrataSource).Path "scripts\build-windows-standalone.ps1"
    if (-not (Test-Path -LiteralPath $strataBuilder -PathType Leaf)) { throw "Could not find cxl-strata builder: $strataBuilder" }
    $StrataPayload = & $strataBuilder
}
$strataSource = (Resolve-Path -LiteralPath $StrataPayload).Path
if (-not (Test-Path -LiteralPath (Join-Path $strataSource "strata.exe") -PathType Leaf)) {
    throw "STRATA payload must contain strata.exe at its root."
}

if (-not $SkipApplicationBuild) {
    cmake -S $repo -B (Join-Path $repo "build") -G "Visual Studio 17 2022" -A x64
    cmake --build (Join-Path $repo "build") --config $Configuration --target scylla-core-shared
    dotnet publish (Join-Path $repo "scylla-fluent\Scylla.csproj") -c $Configuration -r win-x64 --self-contained true `
        -p:Platform=x64 -p:ScyllaNativeOutputDir="$(Join-Path $repo "build\$Configuration")" -o $scyllaPayload
}

if (-not (Test-Path -LiteralPath (Join-Path $scyllaPayload "scylla.exe") -PathType Leaf)) {
    throw "Scylla publish output is missing scylla.exe. Run without -SkipApplicationBuild first."
}

New-Item -ItemType Directory -Force -Path $strataPayload, $artifacts | Out-Null
Copy-Item -Path (Join-Path $strataSource "*") -Destination $strataPayload -Recurse -Force

dotnet build (Join-Path $PSScriptRoot "Scylla.Installer.wixproj") -c $Configuration `
    -p:Version=$Version -p:PublishDir=$scyllaPayload -p:StrataDir=$strataPayload `
    -o $artifacts

Write-Host "Created $(Join-Path $artifacts "ScyllaSetup.msi")"
