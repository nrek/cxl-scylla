#Requires -Version 5.1
<#
.SYNOPSIS
  Read-only host preflight for Scylla v0.0.1. Does not mutate the host.
.PARAMETER Workspace
  Optional project path or profile name (e.g. scylla-test or D:\AgentWork\scylla-test).
.EXAMPLE
  .\scripts\Test-AgentCageHost.ps1
  .\scripts\Test-AgentCageHost.ps1 -Workspace scylla-test
#>
[CmdletBinding()]
param(
    [string]$Workspace
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Continue'

. (Join-Path $PSScriptRoot 'AgentCage.Common.ps1')

$cfg = Get-ScyllaConfig
$fail = New-Object System.Collections.Generic.List[string]
$warn = New-Object System.Collections.Generic.List[string]
$ok = New-Object System.Collections.Generic.List[string]

function Add-Check {
    param([bool]$Pass, [string]$OkMsg, [string]$FailMsg, [switch]$WarningOnly)
    if ($Pass) { $ok.Add($OkMsg) | Out-Null }
    elseif ($WarningOnly) { $warn.Add($FailMsg) | Out-Null }
    else { $fail.Add($FailMsg) | Out-Null }
}

Write-ScyllaLog "Config: $($cfg._configPath)"
Write-ScyllaLog "Read-only inspection. No Hyper-V, SMB, ACL, or account changes."

$ed = Get-ScyllaWindowsEdition
Add-Check -Pass $ed.HyperVSkuOk `
    -OkMsg "Windows edition supports Hyper-V ($($ed.ProductName) / $($ed.EditionId))" `
    -FailMsg "Windows edition is not Pro/Enterprise/Education ($($ed.ProductName) / $($ed.EditionId)). Hyper-V is not a supported SKU. Upgrade this host or run Phase A on a Pro/Enterprise machine."

$admin = Test-ScyllaIsAdministrator
Add-Check -Pass $admin `
    -OkMsg 'Current session is elevated (needed later for share/ACL/VM cmdlets)' `
    -FailMsg 'Current session is not elevated. Preflight can still inspect; launch/stop will require an elevated prompt.' `
    -WarningOnly

$hv = Test-ScyllaHyperVCmdletsPresent
Add-Check -Pass $hv.Ok `
    -OkMsg 'Hyper-V PowerShell cmdlets are available' `
    -FailMsg "Hyper-V cmdlets missing: $($hv.Missing -join ', '). Install Hyper-V management tools on a supported edition. This script will not enable the feature."

$vmms = Get-Service -Name vmms -ErrorAction SilentlyContinue
Add-Check -Pass ($null -ne $vmms -and $vmms.Status -eq 'Running') `
    -OkMsg 'Hyper-V VMMS service is running' `
    -FailMsg 'Hyper-V VMMS service is not running (or not installed).'

$root = $null
try { $root = Get-ScyllaCanonicalPath -Path $cfg.workspaceRoot } catch { $root = $cfg.workspaceRoot }
Add-Check -Pass (Test-Path -LiteralPath $cfg.workspaceRoot -PathType Container) `
    -OkMsg "Workspace root exists ($($cfg.workspaceRoot))" `
    -FailMsg "Workspace root missing: $($cfg.workspaceRoot). Create it manually. This script will not create it."

$acct = $cfg.accessAccount
$acctExists = Get-ScyllaLocalUserExists -Name $acct
Add-Check -Pass $acctExists `
    -OkMsg "Share identity exists ($acct)" `
    -FailMsg "Share identity missing: $acct. Create it manually as a non-admin local account. This script will not create it."

if ($acctExists) {
    $isAdmin = Test-ScyllaAccountIsAdmin -Name $acct
    Add-Check -Pass (-not $isAdmin) `
        -OkMsg "$acct is not in Administrators" `
        -FailMsg "$acct is a member of Administrators. The guest must never receive a host admin identity."
}

$share = Get-ScyllaShareOrNull -Name $cfg.shareName
if ($null -eq $share) {
    $ok.Add("No stale SMB share named $($cfg.shareName)") | Out-Null
} else {
    $warn.Add("Stale share present: $($cfg.shareName) -> $($share.Path). launch will refuse unless it can remove this share first.") | Out-Null
}

$vmOk = $false
$vmObj = $null
if ($hv.Ok) {
    try {
        $vmObj = Get-VM -Name $cfg.vmName -ErrorAction Stop
        $vmOk = $true
        $ok.Add("VM $($cfg.vmName) exists (state=$($vmObj.State))") | Out-Null
    } catch {
        $fail.Add("Hyper-V VM '$($cfg.vmName)' was not found. Complete host bootstrap first. This script will not create the VM.") | Out-Null
    }
} else {
    $fail.Add("Cannot query VM '$($cfg.vmName)' without Hyper-V cmdlets.") | Out-Null
}

if ($vmOk) {
    try {
        $snaps = @(Get-VMSnapshot -VMName $cfg.vmName -ErrorAction Stop)
        $baseName = 'AGENT-CAGE-BASELINE'
        if ($cfg.checkpoint -and $cfg.checkpoint.baselineName) { $baseName = [string]$cfg.checkpoint.baselineName }
        $hasBase = $false
        foreach ($s in $snaps) { if ($s.Name -eq $baseName) { $hasBase = $true } }
        Add-Check -Pass $hasBase `
            -OkMsg "Baseline checkpoint present ($baseName)" `
            -FailMsg "Baseline checkpoint missing ($baseName). Create it after guest tooling + firewall, with no workspace mounted."
    } catch {
        $fail.Add("Could not list checkpoints: $($_.Exception.Message)") | Out-Null
    }

    try {
        $svc = @(Get-VMIntegrationService -VMName $cfg.vmName -ErrorAction Stop)
        foreach ($s in $svc) {
            $name = [string]$s.Name
            $enabled = [bool]$s.Enabled
            if ($name -match 'Guest Service Interface') {
                Add-Check -Pass $enabled `
                    -OkMsg 'Guest Service Interface enabled (PowerShell Direct)' `
                    -FailMsg 'Guest Service Interface is disabled. PowerShell Direct launch will fail until it is enabled on the VM.' `
                    -WarningOnly
            }
            if ($name -match 'Copy' -or $name -match 'Clipboard') {
                if ($enabled) {
                    $warn.Add("Integration service enabled (host-data channel): $name. Disable clipboard/drive redirection for cage operation.") | Out-Null
                }
            }
        }
    } catch {
        $warn.Add("Could not inspect VM integration services: $($_.Exception.Message)") | Out-Null
    }

    try {
        $dvd = Get-VMHardDiskDrive -VMName $cfg.vmName -ErrorAction SilentlyContinue
        if ($dvd) { $ok.Add("VM has disk attached") | Out-Null }
    } catch { }
}

if ($Workspace) {
    $resolved = Resolve-ScyllaLaunchTarget -Target $Workspace -WorkspaceRoot $cfg.workspaceRoot
    $val = Resolve-ScyllaWorkspace -Requested $resolved -WorkspaceRoot $cfg.workspaceRoot
    Add-Check -Pass ([bool]$val.Ok) `
        -OkMsg "Workspace valid: $($val.Path)" `
        -FailMsg "Workspace invalid ($resolved): $($val.Reason)"
}

Write-Host ''
Write-Host '--- PASS ---'
if ($ok.Count -eq 0) { Write-Host '(none)' } else { $ok | ForEach-Object { Write-Host "  OK    $_" } }
Write-Host '--- WARN ---'
if ($warn.Count -eq 0) { Write-Host '(none)' } else { $warn | ForEach-Object { Write-Host "  WARN  $_" } }
Write-Host '--- FAIL ---'
if ($fail.Count -eq 0) { Write-Host '(none)' } else { $fail | ForEach-Object { Write-Host "  FAIL  $_" } }
Write-Host ''

if ($fail.Count -gt 0) {
    Write-ScyllaHostNotReady 'Phase A host bootstrap is incomplete. See FAIL lines. Scripts will not enable Hyper-V, create AGENT-CAGE, or create AgentCageAccess.'
    exit 2
}

Write-ScyllaLog -Level OK 'Host preflight passed (read-only).'
exit 0
