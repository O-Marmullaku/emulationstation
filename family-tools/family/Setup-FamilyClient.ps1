#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$HostName,
    [string]$Account,
    [string]$ClientId = $env:COMPUTERNAME,
    [Parameter(Mandatory)][string]$LocalRoot,
    [string]$DisplayName,
    [string]$AccentColor
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Family.Common.ps1')
Assert-FamilyClientId $ClientId
$appearance = Resolve-FamilyClientAppearance -ClientId $ClientId -ProfilesPath (Join-Path $PSScriptRoot 'profiles.json') `
    -DefaultDisplayName $env:COMPUTERNAME -DisplayName $DisplayName `
    -DisplayNameSpecified $PSBoundParameters.ContainsKey('DisplayName') -AccentColor $AccentColor `
    -AccentColorSpecified $PSBoundParameters.ContainsKey('AccentColor')
$DisplayName = $appearance.DisplayName
$AccentColor = $appearance.AccentColor

if ($HostName -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$') { throw 'HostName must be a LAN hostname or IPv4 address.' }
if ($Account -and $Account -notmatch '^[A-Za-z0-9_.-]+\\[A-Za-z0-9_.-]+$') { throw 'Account must be HOST\User; do not include a password.' }
if (-not [IO.Path]::IsPathRooted($LocalRoot) -or $LocalRoot.StartsWith('\\')) { throw 'LocalRoot must be an absolute local drive path, not a UNC path.' }
$local = [IO.Path]::GetFullPath($LocalRoot).TrimEnd('\')
if ($local -match '^[RS]:') { throw 'LocalRoot cannot use R: or S:, reserved for the family SMB mappings.' }
if (Test-Path -LiteralPath $local) { throw "LocalRoot already exists; refusing to overwrite it: $local" }

$settings = [pscustomobject]@{ HostName = $HostName; Account = $Account; ClientId = $ClientId }
Connect-FamilyShares $settings
foreach ($required in @('R:\roms', 'R:\bios', 'R:\emulators', 'R:\emulationstation', 'R:\RetroBat.exe', 'S:\')) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required host item is unavailable: $required" }
}

if (-not (Test-Path -LiteralPath 'S:\profiles.json' -PathType Leaf)) { throw 'Shared profiles are missing. Install the family frontend on the host first.' }

$parent = Split-Path -Parent $local
if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
$stage = Join-Path $parent ((Split-Path -Leaf $local) + '.staging-' + [guid]::NewGuid().ToString('N'))
try {
    $null = New-Item -ItemType Directory -Path $stage
    $source = 'R:\'
    $robocopy = Join-Path $env:WINDIR 'System32\robocopy.exe'
    & $robocopy $source $stage /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ /XD 'R:\roms' 'R:\bios' 'R:\saves' | Out-Host
    if ($LASTEXITCODE -ge 8) { throw "Robocopy failed with exit code $LASTEXITCODE." }

    $launcherConfig = @(
        (Join-Path $stage 'emulationstation\emulatorLauncher.cfg'),
        (Join-Path $stage 'emulatorLauncher.cfg')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    $systemsConfig = @(
        (Join-Path $stage 'emulationstation\.emulationstation\es_systems.cfg'),
        (Join-Path $stage '.emulationstation\es_systems.cfg')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $launcherConfig -or -not $systemsConfig) { throw 'The copied RetroBat layout did not contain the expected launcher and EmulationStation configs.' }
    if (-not (Test-Path -LiteralPath (Join-Path $stage 'emulationstation\emulationstation.exe') -PathType Leaf)) { throw 'The copied RetroBat layout did not contain emulationstation\emulationstation.exe.' }

    Set-FamilyLauncherConfig -Path $launcherConfig -ClientId $ClientId
    $changed = Set-FamilyRomPaths -Path $systemsConfig
    Set-FamilyPs2BiosArgument -Path $systemsConfig -BiosRoot ('S:\' + $ClientId + '\bios\pcsx2\bios')
    if (-not (Test-Path -LiteralPath (Join-Path $stage 'RetroBat.exe') -PathType Leaf)) { throw 'RetroBat.exe was not copied into the local runtime.' }

    $clientInfo = [ordered]@{ HostName = $HostName; Account = $Account; ClientId = $ClientId; ReadDrive = 'R:'; SavesDrive = 'S:'; FamilyRoot = 'S:\'; BiosRoot = 'R:\bios'; ComputerName = $env:COMPUTERNAME; DisplayName = $DisplayName; AccentColor = $AccentColor }
    [IO.File]::WriteAllText((Join-Path $stage 'family-client.json'), ($clientInfo | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    $clientLauncher = '@echo off' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0family\Start-FamilyRetroBat.ps1" -LocalRoot "%~dp0."' + "`r`n" + 'if errorlevel 1 pause' + "`r`n"
    [IO.File]::WriteAllText((Join-Path $stage 'Start-Family-RetroBat.cmd'), $clientLauncher, [Text.Encoding]::ASCII)
    [void](Assert-FamilyPathWithin -Root $parent -Child $stage)
    if ((Split-Path -Parent ([IO.Path]::GetFullPath($local))) -ine [IO.Path]::GetFullPath($parent)) { throw 'LocalRoot must be a direct child of its parent staging folder.' }
    Move-Item -LiteralPath $stage -Destination $local
    Write-Host "Client runtime installed at $local; $changed ROM path entries now use R:."
    Write-Host "Choose a shared save profile at startup. Run Start-FamilyRetroBat.ps1 -LocalRoot '$local' to launch."
} catch {
    if (Test-Path -LiteralPath $stage) {
        [void](Assert-FamilyPathWithin -Root $parent -Child $stage)
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    throw
}



