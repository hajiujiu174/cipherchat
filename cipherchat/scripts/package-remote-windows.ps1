[CmdletBinding()]
param(
    [string]$Destination = '',
    [string]$BasePackage = '',
    [string]$Certificate = '',
    [string]$ClientBinary = '',
    [string]$ServerAddress = '2603:1040:f02:d::21d',
    [string]$Launcher = '',
    [string]$Readme = '',
    [string]$UpdaterBinary = '',
    [string]$BootstrapBinary = '',
    [string]$ReleaseVersion = '',
    [UInt64]$ReleaseSequence = 0,
    [string]$ReleaseNotes = '',
    [string]$UpdateFeed = '',
    [string]$QmlDirectory = '',
    [string]$ThirdPartyNotice = '',
    [string]$CompilerRuntimeDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $projectRoot 'artifacts\CipherChat-Remote-Windows-x64-server2-final' }
if (-not $BasePackage) { $BasePackage = Join-Path $projectRoot 'artifacts\CipherChat-Windows-x64' }
if (-not $Certificate) { $Certificate = Join-Path $projectRoot 'artifacts\server-migration-20260915\server-cert.pem' }
if (-not $ClientBinary) { $ClientBinary = Join-Path $projectRoot 'build\file-transfer-release\cipherchat.exe' }
if (-not $Launcher) { $Launcher = Join-Path $PSScriptRoot 'start-remote.cmd' }
if (-not $Readme) { $Readme = Join-Path $projectRoot 'docs\remote-client.md' }
if (Test-Path -LiteralPath $Destination) { throw 'Destination exists. Choose a new directory; existing releases are preserved.' }
if (Test-Path -LiteralPath ($Destination + '.zip')) { throw 'ZIP exists. Choose a new destination.' }
$BasePackage = (Resolve-Path -LiteralPath $BasePackage).Path
$Certificate = (Resolve-Path -LiteralPath $Certificate).Path
$ClientBinary = (Resolve-Path -LiteralPath $ClientBinary).Path
$inputs = @(
    @{Source=$ClientBinary; Target='cipherchat.exe'},
    @{Source=(Join-Path $BasePackage 'qt.conf'); Target='qt.conf'},
    @{Source=(Join-Path $BasePackage 'THIRD_PARTY.md'); Target='THIRD_PARTY.md'},
    @{Source=$Launcher; Target='start-remote.cmd'},
    @{Source=$Readme; Target='README.md'},
    @{Source=$Certificate; Target='server-cert.pem'}
)
foreach ($dll in Get-ChildItem -LiteralPath $BasePackage -File -Filter '*.dll') {
    $inputs += @{Source=$dll.FullName; Target=$dll.Name}
}
foreach ($item in $inputs) {
    if (-not (Test-Path -LiteralPath $item.Source -PathType Leaf)) { throw "Missing file: $($item.Source)" }
}
[IO.Directory]::CreateDirectory($Destination) | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path
foreach ($item in $inputs) { Copy-Item -LiteralPath $item.Source -Destination (Join-Path $Destination $item.Target) }
foreach ($directory in 'platforms','styles','licenses') {
    Copy-Item -LiteralPath (Join-Path $BasePackage $directory) -Destination (Join-Path $Destination $directory) -Recurse
}
if ($QmlDirectory) {
    $QmlDirectory = (Resolve-Path -LiteralPath $QmlDirectory).Path
    $deploy = 'C:\Qt\6.11.2\mingw_64\bin\windeployqt.exe'
    if (-not (Test-Path -LiteralPath $deploy)) { throw 'The configured Qt deployment tool is missing.' }
    & $deploy --release --force --no-translations --no-compiler-runtime --qmldir $QmlDirectory (Join-Path $Destination 'cipherchat.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt Quick / 3D dependency deployment failed.' }
    $rhineLicenses = Join-Path $Destination 'licenses\rhinelab'
    New-Item -ItemType Directory -Path $rhineLicenses | Out-Null
    foreach ($name in 'LICENSE','NOTICE.md') { Copy-Item -LiteralPath (Join-Path $projectRoot "third_party\rhinelab\$name") -Destination $rhineLicenses }
    foreach ($module in 'qtquick3d','qtshadertools','qtqml','qtquick') {
        $notices = @(Get-ChildItem -LiteralPath "C:\Qt\Docs\Qt-6.11.2\$module" -Filter '*attribution*.html' -File -ErrorAction SilentlyContinue)
        if ($notices.Count) {
            $noticeDirectory = Join-Path $Destination "licenses\$module"
            New-Item -ItemType Directory -Path $noticeDirectory | Out-Null
            foreach ($notice in $notices) { Copy-Item -LiteralPath $notice.FullName -Destination $noticeDirectory }
        }
    }
}
if ($CompilerRuntimeDirectory) {
    foreach ($runtime in 'libstdc++-6.dll','libgcc_s_seh-1.dll','libwinpthread-1.dll') {
        Copy-Item -LiteralPath (Join-Path $CompilerRuntimeDirectory $runtime) -Destination (Join-Path $Destination $runtime)
    }
}
if ($ThirdPartyNotice) { Copy-Item -LiteralPath $ThirdPartyNotice -Destination (Join-Path $Destination 'THIRD_PARTY.md') }
if ($ReleaseVersion) {
    if (-not $UpdaterBinary -or -not $BootstrapBinary -or -not $UpdateFeed -or -not $ReleaseSequence) { throw 'Update releases need helper, bootstrap, feed and sequence.' }
    Copy-Item -LiteralPath $UpdaterBinary -Destination (Join-Path $Destination 'cipherchat-updater.exe')
    Copy-Item -LiteralPath $BootstrapBinary -Destination (Join-Path $Destination 'cipherchat-launcher.exe')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rollback-update.cmd') -Destination $Destination
    @{host=$ServerAddress; port=55220; server_name=$ServerAddress} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Destination 'update-config.json') -Encoding utf8NoBOM
    python (Join-Path $PSScriptRoot 'build-update-feed.py') $Destination $UpdateFeed --version $ReleaseVersion --sequence $ReleaseSequence --notes $ReleaseNotes
    if ($LASTEXITCODE -ne 0) { throw 'Failed to generate update feed.' }
}
$files = @(Get-ChildItem -LiteralPath $Destination -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{path=$_.FullName.Substring($Destination.Length+1).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{format=1; built_at=[DateTime]::UtcNow.ToString('o'); configuration='Release'; architecture='x64';
  server=$ServerAddress; port=55220; file_transport='window-cbor-v1'; legacy_file_transfer=$false; files=$files} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding UTF8
Compress-Archive -LiteralPath $Destination -DestinationPath ($Destination + '.zip') -CompressionLevel Optimal
Get-FileHash -LiteralPath ($Destination + '.zip') -Algorithm SHA256
