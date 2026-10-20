[CmdletBinding()]
param(
    [ValidateRange(1,65535)][int]$Port = 55220,
    [string]$DataDirectory = (Join-Path $env:LOCALAPPDATA 'CipherChat\demo'),
    [string]$BinaryDirectory = '',
    [switch]$ThirdClient
)
$ErrorActionPreference = 'Stop'
if (-not $BinaryDirectory) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'cipherchat.exe')) { $BinaryDirectory = $PSScriptRoot }
    else { $BinaryDirectory = Join-Path (Split-Path $PSScriptRoot) 'artifacts\CipherChat-Windows-x64' }
}
$BinaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
foreach ($exe in 'cipherchat.exe','cipherchat-server.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $BinaryDirectory $exe))) { throw "Missing $exe in $BinaryDirectory" }
}
[IO.Directory]::CreateDirectory($DataDirectory) | Out-Null
$DataDirectory = (Resolve-Path -LiteralPath $DataDirectory).Path
$serverDirectory = Join-Path $DataDirectory 'server'
[IO.Directory]::CreateDirectory($serverDirectory) | Out-Null
# Reserve once to reject an occupied port; the server checks bind again after release.
$probe = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any, $Port)
try { $probe.Start() } finally { $probe.Stop() }
$clients = @()
$server = $null
try {
    $runId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
    $server = Start-Process -FilePath (Join-Path $BinaryDirectory 'cipherchat-server.exe') -ArgumentList @($Port,'server.db','cert.pem','key.pem') -WorkingDirectory $serverDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $serverDirectory "$runId.stdout.log") -RedirectStandardError (Join-Path $serverDirectory "$runId.stderr.log")
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    $ready = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $server.HasExited) {
        if (Test-Path -LiteralPath (Join-Path $serverDirectory 'cert.pem')) {
            $socket = New-Object Net.Sockets.TcpClient
            try { $socket.Connect('127.0.0.1',$Port); $ready = $socket.Connected } catch {} finally { $socket.Dispose() }
            if ($ready) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if (-not $ready -or $server.HasExited) { throw "Server failed to start. See logs in $serverDirectory" }
    $names = @('alice','bob')
    if ($ThirdClient) { $names += 'carol' }
    foreach ($name in $names) {
        $profileRoot = Join-Path $DataDirectory $name
        [IO.Directory]::CreateDirectory($profileRoot) | Out-Null
        $arguments = @('--host','127.0.0.1','--port', $Port, '--server-name','localhost','--ca', ('"'+(Join-Path $serverDirectory 'cert.pem')+'"'),'--data-dir',('"'+$profileRoot+'"'),'--username',$name)
        $clients += Start-Process -FilePath (Join-Path $BinaryDirectory 'cipherchat.exe') -ArgumentList $arguments -WorkingDirectory $BinaryDirectory -WindowStyle Normal -PassThru
    }
    Write-Host "CipherChat demo ready at 127.0.0.1:$Port. Enter passwords in the windows."
    Write-Host "Data: $DataDirectory. Close every client window to stop this demo server."
    while (@($clients | Where-Object { -not $_.HasExited }).Count -gt 0) {
        if ($server.HasExited) { throw 'Demo server exited unexpectedly.' }
        Start-Sleep -Milliseconds 200
    }
} finally {
    foreach ($client in $clients) { if (-not $client.HasExited) { $client.CloseMainWindow() | Out-Null } }
    if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id }
}
