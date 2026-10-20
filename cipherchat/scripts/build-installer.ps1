[CmdletBinding()]
param(
    [string]$Package = 'artifacts/CipherChat-Remote-Windows-x64-contacts-20260920',
    [string]$Output = 'artifacts/CipherChat-2026.9.20.1-Windows-x64.msi',
    [string]$Work = 'build/installer-20260920'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    python scripts/installer/build_msi.py --package $Package --output $Output --work $Work
    if ($LASTEXITCODE -ne 0) { throw 'Installer build failed; inspect the build directory.' }
} finally { Pop-Location }
