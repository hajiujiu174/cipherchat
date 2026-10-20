param([string]$QtPrefix = 'D:/msys64/mingw64')
$ErrorActionPreference = 'Stop'
$env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
$buildDirectory = Join-Path $PSScriptRoot '../../work/desktop-launcher-build'
& (Join-Path $QtPrefix 'bin/cmake.exe') -S $PSScriptRoot -B $buildDirectory -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_PREFIX_PATH=$QtPrefix"
if ($LASTEXITCODE -ne 0) { throw '界面选择器配置失败' }
& (Join-Path $QtPrefix 'bin/cmake.exe') --build $buildDirectory -j 2
if ($LASTEXITCODE -ne 0) { throw '界面选择器构建失败' }
Write-Host "构建完成：$buildDirectory/CipherChat-UI-Selector.exe"
