#Requires -Version 5.1
#Requires -RunAsAdministrator
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = 'B:\GAMES_permanent\MiddleMan\RetroBat'
$readShare = 'RetroBatFamilyRead'
$saveShare = 'RetroBatFamilySaves'
$saveRoot = Join-Path $root 'saves\family'
$firewallAllow = 'RetroBat Family SMB - Private LAN'
$firewallPublicBlock = 'RetroBat Family SMB - Block Public'
$authenticatedUsers = [System.Security.Principal.SecurityIdentifier]::new('S-1-5-11').Translate([System.Security.Principal.NTAccount]).Value
$hostOwner = "$env:COMPUTERNAME\Osman"

if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "The official RetroBat host install was not found: $root" }

foreach ($required in @('roms', 'bios', 'emulators', 'emulationstation', 'RetroBat.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $required))) { throw "Host install is missing $required under $root" }
}
$activePrivate = Get-NetConnectionProfile -ErrorAction Stop | Where-Object { $_.NetworkCategory -eq 'Private' }
if (-not $activePrivate) { throw 'Set the active family LAN connection profile to Private before preparing SMB access.' }
if (Get-SmbServerConfiguration | Where-Object { -not $_.EnableSMB2Protocol -or $_.EnableSMB1Protocol }) {
    throw 'SMB2 must be enabled and SMB1 disabled. This script will not change SMB server protocol settings.'
}
try { $hostOwner = ([System.Security.Principal.NTAccount]::new($hostOwner)).Translate([System.Security.Principal.SecurityIdentifier]).Translate([System.Security.Principal.NTAccount]).Value }
catch { throw "The expected local owner account $hostOwner could not be resolved; no host configuration was changed." }

if (Get-SmbShare -Name $readShare,$saveShare -ErrorAction SilentlyContinue) { throw 'One or both family SMB share names already exist; refusing to alter existing shares.' }
if (Get-NetFirewallRule -Name $firewallAllow,$firewallPublicBlock -ErrorAction SilentlyContinue) { throw 'A family SMB firewall rule name already exists; inspect it before running again.' }

# Report existing rules that can apply to SMB itself (System or LanmanServer on TCP/445).
foreach ($rule in (Get-NetFirewallRule -Enabled True -Direction Inbound -Action Allow -ErrorAction Stop)) {
    $portFilter = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $rule
    $protocol = [string]$portFilter.Protocol
    $ports = @($portFilter.LocalPort | ForEach-Object { [string]$_ })
    $covers445 = ($protocol -in @('TCP','6','Any','256')) -and (($ports -contains 'Any') -or ($ports -contains '445') -or ($ports | Where-Object { $_ -match '^\d+-\d+$' -and [int]($_ -split '-')[0] -le 445 -and [int]($_ -split '-')[1] -ge 445 }))
    if (-not $covers445) { continue }
    $program = [string](Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $rule).Program
    $service = [string](Get-NetFirewallServiceFilter -AssociatedNetFirewallRule $rule).Service
    if (($program -notmatch '(?i)(^|\\)System$') -and ($service -notmatch '(?i)^LanmanServer$')) { continue }
    $remote = @((Get-NetFirewallAddressFilter -AssociatedNetFirewallRule $rule).RemoteAddress | ForEach-Object { [string]$_ })
    $profiles = [string]$rule.Profile
    Write-Host "Existing SMB allow rule: $($rule.DisplayName); program=$program; service=$service; profile=$profiles; remote=$($remote -join ',')"
}

New-Item -ItemType Directory -Force -Path $saveRoot | Out-Null
$admin = [System.Security.Principal.SecurityIdentifier]::new('S-1-5-32-544').Translate([System.Security.Principal.NTAccount]).Value
$system = [System.Security.Principal.SecurityIdentifier]::new('S-1-5-18').Translate([System.Security.Principal.NTAccount]).Value
function Set-FamilyTreeAcl {
    param([string]$Path, [System.Security.AccessControl.FileSystemRights]$ClientRights)
    $acl = Get-Acl -LiteralPath $Path
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($existing in @($acl.Access)) { [void]$acl.RemoveAccessRuleSpecific($existing) }
    $entries = @(
        [pscustomobject]@{ Principal = $hostOwner; Rights = 'FullControl' },
        [pscustomobject]@{ Principal = $admin; Rights = 'FullControl' },
        [pscustomobject]@{ Principal = $system; Rights = 'FullControl' },
        [pscustomobject]@{ Principal = $authenticatedUsers; Rights = [string]$ClientRights }
    )
    foreach ($entry in $entries) {
        $rule = [System.Security.AccessControl.FileSystemAccessRule]::new(
            $entry.Principal, $entry.Rights, 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        [void]$acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}
$rootAclBackup = Get-Acl -LiteralPath $root
$saveAclBackup = Get-Acl -LiteralPath $saveRoot
$createdShares = [System.Collections.Generic.List[string]]::new()
$createdFirewallRules = [System.Collections.Generic.List[string]]::new()
$createdPicker = $false
$createdProfiles = $false
try {
    Set-FamilyTreeAcl -Path $root -ClientRights ReadAndExecute
    Set-FamilyTreeAcl -Path $saveRoot -ClientRights Modify
    New-SmbShare -Name $readShare -Path $root -FullAccess $admin,$system -ReadAccess $authenticatedUsers -EncryptData $true -FolderEnumerationMode AccessBased | Out-Null
    $createdShares.Add($readShare)
    New-SmbShare -Name $saveShare -Path $saveRoot -FullAccess $admin,$system -ChangeAccess $authenticatedUsers -EncryptData $true -FolderEnumerationMode AccessBased | Out-Null
    $createdShares.Add($saveShare)
    New-NetFirewallRule -Name $firewallAllow -DisplayName $firewallAllow -Direction Inbound -Action Allow -Protocol TCP -LocalPort 445 -RemoteAddress LocalSubnet -Profile Private | Out-Null
    $createdFirewallRules.Add($firewallAllow)
    New-NetFirewallRule -Name $firewallPublicBlock -DisplayName $firewallPublicBlock -Direction Inbound -Action Block -Protocol TCP -LocalPort 445 -RemoteAddress Any -Profile Public | Out-Null
    $createdFirewallRules.Add($firewallPublicBlock)
    $pickerTarget = Join-Path $root 'emulationstation\FamilyProfilePicker.exe'
    if (-not (Test-Path -LiteralPath $pickerTarget -PathType Leaf)) {
        $pickerBuild = Join-Path (Split-Path -Parent $PSScriptRoot) 'family\Build-FamilyProfilePicker.ps1'
        if (-not (Test-Path -LiteralPath $pickerBuild -PathType Leaf)) { throw "Family profile picker build script not found: $pickerBuild" }
        & $pickerBuild -OutputPath $pickerTarget
        $createdPicker = $true
    }
    $profilesTarget = Join-Path $saveRoot 'profiles.json'
    if (-not (Test-Path -LiteralPath $profilesTarget -PathType Leaf)) {
        $profilesSource = Join-Path $PSScriptRoot 'profiles.json'
        if (-not (Test-Path -LiteralPath $profilesSource -PathType Leaf)) { throw "Initial profile data is missing: $profilesSource" }
        [IO.File]::Copy($profilesSource, $profilesTarget, $false)
        $createdProfiles = $true
    }
} catch {
    if ($createdPicker -and (Test-Path -LiteralPath $pickerTarget)) { [void](Assert-FamilyPathWithin -Root $root -Child $pickerTarget); Remove-Item -LiteralPath $pickerTarget -Force -ErrorAction SilentlyContinue }
    if ($createdProfiles -and (Test-Path -LiteralPath $profilesTarget)) { [void](Assert-FamilyPathWithin -Root $saveRoot -Child $profilesTarget); Remove-Item -LiteralPath $profilesTarget -Force -ErrorAction SilentlyContinue }
    foreach ($name in $createdShares) { Remove-SmbShare -Name $name -Force -ErrorAction SilentlyContinue }
    foreach ($name in $createdFirewallRules) { Remove-NetFirewallRule -Name $name -ErrorAction SilentlyContinue }
    Set-Acl -LiteralPath $root -AclObject $rootAclBackup -ErrorAction SilentlyContinue
    Set-Acl -LiteralPath $saveRoot -AclObject $saveAclBackup -ErrorAction SilentlyContinue
    throw
}

Write-Host "Prepared read-only binaries/ROM/BIOS share: \\$env:COMPUTERNAME\$readShare"
Write-Host "Prepared per-client saves share root: \\$env:COMPUTERNAME\$saveShare"
Write-Host 'SMB is allowed only on Private/LocalSubnet by the family allow rule and blocked on Public profiles by the family block rule. Existing firewall rules were not changed.'
Write-Host 'Access is granted to authenticated Windows users on the Private LAN; anonymous access is not enabled. Each client uses a separate save folder, but authenticated users can access all folders.'



