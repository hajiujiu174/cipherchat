param([string]$Id)
$ErrorActionPreference = 'Stop'
$frameworkRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $frameworkRoot 'tasks.json') -Raw | ConvertFrom-Json
$byId = @{}
foreach ($item in $manifest.tasks) { $byId[$item.id] = $item }
if ($Id) {
    if (-not $byId.ContainsKey($Id)) { throw "不存在任务 $Id" }
    $task = $byId[$Id]
} else {
    $task = $manifest.tasks | Where-Object {
        $candidate = $_
        $candidate.status -ne 'done' -and @($candidate.deps | Where-Object { $byId[$_].status -ne 'done' }).Count -eq 0
    } | Select-Object -First 1
}
if (-not $task) { Write-Output '没有前置已完成的待办；查看 tasks.json 中的状态与依赖。'; return }
$unmet = @($task.deps | Where-Object { $byId[$_].status -ne 'done' })
Write-Output ("任务：{0} {1}" -f $task.id, $task.title)
if ($unmet.Count) { Write-Output ("尚未完成的前置：{0}；当前仅可用测试替身独立开发。" -f ($unmet -join ', ')) }
Write-Output '可复制提示词：'
Write-Output ("请在 D:\Work\WORKDONE\cipherchat 完成 framework/tasks/{0}.md。先读 framework/README.md、framework/docs/contracts.md、framework/docs/baseline.md。只在任务卡范围填充和验证，保持其他任务状态。报告文件改动、实际测试结果和未完成项。" -f $task.id)
Write-Output ''
Get-Content -LiteralPath (Join-Path $frameworkRoot $task.card)

