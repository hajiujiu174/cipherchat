param(
    [string]$QtPrefix = 'D:/msys64/mingw64',
    [switch]$WithCipherChat,
    [switch]$DemoOnly,
    [string]$CipherChatSource = (Join-Path $PSScriptRoot '../cipherchat')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw '找不到 Qt 6。请通过 -QtPrefix 指定与你的编译器匹配的 Qt 安装目录。'
}
$env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
$buildDirectory = Join-Path $PSScriptRoot 'work/build'
$WithCipherChat = -not $DemoOnly
$arguments = @('-S', $PSScriptRoot, '-B', $buildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$QtPrefix", '-DBUILD_TESTING=ON',
    "-DMESSAGER_WITH_CIPHERCHAT=$(if ($WithCipherChat) {'ON'} else {'OFF'})")
if ($WithCipherChat) {
    $resolvedBackend = (Resolve-Path -LiteralPath $CipherChatSource).Path
    $arguments += "-DCIPHERCHAT_SOURCE_DIR=$resolvedBackend"
}
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
$targets = @('Messager', 'messager_tests')
if ($WithCipherChat) { $targets += @('cipherchat_adapter_tests', 'messager_live_tests') }
& cmake --build $buildDirectory --target @targets -j 4
if ($LASTEXITCODE -ne 0) { throw '构建失败' }
& ctest --test-dir $buildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "检查未通过，请查看 $buildDirectory 中的 *test-results.txt" }
Write-Host "构建与检查完成：$buildDirectory\Messager.exe"
