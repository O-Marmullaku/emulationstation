#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $workspace ('work\family-frontend-selftest-' + [guid]::NewGuid().ToString('N'))
$installer = Join-Path $PSScriptRoot 'Install-FamilyFrontend.ps1'
function Assert([bool]$Condition,[string]$Message) { if (-not $Condition) { throw "FAIL: $Message" } }
function New-TestPackage([string]$Path,[string]$Tag) {
    $src = Join-Path $testRoot ('pkg-' + $Tag)
    New-Item -ItemType Directory -Path (Join-Path $src 'emulationstation') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $src 'family') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $src 'emulationstation\emulationstation.exe'),"fake-es-$Tag")
    [IO.File]::WriteAllText((Join-Path $src 'emulationstation\FamilyProfilePicker.exe'),"fake-picker-$Tag")
    [IO.File]::WriteAllText((Join-Path $src 'family\Start-FamilyRetroBat.ps1'),"fake-start-$Tag")
    [IO.File]::WriteAllText((Join-Path $src 'family\Family.Common.ps1'),"fake-common-$Tag")
    [IO.File]::WriteAllText((Join-Path $src 'family\profiles.json'),'[{"ClientId":"SEED","DisplayName":"Seed","AccentColor":"#123456"}]')
    [IO.File]::WriteAllText((Join-Path $src 'Start-Family-RetroBat.cmd'),'package wrapper placeholder')
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    Compress-Archive -Path (Join-Path $src '*') -DestinationPath $Path
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($Path + '.sha256',"$hash  $(Split-Path -Leaf $Path)`n")
}
function New-TestRoot([string]$Path,[switch]$Client) {
    New-Item -ItemType Directory -Path (Join-Path $Path 'emulationstation') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $Path 'RetroBat.exe'),'stub')
    [IO.File]::WriteAllText((Join-Path $Path 'emulationstation\unrelated.ini'),'keep')
    if ($Client) { [IO.File]::WriteAllText((Join-Path $Path 'family-client.json'),'{}') }
}
try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $zip = Join-Path $testRoot 'family.zip'
    $root = Join-Path $testRoot 'host\RetroBat'
    New-TestPackage $zip 'v1'
    New-TestRoot $root
    & $installer -RetroBatRoot $root -PackagePath $zip | Out-Null
    $seedPath = Join-Path $root 'saves\family\profiles.json'
    Assert (Test-Path -LiteralPath $seedPath) 'host received initial profile seed'
    $backupRoot = Join-Path (Split-Path -Parent $root) 'RetroBat-family-backups'
    $firstInstallBackup = Get-ChildItem -LiteralPath $backupRoot -Directory | Where-Object {
        $m = Get-Content -LiteralPath (Join-Path $_.FullName 'manifest.json') -Raw | ConvertFrom-Json
        @($m.Entries | Where-Object { $_.Path -eq 'emulationstation\emulationstation.exe' -and $_.Existed -eq $false }).Count -eq 1
    } | Select-Object -First 1 -ExpandProperty FullName
    Assert ([bool]$firstInstallBackup) 'first install backup records absent package files'
    [IO.File]::WriteAllText($seedPath,'[{"ClientId":"CUSTOM","DisplayName":"My profile","AccentColor":"#ABCDEF"}]')
    [IO.File]::WriteAllText((Join-Path $root 'emulationstation\unrelated.ini'),'keep-after-reapply')
    New-TestPackage $zip 'v2'
    & $installer -RetroBatRoot $root -PackagePath $zip | Out-Null
    Assert ((Get-Content -LiteralPath (Join-Path $root 'emulationstation\emulationstation.exe') -Raw) -eq 'fake-es-v2') 'reapply replaced packaged frontend'
    Assert ((Get-Content -LiteralPath $seedPath -Raw) -like '*CUSTOM*') 'reapply preserved user profile registry'
    Assert ((Get-Content -LiteralPath (Join-Path $root 'emulationstation\unrelated.ini') -Raw) -eq 'keep-after-reapply') 'unrelated file was preserved'
    $preReapplyBackup = Get-ChildItem -LiteralPath $backupRoot -Directory | Where-Object {
        $old = Join-Path $_.FullName 'files\emulationstation\emulationstation.exe'
        (Test-Path -LiteralPath $old) -and (Get-Content -LiteralPath $old -Raw) -eq 'fake-es-v1'
    } | Select-Object -First 1 -ExpandProperty FullName
    Assert ([bool]$preReapplyBackup) 'reapply created a recoverable pre-reapply backup'
    & $installer -RetroBatRoot $root -RestoreBackup $preReapplyBackup | Out-Null
    Assert ((Get-Content -LiteralPath (Join-Path $root 'emulationstation\emulationstation.exe') -Raw) -eq 'fake-es-v1') 'validated backup restored previous deployed frontend'
    Assert ((Get-Content -LiteralPath $seedPath -Raw) -like '*CUSTOM*') 'restore preserved existing profile data'
    Assert ((Get-Content -LiteralPath (Join-Path $root 'Start-Family-RetroBat.cmd') -Raw) -match '-HostLocal') 'host wrapper uses host-local mode'
    & $installer -RetroBatRoot $root -RestoreBackup $firstInstallBackup | Out-Null
    Assert ((Get-Content -LiteralPath $seedPath -Raw) -like '*CUSTOM*') 'rollback to pre-install backup retained profile data'
    & $installer -RetroBatRoot $root -PackagePath $zip | Out-Null

    $clientRoot = Join-Path $testRoot 'client\RetroBat'
    New-TestRoot $clientRoot -Client
    $dirPackage = Join-Path $testRoot 'directory-package'
    Expand-Archive -LiteralPath $zip -DestinationPath $dirPackage
    $digestLines = [System.Collections.Generic.List[string]]::new()
    foreach ($f in (Get-ChildItem -LiteralPath $dirPackage -File -Recurse | Sort-Object { $_.FullName.Substring($dirPackage.Length + 1).ToLowerInvariant() })) {
        $rel = $f.FullName.Substring($dirPackage.Length + 1).Replace('\','/')
        $digestLines.Add(((Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $rel))
    }
    $hashBytes = [Text.Encoding]::UTF8.GetBytes(($digestLines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $dirHash = ([BitConverter]::ToString($sha.ComputeHash($hashBytes))).Replace('-','') } finally { $sha.Dispose() }
    [IO.File]::WriteAllText($dirPackage + '.sha256',$dirHash)
    & $installer -RetroBatRoot $clientRoot -PackagePath $dirPackage | Out-Null
    Assert (-not (Test-Path -LiteralPath (Join-Path $clientRoot 'saves\family'))) 'client did not get local shared profile/save root'
    Assert ((Get-Content -LiteralPath (Join-Path $clientRoot 'Start-Family-RetroBat.cmd') -Raw) -match '-LocalRoot "%~dp0\."') 'client wrapper uses a safe local runtime root argument'

    $seedFailureRoot = Join-Path $testRoot 'seed-failure\RetroBat'
    New-TestRoot $seedFailureRoot
    New-Item -ItemType Directory -Path (Join-Path $seedFailureRoot 'saves\family\profiles.json') -Force | Out-Null
    $seedFailureRejected = $false
    try { & $installer -RetroBatRoot $seedFailureRoot -PackagePath $zip | Out-Null } catch { $seedFailureRejected = $true }
    Assert $seedFailureRejected 'installer rejected a non-file profile seed target'
    Assert (-not (Test-Path -LiteralPath (Join-Path $seedFailureRoot 'emulationstation\emulationstation.exe'))) 'seed failure rolled back newly installed frontend files'
    Assert (Test-Path -LiteralPath (Join-Path $seedFailureRoot 'saves\family\profiles.json') -PathType Container) 'seed failure preserved the conflicting user directory'

    $tampered = Join-Path $testRoot 'tampered.zip'
    Copy-Item -LiteralPath $zip -Destination $tampered
    [IO.File]::WriteAllText($tampered + '.sha256',('0' * 64))
    $rejected = $false
    try { & $installer -RetroBatRoot $root -PackagePath $tampered | Out-Null } catch { $rejected = $true }
    Assert $rejected 'installer rejected mismatched package checksum'
    Assert ((Get-Content -LiteralPath (Join-Path $root 'emulationstation\emulationstation.exe') -Raw) -eq 'fake-es-v2') 'checksum rejection left runtime unchanged'

    $legacy = Join-Path $backupRoot 'legacy-profile-entry'
    $legacySaved = Join-Path $legacy 'files\saves\family\profiles.json'
    New-Item -ItemType Directory -Path (Split-Path -Parent $legacySaved) -Force | Out-Null
    [IO.File]::WriteAllText($legacySaved,'[{"ClientId":"OLD","DisplayName":"Must not restore"}]')
    [pscustomobject]@{ Schema=1; Root=$root; Entries=@([pscustomobject]@{ Path='saves\family\profiles.json'; Existed=$true }) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $legacy 'manifest.json')
    & $installer -RetroBatRoot $root -RestoreBackup $legacy | Out-Null
    Assert ((Get-Content -LiteralPath $seedPath -Raw) -like '*CUSTOM*') 'restore ignored legacy profile-registry backup entry'

    $bad = Join-Path $backupRoot 'malicious-test'
    New-Item -ItemType Directory -Path $bad -Force | Out-Null
    $escape = Join-Path $testRoot 'escape.txt'
    [IO.File]::WriteAllText($escape,'untouched')
    [pscustomobject]@{ Schema=1; Root=$root; Entries=@([pscustomobject]@{ Path='..\escape.txt'; Existed=$true }) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bad 'manifest.json')
    $rejected = $false
    try { & $installer -RetroBatRoot $root -PackagePath $zip -RestoreBackup $bad | Out-Null } catch { $rejected = $true }
    Assert $rejected 'restore rejected path traversal in manifest'
    Assert ((Get-Content -LiteralPath $escape -Raw) -eq 'untouched') 'unsafe restore did not touch outside file'

    $unknown = Join-Path $backupRoot 'unknown-test'
    New-Item -ItemType Directory -Path (Join-Path $unknown 'files') -Force | Out-Null
    [pscustomobject]@{ Schema=1; Root=$root; Entries=@() } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $unknown 'manifest.json')
    [IO.File]::WriteAllText((Join-Path $unknown 'extra.txt'),'x')
    $rejected = $false
    try { & $installer -RetroBatRoot $root -PackagePath $zip -RestoreBackup $unknown | Out-Null } catch { $rejected = $true }
    Assert $rejected 'restore rejected unknown backup paths'
    Write-Output 'PASS: family frontend synthetic deployment, reapply, restore, client mode, profile preservation, and unsafe manifest checks.'
} finally {
    $fullTest = [IO.Path]::GetFullPath($testRoot)
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $workspace 'work')).TrimEnd('\') + '\'
    if (-not $fullTest.StartsWith($expectedParent,[StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing to remove test directory outside workspace work/.' }
    if (Test-Path -LiteralPath $fullTest) { Remove-Item -LiteralPath $fullTest -Recurse -Force }
}
