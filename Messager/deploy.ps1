param(
    [string]$QtPrefix = 'D:/msys64/mingw64',
    [string]$Destination = (Join-Path $PSScriptRoot 'runtime-v0.3.6'),
    [string]$Certificate = (Join-Path $PSScriptRoot '../cipherchat/artifacts/CipherChat-Remote-Windows-x64-codex-20260929/server-cert.pem')
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $QtPrefix 'bin'
$exe = Join-Path $PSScriptRoot 'work/build/Messager.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw '请先构建 Messager。' }
$env:PATH = "$bin;$env:PATH"
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath $exe -Destination $Destination -Force
& "$bin/windeployqt.exe" --release --no-translations --compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --skip-plugin-types generic,networkinformation,tls,styles --dir $Destination "$Destination/Messager.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt 运行库部署失败。' }
# Follow PE imports recursively, including OpenSSL, SQLite and font dependencies.
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $Destination -Recurse -File | Where-Object Extension -In '.exe','.dll' | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$records = @()
while ($queue.Count) {
    $file = $queue.Dequeue()
    if (-not $seen.Add($file)) { continue }
    $imports = & "$bin/objdump.exe" -p $file | Select-String 'DLL Name:' | ForEach-Object { ($_.Line -split 'DLL Name:')[1].Trim() }
    foreach ($dependency in $imports) {
        $local = Join-Path $Destination $dependency
        $source = Join-Path $bin $dependency
        if (Test-Path -LiteralPath $local) { $queue.Enqueue($local); continue }
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination $local
            $queue.Enqueue($local)
            continue
        }
        if ($dependency -match '^(api-ms-|ext-ms-)' -or (Test-Path -LiteralPath (Join-Path "$env:WINDIR/System32" $dependency))) { continue }
        throw "未解析的运行库依赖：$dependency，来源 $file"
    }
    $records += [ordered]@{ binary = [IO.Path]::GetRelativePath($Destination, $file); imports = @($imports) }
}
Set-Content -LiteralPath "$Destination/qt.conf" -Value "[Paths]`nPlugins=." -Encoding ascii
if (Test-Path -LiteralPath $Certificate) {
    Copy-Item -LiteralPath $Certificate -Destination "$Destination/server-cert.pem" -Force
    [ordered]@{ host='2603:1040:f02:d::21d'; port=55220; serverName='2603:1040:f02:d::21d'; directEnabled=$false } | ConvertTo-Json | Set-Content -LiteralPath "$Destination/connection.json" -Encoding utf8NoBOM
}
$licenses = Join-Path $Destination 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
foreach ($name in @('qt6-base','gcc-libs','winpthreads','glib2','gettext-runtime','libiconv','icu','harfbuzz','freetype','bzip2','libpng','zlib','brotli','pcre2','double-conversion','md4c','graphite2','libb2','zstd','libjpeg-turbo','openssl','sqlite3')) {
    $source = Join-Path $QtPrefix "share/licenses/$name"
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $licenses -Recurse -Force }
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'evidence/dependencies-v0.3.6.json') -Encoding utf8NoBOM
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'docs/THIRD_PARTY_NOTICES.txt') -Destination $licenses -Force
Write-Host "运行库部署完成：$Destination"
