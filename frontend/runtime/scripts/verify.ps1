param(
    [ValidateSet('core-debug', 'implementation-debug', 'desktop-debug')]
    [string]$Preset = 'core-debug',
    [switch]$AuditOnly
)
$ErrorActionPreference = 'Stop'
$frameworkRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $frameworkRoot 'tasks.json') -Raw | ConvertFrom-Json
$known = @{}
foreach ($task in $manifest.tasks) {
    if ($known.ContainsKey($task.id)) { throw "重复任务 $($task.id)" }
    $known[$task.id] = $task
    if (-not (Test-Path -LiteralPath (Join-Path $frameworkRoot $task.card))) { throw "缺任务卡 $($task.id)" }
}
foreach ($task in $manifest.tasks) {
    foreach ($dependency in $task.deps) {
        if (-not $known.ContainsKey($dependency)) { throw "未知依赖 $dependency" }
    }
}
# Kahn 拓扑检查，防止模型自行制造循环依赖。
$visited = @{}
while ($visited.Count -lt $known.Count) {
    $before = $visited.Count
    foreach ($task in $manifest.tasks) {
        if (-not $visited.ContainsKey($task.id) -and @($task.deps | Where-Object { -not $visited.ContainsKey($_) }).Count -eq 0) {
            $visited[$task.id] = $true
        }
    }
    if ($visited.Count -eq $before) { throw '任务依赖存在环' }
}
$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $frameworkRoot 'src'),(Join-Path $frameworkRoot 'include') -Recurse -File
$sourceFiles += Get-Item -LiteralPath (Join-Path $frameworkRoot 'gui/business_worker.hpp'),(Join-Path $frameworkRoot 'gui/business_worker.cpp')
foreach ($file in $sourceFiles) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    if ($content -match '#include\s*[<"](?:Q[A-Z]|Qt|server/)') { throw "核心越层包含：$($file.FullName)" }
    foreach ($match in [regex]::Matches($content, 'TODO\[(M\d-\d{2})\]|pending(?:<[^\r\n]*?>)?\("(M\d-\d{2})"\)')) {
        $taskId = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
        if (-not $known.ContainsKey($taskId)) { throw "未登记任务 $taskId" }
    }
}
# Qt selectors belong to the UI adapter, never to reusable acceptance scenarios.
foreach ($scenario in 'tests/acceptance/gui_driver.cpp','tests/acceptance/test_M8_preferences.cpp') {
    $content = Get-Content -LiteralPath (Join-Path $frameworkRoot $scenario) -Raw
    if ($content -match 'findChild|mainTabs|tabText|#include\s*[<"]Q(?:Label|LineEdit|PushButton|TabWidget|ProgressBar|CheckBox)') {
        throw "验收流程依赖具体控件，请移入 tests/ui/qt_client_page.cpp：$scenario"
    }
}
$milestones = @($manifest.tasks | ForEach-Object { $_.milestone } | Sort-Object -Unique)
foreach ($required in 'M0','M1','M2','M3','M4','M5','M6') {
    if ($milestones -notcontains $required) { throw 'M0-M6 覆盖不完整' }
}
if (@($milestones | Where-Object { $_ -notmatch '^M\d+$' }).Count) { throw '里程碑名称无效' }
Write-Output ("框架清单检查通过：{0} 项任务，无循环，核心无 Qt/server 包含。" -f $known.Count)
if ($AuditOnly) { return }
Push-Location $frameworkRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }
    & cmake --build --preset $Preset -j 4
    if ($LASTEXITCODE -ne 0) { throw 'CMake 构建失败' }
    & ctest --preset $Preset --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw 'CTest 失败' }
    Write-Output '框架验证通过；已登记的业务验收之外，其余功能仍按 tasks.json 保持未完成。'
} finally {
    Pop-Location
}
