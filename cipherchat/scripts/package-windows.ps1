[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [string]$Destination = '',
    [string]$QtDirectory = 'C:\Qt\6.11.2\mingw_64',
    [string]$MingwDirectory = 'D:\msys64\mingw64'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build\desktop-release' }
if (-not $Destination) { $Destination = Join-Path $projectRoot 'artifacts\CipherChat-Windows-x64' }
if (Test-Path -LiteralPath $Destination) { throw 'Destination already exists. Choose a new -Destination; existing packages are never removed.' }
$inputs = @(
    @{Source=(Join-Path $BuildDirectory 'framework\cipherchat.exe'); Target='cipherchat.exe'},
    @{Source=(Join-Path $BuildDirectory 'server\cipherchat-server.exe'); Target='cipherchat-server.exe'},
    @{Source=(Join-Path $QtDirectory 'plugins\platforms\qwindows.dll'); Target='platforms\qwindows.dll'},
    @{Source=(Join-Path $QtDirectory 'plugins\styles\qmodernwindowsstyle.dll'); Target='styles\qmodernwindowsstyle.dll'},
    @{Source=(Join-Path $PSScriptRoot 'start-demo.ps1'); Target='start-demo.ps1'},
    @{Source=(Join-Path $projectRoot 'README.md'); Target='README.md'},
    @{Source=(Join-Path $projectRoot 'docs\THIRD_PARTY.md'); Target='THIRD_PARTY.md'}
)
foreach ($inputFile in $inputs) { if (-not (Test-Path -LiteralPath $inputFile.Source)) { throw "Missing input: $($inputFile.Source)" } }
[IO.Directory]::CreateDirectory($Destination) | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path
foreach ($inputFile in $inputs) {
    $target = Join-Path $Destination $inputFile.Target
    [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
    Copy-Item -LiteralPath $inputFile.Source -Destination $target
}
Set-Content -LiteralPath (Join-Path $Destination 'qt.conf') -Value "[Paths]`nPlugins=." -Encoding ASCII
$queue = New-Object 'Collections.Generic.Queue[string]'
Get-ChildItem -LiteralPath $Destination -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if ($seen.ContainsKey($binary)) { continue }
    $seen[$binary] = $true
    $imports = & (Join-Path $MingwDirectory 'bin\objdump.exe') -p $binary
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch 'DLL Name:\s+(\S+)') { continue }
        $dll = $Matches[1]
        if ($dll -match '^(api-ms-|ext-ms-)' -or (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$dll"))) { continue }
        $target = Join-Path $Destination $dll
        if (Test-Path -LiteralPath $target) { continue }
        # GCC 16 runtime must come from the compiler used for this project.
        $roots = if ($dll -like 'Qt6*') { @("$QtDirectory\bin","$MingwDirectory\bin") } else { @("$MingwDirectory\bin","$QtDirectory\bin") }
        $source = $null
        foreach ($root in $roots) { $candidate = Join-Path $root $dll; if (Test-Path -LiteralPath $candidate) { $source = $candidate; break } }
        if (-not $source) { throw "Unresolved dependency $dll imported by $binary" }
        Copy-Item -LiteralPath $source -Destination $target
        $queue.Enqueue($target)
    }
}
$licenseRoot = Join-Path $Destination 'licenses'
[IO.Directory]::CreateDirectory($licenseRoot) | Out-Null
foreach ($library in 'gcc-libs','openssl','sqlite3','winpthreads','zlib','zstd','libb2','pcre2','harfbuzz','freetype','libpng','double-conversion','brotli') {
    $source = Join-Path $MingwDirectory "share\licenses\$library"
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $licenseRoot -Recurse }
}
$qtLicenses = Join-Path $projectRoot 'docs\licenses'
if (Test-Path -LiteralPath $qtLicenses) { Copy-Item -LiteralPath $qtLicenses -Destination (Join-Path $licenseRoot 'qt') -Recurse }
$files = @(Get-ChildItem -LiteralPath $Destination -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{path=$_.FullName.Substring($Destination.Length+1).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{format=1; built_at=[DateTime]::UtcNow.ToString('o'); configuration='Release'; architecture='x64'; files=$files} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding UTF8
Write-Output "Packaged $($files.Count) files in $Destination. Run M6-01 before creating the release ZIP."
