[CmdletBinding()]
param([switch]$ThirdClient)

$ErrorActionPreference = 'Stop'
$entry = Join-Path $PSScriptRoot '..\..\..\NEXTALK-Refactor\.local\wsl-demo\Start-NEXTALK-WSL.ps1'
& $entry -ThirdClient:$ThirdClient
