[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$Version = "1.0.0",
    [string]$StrataSource = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "cxl-strata"),
    [string]$StrataPayload = "",
    [string]$CMakeGenerator = "",
    [switch]$SkipApplicationBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$payload = Join-Path $PSScriptRoot "payload"
$scyllaPayload = Join-Path $payload "scylla"
$strataPayloadDir = Join-Path $payload "strata"
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
    if (-not $CMakeGenerator) {
        $generatorLine = & cmake --help | Select-String '^\*?\s+(Visual Studio \d+ \d+)' | Select-Object -First 1
        if ($generatorLine -and $generatorLine.Line -match '^\*?\s+(Visual Studio \d+ \d+)') { $CMakeGenerator = $Matches[1] }
        if (-not $CMakeGenerator) { throw "No supported Visual Studio CMake generator was found." }
    }
    $packageBuild = Join-Path $repo "build\package"
    cmake -S $repo -B $packageBuild -G $CMakeGenerator -A x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }
    cmake --build $packageBuild --config $Configuration --target scylla-core-shared
    if ($LASTEXITCODE -ne 0) { throw "Native Scylla build failed with exit code $LASTEXITCODE." }
    if (Test-Path -LiteralPath $scyllaPayload) {
        Remove-Item -LiteralPath $scyllaPayload -Recurse -Force
    }
    dotnet publish (Join-Path $repo "scylla-fluent\Scylla.csproj") -c $Configuration -r win-x64 --self-contained true `
        -p:Platform=x64 -p:ScyllaNativeOutputDir="$(Join-Path $packageBuild $Configuration)" -o $scyllaPayload
    if ($LASTEXITCODE -ne 0) { throw "Scylla publish failed with exit code $LASTEXITCODE." }
}

if (-not (Test-Path -LiteralPath (Join-Path $scyllaPayload "scylla.exe") -PathType Leaf)) {
    throw "Scylla publish output is missing scylla.exe. Run without -SkipApplicationBuild first."
}
if (Test-Path -LiteralPath (Join-Path $scyllaPayload "scylla-workbench.exe") -PathType Leaf) {
    throw "Deprecated scylla-workbench.exe is present in the staging payload. Rebuild without -SkipApplicationBuild."
}

if (Test-Path -LiteralPath $strataPayloadDir) {
    Remove-Item -LiteralPath $strataPayloadDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $strataPayloadDir, $artifacts | Out-Null
Copy-Item -Path (Join-Path $strataSource "*") -Destination $strataPayloadDir -Recurse -Force

dotnet build (Join-Path $PSScriptRoot "Scylla.Installer.wixproj") -c $Configuration `
    -p:Version=$Version -p:PublishDir=$scyllaPayload -p:StrataDir=$strataPayloadDir `
    -o $artifacts
if ($LASTEXITCODE -ne 0) { throw "MSI build failed with exit code $LASTEXITCODE." }

Write-Host "Created $(Join-Path $artifacts "ScyllaSetup.msi")"
