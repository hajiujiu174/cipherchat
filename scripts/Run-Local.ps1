[CmdletBinding()]
param([ValidateSet('Demo','Client','Server')][string]$Mode = 'Demo')

$ErrorActionPreference = 'Stop'
$entry = Join-Path $PSScriptRoot '..\.local\wsl-demo\Start-NEXTALK-WSL.ps1'
switch ($Mode) {
    'Demo' { & $entry }
    'Client' { & $entry -Single }
    'Server' { & $entry -ServerOnly }
}
