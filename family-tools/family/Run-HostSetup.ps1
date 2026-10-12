$ErrorActionPreference='Stop'
Start-Transcript -Path (Join-Path $PSScriptRoot 'host-setup.log') -Force
try { & (Join-Path $PSScriptRoot 'Setup-FamilyHost.ps1'); 'SUCCESS' | Set-Content (Join-Path $PSScriptRoot 'host-setup.status') }
catch { $_ | Out-String | Set-Content (Join-Path $PSScriptRoot 'host-setup.status'); Write-Error $_ }
finally { Stop-Transcript }
