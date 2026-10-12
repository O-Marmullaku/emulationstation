#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$LocalRoot,
    [switch]$HostLocal,
    [ValidateRange(10,300)][int]$StartupTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Family.Common.ps1')

function Get-FamilyProcessRecord {
    param([Parameter(Mandatory)]$Process)

    $creationDate = $Process.CreationDate
    if ($creationDate -isnot [datetime]) {
        $creationDate = [System.Management.ManagementDateTimeConverter]::ToDateTime([string]$creationDate)
    }
    [pscustomobject]@{
        ProcessId = [int]$Process.ProcessId
        ParentProcessId = [int]$Process.ParentProcessId
        CreationTicks = [long]$creationDate.ToUniversalTime().Ticks
    }
}

function Update-FamilyProcessTree {
    param(
        [Parameter(Mandatory)][hashtable]$ActiveByPid,
        [Parameter(Mandatory)][object[]]$Processes
    )

    $currentByPid = @{}
    foreach ($process in $Processes) { $currentByPid[$process.ProcessId] = $process.CreationTicks }

    foreach ($trackedPid in @($ActiveByPid.Keys)) {
        if (-not $currentByPid.ContainsKey([int]$trackedPid) -or $currentByPid[[int]$trackedPid] -ne $ActiveByPid[[int]$trackedPid]) {
            [void]$ActiveByPid.Remove([int]$trackedPid)
        }
    }

    # Resolve descendants to closure so grandchildren are followed even if listed before their parents.
    $addedProcess = $true
    while ($addedProcess) {
        $addedProcess = $false
        foreach ($process in $Processes) {
            $parentPid = $process.ParentProcessId
            if ($parentPid -eq $process.ProcessId -or -not $ActiveByPid.ContainsKey($parentPid)) { continue }
            if (-not $ActiveByPid.ContainsKey($process.ProcessId)) {
                $ActiveByPid[$process.ProcessId] = $process.CreationTicks
                $addedProcess = $true
            }
        }
    }

    return $ActiveByPid.Count
}

if ($HostLocal) {
    $root = 'B:\GAMES_permanent\MiddleMan\RetroBat'
    $familyRoot = Join-Path $root 'saves\family'
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Host RetroBat runtime not found: $root" }
    if (-not (Test-Path -LiteralPath $familyRoot -PathType Container)) { throw "Host save folder not found: $familyRoot. Run Setup-FamilyHost.ps1 first." }
    $biosRoot = Join-Path $root 'bios'
    $romsRoot = Join-Path $root 'roms'
} else {
    if (-not $LocalRoot) { throw 'Provide -LocalRoot for a client runtime, or use -HostLocal on the host computer.' }
    $root = [IO.Path]::GetFullPath($LocalRoot)
    $config = Join-Path $root 'family-client.json'
    if (-not (Test-Path -LiteralPath $config -PathType Leaf)) { throw "Client config not found: $config. Run Setup-FamilyClient.ps1 first." }
    $settings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    Connect-FamilyShares $settings
    $familyRoot = if ($settings.PSObject.Properties['FamilyRoot']) { [string]$settings.FamilyRoot } else { 'S:\' }
    if (-not $familyRoot) { $familyRoot = 'S:\' }
    foreach ($required in @('R:\roms', 'R:\bios', $familyRoot)) {
        if (-not (Test-Path -LiteralPath $required)) { throw "Required host item is unavailable: $required" }
    }
    $biosRoot = 'R:\bios'
    $romsRoot = 'R:\roms'
}

$frontendPath = Join-Path $root 'emulationstation\emulationstation.exe'
$launcherPath = Join-Path $root 'RetroBat.exe'
$pickerPath = Join-Path $root 'emulationstation\FamilyProfilePicker.exe'
$launcherConfig = Join-Path $root 'emulationstation\emulatorLauncher.cfg'
$systemsConfig = Join-Path $root 'emulationstation\.emulationstation\es_systems.cfg'
foreach ($required in @($launcherPath, $frontendPath, $pickerPath, $launcherConfig, $systemsConfig)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required runtime file is missing: $required" }
}
$profilesPath = Join-Path $familyRoot 'profiles.json'
if (-not (Test-Path -LiteralPath $profilesPath -PathType Leaf)) { throw "Shared profile list is missing: $profilesPath. Run Setup-FamilyHost.ps1 first." }

# Lock the local runtime/config while the selector and frontend use it.
$lock = New-FamilySessionLock $root
$resultPath = Join-Path $root ('.family-profile-selection-' + [guid]::NewGuid().ToString('N') + '.txt')
try {
    [void](Assert-FamilyPathWithin -Root $root -Child $resultPath)
    $picker = Start-Process -FilePath $pickerPath -ArgumentList @('"' + $profilesPath + '"', '"' + $resultPath + '"') -WorkingDirectory (Split-Path -Parent $pickerPath) -PassThru -Wait -ErrorAction Stop
    if ($picker.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $resultPath -PathType Leaf)) { throw 'Profile selection was cancelled or could not be completed.' }
    $profileId = [IO.File]::ReadAllText($resultPath).Trim()
    Assert-FamilyClientId $profileId
    $profiles = Get-Content -LiteralPath $profilesPath -Raw | ConvertFrom-Json
    $profile = $profiles | Where-Object { [string]$_.ClientId -ieq $profileId } | Select-Object -First 1
    if (-not $profile) { throw "Selected profile no longer exists in the shared list: $profileId" }

    $profileRoot = Join-Path $familyRoot $profileId
    New-Item -ItemType Directory -Force -Path $profileRoot | Out-Null
    foreach ($knownProfile in $profiles) {
        Assert-FamilyClientId ([string]$knownProfile.ClientId)
        $knownRoot = Join-Path $familyRoot ([string]$knownProfile.ClientId)
        $knownBios = Join-Path $knownRoot 'bios\pcsx2\bios'
        if ((Test-Path -LiteralPath $knownBios) -and (Get-ChildItem -LiteralPath $knownBios -Filter '*.bin' -File | Select-Object -First 1)) { continue }
        New-Item -ItemType Directory -Force -Path $knownRoot | Out-Null
        $profileLock = New-FamilySessionLock $knownRoot
        try { Initialize-FamilyClientBios -SourceRoot $biosRoot -SaveRoot $knownRoot }
        finally { $profileLock.Dispose() }
    }
    Set-FamilyLauncherConfig -Path $launcherConfig -ClientId $profileId -BiosRoot $biosRoot -RomsRoot $romsRoot -SavesRoot $profileRoot
    Set-FamilyPs2BiosArgument -Path $systemsConfig -BiosRoot (Join-Path $profileRoot 'bios\pcsx2\bios')

    $env:RETROBAT_FAMILY_ROOT = [IO.Path]::GetFullPath($familyRoot)
    $env:RETROBAT_PROFILE_ID = $profileId
    $frontendFullPath = [IO.Path]::GetFullPath($frontendPath)
    $launcher = Start-Process -FilePath $launcherPath -WorkingDirectory $root -PassThru -ErrorAction Stop
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    $frontend = $null
    while (-not $frontend -and [DateTime]::UtcNow -lt $deadline) {
        foreach ($process in (Get-CimInstance Win32_Process -Filter "Name='emulationstation.exe'" -ErrorAction Stop)) {
            if ($process.ExecutablePath -and [IO.Path]::GetFullPath([string]$process.ExecutablePath) -ieq $frontendFullPath) { $frontend = $process; break }
        }
        if (-not $frontend) { Start-Sleep -Milliseconds 250 }
    }
    if (-not $frontend) { throw "RetroBat did not start the expected EmulationStation process within $StartupTimeoutSeconds seconds: $frontendFullPath (launcher PID $($launcher.Id))." }

    # Track PID plus creation time. PIDs can be reused after an exited process.
    $frontendIdentity = Get-FamilyProcessRecord $frontend
    $activeByPid = @{}
    $activeByPid[$frontendIdentity.ProcessId] = $frontendIdentity.CreationTicks
    do {
        $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop | ForEach-Object { Get-FamilyProcessRecord $_ })
        [void](Update-FamilyProcessTree -ActiveByPid $activeByPid -Processes $processes)
        if ($activeByPid.Count -gt 0) { Start-Sleep -Milliseconds 300 }
    } while ($activeByPid.Count -gt 0)
} finally {
    Remove-Item -LiteralPath Env:RETROBAT_FAMILY_ROOT -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath Env:RETROBAT_PROFILE_ID -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $resultPath) { [void](Assert-FamilyPathWithin -Root $root -Child $resultPath); Remove-Item -LiteralPath $resultPath -Force }
    $lock.Dispose()
}
