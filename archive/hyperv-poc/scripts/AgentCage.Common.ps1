#Requires -Version 5.1
<#
.SYNOPSIS
  Shared Scylla v0.0.1 helpers. Dot-source only. Never stores passwords.
#>
Set-StrictMode -Version 2

# Captured when this file is dot-sourced ($PSScriptRoot = scripts/).
$script:ScyllaScriptsDir = $PSScriptRoot
$script:ScyllaRepoRoot = Split-Path -Parent $script:ScyllaScriptsDir

function Get-ScyllaRepoRoot {
    return $script:ScyllaRepoRoot
}

function Write-ScyllaLog {
    param(
        [Parameter(Mandatory)][string]$Message,
        [ValidateSet('INFO', 'WARN', 'OK', 'FAIL')][string]$Level = 'INFO'
    )
    $ts = [DateTime]::UtcNow.ToString('o')
    Write-Host "[$ts] $Level  $Message"
}

function Write-ScyllaRefuse {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "SCYLLA: REFUSED"
    Write-Host $Message
}

function Write-ScyllaHostNotReady {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "SCYLLA: HOST NOT READY"
    Write-Host $Message
}

function Write-ScyllaLaunchFailed {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "SCYLLA: LAUNCH FAILED"
    Write-Host $Message
}

function Test-ScyllaIsAdministrator {
    $wid = [Security.Principal.WindowsIdentity]::GetCurrent()
    $wp = New-Object Security.Principal.WindowsPrincipal $wid
    return $wp.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-ScyllaWindowsEdition {
    try {
        $p = Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
        $editionId = [string]$p.EditionID
        $productName = [string]$p.ProductName
        $display = $null
        if ($p.PSObject.Properties['DisplayVersion']) { $display = [string]$p.DisplayVersion }
        $isHome = $editionId -match '^(Core|CoreN|CoreSingleLanguage|CoreCountrySpecific)$'
        $isSupported = $editionId -match '^(Professional|ProfessionalN|ProfessionalWorkstation|Enterprise|EnterpriseN|Education|EducationN|Server.*)$'
        return [pscustomobject]@{
            EditionId     = $editionId
            ProductName   = $productName
            DisplayVersion = $display
            IsHome        = [bool]$isHome
            HyperVSkuOk   = [bool]$isSupported
        }
    } catch {
        return [pscustomobject]@{
            EditionId     = 'unknown'
            ProductName   = 'unknown'
            DisplayVersion = $null
            IsHome        = $false
            HyperVSkuOk   = $false
        }
    }
}

function Get-ScyllaConfig {
    param([string]$RepoRoot)
    if (-not $RepoRoot) { $RepoRoot = Get-ScyllaRepoRoot }
    $local = Join-Path $RepoRoot 'config\agent-cage.local.json'
    $example = Join-Path $RepoRoot 'config\agent-cage.example.json'
    $path = if (Test-Path -LiteralPath $local) { $local } else { $example }
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Config not found: $path"
    }
    $raw = Get-Content -LiteralPath $path -Raw -Encoding UTF8
    $cfg = $raw | ConvertFrom-Json
    if (-not $cfg.vmName) { throw 'Config missing vmName' }
    if (-not $cfg.workspaceRoot) { throw 'Config missing workspaceRoot' }
    if (-not $cfg.shareName) { throw 'Config missing shareName' }
    if (-not $cfg.accessAccount) { throw 'Config missing accessAccount' }
    if (-not $cfg.guestDrive) { $cfg | Add-Member -NotePropertyName guestDrive -NotePropertyValue 'W:' -Force }
    if (-not $cfg.guestWorkspace) { $cfg | Add-Member -NotePropertyName guestWorkspace -NotePropertyValue 'W:\Workspace' -Force }
    $cfg | Add-Member -NotePropertyName _configPath -NotePropertyValue $path -Force
    return $cfg
}

function Get-ScyllaCanonicalPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'Path is empty'
    }
    $trimmed = $Path.Trim()
    if ($trimmed.StartsWith('\\') -or $trimmed.StartsWith('//')) {
        throw 'UNC paths are rejected in v0.0.1'
    }
    $full = [System.IO.Path]::GetFullPath($trimmed)
    if ($full.StartsWith('\\?\')) { $full = $full.Substring(4) }
    if ($full.StartsWith('\\.\') -or $full.StartsWith('\\')) {
        throw "Rejected non-drive path: $full"
    }
    return $full.TrimEnd('\')
}

function Test-ScyllaIsUnderRoot {
    param(
        [Parameter(Mandatory)][string]$Candidate,
        [Parameter(Mandatory)][string]$Root
    )
    $c = Get-ScyllaCanonicalPath -Path $Candidate
    $r = Get-ScyllaCanonicalPath -Path $Root
    if ($c.Equals($r, [StringComparison]::OrdinalIgnoreCase)) { return $false }
    $prefix = $r + '\'
    return $c.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-ScyllaReparseTarget {
    param([Parameter(Mandatory)]$Item)
    if ($null -eq $Item) { return $null }
    if ($PSVersionTable.PSVersion.Major -ge 6) {
        if ($Item.PSObject.Properties['LinkTarget'] -and $Item.LinkTarget) {
            $t = $Item.LinkTarget
            if ($t -is [array]) { return [string]$t[0] }
            return [string]$t
        }
        try {
            $resolved = $Item.ResolveLinkTarget($true)
            if ($resolved) { return $resolved.FullName }
        } catch { }
    }
    if ($Item.PSObject.Properties['Target'] -and $Item.Target) {
        $t = $Item.Target
        if ($t -is [array]) { return [string]$t[0] }
        return [string]$t
    }
    return $null
}

function Test-ScyllaReparseEscape {
    param(
        [Parameter(Mandatory)][string]$WorkspacePath,
        [Parameter(Mandatory)][string]$BoundaryRoot
    )
    $escapes = [System.Collections.Generic.List[string]]::new()
    $all = @()
    $all += @(Get-Item -LiteralPath $WorkspacePath -Force -ErrorAction Stop)
    $all += @(Get-ChildItem -LiteralPath $WorkspacePath -Recurse -Force -ErrorAction SilentlyContinue)
    foreach ($item in $all) {
        $isReparse = ([int]$item.Attributes -band [int][IO.FileAttributes]::ReparsePoint) -ne 0
        if (-not $isReparse) { continue }
        $target = Get-ScyllaReparseTarget -Item $item
        if ([string]::IsNullOrWhiteSpace($target)) {
            [void]$escapes.Add("$($item.FullName) (reparse target unresolved — fail closed)")
            continue
        }
        if ($target.StartsWith('\\')) {
            [void]$escapes.Add("$($item.FullName) -> $target (UNC)")
            continue
        }
        try {
            $canonTarget = Get-ScyllaCanonicalPath -Path $target
        } catch {
            [void]$escapes.Add("$($item.FullName) -> $target (uncanonical)")
            continue
        }
        $boundary = Get-ScyllaCanonicalPath -Path $BoundaryRoot
        $okSame = $canonTarget.Equals($boundary, [StringComparison]::OrdinalIgnoreCase)
        $okUnder = Test-ScyllaIsUnderRoot -Candidate $canonTarget -Root $boundary
        if (-not ($okSame -or $okUnder)) {
            [void]$escapes.Add("$($item.FullName) -> $canonTarget")
        }
    }
    return [string[]]$escapes.ToArray()
}

function Resolve-ScyllaWorkspace {
    <#
    .SYNOPSIS
      Canonicalize and validate a host path for share exposure.
    .OUTPUTS
      Hashtable: Ok, Path, Reason
    #>
    param(
        [Parameter(Mandatory)][string]$Requested,
        [Parameter(Mandatory)][string]$WorkspaceRoot
    )
    try {
        $root = Get-ScyllaCanonicalPath -Path $WorkspaceRoot
    } catch {
        return @{ Ok = $false; Path = $null; Reason = "workspaceRoot invalid: $($_.Exception.Message)" }
    }
    $req = $Requested.Trim()
    if ($req.StartsWith('\\') -or $req.StartsWith('//')) {
        return @{ Ok = $false; Path = $null; Reason = 'UNC paths are rejected in v0.0.1' }
    }
    try {
        $full = Get-ScyllaCanonicalPath -Path $req
    } catch {
        return @{ Ok = $false; Path = $null; Reason = $_.Exception.Message }
    }
    if ($full.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
        return @{ Ok = $false; Path = $full; Reason = "The workspace root itself may not be shared ($root). Pick one project directory beneath it." }
    }
    if (-not (Test-ScyllaIsUnderRoot -Candidate $full -Root $root)) {
        return @{ Ok = $false; Path = $full; Reason = "Workspace resolves outside $root." }
    }
    if (-not (Test-Path -LiteralPath $full)) {
        return @{ Ok = $false; Path = $full; Reason = "Path does not exist: $full" }
    }
    $item = Get-Item -LiteralPath $full -Force
    if (-not $item.PSIsContainer) {
        return @{ Ok = $false; Path = $full; Reason = "Path is not a directory: $full" }
    }
    if (([int]$item.Attributes -band [int][IO.FileAttributes]::ReparsePoint) -ne 0) {
        return @{ Ok = $false; Path = $full; Reason = "Workspace path is a reparse point: $full" }
    }
    $escapes = @($(Test-ScyllaReparseEscape -WorkspacePath $full -BoundaryRoot $full) | ForEach-Object { [string]$_ })
    if ($escapes.Count -gt 0) {
        $list = $escapes -join '; '
        return @{ Ok = $false; Path = $full; Reason = "Reparse-point escape detected: $list" }
    }
    return @{ Ok = $true; Path = $full; Reason = $null }
}

function Resolve-ScyllaLaunchTarget {
    param(
        [Parameter(Mandatory)][string]$Target,
        [Parameter(Mandatory)][string]$WorkspaceRoot
    )
    $t = $Target.Trim()
    if ($t -match '^[A-Za-z]:\\' -or $t.Contains('\') -or $t.Contains('/')) {
        return $t
    }
    return (Join-Path $WorkspaceRoot $t)
}

function Test-ScyllaHyperVCmdletsPresent {
    $needed = @('Get-VM', 'Start-VM', 'Stop-VM', 'Get-VMSnapshot', 'Checkpoint-VM', 'Restore-VMSnapshot', 'Get-VMIntegrationService')
    $missing = @()
    foreach ($n in $needed) {
        if (-not (Get-Command $n -ErrorAction SilentlyContinue)) { $missing += $n }
    }
    return @{ Ok = ($missing.Count -eq 0); Missing = $missing }
}

function Get-ScyllaLocalUserExists {
    param([Parameter(Mandatory)][string]$Name)
    try {
        $null = Get-LocalUser -Name $Name -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Get-ScyllaShareOrNull {
    param([Parameter(Mandatory)][string]$Name)
    try {
        return Get-SmbShare -Name $Name -ErrorAction Stop
    } catch {
        return $null
    }
}

function Test-ScyllaAccountIsAdmin {
    param([Parameter(Mandatory)][string]$Name)
    try {
        $group = Get-LocalGroupMember -Group 'Administrators' -ErrorAction Stop
        foreach ($m in $group) {
            if ($m.Name -like "*\$Name" -or $m.Name -eq $Name) { return $true }
        }
    } catch { }
    return $false
}
