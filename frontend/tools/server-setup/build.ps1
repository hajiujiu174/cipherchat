$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot '../../artifacts/server-switch-20261020'
New-Item -ItemType Directory -Force -Path $output | Out-Null
& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /platform:anycpu /optimize+ /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll /reference:Microsoft.CSharp.dll "/win32manifest:$(Join-Path $PSScriptRoot 'setup.manifest')" "/out:$(Join-Path $output 'setup.exe')" (Join-Path $PSScriptRoot 'Setup.cs')
if ($LASTEXITCODE -ne 0) { throw '切换工具构建失败' }
Get-FileHash (Join-Path $output 'setup.exe') -Algorithm SHA256
