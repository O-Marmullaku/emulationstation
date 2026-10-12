#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$PackagePath,
    [string]$RetroBatRoot = 'B:\GAMES_permanent\MiddleMan\RetroBat',
    [string]$RestoreBackup
)
$ErrorActionPreference = 'Stop'

function Test-Within([string]$Parent,[string]$Child) {
    $p = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    $c = [IO.Path]::GetFullPath($Child)
    return $c.StartsWith($p,[StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoReparseAncestors([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $drive = [IO.Path]::GetPathRoot($full)
    $tail = $full.Substring($drive.Length).Split([char[]]@('\','/'),[StringSplitOptions]::RemoveEmptyEntries)
    $current = $drive.TrimEnd('\')
    foreach ($part in $tail) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing a reparse-point path component: $current" }
        }
    }
}
function Assert-SafeRelative([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative -match '[:<>"|?*]' -or $Relative -match '[\. ]([\\/]|$)') { throw "Unsafe relative path in manifest: $Relative" }
    $r = $Relative.Replace('/','\')
    if ($r -match '(^|\\)\.(\\|$)' -or $r -match '[\x00-\x1f]') { throw "Invalid relative path: $Relative" }
    return $r
}
function Assert-ManagedRelative([string]$Relative) {
    $r = Assert-SafeRelative $Relative
    if ($r -notmatch '^(emulationstation|family)\\' -and $r -notin @('Start-Family-RetroBat.cmd','saves\family\profiles.json')) { throw "Path is outside the family installer manifest scope: $r" }
    return $r
}
function Get-DirectoryDigest([string]$Path) {
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($f in (Get-ChildItem -LiteralPath $Path -File -Recurse | Sort-Object { $_.FullName.Substring($Path.TrimEnd('\').Length + 1).ToLowerInvariant() })) {
        if (($f.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package contains a reparse point: $($f.FullName)" }
        $rel = $f.FullName.Substring($Path.TrimEnd('\').Length + 1).Replace('\','/')
        $lines.Add(((Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $rel))
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n") + "`n")
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','') } finally { $sha.Dispose() }
}
function Assert-Package([string]$Path) {
    Assert-NoReparseAncestors $Path
    $isDir = Test-Path -LiteralPath $Path -PathType Container
    if (-not $isDir -and -not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Package not found: $Path" }
    $sidecar = $Path + '.sha256'
    if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) { throw "Validated SHA-256 sidecar is required: $sidecar" }
    $expected = ((Get-Content -LiteralPath $sidecar -Raw).Trim() -split '\s+')[0]
    if ($expected -notmatch '^[0-9A-Fa-f]{64}$') { throw 'Package SHA-256 sidecar is malformed.' }
    $actual = if ($isDir) { Get-DirectoryDigest $Path } else { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    if ($actual -ine $expected) { throw 'Package checksum mismatch.' }
    return $isDir
}
function Read-Manifest([string]$BackupPath) {
    $backupRoot = Join-Path (Split-Path -Parent $root) 'RetroBat-family-backups'
    $full = [IO.Path]::GetFullPath($BackupPath).TrimEnd('\')
    if (-not (Test-Within $backupRoot $full) -or -not (Test-Path -LiteralPath $full -PathType Container)) { throw 'RestoreBackup must be an existing child of RetroBat-family-backups.' }
    Assert-NoReparseAncestors $full
    $manifestPath = Join-Path $full 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Backup manifest.json is missing.' }
    $m = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($m.Schema -ne 1 -or -not ($m.Entries -is [array]) -or [string]$m.Root -ine $root) { throw 'Backup manifest schema or installation root is invalid.' }
    $entries = [System.Collections.Generic.List[object]]::new()
    $seen = @{}
    foreach ($e in $m.Entries) {
        $rel = Assert-ManagedRelative ([string]$e.Path)
        if ($seen.ContainsKey($rel.ToLowerInvariant()) -or $e.Existed -isnot [bool]) { throw 'Backup manifest has duplicate or invalid entries.' }
        $seen[$rel.ToLowerInvariant()] = $true
        $saved = Join-Path $full (Join-Path 'files' $rel)
        if ($e.Existed -and -not (Test-Path -LiteralPath $saved -PathType Leaf)) { throw "Backup file is missing: $rel" }
        if (-not $e.Existed -and (Test-Path -LiteralPath $saved)) { throw "Unexpected backup file for absent path: $rel" }
        $entries.Add([pscustomobject]@{ Path=$rel; Existed=[bool]$e.Existed; Saved=$saved })
    }
    $expectedFiles = @('manifest.json') + @($entries | Where-Object Existed | ForEach-Object { 'files\' + $_.Path })
    $backupItems = @(Get-ChildItem -LiteralPath $full -Force -Recurse)
    if (@($backupItems | Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 }).Count) { throw 'Backup contains a reparse point.' }
    $actualFiles = @($backupItems | Where-Object { -not $_.PSIsContainer } | ForEach-Object { $_.FullName.Substring($full.Length + 1).Replace('/','\') })
    if (@(Compare-Object ($expectedFiles | Sort-Object) ($actualFiles | Sort-Object)).Count -ne 0) { throw 'Backup contains unknown or unlisted files.' }
    $allowedDirs = @{}
    foreach ($relFile in $expectedFiles) {
        $parts = $relFile -split '\\'
        for ($i=1; $i -lt $parts.Count; $i++) { $allowedDirs[($parts[0..($i-1)] -join '\').ToLowerInvariant()] = $true }
    }
    foreach ($d in ($backupItems | Where-Object PSIsContainer)) {
        $relDir = $d.FullName.Substring($full.Length + 1).Replace('/','\').ToLowerInvariant()
        if (-not $allowedDirs.ContainsKey($relDir)) { throw "Backup contains an unknown directory: $relDir" }
    }
    return ,$entries.ToArray()
}
function New-Backup([object[]]$Paths) {
    $parent = Split-Path -Parent $root
    $backupRoot = Join-Path $parent 'RetroBat-family-backups'
    Assert-NoReparseAncestors $backupRoot
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    $dir = Join-Path $backupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $dir | Out-Null
    $items = [System.Collections.Generic.List[object]]::new()
    foreach ($rel0 in $Paths) {
        $rel = Assert-ManagedRelative ([string]$rel0)
        $dest = Join-Path $root $rel
        if (-not (Test-Within $root $dest)) { throw 'Deployment path escapes RetroBat root.' }
        Assert-NoReparseAncestors $dest
        $exists = Test-Path -LiteralPath $dest -PathType Leaf
        $items.Add([pscustomobject]@{ Path=$rel; Existed=$exists })
        if ($exists) {
            $save = Join-Path (Join-Path $dir 'files') $rel
            New-Item -ItemType Directory -Path (Split-Path -Parent $save) -Force | Out-Null
            Copy-Item -LiteralPath $dest -Destination $save
        } elseif (Test-Path -LiteralPath $dest) { throw "Refusing to replace a non-file deployment target: $dest" }
    }
    [pscustomobject]@{ Schema=1; Root=$root; Entries=@($items.ToArray()) } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dir 'manifest.json') -Encoding UTF8
    return $dir
}
function Restore-Entries([object[]]$Entries) {
    foreach ($e in $Entries) {
        if ([string]$e.Path -ieq 'saves\family\profiles.json') { continue }
        $dest = Join-Path $root $e.Path
        if (-not (Test-Within $root $dest)) { throw 'Restore target escapes RetroBat root.' }
        Assert-NoReparseAncestors $dest
        if ($e.Existed) {
            Assert-NoReparseAncestors $e.Saved
            New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
            Copy-Item -LiteralPath $e.Saved -Destination $dest -Force
        } elseif (Test-Path -LiteralPath $dest -PathType Leaf) { Remove-Item -LiteralPath $dest -Force }
        elseif (Test-Path -LiteralPath $dest) { throw "Restore target became a non-file: $dest" }
    }
}

$root = [IO.Path]::GetFullPath($RetroBatRoot).TrimEnd('\')
Assert-NoReparseAncestors $root
if (-not (Test-Path -LiteralPath (Join-Path $root 'RetroBat.exe') -PathType Leaf)) { throw "RetroBatRoot must be an existing RetroBat installation: $root" }
$runningNames = @('emulationstation.exe','emulatorLauncher.exe','pcsx2-qt.exe','pcsx2.exe','pcsx2x64.exe')
$live = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -in $runningNames -and $_.ExecutablePath -and (Test-Within $root ([string]$_.ExecutablePath))
})
if ($live.Count) { throw ('Close processes running from this RetroBat root first: ' + (($live | ForEach-Object { "$($_.Name) PID $($_.ProcessId)" }) -join ', ')) }

$backupParent = Join-Path (Split-Path -Parent $root) 'RetroBat-family-backups'
$activeBackup = $null
$stage = $null
$created = [System.Collections.Generic.List[string]]::new()
$packageRoot = $null
try {
    if ($RestoreBackup) {
        $restoreEntries = Read-Manifest $RestoreBackup
        $activeBackup = New-Backup @($restoreEntries | Where-Object { $_.Path -ine 'saves\family\profiles.json' } | ForEach-Object Path)
        try { Restore-Entries $restoreEntries }
        catch { Restore-Entries (Read-Manifest $activeBackup); throw }
        Write-Output "Restored family frontend files from: $RestoreBackup"
        Write-Output "Undo backup: $activeBackup"
        return
    }
    if ([string]::IsNullOrWhiteSpace($PackagePath)) { throw 'PackagePath is required unless RestoreBackup is specified.' }
    $isDirectory = Assert-Package $PackagePath
    if ($isDirectory) { $packageRoot = [IO.Path]::GetFullPath($PackagePath) }
    else {
        $stage = Join-Path (Split-Path -Parent $root) ('.family-package-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $stage | Out-Null
        Expand-Archive -LiteralPath $PackagePath -DestinationPath $stage
        $packageRoot = $stage
    }
    foreach ($item in (Get-ChildItem -LiteralPath $packageRoot -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package contains a reparse point: $($item.FullName)" }
    }
    foreach ($required in @('emulationstation\emulationstation.exe','emulationstation\FamilyProfilePicker.exe','family\Start-FamilyRetroBat.ps1','family\Family.Common.ps1','family\profiles.json','Start-Family-RetroBat.cmd')) {
        if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $required) -PathType Leaf)) { throw "Package missing required file: $required" }
    }
    $client = Test-Path -LiteralPath (Join-Path $root 'family-client.json') -PathType Leaf
    $relativeFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($sub in @('emulationstation','family')) {
        $base = Join-Path $packageRoot $sub
        foreach ($file in (Get-ChildItem -LiteralPath $base -File -Recurse)) {
            if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Package contains a reparse point: $($file.FullName)" }
            $relativeFiles.Add((Assert-ManagedRelative $file.FullName.Substring($packageRoot.TrimEnd('\').Length + 1)))
        }
    }
    $relativeFiles.Add('Start-Family-RetroBat.cmd')
    $relativeFiles = @($relativeFiles | Sort-Object -Unique)
    $activeBackup = New-Backup $relativeFiles
    try {
        foreach ($rel in $relativeFiles) {
            $dest = Join-Path $root $rel
            if (-not (Test-Within $root $dest)) { throw 'Deployment path escapes RetroBat root.' }
            Assert-NoReparseAncestors $dest
            if ($rel -eq 'Start-Family-RetroBat.cmd') {
                $mode = if ($client) { '-LocalRoot "%~dp0."' } else { '-HostLocal' }
                $cmd = "@echo off`r`nsetlocal`r`npowershell.exe -NoProfile -ExecutionPolicy Bypass -File `"%~dp0family\Start-FamilyRetroBat.ps1`" $mode`r`nexit /b %ERRORLEVEL%`r`n"
                New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
                [IO.File]::WriteAllText($dest,$cmd,[Text.Encoding]::ASCII)
            } else {
                New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
                Copy-Item -LiteralPath (Join-Path $packageRoot $rel) -Destination $dest -Force
            }
            $created.Add($rel)
        }
        if (-not $client) {
            $seedPath = Join-Path $root 'saves\family\profiles.json'
            Assert-NoReparseAncestors $seedPath
            if (-not (Test-Path -LiteralPath $seedPath -PathType Leaf)) {
                if (Test-Path -LiteralPath $seedPath) { throw "Profile seed target is not a file: $seedPath" }
                New-Item -ItemType Directory -Path (Split-Path -Parent $seedPath) -Force | Out-Null
                [IO.File]::Copy((Join-Path $packageRoot 'family\profiles.json'),$seedPath,$false)
            }
        }
    } catch {
        Restore-Entries (Read-Manifest $activeBackup)
        throw
    }
    Write-Output "Installed family frontend files under: $root"
    Write-Output "Backup: $activeBackup"
    if ($client) { Write-Output 'Client runtime detected; no local family saves were seeded.' }
    else { Write-Output 'Host runtime detected; shared profile seed was added only if absent.' }
} finally {
    if ($stage -and (Test-Path -LiteralPath $stage)) {
        if (-not (Test-Within (Split-Path -Parent $root) $stage)) { throw 'Refusing to remove unsafe package staging path.' }
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
