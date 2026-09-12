param([int]$WaitForProcessId = 0)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = Join-Path $projectRoot '.tmpcl/terminal-build'
$target = Join-Path $projectRoot 'scylla-fluent/bin/x64/Release/net8.0-windows10.0.19041.0'
$files = @('scylla.dll', 'scylla.pdb', 'scylla-core.dll', 'scylla-broker.exe')
$log = Join-Path $source 'install-terminal.log'
try {
    foreach ($name in $files) {
        if (!(Test-Path -LiteralPath (Join-Path $source $name))) { throw "Missing staged file: $name" }
    }
    if ($WaitForProcessId -gt 0) {
        $running = Get-Process -Id $WaitForProcessId -ErrorAction SilentlyContinue
        if ($running) { $running.WaitForExit() }
    }
    # Never stop the user's app or a shell session. Wait for a normal shutdown.
    while (Get-Process -Name scylla,scylla-broker -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $target }) {
        Start-Sleep -Seconds 1
    }
    $backup = Join-Path $source ('previous-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    foreach ($name in $files) {
        $dest = Join-Path $target $name
        if (Test-Path -LiteralPath $dest) { Copy-Item -LiteralPath $dest -Destination (Join-Path $backup $name) }
    }
    try {
        foreach ($name in $files) {
            $src = Join-Path $source $name
            $dest = Join-Path $target $name
            Copy-Item -LiteralPath $src -Destination $dest -Force
            if ((Get-FileHash -LiteralPath $src).Hash -ne (Get-FileHash -LiteralPath $dest).Hash) {
                throw "Hash mismatch: $name"
            }
        }
    }
    catch {
        foreach ($name in $files) {
            $saved = Join-Path $backup $name
            if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination (Join-Path $target $name) -Force }
        }
        throw
    }
    "PASS: terminal fix installed in $target; backup: $backup" | Set-Content -LiteralPath $log
}
catch {
    "FAIL: $_" | Set-Content -LiteralPath $log
    throw
}
