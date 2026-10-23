param(
    [ValidateSet('core', 'ui', 'layout', 'all')]
    [string]$Suite = 'core',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$frameworkRoot = Split-Path -Parent $PSScriptRoot
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;D:\msys64\mingw64\bin;' + $env:PATH
Push-Location -LiteralPath $frameworkRoot
try {
    if (-not $SkipBuild) {
        & cmake --preset verification
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & cmake --build --preset verification -j 4
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $arguments = @('--preset', 'verification', '--no-tests=error')
    if ($Suite -ne 'all') { $arguments += @('-L', "^$Suite`$") }
    & ctest @arguments
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
