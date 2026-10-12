#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SourceRoot,
    [string]$BuildRoot,
    [string]$LauncherBuildRoot,
    [string]$CMakePath = 'cmake'
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$workRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'work'))
if (-not (Test-Path -LiteralPath $workRoot -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $workRoot -Force
}
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    if (Test-Path -LiteralPath (Join-Path $workspaceRoot 'CMakeLists.txt') -PathType Leaf) {
        $SourceRoot = $workspaceRoot
    } else {
        $SourceRoot = Join-Path $workRoot 'emulationstation'
    }
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) { $BuildRoot = Join-Path $workspaceRoot 'work\emulationstation-build-baseline' }
if (-not $LauncherBuildRoot) { $LauncherBuildRoot = Join-Path $workRoot 'emulatorlauncher\emulatorLauncher\bin\x64\Release' }
foreach ($file in @('emulatorLauncher.exe', 'EmulatorLauncher.Common.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $LauncherBuildRoot $file) -PathType Leaf)) { throw "Build the matching thumbnail-fixed launcher first; missing $file in $LauncherBuildRoot" }
}
$outputRoot = [IO.Path]::GetFullPath($scriptRoot)
$sourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$buildRoot = [IO.Path]::GetFullPath($BuildRoot)

if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw "EmulationStation source root is invalid: $sourceRoot"
}
if (Test-Path -LiteralPath $CMakePath -PathType Leaf) {
    $cmake = [IO.Path]::GetFullPath($CMakePath)
} else {
    $cmakeCommand = Get-Command $CMakePath -CommandType Application -ErrorAction Stop
    $cmake = $cmakeCommand.Source
}
$ctestCandidate = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (Test-Path -LiteralPath $ctestCandidate -PathType Leaf) {
    $ctest = $ctestCandidate
} else {
    $ctestCommand = Get-Command ctest -CommandType Application -ErrorAction Stop
    $ctest = $ctestCommand.Source
}

$stageRoot = Join-Path $workRoot ('family-frontend-stage-' + [guid]::NewGuid().ToString('N'))
$tempArchive = Join-Path $workRoot ('family-frontend-' + [guid]::NewGuid().ToString('N') + '.zip')
$tempChecksum = "$tempArchive.sha256"
$packagePath = Join-Path $outputRoot 'family-frontend.zip'
$checksumPath = "$packagePath.sha256"

try {
    & $cmake -S $sourceRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64 -DRETROBAT=ON -DBUILD_FAMILY_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }

    & $cmake --build $buildRoot --config Release --target emulationstation family_profiles_test family_preferences_test family_preferences_switch_test family_profile_files_test family_usage_test --parallel 16
    if ($LASTEXITCODE -ne 0) { throw "Release build failed ($LASTEXITCODE)." }

    & $ctest --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "EmulationStation tests failed ($LASTEXITCODE)." }

    $nativeExe = Join-Path $sourceRoot 'bin\x64\Release\emulationstation.exe'
    if (-not (Test-Path -LiteralPath $nativeExe -PathType Leaf)) {
        throw "Release EmulationStation executable was not produced: $nativeExe"
    }

    $emulationStationStage = Join-Path $stageRoot 'emulationstation'
    $familyStage = Join-Path $stageRoot 'family'
    $null = New-Item -ItemType Directory -Path $familyStage -Force
    $null = New-Item -ItemType Directory -Path $emulationStationStage -Force
    Copy-Item -LiteralPath $nativeExe -Destination (Join-Path $emulationStationStage 'emulationstation.exe')
    foreach ($file in @('emulatorLauncher.exe', 'EmulatorLauncher.Common.dll')) {
        Copy-Item -LiteralPath (Join-Path $LauncherBuildRoot $file) -Destination (Join-Path $emulationStationStage $file)
    }

    $pickerBuildScript = Join-Path $outputRoot 'family\Build-FamilyProfilePicker.ps1'
    $pickerSource = Join-Path $outputRoot 'family\FamilyProfilePicker.cs'
    $profilesSource = Join-Path $outputRoot 'family\profiles.json'
    foreach ($required in @($pickerBuildScript, $pickerSource, $profilesSource)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Family frontend input is missing: $required" }
    }
    Copy-Item -LiteralPath $pickerBuildScript -Destination (Join-Path $familyStage 'Build-FamilyProfilePicker.ps1')

    $pickerExe = Join-Path $emulationStationStage 'FamilyProfilePicker.exe'
    & (Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $pickerBuildScript -OutputPath $pickerExe
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $pickerExe -PathType Leaf)) {
        throw 'Family profile picker build failed.'
    }

    Copy-Item -LiteralPath $pickerSource -Destination (Join-Path $familyStage 'FamilyProfilePicker.cs')
    Copy-Item -LiteralPath $profilesSource -Destination (Join-Path $familyStage 'profiles.json')
    $familyScripts = @(
        'Family.Common.ps1',
        'Run-HostSetup.ps1',
        'Setup-FamilyClient.ps1',
        'Setup-FamilyHost.ps1',
        'Start-FamilyRetroBat.ps1'
    )
    foreach ($scriptName in $familyScripts) {
        $scriptSource = Join-Path $outputRoot ('family\' + $scriptName)
        if (-not (Test-Path -LiteralPath $scriptSource -PathType Leaf)) { throw "Family frontend script is missing: $scriptSource" }
        Copy-Item -LiteralPath $scriptSource -Destination (Join-Path $familyStage $scriptName)
    }

    $rootLauncher = Join-Path $outputRoot 'Start-Family-RetroBat.cmd'
    if (-not (Test-Path -LiteralPath $rootLauncher -PathType Leaf)) { throw "Family launcher is missing: $rootLauncher" }
    Copy-Item -LiteralPath $rootLauncher -Destination (Join-Path $stageRoot 'Start-Family-RetroBat.cmd')

    Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $tempArchive -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $tempArchive -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($tempChecksum, "$hash`n", [Text.Encoding]::ASCII)
    Move-Item -LiteralPath $tempArchive -Destination $packagePath -Force
    Move-Item -LiteralPath $tempChecksum -Destination $checksumPath -Force

    Write-Host "Packaged $packagePath"
    Write-Host "SHA256 $hash"
}
finally {
    $workPrefix = $workRoot.TrimEnd('\') + '\'
    foreach ($temporaryPath in @($stageRoot, $tempArchive, $tempChecksum)) {
        if (-not $temporaryPath -or -not (Test-Path -LiteralPath $temporaryPath)) { continue }
        $resolvedTemporaryPath = [IO.Path]::GetFullPath($temporaryPath)
        if (-not $resolvedTemporaryPath.StartsWith($workPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean temporary path outside work/: $resolvedTemporaryPath"
        }
        Remove-Item -LiteralPath $resolvedTemporaryPath -Recurse -Force
    }
}
