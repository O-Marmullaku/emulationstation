#Requires -Version 5.1
[CmdletBinding()]
param([string]$OutputPath)
$ErrorActionPreference='Stop'
if (-not $OutputPath) { $OutputPath = Join-Path $PSScriptRoot 'FamilyProfilePicker.exe' }
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path -LiteralPath $csc)){throw "C# compiler not found: $csc"}
$out=[IO.Path]::GetFullPath($OutputPath)
$parent=Split-Path -Parent $out
if(-not(Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Path $parent -Force|Out-Null}
& $csc /nologo /target:winexe /optimize+ /out:$out /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll (Join-Path $PSScriptRoot 'FamilyProfilePicker.cs')
if($LASTEXITCODE -ne 0){throw "C# compile failed ($LASTEXITCODE)."}
Write-Host "Built $out"

