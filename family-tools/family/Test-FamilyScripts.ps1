#Requires -Version 5.1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Family.Common.ps1')

$temp = Join-Path (Join-Path (Get-Location) 'work') ('family-selfcheck-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $config = Join-Path $temp 'emulatorLauncher.cfg'
    [IO.File]::WriteAllText($config, "home=.`\.emulationstation`r`nbios=.`\.\.\bios`r`nsaves=.`\.\.\saves`r", [Text.UTF8Encoding]::new($false))
    Set-FamilyLauncherConfig -Path $config -ClientId 'family-test_1'
    $text = [IO.File]::ReadAllText($config)
    if (-not $text.Contains('bios=R:\bios') -or -not $text.Contains('roms=R:\roms') -or -not $text.Contains('saves=S:\family-test_1')) { throw 'Launcher path configuration self-check failed.' }

    $systems = Join-Path $temp 'es_systems.cfg'
    [IO.File]::WriteAllText($systems, '<systemList><system><name>ps2</name><command>"%HOME%\emulatorLauncher.exe" -system %SYSTEM% -bios S:\family-test_1\bios -saves S:\family-test_1 -rom %ROM%</command><path>~\..\roms\ps2</path></system></systemList>', [Text.UTF8Encoding]::new($false))
    if ((Set-FamilyRomPaths $systems) -ne 1) { throw 'ROM path rewrite self-check failed.' }
    Set-FamilyPs2BiosArgument -Path $systems -BiosRoot 'S:\profile-test\bios\pcsx2\bios'
    [xml]$systemsXml = [IO.File]::ReadAllText($systems)
    $ps2Command = $systemsXml.SelectSingleNode("/systemList/system[name='ps2']/command").InnerText
    if (-not $ps2Command.Contains('-bios S:\profile-test\bios\pcsx2\bios -rom %ROM%') -or -not [IO.File]::ReadAllText($systems).Contains('<path>R:\roms\ps2</path>')) { throw 'PS2 BIOS rewrite or ROM path self-check failed.' }
    $invalidRejected = $false
    try { Assert-FamilyClientId '../bad' } catch { $invalidRejected = $true }
    if (-not $invalidRejected) { throw 'ClientId validation self-check failed.' }

    # Exercise the same direct ConvertFrom-Json assignment and profile lookup used by startup under Windows PowerShell 5.1.
    $startupPath = Join-Path $PSScriptRoot 'Start-FamilyRetroBat.ps1'
    if (-not [IO.File]::ReadAllText($startupPath).Contains('$profiles = Get-Content -LiteralPath $profilesPath -Raw | ConvertFrom-Json')) { throw 'Startup does not use direct ConvertFrom-Json assignment for profile lookup.' }
    $multiRegistryPath = Join-Path $temp 'multi-profiles.json'
    [IO.File]::WriteAllText($multiRegistryPath, '[{"ClientId":"first-pc","DisplayName":"First","AccentColor":"#112233"},{"ClientId":"qa-blue","DisplayName":"QA Blue","AccentColor":"#445566"},{"ClientId":"third-pc","DisplayName":"Third","AccentColor":"#778899"}]', [Text.UTF8Encoding]::new($false))
    $multiProfilesPath = $multiRegistryPath
    $profiles = Get-Content -LiteralPath $multiProfilesPath -Raw | ConvertFrom-Json
    $selectedProfile = $profiles | Where-Object { [string]$_.ClientId -ieq 'qa-blue' } | Select-Object -First 1
    if (@($profiles).Count -ne 3 -or -not $selectedProfile -or $selectedProfile.DisplayName -ne 'QA Blue') { throw 'Multi-profile lookup failed for direct Windows PowerShell 5.1 JSON array assignment.' }

    $profiles = Join-Path $temp 'profiles.json'
    [IO.File]::WriteAllText($profiles, '[{"ClientId":"test-pc","DisplayName":"Test PC","AccentColor":"#A1B2C3"}]', [Text.UTF8Encoding]::new($false))
    $profileAppearance = Resolve-FamilyClientAppearance -ClientId 'TEST-PC' -ProfilesPath $profiles -DefaultDisplayName 'Computer' -DisplayNameSpecified:$false -AccentColorSpecified:$false
    if ($profileAppearance.DisplayName -ne 'Test PC' -or $profileAppearance.AccentColor -ne '#A1B2C3') { throw 'Profile appearance lookup self-check failed.' }
    $explicitAppearance = Resolve-FamilyClientAppearance -ClientId 'test-pc' -ProfilesPath $profiles -DefaultDisplayName 'Computer' -DisplayName 'My Label' -DisplayNameSpecified:$true -AccentColor '#112233' -AccentColorSpecified:$true
    if ($explicitAppearance.DisplayName -ne 'My Label' -or $explicitAppearance.AccentColor -ne '#112233') { throw 'Explicit appearance override self-check failed.' }
    $fallbackAppearance = Resolve-FamilyClientAppearance -ClientId 'unknown-pc' -ProfilesPath $profiles -DefaultDisplayName 'Computer' -DisplayNameSpecified:$false -AccentColorSpecified:$false
    if ($fallbackAppearance.DisplayName -ne 'Computer' -or $fallbackAppearance.AccentColor -ne '#5588CC') { throw 'Profile fallback self-check failed.' }

    $pickerOutput = Join-Path $temp 'FamilyProfilePicker.exe'
    & (Join-Path $PSScriptRoot 'Build-FamilyProfilePicker.ps1') -OutputPath $pickerOutput
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pickerOutput)) { throw 'Profile picker compilation self-check failed.' }
    $pickerAssembly = [Reflection.Assembly]::Load([IO.File]::ReadAllBytes($pickerOutput))
    $store = $pickerAssembly.GetType('FamilyProfileStore')
    $created = $store.GetMethod('Create').Invoke($null, [object[]]@([string]$profiles, [string]'New Test Person', [string]'#12AB34'))
    if ($created.ClientId -notmatch '^profile-[0-9A-Fa-f]{32}$') { throw 'Created profile ID is not a GUID-based ID.' }
    if ($created.DisplayName -ne 'New Test Person' -or $created.AccentColor -ne '#12AB34') { throw 'Profile creation data did not round-trip.' }
    $readBack = $store.GetMethod('Read').Invoke($null, [object[]]@([string]$profiles))
    if (@($readBack).Count -ne 2) { throw 'Profile list did not retain existing rows plus the new profile.' }
    $lockPath = $profiles + '.lock'
    $heldProfileLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $lockRejected = $false
    try { [void]$store.GetMethod('Create').Invoke($null, [object[]]@([string]$profiles, [string]'Blocked Name', [string]'#445566')) } catch { $lockRejected = $true } finally { $heldProfileLock.Dispose() }
    if (-not $lockRejected) { throw 'Profile creation ignored the exclusive metadata lock.' }
    function Get-SmbMapping {
        [CmdletBinding()]
        param([string]$LocalPath)
        if ($LocalPath -eq 'R:') { return [pscustomobject]@{ LocalPath = 'R:'; RemotePath = '\\family-host\RetroBatFamilyRead' } }
    }
    Connect-FamilyShare -Drive R -RemotePath '\\family-host\RetroBatFamilyRead' -Account ''

    [void](Assert-FamilyPathWithin -Root $temp -Child (Join-Path $temp 'safe-child'))
    $outsideRejected = $false
    try { [void](Assert-FamilyPathWithin -Root $temp -Child (Join-Path (Split-Path -Parent $temp) 'outside-child')) } catch { $outsideRejected = $true }
    if (-not $outsideRejected) { throw 'Recursive path containment self-check failed.' }

    $first = New-FamilySessionLock $temp
    $secondRejected = $false
    try { $second = New-FamilySessionLock $temp; $second.Dispose() } catch { $secondRejected = $true }
    $first.Dispose()
    if (-not $secondRejected) { throw 'Exclusive lock self-check failed.' }

    foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1') {
        $tokens = $null; $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
        if ($errors.Count) { throw "PowerShell syntax error in $($file.Name): $($errors[0].Message)" }
    }
    Write-Host 'PASS: local config routing, ROM rewrite, PS2 BIOS rewrite, empty-account mapping, profile defaults/overrides, path containment, ClientId validation, exclusive lock, and PowerShell syntax.'
    Write-Host 'SMB connectivity, ACLs, firewall behavior, and a real RetroBat launch were not exercised.'
} finally {
    [void](Assert-FamilyPathWithin -Root (Split-Path -Parent $temp) -Child $temp)
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}





