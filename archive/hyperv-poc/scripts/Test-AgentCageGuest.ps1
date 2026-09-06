#Requires -Version 5.1
<#
.SYNOPSIS
  Guest-side isolation verification. Run INSIDE AGENT-CAGE. Non-destructive.
.PARAMETER HostName
  Host computer name used for admin-share probes (\\HOST\C$). Default: default gateway reverse lookup skipped; uses -HostName or env SCYLLA_HOST.
.PARAMETER LanSmbTarget
  Optional UNC to a known LAN SMB path that must be unreachable. Not guessed.
.PARAMETER LanSshTarget
  Optional host:port for a known LAN SSH service that must be unreachable. Not guessed.
.EXAMPLE
  .\Test-AgentCageGuest.ps1 -HostName MONK
#>
[CmdletBinding()]
param(
    [string]$HostName = $env:SCYLLA_HOST,
    [string]$LanSmbTarget,
    [string]$LanSshTarget,
    [string]$WorkspacePath = 'W:\Workspace'
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Continue'

$pass = New-Object System.Collections.Generic.List[string]
$fail = New-Object System.Collections.Generic.List[string]
$skip = New-Object System.Collections.Generic.List[string]

function Expect-True {
    param([bool]$Condition, [string]$Ok, [string]$Bad)
    if ($Condition) { $pass.Add($Ok) | Out-Null } else { $fail.Add($Bad) | Out-Null }
}

function Expect-Unavailable {
    param([scriptblock]$Probe, [string]$Label)
    try {
        $result = & $Probe
        if ($result) {
            $fail.Add("$Label was reachable (must remain unavailable)") | Out-Null
        } else {
            $pass.Add("$Label unavailable") | Out-Null
        }
    } catch {
        $pass.Add("$Label unavailable ($($_.Exception.Message.Split([char]10)[0]))") | Out-Null
    }
}

Write-Host "Scylla guest isolation test"
Write-Host "WorkspacePath=$WorkspacePath HostName=$HostName"
Write-Host ''

$wsExists = Test-Path -LiteralPath $WorkspacePath
Expect-True -Condition $wsExists -Ok "$WorkspacePath exists" -Bad "$WorkspacePath is missing"

if ($wsExists) {
    try {
        $probeFile = Join-Path $WorkspacePath ("scylla-guest-probe-{0}.txt" -f [Guid]::NewGuid().ToString('N'))
        'scylla-guest-write' | Set-Content -LiteralPath $probeFile -Encoding UTF8
        $read = Get-Content -LiteralPath $probeFile -Raw
        Expect-True -Condition ($read -match 'scylla-guest-write') -Ok "read/write under $WorkspacePath" -Bad "read/write failed under $WorkspacePath"
        Remove-Item -LiteralPath $probeFile -Force -ErrorAction SilentlyContinue
    } catch {
        $fail.Add("read/write exception: $($_.Exception.Message)") | Out-Null
    }

    if (Get-Command git -ErrorAction SilentlyContinue) {
        Push-Location $WorkspacePath
        try {
            git rev-parse --is-inside-work-tree 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) {
                git status --porcelain=v1 2>&1 | Out-Null
                Expect-True -Condition ($LASTEXITCODE -eq 0) -Ok "git inspect ran in $WorkspacePath" -Bad "git status failed in $WorkspacePath"
            } else {
                $skip.Add("Git present but $WorkspacePath is not a repository") | Out-Null
            }
        } finally { Pop-Location }
    } else {
        $skip.Add('Git not installed in guest') | Out-Null
    }
}

try {
    $req = Invoke-WebRequest -Uri 'https://example.com' -UseBasicParsing -TimeoutSec 15
    Expect-True -Condition ($req.StatusCode -ge 200 -and $req.StatusCode -lt 400) -Ok 'Public Internet (https://example.com) works' -Bad "Public Internet failed (HTTP $($req.StatusCode))"
} catch {
    $fail.Add("Public Internet failed: $($_.Exception.Message)") | Out-Null
}

if ([string]::IsNullOrWhiteSpace($HostName)) {
    $skip.Add('HostName not set — skipping \\HOST\C$ / D$ / ADMIN$ probes. Pass -HostName <computer>.') | Out-Null
} else {
    foreach ($adminShare in @('C$', 'D$', 'ADMIN$')) {
        $unc = "\\$HostName\$adminShare"
        Expect-Unavailable -Label $unc -Probe {
            Test-Path -LiteralPath $unc
        }
    }
    foreach ($p in @("\\$HostName\Users", "\\$HostName\C$\Users")) {
        Expect-Unavailable -Label $p -Probe { Test-Path -LiteralPath $p }
    }
}

if ($LanSmbTarget) {
    Expect-Unavailable -Label "LAN SMB $LanSmbTarget" -Probe { Test-Path -LiteralPath $LanSmbTarget }
} else {
    $skip.Add('LAN SMB target not configured (will not guess RFC1918). Pass -LanSmbTarget \\server\share.') | Out-Null
}

if ($LanSshTarget) {
    $parts = $LanSshTarget.Split(':')
    $h = $parts[0]
    $port = 22
    if ($parts.Count -gt 1) { $port = [int]$parts[1] }
    Expect-Unavailable -Label "LAN SSH ${h}:${port}" -Probe {
        Test-NetConnection -ComputerName $h -Port $port -WarningAction SilentlyContinue | Select-Object -ExpandProperty TcpTestSucceeded
    }
} else {
    $skip.Add('LAN SSH target not configured. Pass -LanSshTarget host:22.') | Out-Null
}

# Do not probe operator profile paths on the host via guessed usernames.
$skip.Add('Host Documents/Downloads/.ssh are host-side; covered by admin-share probes plus Phase A manual checks.') | Out-Null

Write-Host ''
Write-Host '--- PASS ---'
if ($pass.Count -eq 0) { Write-Host '(none)' } else { $pass | ForEach-Object { Write-Host "  PASS  $_" } }
Write-Host '--- FAIL ---'
if ($fail.Count -eq 0) { Write-Host '(none)' } else { $fail | ForEach-Object { Write-Host "  FAIL  $_" } }
Write-Host '--- SKIP ---'
if ($skip.Count -eq 0) { Write-Host '(none)' } else { $skip | ForEach-Object { Write-Host "  SKIP  $_" } }
Write-Host ''

if ($fail.Count -gt 0) {
    Write-Host 'SCYLLA: GUEST BOUNDARY FAILED'
    exit 2
}
Write-Host 'SCYLLA: GUEST BOUNDARY OK'
exit 0
