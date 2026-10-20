[CmdletBinding()]
param(
    [string]$Installer = 'artifacts/CipherChat-2026.9.20.1-Windows-x64.msi',
    [string]$Work = ''
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    if (-not $Work) { $Work = 'build/installer-validation-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8) }
    python scripts/installer/test_msi.py --installer $Installer --work $Work
    if ($LASTEXITCODE -ne 0) { throw 'Installer validation failed; logs preserved in the work directory.' }
} finally { Pop-Location }
