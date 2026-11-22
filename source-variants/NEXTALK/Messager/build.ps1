param(
    [string]$QtPrefix = 'D:/msys64/mingw64',
    [switch]$WithNextalk,
    [switch]$DemoOnly,
    [string]$NextalkSource = (Join-Path $PSScriptRoot '../nextalk')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw '找不到 Qt 6。请通过 -QtPrefix 指定与你的编译器匹配的 Qt 安装目录。'
}
$env:PATH = 'D:\msys64\mingw64\bin;' + (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
$buildDirectory = Join-Path $PSScriptRoot 'work/build'
$WithNextalk = -not $DemoOnly
$arguments = @('-U','Qt6*','-S', $PSScriptRoot, '-B', $buildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtPrefix", '-DBUILD_TESTING=ON',
    '-DCMAKE_CXX_COMPILER=D:/msys64/mingw64/bin/g++.exe',
    "-DMESSAGER_WITH_NEXTALK=$(if ($WithNextalk) {'ON'} else {'OFF'})")
if ($WithNextalk) {
    $resolvedBackend = (Resolve-Path -LiteralPath $NextalkSource).Path
    $arguments += "-DNEXTALK_SOURCE_DIR=$resolvedBackend"
}
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
# Build the default target so every registered CTest executable exists.
& cmake --build $buildDirectory -j 4
if ($LASTEXITCODE -ne 0) { throw '构建失败' }
& ctest --test-dir $buildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "检查未通过，请查看 $buildDirectory 中的 *test-results.txt" }
Write-Host "构建与检查完成：$buildDirectory\Messager.exe"
