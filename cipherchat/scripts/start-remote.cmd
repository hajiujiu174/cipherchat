@echo off
setlocal
if not exist "%~dp0cipherchat.exe" (
  echo Please extract the complete CipherChat package first.
  pause
  exit /b 1
)
if not exist "%~dp0server-cert.pem" (
  echo Missing server-cert.pem. Extract the complete package again.
  pause
  exit /b 1
)
start "" "%~dp0cipherchat.exe" --host 2603:1040:f02:d::21d --port 55220 --server-name 2603:1040:f02:d::21d --ca "%~dp0server-cert.pem" --data-dir "%LOCALAPPDATA%\CipherChat\remote-server2"
