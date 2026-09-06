#Requires -Version 5.1
<#
.SYNOPSIS
  Hyper-V-free tests for Scylla workspace path rules (v0.0.1).
#>
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

. (Join-Path (Split-Path -Parent $PSScriptRoot) 'AgentCage.Common.ps1')

$failed = 0
function Expect-False($cond, $name) {
    if ($cond) { Write-Host "FAIL  $name"; $script:failed++ } else { Write-Host "OK    $name" }
}
function Expect-True($cond, $name) {
    if (-not $cond) { Write-Host "FAIL  $name"; $script:failed++ } else { Write-Host "OK    $name" }
}

$root = Join-Path $env:TEMP ("ScyllaValRoot-" + [Guid]::NewGuid().ToString('N'))
$proj = Join-Path $root 'synq'
$sibling = Join-Path $root 'jarvis'
$outside = Join-Path $env:TEMP ("ScyllaValOutside-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $proj, $sibling, $outside | Out-Null
'ok' | Set-Content -LiteralPath (Join-Path $proj 'readme.txt')

$r = Resolve-ScyllaWorkspace -Requested $proj -WorkspaceRoot $root
Expect-True $r.Ok "valid project under root ($($r.Path)) reason=$($r.Reason)"

$r = Resolve-ScyllaWorkspace -Requested $root -WorkspaceRoot $root
Expect-False $r.Ok "refuse sharing the workspace root itself"

$r = Resolve-ScyllaWorkspace -Requested $outside -WorkspaceRoot $root
Expect-False $r.Ok "refuse path outside root"

$r = Resolve-ScyllaWorkspace -Requested (Join-Path $proj '..\..\..') -WorkspaceRoot $root
Expect-False $r.Ok "refuse .. traversal that escapes"

$r = Resolve-ScyllaWorkspace -Requested (Join-Path $proj '..\jarvis') -WorkspaceRoot $root
Expect-True $r.Ok "sibling under root is a different project (valid path, not the original)"
if ($r.Ok) {
    Expect-True ($r.Path -eq (Get-ScyllaCanonicalPath $sibling)) "sibling canonicalizes to jarvis"
}

$r = Resolve-ScyllaWorkspace -Requested '\\NAS\Projects\synq' -WorkspaceRoot $root
Expect-False $r.Ok "refuse UNC"

$r = Resolve-ScyllaWorkspace -Requested 'D:\Clients\Acme' -WorkspaceRoot $root
Expect-False $r.Ok "refuse unrelated drive path when outside this test root"

$junction = Join-Path $proj 'escape'
cmd /c "mklink /J `"$junction`" `"$outside`"" | Out-Null
$r = Resolve-ScyllaWorkspace -Requested $proj -WorkspaceRoot $root
Expect-False $r.Ok "refuse workspace containing junction that escapes ($($r.Reason))"

cmd /c "rmdir `"$junction`"" | Out-Null

$r = Resolve-ScyllaLaunchTarget -Target 'synq' -WorkspaceRoot $root
Expect-True ($r -eq (Join-Path $root 'synq')) "profile name joins workspaceRoot"

Remove-Item -LiteralPath $root, $outside -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($failed -gt 0) {
    Write-Host "SCYLLA: VALIDATION TESTS FAILED ($failed)"
    exit 1
}
Write-Host 'SCYLLA: VALIDATION TESTS OK'
exit 0
