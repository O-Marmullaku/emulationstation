#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$LocalRoot,
    [switch]$HostLocal,
    [ValidateRange(10,300)][int]$StartupTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Family.Common.ps1')

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

    # Follow this runtime's frontend process tree so the local config lock remains held during game sessions.
    $activeIds = [System.Collections.Generic.HashSet[int]]::new()
    [void]$activeIds.Add([int]$frontend.ProcessId)
    do {
        $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop)
        foreach ($process in $processes) { if ($activeIds.Contains([int]$process.ParentProcessId)) { [void]$activeIds.Add([int]$process.ProcessId) } }
        $liveIds = @($processes | Where-Object { $activeIds.Contains([int]$_.ProcessId) } | ForEach-Object { [int]$_.ProcessId })
        if ($liveIds.Count -gt 0) { Start-Sleep -Milliseconds 300 }
    } while ($liveIds.Count -gt 0)
} finally {
    Remove-Item -LiteralPath Env:RETROBAT_FAMILY_ROOT -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath Env:RETROBAT_PROFILE_ID -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $resultPath) { [void](Assert-FamilyPathWithin -Root $root -Child $resultPath); Remove-Item -LiteralPath $resultPath -Force }
    $lock.Dispose()
}
