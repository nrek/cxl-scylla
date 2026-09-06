#Requires -Version 5.1
<#
.SYNOPSIS
  Scylla v0.0.1 PowerShell PoC. Does not create the VM or AgentCageAccess.
.EXAMPLE
  .\scripts\AgentCage.ps1 status
  .\scripts\AgentCage.ps1 launch scylla-test
  .\scripts\AgentCage.ps1 launch scylla-test -App cursor
  .\scripts\AgentCage.ps1 stop
  .\scripts\AgentCage.ps1 restore-baseline
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('launch', 'status', 'stop', 'restore-baseline')]
    [string]$Command = 'status',

    [Parameter(Position = 1)]
    [string]$Target,

    [ValidateSet('cursor', 'codex', 'both', 'none')]
    [string]$App
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'AgentCage.Common.ps1')

$cfg = Get-ScyllaConfig
if (-not $App) {
    if ($cfg.defaultApplication) { $App = [string]$cfg.defaultApplication } else { $App = 'cursor' }
}

function Assert-ScyllaElevated {
    if (-not (Test-ScyllaIsAdministrator)) {
        Write-ScyllaHostNotReady 'An elevated PowerShell session is required for launch/stop/restore-baseline (SMB share, ACL, Hyper-V).'
        exit 2
    }
}

function Assert-ScyllaHyperV {
    $hv = Test-ScyllaHyperVCmdletsPresent
    if (-not $hv.Ok) {
        Write-ScyllaHostNotReady "Hyper-V cmdlets missing: $($hv.Missing -join ', '). Complete Phase A on Windows 11 Pro/Enterprise. This script will not enable Hyper-V."
        exit 2
    }
}

function Get-ScyllaVmOrFail {
    try {
        return Get-VM -Name $cfg.vmName -ErrorAction Stop
    } catch {
        Write-ScyllaHostNotReady "Hyper-V VM '$($cfg.vmName)' was not found. Complete host bootstrap first."
        exit 2
    }
}

function Wait-ScyllaGuestReady {
    param(
        [Parameter(Mandatory)]$GuestCredential,
        [int]$TimeoutSeconds = 180
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $last = 'no attempt yet'
    while ((Get-Date) -lt $deadline) {
        try {
            $r = Invoke-Command -VMName $cfg.vmName -Credential $GuestCredential -ScriptBlock { 'ready' } -ErrorAction Stop
            if ($r -eq 'ready') { return }
            $last = "unexpected result: $r"
        } catch {
            $last = $_.Exception.Message
        }
        Start-Sleep -Seconds 3
    }
    throw "Guest did not become ready via PowerShell Direct within ${TimeoutSeconds}s. Last error: $last"
}

function Invoke-ScyllaRollbackShare {
    param([string]$WorkspacePath)
    try {
        $existing = Get-ScyllaShareOrNull -Name $cfg.shareName
        if ($existing) {
            Remove-SmbShare -Name $cfg.shareName -Force -ErrorAction Stop
            Write-ScyllaLog "Removed share $($cfg.shareName)"
        }
    } catch {
        Write-ScyllaLog -Level WARN "Could not remove share: $($_.Exception.Message)"
    }
    if ($WorkspacePath) {
        try {
            $acct = "$env:COMPUTERNAME\$($cfg.accessAccount)"
            icacls $WorkspacePath /remove $acct 2>$null | Out-Null
        } catch { }
    }
}

function Invoke-ScyllaStatus {
    $ed = Get-ScyllaWindowsEdition
    $hv = Test-ScyllaHyperVCmdletsPresent
    $share = Get-ScyllaShareOrNull -Name $cfg.shareName
    $vmState = 'unknown (no Hyper-V cmdlets)'
    $checkpoint = 'unknown'
    if ($hv.Ok) {
        try {
            $vm = Get-VM -Name $cfg.vmName -ErrorAction Stop
            $vmState = [string]$vm.State
        } catch {
            $vmState = 'MISSING'
        }
        try {
            $baseName = 'AGENT-CAGE-BASELINE'
            if ($cfg.checkpoint -and $cfg.checkpoint.baselineName) { $baseName = [string]$cfg.checkpoint.baselineName }
            $snaps = @(Get-VMSnapshot -VMName $cfg.vmName -ErrorAction Stop)
            $checkpoint = 'missing'
            foreach ($s in $snaps) { if ($s.Name -eq $baseName) { $checkpoint = "present ($baseName)" } }
        } catch {
            $checkpoint = 'unavailable'
        }
    }
    $hostWs = if ($share) { $share.Path } else { '(none)' }
    Write-Host "VM: $($cfg.vmName)"
    Write-Host "VM state: $vmState"
    Write-Host "Share: $($cfg.shareName)"
    Write-Host "Host workspace: $hostWs"
    Write-Host "Guest mapping: $($cfg.guestWorkspace)"
    Write-Host "Internet policy: expected allow=$($cfg.network.internet) (not auto-probed from host)"
    Write-Host "LAN policy: expected allow=$($cfg.network.lan) (not auto-probed; no guessed IPs)"
    Write-Host "Baseline checkpoint: $checkpoint"
    Write-Host "Windows edition: $($ed.ProductName) ($($ed.EditionId)) HyperVSkuOk=$($ed.HyperVSkuOk)"
    Write-Host "Config: $($cfg._configPath)"
}

function Invoke-ScyllaStop {
    Assert-ScyllaElevated
    Assert-ScyllaHyperV
    $vm = $null
    try { $vm = Get-VM -Name $cfg.vmName -ErrorAction Stop } catch { }

    $share = Get-ScyllaShareOrNull -Name $cfg.shareName
    $sharePath = if ($share) { $share.Path } else { $null }

    if ($vm -and $vm.State -ne 'Off') {
        try {
            $guestCred = $script:ScyllaGuestCredential
            if ($guestCred) {
                Invoke-Command -VMName $cfg.vmName -Credential $guestCred -ErrorAction SilentlyContinue -ScriptBlock {
                    param($Drive)
                    $ws = Join-Path $Drive 'Workspace'
                    if (Test-Path -LiteralPath $ws) { cmd /c "rmdir `"$ws`"" | Out-Null }
                    cmd /c "net use $Drive /delete /y" | Out-Null
                    cmd /c "subst $Drive /d" | Out-Null
                    cmd /c "net use Y: /delete /y" | Out-Null
                } -ArgumentList $cfg.guestDrive.TrimEnd('\')
            }
        } catch {
            Write-ScyllaLog -Level WARN "Guest unmount skipped: $($_.Exception.Message)"
        }
        Write-ScyllaLog "Stopping $($cfg.vmName)"
        Stop-VM -Name $cfg.vmName -Force -ErrorAction Continue
    } else {
        Write-ScyllaLog "VM already off or missing"
    }

    Invoke-ScyllaRollbackShare -WorkspacePath $sharePath
    $script:ScyllaSharePassword = $null
    $script:ScyllaGuestCredential = $null
    Write-ScyllaLog -Level OK 'stop complete (idempotent).'
    Invoke-ScyllaStatus
}

function Invoke-ScyllaRestoreBaseline {
    Assert-ScyllaElevated
    Assert-ScyllaHyperV
    $null = Get-ScyllaVmOrFail
    $baseName = 'AGENT-CAGE-BASELINE'
    if ($cfg.checkpoint -and $cfg.checkpoint.baselineName) { $baseName = [string]$cfg.checkpoint.baselineName }
    Invoke-ScyllaStop
    Write-ScyllaLog "Restoring snapshot $baseName"
    Restore-VMSnapshot -Name $baseName -VMName $cfg.vmName -Confirm:$false
    Write-ScyllaLog -Level OK "Restored $baseName"
}

function Invoke-ScyllaLaunch {
    param([Parameter(Mandatory)][string]$LaunchTarget)

    Assert-ScyllaElevated
    Assert-ScyllaHyperV
    $ed = Get-ScyllaWindowsEdition
    if (-not $ed.HyperVSkuOk) {
        Write-ScyllaHostNotReady "Windows edition $($ed.ProductName) ($($ed.EditionId)) does not support Hyper-V. Use Pro/Enterprise."
        exit 2
    }

    $requested = Resolve-ScyllaLaunchTarget -Target $LaunchTarget -WorkspaceRoot $cfg.workspaceRoot
    $val = Resolve-ScyllaWorkspace -Requested $requested -WorkspaceRoot $cfg.workspaceRoot
    if (-not $val.Ok) {
        Write-ScyllaRefuse $val.Reason
        exit 2
    }
    $workspace = $val.Path
    Write-ScyllaLog "Workspace canonical: $workspace"

    if (-not (Get-ScyllaLocalUserExists -Name $cfg.accessAccount)) {
        Write-ScyllaHostNotReady "Share identity '$($cfg.accessAccount)' was not found. Create it manually (non-admin). This script will not create it."
        exit 2
    }
    if (Test-ScyllaAccountIsAdmin -Name $cfg.accessAccount) {
        Write-ScyllaRefuse "$($cfg.accessAccount) is in Administrators. Refusing to expose a workspace with a host-admin share identity."
        exit 2
    }

    $null = Get-ScyllaVmOrFail

    $stale = Get-ScyllaShareOrNull -Name $cfg.shareName
    if ($stale) {
        Write-ScyllaLog "Removing stale share $($cfg.shareName) -> $($stale.Path)"
        try {
            Remove-SmbShare -Name $cfg.shareName -Force -ErrorAction Stop
        } catch {
            Write-ScyllaRefuse "Existing $($cfg.shareName) share could not be safely cleared. No new workspace was exposed. $($_.Exception.Message)"
            exit 2
        }
    }

    $acct = "$env:COMPUTERNAME\$($cfg.accessAccount)"
    $shareCreated = $false
    try {
        Write-ScyllaLog "Granting Modify to $acct on $workspace"
        $grant = icacls $workspace /grant "${acct}:(OI)(CI)M" 2>&1
        if ($LASTEXITCODE -ne 0) { throw "icacls failed: $grant" }

        Write-ScyllaLog "Creating SMB share $($cfg.shareName)"
        New-SmbShare -Name $cfg.shareName -Path $workspace -FullAccess $acct -FolderEnumerationMode AccessBased | Out-Null
        $shareCreated = $true
    } catch {
        Write-ScyllaLaunchFailed $_.Exception.Message
        Invoke-ScyllaRollbackShare -WorkspacePath $workspace
        exit 2
    }

    $vm = Get-VM -Name $cfg.vmName
    if ($vm.State -eq 'Off') {
        Write-ScyllaLog "Starting $($cfg.vmName)"
        Start-VM -Name $cfg.vmName
    }

    try {
        Enable-VMIntegrationService -VMName $cfg.vmName -Name 'Guest Service Interface' -ErrorAction SilentlyContinue
    } catch { }

    if (-not $script:ScyllaGuestCredential) {
        Write-Host "Enter GUEST credential for PowerShell Direct (non-admin 'agent' account)."
        $script:ScyllaGuestCredential = Get-Credential -Message "AGENT-CAGE guest account (e.g. AGENT-CAGE\agent)"
    }
    if (-not $script:ScyllaSharePassword) {
        Write-Host "Enter HOST share password for $($cfg.accessAccount). It is held in this process only."
        $shareCred = Get-Credential -UserName $acct -Message "Host share identity $($cfg.accessAccount)"
        $script:ScyllaSharePassword = $shareCred.GetNetworkCredential().Password
    }

    try {
        Wait-ScyllaGuestReady -GuestCredential $script:ScyllaGuestCredential
    } catch {
        Write-ScyllaLaunchFailed $_.Exception.Message
        Write-ScyllaLog -Level WARN 'Workspace share remains until stop, unless rollback succeeds.'
        Invoke-ScyllaRollbackShare -WorkspacePath $workspace
        exit 2
    }

    $hostName = $env:COMPUTERNAME
    $guestDrive = $cfg.guestDrive.TrimEnd('\')
    if (-not $guestDrive.EndsWith(':')) { $guestDrive = $guestDrive + ':' }
    $shareName = $cfg.shareName
    $shareUser = $acct
    $sharePass = $script:ScyllaSharePassword
    $guestWs = $cfg.guestWorkspace
    $appChoice = $App

    try {
        $mount = Invoke-Command -VMName $cfg.vmName -Credential $script:ScyllaGuestCredential -ArgumentList $hostName, $shareName, $guestDrive, $shareUser, $sharePass, $guestWs, $appChoice -ScriptBlock {
            param($HostName, $ShareName, $Drive, $User, $Password, $GuestWorkspace, $App)
            $ErrorActionPreference = 'Stop'
            $unc = "\\$HostName\$ShareName"
            # Raw SMB on a helper letter so W:\Workspace is a junction, not a folder inside the project.
            $raw = 'Y:'
            cmd /c "net use $raw /delete /y" 2>$null | Out-Null
            cmd /c "net use $Drive /delete /y" 2>$null | Out-Null
            cmd /c "subst $Drive /d" 2>$null | Out-Null
            $net = cmd /c "net use $raw $unc /user:$User $Password /persistent:no"
            if ($LASTEXITCODE -ne 0) { throw "net use $raw failed: $net" }

            $mountRoot = Join-Path $env:ProgramData 'Scylla\W'
            New-Item -ItemType Directory -Path $mountRoot -Force | Out-Null
            $subst = cmd /c "subst $Drive `"$mountRoot`""
            if ($LASTEXITCODE -ne 0) { throw "subst $Drive failed: $subst" }

            $ws = $GuestWorkspace
            if (Test-Path -LiteralPath $ws) {
                cmd /c "rmdir `"$ws`"" 2>$null | Out-Null
            }
            $link = cmd /c "mklink /J `"$ws`" $raw\"
            if (-not (Test-Path -LiteralPath $ws)) {
                throw "Guest mapping missing after junction: $ws ($link)"
            }

            $launched = @()
            $cursorExe = @(
                "$env:LOCALAPPDATA\Programs\cursor\Cursor.exe",
                "$env:ProgramFiles\Cursor\Cursor.exe"
            ) | Where-Object { Test-Path $_ } | Select-Object -First 1
            function Find-Codex {
                $cmd = Get-Command codex -ErrorAction SilentlyContinue
                if ($cmd) { return $cmd.Source }
                return $null
            }

            if ($App -eq 'cursor' -or $App -eq 'both') {
                if ($cursorExe) {
                    Start-Process -FilePath $cursorExe -ArgumentList "`"$ws`""
                    $launched += "Cursor $ws"
                } else {
                    $launched += 'Cursor NOT FOUND'
                }
            }
            if ($App -eq 'codex' -or $App -eq 'both') {
                $codex = Find-Codex
                if ($codex) {
                    Start-Process -FilePath $codex -ArgumentList "`"$ws`""
                    $launched += "Codex $ws"
                } else {
                    $launched += 'Codex NOT FOUND'
                }
            }

            [pscustomobject]@{
                Unc       = $unc
                Workspace = $ws
                Listing   = @(Get-ChildItem -LiteralPath $ws -ErrorAction SilentlyContinue | Select-Object -First 8 -ExpandProperty Name)
                Launched  = $launched
            }
        }
    } catch {
        Write-ScyllaLaunchFailed "The guest did not mount $($cfg.guestWorkspace). Workspace share has been removed. $($_.Exception.Message)"
        try {
            Invoke-Command -VMName $cfg.vmName -Credential $script:ScyllaGuestCredential -ErrorAction SilentlyContinue -ScriptBlock {
                param($Drive)
                cmd /c "net use $Drive /delete /y" | Out-Null
            } -ArgumentList $guestDrive
        } catch { }
        Invoke-ScyllaRollbackShare -WorkspacePath $workspace
        exit 2
    } finally {
        $sharePass = $null
    }

    Write-ScyllaLog -Level OK "Mounted $($mount.Unc) -> $($mount.Workspace)"
    Write-ScyllaLog "Guest listing: $($mount.Listing -join ', ')"
    Write-ScyllaLog "Apps: $($mount.Launched -join '; ')"
    Write-Host ''
    Write-Host "timestamp: $([DateTime]::UtcNow.ToString('o'))"
    Write-Host "command: launch"
    Write-Host "profile/path: $LaunchTarget"
    Write-Host "canonical workspace: $workspace"
    Write-Host "share: $($cfg.shareName) -> $workspace"
    Write-Host "guest mapping: $($mount.Workspace)"
    Write-Host "VM: $($cfg.vmName) state=$((Get-VM -Name $cfg.vmName).State)"
    Write-Host "checkpoint: (see status)"
    Write-Host "validation: OK"
}

switch ($Command) {
    'status' { Invoke-ScyllaStatus; exit 0 }
    'stop' { Invoke-ScyllaStop }
    'restore-baseline' { Invoke-ScyllaRestoreBaseline }
    'launch' {
        if ([string]::IsNullOrWhiteSpace($Target)) {
            Write-ScyllaRefuse 'launch requires a profile name or path. Example: .\scripts\AgentCage.ps1 launch scylla-test'
            exit 2
        }
        Invoke-ScyllaLaunch -LaunchTarget $Target
    }
}
