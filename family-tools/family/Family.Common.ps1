Set-StrictMode -Version Latest

function Assert-FamilyClientId {
    param([Parameter(Mandatory)][string]$ClientId)
    if ($ClientId -notmatch '^(?:[A-Za-z0-9][A-Za-z0-9_-]{0,39}|profile-[0-9A-Fa-f]{32})$') {
        throw 'ClientId must be a short safe name or profile- followed by a GUID.'
    }
}

function Resolve-FamilyClientAppearance {
    param(
        [Parameter(Mandatory)][string]$ClientId,
        [Parameter(Mandatory)][string]$ProfilesPath,
        [Parameter(Mandatory)][string]$DefaultDisplayName,
        [AllowNull()][string]$DisplayName,
        [bool]$DisplayNameSpecified = $false,
        [AllowNull()][string]$AccentColor,
        [bool]$AccentColorSpecified = $false
    )
    Assert-FamilyClientId $ClientId
    $profile = $null
    if (Test-Path -LiteralPath $ProfilesPath -PathType Leaf) {
        $profiles = @(Get-Content -LiteralPath $ProfilesPath -Raw | ConvertFrom-Json)
        $profile = $profiles | Where-Object { [string]$_.ClientId -ieq $ClientId } | Select-Object -First 1
    }
    if (-not $DisplayNameSpecified) {
        $DisplayName = if ($profile -and $profile.DisplayName) { [string]$profile.DisplayName } else { $DefaultDisplayName }
    }
    if (-not $AccentColorSpecified) {
        $AccentColor = if ($profile -and $profile.AccentColor) { [string]$profile.AccentColor } else { '#5588CC' }
    }
    if ($AccentColor -notmatch '^#[0-9A-Fa-f]{6}$') { throw "AccentColor must be a six-digit hex color: $AccentColor" }
    return [pscustomobject]@{ DisplayName = $DisplayName; AccentColor = $AccentColor }
}

function Assert-FamilyPathWithin {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Child)
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $childPath = [IO.Path]::GetFullPath($Child)
    if (-not $childPath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing recursive operation outside the expected folder. Root: $rootPath Child: $childPath"
    }
    return $childPath
}

function Connect-FamilyShare {
    param(
        [Parameter(Mandatory)][ValidateSet('R','S')][string]$Drive,
        [Parameter(Mandatory)][string]$RemotePath,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Account
    )

    $localPath = $Drive + ':'
    $mapping = Get-SmbMapping -LocalPath $localPath -ErrorAction SilentlyContinue
    if ($mapping) {
        if ($mapping.RemotePath.TrimEnd('\') -ieq $RemotePath.TrimEnd('\')) { return }
        throw "$localPath is already mapped to $($mapping.RemotePath); disconnect it or choose another drive letter."
    }
    if (Test-Path $localPath) { throw "$localPath is already in use by a local drive or an unrecognized mapping." }

    $net = Join-Path $env:WINDIR 'System32\net.exe'
    if ($Account) { & $net use $localPath $RemotePath "/user:$Account" '*' '/persistent:no' | Out-Host }
    else { & $net use $localPath $RemotePath '/persistent:no' | Out-Host }
    if ($LASTEXITCODE -ne 0) { throw "Could not map $localPath to $RemotePath. The password prompt must complete successfully." }

    $mapping = Get-SmbMapping -LocalPath $localPath -ErrorAction SilentlyContinue
    if (-not $mapping -or $mapping.RemotePath.TrimEnd('\') -ine $RemotePath.TrimEnd('\')) {
        throw "Windows did not confirm the expected mapping for $localPath."
    }
}

function Connect-FamilyShares {
    param([Parameter(Mandatory)][pscustomobject]$Settings)
    Assert-FamilyClientId $Settings.ClientId
    if ($Settings.HostName -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$') { throw 'HostName must be a LAN hostname or IPv4 address.' }
    if ($Settings.Account -and $Settings.Account -notmatch '^[A-Za-z0-9_.-]+\\[A-Za-z0-9_.-]+$') { throw 'Account must be HOST\User, with no password.' }
    $account = [string]$Settings.Account
    Connect-FamilyShare -Drive R -RemotePath "\\$($Settings.HostName)\RetroBatFamilyRead" -Account $account
    Connect-FamilyShare -Drive S -RemotePath "\\$($Settings.HostName)\RetroBatFamilySaves" -Account $account
}

function Set-FamilyLauncherConfig {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ClientId,
        [string]$BiosRoot = 'R:\bios',
        [string]$RomsRoot = 'R:\roms',
        [string]$SavesRoot
    )

    Assert-FamilyClientId $ClientId
    if (-not $SavesRoot) { $SavesRoot = "S:\$ClientId" }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Launcher config not found: $Path" }

    $lines = [System.Collections.Generic.List[string]]::new()
    [IO.File]::ReadAllLines($Path) | ForEach-Object { $lines.Add($_) }
    foreach ($entry in @(@('bios', $BiosRoot), @('roms', $RomsRoot), @('saves', $SavesRoot))) {
        $key = [string]$entry[0]
        $value = [string]$entry[1]
        $pattern = '^\s*' + [regex]::Escape($key) + '\s*=.*$'
        $found = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match $pattern) { $lines[$i] = "$key=$value"; $found = $true }
        }
        if (-not $found) { $lines.Add("$key=$value") }
    }
    [IO.File]::WriteAllText($Path, ($lines -join "`r`n"), [Text.UTF8Encoding]::new($false))
}

function Set-FamilyPs2BiosArgument {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$BiosRoot)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "EmulationStation systems config not found: $Path" }
    $document = [System.Xml.XmlDocument]::new()
    $document.PreserveWhitespace = $true
    $document.Load($Path)
    $commandNode = $document.SelectSingleNode("/systemList/system[name='ps2']/command")
    if (-not $commandNode) { throw "The PS2 system command was not found in $Path." }
    $command = $commandNode.InnerText
    $command = [regex]::Replace($command, '\s+-bios\s+(?:"[^"]*"|\S+)', '')
    $romArgument = [regex]::new('\s+-rom\s+%ROM%', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $romArgument.IsMatch($command)) { throw 'The PS2 command has no standard -rom %ROM% argument; refusing to change it.' }
    $commandNode.InnerText = $romArgument.Replace($command, " -bios $BiosRoot -rom %ROM%", 1)
    $document.Save($Path)
}
function Initialize-FamilyClientBios {
    param([Parameter(Mandatory)][string]$SourceRoot, [Parameter(Mandatory)][string]$SaveRoot)
    $source = Join-Path $SourceRoot 'pcsx2\bios'
    if (-not (Test-Path -LiteralPath $source -PathType Container) -or
        -not (Get-ChildItem -LiteralPath $source -Filter '*.bin' -File -Recurse | Select-Object -First 1)) {
        throw "No PCSX2 BIOS image was found under $source."
    }
    $target = Join-Path $SaveRoot 'bios\pcsx2\bios'
    $targetRoot = Split-Path -Parent $target
    if (Test-Path -LiteralPath $targetRoot) {
        if ((Test-Path -LiteralPath $target -PathType Container) -and
            (Get-ChildItem -LiteralPath $target -Filter '*.bin' -File -Recurse | Select-Object -First 1)) { return }
        throw "Per-client PCSX2 BIOS folder already exists but has no BIOS image; refusing to overwrite it: $targetRoot"
    }
    $stage = Join-Path $SaveRoot ('.bios-staging-' + [guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $stage | Out-Null
        $robocopy = Join-Path $env:WINDIR 'System32\robocopy.exe'
        & $robocopy $source (Join-Path $stage 'bios') /E /COPY:DAT /DCOPY:DAT /R:1 /W:1 /XJ | Out-Host
        if ($LASTEXITCODE -ge 8) { throw "Could not copy the shared PCSX2 BIOS (Robocopy exit code $LASTEXITCODE)." }
        if (-not (Get-ChildItem -LiteralPath (Join-Path $stage 'bios') -Filter '*.bin' -File -Recurse | Select-Object -First 1)) {
            throw 'The copied per-client BIOS folder contains no BIOS image.'
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Move-Item -LiteralPath (Join-Path $stage 'bios') -Destination $target
    } catch {
        if (Test-Path -LiteralPath $stage) {
            [void](Assert-FamilyPathWithin -Root $SaveRoot -Child $stage)
            Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        }
        throw
    }
}

function Set-FamilyRomPaths {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "EmulationStation systems config not found: $Path" }
    $before = [IO.File]::ReadAllText($Path)
    $pattern = '~\\\.\.\\roms\\'
    $count = [regex]::Matches($before, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase).Count
    $after = [regex]::Replace($before, $pattern, 'R:\roms\', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($count -eq 0) { throw "No standard RetroBat ROM paths were found in $Path; refusing to change an unexpected config." }
    [IO.File]::WriteAllText($Path, $after, [Text.UTF8Encoding]::new($false))
    return $count
}

function New-FamilySessionLock {
    param([Parameter(Mandatory)][string]$RootPath)
    $lockPath = Join-Path $RootPath '.retrobat-session.lock'
    try {
        $stream = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $stream.SetLength(0)
        $bytes = [Text.Encoding]::UTF8.GetBytes("$env:COMPUTERNAME $PID $([DateTime]::UtcNow.ToString('o'))")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return $stream
    } catch {
        throw "Could not acquire the exclusive RetroBat runtime lock in $RootPath. Another RetroBat session may already use this client ID, or SMB is unavailable. $($_.Exception.Message)"
    }
}





