param(
    [ValidateSet('All','Frontend','Backend')][string]$Target = 'All',
    [string]$Toolchain = 'D:/msys64/mingw64',
    [ValidateRange(1,24)][int]$Jobs = 6,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $Toolchain 'bin'
$compiler = Join-Path $bin 'g++.exe'
$ninja = Join-Path $bin 'ninja.exe'
foreach ($file in @($compiler, $ninja)) {
    if (!(Test-Path -LiteralPath $file)) { throw "Missing build dependency: $file" }
}
$env:PATH = "$bin;$env:PATH"
$sides = if ($Target -eq 'All') { @('frontend','backend') } else { @($Target.ToLowerInvariant()) }
$testing = if ($Test) { 'ON' } else { 'OFF' }

foreach ($side in $sides) {
    $source = Join-Path $root $side
    $build = Join-Path $root "build/$side"
    $flags = @('-S', $source, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_PREFIX_PATH=$Toolchain",
        "-DBUILD_TESTING=$testing")
    if ($side -eq 'backend') { $flags += "-DCMAKE_C_COMPILER=$(Join-Path $bin 'gcc.exe')" }
    & cmake @flags
    if ($LASTEXITCODE -ne 0) { throw "Configuration failed: $side" }

    $flags = @('--build', $build, '--parallel', $Jobs)
    if (!$Test) { $flags += @('--target', $(if ($side -eq 'frontend') { 'Messager' } else { 'nextalk-server' })) }
    & cmake @flags
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $side" }
    if ($Test) {
        & ctest --test-dir $build --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed: $side" }
    }
}
